#include "stdafx.h"
#include "Common/PropTreeSearchBar.h"

#include <algorithm>
#include <cmath>

// The magnifier and the two arrows are a curve and diagonals, and plain GDI has
// no smoothing for either, so both came out stepped at every dpi. GDI+ is already
// up for the whole process - GdiplusStartup() runs in App\SQLTools.cpp and
// Gdiplus.lib is #pragma comment linked there - so this adds the header and
// nothing else. No file scope `using namespace Gdiplus`: the names it brings in
// (Pen, Color, Point, Path) are easy to collide with in a file that pulls in this
// many headers, so the two paint functions open the namespace locally. The same
// choice, for the same reason, as the tree's expand chevron in PropTreeItem.cpp.
#include <gdiplus.h>

// ECM_FIRST + 1. Not in the SDK headers this project builds against unless
// commctrl.h is new enough, and the bar has to compile either way - when the
// message is unknown to the running comctl32 it is simply ignored and the field
// shows no cue banner.
#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif

namespace Common {

namespace {

// The palette. Deliberately literal RGB values and not GetSysColor() for the
// accents: a search field that is the same gray as everything around it does not
// read as a field, and the point of the bar is to be recognisable at a glance.
// The background and the text still come from the system, so the bar follows the
// user's colour scheme where it matters.
const COLORREF CLR_ACCENT    = RGB(0x00, 0x78, 0xD7);  // focused border
const COLORREF CLR_BORDER    = RGB(0x8A, 0x8A, 0x8A);  // resting border
const COLORREF CLR_FIELD     = RGB(0xFF, 0xFF, 0xFF);
const COLORREF CLR_ICON      = RGB(0x5A, 0x5A, 0x5A);
const COLORREF CLR_COUNTER   = RGB(0x5A, 0x5A, 0x5A);
const COLORREF CLR_NOMATCH   = RGB(0xC4, 0x2B, 0x1C);  // a query that found nothing
const COLORREF CLR_GLYPH     = RGB(0x3C, 0x3C, 0x3C);  // both arrows and the gear
const COLORREF CLR_GLYPH_OFF = RGB(0xBB, 0xBB, 0xBB);  // nothing to step through
const COLORREF CLR_BTN_HOT   = RGB(0xE3, 0xE3, 0xE3);  // a button under the pointer
const COLORREF CLR_BTN_DOWN  = RGB(0xCF, 0xCF, 0xCF);  // ... and held down

// How far the text sits inside the field, top and bottom. The field is a little
// taller than the text so the rounded corners do not crowd it.
const int EDIT_VINSET_AT96 = 2;

// How far below that symmetric inset the edit is placed. At the inset alone the text
// reads high in the field, so the edit is pushed down - and GetPreferredHeight() grows
// the field by the same amount, which is what keeps the bottom inset at
// EDIT_VINSET_AT96. Without that growth a 3px shift lands the edit's bottom edge one
// pixel past rcField's, and since the edit paints its own white background that shows
// as a square white corner poking out of the rounded field onto the bar's gray.
const int EDIT_VSHIFT_AT96 = 3;

// Stroke of the magnifier, both arrows and the settings gear, in hundredths of a pixel
// at 96dpi, so 200 is 2.0px. All four share it so the glyphs read as a set. Hundredths
// because ScaleMetric() returns an int: handing it 2 would quantise the stroke back to
// whole pixels, which is exactly what a fractional GDI+ pen is for. MulDiv rounds rather
// than truncates, so this tracks the dpi closely - 2.0px at 96, 2.5 at 120, 3.0 at 144,
// 4.0 at 192.
const int GLYPH_STROKE_X100_AT96 = 200;

} // unnamed namespace

////////////////////////////////////////////////////////////////////////////////
// Construction

CPropTreeSearchBar::CPropTreeSearchBar ()
    : m_nCurrent(0)
    , m_nTotal(0)
    , m_bNoMatch(FALSE)
    , m_bIncludeDesc(TRUE)      // PSF_INFO, in PSF_DEFAULT
    , m_bIncludeValues(TRUE)    // PSF_VALUE, not in PSF_DEFAULT but on in the profile - see below
    , m_bIncludeChoices(TRUE)   // PSF_CHOICES, in PSF_DEFAULT
    , m_bFieldFocus(FALSE)
    , m_bTracking(FALSE)
    , m_nBtnHot(BTN_NONE)
    , m_nBtnDown(BTN_NONE)
{
    // The field's fill is a literal white so it has to be a brush of its own; the
    // sheet's background is what a property sheet paints itself with.
    m_brushBack.CreateSolidBrush(::GetSysColor(COLOR_BTNFACE));
    m_brushField.CreateSolidBrush(CLR_FIELD);
}

CPropTreeSearchBar::~CPropTreeSearchBar ()
{
}

int CPropTreeSearchBar::ScaleMetric (int nAt96dpi)
{
    HDC hScreen = ::GetDC(NULL);

    if (!hScreen)
        return nAt96dpi;

    const int nDpiY = ::GetDeviceCaps(hScreen, LOGPIXELSY);
    ::ReleaseDC(NULL, hScreen);

    return MulDiv(nAt96dpi, (nDpiY > 0) ? nDpiY : 96, 96);
}

int CPropTreeSearchBar::GetPreferredHeight (CFont* pFont)
{
    // The text height drives everything else, so a taller font at a higher dpi -
    // or a sheet whose dialog font the user has enlarged - gets a taller bar
    // instead of a bar that clips what is typed into it.
    int nTextH = 13;        // 9pt Segoe UI at 96dpi, the fallback when there is no font

    HDC hScreen = ::GetDC(NULL);

    if (hScreen)
    {
        if (pFont && pFont->GetSafeHandle())
        {
            HFONT hOld = (HFONT)::SelectObject(hScreen, pFont->GetSafeHandle());

            TEXTMETRIC tm;
            if (::GetTextMetrics(hScreen, &tm))
                nTextH = tm.tmHeight;

            ::SelectObject(hScreen, hOld);
        }

        ::ReleaseDC(NULL, hScreen);
    }

    // The field is the text plus its vertical inset, but never shorter than 24px:
    // below that a rounded rect stops reading as a field and starts reading as a
    // rule, however comfortably the font itself would fit.
    //
    // The shift is added on top because ComputeLayout() places the edit that much
    // lower inside the field, and the field has to be that much taller to keep the
    // edit's bottom inset intact - see EDIT_VSHIFT_AT96. It makes the bar 3px taller
    // at 96dpi, which CreateSearchBar() takes out of the page tree's height.
    const int nField = std::max(nTextH + ScaleMetric(10), ScaleMetric(24))
                     + ScaleMetric(EDIT_VSHIFT_AT96);

    return nField + 2 * ScaleMetric(PAD_TOP);
}

////////////////////////////////////////////////////////////////////////////////
// Creation

BOOL CPropTreeSearchBar::PreCreateWindow (CREATESTRUCT& cs)
{
    // A class of our own rather than the MFC default, so that Tab walks into the
    // edit: IsDialogMessage() only descends into a child window that carries
    // WS_EX_CONTROLPARENT, and without it the field would be skipped and Ctrl+F
    // would be the only way to reach it.
    cs.lpszClass = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
    cs.dwExStyle |= WS_EX_CONTROLPARENT;

    return CWnd::PreCreateWindow(cs);
}

BOOL CPropTreeSearchBar::Create (const CRect& rc, CWnd* pParentWnd, LPCTSTR pszCueBanner)
{
    // Take the parent's font before the window exists: ComputeLayout() measures
    // the counter with it, and a bar drawn in the default GUI font would not match
    // the sheet it sits on.
    if (pParentWnd && pParentWnd->GetSafeHwnd())
    {
        CFont* pFont = pParentWnd->GetFont();

        if (pFont && pFont->GetSafeHandle())
        {
            LOGFONT lf;
            pFont->GetLogFont(&lf);
            m_font.CreateFontIndirect(&lf);
        }
    }

    if (!m_font.GetSafeHandle())
        m_font.CreateStockObject(DEFAULT_GUI_FONT);

    if (!CWnd::Create(NULL, _T(""),
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
            rc, pParentWnd, 0))
    {
        return FALSE;
    }

    const Layout lay = ComputeLayout();

    // No WS_BORDER and no WS_EX_CLIENTEDGE: the bar draws the field's frame itself,
    // and a second one from the edit would show as a box inside the rounded rect.
    if (!m_edit.Create(WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, lay.rcEdit, this, IDC_BAR_EDIT))
    {
        DestroyWindow();
        return FALSE;
    }

    m_edit.SetFont(&m_font);

    // TRUE keeps the hint up while the field has focus, which is what a search box
    // wants: the user is looking at it precisely when it is empty.
    if (pszCueBanner && *pszCueBanner)
        m_edit.SendMessage(EM_SETCUEBANNER, TRUE, (LPARAM)pszCueBanner);

    return TRUE;
}

void CPropTreeSearchBar::OnDestroy ()
{
    KillTimer(TIMER_DEBOUNCE);

    // Explicitly, because the alternative is CWnd::~CWnd() detaching a handle that
    // Windows has already destroyed and MFC warning about it on the way out.
    if (m_edit.GetSafeHwnd())
        m_edit.DestroyWindow();

    CWnd::OnDestroy();
}

////////////////////////////////////////////////////////////////////////////////
// Layout

CPropTreeSearchBar::Layout CPropTreeSearchBar::ComputeLayout () const
{
    Layout lay;
    lay.bShowCounter = FALSE;

    CRect rc;
    GetClientRect(rc);

    // The field is the bar minus a margin of sheet background above and below it,
    // which is what separates it from the page tree underneath.
    lay.rcField = rc;
    lay.rcField.DeflateRect(0, ScaleMetric(PAD_TOP));

    const int nMidY = (lay.rcField.top + lay.rcField.bottom) / 2;

    // -- magnifier, vertically centred on the field
    const int nIcon = ScaleMetric(ICON_SIZE);
    lay.rcIcon.SetRect(rc.left + ScaleMetric(PAD_FIELD), nMidY - nIcon / 2,
                       rc.left + ScaleMetric(PAD_FIELD) + nIcon, nMidY - nIcon / 2 + nIcon);

    int xLeft = lay.rcIcon.right + ScaleMetric(ICON_GAP);

    int xRight = lay.rcField.right - ScaleMetric(PAD_FIELD);

    // -- settings, rightmost of the lot. Laid out unconditionally, unlike the counter:
    //    what a search matches is worth choosing before the first search, and a button
    //    that only appeared once there were hits could not be used to affect them.
    const int nSettings  = ScaleMetric(SETTINGS_SIZE);
    const int nSettingsY = nMidY - nSettings / 2;

    lay.rcSettings.SetRect(xRight - nSettings, nSettingsY, xRight, nSettingsY + nSettings);
    xRight = lay.rcSettings.left - ScaleMetric(SETTINGS_GAP);

    // -- arrows, worked back from the settings button: down first because it is the
    //    outermost, so the pair reads top-to-bottom as previous-then-next
    const int nBtn = ScaleMetric(BTN_SIZE);
    const int nBtnY = nMidY - nBtn / 2;

    lay.rcDown.SetRect(xRight - nBtn, nBtnY, xRight, nBtnY + nBtn);
    xRight = lay.rcDown.left - ScaleMetric(BTN_GAP);

    lay.rcUp.SetRect(xRight - nBtn, nBtnY, xRight, nBtnY + nBtn);
    xRight = lay.rcUp.left - ScaleMetric(COUNTER_GAP);

    // -- counter, only while there is something to count. Measured rather than
    //    assumed, because it is right aligned: the space it takes comes straight
    //    out of the edit.
    if (m_nTotal > 0 || m_bNoMatch)
    {
        CString sCounter;
        sCounter.Format(_T("%d/%d"), m_nCurrent, m_nTotal);

        CClientDC dc(const_cast<CPropTreeSearchBar*>(this));
        CFont* pOldFont = dc.SelectObject(const_cast<CFont*>(&m_font));
        const CSize size = dc.GetTextExtent(sCounter);
        dc.SelectObject(pOldFont);

        const CRect rcCounter(xRight - size.cx, nMidY - size.cy / 2, xRight, nMidY + size.cy / 2);

        // Dropping the counter rather than shrinking the edit is the trade these
        // narrow columns force: a field too narrow to see what is typed in it is
        // worse than one with no running total beside it.
        if (rcCounter.left - xLeft >= ScaleMetric(EDIT_MIN))
        {
            lay.rcCounter  = rcCounter;
            lay.bShowCounter = TRUE;
            xRight = rcCounter.left - ScaleMetric(COUNTER_GAP);
        }
    }

    const int nVInset = ScaleMetric(EDIT_VINSET_AT96);
    const int nVShift = ScaleMetric(EDIT_VSHIFT_AT96);

    // Top inset plus the shift, bottom inset untouched: the edit moves down as a whole
    // rather than growing, so its height - and the text it has to fit - is unchanged.
    // The field was grown by the same nVShift in GetPreferredHeight(), which is what
    // keeps rcField.bottom - nVInset far enough below the new top to hold it.
    lay.rcEdit.SetRect(xLeft, lay.rcField.top + nVInset + nVShift,
                       xRight, lay.rcField.bottom - nVInset);

    return lay;
}

int CPropTreeSearchBar::HitButton (const CPoint& pt) const
{
    const Layout lay = ComputeLayout();

    if (lay.rcSettings.PtInRect(pt))
        return BTN_SETTINGS;

    if (lay.rcUp.PtInRect(pt))
        return BTN_UP;

    if (lay.rcDown.PtInRect(pt))
        return BTN_DOWN;

    return BTN_NONE;
}

void CPropTreeSearchBar::OnSize (UINT nType, int cx, int cy)
{
    CWnd::OnSize(nType, cx, cy);

    // Reached once during Create(), before the edit exists, and again whenever the
    // sheet moves the bar - the edit is the only child, and only its width follows.
    if (m_edit.GetSafeHwnd())
        m_edit.MoveWindow(ComputeLayout().rcEdit);
}

////////////////////////////////////////////////////////////////////////////////
// Painting

BOOL CPropTreeSearchBar::OnEraseBkgnd (CDC* pDC)
{
    // Painted here and returning TRUE, rather than through a class background
    // brush: the bar redraws often - every focus change, every hover, every
    // counter update - and a brush in the class would flash the whole strip before
    // OnPaint() drew over it.
    CRect rc;
    GetClientRect(rc);
    pDC->FillRect(rc, &m_brushBack);

    return TRUE;
}

void CPropTreeSearchBar::OnPaint ()
{
    CPaintDC dc(this);

    const Layout lay = ComputeLayout();

    PaintField(dc, lay);
    PaintIcon(dc, lay.rcIcon);

    if (lay.bShowCounter)
        PaintCounter(dc, lay.rcCounter);

    // Enabled by there being a query rather than by there being hits: a query that
    // matched nothing is still one the user may want to edit and step through, and
    // graying the arrows out the moment the count drops to zero would look like
    // the bar had stopped working.
    const BOOL bSteppable = !GetText().Trim().IsEmpty();

    PaintArrow(dc, lay.rcUp,   TRUE,  bSteppable);
    PaintArrow(dc, lay.rcDown, FALSE, bSteppable);

    // Enabled whatever the query, unlike the arrows: this one is about what a search
    // will match, which is worth setting before there is anything to match.
    PaintSettings(dc, lay.rcSettings);
}

void CPropTreeSearchBar::PaintField (CDC& dc, const Layout& lay)
{
    // One RoundRect does both the fill and the frame, so the two can never disagree
    // about where the corner is. The border is the only thing that says whether the
    // field has the caret, which is why it changes colour rather than the fill.
    CPen pen(PS_SOLID, std::max(1, ScaleMetric(1)), m_bFieldFocus ? CLR_ACCENT : CLR_BORDER);

    CPen*   pOldPen   = dc.SelectObject(&pen);
    CBrush* pOldBrush = dc.SelectObject(&m_brushField);

    const int nCorner = ScaleMetric(CORNER) * 2;      // RoundRect takes the ellipse, not the radius
    dc.RoundRect(lay.rcField, CPoint(nCorner, nCorner));

    dc.SelectObject(pOldBrush);
    dc.SelectObject(pOldPen);
}

void CPropTreeSearchBar::PaintIcon (CDC& dc, const CRect& rc)
{
    // Drawn rather than taken from a bitmap or a glyph font: a magnifier is two
    // strokes, and a resource would have to be supplied at every dpi the app can
    // run at while this one is measured off the same scale as everything else here.
    using namespace Gdiplus;

    if (rc.Width() < 4 || rc.Height() < 4)
        return;

    const COLORREF cr = m_bFieldFocus ? CLR_ACCENT : CLR_ICON;

    // Fractional because a GDI+ pen width is a REAL and the smoothing makes 2.25px
    // visibly different from 2px, where the GDI pen this replaced could only ever be a
    // whole number of pixels wide. GLYPH_STROKE_X100_AT96 carries the weight itself.
    const REAL fStroke = std::max(1.0f, ScaleMetric(GLYPH_STROKE_X100_AT96) / 100.0f);

    Graphics g(dc.GetSafeHdc());
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    // Round caps and a round join, so the handle meets the lens without a notch and
    // neither end of a stroke reads as a chisel.
    Pen pen(Color(255, GetRValue(cr), GetGValue(cr), GetBValue(cr)), fStroke);
    pen.SetLineJoin(LineJoinRound);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);

    // The lens takes the upper left two thirds, the handle runs out of it to the
    // lower right corner - the shape every user already reads as "search".
    //
    // GDI+ centres the stroke on the path, where the GDI Ellipse this replaced kept
    // its pen inside the box, so the lens is pulled in by half a stroke to stay
    // inside the rect the layout allotted the icon.
    const REAL fHalf  = fStroke / 2;
    const REAL fLensX = rc.left + fHalf;
    const REAL fLensY = rc.top  + fHalf;
    const REAL fLensW = rc.Width()  * 2 / 3.0f - fStroke;
    const REAL fLensH = rc.Height() * 2 / 3.0f - fStroke;

    g.DrawEllipse(&pen, fLensX, fLensY, fLensW, fLensH);

    // Out of the lens three quarters of the way along it, down to the corner of the
    // icon's box.
    g.DrawLine(&pen,
               fLensX + fLensW * 0.75f, fLensY + fLensH * 0.75f,
               rc.right - fHalf, rc.bottom - fHalf);
}


void CPropTreeSearchBar::PaintCounter (CDC& dc, const CRect& rc)
{
    CString sCounter;
    sCounter.Format(_T("%d/%d"), m_nCurrent, m_nTotal);

    const int nOldBkMode = dc.SetBkMode(TRANSPARENT);
    const COLORREF clrOld = dc.SetTextColor(m_bNoMatch ? CLR_NOMATCH : CLR_COUNTER);

    CFont* pOldFont = dc.SelectObject(&m_font);
    dc.DrawText(sCounter, const_cast<CRect&>(rc),
                DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    dc.SelectObject(pOldFont);

    dc.SetTextColor(clrOld);
    dc.SetBkMode(nOldBkMode);
}

void CPropTreeSearchBar::PaintArrow (CDC& dc, const CRect& rc, BOOL bUp, BOOL bEnabled)
{
    // Which button this is, worked out from the rect rather than passed in as well,
    // so the hover and pressed backgrounds cannot be attributed to the wrong one.
    const int nWhich = bUp ? BTN_UP : BTN_DOWN;

    PaintButtonBg(dc, rc, nWhich, bEnabled);

    // An arrow: the head is the chevron this replaced, two strokes meeting at a point,
    // and the shaft runs out of that point to the far side of the head. The shaft is the
    // part that matters - a bare chevron is the glyph the tree's own expanders use, and
    // this strip sits directly above that tree, so the two were one visual family too
    // many. An arrow says "step", and it says it without a label beside it.
    //
    // GDI+ for the same reason the tree's expanders are GDI+ (_DrawExpand in
    // PropTreeItem.cpp): both legs are diagonal, and GDI can only draw a diagonal stepped.
    using namespace Gdiplus;

    const COLORREF cr = bEnabled ? CLR_GLYPH : CLR_GLYPH_OFF;

    // The same stroke as the icon (see PaintIcon), so all four glyphs read as a set.
    // Deliberately heavier than the tree's expand chevron - _DrawExpand() in
    // PropTreeItem.cpp draws at a fixed 1.55f and the two families share a dialog in
    // tree mode - because this is a button the user aims at, not a 12px expander, and
    // because this one scales with the bar where _DrawExpand()'s slot does not.
    const REAL fStroke = std::max(1.0f, ScaleMetric(GLYPH_STROKE_X100_AT96) / 100.0f);

    // The centre is a fraction rather than an integer division: now that the stroke
    // is smoothed, landing the apex on a half pixel is what keeps the two legs even
    // instead of one drawing a shade heavier than the other.
    const REAL fMidX = (rc.left + rc.right) / 2.0f;
    const REAL fMidY = (rc.top + rc.bottom) / 2.0f;

    const REAL fHalf = static_cast<REAL>(std::max(2, rc.Width()  / 4));  // head, side to side
    const REAL fRise = static_cast<REAL>(std::max(1, rc.Height() / 5));  // apex to either tip
    const REAL fOver = static_cast<REAL>(std::max(1, rc.Height() / 8));  // shaft, past the tips

    // Both arrows are laid out inside ONE band centred on the button, and it is the band
    // that is derived rather than the shaft. Apex to centre is the head's rise plus half
    // the overshoot, which puts the shaft's far end the same distance on the other side:
    // up and down then share both boundaries and are exact mirror images of each other.
    //
    // Deriving the shaft per arrow instead - apex where the chevron's was, tail fOver past
    // the tips - would make each glyph taller on one side only, so the pair would sit a
    // fraction off centre in opposite directions and read as one button higher than its
    // neighbour. Sharing the band is what keeps the vertical alignment the chevron's had:
    // the glyph still straddles fMidY. The head moves up (or down) by half the overshoot
    // to stay inside it, which is the whole cost.
    const REAL fBand = fRise + fOver / 2.0f;    // centre to the apex, and to the shaft's end
    const REAL fTip  = 2.0f * fRise - fBand;    // centre to the tips, on the tail's side
    const REAL fDir  = bUp ? -1.0f : 1.0f;      // up: the apex is above the centre

    const PointF ptApex(fMidX, fMidY + fDir * fBand);
    const PointF ptTail(fMidX, fMidY - fDir * fBand);
    const REAL   fTipY  = fMidY - fDir * fTip;

    Graphics g(dc.GetSafeHdc());
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    const Color clrInk(255, GetRValue(cr), GetGValue(cr), GetBValue(cr));

    // Shaft first, and flat-capped at the apex so it cannot poke out of the point: a
    // round start cap extends a stroke half its width past the endpoint, which here
    // would leave a stub of shaft sticking out beyond the arrowhead. The round cap at
    // the tail stays - that end is one the user looks at. Drawn before the head so the
    // head's round join at the apex lands on top of the flat end and the three strokes
    // meet in one smooth point, no seam and no notch.
    Pen penShaft(clrInk, fStroke);
    penShaft.SetStartCap(LineCapFlat);
    penShaft.SetEndCap(LineCapRound);

    g.DrawLine(&penShaft, ptApex, ptTail);

    Pen penHead(clrInk, fStroke);
    penHead.SetLineJoin(LineJoinRound);
    penHead.SetStartCap(LineCapRound);
    penHead.SetEndCap(LineCapRound);

    PointF head[3];
    head[0] = PointF(fMidX - fHalf, fTipY);     // one tip,
    head[1] = ptApex;                           // the apex above or below it,
    head[2] = PointF(fMidX + fHalf, fTipY);     // the other tip

    g.DrawLines(&penHead, head, 3);
}

void CPropTreeSearchBar::PaintButtonBg (CDC& dc, const CRect& rc, int nWhich, BOOL bEnabled)
{
    // Pressed wins over hot: the pointer can be both inside the button and holding it
    // down, and only one of the two fills can be drawn. Shared by the arrows and the
    // settings button so the whole strip lights up the same way.
    if (nWhich == m_nBtnDown)
        dc.FillSolidRect(rc, CLR_BTN_DOWN);
    else if (nWhich == m_nBtnHot && bEnabled)
        dc.FillSolidRect(rc, CLR_BTN_HOT);
}

void CPropTreeSearchBar::PaintSettings (CDC& dc, const CRect& rc)
{
    // Always enabled, so the hover and pressed fills are always allowed.
    PaintButtonBg(dc, rc, BTN_SETTINGS, TRUE);

    // A gear: a ring with eight short radial teeth. Drawn rather than taken from a
    // resource for the same reason the magnifier is - a resource would have to be
    // supplied at every dpi the app can run at, while this is measured off the same
    // scale as everything else here. GDI+ because the teeth are diagonals and the ring
    // is a curve, and GDI can draw neither smoothly.
    using namespace Gdiplus;

    const COLORREF cr = IsWindowEnabled() ? CLR_GLYPH : CLR_GLYPH_OFF;

    // Shared with the magnifier and the arrows so all four glyphs read as a set.
    const REAL fStroke = std::max(1.0f, ScaleMetric(GLYPH_STROKE_X100_AT96) / 100.0f);

    const REAL fMidX = (rc.left + rc.right) / 2.0f;
    const REAL fMidY = (rc.top + rc.bottom) / 2.0f;

    // Every radius is a fraction of the space the stroke leaves, so a tooth tip plus
    // its round cap cannot reach past the button however the stroke is later tuned.
    const REAL fUsable   = static_cast<REAL>(rc.Width()) - fStroke;
    const REAL fRing     = fUsable * 0.24f;     // ring radius
    const REAL fToothOut = fUsable * 0.44f;     // tooth tip, measured from the centre

    Graphics g(dc.GetSafeHdc());
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    Pen pen(Color(255, GetRValue(cr), GetGValue(cr), GetBValue(cr)), fStroke);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);

    g.DrawEllipse(&pen, fMidX - fRing, fMidY - fRing, fRing * 2.0f, fRing * 2.0f);

    // The teeth start at the ring rather than clear of it, so their round caps merge
    // into it instead of leaving eight separate spokes orbiting a circle.
    const int nTeeth = 8;

    for (int i = 0; i < nTeeth; ++i)
    {
        const REAL fAng = (static_cast<REAL>(i) * 360.0f / nTeeth) * 3.14159265f / 180.0f;
        const REAL fCos = std::cos(fAng);
        const REAL fSin = std::sin(fAng);

        g.DrawLine(&pen, fMidX + fRing * fCos,     fMidY + fRing * fSin,
                         fMidX + fToothOut * fCos, fMidY + fToothOut * fSin);
    }
}

void CPropTreeSearchBar::ShowSettingsMenu ()
{
    const Layout lay = ComputeLayout();

    CMenu menu;

    if (!menu.CreatePopupMenu())
        return;

    // All three options, each ticked from the member that holds it. What acts on them is
    // the sheet, via PSBN_SETTINGS - see the OnMenuInclude*() handlers below.
    //
    // A flat list of the fields a search may match beyond the ones it always does. Labels
    // and group names are not offered because turning those off would leave nothing
    // sensible to search. PSF_PATH stays out for a different reason: a row that matches
    // only through its path is dropped again by the bPath && !bSelf rule in
    // EnumCollectMatches(), so it cannot add a hit of its own. Every field that CAN add
    // one is here, and that is the point of the menu - a hit the user cannot turn off is
    // a bug they have to work around by not searching for that word.
    menu.AppendMenu(MF_STRING | (m_bIncludeDesc ? MF_CHECKED : MF_UNCHECKED),
                    IDM_INCLUDE_DESC, _T("Including descriptions"));

    menu.AppendMenu(MF_STRING | (m_bIncludeValues ? MF_CHECKED : MF_UNCHECKED),
                    IDM_INCLUDE_VALUES, _T("Including values"));

    // "Dropdown entries" rather than the code's "choices", because it is every entry of a
    // combo row's list and not just the one picked - which is exactly how it differs from
    // "Including values" above it, and why the two are worth separating.
    menu.AppendMenu(MF_STRING | (m_bIncludeChoices ? MF_CHECKED : MF_UNCHECKED),
                    IDM_INCLUDE_CHOICES, _T("Including dropdown entries"));

    // Dropped under the button and left aligned to it, the way a toolbar drop-down sits.
    CPoint pt(lay.rcSettings.left, lay.rcSettings.bottom);
    ClientToScreen(&pt);

    // The shell's own recipe: without the SetForegroundWindow()/WM_NULL pair a menu on a
    // window that is not already the foreground one can fail to dismiss when the user
    // clicks somewhere else, because only the foreground window may own a menu.
    SetForegroundWindow();

    menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, this);

    ::PostMessage(m_hWnd, WM_NULL, 0, 0);
}

void CPropTreeSearchBar::OnMenuIncludeDesc ()
{
    // Reported rather than acted on. The bar holds the tick and the sheet holds the
    // field set, and only the sheet can re-run a search - so the toggle is followed by
    // PSBN_SETTINGS and the bar does nothing else with it. No repaint either: the gear
    // does not carry the tick, so the only thing that changed on screen is a menu that
    // has already closed.
    m_bIncludeDesc = !m_bIncludeDesc;

    Notify(PSBN_SETTINGS);
}

void CPropTreeSearchBar::OnMenuIncludeValues ()
{
    // The same shape as OnMenuIncludeDesc(), for the reasons given there. Two handlers
    // rather than one switched on the command id, because MFC's ON_COMMAND does not pass
    // the id and each would need it to know which member to toggle.
    m_bIncludeValues = !m_bIncludeValues;

    Notify(PSBN_SETTINGS);
}

void CPropTreeSearchBar::OnMenuIncludeChoices ()
{
    // And again for PSF_CHOICES - see OnMenuIncludeDesc().
    m_bIncludeChoices = !m_bIncludeChoices;

    Notify(PSBN_SETTINGS);
}

HBRUSH CPropTreeSearchBar::OnCtlColor (CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
    if (pWnd && pWnd->GetSafeHwnd() == m_edit.GetSafeHwnd())
    {
        // The edit has to paint on the field's white, not on the sheet's gray: with
        // WS_CLIPCHILDREN the bar's RoundRect never reaches under the edit, so
        // whatever brush is returned here is the field's fill for that part of it.
        pDC->SetTextColor(::GetSysColor(COLOR_WINDOWTEXT));
        pDC->SetBkColor(CLR_FIELD);
        pDC->SetBkMode(OPAQUE);

        return (HBRUSH)m_brushField.GetSafeHandle();
    }

    return CWnd::OnCtlColor(pDC, pWnd, nCtlColor);
}

////////////////////////////////////////////////////////////////////////////////
// Mouse

void CPropTreeSearchBar::OnMouseMove (UINT nFlags, CPoint point)
{
    // One outstanding TME_LEAVE is enough to be told when the pointer goes; asking
    // for another on every move would just replace it.
    if (!m_bTracking)
    {
        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, m_hWnd, 0 };

        if (::TrackMouseEvent(&tme))
            m_bTracking = TRUE;
    }

    const int nHot = HitButton(point);

    if (nHot != m_nBtnHot)
    {
        m_nBtnHot = nHot;
        InvalidateRect(NULL);       // the hover background, nothing else moved
    }

    CWnd::OnMouseMove(nFlags, point);
}

void CPropTreeSearchBar::OnMouseLeave ()
{
    m_bTracking = FALSE;

    // The pressed state survives leaving the window only while something is holding
    // it: a click that began on an arrow and was dragged off has to be able to come
    // back and fire on release, which is what the capture in OnLButtonDown() is for.
    if (GetCapture() != this)
        m_nBtnDown = BTN_NONE;

    m_nBtnHot = BTN_NONE;
    InvalidateRect(NULL);
}

void CPropTreeSearchBar::OnLButtonDown (UINT nFlags, CPoint point)
{
    const int nBtn = HitButton(point);

    if (nBtn != BTN_NONE)
    {
        m_nBtnDown = nBtn;

        // Captured so that dragging off the button and back on still releases it
        // there, and dragging away altogether cancels it - button semantics rather
        // than "whatever the mouse happened to be over when it came up".
        SetCapture();
        InvalidateRect(NULL);
        return;
    }

    // Clicks that land on the edit never reach here, it is a child window of its
    // own. What does reach here is the field's padding, the magnifier and the
    // counter - all of which should behave like the field, so the caret goes in.
    const Layout lay = ComputeLayout();

    if (lay.rcField.PtInRect(point))
        m_edit.SetFocus();

    CWnd::OnLButtonDown(nFlags, point);
}

void CPropTreeSearchBar::OnLButtonUp (UINT nFlags, CPoint point)
{
    if (m_nBtnDown != BTN_NONE)
    {
        const int nWas = m_nBtnDown;

        m_nBtnDown = BTN_NONE;
        ReleaseCapture();
        InvalidateRect(NULL);

        // Fired only if the button came up on the same one it went down on.
        if (HitButton(point) == nWas)
        {
            if (nWas == BTN_SETTINGS)
                ShowSettingsMenu();
            else
                Notify((nWas == BTN_UP) ? PSBN_PREV : PSBN_NEXT);
        }

        return;
    }

    CWnd::OnLButtonUp(nFlags, point);
}

////////////////////////////////////////////////////////////////////////////////
// Keys

BOOL CPropTreeSearchBar::PreTranslateMessage (MSG* pMsg)
{
    if (pMsg->message == WM_KEYDOWN && pMsg->hwnd == m_edit.GetSafeHwnd())
    {
        const BOOL bShift = (HIBYTE(::GetKeyState(VK_SHIFT)) != 0);

        switch (pMsg->wParam)
        {
        case VK_RETURN:
            // Stepped rather than walked, exactly like F3 and Shift+F3 on the sheet.
            Notify(bShift ? PSBN_PREV : PSBN_NEXT);
            return TRUE;

        case VK_UP:
            // A single line edit does nothing with Up and Down, so they are free to
            // mean what they mean everywhere else in a search box: step through the
            // hits without leaving the field.
            Notify(PSBN_PREV);
            return TRUE;

        case VK_DOWN:
            Notify(PSBN_NEXT);
            return TRUE;

        case VK_ESCAPE:
            // Esc clears the query while there is one to clear. With the field
            // already empty it is left alone on purpose, so it reaches the dialog
            // and closes the sheet the way Esc does everywhere else in it.
            if (!GetText().IsEmpty())
            {
                SetText(_T(""));
                Notify(PSBN_CLEARED);
                return TRUE;
            }
            break;
        }
    }

    return CWnd::PreTranslateMessage(pMsg);
}

void CPropTreeSearchBar::OnEditChange ()
{
    // Repainted at once even though the search is not run at once: the arrows
    // light up the moment there is a query, and making that wait for the debounce
    // would look like the bar had not noticed the keystroke.
    InvalidateRect(NULL);

    RestartDebounce();
}

void CPropTreeSearchBar::OnEditSetFocus ()
{
    m_bFieldFocus = TRUE;
    InvalidateRect(NULL);       // the border goes to the accent colour
}

void CPropTreeSearchBar::OnEditKillFocus ()
{
    m_bFieldFocus = FALSE;
    InvalidateRect(NULL);
}

void CPropTreeSearchBar::OnTimer (UINT_PTR nIDEvent)
{
    if (nIDEvent == TIMER_DEBOUNCE)
    {
        KillTimer(TIMER_DEBOUNCE);
        Notify(PSBN_TEXTCHANGED);
        return;
    }

    CWnd::OnTimer(nIDEvent);
}

////////////////////////////////////////////////////////////////////////////////
// What the sheet reads and writes

CString CPropTreeSearchBar::GetText () const
{
    CString sText;

    if (m_edit.GetSafeHwnd())
        m_edit.GetWindowText(sText);

    return sText;
}

void CPropTreeSearchBar::SetText (LPCTSTR pszText)
{
    if (!m_edit.GetSafeHwnd())
        return;

    m_edit.SetWindowText(pszText ? pszText : _T(""));

    // SetWindowText() raises EN_CHANGE just like a keystroke does, and without this
    // a text the sheet put into the bar would come back to it as PSBN_TEXTCHANGED
    // and start a search of its own.
    KillTimer(TIMER_DEBOUNCE);

    InvalidateRect(NULL);       // the arrows and the counter follow the text
}

void CPropTreeSearchBar::FocusAndSelect ()
{
    if (!m_edit.GetSafeHwnd())
        return;

    m_edit.SetFocus();

    // Selected rather than left where the caret was: Ctrl+F on a sheet that has
    // already been searched is a request for a different query, and typing over
    // the old one is what every other find box does with that key.
    m_edit.SetSel(0, -1);
}

void CPropTreeSearchBar::RestoreFocus ()
{
    // SetFocus() alone, no SetSel(): an edit keeps the caret and selection it had
    // when it lost the focus, and this is the call that follows a page switch the
    // bar itself caused. Selecting everything here would throw away the place the
    // user had got to in their own query.
    if (m_edit.GetSafeHwnd())
        m_edit.SetFocus();
}

BOOL CPropTreeSearchBar::HasFieldFocus () const
{
    return (m_edit.GetSafeHwnd() != NULL) && (::GetFocus() == m_edit.GetSafeHwnd());
}

BOOL CPropTreeSearchBar::GetIncludeDescriptions () const
{
    return m_bIncludeDesc;
}

void CPropTreeSearchBar::SetIncludeDescriptions (BOOL bInclude)
{
    // Deliberately silent. The sheet calls this to seed a bar it has just built and to
    // echo back a change the bar itself reported as PSBN_SETTINGS, so a Notify() here
    // would turn one click of the checkbox into a second search for the same click.
    m_bIncludeDesc = (bInclude ? TRUE : FALSE);
}

BOOL CPropTreeSearchBar::GetIncludeValues () const
{
    return m_bIncludeValues;
}

void CPropTreeSearchBar::SetIncludeValues (BOOL bInclude)
{
    // Silent for the reason SetIncludeDescriptions() is.
    m_bIncludeValues = (bInclude ? TRUE : FALSE);
}

BOOL CPropTreeSearchBar::GetIncludeChoices () const
{
    return m_bIncludeChoices;
}

void CPropTreeSearchBar::SetIncludeChoices (BOOL bInclude)
{
    // Silent for the reason SetIncludeDescriptions() is.
    m_bIncludeChoices = (bInclude ? TRUE : FALSE);
}

void CPropTreeSearchBar::SetCounter (int nCurrent, int nTotal)
{
    if (m_nCurrent == nCurrent && m_nTotal == nTotal)
        return;

    m_nCurrent = nCurrent;
    m_nTotal   = nTotal;

    // The whole bar, not just the counter: whether the counter is shown at all
    // depends on its measured width, and showing or hiding it moves the edit.
    InvalidateRect(NULL);
}

void CPropTreeSearchBar::SetNoMatch (BOOL bNoMatch)
{
    if (m_bNoMatch == bNoMatch)
        return;

    m_bNoMatch = bNoMatch;
    InvalidateRect(NULL);
}

////////////////////////////////////////////////////////////////////////////////
// Talking to the parent

void CPropTreeSearchBar::Notify (UINT nCode)
{
    CWnd* pParent = GetParent();

    if (pParent && pParent->GetSafeHwnd())
        pParent->SendMessage(WM_SQLT_SEARCHBAR, nCode, 0);
}

void CPropTreeSearchBar::RestartDebounce ()
{
    // SetTimer() on an id that is already running replaces it, so every keystroke
    // pushes the search further out. That is the point: the sheet searches once per
    // pause in typing rather than once per character, and one of its searches walks
    // every page it has - the first one has to create them all first.
    SetTimer(TIMER_DEBOUNCE, DEBOUNCE_MS, NULL);
}

////////////////////////////////////////////////////////////////////////////////

BEGIN_MESSAGE_MAP (CPropTreeSearchBar, CWnd)
    ON_WM_DESTROY   ()
    ON_WM_PAINT     ()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE      ()
    ON_WM_TIMER     ()
    ON_WM_MOUSEMOVE ()
    ON_WM_MOUSELEAVE()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP ()
    ON_WM_CTLCOLOR  ()

    // The settings menu is tracked against this window, so its one command comes back
    // here rather than to the sheet.
    ON_COMMAND (IDM_INCLUDE_DESC,   OnMenuIncludeDesc)
    ON_COMMAND (IDM_INCLUDE_VALUES,  OnMenuIncludeValues)
    ON_COMMAND (IDM_INCLUDE_CHOICES, OnMenuIncludeChoices)

    // EN_* arrive here because the edit's parent is the bar, and EN_SETFOCUS /
    // EN_KILLFOCUS are how it learns the caret came and went without subclassing.
    ON_EN_CHANGE   (IDC_BAR_EDIT, OnEditChange)
    ON_EN_SETFOCUS (IDC_BAR_EDIT, OnEditSetFocus)
    ON_EN_KILLFOCUS(IDC_BAR_EDIT, OnEditKillFocus)
END_MESSAGE_MAP ()

}; // namespace Common

