#if !defined(AFX_PROPTREEITEMCOLOR_H__50C09AC0_1F02_4150_AA6A_5151345D87A2__INCLUDED_)
#define AFX_PROPTREEITEMCOLOR_H__50C09AC0_1F02_4150_AA6A_5151345D87A2__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// PropTreeItemColor.h : header file
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
//
//	(ak) MODIFIED. The palette popup this item opens was rewritten. The shipped one was a
//	40 swatch 8x5 grid under a "More Colors" button, mouse only, with nothing to show which
//	swatch held the attribute's current colour. It now offers an Office style theme matrix,
//	the standard colours, the custom slots ChooseColor() fills and a "More Colors..." row,
//	and it is keyboard accessible: the arrows move, Enter or Space commits, Escape restores
//	the colour the popup opened with. Every swatch is named in a tooltip. An Automatic row
//	is available to a host that models an optional colour, but see SetAllowAutomatic() for
//	why it is off by default. PropTreeItemColor.cpp carries the details.

#include <afxcmn.h>		// (ak) CToolTipCtrl - the way PropTreeItemEdit.h pulls it in
#include "PropTreeItem.h"

// (ak) Cells the popup model holds at most: 1 Automatic + 5*10 theme + 10 standard +
// 16 custom + 1 More Colors. A fixed array keeps the vendored library free of the STL its
// hosts use; the model is rebuilt on every activation anyway.
#define CLRPOP_MAXCELLS		80

// (ak) How much larger than the shipped size the palette popup is drawn, in percent.
//
// SetPopupScale() takes the percentage directly. SetPopupScaleStep() takes the same range the
// way a font size ladder does - step 0 is the size the popup has always been and every step
// after it is one rung up, the top rung being exactly double. Both write the one value, so
// there is no question of the two disagreeing, and neither can push the popup past
// CLRPOP_SCALE_MAX.
//
// The ceiling is double because the popup is positioned against the work area of the monitor
// its cell is on, and PositionPopup() clamps to that work area when the popup does not fit
// inside it. Asking for a palette larger than the screen would not give a larger palette, it
// would give a cropped one. Double still fits comfortably: the popup is 192 px wide at 100%
// and 384 px at 200%, against a 1024x768 work area.
#define CLRPOP_SCALE_MIN	100		// percent - the size the popup has always been
#define CLRPOP_SCALE_MAX	200		// percent - double, and the ceiling
#define CLRPOP_SCALE_STEPS	4		// rungs between the two, so 25% apart

/////////////////////////////////////////////////////////////////////////////
// CPropTreeItemColor window

class CPropTreeItemColor : public CWnd, public CPropTreeItem
{
// Construction
public:
	CPropTreeItemColor();
	virtual ~CPropTreeItemColor();

// Attributes
public:
	// The attribute area needs drawing
	virtual void DrawAttribute(CDC* pDC, const RECT& rc);

	// Retrieve the item's attribute value
	COLORREF GetItemValue();

	// Set the item's attribute value
	void SetItemValue(COLORREF);

	// (ak) CPropTreeItem: contributes the colour as #RRGGBB to a settings search -
	// see PropTreeItem.h for the contract. CLR_INVALID (the Automatic row) adds
	// nothing: there is no colour to find.
	virtual void AppendSearchValue(CString& sText);

	// Called when attribute area has changed size
	virtual void OnMove();

	// Called when the item needs to refresh its data
	virtual void OnRefresh();

	// Called when the item needs to commit its changes
	virtual void OnCommit();

	// Called to activate the item
	virtual void OnActivate();

    // ak
    virtual void OnCancel();

	static void SetDefaultColorsList(COLORREF* pColors);

	// (ak) TRUE puts an "Automatic" row at the top of the popup. Picking it leaves CLR_INVALID
	// in the item, meaning "no colour of its own".
	//
	// It is OFF by default, and a host has to earn it. CLR_INVALID is only a safe value for an
	// item whose host treats the colour as genuinely optional - one where "unset" is a state the
	// data model represents AND every consumer of that state guards for it. CVisualAttributes-
	// PageTree, the only host in SQLTools, is not that: there the mask bit that decides whether
	// a Foreground row is built at all is schema - which properties the attribute has - and is
	// never assigned by the page. So committing CLR_INVALID cannot clear it, and the attribute
	// is left claiming a foreground colour whose value is 0xFFFFFFFF. VisualAttribute::
	// Construct() copies that straight through, because the bit is set, into consumers that do
	// not test for it - OEViewPaintAccessories.cpp takes m_Foreground as the editor's text
	// colour and as a pen colour unchecked - and the result is white text on a white editor.
	// Clearing the bit instead is not the answer either: the row is built from it, so it would
	// vanish and could not be put back.
	//
	// The row and the CLR_INVALID rendering in DrawAttribute() stay, because a host that does
	// model an optional colour wants them; they are simply not reachable from this one.
	void SetAllowAutomatic(BOOL bAllow);

	// (ak) Size of the palette popup, as a percentage of the shipped one. Clamped into
	// [CLRPOP_SCALE_MIN, CLRPOP_SCALE_MAX] - see those constants for why the ceiling is double.
	//
	// Safe to call at any time. Before the item is in a tree it only records the value. While
	// the popup is open it lays the cells out again, moves the tooltips onto the new
	// rectangles, drops the derived font so the next paint makes one at the new size, and
	// resizes the window where it stands - so the change is visible without a reopen.
	void SetPopupScale(int nPercent);
	int  GetPopupScale() const { return m_nScalePct; }

	// (ak) The same range as a ladder of rungs, for a host that would rather offer "smaller /
	// larger" than a percentage. Step 0 is CLRPOP_SCALE_MIN and CLRPOP_SCALE_STEPS is
	// CLRPOP_SCALE_MAX. A step outside that clamps rather than fails, the way a font size
	// request does.
	void SetPopupScaleStep(int nStep);
	int  GetPopupScaleStep() const;

	// (ak) The scale every CPropTreeItemColor constructed after the call starts with, for a
	// host that wants one size across a page and would rather not reach every row. Mirrors
	// SetDefaultColorsList() above; an item's own SetPopupScale() overrides it. Call it before
	// the items are created - it is read in the constructor, not at paint time.
	static void SetDefaultPopupScale(int nPercent);
	static int  GetDefaultPopupScale();

protected:
	// (ak) One cell of the popup: a colour swatch, the Automatic row or the More Colors row.
	enum CellKind { CELL_COLOR = 0, CELL_AUTOMATIC, CELL_MORE };
	enum CellSection { SEC_AUTOMATIC = 0, SEC_THEME, SEC_STANDARD, SEC_CUSTOM, SEC_MORE, SEC_COUNT };

	struct ColorCell
	{
		int			kind;		// CellKind
		COLORREF	color;		// CLR_INVALID on the Automatic row
		int			section;	// CellSection
		int			col;		// column inside the section grid, 0 on a full width row
		int			row;		// row inside the section grid
		CRect		rcCell;		// hit test, hover and focus ring
		CRect		rcSwatch;	// the painted swatch
		CString		name;		// tooltip and accessible name
	};

	// Popup model
	void	BuildCells();
	void	LayoutCells();

	// (ak) Popup geometry at the item's scale. Each is the compile time constant of the same
	// name in PropTreeItemColor.cpp multiplied by m_nScalePct and rounded to the nearest pixel.
	// LayoutCells() and the painting that reads those rectangles back both come through here,
	// so the model and the pixels drawn inside it cannot disagree about how big a cell is.
	// Declared out of line because the constants they scale live in the .cpp.
	int  Scaled(int nBase) const;
	int  PopMargin() const;
	int  PopCell() const;
	int  PopSwatch() const;
	int  PopLabel() const;
	int  PopWideRow() const;
	int  PopGap() const;
	int  PopWidth() const;

	// (ak) The face the popup's text is drawn in: the tree's normal font at the item's scale,
	// or that font itself when the scale is 100% and there is nothing to derive. Made on first
	// use and dropped by SetPopupScale(), so a resized popup is never left holding the text
	// size of the popup it used to be.
	CFont* EnsurePopupFont();

	void	BuildToolTips();
	int		CellFromPoint(const CPoint& pt) const;
	int		CellForColor(COLORREF color) const;
	int		FirstCellOfSection(int nSection) const;

	// Popup navigation and commit
	void	SetCurrent(int nCell);
	void	MoveCurrent(int dCol, int dRow);
	void	MoveCurrentSection(int dSection);
	void	CommitCell(int nCell);
	void	ChooseMoreColors();

	// Popup window
	void	PositionPopup(BOOL bShow);

	// (ak) The unconditional hide, and the unconditional close.
	//
	// Every path that is supposed to take the popup off the screen ends in one of these two,
	// because the paths that go through the base class cannot be relied on to. CPropTreeItem::-
	// CommitChanges() carries an m_bCommitOnce latch: it is set on the first commit after an
	// Activate() and cleared only by the next one, and while it is set CommitChanges() returns
	// WITHOUT calling OnCommit() - which is the only thing that hides the window. A popup that
	// is on screen while its item is in that state cannot be closed by the tree at all:
	// CPropTreeList::EnumDeactivateItems() and CPropTree::EnumDeactivateAll() both test
	// IsActivated(), which the latch has already cleared, so they skip the item and walk on.
	// That is how two palettes end up floating over the page with no way to dismiss the older.
	//
	// HidePopup() is idempotent and hides whatever the state says. ForceClose() is HidePopup()
	// with the value thrown away first, for the paths where nothing was chosen.
	void	HidePopup();
	void	ForceClose();

	// (ak) A WH_GETMESSAGE thread hook, installed for as long as the popup is open. It is how a
	// popup that is shown without being activated hears about a click that lands outside it, and
	// about an Escape that is on its way to the property sheet instead of to it - see
	// PopupHookProc() for why this is a hook and not SetCapture().
	void	InstallPopupHook();
	void	RemovePopupHook();
	void	DismissOutsideClick();

	// Popup painting
	void	DrawCell(CDC* pDC, const ColorCell& cell);
	void	DrawSwatch(CDC* pDC, const CRect& rc, COLORREF color, BOOL bAutomatic);
	void	DrawSectionLabel(CDC* pDC, int nSection, int y);

	COLORREF	m_cColor;
	COLORREF	m_cPrevColor;
	BOOL		m_bInDialog;	// ChooseColor() is up, KillFocus must not commit
	BOOL		m_bAllowAutomatic;

	// (ak) Percentage of the shipped popup size this item draws at. Read by Scaled(), and
	// through it by every rectangle in the popup.
	int			m_nScalePct;

	ColorCell	m_cells[CLRPOP_MAXCELLS];
	int			m_nCellCount;
	int			m_nCurrent;		// cell the keyboard or the mouse is on
	int			m_nHot;			// cell under the mouse
	BOOL		m_bMoreDown;	// the More Colors row is pressed
	BOOL		m_bTracking;	// TrackMouseEvent is armed
	CSize		m_sizePopup;	// client size of the popup
	CFont		m_fontPopup;	// (ak) the tree's normal font at m_nScalePct; unused at 100%
	CToolTipCtrl m_toolTip;

	static COLORREF*	s_pColors;

	// (ak) What m_nScalePct is initialised to. See SetDefaultPopupScale().
	static int			s_nDefaultScalePct;

	// (ak) The message hook is one per thread, and only one popup is ever open at a time -
	// CPropTreeList::OnLButtonDown() runs EnumDeactivateItems() before it activates the item
	// that was clicked. So the hook handle and the popup it belongs to are statics, not
	// per-instance state.
	static HHOOK				s_hPopupHook;
	static CPropTreeItemColor*	s_pHookOwner;


	// (ak) The one popup that is on screen, or NULL. OnActivate() closes whatever is in here
	// before it opens its own, which is what makes "more than one palette at a time" impossible
	// even when a deactivation path failed to hide its window - see HidePopup().
	static CPropTreeItemColor*	s_pOpenPopup;

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CPropTreeItemColor)
public:
	virtual BOOL PreTranslateMessage(MSG* pMsg);
	//}}AFX_VIRTUAL

// Implementation
public:

	// Generated message map functions
protected:
	//{{AFX_MSG(CPropTreeItemColor)
	afx_msg void OnKillFocus(CWnd* pNewWnd);
	afx_msg void OnPaint();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnClose();
	afx_msg void OnMouseMove(UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave();
	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg UINT OnGetDlgCode();
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
	//}}AFX_MSG

	// (ak) Added by hand, outside the ClassWizard block. The teardown that goes with the message
	// hook, and the right click that dismisses the popup from inside it.
	afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnDestroy();

	// (ak) The WH_GETMESSAGE thread procedure. Static, because that is what SetWindowsHookEx()
	// takes; it finds the popup it is watching through s_pHookOwner.
	static LRESULT CALLBACK PopupHookProc(int nCode, WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_PROPTREEITEMCOLOR_H__50C09AC0_1F02_4150_AA6A_5151345D87A2__INCLUDED_)
