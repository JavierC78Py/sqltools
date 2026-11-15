/*
    SQLTools is a tool for Oracle database developers and DBAs.
    Copyright (C) 1997-2017 Aleksey Kochetov

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

/***************************************************************************/
/*      Purpose: Grid implementation                                       */
/*      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~            */
/*      (c) 1996-1999,2003,2017 Aleksey Kochetov                           */
/***************************************************************************/

// 27.10.2003 bug fix, export settings do not affect on quick html/csv viewer launch
// 16.02.2002 bug fix, exception on scrolling if grid contains more then 32K rows
// 22.03.2004 bug fix, CreareAs file property is ignored for "Open In Editor", "Query", etc (always in Unix format)
// 16.12.2004 (Ken Clubok) Add CSV prefix option
// 2017-11-28 implemented range selection
// 2017-11-28 added 3 new export formats

#include "stdafx.h"
#include <list>
#include <fstream>
#include <sstream>
#include <regex> // used in GridView::DoFileSave
#include "GridManager.h"
#include "GridView.h"
#include "GridSourceBase.h"
#include "XlsxCellMap.h" // OG2::FileBaseName, for the worksheet name an XLSX export gives its sheet
#include "COMMON/TempFilesManager.h"
#include "COMMON/AppUtilities.h"
#include <COMMON/AppGlobal.h> // Global::SetStatusText, for the JD9 truncation warning on export
//#include "COMMON/GUICommandDictionary.h"
#include "PopupViewer.h"

// the following section is included only for OnGridSettings
#include "Dialogs/GridPageTree.h"
#include "Dialogs/PropServerScriptPageTree.h"
#include "OpenEditor/VisualAttributesPageTree.h"
#include "COMMON/PropertySheetMem.h"
#include "COMMON/FilenameTemplate.h"

#include "SQLWorksheet/SQLWorksheetDoc.h" // it's bad because of dependency on CPLSWorksheetDoc

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace OG2 /* OG = OpenGrid V2 */
{

    UINT GridView::m_uWheelScrollLines; // cached value for MS Weel support
    static LPCWSTR g_szClassName = L"Kochware.OpenGrid.V2";

    IMPLEMENT_DYNCREATE(GridView, CView)
    IMPLEMENT_DYNCREATE(StrGridView, GridView)


StrGridView::StrGridView ()
{
    m_pStrSource = new GridStringSource;
    m_pManager = new GridManager(this, m_pStrSource, TRUE);
    m_pManager->m_Options.m_Editing = true;
    m_ShouldDeleteManager = true;
}

void StrGridView::Clear ()
{
    m_pStrSource->DeleteAll();
    m_pManager->Clear();
}

GridView::GridView()
: m_Erase(FALSE),
  m_pManager(0),
  m_ShouldDeleteManager(FALSE),
  m_toolbarNavigationDir(edVert),
  m_uPopupMenuId(IDR_GRID_POPUP),
  m_uHeaderPopupMenuId(0),
  m_uExpFileCounter(0),
  m_menuTarget(mtNone),
  m_menuSortColumn(-1),
  m_menuCellRow(-1),
  m_menuCellCol(-1)
{
    GetSQLToolsSettings().AddSubscriber(this);
}

GridView::~GridView()
{
    try { EXCEPTION_FRAME;

        GetSQLToolsSettings().RemoveSubscriber(this);

        if (m_ShouldDeleteManager)
            delete m_pManager;

        PopupFrameWnd::DestroyPopup(this);
    }
    _DESTRUCTOR_HANDLER_;
}

bool GridView::IsEmpty () const
{
    _ASSERTE(m_pManager && GetGridSource());
    return GetGridSource()->IsEmpty();
}

void GridView::setExportSettings (const SQLToolsSettings& setting)
{
    _ASSERTE(m_pManager && GetGridSource());

    IGridTextSource* source = dynamic_cast<IGridTextSource*>(GetGridSource());
    if (!source)
        return;

    switch (setting.GetGridExpFormat())
    {
    case 0: source->SetOutputFormat(etfPlainText);           break;
    case 1: source->SetOutputFormat(etfQuoteDelimited);      break;
    case 2: source->SetOutputFormat(etfTabDelimitedText);    break;
    case 3: source->SetOutputFormat(etfXmlElem);             break;
    case 4: source->SetOutputFormat(etfJson);                break;
    case 5: source->SetOutputFormat(etfHtml);                break;
    case 6: source->SetOutputFormat(etfPLSQL1);              break;
    case 7: source->SetOutputFormat(etfPLSQL2);              break;
    case 8: source->SetOutputFormat(etfPLSQL3);              break;
    case 9: source->SetOutputFormat(etfPLSQL4);              break;
    case 10: source->SetOutputFormat(etfXlsx);               break;   // XE1
    default: _ASSERTE(0);
    }

    source->SetFieldDelimiterChar (setting.GetGridExpCommaChar()[0]);
    source->SetQuoteChar          (setting.GetGridExpQuoteChar()[0]);
    source->SetQuoteEscapeChar    (setting.GetGridExpQuoteEscapeChar()[0]);
    source->SetPrefixChar         (setting.GetGridExpPrefixChar()[0]);
    source->SetOutputWithHeader   (setting.GetGridExpWithHeader());
    source->SetJsonPretty         (setting.GetGridExpJsonPretty());

    source->SetXmlRootElement      (setting.GetGridExpXmlRootElement());
    source->SetXmlRecordElement    (setting.GetGridExpXmlRecordElement());
    source->SetXmlFieldElementCase (setting.GetGridExpXmlFieldElementCase());

    // XE10: no file name is known here, so no sheet name is either; DoFileSave sets the one the user
    // chose. Clearing it on every settings push is what keeps a name from outliving its own export.
    source->SetXlsxSheetName       (std::wstring());
}

void GridView::getSelectionForExport (int& row, int& nrows, int& col, int& ncols) const
{
    row = nrows = col = ncols = -1;

    if (!m_pManager->IsRangeSelectionEmpty())
    {
        int first, last;
        if (m_pManager->m_Rulers[edVert].GetNormalizedSelection(first, last))
        {
            row = first;
            nrows = last - first + 1;
        }
        if (m_pManager->m_Rulers[edHorz].GetNormalizedSelection(first, last))
        {
            col = first;
            ncols = last - first + 1;
        }
    }
    else
    {
        row = m_pManager->m_Rulers[edVert].GetCurrent();
        col = m_pManager->m_Rulers[edHorz].GetCurrent();
        nrows = ncols = 1;
    }
}

bool GridView::GetSelectedCellRange (GridCellRange& range) const
{
    if (!m_pManager || !GetGridSource())
        return false;

    getSelectionForExport(range.row, range.nrows, range.col, range.ncols);

    if (range.row < 0 || range.col < 0 || range.nrows < 1 || range.ncols < 1)
        return false;

    return range.row + range.nrows <= GetGridSourceCount(edVert)
        && range.col + range.ncols <= GetGridSourceCount(edHorz);
}

void GridView::notifyCtrlLeftClick ()
{
    if (!m_ctrlLeftClickFn)
        return;

    GridCellRange range;

    if (!GetSelectedCellRange(range))
        return;

    m_ctrlLeftClickFn(*this, range);
}

BEGIN_MESSAGE_MAP(GridView, CView)
    //{{AFX_MSG_MAP(GridView)
    ON_WM_CREATE()
    ON_WM_DESTROY()
    ON_WM_SIZE()
    ON_WM_HSCROLL()
    ON_WM_VSCROLL()
    ON_WM_GETDLGCODE()
    ON_WM_CTLCOLOR()
    ON_WM_PAINT()
    ON_WM_SETFOCUS()
    ON_WM_KILLFOCUS()
    ON_WM_KEYDOWN()
    ON_WM_LBUTTONDBLCLK()
    ON_WM_LBUTTONDOWN()
    ON_WM_MBUTTONDOWN()
    ON_WM_LBUTTONUP()
    ON_WM_MOUSEMOVE()
    ON_WM_TIMER()
    ON_COMMAND(ID_EDIT_COPY, OnEditCopy)
    ON_COMMAND(ID_EDIT_PASTE, OnEditPaste)
    ON_COMMAND(ID_EDIT_CUT, OnEditCut)
    ON_UPDATE_COMMAND_UI(ID_EDIT_COPY, OnUpdateEditGroup)
    ON_UPDATE_COMMAND_UI(ID_EDIT_PASTE, OnUpdateEditGroup)
    ON_UPDATE_COMMAND_UI(ID_EDIT_CUT, OnUpdateEditGroup)

    ON_COMMAND(ID_GRID_COPY_AS_TEXT,             OnGridCopyAsText            )
    ON_COMMAND(ID_GRID_COPY_AS_PLSQL_VALUES,     OnGridCopyAsPlsqlValues     )
    ON_COMMAND(ID_GRID_COPY_AS_PLSQL_EXPRESSION, OnGridCopyAsPlsqlExpression )
    ON_COMMAND(ID_GRID_COPY_AS_PLSQL_IN_LIST,    OnGridCopyAsPlsqlInList     )
    ON_COMMAND(ID_GRID_COPY_AS_PLSQL_INSERTS,    OnGridCopyAsPlsqlInserts    )
    ON_UPDATE_COMMAND_UI_RANGE(ID_GRID_COPY_AS_FIRST, ID_GRID_COPY_AS_LAST, OnUpdateEditGroup)

    ON_WM_RBUTTONDOWN()
    ON_WM_MOUSEWHEEL()
    //}}AFX_MSG_MAP
    ON_MESSAGE(WM_GETFONT, OnGetFont)
    ON_WM_CONTEXTMENU()
    ON_WM_SETTINGCHANGE()
    ON_WM_INITMENUPOPUP()

    ON_COMMAND(ID_GRID_OUTPUT_OPTIONS, OnGridOutputOptions)
    ON_COMMAND(ID_GRID_OUTPUT_EXPORT, OnFileExport)
    ON_COMMAND(ID_GRID_OUTPUT_EXPORT_AND_OPEN, OnGridOutputExportAndOpen)

    ON_COMMAND(ID_GRID_COPY_COL_HEADER, OnGridCopyColHeader)
    ON_COMMAND(ID_GRID_COPY_HEADERS, OnGridCopyHeaders)
    ON_COMMAND(ID_GRID_COPY_ROW, OnGridCopyRow)
    ON_COMMAND(ID_GRID_COPY_ALL, OnGridCopyAll)
    ON_COMMAND(ID_GRID_COPY_COL, OnGridCopyCol)

    ON_UPDATE_COMMAND_UI(ID_GRID_OUTPUT_EXPORT, OnUpdateEditGroup)
    ON_UPDATE_COMMAND_UI(ID_GRID_OUTPUT_EXPORT_AND_OPEN, OnUpdateEditGroup)

    ON_UPDATE_COMMAND_UI(ID_GRID_COPY_ALL, OnUpdateEditGroup)
    ON_UPDATE_COMMAND_UI(ID_GRID_COPY_COL_HEADER, OnUpdate_CopyHeaders)
    ON_UPDATE_COMMAND_UI(ID_GRID_COPY_HEADERS, OnUpdate_CopyHeaders)
    ON_UPDATE_COMMAND_UI(ID_GRID_COPY_ROW, OnUpdate_GridHeaderItem)
    ON_UPDATE_COMMAND_UI(ID_GRID_COPY_COL, OnUpdate_GridHeaderItem)

    ON_COMMAND(ID_GRID_CLEAR_SELECTION, OnGridClearSelection)
    ON_UPDATE_COMMAND_UI(ID_GRID_CLEAR_SELECTION, OnUpdate_GridClearSelection)

    ON_COMMAND(ID_GRID_OUTPUT_OPEN, OnGridOutputOpen)
    ON_UPDATE_COMMAND_UI(ID_GRID_OUTPUT_OPEN, OnUpdateEditGroup)
    ON_WM_SYSCOLORCHANGE()

    ON_COMMAND(ID_GRID_MOVE_UP,      OnMoveUp)
    ON_COMMAND(ID_GRID_MOVE_DOWN,    OnMoveDown)
    ON_COMMAND(ID_GRID_MOVE_PGUP,    OnMovePgUp)
    ON_COMMAND(ID_GRID_MOVE_PGDOWN,  OnMovePgDown)
    ON_COMMAND(ID_GRID_MOVE_END,     OnMoveEnd)
    ON_COMMAND(ID_GRID_MOVE_HOME,    OnMoveHome)

    ON_UPDATE_COMMAND_UI(ID_GRID_MOVE_UP,     OnUpdate_MoveUp    )
    ON_UPDATE_COMMAND_UI(ID_GRID_MOVE_DOWN,   OnUpdate_MoveDown  )
    ON_UPDATE_COMMAND_UI(ID_GRID_MOVE_PGUP,   OnUpdate_MovePgUp  )
    ON_UPDATE_COMMAND_UI(ID_GRID_MOVE_PGDOWN, OnUpdate_MovePgDown)
    ON_UPDATE_COMMAND_UI(ID_GRID_MOVE_END,    OnUpdate_MoveEnd   )
    ON_UPDATE_COMMAND_UI(ID_GRID_MOVE_HOME,   OnUpdate_MoveHome  )

    ON_COMMAND(ID_GRID_CLEAR_RECORDS, OnGridClearRecords)
    ON_UPDATE_COMMAND_UI(ID_GRID_CLEAR_RECORDS, OnUpdate_GridClearRecords)

    ON_COMMAND(ID_GRID_ROTATE, OnRotate)
    ON_UPDATE_COMMAND_UI(ID_GRID_ROTATE, OnUpdate_Rotate)

    ON_COMMAND_RANGE(ID_GRID_SORT_FIRST, ID_GRID_SORT_LAST, OnGridSort)
    ON_UPDATE_COMMAND_UI_RANGE(ID_GRID_SORT_FIRST, ID_GRID_SORT_LAST, OnUpdate_GridSort)

    ON_COMMAND(ID_GRID_CLEAR_SORT, OnGridClearSort)
    ON_UPDATE_COMMAND_UI(ID_GRID_CLEAR_SORT, OnUpdate_GridClearSort)

    ON_COMMAND(ID_GRID_CELL_EDIT,     OnGridCellEdit     )
    ON_COMMAND(ID_GRID_ROW_INSERT,    OnGridRowInsert    )
    ON_COMMAND(ID_GRID_ROW_DUPLICATE, OnGridRowDuplicate )
    ON_COMMAND(ID_GRID_ROW_POST,      OnGridRowPost      )
    ON_COMMAND(ID_GRID_ROW_DISCARD,   OnGridRowDiscard   )
    ON_COMMAND(ID_GRID_ROW_DELETE,    OnGridRowDelete    )
    ON_UPDATE_COMMAND_UI(ID_GRID_CELL_EDIT,     OnUpdate_GridCellEdit     )
    ON_UPDATE_COMMAND_UI(ID_GRID_ROW_INSERT,    OnUpdate_GridRowInsert    )
    ON_UPDATE_COMMAND_UI(ID_GRID_ROW_DUPLICATE, OnUpdate_GridRowDuplicate )
    ON_UPDATE_COMMAND_UI(ID_GRID_ROW_POST,      OnUpdate_GridRowPost      )
    ON_UPDATE_COMMAND_UI(ID_GRID_ROW_DISCARD,   OnUpdate_GridRowDiscard   )
    ON_UPDATE_COMMAND_UI(ID_GRID_ROW_DELETE,    OnUpdate_GridRowDelete    )

    ON_COMMAND(ID_GRID_SETTINGS, OnGridSettings)
    ON_UPDATE_COMMAND_UI(ID_GRID_SETTINGS, OnUpdate_GridSettings)

    ON_COMMAND(ID_GRID_OPEN_WITH_IE, OnGridOpenWithIE)
    ON_COMMAND(ID_GRID_OPEN_WITH_EXCEL, OnGridOpenWithExcel)
    ON_UPDATE_COMMAND_UI(ID_GRID_OPEN_WITH_IE, OnUpdateEditGroup)
    ON_UPDATE_COMMAND_UI(ID_GRID_OPEN_WITH_EXCEL, OnUpdateEditGroup)

    ON_COMMAND(ID_GRID_POPUP, OnGridPopup)
    ON_UPDATE_COMMAND_UI(ID_GRID_POPUP, OnUpdate_GridPopup)

END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// GridView message handlers

BOOL GridView::PreCreateWindow(CREATESTRUCT& cs)
{
    WNDCLASS wndClass;
    BOOL bRes = CView::PreCreateWindow(cs);
    HINSTANCE hInstance = AfxGetInstanceHandle();

    // see if the class already exists
    if (!::GetClassInfo(hInstance, g_szClassName, &wndClass))
    {
        // get default stuff
        ::GetClassInfo(hInstance, cs.lpszClass, &wndClass);
        wndClass.lpszClassName = g_szClassName;
        wndClass.style &= CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wndClass.hbrBackground = 0;
        // register a new class
        if (!AfxRegisterClass(&wndClass))
            AfxThrowResourceException();
    }
    cs.lpszClass = g_szClassName;

    return bRes;
}

LRESULT GridView::OnGetFont (WPARAM, LPARAM)
{
    if (m_paintAccessories.get() && (HFONT)m_paintAccessories->m_Font)
    {
        return (LRESULT)(HFONT)m_paintAccessories->m_Font;
    }
    else if (GetParent())
    {
        return GetParent()->SendMessage(WM_GETFONT, 0, 0L);
    }
    else
        return (LRESULT)::GetStockObject(DEFAULT_GUI_FONT);
}

void GridView::OnPrepareDC(CDC* pDC, CPrintInfo* pInfo)
{
    CView::OnPrepareDC(pDC, pInfo);

    if (m_paintAccessories.get() && (HFONT)m_paintAccessories->m_Font)
    {
        pDC->SelectObject(&m_paintAccessories->m_Font);
    }
    else if (GetParent())
    {
        HFONT hfont = (HFONT)GetParent()->SendMessage(WM_GETFONT, 0, 0L);
        if (hfont)
            pDC->SelectObject(hfont);
    }
    else
        pDC->SelectObject(CFont::FromHandle((HFONT)::GetStockObject(DEFAULT_GUI_FONT)));
}

int GridView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
    if (m_pManager == 0 || CView::OnCreate(lpCreateStruct) == -1)
        return -1;

    m_paintAccessories = CGridViewPaintAccessories::GetPaintAccessories(GetVisualAttributeSetName());
    m_paintAccessories->OnSettingsChanged(GetSQLToolsSettings().GetVASet(GetVisualAttributeSetName()));

    CRect rect;
    GetClientRect(rect);

    TEXTMETRIC tm;
    CClientDC dc(this);
    OnPrepareDC(&dc, 0);
    dc.GetTextMetrics(&tm);

    CSize size((2 * tm.tmAveCharWidth + tm.tmMaxCharWidth) / 3, tm.tmHeight);
    m_pManager->EvOpen(rect, size);

    SetTimer(1, 100, NULL);

    return 0;
}

void GridView::OnDestroy()
{
    m_pManager->EvClose();

    KillTimer(1);

    CView::OnDestroy();
}

void GridView::OnSize(UINT nType, int cx, int cy)
{
    CView::OnSize(nType, cx, cy);

    if (cx > 0 && cy > 0
    && (nType == SIZE_MAXIMIZED || nType == SIZE_RESTORED))
    {
        m_pManager->EvSize(CSize(cx, cy));
        Invalidate(TRUE);
        m_Erase = TRUE;
    }
}

void GridView::OnHScroll(UINT nSBCode, UINT /*nPos*/, CScrollBar*)
{
// 16.02.2002 bug fix, exception on scrolling if grid contains more then 32K rows
    SCROLLINFO scrollInfo;
    scrollInfo.cbSize = sizeof(scrollInfo);
    scrollInfo.fMask = SIF_TRACKPOS;
    GetScrollInfo(SB_HORZ, &scrollInfo);

    m_pManager->EvScroll(edHorz, nSBCode, scrollInfo.nTrackPos);
}

void GridView::OnVScroll(UINT nSBCode, UINT /*nPos*/, CScrollBar*)
{
// 16.02.2002 bug fix, exception on scrolling if grid contains more then 32K rows
    SCROLLINFO scrollInfo;
    scrollInfo.cbSize = sizeof(scrollInfo);
    scrollInfo.fMask = SIF_TRACKPOS;
    GetScrollInfo(SB_VERT, &scrollInfo);

    m_pManager->EvScroll(edVert, nSBCode, scrollInfo.nTrackPos);
}

UINT GridView::OnGetDlgCode()
{
    return ((!m_pManager->m_Options.m_Tabs) ? 0 : DLGC_WANTTAB)
             | DLGC_WANTARROWS | DLGC_WANTCHARS
             | CView::OnGetDlgCode();
}

HBRUSH GridView::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
    if (nCtlColor == CTLCOLOR_EDIT
    || nCtlColor == CTLCOLOR_STATIC)
    {
        pDC->SetBkColor(RGB(255,255,255));
        return (HBRUSH)::GetStockObject(WHITE_BRUSH);
    }
    else
        return CView::OnCtlColor(pDC, pWnd, nCtlColor);
}

void GridView::OnPaint()
{
    CPaintDC dc(this);
    OnPrepareDC(&dc, 0);

    m_pManager->Paint(dc, (m_Erase && dc.m_ps.fErase), dc.m_ps.rcPaint);

    m_Erase = FALSE;
}

void GridView::OnSetFocus(CWnd* pOldWnd)
{
    m_pManager->EvSetFocus(TRUE);

    CView::OnSetFocus(pOldWnd);

    SendMessage(WM_NCACTIVATE, TRUE);

    // don't recall why I added this but it looks wrong now
    //if (GetParent())
    //    GetParent()->SendNotifyMessage(GetDlgCtrlID(), 1, (LPARAM)this);
}

void GridView::OnKillFocus(CWnd* pNewWnd)
{
    m_pManager->EvSetFocus(FALSE);

    CView::OnKillFocus(pNewWnd);

    SendMessage(WM_NCACTIVATE, FALSE);

    // don't recall why I added this but it looks wrong now
    //if (GetParent())
    //    GetParent()->SendNotifyMessage(GetDlgCtrlID(), 2, (LPARAM)this);
}

void GridView::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    if (!m_pManager->EvKeyDown(nChar, nRepCnt, nFlags))
        CView::OnKeyDown(nChar, nRepCnt, nFlags);
}

BOOL GridView::PreTranslateMessage(MSG* pMsg)
{
    // Runs outside WindowProc, so it needs a frame of its own: EvKeyDown reaches CloseEditor, which
    // asserts one. A failed key is consumed, so OnKeyDown does not report it a second time.
    try { EXCEPTION_FRAME;

        // The frame translates its accelerator table only once the walk up from the focused window
        // reaches it, so claiming a key here is what lets the grid see it. Covers the editor too.
        if (pMsg->message == WM_KEYDOWN && m_pManager
            && m_pManager->ClaimKey(static_cast<UINT>(pMsg->wParam)))
        {
            m_pManager->EvKeyDown(static_cast<UINT>(pMsg->wParam), 1, 0);
            return TRUE;
        }

        return CView::PreTranslateMessage(pMsg);
    }
    _DEFAULT_HANDLER_;

    return TRUE;
}

void GridView::OnLButtonDblClk(UINT nFlags, CPoint point)
{
    m_pManager->EvLButtonDblClk(nFlags, point);
}

    const int DRAGGING_THRESHOLD = 3;

void GridView::OnLButtonDown (UINT nFlags, CPoint point)
{
    m_lastClickRect.TopLeft() = point;
    m_lastClickRect.TopLeft().Offset(-DRAGGING_THRESHOLD, -DRAGGING_THRESHOLD);
    m_lastClickRect.BottomRight() = point;
    m_lastClickRect.BottomRight().Offset(DRAGGING_THRESHOLD, DRAGGING_THRESHOLD);

    if (!m_pManager->IsPointOverRangeSelection(point))
        m_pManager->EvLButtonDown(nFlags, point);

    // Alt+click raises the Popup Viewer. Cached from the click and moved outside the range guard so
    // it also fires inside a range selection and names the clicked cell, not a stale one.
    if ((0xFF00 & GetKeyState(VK_MENU)))
    {
        CacheMenuTarget(point);
        OnGridPopup();
    }

    if ((nFlags & MK_CONTROL) && !(nFlags & MK_SHIFT) && m_pManager->IsDataCell(point))
        notifyCtrlLeftClick();
}

void GridView::OnMButtonDown (UINT /*nFlags*/, CPoint point)
{
    // Middle-click is purely a "read the value" action: it raises the Popup Viewer for the clicked
    // cell and never mutates the selection, so there are no modifiers to discard.
    CacheMenuTarget(point);
    OnGridPopup();
}

void GridView::OnLButtonUp(UINT nFlags, CPoint point)
{
    CView::OnLButtonUp(nFlags, point);

    if (m_pManager->IsPointOverRangeSelection(point)
    && m_lastClickRect.PtInRect(point))
        m_pManager->NavGridManager::EvLButtonDown(nFlags, point);

    m_pManager->EvLButtonUp(nFlags, point);
}

// check the history for tooltips support 
void GridView::OnMouseMove (UINT nFlags, CPoint point)
{
    m_pManager->EvMouseMove(nFlags, point);

    if ((nFlags & MK_LBUTTON) && !m_pManager->ResizingProcess()) // Drag & Drop
    {
        if (/*m_pManager->IsPointOverRangeSelection(point)
        && */!m_lastClickRect.PtInRect(point))
        {
            setExportSettings(exportSettingsFor(eslotClipboard)); // to beginning of drag

            int row, nrows, col, ncols;
            getSelectionForExport(row, nrows, col, ncols);

            if (GetGridSource())
            {
                if (m_pManager->IsFixedCorner(point))
                    GetGridSource()->DoDragDrop(row, 1, col, 1, ecDragTopLeftCorner);
                else if (m_pManager->IsFixedCol(point.x))
                {
                    if (row < GetGridSource()->GetCount(edVert))
                        GetGridSource()->DoDragDrop(row, nrows, -1, -1, ecDragRowHeader);
                }
                else if (m_pManager->IsFixedRow(point.y))
                {
                    if (col < GetGridSource()->GetCount(edHorz))
                        GetGridSource()->DoDragDrop(-1, -1, col, ncols, ecDragColumnHeader);
                }
                else if (m_pManager->IsDataCell(point))
                    GetGridSource()->DoDragDrop(row, nrows, col, ncols, ecDataCell);
            }
        }
    }

    CView::OnMouseMove(nFlags, point);
}

void GridView::OnTimer (UINT_PTR)
{
    m_pManager->IdleAction();
}

void GridView::OnSettingsChanged ()
{
    m_paintAccessories = CGridViewPaintAccessories::GetPaintAccessories(GetVisualAttributeSetName());
    m_paintAccessories->OnSettingsChanged(GetSQLToolsSettings().GetVASet(GetVisualAttributeSetName()));

    TEXTMETRIC tm;
    CClientDC dc(this);
    OnPrepareDC(&dc, 0);
    dc.GetTextMetrics(&tm);

    CSize size((2 * tm.tmAveCharWidth + tm.tmMaxCharWidth) / 3, tm.tmHeight);
    m_pManager->EvFontChanged(size);
}

LRESULT GridView::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    try { EXCEPTION_FRAME;

        return CView::WindowProc(message, wParam, lParam);
    }
    _DEFAULT_HANDLER_;
    return 0;
}

BOOL GridView::OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo)
{
    try { EXCEPTION_FRAME;

        CWnd* pInplaceEditor = m_pManager->GetEditor();

        if (pInplaceEditor != NULL && pInplaceEditor->OnCmdMsg(nID, nCode, pExtra, pHandlerInfo))
            return TRUE;

        // then pump through frame
        if (CView::OnCmdMsg(nID, nCode, pExtra, pHandlerInfo))
            return TRUE;

        return FALSE;
    }
    _DEFAULT_HANDLER_;

    return TRUE;
}

void GridView::OnContextMenu (CWnd*, CPoint pos)
{
    if (GetGridSource())
    {
        if (pos.x == -1 && pos.y == -1) // invoked by keyboard
        {
            // Nothing to classify, so fall back to the data-cell menu and its behaviour. A -1 cell
            // means "no click to honour", which is what makes OnGridPopup use the current cell here.
            m_menuTarget     = mtDataCell;
            m_menuSortColumn = -1;
            m_menuCellRow    = -1;
            m_menuCellCol    = -1;

            if (m_pManager->IsCurrentVisible()) 
            {
                CRect rc;
                m_pManager->GetCurRect(rc);
                ClientToScreen(rc);
                pos.x = rc.left + 3;
                pos.y = rc.bottom;
            }
            else
            {
                int loc[2];
                m_pManager->m_Rulers[edHorz].GetDataArea(loc);
                pos.x = loc[0];
                m_pManager->m_Rulers[edVert].GetDataArea(loc);
                pos.y = loc[0];
                ClientToScreen(&pos);
            }
        }
        // Two menus, not one per strip: the header one covers column, row and corner alike, and its
        // update handlers decide what applies. A view that never sets one keeps the data-cell menu.
        const bool headerContext = m_uHeaderPopupMenuId != 0
                                && m_menuTarget != mtNone && m_menuTarget != mtDataCell;

        CMenu menu;
        VERIFY(menu.LoadMenu(headerContext ? m_uHeaderPopupMenuId : m_uPopupMenuId));
        CMenu* pPopup = menu.GetSubMenu(0);
        ASSERT(pPopup != NULL);

        // MFC never runs this loop for a view-owned popup, so the update handlers do not fire and
        // every GRAYED item stays gray. Do not set m_pSubMenu: CCmdUI::Enable returns early if it is.
        CCmdUI cmdUI;
        cmdUI.m_pOther     = NULL;
        cmdUI.m_pMenu      = pPopup;
        cmdUI.m_nIndexMax  = pPopup->GetMenuItemCount();

        for (UINT i = 0; i < cmdUI.m_nIndexMax; i++)
        {
            cmdUI.m_nIndex = i;
            cmdUI.m_nID    = pPopup->GetMenuItemID(i);
            cmdUI.DoUpdate(this, FALSE);

            // Walk submenus (1 level)
            CMenu* pSubMenu = pPopup->GetSubMenu(i);
            if (pSubMenu != NULL)
            {
                UINT nSubItemCount = pSubMenu->GetMenuItemCount();
                for (UINT j = 0; j < nSubItemCount; j++)
                {
                    cmdUI.m_nIndex = j;
                    cmdUI.m_pMenu  = pSubMenu;
                    cmdUI.m_nID    = pSubMenu->GetMenuItemID(j);
                    cmdUI.DoUpdate(this, FALSE);
                }
                // Restore for next iteration
                cmdUI.m_pMenu = pPopup;
            }
        }

        //Common::GUICommandDictionary::AddAccelDescriptionToMenu(pPopup->m_hMenu);
        pPopup->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, pos.x, pos.y, this);
    }
}

void GridView::OnInitMenuPopup (CMenu* pPopupMenu, UINT nIndex, BOOL bSysMenu)
{
    // No hardcoded EnableMenuItem list here. Enablement lives only in the manual CCmdUI loop in
    // OnContextMenu, so there is one source of truth rather than two that can disagree.
    CView::OnInitMenuPopup(pPopupMenu, nIndex, bSysMenu);
}

    struct ClipboardGuard
    {
        CWnd* m_pWnd;
        bool  m_open;

        ClipboardGuard (CWnd* pWnd) : m_pWnd(pWnd), m_open(false) {}
        ~ClipboardGuard () { CloseClipboard(); }

        bool OpenClipboard ()
        {
            if (!m_open)
            {
                if (m_pWnd->OpenClipboard())
                    m_open = true;
            }
            return m_open;
        }

        bool CloseClipboard ()
        {
            if (m_open)
            {
                ::CloseClipboard();
                m_open = false;
            }
            return m_open;
        }
    };

void GridView::DoEditCopy (const SQLToolsSettings& setting, int format)
{
    CWaitCursor wait;
    ClipboardGuard cg(this);

    if (GetGridSource()
    && cg.OpenClipboard())
    {
        setExportSettings(setting);

        int row, nrows, col, ncols;
        getSelectionForExport(row, nrows, col, ncols);

        GetGridSource()->DoEditCopy(row, nrows, col, ncols, ecDataCell, format);
        cg.CloseClipboard();
    }
}

void GridView::OnEditCopy ()
{
    SQLToolsSettings settings = exportSettingsFor(eslotClipboard);

    if (runExportSheetForSlot(settings, L"Copy", eslotClipboard))
    {
        DoEditCopy(settings, etfDefault);
    }
}

// "Copy As" never asks: the chosen format wins, and the Clipboard slot supplies the rest.
void GridView::OnGridCopyAsText ()
{
    DoEditCopy(exportSettingsFor(eslotClipboard), etfPlainText);
}

void GridView::OnGridCopyAsPlsqlValues ()
{
    DoEditCopy(exportSettingsFor(eslotClipboard), etfPLSQL1);
}

void GridView::OnGridCopyAsPlsqlExpression ()
{
    DoEditCopy(exportSettingsFor(eslotClipboard), etfPLSQL2);
}

void GridView::OnGridCopyAsPlsqlInList ()
{
    DoEditCopy(exportSettingsFor(eslotClipboard), etfPLSQL3);
}

void GridView::OnGridCopyAsPlsqlInserts ()
{
    DoEditCopy(exportSettingsFor(eslotClipboard), etfPLSQL4);
}

void GridView::OnEditPaste ()
{
    //GetGridSource()->DoEditPaste();
}

void GridView::OnEditCut ()
{
    //GetGridSource()->DoEditCut();
}

void GridView::OnUpdateEditGroup(CCmdUI* pCmdUI)
{
    if (!IsEmpty()
    && GetGridSource()
    && GetGridSourceCount(edHorz) > 0
    && GetGridSourceCount(edVert) > 0)
        pCmdUI->Enable(TRUE);
    else
        pCmdUI->Enable(FALSE);
}

void GridView::OnUpdate_CopyHeaders(CCmdUI* pCmdUI)
{
    if (!IsEmpty()
    && GetGridSource()
    && GetGridSourceCount(edHorz) > 0)
        pCmdUI->Enable(TRUE);
    else
        pCmdUI->Enable(FALSE);
}

// Axis-specific copy verbs, offered only on their own axis. Off the headers - the data-cell menu and
// the keyboard fallback - both stay available, exactly as they did before the menus were split.
void GridView::OnUpdate_GridHeaderItem(CCmdUI* pCmdUI)
{
    bool applicable = true;

    if (m_menuTarget == mtColumnHeader)
        applicable = (pCmdUI->m_nID == ID_GRID_COPY_COL);
    else if (m_menuTarget == mtRowHeader)
        applicable = (pCmdUI->m_nID == ID_GRID_COPY_ROW);
    else if (m_menuTarget == mtCorner)
        applicable = false;   // the corner names neither a single column nor a single row

    pCmdUI->Enable(applicable
                && !IsEmpty()
                && GetGridSource()
                && GetGridSourceCount(edHorz) > 0
                && GetGridSourceCount(edVert) > 0);
}

// Ruler state only, so unlike Select All this never reaches the source and cannot start a fetch.
void GridView::OnGridClearSelection ()
{
    if (m_pManager)
    {
        m_pManager->m_Rulers[edHorz].ResetSelection();
        m_pManager->m_Rulers[edVert].ResetSelection();
        m_pManager->RepaintAll();
    }
}

void GridView::OnUpdate_GridClearSelection (CCmdUI* pCmdUI)
{
    pCmdUI->Enable(m_pManager != nullptr && !IsEmpty() && !m_pManager->IsRangeSelectionEmpty());
}

// One classification of a click, shared by the right-click menu and the Alt/middle-click Popup Viewer.
// Neither can take it from GetCurrent(): a click on an existing selection skips EvLButtonDown entirely.
void GridView::CacheMenuTarget (const CPoint& point)
{
    m_menuTarget     = mtDataCell;
    m_menuSortColumn = -1;
    m_menuCellRow    = -1;
    m_menuCellCol    = -1;

    if (m_pManager->IsFixedCorner(point))
        m_menuTarget = mtCorner;
    else if (m_pManager->IsFixedRow(point.y))
    {
        m_menuTarget     = mtColumnHeader;
        m_menuSortColumn = m_pManager->PointToPosition(edHorz, point);
    }
    else if (m_pManager->IsFixedCol(point.x))
        m_menuTarget = mtRowHeader;

    // -1 wherever the click was not on data: a header, the corner, or the empty region past the last
    // row or column. That is what makes OnGridPopup refuse a header instead of showing GetCurrent().
    m_menuCellRow = m_pManager->PointToPosition(edVert, point);
    m_menuCellCol = m_pManager->PointToPosition(edHorz, point);
}

void GridView::OnRButtonDown(UINT nFlags, CPoint point)
{
    // Classify before anything can move: the menu is built later from this, and Sort has to be told
    // which column was clicked instead of reading it back from a current cell that may not have moved.
    CacheMenuTarget(point);

    // A header that already is the selection is left alone. Rebuilding it here is what used to
    // collapse a multi-column pick to the single column under the cursor before the menu opened.
    bool hit = m_pManager->IsPointOverRangeSelection(point)
            || m_pManager->IsPointOverSelectedHeader(point);

    if (!hit)
        // Right-click is a targeting gesture, not a selection gesture, so the modifier keys are
        // ignored and Shift+RClick no longer extends the selection before the menu opens.
        m_pManager->EvLButtonDown(nFlags & ~(MK_SHIFT | MK_CONTROL), point);

    CView::OnRButtonDown(nFlags, point);
}

void GridView::OnFileExport ()
{
    SQLToolsSettings settings = exportSettingsFor(eslotFile);

    if (runExportSheetForSlot(settings, L"File Save", eslotFile))
    {
        DoFileSave(settings, etfDefault, false);
    }
}

void GridView::OnGridOutputExportAndOpen()
{
    SQLToolsSettings settings = exportSettingsFor(eslotFile);

    if (runExportSheetForSlot(settings, L"File Save and Shell Open", eslotFile))
    {
        DoFileSave(settings, etfDefault, true);
    }
}

void GridView::DoFileSave (const SQLToolsSettings& settings, int _format, bool shellOpen)
{
    if (GetGridSource())
    {
        setExportSettings(settings);

        const wchar_t* filter = 0;
        const wchar_t* extension = 0;

        std::wstring filename;
        {
            std::string fn;
            Common::FilenameTemplate::Format(settings.GetGridExpFilenameTemplate().c_str(), fn, m_uExpFileCounter);
            filename = Common::wstr(fn);

            if (CDocument* doc = GetDocument())
            {
                CString title = doc->GetTitle();
                title.TrimRight(L"* ");
                filename = std::regex_replace(filename, std::wregex(L"&script"), std::wstring(title));
            }
        }

        int format = (_format != etfDefault) ? _format : settings.GetGridExpFormat();
        switch (format)
        {
        case 0:
            filter    = L"Text Files (*.txt)|*.txt|All Files (*.*)|*.*||";
            extension = L"txt";
            break;
        case 6:
        case 7:
        case 8:
        case 9:
            filter    = L"SQL Files (*.sql)|*.sql|All Files (*.*)|*.*||";
            extension = L"sql";
            break;
        case 1:
            filter    = L"CSV Files (Comma delimited) (*.csv)|*.csv|All Files (*.*)|*.*||";
            extension = L"csv";
            break;
        case 2:
            filter    = L"Text Files (Tab delimited) (*.txt)|*.txt|All Files (*.*)|*.*||";
            extension = L"txt";
            break;
        case 3:
            filter    = L"XML Files (*.xml)|*.xml|All Files (*.*)|*.*||";
            extension = L"xml";
            break;
        case 4:
            filter    = L"JSON Files (*.json)|*.json|All Files (*.*)|*.*||";
            extension = L"json";
            break;
        case 5:
            filter    = L"HTML Files (*.html; *.htm)|*.html;*.htm|All Files (*.*)|*.*||";
            extension = L"html";
            break;
        case 10:
            filter    = L"Excel Workbook (*.xlsx)|*.xlsx|All Files (*.*)|*.*||";
            extension = L"xlsx";
            break;
        }

        CFileDialog dial(FALSE, extension, filename.c_str(),
                        OFN_HIDEREADONLY|OFN_OVERWRITEPROMPT|OFN_ENABLESIZING,
                        filter, this, 0);

        if (dial.DoModal() == IDOK)
        {
            CWaitCursor wait;

            m_uExpFileCounter++;
            CString path = dial.GetPathName();

            // XE10: the sheet is named after the file the user chose, so a workbook that is renamed or
            // merged still says what it holds. The writer applies Excel's rules to the name.
            if (IGridTextSource* src = dynamic_cast<IGridTextSource*>(GetGridSource()))
                src->SetXlsxSheetName(OG2::FileBaseName((const wchar_t*)path));

            // XE1: a worksheet is a zip, so its stream has to be binary - a text-mode stream on
            // Windows turns every 0x0A byte of it into CRLF and Excel then refuses the file.
            std::ofstream of(path, OG2::IsBinaryExportFormat(format) ? std::ios::binary : std::ios::out);
            
            int row, nrows, col, ncols;
            getSelectionForExport(row, nrows, col, ncols);
            if (!(nrows != 1 || ncols != 1)) // it is a single cell
                row = nrows = col = ncols = -1;

            GetGridSource()->ExportText(of, row, nrows, col, ncols, format);

            // XE10: read before the close, after which a stream answers -1.
            const std::streamoff written = of.tellp();
            of.close();

            // Nothing worth keeping where the user may have overwritten a file to get it. Emptiness
            // counts only for a worksheet: a text export with no rows can be legitimately empty.
            if (of.fail() || (OG2::IsBinaryExportFormat(format) && written <= 0))
            {
                ::DeleteFile(path);
                Global::SetStatusText("Export failed - the file was removed rather than left empty");
                return;
            }

            // The export path has no status channel, so the caller counts. Format-agnostic and
            // ephemeral by design: a status line, never a modal box on a data path.
            if (const int n = OG2::countTruncatedCells(GetGridSource(), row, nrows, col, ncols))
            {
                std::ostringstream o;
                o << n << " exported value(s) were truncated";
                Global::SetStatusText(o.str().c_str());
            }

            if (shellOpen)
            {
                // Only a file with something in it goes to the shell. What is left is a text
                // export that wrote nothing with a good stream - ExportText catching a fetch failure.
                if (written > 0)
                    Common::AppShellOpenFile(path);
                else
                    Global::SetStatusText("Export wrote nothing - the file was not opened");
            }
        }
    }
}

BOOL GridView::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt)
{
    if (!m_uWheelScrollLines)
        ::SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &m_uWheelScrollLines, 0);

    // Shift+wheel scrolls horizontally; a bare wheel keeps its vertical behaviour. The scroll
    // codes are axis-neutral in EvScroll: SB_LINELEFT is "toward origin", SB_LINERIGHT "toward end".
    const bool horizontal = (nFlags & MK_SHIFT) != 0;

    if (m_uWheelScrollLines == WHEEL_PAGESCROLL)
    {
        if (horizontal)
            OnHScroll(zDelta > 0 ? SB_PAGEUP : SB_PAGEDOWN, 0, 0);
        else
            OnVScroll(zDelta > 0 ? SB_PAGEDOWN : SB_PAGEUP, 0, 0);
    }
    else
    {
        int nToScroll = ::MulDiv(zDelta, m_uWheelScrollLines, WHEEL_DELTA);

        if (zDelta > 0)
            while (nToScroll--)
            {
                if (horizontal) OnHScroll(SB_LINELEFT, 0, 0);
                else            OnVScroll(SB_LINELEFT, 0, 0);
            }
        else
            while (nToScroll++)
            {
                if (horizontal) OnHScroll(SB_LINERIGHT, 0, 0);
                else            OnVScroll(SB_LINERIGHT, 0, 0);
            }
    }

    // A handled Shift+wheel must not fall through to the base class, which forwards WM_MOUSEWHEEL to
    // the parent and would add a vertical scroll on top of our horizontal one.
    if (horizontal)
        return TRUE;

    return CView::OnMouseWheel(nFlags, zDelta, pt);
}

void GridView::OnSettingChange (UINT, LPCTSTR)
{
    m_uWheelScrollLines = 0;
}

void GridView::OnGridOutputOptions ()
{
    SQLToolsSettings settings = GetSQLToolsSettings();

    if (runGlobalExportSheet(settings))
    {
        GetSQLToolsSettingsForUpdate() = settings;
        GetSQLToolsSettingsForUpdate().NotifySettingsChanged();
    }
}

void GridView::OnGridCopyAll ()
{
    SQLToolsSettings settings = exportSettingsFor(eslotClipboard);

    if (runExportSheetForSlot(settings, L"Copy All", eslotClipboard))
    {
        CWaitCursor wait;
        ClipboardGuard cg(this);

        if (GetGridSource() && cg.OpenClipboard())
        {
            setExportSettings(settings);
            GetGridSource()->DoEditCopy(-1, -1, -1, -1, ecEverything, etfDefault);
            cg.CloseClipboard();
        }
    }
}

void GridView::OnGridCopyColHeader ()
{
    CWaitCursor wait;
    ClipboardGuard cg(this);

    if (GetGridSource() && cg.OpenClipboard())
    {
        setExportSettings(exportSettingsFor(eslotClipboard));

        int row, nrows, col, ncols;
        getSelectionForExport(row, nrows, col, ncols);

        GetGridSource()->DoEditCopy(-1, -1, col, ncols, ecColumnHeader, etfDefault);
        cg.CloseClipboard();
    }
}

void GridView::OnGridCopyHeaders()
{
    CWaitCursor wait;
    ClipboardGuard cg(this);

    if (GetGridSource() && cg.OpenClipboard())
    {
        setExportSettings(exportSettingsFor(eslotClipboard));

        GetGridSource()->DoEditCopy(-1, -1, -1, -1, ecFieldNames, etfDefault);
        cg.CloseClipboard();
    }
}

void GridView::OnGridCopyRow()
{
    SQLToolsSettings settings = exportSettingsFor(eslotClipboard);

    if (runExportSheetForSlot(settings, L"Copy Row", eslotClipboard))
    {
        CWaitCursor wait;
        ClipboardGuard cg(this);

        if (GetGridSource() && cg.OpenClipboard())
        {
            setExportSettings(settings);
            int row = m_pManager->m_Rulers[edVert].GetCurrent();
            GetGridSource()->DoEditCopy(row, 1, -1, -1, ecRow, etfDefault);
            cg.CloseClipboard();
        }
    }
}

void GridView::OnGridCopyCol ()
{
    SQLToolsSettings settings = exportSettingsFor(eslotClipboard);

    if (runExportSheetForSlot(settings, L"Copy Column", eslotClipboard))
    {
        CWaitCursor wait;
        ClipboardGuard cg(this);

        if (GetGridSource() && cg.OpenClipboard())
        {
            setExportSettings(settings);
            int row = m_pManager->m_Rulers[edVert].GetCurrent();
            int col = m_pManager->m_Rulers[edHorz].GetCurrent();
            GetGridSource()->DoEditCopy(-1, -1, col, 1, ecColumn, etfDefault);
            cg.CloseClipboard();
        }
    }
}

void GridView::OnGridOutputOpen()
{
    SQLToolsSettings settings = exportSettingsFor(eslotEditor);

    if (runExportSheetForSlot(settings, L"Open in Editor", eslotEditor))
    {
        DoOpenInEditor(settings, etfDefault);
    }
}

CPLSWorksheetDoc* GridView::DoOpenInEditor (const SQLToolsSettings& settings, int _format)
{
    if (!IsEmpty()
    && GetGridSource()
    && GetGridSourceCount(edHorz) > 0
    && GetGridSourceCount(edVert) > 0)
    {
        POSITION pos = AfxGetApp()->GetFirstDocTemplatePosition();

        if (CDocTemplate* pDocTemplate = AfxGetApp()->GetNextDocTemplate(pos))
        {
            if (CDocument* doc = pDocTemplate->OpenDocumentFile(NULL))
            {
                CWaitCursor wait;

                int format = (_format != etfDefault) ? _format : settings.GetGridExpFormat();
                // XE1: an editor document holds text and a worksheet is a zip, and the format
                // setting is shared with the file export, so this falls back to CSV.
                if (OG2::IsBinaryExportFormat(format))
                    format = etfQuoteDelimited;
                setExportSettings(settings);

                int row, nrows, col, ncols;
                getSelectionForExport(row, nrows, col, ncols);
                if (!(nrows != 1 || ncols != 1)) // it is a single cell
                    row = nrows = col = ncols = -1;

                std::ostringstream of;
                GetGridSource()->ExportText(of, row, nrows, col, ncols, format);

                // A streamer that refuses sets failbit rather than throwing, so the document
                // opened to hold its text is closed again and the status line is all the user sees.
                if (of.fail())
                {
                    doc->OnCloseDocument();
                    return NULL;
                }

                string text = of.str();

                CMemFile mf((BYTE*)text.c_str(), text.length());
                CArchive ar(&mf, CArchive::load);
                doc->Serialize(ar);

                // it's bad because of dependency on CPLSWorksheetDoc
                ASSERT_KINDOF(CPLSWorksheetDoc, doc);
                CPLSWorksheetDoc* pDoc = (CPLSWorksheetDoc*)doc;

                // 03.06.2003 bug fix, "Open In Editor" does not set document type
                // 29.06.2003 bug fix, "Open In Editor" fails on comma/tab delimited formats
                const char* name = "Text";

                switch (format)
                {
                case 0: //etfPlainText
                case 6: //etfPLSQL1
                case 7: //etfPLSQL2
                case 8: //etfPLSQL3
                case 9: //etfPLSQL3
                    name = "PL/SQL";
                    break;
                case 1: //etfQuoteDelimited
                case 2: //etfTabDelimitedText
                case 4: //etfJson - no JSON language class exists, so FindByName("JSON") would
                        // yield a null ClassSettingsPtr. "Text" is what CSV and tab already use.
                    name = "Text";
                    break;
                case 3: //etfXmlElem 
                case 5: //etfHtml
                    name = "XML";
                    break;
                }

                pDoc->SetClassSetting(name);
                // 22.03.2004 bug fix, CreareAs file property is ignored for "Open In Editor", "Query", etc (always in Unix format)
                pDoc->DefaultFileFormat();
                return pDoc;
            }
        }
    }
    return NULL;
}

void GridView::OnSysColorChange()
{
    CView::OnSysColorChange();

    m_pManager->InitColors();
}


void GridView::OnMoveUp ()
{
    if (!IsEmpty())
        m_pManager->MoveToLeft(m_toolbarNavigationDir);
}

void GridView::OnMoveDown ()
{
    if (!IsEmpty())
        m_pManager->MoveToRight(m_toolbarNavigationDir);
}

void GridView::OnMovePgUp ()
{
    if (!IsEmpty())
        m_pManager->MoveToPageUp(m_toolbarNavigationDir);
}

void GridView::OnMovePgDown ()
{
    if (!IsEmpty())
        m_pManager->MoveToPageDown(m_toolbarNavigationDir);
}

void GridView::OnMoveEnd ()
{
    if (!IsEmpty())
        m_pManager->MoveToEnd(m_toolbarNavigationDir);
}

void GridView::OnMoveHome ()
{
    if (!IsEmpty())
        m_pManager->MoveToHome(m_toolbarNavigationDir);
}

void GridView::OnUpdate_MoveUp (CCmdUI* pCmdUI)
{
    pCmdUI->Enable(CanMoveUp());
}

void GridView::OnUpdate_MoveDown (CCmdUI* pCmdUI)
{
    pCmdUI->Enable(CanMoveDown());
}

void GridView::OnUpdate_MovePgUp (CCmdUI* pCmdUI)
{
    pCmdUI->Enable(CanMoveUp());
}

void GridView::OnUpdate_MovePgDown (CCmdUI* pCmdUI)
{
    pCmdUI->Enable(CanMoveDown());
}

void GridView::OnUpdate_MoveEnd (CCmdUI* pCmdUI)
{
    pCmdUI->Enable(CanMoveDown());
}

void GridView::OnUpdate_MoveHome (CCmdUI* pCmdUI)
{
    pCmdUI->Enable(CanMoveUp());
}

void GridView::OnRotate ()
{
    CWaitCursor wait;

    AGridSource* gridSource = GetGridSource();

    // Ahead of SetOrientation, not inside Rotate: a staged row is named by one index on one axis, and
    // flipping the axis strands it. CanSort refuses the same permute for the same reason.
    if (gridSource == nullptr || gridSource->HasPendingRow())
        return;

    if (m_pManager->m_Options.m_Rotation && !IsEmpty())
    {
        if (GridSourceBase* source = dynamic_cast<GridSourceBase*>(gridSource))
        {
            source->SetOrientation(source->GetOrientation() == eoVert ? eoHorz : eoVert);
            m_pManager->m_Options.m_ColSizingAsOne = source->GetOrientation() == eoHorz;
            m_pManager->Rotate();

            m_toolbarNavigationDir = source->GetOrientation() == eoVert ? edVert : edHorz;

            if (m_pManager->m_Options.m_ColSizingAsOne)
            {
                int sizeAsOne = m_pManager->m_Rulers[edHorz].GetScrollSize() / 2 - 1;
                m_pManager->m_Rulers[edHorz].SetSizeAsOne(sizeAsOne);
                m_pManager->RecalcRuler(edHorz, true/*adjust*/, true/*invalidate*/);
            }
        }
    }
}

void GridView::OnUpdate_Rotate (CCmdUI* pCmdUI)
{
    AGridSource* source = GetGridSource();

    // Greyed rather than left to refuse silently. This cannot gate the accelerators, which is what
    // OnRotate's own check is for.
    pCmdUI->Enable(m_pManager->m_Options.m_Rotation && !IsEmpty()
                && (source == nullptr || !source->HasPendingRow()));
}

void GridView::OnGridSort (UINT nID)
{
    ValueGridSource* source = dynamic_cast<ValueGridSource*>(GetGridSource());
    if (!source)
        return;

    // Saved before the permute: the editor binds a logical row the sort is about to move.
    m_pManager->EndEditMode();

    // The column the menu was raised over, taken from the click itself. GetCurrentPos is only the
    // fallback for the keyboard menu: preserving a selection means the current cell never moved.
    const int col = (m_menuSortColumn >= 0) ? m_menuSortColumn : m_pManager->GetCurrentPos(edHorz);

    // Rows the sort has to cover may not all be in yet, so they are fetched here rather than the
    // command being refused. A false return changed nothing and is the right response to a cancel.
    source->PrepareSort();

    source->SortByColumn(col, nID == ID_GRID_SORT_ASCENDING);
}

// Reached only via the manual CCmdUI loop in OnContextMenu. MFC runs that loop in
// CFrameWnd::OnInitMenuPopup, not for a view-owned popup, so without it these keep their GRAYED state.
void GridView::OnUpdate_GridSort (CCmdUI* pCmdUI)
{
    ValueGridSource* source = dynamic_cast<ValueGridSource*>(GetGridSource());

    // Greys only for refusals a fetch cannot fix, mirroring SortByColumn: CanSort() is absent because
    // OciGridSource answers false until all rows are in, but its staged-row half does belong here.
    bool canSort = source != nullptr
                && !IsEmpty()
                && GetGridSourceCount(edVert) > 0
                && source->IsTableOrientation()
                && !source->HasPendingRow();

    // Sort needs a column to name. A row header and the corner offer none; a data cell and the
    // keyboard menu both fall back to the current column, which is how this has always worked.
    if (m_menuTarget == mtRowHeader || m_menuTarget == mtCorner)
        canSort = false;

    pCmdUI->Enable(canSort);

    // Check the direction already in force on the column the menu was raised over - the feedback a
    // header sort glyph would give, available here for free because a menu is built on demand.
    const int col = (m_menuSortColumn >= 0) ? m_menuSortColumn : m_pManager->GetCurrentPos(edHorz);

    bool checked = canSort
                && source->IsSorted()
                && source->GetSortColumn() == col
                && source->GetSortAscending() == (pCmdUI->m_nID == ID_GRID_SORT_ASCENDING);

    pCmdUI->SetCheck(checked ? 1 : 0);
}

void GridView::OnGridClearSort ()
{
    // Restoring fetch order permutes rows as much as sorting does, so the editor goes first.
    if (m_pManager)
        m_pManager->EndEditMode();

    if (ValueGridSource* source = dynamic_cast<ValueGridSource*>(GetGridSource()))
        source->ClearSort();
}

// Reached only via the manual CCmdUI loop in OnContextMenu, exactly as OnUpdate_GridSort is.
void GridView::OnUpdate_GridClearSort (CCmdUI* pCmdUI)
{
    ValueGridSource* source = dynamic_cast<ValueGridSource*>(GetGridSource());

    // Needs no column to name, unlike the two sort directions, so it stays live on a row header and on
    // the corner: restoring fetch order belongs to the whole result set. IsSorted() is the only gate.
    pCmdUI->Enable(source != nullptr
                && !IsEmpty()
                && source->IsTableOrientation()
                && source->IsSorted());
}

// The popup's route into the edit gestures. Each goes through the manager, which is where the ruler
// and the editor live, so a menu item and its accelerator cannot drift into doing different things.
void GridView::OnGridCellEdit ()
{
    if (m_pManager)
        m_pManager->EditCurrentCell();
}

void GridView::OnGridRowInsert ()
{
    if (m_pManager)
        m_pManager->BeginPendingRow();
}

void GridView::OnGridRowDuplicate ()
{
    if (m_pManager)
        m_pManager->DuplicateCurrentRow();
}

void GridView::OnGridRowPost ()
{
    if (m_pManager)
        m_pManager->PostPendingRow();
}

void GridView::OnGridRowDiscard ()
{
    if (m_pManager)
        m_pManager->DiscardPendingRow();
}

void GridView::OnGridRowDelete ()
{
    if (m_pManager)
        m_pManager->DeleteCurrentRow();
}

// ClaimKey's bare-F2 answer, so the item is live exactly where the key works: an editing grid with a
// modifiable source. IsEditMode greys it while an editor is open, which SetEditMode asserts against.
void GridView::OnUpdate_GridCellEdit (CCmdUI* pCmdUI)
{
    AGridSource* source = GetGridSource();

    const bool canEdit = (m_pManager != nullptr)
                      && m_pManager->m_Options.m_Editing
                      && !m_pManager->IsEditMode();

    pCmdUI->Enable(canEdit && source != nullptr && source->CanModify());
}

// Reached only via the manual CCmdUI loop in OnContextMenu, as OnUpdate_GridSort is. The source
// answers enablement, alone knowing whether a database, one table and one orientation are present.
void GridView::OnUpdate_GridRowInsert (CCmdUI* pCmdUI)
{
    AGridSource* source = GetGridSource();
    pCmdUI->Enable(source != nullptr && source->CanInsertRow());
}

// The same gate as Insert: a duplicate is a staged row with a starting point, so it is available
// exactly where a blank one is, and refused by the same "one row at a time" rule.
void GridView::OnUpdate_GridRowDuplicate (CCmdUI* pCmdUI)
{
    AGridSource* source = GetGridSource();
    pCmdUI->Enable(source != nullptr && source->CanInsertRow());
}

void GridView::OnUpdate_GridRowPost (CCmdUI* pCmdUI)
{
    AGridSource* source = GetGridSource();
    pCmdUI->Enable(source != nullptr && source->HasPendingRow());
}

void GridView::OnUpdate_GridRowDiscard (CCmdUI* pCmdUI)
{
    AGridSource* source = GetGridSource();
    pCmdUI->Enable(source != nullptr && source->HasPendingRow());
}

void GridView::OnUpdate_GridRowDelete (CCmdUI* pCmdUI)
{
    AGridSource* source = GetGridSource();

    // Greyed while a row is staged, not left to fail: a staged row has no ROWID yet, so there is
    // nothing a DELETE could address. Discard is the verb that applies to it.
    pCmdUI->Enable(source != nullptr
                && source->CanDeleteRow()
                && !source->HasPendingRow());
}


void GridView::OnGridClearRecords()
{
    if (m_pManager->m_Options.m_ClearRecords)
        GetGridSource()->DeleteAll();
}

void GridView::OnUpdate_GridClearRecords(CCmdUI* pCmdUI)
{
    if (m_pManager->m_Options.m_ClearRecords
    && !IsEmpty()
    && GetGridSource()
    && GetGridSourceCount(edHorz) > 0
    && GetGridSourceCount(edVert) > 0) {
        pCmdUI->Enable(true);
    } else {
        pCmdUI->Enable(false);
    }
}

void GridView::OnGridSettings()
{
    SQLToolsSettings settings = GetSQLToolsSettings();

    // Was CPropGridPage1 + CPropGridPage2 + CPropGridOutputPage1 +
    // CPropGridOutputPage2: one CPropTree page holding all four groups.
    CGridPageTree    gridPage(settings);

    // Was CPropHistoryPage: only the "Execution history" group of CPropServerScriptPageTree is built
    // (session/script groups off); the title keeps the tree node reading "SQLTools::History".
    CPropServerScriptPageTree histPage(settings);
    histPage.m_bShowSessionScriptGroups = FALSE;
    histPage.m_psp.pszTitle = L"SQLTools::History";

    // Was CVisualAttributesPage: this CPropTree page shows the same five GetVASets() sets, and its
    // default title "SQLTools::Font and Color" is the one the classic page was given here.
    CVisualAttributesPageTree vaPage(settings.GetVASets());

    static UINT gStarPage = 0;
    Common::CPropertySheetMem sheet(L"Grid settings", gStarPage);
    sheet.SetTreeViewMode(/*bTreeViewMode =*/TRUE, /*bPageCaption =*/FALSE, /*bTreeImages =*/FALSE);
    sheet.SetTreeWidth(240);

    sheet.m_psh.dwFlags |= PSH_NOAPPLYNOW;
    sheet.m_psh.dwFlags &= ~PSH_HASHELP;

    sheet.AddPage(&gridPage);
    sheet.AddPage(&histPage);
    sheet.AddPage(&vaPage);

    int retVal = sheet.DoModal();

    if (retVal == IDOK)
    {
        GetSQLToolsSettingsForUpdate() = settings;
        GetSQLToolsSettingsForUpdate().NotifySettingsChanged();
    }
}

void GridView::OnUpdate_GridSettings (CCmdUI* pCmdUI)
{
    pCmdUI->Enable();
}

void GridView::DoOpen (ETextFormat frm, const wchar_t* ext)
{
    CWaitCursor wait;

    if (IGridTextSource* source = dynamic_cast<IGridTextSource*>(GetGridSource()))
    {
        // 27.10.2003 bug fix, export settings do not affect on quick html/csv viewer launch
        setExportSettings(GetSQLToolsSettings());

        std::wstring file = TempFilesManager::CreateFile(ext);

        if (!file.empty())
        {
            // XE1: binary for the formats that are not text, so a quick-open of a worksheet cannot
            // be corrupted by CRLF translation.
            std::ofstream of(file.c_str(), OG2::IsBinaryExportFormat(frm) ? std::ios::binary : std::ios::out);

            if (!_wcsicmp  (ext, L"CSV"))
            {
                unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
                of.write((char*)bom, sizeof(bom));
            }

            int row, nrows, col, ncols;
            getSelectionForExport(row, nrows, col, ncols);
            if (!(nrows != 1 || ncols != 1)) // it is a single cell
                row = nrows = col = ncols = -1;

            GetGridSource()->ExportText(of, row, nrows, col, ncols, frm);

            of.close();

            // A failed export must not hand the shell an empty file.
            if (!of.fail())
                Common::AppShellOpenFile(file.c_str());
            else
                Global::SetStatusText("Export failed - the file was not opened");
        }
        else
        {
            // The last modal box on an export path - quick-open is a data path like the rest.
            Global::SetStatusText(L"Export failed: cannot generate a temporary file name");
        }
    }
}

void GridView::OnGridOpenWithIE ()
{
    DoOpen(etfHtml, L"HTM");
}

void GridView::OnGridOpenWithExcel ()
{
    DoOpen(etfXlsx, L"xlsx");
}

/*
2017-12-10 Popuip Viewer is based on the code from sqltools++ by Randolf Geist
*/
void GridView::OnGridPopup ()
{
    if (IGridTextSource* source = dynamic_cast<IGridTextSource*>(GetGridSource()))
    {
        // The cell the user clicked, not GetCurrent(): a click inside a selection never moves it,
        // and a click on a header moves one axis at most, so the other stays stale.
        int row = m_menuCellRow;
        int col = m_menuCellCol;

        if (row < 0 && col < 0)
        {
            row = m_pManager->m_Rulers[edVert].GetCurrent();
            col = m_pManager->m_Rulers[edHorz].GetCurrent();
        }

        // One axis alone is a header, and a header is not a cell. Showing GetCurrent() on the other
        // axis would put a cell the user never touched into the viewer, so refuse instead.
        if (row < 0 || col < 0)
            return;

        if (row >= GetGridSourceCount(edVert) || col >= GetGridSourceCount(edHorz))
            return;

        string text, line;
        source->GetCellStr(text, row, col);

        istringstream in(text);
        ostringstream out;

        while (getline(in, line))
        {
            if (line.size() > 0 && *line.rbegin() == '\r')
                out.write(line.c_str(), line.size()-1);
            else
                out.write(line.c_str(), line.size());
            out << '\r' << '\n';
        }

        PopupFrameWnd::ShowPopup (this, out.str());
    }
}

// Greys the item when OnGridPopup would refuse it, so the menu says up front what a click will do.
void GridView::OnUpdate_GridPopup (CCmdUI* pCmdUI)
{
    // The keyboard menu caches no cell, and there the current cell is the answer.
    const bool haveCell = (m_menuCellRow >= 0 && m_menuCellCol >= 0)
                       || (m_menuCellRow < 0 && m_menuCellCol < 0);

    pCmdUI->Enable(dynamic_cast<IGridTextSource*>(GetGridSource()) != nullptr
                && !IsEmpty()
                && haveCell
                && GetGridSourceCount(edVert) > 0
                && GetGridSourceCount(edHorz) > 0);
}

}//namespace OG2



