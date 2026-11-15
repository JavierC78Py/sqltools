// PropTree.h : header file
//
//  Copyright (C) 1998-2001 Scott Ramsay
//	sramsay@gonavi.com
//	http://www.gonavi.com
//
//  This material is provided "as is", with absolutely no warranty expressed
//  or implied. Any use is at your own risk.
// 
//  Permission to use or copy this software for any purpose is hereby granted 
//  without fee, provided the above notices are retained on all copies.
//  Permission to modify the code and to distribute modified code is granted,
//  provided the above notices are retained, and a notice that the code was
//  modified is included with the above copyright notice.
// 
//	If you use this code, drop me an email.  I'd like to know if you find the code
//	useful.

#if !defined(AFX_PROPT_H__386AA426_6FB7_4B4B_9563_C4CC045BB0C9__INCLUDED_)
#define AFX_PROPT_H__386AA426_6FB7_4B4B_9563_C4CC045BB0C9__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "PropTreeList.h"
#include "PropTreeInfo.h"

#include "PropTreeItem.h"
#include "PropTreeItemStatic.h"
#include "PropTreeItemEdit.h"
#include "PropTreeItemCombo.h"
#include "PropTreeItemColor.h"
#include "PropTreeItemDateTime.h"

class CPropTree;

// (ak) Forward declared rather than included: every page that hosts a tree pulls this header
// in, and only PropTree.cpp and PropTreeItem.cpp need the CXPTheme definition itself.
class CXPTheme;

typedef BOOL (CALLBACK* ENUMPROPITEMPROC)(CPropTree*, CPropTreeItem*, LPARAM);

// CPropTree window styles
#define PTS_NOTIFY				0x00000001

// (ak) Width of the sunken border CPropTreeList::OnPaint() draws inside the list window and
// clips its rows to. DrawItem() places a row at m_loc.y - m_Origin.y, so the rows are on
// screen only inside [border, client height - border), and anything deciding whether a row
// is visible has to allow for it - CPropTree::EnsureVisible() reads this. Without it a row
// flush to either edge tested as fully visible with its first and last two pixels clipped
// away. Kept here rather than beside the other PROPTREEITEM_* defines, which are repeated
// per .cpp, because two files need the same number.
#define PROPTREELIST_BORDER			2

// CPropTree HitTest return codes
#define HTPROPFIRST					50

#define HTLABEL						(HTPROPFIRST + 0)
#define HTCOLUMN					(HTPROPFIRST + 1)
#define HTEXPAND					(HTPROPFIRST + 2)
#define HTATTRIBUTE					(HTPROPFIRST + 3)
#define HTCHECKBOX					(HTPROPFIRST + 4)

// CPropTree WM_NOTIFY notification structure
typedef struct _NMPROPTREE
{
    NMHDR			hdr;
	CPropTreeItem*	pItem;
} NMPROPTREE, *PNMPROPTREE, FAR *LPNMPROPTREE;

// CPropTree specific Notification Codes
#define PTN_FIRST					(0U-1100U)

#define PTN_INSERTITEM				(PTN_FIRST-1)
#define PTN_DELETEITEM				(PTN_FIRST-2)
#define PTN_DELETEALLITEMS			(PTN_FIRST-3)
#define PTN_ITEMCHANGED				(PTN_FIRST-5)
#define PTN_ITEMBUTTONCLICK			(PTN_FIRST-6)
#define PTN_SELCHANGE				(PTN_FIRST-7)
#define PTN_ITEMEXPANDING			(PTN_FIRST-8)
#define PTN_COLUMNCLICK				(PTN_FIRST-9)
#define PTN_PROPCLICK				(PTN_FIRST-10)
#define PTN_CHECKCLICK				(PTN_FIRST-12)

/////////////////////////////////////////////////////////////////////////////
// CPropTree window

class CPropTree : public CWnd
{
// Construction
public:
	CPropTree();
	virtual ~CPropTree();

	BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);

// Attributes/Operations
public:
	static CFont* GetNormalFont();
	static CFont* GetBoldFont();

	// Returns the root item of the tree
	CPropTreeItem* GetRootItem();

	// Returns the focused item or NULL for none
	CPropTreeItem* GetFocusedItem();

	// Enumerates an item and all its child items
	BOOL EnumItems(CPropTreeItem* pItem, ENUMPROPITEMPROC proc, LPARAM lParam = 0L);

	// Insert a created CPropTreeItem into the control
	CPropTreeItem* InsertItem(CPropTreeItem* pItem, CPropTreeItem* pParent = NULL);

	// Delete an item and ALL its children
	void DeleteItem(CPropTreeItem* pItem);

	// Delete all items from the tree
	void DeleteAllItems();

	// Return the splitter position
	LONG GetColumn();

	// Set the splitter position
	void SetColumn(LONG nColumn);

	// Sets the focused item
	void SetFocusedItem(CPropTreeItem* pItem);

	// Show or hide the info text
	void ShowInfoText(BOOL bShow = TRUE);

	// Set the height of the info pane in pixels (call before or after Create)
	void SetInfoHeight(LONG nHeight);

	// (ak) How many indent levels the gutter spans - the COLOR_BTNFACE band
	// CPropTreeList::OnPaint() paints down the left edge of the list. One level is the default
	// and the width the control shipped with: it holds the chevron of a root level item. A host
	// whose rows nest deeper asks for more levels, so a chevron that has stepped in with its row
	// still sits on the band instead of on the white item fill beside it. The gutter is also
	// where the selection rectangle and the dotted row separator of CPropTreeItem::DrawItem()
	// start, and the minimum CPropTree::SetColumn() clamps to, so all three move with it.
	//
	// Taken in levels rather than pixels because PROPTREEITEM_EXPANDCOLUMN is private to this
	// library's .cpp files - a caller could not name it, and so could not stay in step with it.
	// Call before or after Create; anything below one is taken as one.
	void SetGutterLevels(int nLevels);

	// (ak) TRUE to give the children of a root level item the one level of indent every other
	// parent gives its children. The control shipped without it - CPropTreeItem::DrawItem()
	// passed x + (IsRootLevel() ? 0 : PNINDENT) down, so a top level group handed its children
	// its own x and the first level of nesting added no indent at all. That is invisible in a two
	// level tree, where the gray shading of a root level row is the hierarchy cue and there is no
	// third level to compare it against, which describes every tree this control hosts but the
	// visual attributes page. A host whose rows nest deeper turns this on, and depth then shows in
	// the chevron and the label alike because both derive from the same m_loc.x.
	// Off by default, so nothing that already looks right moves. Call before or after Create.
	void SetIndentRootChildren(BOOL bIndent = TRUE);

	// Returns TRUE if the item is visible (its parent is expanded)
	BOOL IsItemVisible(CPropTreeItem* pItem);

	// Ensures that an item is visible
	void EnsureVisible(CPropTreeItem* pItem);

	// (ak) pItem's vertical offset in tree coordinates - the y that
	// CPropTreeList::OnPaint() would hand its DrawItem() - walked out of the tree, or -1
	// when pItem is not in this tree at all.
	//
	// CPropTreeItem::GetLocation() cannot answer this. m_loc is written only by
	// DrawItem(), so a row that has never been painted still holds the (0,0) it was
	// constructed with, and EnsureVisible() - which used to trust it - found (0,0)
	// inside the client rect whenever the list was scrolled to the top, concluded the
	// row was already in view, and did not scroll. The row came out focused, selected
	// and described in the info pane while the list stayed where it was: what a search
	// hit on the page that was already showing looked like. Painting first is no help,
	// because the row being looked for is by definition the one off screen.
	LONG GetItemOffsetY(CPropTreeItem* pItem);

	// (ak) The height the list lays out: the sum of the root's children and everything
	// under them. The root item itself is never drawn - CPropTreeList::OnPaint() starts at
	// its first child with y = 0 - so GetRootItem()->GetTotalHeight() overstates this by
	// the root's own height. It is the vertical scrollbar's range, and the measure anything
	// clamping a scroll offset against that range has to use.
	LONG GetContentHeight();

	// do a hit test on the control (returns a HTxxxx code)
	LONG HitTest(const POINT& pt);

	// find an item by a location
	CPropTreeItem* FindItem(const POINT& pt);

	// find an item by item id
	CPropTreeItem* FindItem(UINT nCtrlID);

	// (ak) The item whose check box the cursor is currently over, and whether the left button
	// is held down on it. Read by CPropTreeItem::DrawItem() so a check box can paint the
	// themed hot and pressed states; maintained by CPropTreeList, which owns the mouse, and
	// forwarded from there. Both are transient - CPropTreeList::ClearHotCheckItem() drops
	// them wherever rows move or items are destroyed - so neither is meaningful outside a
	// paint or a mouse message.
	CPropTreeItem* GetHotCheckItem() const;
	BOOL IsCheckPressed() const;

	// (ak) The process wide BUTTON theme handle CPropTreeItem::DrawItem() paints check boxes
	// with, or NULL when theming is unavailable and the caller has to fall back to
	// DrawFrameControl(). Opened lazily on first use, dropped by WM_THEMECHANGED and by
	// FreeGlobalResources() when the last CPropTree in the process goes away.
	static CXPTheme* GetButtonTheme();
	static void ResetButtonTheme();

	// (ak) The process wide TREEVIEW theme handle CPropTreeItem::DrawItem() paints the
	// selection of a row with, or NULL when theming is unavailable and the caller has to
	// fall back to the COLOR_HIGHLIGHT fill. Same lifetime as the BUTTON handle above, and
	// a handle of its own rather than a second class on the same one: DrawThemeBackground()
	// resolves a part id against the class the handle was opened for, so one handle opened
	// for "BUTTON;TREEVIEW" would leave every call guessing which half it landed in.
	static CXPTheme* GetTreeTheme();
	static void ResetTreeTheme();

protected:
	// Actual tree control
	CPropTreeList	m_List;

	// Descriptive control
	CPropTreeInfo	m_Info;

	// TRUE to show info control
	BOOL			m_bShowInfo;

	// Height of the info control
	LONG			m_nInfoHeight;

	// (ak) Width of the gutter in pixels - see SetGutterLevels(). PROPTREEITEM_EXPANDCOLUMN,
	// one level, until a host asks for more.
	LONG			m_nGutterWidth;

	// (ak) TRUE if the children of a root level item indent by a level - see
	// SetIndentRootChildren(). FALSE, the way the control shipped, until a host asks.
	BOOL			m_bIndentRootChildren;

	// Optional footer line drawn at the bottom of the info pane
	CString			m_sInfoFooter;

	// Root level tree item
	CPropTreeItem	m_Root;

	// (ak) Worker for GetItemOffsetY(): descends pParent's children in the order
	// CPropTreeList::OnPaint() draws them, adding to y, and returns TRUE once pTarget
	// has been reached with y holding that row's offset.
	//
	// y is an in/out parameter and the recursion adds to it as it searches, so a descent
	// that comes back empty has already moved y over everything it looked at. The caller
	// must therefore search with a copy and commit it only on success - see the note in
	// the implementation for what double-counting an open group did to the rows below it.
	static BOOL FindItemOffsetY(CPropTreeItem* pParent, CPropTreeItem* pTarget, LONG& y);

	// Linked list of visible items
	CPropTreeItem*	m_pVisbleList;

	// Pointer to the focused item (selected)
	CPropTreeItem*	m_pFocus;

	// PropTree scroll position. x = splitter position, y = vscroll position
	CPoint			m_Origin;

	// auto generated last created ID
	UINT			m_nLastUID;

	// Number of CPropTree controls in the current application
	static UINT		s_nInstanceCount;

	static CFont*	s_pNormalFont;
	static CFont*	s_pBoldFont;

	// (ak) see GetButtonTheme()/ResetButtonTheme()
	static CXPTheme*	s_pButtonTheme;

	// (ak) see GetTreeTheme()/ResetTreeTheme()
	static CXPTheme*	s_pTreeTheme;

	BOOL			m_bDisableInput;

	// Used for enumeration
	static CPropTreeItem*	s_pFound;

public:
	//
	// functions used by CPropTreeItem (you normally dont need to call these directly)
	//

	void AddToVisibleList(CPropTreeItem* pItem);
	void ClearVisibleList();

	void SetOriginOffset(LONG nOffset);
	void UpdatedItems();
	void UpdateMoveAllItems();
	void RefreshItems(CPropTreeItem* pItem = NULL);

	// enable or disable tree input
	void DisableInput(BOOL bDisable = TRUE);
	BOOL IsDisableInput();

	BOOL IsSingleSelection();

	CPropTreeItem* GetVisibleList();
	CWnd* GetCtrlParent();

	const POINT& GetOrigin();

	// (ak) Width of the gutter in pixels: SetGutterLevels() * PROPTREEITEM_EXPANDCOLUMN. Read by
	// CPropTreeList::OnPaint() for the band itself, and by CPropTreeItem::DrawItem() for the two
	// things that begin where the band ends.
	LONG GetGutterWidth() const { return m_nGutterWidth; }

	// (ak) read by CPropTreeItem::DrawItem() when it recurses into its children; see
	// SetIndentRootChildren().
	BOOL GetIndentRootChildren() const { return m_bIndentRootChildren; }

	void SelectItems(CPropTreeItem* pItem, BOOL bSelect = TRUE);

    void DeactivateAll ();

	// Focus on the first visible item
	CPropTreeItem *FocusFirst();

	// Focus on the last visible item
	CPropTreeItem *FocusLast();

	// Focus on the previous item
	CPropTreeItem *FocusPrev();
	
	// Focus on the next item
	CPropTreeItem *FocusNext();

	virtual LRESULT SendNotify(UINT nNotifyCode, CPropTreeItem* pItem = NULL);

protected:
	// Resize the child windows to fit the exact dimensions the CPropTree control
	virtual void ResizeChildWindows(int cx, int cy);

	// Initialize global resources, brushes, fonts, etc.
	void InitGlobalResources();

	// Free global resources, brushes, fonts, etc.
	void FreeGlobalResources();

	// Recursive version of DeleteItem
	void Delete(CPropTreeItem* pItem);

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CPropTree)
	//}}AFX_VIRTUAL

// Implementation
private:
	static BOOL CALLBACK EnumFindItem(CPropTree* pProp, CPropTreeItem* pItem, LPARAM lParam);
	static BOOL CALLBACK EnumSelectAll(CPropTree*, CPropTreeItem* pItem, LPARAM lParam);
	static BOOL CALLBACK EnumDeactivateAll(CPropTree*, CPropTreeItem* pItem, LPARAM);
	static BOOL CALLBACK EnumMoveAll(CPropTree*, CPropTreeItem* pItem, LPARAM);
	static BOOL CALLBACK EnumRefreshAll(CPropTree*, CPropTreeItem* pItem, LPARAM);

	// Generated message map functions
protected:
	//{{AFX_MSG(CPropTree)
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnEnable(BOOL bEnable);
	afx_msg void OnSysColorChange();
	afx_msg LRESULT OnThemeChanged();
	afx_msg void OnSetFocus(CWnd* pOldWnd);
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_PROPT_H__386AA426_6FB7_4B4B_9563_C4CC045BB0C9__INCLUDED_)
