#if !defined(AFX_PROPTREELIST_H__2E09E831_09F5_44AA_B41D_9C4BF495873C__INCLUDED_)
#define AFX_PROPTREELIST_H__2E09E831_09F5_44AA_B41D_9C4BF495873C__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// PropTreeList.h : header file
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

class CPropTree;

// (ak) named by the check box hover state below; PropTree.h includes PropTreeItem.h before
// this header, but the declaration is repeated so this one stays self contained.
class CPropTreeItem;

/////////////////////////////////////////////////////////////////////////////
// CPropTreeList window

class CPropTreeList : public CWnd
{
// Construction
public:
	CPropTreeList();
	virtual ~CPropTreeList();

	BOOL Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID);

// Attributes
public:
	void SetPropOwner(CPropTree* pProp);

protected:
	// CPropTree class that this class belongs
	CPropTree*		m_pProp;

	// bitmap back buffer for flicker free drawing
	CBitmap			m_BackBuffer;

	// current diminsions of the back buffer
	CSize			m_BackBufferSize;

	// splitter pevious position
	LONG			m_nPrevCol;

	// TRUE if we are dragging the splitter
	BOOL			m_bColDrag;

	// (ak) Check box hover and press state. The list owns the mouse, so it is what works out
	// which box the cursor is over; CPropTreeItem::DrawItem() reads the pair back through
	// CPropTree::GetHotCheckItem()/IsCheckPressed() and picks the themed CBS_* state from it.
	//
	// m_pHotCheckItem is a raw pointer to a row the host owns, so it has to be dropped
	// whenever that row can have gone away - CPropTree::DeleteItem()/DeleteAllItems()/Delete()
	// - or whenever the rows have moved out from under the cursor without the mouse moving,
	// which is scrolling and resizing. ClearHotCheckItem() is the single place that does it.
	CPropTreeItem*	m_pHotCheckItem;

	// (ak) TRUE while the left button is held on the hot check box
	BOOL			m_bCheckPressed;

	// (ak) TRUE when this list took the mouse capture for a check box press rather than for a
	// splitter drag. The two are released by different paths in OnLButtonUp() and must not
	// free each other's capture.
	BOOL			m_bCheckCapture;

	// (ak) TRUE while a TME_LEAVE request is outstanding, so TrackLeave() asks for
	// WM_MOUSELEAVE once per entry into the window instead of on every mouse move.
	BOOL			m_bTrackingLeave;

// Operations
public:
	void UpdateResize();

	// (ak) Drop the check box hover/press state without repainting - the callers all repaint
	// themselves. Public because CPropTree reaches in from the places that destroy items or
	// move rows.
	void ClearHotCheckItem();

	// (ak) read by CPropTree, and through it by CPropTreeItem::DrawItem()
	CPropTreeItem* GetHotCheckItem() const { return m_pHotCheckItem; }
	BOOL IsCheckPressed() const { return m_bCheckPressed; }

protected:
	void RecreateBackBuffer(int cx, int cy);
	void CheckVisibleFocus();

	// (ak) Work out which check box, if any, the cursor is over and repaint the rows whose
	// state that changed.
	void UpdateHotCheckItem(const CPoint& pt);

	// (ak) Repaint one row. Hover fires on every mouse move across the list, and the list is
	// tall, so it repaints the row that was lit and the one that is now lit rather than the
	// whole control.
	void InvalidateItemRow(CPropTreeItem* pItem);

	// (ak) Ask for WM_MOUSELEAVE. Without it a cursor that leaves over another window leaves
	// the last box lit, because no further mouse move arrives to notice.
	void TrackLeave();

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CPropTreeList)
	//}}AFX_VIRTUAL

// Implementation
public:

	// Generated message map functions
protected:
	//{{AFX_MSG(CPropTreeList)
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnPaint();
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnLButtonDblClk(UINT nFlags, CPoint point);
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg UINT OnGetDlgCode();
	//}}AFX_MSG
public:
	afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
	DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_PROPTREELIST_H__2E09E831_09F5_44AA_B41D_9C4BF495873C__INCLUDED_)
