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

#pragma once
#ifndef __GridView_h__
#define __GridView_h__

/***************************************************************************/
/*      Purpose: Grid implementation                                       */
/*      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~            */
/*      (c) 1996-1999,2003,2017 Aleksey Kochetov                           */
/***************************************************************************/

#include "stdafx.h"
#include <functional>
#include <list>
#include <COMMON/VisualAttributes.h>
#include "GridManager.h"
#include "GridExportSlots.h"
#include "GridViewPaintAccessories.h"

// TODO: it should be replaced with grid own settings
#include <OpenEditor/OESettings.h>
#include "App/SQLTools.h" 

    class CPLSWorksheetDoc;

namespace OG2 /* OG = OpenGrid */
{
    using Common::VisualAttribute;
    using Common::VisualAttributesSet;

struct GridCellRange
{
    int row;
    int nrows;
    int col;
    int ncols;

    GridCellRange () : row(-1), nrows(0), col(-1), ncols(0) {}
};

class GridView;

typedef std::function<void (GridView&, const GridCellRange&)> GridCtrlLeftClickFn;

class GridView : public CView, protected OpenEditor::SettingsSubscriber
{
protected:
    GridView();
    virtual ~GridView();
    DECLARE_DYNCREATE(GridView)

    void setExportSettings (const SQLToolsSettings& setting);
    void getSelectionForExport (int& row, int& nrows, int& col, int& ncols) const;

public:
    const AGridManager* GetGridManager () const;
    AGridManager* GetGridManager ();
    const AGridSource* GetGridSource () const;
    AGridSource* GetGridSource ();
    int GetGridSourceCount (eDirection dir) const;

    void SetVScroller (HWND hBarWnd);
    void SetHScroller (HWND hBarWnd);
    void RecalcVScroller ();
    void RecalcHScroller ();

    virtual bool IsEmpty () const;
    bool CanMoveUp () const;
    bool CanMoveDown () const;

    //{{AFX_VIRTUAL(GridView)
    public:
    virtual void OnPrepareDC(CDC* pDC, CPrintInfo* pInfo = NULL);
    virtual BOOL OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo);
    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
    // Claims the keys the grid needs before the frame's accelerator table can turn them into
    // commands - see EditGridManager::ClaimKey.
    virtual BOOL PreTranslateMessage(MSG* pMsg);
    protected:
    virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
    virtual void OnDraw(CDC*) {};
    //}}AFX_VIRTUAL

protected:
    //{{AFX_MSG(GridView)
    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnDestroy();
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
    afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
    afx_msg UINT OnGetDlgCode();
    afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
    afx_msg void OnPaint();
    afx_msg void OnSetFocus(CWnd* pOldWnd);
    afx_msg void OnKillFocus(CWnd* pNewWnd);
    afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnMButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    void DoEditCopy (const SQLToolsSettings& setting, int);
    afx_msg void OnEditCopy();
    afx_msg void OnGridCopyAsText           ();
    afx_msg void OnGridCopyAsPlsqlValues    ();
    afx_msg void OnGridCopyAsPlsqlExpression();
    afx_msg void OnGridCopyAsPlsqlInList    ();
    afx_msg void OnGridCopyAsPlsqlInserts   ();
    afx_msg void OnEditPaste();
    afx_msg void OnEditCut();
    afx_msg void OnUpdateEditGroup(CCmdUI* pCmdUI);
    afx_msg void OnUpdate_CopyHeaders(CCmdUI* pCmdUI);
    afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    //}}AFX_MSG
    afx_msg void OnFileExport();
    afx_msg LRESULT OnGetFont (WPARAM, LPARAM);
    afx_msg void OnContextMenu(CWnd* pWnd, CPoint pos);
    afx_msg void OnSettingChange (UINT uFlags, LPCTSTR lpszSection);

    DECLARE_MESSAGE_MAP()

protected:
    static UINT m_uWheelScrollLines;   // cached value for MS Weel support

    CGridViewPaintAccessoriesPtr m_paintAccessories;
    GridManager* m_pManager;
    BOOL m_ShouldDeleteManager;
    eDirection m_toolbarNavigationDir;
    std::string m_visualAttributeSetName;
    UINT m_uPopupMenuId;
    // Popup shown for the header strips in place of the data-cell one. Zero keeps a single menu,
    // which is what the plain GridView on IDR_GRID_POPUP wants; OciGridView sets it in its ctor.
    UINT m_uHeaderPopupMenuId;
    UINT m_uExpFileCounter;
    GridCtrlLeftClickFn m_ctrlLeftClickFn;

    // Where the last click landed, cached by CacheMenuTarget before the click can move anything: the
    // menu is built later, and a click on an existing selection skips EvLButtonDown outright.
    enum eContextMenuTarget { mtNone, mtDataCell, mtColumnHeader, mtRowHeader, mtCorner };
    eContextMenuTarget m_menuTarget;
    int m_menuSortColumn;   // absolute column under the click, or -1 when it was not a column header
    int m_menuCellRow;      // absolute data-cell row under the click, or -1 off the data rows
    int m_menuCellCol;      // absolute data-cell column under the click, or -1 off the data columns
    CRect m_lastClickRect;  // down-position rect for drag detection; per instance, not file-static (F15)

    // Classifies a click into the four members above. Shared by the right-click menu and the
    // Alt/middle-click Popup Viewer, which must act on the cell clicked and not on GetCurrent().
    void CacheMenuTarget (const CPoint& point);

    // SettingsSubscriber
    virtual void OnSettingsChanged ();

private:
    BOOL m_Erase;
    //CToolTipCtrl m_wndToolTip;
    
protected:
    void DoOpen (ETextFormat frm, const wchar_t* ext);
    void DoFileSave (const SQLToolsSettings&, int format, bool shellOpen);
    CPLSWorksheetDoc* DoOpenInEditor (const SQLToolsSettings&, int format);

    void notifyCtrlLeftClick ();

public:
    void SetCtrlLeftClickHandler (const GridCtrlLeftClickFn& fn)           { m_ctrlLeftClickFn = fn; }
    bool GetSelectedCellRange (GridCellRange&) const;

    void SetVisualAttributeSetName (const std::string&);
    const std::string& GetVisualAttributeSetName () const;

    afx_msg void OnInitMenuPopup (CMenu* pPopupMenu, UINT nIndex, BOOL bSysMenu);
    afx_msg void OnGridOutputOptions ();
    afx_msg void OnGridOutputExportAndOpen ();
    afx_msg void OnGridCopyAll();
    afx_msg void OnGridCopyColHeader();
    afx_msg void OnGridCopyHeaders();
    afx_msg void OnGridCopyRow();
    afx_msg void OnGridCopyCol();

    afx_msg void OnGridOutputOpen();
    afx_msg void OnSysColorChange();

    afx_msg void OnMoveUp ();
    afx_msg void OnMoveDown ();   
    afx_msg void OnMovePgUp ();   
    afx_msg void OnMovePgDown (); 
    afx_msg void OnMoveEnd ();    
    afx_msg void OnMoveHome ();   

    afx_msg void OnUpdate_MoveUp    (CCmdUI* pCmdUI);
    afx_msg void OnUpdate_MoveDown  (CCmdUI* pCmdUI);
    afx_msg void OnUpdate_MovePgUp  (CCmdUI* pCmdUI);
    afx_msg void OnUpdate_MovePgDown(CCmdUI* pCmdUI);
    afx_msg void OnUpdate_MoveEnd   (CCmdUI* pCmdUI);
    afx_msg void OnUpdate_MoveHome  (CCmdUI* pCmdUI);

    afx_msg void OnGridClearRecords ();   
    afx_msg void OnUpdate_GridClearRecords (CCmdUI* pCmdUI);

    afx_msg void OnRotate ();   
    afx_msg void OnUpdate_Rotate (CCmdUI* pCmdUI);

    // Sort by the current column, from the data grid's popup menu. One range handler for both
    // directions; see OnGridSort for why the direction is not toggled.
    afx_msg void OnGridSort (UINT nID);
    afx_msg void OnUpdate_GridSort (CCmdUI* pCmdUI);

    // Drops the active sort, offered beside Sort Ascending/Descending only while one is in force.
    afx_msg void OnGridClearSort ();
    afx_msg void OnUpdate_GridClearSort (CCmdUI* pCmdUI);

    // Row surgery from the data grid's popup menu, each the gesture its own accelerator takes and
    // routed through the manager so the two cannot drift. The source answers enablement.
    afx_msg void OnGridCellEdit ();
    afx_msg void OnGridRowInsert ();
    afx_msg void OnGridRowDuplicate ();
    afx_msg void OnGridRowPost ();
    afx_msg void OnGridRowDiscard ();
    afx_msg void OnGridRowDelete ();
    afx_msg void OnUpdate_GridCellEdit     (CCmdUI* pCmdUI);
    afx_msg void OnUpdate_GridRowInsert    (CCmdUI* pCmdUI);
    afx_msg void OnUpdate_GridRowDuplicate (CCmdUI* pCmdUI);
    afx_msg void OnUpdate_GridRowPost      (CCmdUI* pCmdUI);
    afx_msg void OnUpdate_GridRowDiscard   (CCmdUI* pCmdUI);
    afx_msg void OnUpdate_GridRowDelete    (CCmdUI* pCmdUI);

    // Axis-specific verbs, offered only on their own axis so the one header menu reads correctly
    // for column, row and corner alike. Falls back to the OnUpdateEditGroup test off the headers.
    afx_msg void OnUpdate_GridHeaderItem (CCmdUI* pCmdUI);

    afx_msg void OnGridClearSelection ();
    afx_msg void OnUpdate_GridClearSelection (CCmdUI* pCmdUI);

    afx_msg void OnGridSettings ();
    afx_msg void OnUpdate_GridSettings (CCmdUI* pCmdUI);

    afx_msg void OnGridOpenWithIE ();
    afx_msg void OnGridOpenWithExcel ();
    
    afx_msg void OnGridPopup ();
    afx_msg void OnUpdate_GridPopup (CCmdUI* pCmdUI);
};

/////////////////////////////////////////////////////////////////////////////

class GridStringSource;

class StrGridView : public GridView
{
protected:
    GridStringSource* m_pStrSource;

public:
    StrGridView();

    void Clear ();

    DECLARE_DYNCREATE(StrGridView);
};

/////////////////////////////////////////////////////////////////////////////

inline
const AGridManager* GridView::GetGridManager () const
    { return m_pManager; }

inline
AGridManager* GridView::GetGridManager ()
    { return m_pManager; }

inline
const AGridSource* GridView::GetGridSource () const
    { return m_pManager->m_pSource; }

inline
AGridSource* GridView::GetGridSource ()
    { return m_pManager->m_pSource; }

inline
int GridView::GetGridSourceCount (eDirection dir) const
    { return GetGridSource()->GetCount(dir); }

inline
void GridView::SetVisualAttributeSetName (const std::string& name)
    { m_visualAttributeSetName = name; }

inline
const std::string& GridView::GetVisualAttributeSetName () const
    { return m_visualAttributeSetName; }

inline
void GridView::SetVScroller (HWND hBarWnd)
    { _ASSERTE(m_pManager); m_pManager->m_Rulers[edVert].SetScroller(hBarWnd); }

inline
void GridView::SetHScroller (HWND hBarWnd)
    { _ASSERTE(m_pManager); m_pManager->m_Rulers[edHorz].SetScroller(hBarWnd); }

inline
void GridView::RecalcVScroller ()
    { _ASSERTE(m_pManager); m_pManager->CalcScroller(edVert); }

inline
void GridView::RecalcHScroller ()
    { _ASSERTE(m_pManager); m_pManager->CalcScroller(edHorz); }

inline
bool GridView::CanMoveUp () const     
    { return !IsEmpty() && m_pManager->m_Rulers[m_toolbarNavigationDir].GetCurrent() > 0; }

inline
bool GridView::CanMoveDown () const   
    { return !IsEmpty() && m_pManager->m_Rulers[m_toolbarNavigationDir].GetCurrent() < GetGridSource()->GetCount(m_toolbarNavigationDir)-1; }

//{{AFX_INSERT_LOCATION}}

}//namespace OG2

#endif//__GridView_h__
