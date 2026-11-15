/*
    SQLTools is a tool for Oracle database developers and DBAs.
    Copyright (C) 1997-2026 Aleksey Kochetov

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "stdafx.h"

// Keith Rule's CMemDC, wrapped in a namespace exactly as OEView_2.cpp wraps it: the
// header declares CMemDC at global scope, and MFC has its own CMemDC in afxwin.h
// with a different constructor, so the two have to be kept apart by name.
namespace MyMemDC
{
#include "MemDC.h"
};

#include "Common/SnippetListWnd.h"

#include <COMMON/AppUtilities.h>    // AppSetClipboardText

#include <afxole.h>                 // COleDataSource
#include <algorithm>
#include <cmath>                    // tan(), for the chevron's apex depth

// The card border and the chevron are a curve and two diagonals, and plain GDI
// smooths neither, so both came out stepped at every dpi. GDI+ is already up for
// the whole process - GdiplusStartup() runs in App\SQLTools.cpp and Gdiplus.lib is
// #pragma comment linked there - so this adds the header and nothing else. No file
// scope `using namespace Gdiplus`: the names it brings in (Pen, Color, Point,
// Path) collide too easily in a file that pulls in this many headers, so the paint
// functions open the namespace locally. Same choice, same reason, as
// PropTreeSearchBar.cpp and the tree's expand chevron in PropTreeItem.cpp.
#include <gdiplus.h>

namespace Common {

namespace {

// The palette. Literal RGB for the accents and GetSysColor() for the background
// and the text, which is the split CPropTreeSearchBar uses: a card that is the
// same gray as the surface it sits on does not read as a card, but the pane should
// still follow the user's colour scheme where it matters. The app is not dark-mode
// aware and neither is this control - deliberately, and like every other custom
// surface in it.
const COLORREF CLR_ACCENT      = RGB(0x00, 0x78, 0xD7);  // selected, focused: border
const COLORREF CLR_CARD        = RGB(0xFF, 0xFF, 0xFF);  // card fill at rest
const COLORREF CLR_BORDER      = RGB(0xD5, 0xD5, 0xD5);  // card border at rest
const COLORREF CLR_HOT_FILL    = RGB(0xF2, 0xF7, 0xFD);  // card under the pointer
const COLORREF CLR_HOT_BORDER  = RGB(0x9E, 0xC6, 0xEE);
const COLORREF CLR_SEL_FILL    = RGB(0xE5, 0xF0, 0xFB);  // selected, window focused
const COLORREF CLR_HEADER      = RGB(0x1A, 0x1A, 0x1A);
const COLORREF CLR_BADGE       = RGB(0x6A, 0x6A, 0x6A);  // the keyword, right-aligned
const COLORREF CLR_BADGE_REQ   = RGB(0x1A, 0x1A, 0x1A);  // ... its required part, as the header
const COLORREF CLR_BODY        = RGB(0x33, 0x33, 0x33);
const COLORREF CLR_GROUP       = RGB(0x60, 0x60, 0x60);  // section caption
const COLORREF CLR_CHEVRON_HOT = RGB(0x00, 0x78, 0xD7); // ... and under the pointer

// The muted set, for a card the pane shows but has marked as not in play.
const COLORREF CLR_HEADER_DIM    = RGB(0x8A, 0x8A, 0x8A);
const COLORREF CLR_BADGE_DIM     = RGB(0xA8, 0xA8, 0xA8);
const COLORREF CLR_BADGE_REQ_DIM = RGB(0x6A, 0x6A, 0x6A);
const COLORREF CLR_BODY_DIM      = RGB(0x9A, 0x9A, 0x9A);

// Length of the "{code}" marker the editor uses to decide to re-indent what it is
// given. strlen, not sizeof: sizeof counts the terminator, and the editor's call
// sites pass it, over-reading the literal by one byte. It happens to work because
// the marker is matched by prefix. Not repeated here, and the existing sites are
// not touched by this file.
const int CODE_MARKER_LEN = 6;

// Allocates a moveable HGLOBAL holding cch characters plus a terminator - the
// shape both a clipboard and an OLE text payload want. Returns NULL on failure and
// leaves nothing allocated, so a caller has no partial state to undo.
HGLOBAL AllocGlobalW (const wchar_t* p, size_t cch)
{
    const SIZE_T cb = cch * sizeof(wchar_t);

    HGLOBAL hGlobal = ::GlobalAlloc(GMEM_MOVEABLE, cb + sizeof(wchar_t));
    if (!hGlobal)
        return NULL;

    void* pDst = ::GlobalLock(hGlobal);
    if (!pDst)
    {
        ::GlobalFree(hGlobal);
        return NULL;
    }

    if (cb)
        memcpy(pDst, p, cb);
    memset((BYTE*)pDst + cb, 0, sizeof(wchar_t));

    ::GlobalUnlock(hGlobal);
    return hGlobal;
}

// The ANSI twin, for the marker only: the editor reads CF_PRIVATEFIRST into a
// std::string, so it is written the way CTreeViewer::OnEditCopy and
// CodeOutlineWnd::OnContextMenu write it.
HGLOBAL AllocGlobalA (const char* p, size_t cch)
{
    HGLOBAL hGlobal = ::GlobalAlloc(GMEM_MOVEABLE, cch + 1);
    if (!hGlobal)
        return NULL;

    char* pDst = (char*)::GlobalLock(hGlobal);
    if (!pDst)
    {
        ::GlobalFree(hGlobal);
        return NULL;
    }

    if (cch)
        memcpy(pDst, p, cch);
    pDst[cch] = '\0';

    ::GlobalUnlock(hGlobal);
    return hGlobal;
}

// The keys the list owns, i.e. the ones PreTranslateMessage has to claim before the
// frame's IsDialogMessage() turns them into pane navigation. VK_TAB is deliberately
// absent: tabbing out of the pane has to keep working.
bool IsListKey (UINT nChar)
{
    switch (nChar)
    {
    case VK_UP:
    case VK_DOWN:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_RETURN:
    case VK_SPACE:
        return true;
    }

    return false;
}

} // unnamed namespace

////////////////////////////////////////////////////////////////////////////////
// Construction

CSnippetListWnd::CSnippetListWnd ()
: m_pHeaderFont(NULL)
, m_pBodyFont(NULL)
, m_hBoldBase(NULL)
, m_cyHeaderLine(16)
, m_cyBodyLine(16)
, m_cyGroupLine(16)
, m_nScrollPos(0)
, m_nContentHeight(0)
, m_nHoverRow(-1)
, m_nSelRow(-1)
, m_bTrackingMouse(false)
, m_bDragArmed(false)
, m_nDragRow(-1)
, m_ptDragAnchor(0, 0)
{
}

CSnippetListWnd::~CSnippetListWnd ()
{
}

BOOL CSnippetListWnd::PreCreateWindow (CREATESTRUCT& cs)
{
    // CS_DBLCLKS, or WM_LBUTTONDBLCLK never arrives and "insert at the caret"
    // would have to be a modifier key alone. No background brush: OnEraseBkgnd
    // answers TRUE and OnPaint draws everything, so a brush would only be a second
    // thing to keep in step with the painted background.
    static LPCTSTR pszClass = AfxRegisterWndClass(
        CS_DBLCLKS,
        ::LoadCursor(NULL, IDC_ARROW),
        NULL,
        NULL);

    cs.lpszClass = pszClass;
    cs.style |= WS_CLIPCHILDREN | WS_CLIPSIBLINGS;

    return CWnd::PreCreateWindow(cs);
}

BOOL CSnippetListWnd::Create (const CRect& rc, CWnd* pParentWnd, UINT nID)
{
    // WS_TABSTOP so the pane can be reached from the keyboard at all; WS_VSCROLL
    // because the bar is always created and only shown when the content needs it.
    if (!CWnd::Create(NULL, NULL,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP,
            rc, pParentWnd, nID))
        return FALSE;

    CreateFallbackFonts();
    RecalcMetrics();
    return TRUE;
}

BEGIN_MESSAGE_MAP(CSnippetListWnd, CWnd)
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
    ON_WM_VSCROLL()
    ON_WM_MOUSEWHEEL()
    ON_WM_MOUSEMOVE()
    ON_WM_MOUSELEAVE()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_RBUTTONUP()
    ON_WM_KEYDOWN()
    ON_WM_GETDLGCODE()
    ON_WM_SETFOCUS()
    ON_WM_KILLFOCUS()
    ON_WM_DESTROY()
END_MESSAGE_MAP()

int CSnippetListWnd::ScaleMetric (int nAt96dpi)
{
    // Off the screen's logical DPI, as CPropTreeSearchBar::ScaleMetric does. The
    // app has no per-monitor dpi helper of its own and these panes are sized by the
    // docking framework, so the screen scale is the scale the control is drawn at;
    // at 150% every constant here has to grow with the font or the cards clip their
    // own text.
    HDC hScreen = ::GetDC(NULL);

    if (!hScreen)
        return nAt96dpi;

    const int nDpiY = ::GetDeviceCaps(hScreen, LOGPIXELSY);
    ::ReleaseDC(NULL, hScreen);

    return MulDiv(nAt96dpi, (nDpiY > 0) ? nDpiY : 96, 96);
}

////////////////////////////////////////////////////////////////////////////////
// Fonts

void CSnippetListWnd::CreateFallbackFonts ()
{
    if (m_headerFontOwn.GetSafeHandle() && m_bodyFontOwn.GetSafeHandle())
        return;

    // A pane is expected to hand over the editor's own fonts. These only keep the
    // control legible when it is used before one does, or with none at all: the
    // system message font bolded for headers, and a fixed-pitch face for bodies,
    // because a snippet in a proportional font is not readable as code.
    if (!m_headerFontOwn.GetSafeHandle())
    {
        NONCLIENTMETRICS ncm;
        memset(&ncm, 0, sizeof(ncm));
        ncm.cbSize = sizeof(ncm);
        ::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);

        LOGFONT lf = ncm.lfMessageFont;
        lf.lfWeight = FW_BOLD;
        m_headerFontOwn.CreateFontIndirect(&lf);
    }

    if (!m_bodyFontOwn.GetSafeHandle())
    {
        // CreatePointFont rather than a raw lfHeight: it converts points to pixels
        // at the DC's own dpi, so the fallback scales the way a real font does.
        m_bodyFontOwn.CreatePointFont(90, _T("Consolas"), NULL);
    }
}

void CSnippetListWnd::SetHeaderFont (CFont* pFont)
{
    if (m_pHeaderFont == pFont)
        return;

    m_pHeaderFont = pFont;
    RecalcMetrics();
    LayoutRows();
    UpdateScrollBars();
}

void CSnippetListWnd::SetBodyFont (CFont* pFont)
{
    if (m_pBodyFont == pFont)
        return;

    m_pBodyFont = pFont;
    RecalcMetrics();
    LayoutRows();
    UpdateScrollBars();
}

CFont* CSnippetListWnd::HeaderFont ()
{
    if (!m_pHeaderFont)
        CreateFallbackFonts();

    CFont* pFont = m_pHeaderFont ? m_pHeaderFont : &m_headerFontOwn;

    // A last resort, because CDC::SelectObject asserts on a CFont with no handle and
    // CreateFontIndirect can fail - a face the system does not have, or a GDI handle
    // table that is full. The stock GUI font is always there, and a card drawn in it is
    // uglier than a card that is not drawn at all.
    if (!pFont || !pFont->GetSafeHandle())
        pFont = CFont::FromHandle((HFONT)::GetStockObject(DEFAULT_GUI_FONT));

    return pFont;
}

CFont* CSnippetListWnd::BoldHeaderFont ()
{
    CFont* pBase = HeaderFont();
    const HGDIOBJ hBase = pBase->GetSafeHandle();

    // Built on demand and keyed on the base handle, so a pane calling SetHeaderFont() later
    // is picked up without every paint paying for a CreateFontIndirect it does not need.
    if (m_boldHeaderFont.GetSafeHandle() && m_hBoldBase == hBase)
        return &m_boldHeaderFont;

    LOGFONT lf;
    memset(&lf, 0, sizeof(lf));

    if (pBase->GetLogFont(&lf))
    {
        lf.lfWeight = FW_BOLD;

        m_boldHeaderFont.DeleteObject();
        m_boldHeaderFont.CreateFontIndirect(&lf);
    }

    m_hBoldBase = hBase;

    // As HeaderFont(): never hand a caller a CFont with no handle in it. Should the bold
    // face fail to build, the required part simply draws in the base font instead.
    if (!m_boldHeaderFont.GetSafeHandle())
        return pBase;

    return &m_boldHeaderFont;
}

CFont* CSnippetListWnd::BodyFont ()
{
    if (!m_pBodyFont)
        CreateFallbackFonts();

    CFont* pFont = m_pBodyFont ? m_pBodyFont : &m_bodyFontOwn;

    // As HeaderFont(): never hand a caller a CFont with no handle in it.
    if (!pFont || !pFont->GetSafeHandle())
        pFont = CFont::FromHandle((HFONT)::GetStockObject(DEFAULT_GUI_FONT));

    return pFont;
}

namespace {

// A font's line height in pixels, read off the screen DC.
//
// The screen and not a client DC because SetItems() and Set*Font() both run before
// the pane has created this window in the normal order of work, and because a
// font's pixel height is a property of the dpi it is realised at - which for this
// app is the screen's, exactly as ScaleMetric() assumes.
int FontLineHeight (CFont* pFont, int nFallback)
{
    HDC hScreen = ::GetDC(NULL);

    if (!hScreen)
        return nFallback;

    if (!pFont || !pFont->GetSafeHandle())
    {
        ::ReleaseDC(NULL, hScreen);
        return nFallback;
    }

    CDC dc;
    dc.Attach(hScreen);

    CFont* pOld = dc.SelectObject(pFont);
    TEXTMETRIC tm;
    memset(&tm, 0, sizeof(tm));
    dc.GetTextMetrics(&tm);
    dc.SelectObject(pOld);

    ::ReleaseDC(NULL, dc.Detach());

    const int cy = tm.tmHeight + tm.tmExternalLeading;
    return cy > 0 ? cy : nFallback;
}

// Lines in a body. A trailing newline does not add an empty line: template text
// routinely ends with one, and drawing a blank last line would put an empty row
// and its padding at the bottom of every open card.
int CountBodyLines (const std::wstring& s)
{
    if (s.empty())
        return 0;

    int nLines = 1;
    for (size_t i = 0; i < s.length(); i++)
        if (s[i] == L'\n')
            nLines++;

    const wchar_t chLast = s[s.length() - 1];
    if (chLast == L'\n' || chLast == L'\r')
        nLines--;

    return nLines > 0 ? nLines : 1;
}

} // unnamed namespace

void CSnippetListWnd::RecalcMetrics ()
{
    m_cyHeaderLine = FontLineHeight(HeaderFont(), ScaleMetric(16));
    m_cyBodyLine   = FontLineHeight(BodyFont(),   ScaleMetric(16));
    m_cyGroupLine  = FontLineHeight(HeaderFont(), ScaleMetric(16));
}

////////////////////////////////////////////////////////////////////////////////
// Content

void CSnippetListWnd::SetItems (const std::vector<SnippetItem>& items)
{
    m_vecItems = items;

    // Expansion goes with the old list: a card that was open has no meaning under
    // the new one, and keeping the flags would open whatever now happens to sit at
    // the same row.
    m_nHoverRow   = -1;
    m_nSelRow     = -1;
    m_nScrollPos  = 0;
    m_bDragArmed  = false;
    m_nDragRow    = -1;

    RebuildRows();
    RecalcMetrics();
    LayoutRows();
    UpdateScrollBars();
}

void CSnippetListWnd::RemoveAllItems ()
{
    static const std::vector<SnippetItem> empty;
    SetItems(empty);
}

void CSnippetListWnd::RebuildRows ()
{
    m_vecRows.clear();
    m_vecRows.reserve(m_vecItems.size() + 8);

    const SnippetItem* pPrev = NULL;

    for (size_t i = 0; i < m_vecItems.size(); i++)
    {
        const SnippetItem& item = m_vecItems[i];

        // A caption row wherever the group changes, which is why the caller has to
        // keep the items of one group together: a second run of the same caption
        // would get a second header.
        if (!item.group.empty() && (!pPrev || pPrev->group != item.group))
        {
            Row rowGroup;
            rowGroup.item     = -1;
            rowGroup.text     = item.group.c_str();
            rowGroup.y        = 0;
            rowGroup.height   = 0;
            rowGroup.expanded = false;
            m_vecRows.push_back(rowGroup);
        }

        Row row;
        row.item     = (int)i;
        row.y        = 0;
        row.height   = 0;
        row.expanded = false;
        m_vecRows.push_back(row);

        pPrev = &item;
    }
}

////////////////////////////////////////////////////////////////////////////////
// Layout

int CSnippetListWnd::RowHeight (const Row& row) const
{
    if (!row.IsCard())
        return m_cyGroupLine + 2 * ScaleMetric(GROUP_PAD);

    const int pad = ScaleMetric(CARD_PAD);

    // Nothing wraps, so this does not depend on the width - which is what lets a
    // resize skip LayoutRows() entirely and keeps the hit-test agreeing with the
    // pixels afterwards.
    int cy = 2 * pad + m_cyHeaderLine;

    if (row.expanded && row.item >= 0 && row.item < (int)m_vecItems.size())
    {
        const int nLines = CountBodyLines(m_vecItems[row.item].body);
        if (nLines > 0)
            cy += ScaleMetric(BODY_GAP) + nLines * m_cyBodyLine;
    }

    return cy;
}

void CSnippetListWnd::LayoutRows ()
{
    const int nMargin = ScaleMetric(CARD_MARGIN);
    const int nGap    = ScaleMetric(CARD_GAP);

    // Running y, i.e. prefix sums. RowFromY() binary searches this, so a list of
    // 5000 snippets hit-tests in log n and a frame only ever touches the rows that
    // intersect the client rect.
    int y = nMargin;

    for (size_t i = 0; i < m_vecRows.size(); i++)
    {
        Row& row = m_vecRows[i];

        row.height = RowHeight(row);
        row.y      = y;
        y         += row.height + nGap;
    }

    if (m_vecRows.empty())
        m_nContentHeight = 0;
    else
    {
        const Row& last = m_vecRows.back();
        m_nContentHeight = last.y + last.height + nMargin;
    }
}

int CSnippetListWnd::RowFromY (int yContent) const
{
    if (m_vecRows.empty())
        return -1;

    // The rows are laid out in increasing y and never overlap, so the tops are
    // sorted and the row wanted is the last one whose top is at or above yContent.
    int lo = 0, hi = (int)m_vecRows.size() - 1, nFound = -1;

    while (lo <= hi)
    {
        const int mid = lo + (hi - lo) / 2;

        if (m_vecRows[mid].y <= yContent)
        {
            nFound = mid;
            lo = mid + 1;
        }
        else
            hi = mid - 1;
    }

    if (nFound < 0)
        return -1;

    // The gap between two cards belongs to neither. Reporting the card above would
    // make a click in the gutter toggle it, which reads as the control ignoring
    // where the pointer actually was.
    const Row& row = m_vecRows.at(nFound);
    if (yContent >= row.y + row.height)
        return -1;

    return nFound;
}

int CSnippetListWnd::RowFromItem (int item) const
{
    if (item < 0)
        return -1;

    // A scan rather than a cached item->row map: this runs on selection changes and
    // key presses, never per frame, and the list is hundreds of rows at most. A map
    // would be a third container to keep in step with SetItems().
    for (size_t i = 0; i < m_vecRows.size(); i++)
        if (m_vecRows[i].item == item)
            return (int)i;

    return -1;
}

CRect CSnippetListWnd::RowRect (int row) const
{
    // Initialised before the call, and the call skipped when there is no window yet:
    // GetClientRect() leaves the rect untouched on failure, and RowRect() is reachable
    // from HitTest(), which is public and can be asked before Create().
    CRect rcClient(0, 0, 0, 0);

    if (row < 0 || row >= (int)m_vecRows.size())
        return rcClient;

    if (GetSafeHwnd())
        GetClientRect(&rcClient);

    const Row& r = m_vecRows[row];
    const int y = r.y - m_nScrollPos;

    return CRect(0, y, rcClient.Width(), y + r.height);
}

CRect CSnippetListWnd::CardRect (int row) const
{
    CRect rc = RowRect(row);

    if (rc.IsRectEmpty())
        return rc;

    // Horizontally inset only. Vertically the row IS the card: LayoutRows() put the
    // CARD_GAP between the rows, so deflating here as well would double it and leave
    // the painted border short of the row the hit-test reports.
    rc.DeflateRect(ScaleMetric(CARD_MARGIN), 0);
    return rc;
}

SnippetHit CSnippetListWnd::HitTest (CPoint ptClient) const
{
    SnippetHit hit;

    const int row = RowFromY(ptClient.y + m_nScrollPos);
    if (row < 0 || !m_vecRows.at(row).IsCard())
        return hit;

    const CRect rcCard = CardRect(row);
    if (!rcCard.PtInRect(ptClient))
        return hit;

    hit.item = m_vecRows.at(row).item;

    const int yHeaderBottom = rcCard.top + ScaleMetric(CARD_PAD) + m_cyHeaderLine;

    // The chevron column and the header line both toggle; everything below the
    // header only selects and arms a drag. Reading an open snippet should not be
    // able to close it by accident.
    if (ptClient.y < yHeaderBottom)
    {
        const int xGlyph = rcCard.left + ScaleMetric(CARD_PAD) + ScaleMetric(CHEVRON_COL);
        hit.part = (ptClient.x < xGlyph) ? SNP_CHEVRON : SNP_HEADER;
    }
    else
        hit.part = SNP_BODY;

    return hit;
}

void CSnippetListWnd::UpdateScrollBars ()
{
    if (!GetSafeHwnd())
        return;

    CRect rc;
    GetClientRect(&rc);

    const int nPage   = rc.Height();
    const int nMaxPos = max(0, m_nContentHeight - nPage);

    // Clamped before the bar is told about it, not after: SetScrollInfo clips
    // SIF_POS to the range it is given, so a list that has just shrunk has to get
    // its new range and its new position in one call or the view is left scrolled
    // past the end of the content, showing nothing.
    m_nScrollPos = max(0, min(m_nScrollPos, nMaxPos));

    SCROLLINFO si;
    memset(&si, 0, sizeof(si));
    si.cbSize = sizeof(si);
    si.fMask  = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin   = 0;
    si.nMax   = max(0, m_nContentHeight - 1);
    si.nPage  = (UINT)max(0, nPage);
    si.nPos   = m_nScrollPos;

    // bRedraw TRUE, not FALSE. The bar is non-client area: it is painted on WM_NCPAINT
    // and the Invalidate() below reaches only the client, so a position set without a
    // redraw is stored correctly and never drawn - the list scrolls under a thumb that
    // does not move. ShowScrollBar() masks this when it actually toggles, since that
    // repaints the frame, which is why the bar looks right after a resize and wrong
    // after a wheel notch.
    SetScrollInfo(SB_VERT, &si, TRUE);

    // Hidden rather than shown disabled when everything fits: a pane this narrow
    // loses real width to the bar, and a grayed-out bar still takes it.
    ShowScrollBar(SB_VERT, m_nContentHeight > nPage ? TRUE : FALSE);

    Invalidate(FALSE);
}

void CSnippetListWnd::ScrollTo (int pos)
{
    // Guarded because SetScrollOffset() is public and can be called before Create():
    // GetClientRect() on a window that does not exist would leave the rect holding
    // whatever was on the stack, and the clamp below would then use it.
    if (!GetSafeHwnd())
        return;

    CRect rc;
    GetClientRect(&rc);

    pos = max(0, min(pos, max(0, m_nContentHeight - rc.Height())));

    if (pos == m_nScrollPos)
        return;

    m_nScrollPos = pos;
    UpdateScrollBars();
}

////////////////////////////////////////////////////////////////////////////////
// Painting

namespace {

// A rounded rect, filled and stroked.
//
// GDI+ because GDI's RoundRect has no smoothing and its corners go visibly
// polygonal above 125% - the same reason the search bar's field and the tree's
// chevron are GDI+.
//
// The rect is inset by half a pixel and shrunk by one so the 1px stroke lands
// inside the card instead of straddling its edge and bleeding into the gap the
// layout counted as empty. The diameter is clamped to the shorter side: GDI+ turns
// an arc wider than the rect it is in into a shape that does not close, and a card
// narrower than 2 * radius is a real case in a docked pane being dragged thin.
void FillRoundRect (Gdiplus::Graphics& g, const CRect& rc, int radius,
                    COLORREF crFill, COLORREF crBorder)
{
    using namespace Gdiplus;

    RectF r((REAL)rc.left + 0.5f, (REAL)rc.top + 0.5f,
            (REAL)rc.Width() - 1.0f, (REAL)rc.Height() - 1.0f);

    if (r.Width <= 0.0f || r.Height <= 0.0f)
        return;

    REAL d = (REAL)(radius * 2);                    // GDI+ arcs take a DIAMETER
    if (d > r.Width)  d = r.Width;
    if (d > r.Height) d = r.Height;

    GraphicsPath path;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path.CloseFigure();

    g.SetSmoothingMode(SmoothingModeAntiAlias);

    SolidBrush brush(Color(GetRValue(crFill), GetGValue(crFill), GetBValue(crFill)));
    g.FillPath(&brush, &path);

    Pen pen(Color(GetRValue(crBorder), GetGValue(crBorder), GetBValue(crBorder)), 1.0f);
    g.DrawPath(&pen, &path);
}

} // unnamed namespace

BOOL CSnippetListWnd::OnEraseBkgnd (CDC* pDC)
{
    UNREFERENCED_PARAMETER(pDC);

    // OnPaint draws the whole client, background included. Erasing first would be a
    // second full-size fill per frame, which is exactly the flash the double buffer
    // below exists to remove.
    return TRUE;
}

void CSnippetListWnd::OnPaint ()
{
    CPaintDC dc(this);

    CRect rcClient;
    GetClientRect(&rcClient);

    if (rcClient.IsRectEmpty())
        return;

    // Over the whole client rather than the clip box: MyMemDC::CMemDC sets its
    // window origin to the top-left of the rect it is given, so painting into a
    // clip box that does not start at 0,0 would need every coordinate offset by
    // hand. The blit back is still clipped by the CPaintDC's region, so a hover
    // repaint of one card only reaches the screen for that card.
    MyMemDC::CMemDC memDC(&dc, &rcClient);

    memDC.SetBkMode(TRANSPARENT);
    memDC.FillSolidRect(&rcClient, ::GetSysColor(COLOR_BTNFACE));

    PaintRows(memDC, rcClient);
}

void CSnippetListWnd::PaintRows (CDC& dc, const CRect& rcClip)
{
    if (m_vecRows.empty())
        return;

    // First row that reaches into the view. RowFromY() answers -1 for a point in
    // the gap between two cards, which is exactly where a scroll position often
    // lands, so this searches for the first row whose BOTTOM is past the top of the
    // view: one binary search over the same prefix sums, and no special case for
    // the gaps.
    int lo = 0, hi = (int)m_vecRows.size() - 1;
    int row = (int)m_vecRows.size();

    while (lo <= hi)
    {
        const int mid = lo + (hi - lo) / 2;

        if (m_vecRows[mid].y + m_vecRows[mid].height > m_nScrollPos)
        {
            row = mid;
            hi = mid - 1;
        }
        else
            lo = mid + 1;
    }

    if (row >= (int)m_vecRows.size())
        return;

    // One Graphics for the frame, not one per card: constructing it is not free and
    // a pane shows a dozen cards at a time.
    Gdiplus::Graphics g(dc.GetSafeHdc());

    for (; row < (int)m_vecRows.size(); row++)
    {
        const CRect rcRow = RowRect(row);

        if (rcRow.top >= rcClip.bottom)
            break;

        if (m_vecRows[row].IsCard())
            PaintCard(g, dc, row, CardRect(row));
        else
            PaintGroup(g, dc, m_vecRows[row], rcRow);
    }
}

void CSnippetListWnd::PaintGroup (Gdiplus::Graphics& g, CDC& dc, const Row& row, const CRect& rc)
{
    UNREFERENCED_PARAMETER(g);   // a caption is text only: nothing here to smooth

    CRect rcText = rc;
    rcText.DeflateRect(ScaleMetric(CARD_MARGIN) + ScaleMetric(GROUP_INDENT), ScaleMetric(GROUP_PAD));

    if (rcText.IsRectEmpty())
        return;

    CFont* pOld = dc.SelectObject(HeaderFont());

    dc.SetTextColor(CLR_GROUP);
    dc.DrawText(row.text, row.text.GetLength(), &rcText,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    dc.SelectObject(pOld);
}

void CSnippetListWnd::DrawChevron (Gdiplus::Graphics& g, const CRect& rcGlyph, bool expanded, bool hot)
{
    using namespace Gdiplus;

    if (rcGlyph.Width() < 4 || rcGlyph.Height() < 4)
        return;

    // The glyph _DrawExpand() draws in ThirdParty\PropTree\PropTreeItem.cpp, which is
    // what every settings page's group row uses, copied constant for constant so the
    // two surfaces read as the same control. PropTree's box is 12 px in a 16 px column
    // (PROPTREEITEM_EXPANDBOX / PROPTREEITEM_EXPANDCOLUMN); CHEVRON_COL is that same 16
    // and the box is centred in it here, so the glyph also lands where PropTree's does.
    //
    // Only the box is scaled, and the stroke with it. PropTree hard-codes both at 96 dpi
    // values, so at 150% it draws the same 12 px box with the same 1.55 px stroke; a copy
    // that matched that exactly would match a glyph that is too small for the display it
    // is on. Every other number below is a ratio of the box, exactly as it is there.
    const REAL kBoxAt96   = 12.0f;                  // PROPTREEITEM_EXPANDBOX
    const REAL kStrokeAt96 = 1.55f;                 // _DrawExpand's stroke
    const REAL kInsetRatio = 0.194f;                // ... and its inset
    const REAL kAngle      = 53.13f + 25.0f;        // PROPTREEITEM_EXPANDANGLE, degrees

    const REAL box = (REAL)ScaleMetric((int)kBoxAt96);

    if (box < 4.0f)
        return;

    const REAL stroke = kStrokeAt96 * box / kBoxAt96;
    const REAL inset  = box * kInsetRatio;
    const REAL span   = box - 2 * inset;

    // The opening angle fixes how deep the apex sits below (or right of) the line
    // through the two tips, which is what makes this a chevron rather than a triangle:
    // depth = half the span / tan(half the angle).
    const REAL halfAngle = (REAL)(kAngle / 2 * 3.14159265358979 / 180);
    const REAL depth     = (REAL)(span / 2 / tan((double)halfAngle));

    const REAL x0 = (REAL)(rcGlyph.left + (rcGlyph.Width()  - (int)box) / 2);
    const REAL y0 = (REAL)(rcGlyph.top  + (rcGlyph.Height() - (int)box) / 2);

    const REAL tipOfs  = (box - span)  / 2;         // == inset
    const REAL apexOfs = (box - depth) / 2;
    const REAL centre  = box / 2.0f;

    PointF pts[3];

    if (expanded)
    {
        pts[0] = PointF(x0 + tipOfs,        y0 + apexOfs);            // left tip,
        pts[1] = PointF(x0 + centre,        y0 + apexOfs + depth);     // apex below the middle,
        pts[2] = PointF(x0 + tipOfs + span, y0 + apexOfs);            // right tip
    }
    else
    {
        pts[0] = PointF(x0 + apexOfs,       y0 + tipOfs);             // top tip,
        pts[1] = PointF(x0 + apexOfs + depth, y0 + centre);           // apex right of the middle,
        pts[2] = PointF(x0 + apexOfs,       y0 + tipOfs + span);      // bottom tip
    }

    // PropTree takes the colour from COLOR_BTNTEXT and never changes it. At rest this
    // matches that exactly; the hover tint is the one addition, because here the glyph
    // is the click target and a target that does not acknowledge the pointer invites a
    // second click. The shape is identical in both states.
    const COLORREF cr = hot ? CLR_CHEVRON_HOT : ::GetSysColor(COLOR_BTNTEXT);

    g.SetSmoothingMode(SmoothingModeAntiAlias);

    Pen pen(Color(255, GetRValue(cr), GetGValue(cr), GetBValue(cr)), stroke);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    pen.SetLineJoin(LineJoinRound);

    g.DrawLines(&pen, pts, 3);
}

void CSnippetListWnd::PaintCard (Gdiplus::Graphics& g, CDC& dc, int row, const CRect& rcCard)
{
    if (rcCard.IsRectEmpty() || row < 0 || row >= (int)m_vecRows.size())
        return;

    const Row& r = m_vecRows[row];

    if (r.item < 0 || r.item >= (int)m_vecItems.size())
        return;

    const SnippetItem& item = m_vecItems[r.item];

    // Resolved once rather than at each DrawText, so the four parts of one card cannot
    // disagree about whether it is dimmed.
    const COLORREF crHeader   = item.dimmed ? CLR_HEADER_DIM    : CLR_HEADER;
    const COLORREF crBadge    = item.dimmed ? CLR_BADGE_DIM     : CLR_BADGE;
    const COLORREF crBadgeReq = item.dimmed ? CLR_BADGE_REQ_DIM : CLR_BADGE_REQ;
    const COLORREF crBody     = item.dimmed ? CLR_BODY_DIM      : CLR_BODY;

    const bool bSelected = (row == m_nSelRow);
    const bool bHover    = (row == m_nHoverRow) && !bSelected;

    // A selected card in an unfocused pane falls back to the button colours rather
    // than keeping the accent: the accent means "this is where the keyboard is", and
    // two panes claiming it at once is what makes a docking UI hard to read.
    COLORREF crFill, crBorder;

    if (bSelected)
    {
        const bool bFocused = (GetFocus() == this);
        crFill   = bFocused ? CLR_SEL_FILL : ::GetSysColor(COLOR_BTNFACE);
        crBorder = bFocused ? CLR_ACCENT   : ::GetSysColor(COLOR_BTNSHADOW);
    }
    else if (bHover)
    {
        crFill   = CLR_HOT_FILL;
        crBorder = CLR_HOT_BORDER;
    }
    else
    {
        crFill   = CLR_CARD;
        crBorder = CLR_BORDER;
    }

    FillRoundRect(g, rcCard, ScaleMetric(CARD_RADIUS), crFill, crBorder);

    const int pad = ScaleMetric(CARD_PAD);

    CRect rcHeaderLine(rcCard.left + pad, rcCard.top + pad,
                       rcCard.right - pad, rcCard.top + pad + m_cyHeaderLine);

    if (rcHeaderLine.IsRectEmpty())
        return;

    // The glyph column is a fixed width, so the chevron and the text start of every
    // card line up down the list whatever the name lengths are.
    const int xText = rcHeaderLine.left + ScaleMetric(CHEVRON_COL);

    const CRect rcGlyph(rcHeaderLine.left, rcHeaderLine.top, xText, rcHeaderLine.bottom);
    DrawChevron(g, rcGlyph, r.expanded, bHover || bSelected);

    CRect rcHeader(xText, rcHeaderLine.top, rcHeaderLine.right, rcHeaderLine.bottom);

    // The badge is measured first and the header gets what is left, so a long name ellipsises
    // instead of running under the keyword. Both stay one line: nothing re-lays out here.
    if (!item.badge.empty())
    {
        // HeaderFont() for the optional part, BoldHeaderFont() for the required part: the
        // keyword is chrome like the card name, and each half is measured in what draws it.
        CFont* pOldBadge = dc.SelectObject(HeaderFont());

        // Clamped, not zeroed: a split at the end means the whole keyword is required, the
        // one case that should be all emphasis, all bold, and no badge colour at all.
        const size_t nSplit = (item.badgeSplit < item.badge.length())
                            ? item.badgeSplit : item.badge.length();
        const size_t nOpt   = item.badge.length() - nSplit;

        const CSize szOpt = nOpt ? dc.GetTextExtent(item.badge.c_str() + nSplit, (int)nOpt)
                                 : CSize(0, 0);

        dc.SelectObject(BoldHeaderFont());

        const CSize szReq = nSplit ? dc.GetTextExtent(item.badge.c_str(), (int)nSplit)
                                   : CSize(0, 0);

        // Summed across two fonts and two weights: measuring the badge as one string in
        // either face alone would leave it overhanging the card or stealing header room.
        const int xBadge = rcHeader.right - szReq.cx - szOpt.cx;

        if (xBadge > rcHeader.left)
        {
            CRect rc(xBadge, rcHeader.top, xBadge, rcHeader.bottom);

            if (nSplit)
            {
                rc.right = rc.left + szReq.cx;

                dc.SetTextColor(crBadgeReq);
                dc.DrawText(item.badge.c_str(), (int)nSplit, &rc,
                            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                rc.left = rc.right;
            }

            if (nOpt)
            {
                // Two draws, each in a rect of its own measured width and DT_LEFT: one
                // DT_RIGHT per half would round each independently and open a seam between.
                dc.SelectObject(HeaderFont());
                dc.SetTextColor(crBadge);

                rc.right = rc.left + szOpt.cx;

                dc.DrawText(item.badge.c_str() + nSplit, (int)nOpt, &rc,
                            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }

            rcHeader.right = xBadge - ScaleMetric(BADGE_GAP);
        }

        dc.SelectObject(pOldBadge);
    }

    if (rcHeader.right > rcHeader.left)
    {
        CFont* pOld = dc.SelectObject(HeaderFont());

        // Unchanged by selection: all four fills above are light, and a header that changed
        // on selection would read as the text changing rather than the card being picked.
        dc.SetTextColor(crHeader);
        dc.DrawText(item.header.c_str(), (int)item.header.length(), &rcHeader,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        dc.SelectObject(pOld);
    }

    if (!r.expanded || item.body.empty())
        return;

    // BODY_INDENT is the same metric as CHEVRON_COL on purpose, so the body starts
    // under the header text and not under the chevron.
    CRect rcBody(rcCard.left + pad + ScaleMetric(BODY_INDENT),
                 rcHeaderLine.bottom + ScaleMetric(BODY_GAP),
                 rcCard.right - pad,
                 rcCard.bottom - pad);

    if (rcBody.IsRectEmpty() || rcBody.top >= rcCard.bottom)
        return;

    CFont* pOld = dc.SelectObject(BodyFont());
    dc.SetTextColor(crBody);

    // Line by line rather than one DrawText over the whole body: DrawText wraps at
    // word boundaries unless it is given DT_SINGLELINE, and wrapping code is both
    // wrong to read and impossible to keep in step with the height RowHeight()
    // computed, which counts '\n' and nothing else. The bottom edge of rcBody is the
    // limit, so the loop also stops drawing once it runs off the card.
    const wchar_t* pBody = item.body.c_str();
    const size_t   nBody = item.body.length();

    size_t pos   = 0;
    int    yLine = rcBody.top;

    while (pos < nBody && yLine + m_cyBodyLine <= rcBody.bottom)
    {
        const size_t eol = item.body.find(L'\n', pos);
        size_t len = (eol == std::wstring::npos ? nBody : eol) - pos;

        // Strip the CR of a CRLF body: left in, it is drawn as a box by some fonts
        // and shifts the ellipsis by one character in all of them.
        if (len && pBody[pos + len - 1] == L'\r')
            len--;

        CRect rcLine(rcBody.left, yLine, rcBody.right, yLine + m_cyBodyLine);

        dc.DrawText(pBody + pos, (int)len, &rcLine,
                    DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                    | DT_END_ELLIPSIS | DT_EXPANDTABS);

        if (eol == std::wstring::npos)
            break;

        pos    = eol + 1;
        yLine += m_cyBodyLine;
    }

    dc.SelectObject(pOld);
}

////////////////////////////////////////////////////////////////////////////////
// Selection, expansion, notification

LRESULT CSnippetListWnd::Notify (int code, LPARAM lParam)
{
    // To the immediate parent, which for the snippet pane is the pane itself and not
    // the docking container it sits in. SendMessage, not Post: the pane answers
    // SLN_ACTIVATED by inserting into the active editor, and a posted message would
    // run after whatever else the frame had queued, by which time the active view
    // may have moved. It is also what makes the reply mean anything.
    CWnd* pParent = GetParent();

    if (pParent && pParent->GetSafeHwnd())
        return pParent->SendMessage(WM_SQLT_SNIPPETLIST, (WPARAM)code, lParam);

    return 0L;    // nobody to tell, so nobody handled it
}

void CSnippetListWnd::InvalidateRow (int row)
{
    if (row < 0 || row >= (int)m_vecRows.size() || !GetSafeHwnd())
        return;

    CRect rc = CardRect(row);

    if (rc.IsRectEmpty())
        return;

    // One pixel out: the card's stroke is anti-aliased, so its outermost pixel
    // belongs to the card and has to be repainted with it.
    rc.InflateRect(1, 1);
    InvalidateRect(&rc, FALSE);
}

int CSnippetListWnd::GetSelectedItem () const
{
    if (m_nSelRow < 0 || m_nSelRow >= (int)m_vecRows.size())
        return -1;

    // A caption row carries item == -1, so "no selection" and "over a caption"
    // cannot be told apart by a caller - and never need to be, because captions are
    // not selectable.
    return m_vecRows[m_nSelRow].item;
}

void CSnippetListWnd::SetHoverRow (int row)
{
    if (row == m_nHoverRow)
        return;

    const int nOld = m_nHoverRow;
    m_nHoverRow = row;

    // The two cards whose appearance changed and nothing else. This runs on every
    // mouse move over the pane, and a full invalidate per move is visible as flicker
    // once the list is long enough to scroll.
    InvalidateRow(nOld);
    InvalidateRow(row);
}

void CSnippetListWnd::SetSelectedItem (int item, bool scrollIntoView)
{
    const int row = (item < 0) ? -1 : RowFromItem(item);

    if (row != m_nSelRow)
    {
        const int nOld = m_nSelRow;
        m_nSelRow = row;

        InvalidateRow(nOld);
        InvalidateRow(row);

        Notify(SLN_SELCHANGED, GetSelectedItem());
    }

    if (row >= 0 && scrollIntoView)
        EnsureVisible(item);
}

bool CSnippetListWnd::IsExpanded (int item) const
{
    const int row = RowFromItem(item);
    return row >= 0 && m_vecRows.at(row).expanded;
}

void CSnippetListWnd::SetExpanded (int item, bool expanded)
{
    const int row = RowFromItem(item);

    if (row < 0 || m_vecRows.at(row).expanded == expanded)
        return;

    m_vecRows.at(row).expanded = expanded;

    // Every row below moves, so this is a full relayout and a full repaint rather
    // than an invalidate of one card. UpdateScrollBars() does the invalidating.
    LayoutRows();
    UpdateScrollBars();
}

void CSnippetListWnd::ExpandAll (bool expanded)
{
    bool changed = false;

    for (size_t i = 0; i < m_vecRows.size(); i++)
        if (m_vecRows[i].IsCard() && m_vecRows[i].expanded != expanded)
        {
            m_vecRows[i].expanded = expanded;
            changed = true;
        }

    if (!changed)
        return;

    LayoutRows();
    UpdateScrollBars();
}

bool CSnippetListWnd::ToggleRow (int row)
{
    if (row < 0 || row >= (int)m_vecRows.size() || !m_vecRows[row].IsCard())
        return false;

    m_vecRows[row].expanded = !m_vecRows[row].expanded;

    LayoutRows();
    UpdateScrollBars();

    Notify(SLN_ITEMEXPANDED, m_vecRows[row].item);
    return true;
}

void CSnippetListWnd::EnsureVisible (int item)
{
    const int row = RowFromItem(item);

    if (row < 0 || !GetSafeHwnd())
        return;

    CRect rc;
    GetClientRect(&rc);

    const Row& r = m_vecRows.at(row);
    const int nMargin = ScaleMetric(CARD_MARGIN);

    // To the nearest edge that makes the card whole, and only then. A card already
    // fully in view must not move, or arrowing down a list would jump the moment a
    // tall open card touched the bottom edge.
    if (r.y < m_nScrollPos)
        ScrollTo(r.y - nMargin);
    else if (r.y + r.height > m_nScrollPos + rc.Height())
        ScrollTo(r.y + r.height - rc.Height() + nMargin);
}

void CSnippetListWnd::MoveFocus (int delta)
{
    if (m_vecRows.empty())
        return;

    int row = m_nSelRow;

    // With nothing selected, Down starts at the top and Up at the bottom, which is
    // what every list control does and what makes the first key press predictable.
    if (row < 0 || row >= (int)m_vecRows.size())
        row = (delta < 0) ? (int)m_vecRows.size() : -1;

    // Captions are stepped over rather than stopping the walk: a language heading is
    // not something the user asked to land on, and stopping there would need a rule
    // for what selecting it means.
    for (row += delta; row >= 0 && row < (int)m_vecRows.size(); row += delta)
        if (m_vecRows[row].IsCard())
        {
            SetSelectedItem(m_vecRows[row].item);
            return;
        }
}

////////////////////////////////////////////////////////////////////////////////
// Sizing and scrolling

void CSnippetListWnd::OnSize (UINT nType, int cx, int cy)
{
    CWnd::OnSize(nType, cx, cy);

    // No LayoutRows() here. Nothing wraps, so a narrower pane does not make a card
    // taller - it only shows less of each line, which is what the ellipsis is for.
    // All a resize can change is how much of the content fits, so all it does is
    // re-arm the bar and repaint.
    if (cx > 0 && cy > 0)
        UpdateScrollBars();
}

void CSnippetListWnd::OnVScroll (UINT nSBCode, UINT nPos, CScrollBar* pScrollBar)
{
    UNREFERENCED_PARAMETER(nPos);
    UNREFERENCED_PARAMETER(pScrollBar);

    CRect rc;
    GetClientRect(&rc);

    const int nPage = max(1, rc.Height());

    // A body line rather than a fixed pixel count: the unit the user reads this list
    // in is a line of code, and 16px of it means something different at 200% than it
    // does at 96.
    const int nLine = max(1, m_cyBodyLine);

    switch (nSBCode)
    {
    case SB_LINEUP:        ScrollTo(m_nScrollPos - nLine);  break;
    case SB_LINEDOWN:      ScrollTo(m_nScrollPos + nLine);  break;
    case SB_PAGEUP:        ScrollTo(m_nScrollPos - nPage);  break;
    case SB_PAGEDOWN:      ScrollTo(m_nScrollPos + nPage);  break;
    case SB_TOP:           ScrollTo(0);                     break;
    case SB_BOTTOM:        ScrollTo(m_nContentHeight);      break;

    case SB_THUMBPOSITION:
    case SB_THUMBTRACK:
        {
            SCROLLINFO si;
            memset(&si, 0, sizeof(si));
            si.cbSize = sizeof(si);
            si.fMask  = SIF_TRACKPOS;

            // The track position, not nPos: WM_VSCROLL carries the position as a
            // 16-bit word, which truncates any content height above 65535 - well
            // within reach once a few hundred templates are expanded.
            if (GetScrollInfo(SB_VERT, &si))
                ScrollTo(si.nTrackPos);
        }
        break;
    }
}

BOOL CSnippetListWnd::OnMouseWheel (UINT nFlags, short zDelta, CPoint pt)
{
    UNREFERENCED_PARAMETER(nFlags);
    UNREFERENCED_PARAMETER(pt);

    DWORD dwLines = 3;
    ::SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &dwLines, 0);

    CRect rc;
    GetClientRect(&rc);

    int nAmount;

    if (dwLines == WHEEL_PAGESCROLL || dwLines == 0)
        nAmount = rc.Height();
    else
        nAmount = (int)dwLines * max(1, m_cyBodyLine);

    ScrollTo(m_nScrollPos - (zDelta / WHEEL_DELTA) * nAmount);

    // Handled, and deliberately not passed on: letting the frame see an unclaimed
    // wheel message is how a scroll over one pane ends up scrolling another.
    return TRUE;
}

BOOL CSnippetListWnd::PreTranslateMessage (MSG* pMsg)
{
    if (pMsg->message == WM_KEYDOWN && pMsg->hwnd == GetSafeHwnd() && IsListKey((UINT)pMsg->wParam))
    {
        // Straight back into our own window proc rather than a second copy of the
        // handling: OnKeyDown stays the one place that knows what the keys do.
        SendMessage(WM_KEYDOWN, pMsg->wParam, pMsg->lParam);
        return TRUE;
    }

    return CWnd::PreTranslateMessage(pMsg);
}

////////////////////////////////////////////////////////////////////////////////
// Exception frames
//
// Every handler in this file runs inside one of these two, so none needs its own try
// block: a window message arrives through WindowProc, a command or a UI update
// through OnCmdMsg. PreTranslateMessage needs neither - IsListKey cannot throw, and
// its SendMessage re-enters WindowProc, which is already framed.

LRESULT CSnippetListWnd::WindowProc (UINT message, WPARAM wParam, LPARAM lParam)
{
    try {
        EXCEPTION_FRAME;

        return CWnd::WindowProc(message, wParam, lParam);
    }
    _COMMON_DEFAULT_HANDLER_;

    return 0;
}

BOOL CSnippetListWnd::OnCmdMsg (UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo)
{
    try {
        EXCEPTION_FRAME;

        return CWnd::OnCmdMsg(nID, nCode, pExtra, pHandlerInfo);
    }
    _COMMON_DEFAULT_HANDLER_;

    // TRUE, as COEditorView::OnCmdMsg does: claiming the command stops the frame from
    // routing it on to a handler that would hit the same failure.
    return TRUE;
}

////////////////////////////////////////////////////////////////////////////////
// Mouse

void CSnippetListWnd::OnMouseMove (UINT nFlags, CPoint point)
{
    if (!m_bTrackingMouse)
    {
        TRACKMOUSEEVENT tme;
        memset(&tme, 0, sizeof(tme));
        tme.cbSize    = sizeof(tme);
        tme.dwFlags   = TME_LEAVE;
        tme.hwndTrack = GetSafeHwnd();

        m_bTrackingMouse = ::TrackMouseEvent(&tme) != FALSE;
    }

    if (m_bDragArmed && (nFlags & MK_LBUTTON))
    {
        // The system's own thresholds rather than a constant of our own: they are
        // what every other draggable surface in this app respects, and they are
        // already scaled for the user's dpi and for how far they expect to move the
        // pointer before a press turns into a drag.
        if (abs(point.x - m_ptDragAnchor.x) >= ::GetSystemMetrics(SM_CXDRAG)
         || abs(point.y - m_ptDragAnchor.y) >= ::GetSystemMetrics(SM_CYDRAG))
        {
            StartDrag();
            return;
        }
    }

    // Hover follows the card, not the row: a caption is not hoverable and painting
    // one as though it were would suggest it does something.
    const SnippetHit hit = HitTest(point);
    SetHoverRow(hit.item >= 0 ? RowFromItem(hit.item) : -1);

    CWnd::OnMouseMove(nFlags, point);
}

void CSnippetListWnd::OnMouseLeave ()
{
    m_bTrackingMouse = false;
    SetHoverRow(-1);
}

void CSnippetListWnd::OnLButtonDown (UINT nFlags, CPoint point)
{
    const SnippetHit hit = HitTest(point);

    // Focus on any click inside the list, the background included: without it the
    // arrows keep going to whatever the user clicked last and the pane looks as
    // though it has stopped answering the keyboard.
    SetFocus();

    if (hit.item < 0)
    {
        SetSelectedItem(-1, false);
        CWnd::OnLButtonDown(nFlags, point);
        return;
    }

    SetSelectedItem(hit.item);

    if (hit.part == SNP_CHEVRON)
    {
        // Toggled on the way down so the card opens under the pointer before the
        // button comes back up - the feedback a tree gives. No drag is armed off a
        // toggle: the press already did something, and a drag started from it would
        // either undo it or carry a card the user did not mean.
        ToggleRow(RowFromItem(hit.item));
        CWnd::OnLButtonDown(nFlags, point);
        return;
    }

    m_bDragArmed   = true;
    m_nDragRow     = RowFromItem(hit.item);
    m_ptDragAnchor = point;
    SetCapture();

    CWnd::OnLButtonDown(nFlags, point);
}

void CSnippetListWnd::OnLButtonUp (UINT nFlags, CPoint point)
{
    const bool bWasArmed = m_bDragArmed;

    m_bDragArmed = false;
    m_nDragRow   = -1;

    // Only when this window took the capture: StartDrag() has already given it up
    // before handing the pointer to OLE, and releasing a capture we do not hold
    // would drop it from whichever window does.
    if (bWasArmed && GetCapture() == this)
        ReleaseCapture();

    CWnd::OnLButtonUp(nFlags, point);
}

void CSnippetListWnd::OnLButtonDblClk (UINT nFlags, CPoint point)
{
    const SnippetHit hit = HitTest(point);

    // Not from the chevron. A double click there is the first click's toggle, then
    // this, then the second toggle: the card would end up back where it started and
    // the snippet would have been inserted on the way, which is not a thing anyone
    // meant to do. The chevron's gesture is the toggle and nothing else.
    if (hit.item >= 0 && hit.part != SNP_CHEVRON)
        Notify(SLN_ACTIVATED, hit.item);

    CWnd::OnLButtonDblClk(nFlags, point);
}

void CSnippetListWnd::OnRButtonUp (UINT nFlags, CPoint point)
{
    const SnippetHit hit = HitTest(point);

    // Right-clicking a card selects it first, so the menu the pane is about to track
    // is about the card under the pointer and not about one the user selected a
    // minute ago.
    if (hit.item >= 0 && hit.item != GetSelectedItem())
        SetSelectedItem(hit.item);

    CPoint ptScreen = point;
    ClientToScreen(&ptScreen);

    // Reported whether or not the pointer is on a card: the pane grays the entries that
    // need a selection, and Refresh / Settings... are useful with an empty list - which
    // is exactly when a right click on the background is most likely.
    //
    // Screen coordinates, because the pane tracks the menu and TrackPopupMenu wants
    // them. Client ones would put the menu a pane-width away from the pointer.

    // CWnd::OnRButtonUp reaches DefWindowProc, which turns this WM_RBUTTONUP into a
    // WM_CONTEXTMENU up the frame chain - a second menu behind the one the pane just
    // tracked. So the base runs only when the parent does not claim the click; a host
    // with no SLN_CONTEXTMENU case keeps the stock behaviour rather than a dead button.
    if (!Notify(SLN_CONTEXTMENU, MAKELONG(ptScreen.x, ptScreen.y)))
        CWnd::OnRButtonUp(nFlags, point);
}

////////////////////////////////////////////////////////////////////////////////
// Keyboard

void CSnippetListWnd::OnKeyDown (UINT nChar, UINT nRepCnt, UINT nFlags)
{
    UNREFERENCED_PARAMETER(nRepCnt);
    UNREFERENCED_PARAMETER(nFlags);

    const bool bCtrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const int  item  = GetSelectedItem();

    switch (nChar)
    {
    case VK_DOWN:
        MoveFocus(+1);
        return;

    case VK_UP:
        MoveFocus(-1);
        return;

    case VK_RIGHT:
    case VK_LEFT:
        // Through ToggleRow rather than SetExpanded(), so the pane hears about it.
        // Both are a user gesture, and a gesture that changed expansion silently would
        // be the one case where SLN_ITEMEXPANDED did not mean what it says.
        if (item >= 0 && IsExpanded(item) != (nChar == VK_RIGHT))
            ToggleRow(m_nSelRow);
        return;

    case VK_RETURN:
        // Ctrl+Enter is the keyboard's double click. Bare Enter toggles instead: in a
        // list of things you can open, Enter opening one is the stronger reading, and
        // inserting into the editor from a bare Enter would be a surprise the first
        // time it happened.
        if (bCtrl)
        {
            if (item >= 0)
                Notify(SLN_ACTIVATED, item);
        }
        else
            ToggleRow(m_nSelRow);
        return;

    case VK_SPACE:
        ToggleRow(m_nSelRow);
        return;

    case VK_HOME:
    case VK_END:
        {
            // Walking inwards from the end rather than taking row 0 and the last row:
            // a caption is only ever emitted immediately before an item, so the ends
            // are cards today, and this stays correct if that ever stops being true.
            const int  step = (nChar == VK_HOME) ? +1 : -1;
            int        row  = (nChar == VK_HOME) ? 0 : (int)m_vecRows.size() - 1;

            for (; row >= 0 && row < (int)m_vecRows.size(); row += step)
                if (m_vecRows[row].IsCard())
                {
                    SetSelectedItem(m_vecRows[row].item);
                    break;
                }
        }
        return;

    case VK_PRIOR:
    case VK_NEXT:
        {
            CRect rc;
            GetClientRect(&rc);

            // Paging scrolls and leaves the selection where it is. It is a viewing
            // gesture here, not a selection one: the user pages to look at something
            // else and Ctrl+Enter still inserts the card they picked.
            ScrollTo(m_nScrollPos + ((nChar == VK_NEXT) ? rc.Height() : -rc.Height()));
        }
        return;

    case 'C':
        if (bCtrl)
        {
            CopySelected();
            return;
        }
        break;

    case VK_INSERT:
        if (bCtrl)
        {
            // The other half of the standard copy pair, and the one that still works
            // for a user whose Ctrl+C has been taken over by something else.
            CopySelected();
            return;
        }
        break;
    }

    CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}

UINT CSnippetListWnd::OnGetDlgCode ()
{
    // DLGC_WANTARROWS as well as claiming the keys in PreTranslateMessage(): the pane
    // can be hosted anywhere, and a host that asks the dialog code before deciding
    // what to forward should be told the arrows belong to the list. Not
    // DLGC_WANTALLKEYS - that would claim VK_TAB too and trap the focus in the pane.
    return CWnd::OnGetDlgCode() | DLGC_WANTARROWS;
}

////////////////////////////////////////////////////////////////////////////////
// Copy and drag

bool CSnippetListWnd::CopySelected ()
{
    const int item = GetSelectedItem();

    if (item < 0 || item >= (int)m_vecItems.size())
        return false;

    const std::wstring& text = m_vecItems[item].PayloadText();

    if (text.empty())
        return false;

    // OpenClipboard / EmptyClipboard / CloseClipboard around AppSetClipboardText,
    // which is the sequence CTreeViewer::OnEditCopy uses (TreeViewer.cpp:1051).
    // That helper only calls SetClipboardData, and SetClipboardData fails outright on
    // a clipboard nobody has opened - so calling it "directly", as several places in
    // this app do, silently copies nothing.
    if (!OpenClipboard())
        return false;

    bool ok = (::EmptyClipboard() != FALSE);

    if (ok)
    {
        // CF_UNICODETEXT and not CF_TEXT: the editor's paste path handles either, but
        // only Unicode survives a non-ASCII identifier, and only Unicode makes the
        // paste not depend on the clipboard synthesising the other format.
        ok = Common::AppSetClipboardText(text.c_str(), (int)text.length(), CF_UNICODETEXT);

        // The marker that tells the editor to re-indent what it was given. Length is
        // strlen("{code}") == 6: the call sites elsewhere pass sizeof, which counts the
        // terminator and over-reads the literal by one byte.
        if (ok)
            ok = Common::AppSetClipboardText("{code}", CODE_MARKER_LEN, CF_PRIVATEFIRST);
    }

    CloseClipboard();
    return ok;
}

void CSnippetListWnd::StartDrag ()
{
    m_bDragArmed = false;

    const int row  = m_nDragRow;
    const int item = (row >= 0 && row < (int)m_vecRows.size()) ? m_vecRows[row].item : -1;

    m_nDragRow = -1;

    // Our capture has to go before OLE takes the pointer, whichever way this returns:
    // DoDragDrop() runs its own modal loop and expects to own the mouse from the
    // moment it is called.
    if (GetCapture() == this)
        ReleaseCapture();

    if (item < 0 || item >= (int)m_vecItems.size())
        return;

    const std::wstring& text = m_vecItems[item].PayloadText();

    if (text.empty())
        return;

    // Not Common::SimpleDragDataSource. Its OnRenderGlobalData appends "," and " " on
    // Shift and "\n" on Ctrl, at render time, which is what dragging a column list out
    // of the database browser wants and exactly wrong here: a Shift-dragged snippet
    // would land with a comma on the end of it. That behaviour stays for its callers.
    COleDataSource src;

    HGLOBAL hText = AllocGlobalW(text.c_str(), text.length());
    if (hText)
        src.CacheGlobalData(CF_UNICODETEXT, hText);

    // The marker the editor uses to decide to re-indent what it is given. Both formats
    // are cached up front rather than delay-rendered: a drag is one-shot, there is no
    // chance of the text changing underneath it, and the target decides which of the
    // two it wants.
    //
    // {code} and never {code+cr}: OnEditPaste understands the <CR> sentinel, OnDrop
    // does not, and this is the drop path.
    HGLOBAL hCode = AllocGlobalA("{code}", CODE_MARKER_LEN);
    if (hCode)
        src.CacheGlobalData(CF_PRIVATEFIRST, hCode);

    if (!hText || !hCode)
        return;

    // COPY only. Dragging a snippet does not move it out of the pane, and offering
    // MOVE would have the editor's drop handler try to delete from a source that has
    // nothing to delete from.
    src.DoDragDrop(DROPEFFECT_COPY);
}

////////////////////////////////////////////////////////////////////////////////
// Focus and teardown

void CSnippetListWnd::OnSetFocus (CWnd* pOldWnd)
{
    CWnd::OnSetFocus(pOldWnd);

    // The selected card's fill and border both depend on whether this window has the
    // focus, so gaining it is a repaint of that one card.
    InvalidateRow(m_nSelRow);
}

void CSnippetListWnd::OnKillFocus (CWnd* pNewWnd)
{
    CWnd::OnKillFocus(pNewWnd);

    // ... and so is losing it, which is what stops a card from keeping the accent
    // border after the user has clicked into the editor.
    InvalidateRow(m_nSelRow);
}

void CSnippetListWnd::OnDestroy ()
{
    // The drag state is dropped rather than completed: a drag cannot be in progress
    // while this runs - DoDragDrop() is modal and owns the thread until it returns -
    // but the capture can be left set if the window goes away under a press.
    m_bDragArmed = false;
    m_nDragRow   = -1;

    if (GetCapture() == this)
        ReleaseCapture();

    m_nHoverRow = -1;
    m_nSelRow   = -1;

    CWnd::OnDestroy();
}

}; // namespace Common
