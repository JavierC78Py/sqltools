// PropTree.cpp : implementation file
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

#include "stdafx.h"
#include "PropTree.h"

// (ak) CXPTheme, the project's own dynamic wrapper over uxtheme - the control deliberately
// does not link uxtheme.lib, so the BUTTON handle CPropTreeItem::DrawItem() paints check
// boxes with is opened and closed through this. Lives one directory up, which is on the
// compiler's include path for every configuration.
#include "XPTheme.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define PROPTREEITEM_EXPANDCOLUMN		16			// width of the expand column
#define PROPTREEITEM_COLRNG				5			// width of splitter

static const CString strOfficeFontName	= _T("Tahoma");
static const CString strDefaultFontName = _T("MS Sans Serif");

static int CALLBACK FontFamilyProcFonts(const LOGFONT FAR* lplf, const TEXTMETRIC FAR*, ULONG, LPARAM)
{
	ASSERT(lplf != NULL);
	CString strFont = lplf->lfFaceName;
	return strFont.CollateNoCase (strOfficeFontName) == 0 ? 0 : 1;
}

/////////////////////////////////////////////////////////////////////////////
// CPropTree

UINT CPropTree::s_nInstanceCount;
CFont* CPropTree::s_pNormalFont;
CFont* CPropTree::s_pBoldFont;
CPropTreeItem* CPropTree::s_pFound;
CXPTheme* CPropTree::s_pButtonTheme = NULL;
CXPTheme* CPropTree::s_pTreeTheme = NULL;

CPropTree::CPropTree() :
	m_bShowInfo(TRUE),
	m_nInfoHeight(50),
	m_nGutterWidth(PROPTREEITEM_EXPANDCOLUMN),
	m_bIndentRootChildren(FALSE),
	m_pVisbleList(NULL),
	m_Origin(100,0),
	m_nLastUID(1),
	m_pFocus(NULL),
	m_bDisableInput(FALSE)
{
	m_Root.Expand();

	// init global resources only once
	if (!s_nInstanceCount)
		InitGlobalResources();
	s_nInstanceCount++;
}


CPropTree::~CPropTree()
{
	DeleteAllItems();

	s_nInstanceCount--;

	// free global resource when ALL CPropTrees are destroyed
	if (!s_nInstanceCount)
		FreeGlobalResources();
}


BEGIN_MESSAGE_MAP(CPropTree, CWnd)
	//{{AFX_MSG_MAP(CPropTree)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_ENABLE()
	ON_WM_SYSCOLORCHANGE()
	ON_WM_THEMECHANGED()
	ON_WM_SETFOCUS()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()


/////////////////////////////////////////////////////////////////////////////
// CPropTree message handlers

void CPropTree::OnSetFocus(CWnd* /*pOldWnd*/)
{
	m_List.SetFocus();
	if (!m_pFocus)
		FocusFirst();
}

const POINT& CPropTree::GetOrigin()
{
	return m_Origin;
}


BOOL CPropTree::Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID)
{
	CWnd* pWnd = this;

	LPCTSTR pszCreateClass = AfxRegisterWndClass(CS_HREDRAW|CS_VREDRAW, ::LoadCursor(NULL, IDC_ARROW));

	return pWnd->Create(pszCreateClass, _T(""), dwStyle, rect, pParentWnd, nID);
}


int CPropTree::OnCreate(LPCREATESTRUCT lpCreateStruct) 
{
	if (CWnd::OnCreate(lpCreateStruct) == -1)
		return -1;

	DWORD dwStyle;
	CRect rc;

	GetClientRect(rc);

	// create CPropTreeList
	//

	dwStyle = WS_VISIBLE|WS_CHILD|WS_VSCROLL;

	if (!m_List.Create(dwStyle, rc, this, 100))
	{
		TRACE0("Failed to create CPropTreeList\n");
		return -1;
	}

	m_List.SetPropOwner(this);

	// create CPropTreeInfo
	//

	dwStyle &= ~WS_VSCROLL;
    if (!m_bShowInfo) dwStyle &= ~WS_VISIBLE;

	if (!m_Info.Create(_T(""), dwStyle, rc, this))
	{
		TRACE0("Failed to create CPropTreeInfo\n");
		return -1;
	}

	m_Info.SetPropOwner(this);

	return 0;
}


CWnd* CPropTree::GetCtrlParent()
{
	return &m_List;
}


void CPropTree::OnSize(UINT nType, int cx, int cy) 
{
	CWnd::OnSize(nType, cx, cy);
	ResizeChildWindows(cx, cy);
}


void CPropTree::ResizeChildWindows(int cx, int cy)
{
	if (m_bShowInfo)
	{
		if (IsWindow(m_List.m_hWnd))
			m_List.MoveWindow(0, 0, cx, cy - m_nInfoHeight);

		if (IsWindow(m_Info.m_hWnd))
			m_Info.MoveWindow(0, cy - m_nInfoHeight, cx, m_nInfoHeight);
	}
	else
	{
		if (IsWindow(m_List.m_hWnd))
			m_List.MoveWindow(0, 0, cx, cy);
	}
}


void CPropTree::InitGlobalResources()
{
	NONCLIENTMETRICS info;
	info.cbSize = sizeof(info);

	::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(info), &info, 0);

	LOGFONT lf;
	memset(&lf, 0, sizeof (LOGFONT));

	CWindowDC dc(NULL);
	lf.lfCharSet = (BYTE)GetTextCharsetInfo(dc.GetSafeHdc(), NULL, 0);

	lf.lfHeight = info.lfMenuFont.lfHeight;
	lf.lfWeight = info.lfMenuFont.lfWeight;
	lf.lfItalic = info.lfMenuFont.lfItalic;

	// check if we should use system font
	_tcscpy(lf.lfFaceName, info.lfMenuFont.lfFaceName);

	BOOL fUseSystemFont = (info.lfMenuFont.lfCharSet > SYMBOL_CHARSET);
	if (!fUseSystemFont)
	{
		// check for "Tahoma" font existance:
		if (::EnumFontFamilies(dc.GetSafeHdc(), NULL, FontFamilyProcFonts, 0)==0)
		{
			// Found! Use MS Office font!
			_tcscpy(lf.lfFaceName, strOfficeFontName);
		}
		else
		{
			// Not found. Use default font:
			_tcscpy(lf.lfFaceName, strDefaultFontName);
		}
	}

	s_pNormalFont = new CFont;
	s_pNormalFont->CreateFontIndirect(&lf);

	lf.lfWeight = FW_BOLD;
	s_pBoldFont = new CFont;
	s_pBoldFont->CreateFontIndirect(&lf);
}


void CPropTree::FreeGlobalResources()
{
	if (s_pNormalFont)
	{
		delete s_pNormalFont;
		s_pNormalFont = NULL;
	}

	if (s_pBoldFont)
	{
		delete s_pBoldFont;
		s_pBoldFont = NULL;
	}

	// (ak) the BUTTON theme handle is a global resource like the two fonts above, so it goes
	// with them when the last CPropTree in the process is destroyed.
	ResetButtonTheme();
	ResetTreeTheme();
}


// (ak) The BUTTON theme handle, opened on first use and kept for the rest of the process.
//
// Not opened in InitGlobalResources() with the fonts: that runs from the CPropTree
// constructor, before any of these windows exists, and a theme handle is only worth holding
// while a visual style is actually in effect.
CXPTheme* CPropTree::GetButtonTheme()
{
	// IsAvailable() loads UxTheme.dll on its own first call and caches the attempt; IsActive()
	// answers for the classic theme and IsAppThemed() for a process that has opted out of
	// theming. All three are cheap enough to answer again on every paint, which is what lets a
	// style switch be picked up without a WM_THEMECHANGED having to reach this child window
	// first. Any of them failing means NULL, which is exactly what tells
	// CPropTreeItem::DrawItem() to paint with DrawFrameControl() instead.
	if (!CXPTheme::IsAvailable() || !CXPTheme::IsActive() || !CXPTheme::IsAppThemed())
		return NULL;

	// One wrapper for the life of the process. Allocated once rather than per attempt, so a
	// configuration where Open() keeps failing costs a retry and not a new/delete pair every
	// time a row is painted.
	if (!s_pButtonTheme)
		s_pButtonTheme = new CXPTheme;

	// CXPTheme converts to the HTHEME it holds, so this asks whether an earlier call already
	// succeeded and reaches into uxtheme only when it did not. Open() closes any handle it
	// holds first, so retrying is safe.
	//
	// NULL hwnd on purpose. The handle is shared by every CPropTree in the process, so it must
	// not be bound to one of them - and DrawThemeBackground() needs the class the handle was
	// opened for, not the window it came from.
	HTHEME hTheme = *s_pButtonTheme;

	if (!hTheme)
	{
		s_pButtonTheme->Open(NULL, L"BUTTON");
		hTheme = *s_pButtonTheme;
	}

	return hTheme ? s_pButtonTheme : NULL;
}


// (ak) Drop the cached handle so the next paint reopens it against whatever visual style is
// current. Called on WM_THEMECHANGED and when the last CPropTree goes away.
void CPropTree::ResetButtonTheme()
{
	if (s_pButtonTheme)
	{
		delete s_pButtonTheme;	// CXPTheme's destructor closes the HTHEME
		s_pButtonTheme = NULL;
	}
}


// (ak) The TREEVIEW theme handle, opened on first use and kept for the rest of the process.
//
// Deliberately the same shape as GetButtonTheme() above, including the three availability
// tests answered again on every paint so that a style switch is picked up without a
// WM_THEMECHANGED having to reach the child window first. Written out rather than shared with
// the BUTTON one through a helper taking the slot by reference: the two are four lines apart
// in a control that documents itself this heavily, and a reader who lands on either should
// find the whole rule there and not a call to chase. NULL hwnd for the same reason - the
// handle is shared by every CPropTree in the process and must not be bound to one of them.
CXPTheme* CPropTree::GetTreeTheme()
{
	if (!CXPTheme::IsAvailable() || !CXPTheme::IsActive() || !CXPTheme::IsAppThemed())
		return NULL;

	if (!s_pTreeTheme)
		s_pTreeTheme = new CXPTheme;

	HTHEME hTheme = *s_pTreeTheme;

	if (!hTheme)
	{
		s_pTreeTheme->Open(NULL, L"Explorer::TREEVIEW");
		hTheme = *s_pTreeTheme;
	}

	return hTheme ? s_pTreeTheme : NULL;
}


// (ak) See ResetButtonTheme(). Called beside it, from the same two places.
void CPropTree::ResetTreeTheme()
{
	if (s_pTreeTheme)
	{
		delete s_pTreeTheme;	// CXPTheme's destructor closes the HTHEME
		s_pTreeTheme = NULL;
	}
}


// (ak) Forwarded from the list, which owns the mouse and so owns both of these. Read by
// CPropTreeItem::DrawItem(), which reaches its tree through m_pProp and has no handle on the
// list - CPropTree::m_List is protected and the item is given only the CPropTree.
CPropTreeItem* CPropTree::GetHotCheckItem() const
{
	return m_List.GetHotCheckItem();
}


BOOL CPropTree::IsCheckPressed() const
{
	return m_List.IsCheckPressed();
}


CFont* CPropTree::GetNormalFont()
{
	return s_pNormalFont;
}


CFont* CPropTree::GetBoldFont()
{
	return s_pBoldFont;
}


CPropTreeItem* CPropTree::GetFocusedItem()
{
	return m_pFocus;
}


CPropTreeItem* CPropTree::GetRootItem()
{
	return &m_Root;
}


void CPropTree::ClearVisibleList()
{
	m_pVisbleList = NULL;
}


CPropTreeItem* CPropTree::GetVisibleList()
{
	return m_pVisbleList;
}


void CPropTree::AddToVisibleList(CPropTreeItem* pItem)
{
	if (!pItem)
		return;

	// check for an empty visible list
	if (!m_pVisbleList)
		m_pVisbleList = pItem;
	else
	{
		// Add the new item to the end of the list
		CPropTreeItem* pNext;

		pNext = m_pVisbleList;
		while (pNext->GetNextVisible())
			pNext = pNext->GetNextVisible();

		pNext->SetNextVisible(pItem);
	}

	pItem->SetNextVisible(NULL);
}


BOOL CPropTree::EnumItems(CPropTreeItem* pItem, ENUMPROPITEMPROC proc, LPARAM lParam)
{
	if (!pItem || !proc)
		return FALSE;

	CPropTreeItem* pNext;

	// don't count the root item in any enumerations
	if (pItem!=&m_Root && !proc(this, pItem, lParam))
		return FALSE;

	// recurse thru all child items
	pNext = pItem->GetChild();

	while (pNext)
	{
		if (!EnumItems(pNext, proc, lParam))
			return FALSE;

		pNext = pNext->GetSibling();
	}

	return TRUE;
}


void CPropTree::SetOriginOffset(LONG nOffset)
{
	// (ak) The origin is the vertical scroll offset of the item list and can never be
	// negative - a negative offset paints empty space above the first row and leaves the
	// in-place edit/spin windows behind at the position they were placed for. Callers
	// derive the offset from the scroll range, which goes negative while the content is
	// shorter than the client area, so the value is guarded here as well.
	if (nOffset < 0)
		nOffset = 0;

	m_Origin.y = nOffset;

	// (ak) Every row has just moved relative to the client area, so whichever check box the
	// cursor was over is no longer under it. The next mouse move works the hover state out
	// again; leaving it set here would keep a box lit that the rows have scrolled away from.
	m_List.ClearHotCheckItem();
}

	
void CPropTree::UpdatedItems()
{
	if (!IsWindow(m_hWnd))
		return;

	Invalidate();

	m_List.UpdateResize();
	m_List.Invalidate();
}


void CPropTree::DeleteAllItems()
{
	// (ak) before the rows go: the list may still be holding one of them as the check box the
	// cursor is over, and that pointer would dangle into the next paint.
	m_List.ClearHotCheckItem();

	Delete(NULL);
	UpdatedItems();
	m_nLastUID = 1; // reset uid counter
}


void CPropTree::DeleteItem(CPropTreeItem* pItem)
{
	// (ak) as above - the item being deleted may be the hot one
	m_List.ClearHotCheckItem();

	Delete(pItem);
	UpdatedItems();
}


LONG CPropTree::GetColumn()
{
	return m_Origin.x;
}


void CPropTree::SetColumn(LONG nColumn)
{
	CRect rc;

	GetClientRect(rc);

    // (ak)
//	if (rc.IsRectEmpty())
		// (ak) the gutter, not the bare expand column: the splitter must not be draggable into
		// the band, or the label column would start underneath it. For a tree that kept the
		// default one level this clamps to PROPTREEITEM_EXPANDCOLUMN, as it always did.
		nColumn = __max(GetGutterWidth(), nColumn);
//	else
//		nColumn = __min(__max(PROPTREEITEM_EXPANDCOLUMN, nColumn), rc.Width() - PROPTREEITEM_EXPANDCOLUMN);

	m_Origin.x = nColumn;

	Invalidate();
}


void CPropTree::Delete(CPropTreeItem* pItem)
{
	if (pItem && pItem!=&m_Root && SendNotify(PTN_DELETEITEM, pItem))
		return;

	// passing in a NULL item is the same as calling DeleteAllItems
	if (!pItem)
		pItem = &m_Root;

	// delete children

	CPropTreeItem* pIter;
	CPropTreeItem* pNext;

	pIter = pItem->GetChild();
	while (pIter)
	{
		pNext = pIter->GetSibling();
		DeleteItem(pIter);
		pIter = pNext;
	}

	// unlink from tree
	if (pItem->GetParent())
	{
		if (pItem->GetParent()->GetChild()==pItem)
			pItem->GetParent()->SetChild(pItem->GetSibling());
		else
		{
			pIter = pItem->GetParent()->GetChild();
			while (pIter->GetSibling() && pIter->GetSibling()!=pItem)
				pIter = pIter->GetSibling();

			if (pIter->GetSibling())
				pIter->SetSibling(pItem->GetSibling());
		}
	}

	if (pItem!=&m_Root)
	{
		if (pItem==GetFocusedItem())
			SetFocusedItem(NULL);
		delete pItem;
	}
    else
    {
		SetFocusedItem(NULL);
    }
    ClearVisibleList();
}


void CPropTree::SetFocusedItem(CPropTreeItem* pItem)
{
	m_pFocus = pItem;
	EnsureVisible(m_pFocus);

	if (!IsWindow(m_hWnd))
		return;

	Invalidate();
}


void CPropTree::ShowInfoText(BOOL bShow)
{
	m_bShowInfo = bShow;

	if (IsWindow(m_Info.m_hWnd))
		m_Info.ShowWindow(m_bShowInfo ? SW_SHOWNORMAL : SW_HIDE);

	if (IsWindow(m_hWnd))
	{
		CRect rc;
		GetClientRect(rc);
		ResizeChildWindows(rc.Width(), rc.Height());
	}
}

void CPropTree::SetInfoHeight(LONG nHeight)
{
	m_nInfoHeight = nHeight;
	if (IsWindow(m_hWnd))
	{
		CRect rc;
		GetClientRect(rc);
		ResizeChildWindows(rc.Width(), rc.Height());
	}
}


// (ak) see the declaration in PropTree.h for why this takes levels and not pixels
void CPropTree::SetGutterLevels(int nLevels)
{
	if (nLevels < 1)
		nLevels = 1;

	m_nGutterWidth = nLevels * PROPTREEITEM_EXPANDCOLUMN;

	if (IsWindow(m_hWnd))
	{
		// A gutter that has just grown may have swallowed the splitter; push it back out.
		// SetColumn() clamps to the new width, so passing the current position is enough.
		if (m_Origin.x < m_nGutterWidth)
			SetColumn(m_Origin.x);

		// The band, and the selection rectangle and dotted separator that start at its right
		// edge, all live in the list window, which CPropTree::Invalidate() does not reach.
		if (IsWindow(m_List.m_hWnd))
			m_List.Invalidate();
	}
}


// (ak) see the declaration in PropTree.h for what this is for and why it defaults to off
void CPropTree::SetIndentRootChildren(BOOL bIndent)
{
	m_bIndentRootChildren = bIndent;

	// every row below the top level moves, so the whole list is repainted - the items' m_loc and
	// the rectangles derived from it are recomputed by DrawItem() as it goes
	if (IsWindow(m_List.m_hWnd))
		m_List.Invalidate();
}


BOOL CPropTree::IsItemVisible(CPropTreeItem* pItem)
{
	if (!pItem)
		return FALSE;

	for (CPropTreeItem* pNext = m_pVisbleList; pNext; pNext = pNext->GetNextVisible())
	{
		if (pNext==pItem)
			return TRUE;
	}

	return FALSE;
}


// (ak) See the declaration in PropTree.h for why this walks the tree instead of
// reading CPropTreeItem::GetLocation().
//
// The walk has to agree with the paint, because the offset it produces is compared
// against a client rect in the same space. CPropTreeList::OnPaint() steps
// GetRootItem()->GetChild() then GetSibling(), accumulating what each DrawItem()
// returns, and DrawItem() returns the row's own height plus, when the row is open, its
// descendants' - which is exactly CPropTreeItem::GetTotalHeight(). So summing heights
// here gives the y the paint would have given the row.
LONG CPropTree::GetItemOffsetY(CPropTreeItem* pItem)
{
	if (!pItem)
		return -1;

	LONG y = 0;

	if (!FindItemOffsetY(&m_Root, pItem, y))
		return -1;		// not in this tree: leave the caller to fall back

	return y;
}


BOOL CPropTree::FindItemOffsetY(CPropTreeItem* pParent, CPropTreeItem* pTarget, LONG& y)
{
	for (CPropTreeItem* p = pParent->GetChild(); p; p = p->GetSibling())
	{
		if (p == pTarget)
			return TRUE;		// y already holds the offset of the row wanted

		if (p->IsExpanded() && p->GetChild())
		{
			// (ak) The recursion searches with a COPY of the offset and is only allowed to
			// commit it when it finds the target. It adds as it walks, and what it adds
			// while searching a branch is exactly that branch's descendants - the same
			// number GetTotalHeight() reports below. Passing y itself let both stand, so
			// every open group the search had already been through and not found the item
			// in was counted twice. The rows below such a group came out further down the
			// tree than CPropTreeList::OnPaint() puts them, by the whole height of the
			// group, and EnsureVisible() then read them as below the view and scrolled:
			// with one group open, everything past it jumped as soon as the doubled height
			// had accumulated, which is around the middle of the list.
			LONG nInside = y + p->GetHeight();	// step over this branch's own row ...

			if (FindItemOffsetY(p, pTarget, nInside))
			{
				y = nInside;					// ... and the target was inside
				return TRUE;
			}

			// Not inside, so the whole branch - its own row and its descendants - is
			// still between here and the target.
			y += p->GetTotalHeight();
		}
		else
			y += p->GetHeight();				// a leaf, or a branch that is closed
	}

	return FALSE;
}


LONG CPropTree::GetContentHeight()
{
	// (ak) The height CPropTreeList::OnPaint() lays out. It starts at the root's first
	// child with y = 0, so the root row - never drawn - is not content, and
	// GetRootItem()->GetTotalHeight() comes out one root height too much. The scrollbar
	// range and every clamp against it have to agree on the number, which is why it lives
	// here rather than being written out at each of them.
	LONG nHeight = 0;

	for (CPropTreeItem* pItem = m_Root.GetChild(); pItem; pItem = pItem->GetSibling())
		nHeight += pItem->GetTotalHeight();

	return nHeight;
}


void CPropTree::EnsureVisible(CPropTreeItem* pItem)
{
	if (!pItem)
		return;

	// item is not scroll visible (expand all parents)
	if (!IsItemVisible(pItem))
	{
		CPropTreeItem* pParent;

		pParent = pItem->GetParent();
		while (pParent)
		{
			pParent->Expand();
			pParent = pParent->GetParent();
		}

		UpdatedItems();
		UpdateWindow();
	}

	// (ak) Refresh the list's scroll range before deciding anything, and before
	// m_Origin.y is read below - UpdateResize() zeroes the offset when the whole tree
	// fits the client area, which is the correct offset for that case.
	//
	// The scroll at the end of this function goes through CPropTreeList::OnVScroll(),
	// which clamps the offset it is given against si.nMax read back off the scrollbar.
	// That range is only as current as the last UpdateResize(), and nothing calls one
	// when rows are inserted - CPropTree::InsertItem() does not - so a range can lag
	// behind the tree it describes, in which case nMaxPos = si.nMax - rc.Height() goes
	// negative, gets floored at si.nMin, and the offset is clamped to zero: the list
	// stays at the top with the row focused, selected and described in the info pane
	// but nowhere on screen.
	//
	// Insurance, not a fix for an observed failure: instrumenting this function showed
	// the range already equal to the tree's total height on every call in a real session,
	// so UpdateResize() changed nothing. The caller here reaches it through a page whose
	// window was created while the sheet was hidden, which is exactly the situation that
	// could leave a range stale, so the call is kept.
	//
	// Guarded on the list's window: SetFocusedItem() calls this before checking its own,
	// and UpdateResize() reads the list's client rect, which asserts on a NULL HWND.
	if (IsWindow(m_List.m_hWnd))
		m_List.UpdateResize();

	CRect rc;
	m_List.GetClientRect(rc);

    if (!rc.IsRectEmpty())
    {
	    ASSERT(IsItemVisible(pItem));

	    const LONG nRowH = pItem->GetHeight();

	    // (ak) The view in tree coordinates: OnPaint() clips the rows to the client rect
	    // deflated by the sunken border, so a row is wholly on screen only inside
	    // [nTop, nBottom). One flush to either edge is partially visible and must scroll.
	    const LONG nTop    = m_Origin.y + PROPTREELIST_BORDER;
	    const LONG nBottom = m_Origin.y + rc.Height() - PROPTREELIST_BORDER;

	    CPoint pt;

	    pt = pItem->GetLocation();

	    // (ak) The vertical half comes from the tree walk, not from m_loc, which is
	    // still (0,0) on a row that has never been painted - and (0,0) tests as inside
	    // the view whenever the list is at the top, so the scroll was skipped and the row
	    // stayed out of sight. Only y is compared: m_loc.x is the row's unscrolled indent,
	    // which says nothing about whether the row is on screen vertically.
	    // A row that is not in this tree keeps the old behaviour rather than being
	    // scrolled to a nonsense offset.
	    const LONG nOffsetY = GetItemOffsetY(pItem);

	    if (nOffsetY >= 0)
		    pt.y = nOffsetY;

	    // (ak) Scroll only when the row is not wholly inside the view, and only as far as
	    // brings it in. This used to test PtInRect() against a rect whose bottom had already
	    // been raised by the row height, and then add that height back when it worked the new
	    // offset out, so the downward scroll overshot by exactly one row. The row landed a
	    // fixed distance above the bottom, the row after it tested as outside again, and from
	    // there on every keypress scrolled one row with the highlight never moving on screen.
	    // Going up inherited an offset one row too large and stuck the highlight to the top
	    // edge in the same way.
	    if (pt.y < nTop || pt.y + nRowH > nBottom)
	    {
		    LONG oy;

		    if (pt.y < nTop)
			    oy = pt.y - PROPTREELIST_BORDER;							// above: row flush to the top
		    else
			    oy = pt.y + nRowH - (rc.Height() - PROPTREELIST_BORDER);	// below: row flush to the bottom

		    // The first row cannot be brought clear of the top border either, and
		    // OnVScroll() floors the offset, so asking for less than zero only costs a call.
		    if (oy < 0)
			    oy = 0;

		    // The last row of the list cannot be brought clear of the bottom border, so the
		    // offset asked for there comes back clamped to the one already in force. Asking
		    // anyway would scroll and repaint on every keypress at the end of the list.
		    if (oy != m_Origin.y)
			    m_List.OnVScroll(SB_THUMBTRACK, oy, NULL);
	    }
    }
}


CPropTreeItem* CPropTree::InsertItem(CPropTreeItem* pItem, CPropTreeItem* pParent)
{
	if (!pItem)
		return NULL;

	if (!pParent)
		pParent = &m_Root;

	if (!pParent->GetChild())
		pParent->SetChild(pItem);
	else
	{
		// add to end of the sibling list
		CPropTreeItem* pNext;

		pNext = pParent->GetChild();
		while (pNext->GetSibling())
			pNext = pNext->GetSibling();

		pNext->SetSibling(pItem);
	}

	pItem->SetParent(pParent);
	pItem->SetPropOwner(this);

	// auto generate a default ID
	pItem->SetCtrlID(m_nLastUID++);

	SendNotify(PTN_INSERTITEM, pItem);

	UpdatedItems();

	return pItem;
}



LONG CPropTree::HitTest(const POINT& pt)
{
	POINT p = pt;

	CPropTreeItem* pItem;

	// convert screen to tree coordinates
	p.y += m_Origin.y;

	if ((pItem = FindItem(pt))!=NULL)
	{
		if (!pItem->IsRootLevel() && pt.x >= m_Origin.x - PROPTREEITEM_COLRNG && pt.x <= m_Origin.x + PROPTREEITEM_COLRNG)
			return HTCOLUMN;

		if (pt.x > m_Origin.x + PROPTREEITEM_COLRNG)
			return HTATTRIBUTE;

		if (pItem->HitExpand(p))
			return HTEXPAND;

		if (pItem->HitCheckBox(p))
			return HTCHECKBOX;

		return HTLABEL;
	}

	return HTCLIENT;
}


CPropTreeItem* CPropTree::FindItem(const POINT& pt)
{
	CPropTreeItem* pItem;

	CPoint p = pt;

	// convert screen to tree coordinates
	p.y += m_Origin.y;

	// search the visible list for the item
	for (pItem = m_pVisbleList; pItem; pItem = pItem->GetNextVisible())
	{
		CPoint ipt = pItem->GetLocation();
		if (p.y>=ipt.y && p.y<ipt.y + pItem->GetHeight())
			return pItem;
	}

	return NULL;
}


CPropTreeItem* CPropTree::FindItem(UINT nCtrlID)
{
	s_pFound = NULL;

	EnumItems(&m_Root, EnumFindItem, nCtrlID);

	return s_pFound;
}


BOOL CALLBACK CPropTree::EnumFindItem(CPropTree*, CPropTreeItem* pItem, LPARAM lParam)
{
	ASSERT(pItem!=NULL);

	if (pItem->GetCtrlID()==(UINT)lParam)
	{
		s_pFound = pItem;
		return FALSE;
	}

	return TRUE;
}


BOOL CPropTree::IsDisableInput()
{
	return m_bDisableInput;
}


void CPropTree::DisableInput(BOOL bDisable)
{
	m_bDisableInput = bDisable;

	CWnd* pWnd;

	if ((pWnd = GetParent())!=NULL)
		pWnd->EnableWindow(!bDisable);
}


void CPropTree::SelectItems(CPropTreeItem* pItem, BOOL bSelect)
{
	if (!pItem)
		pItem = &m_Root;

	EnumItems(pItem, EnumSelectAll, (LPARAM)bSelect);
}


// ak
void CPropTree::DeactivateAll ()
{
	EnumItems(&m_Root, EnumDeactivateAll);
}


CPropTreeItem* CPropTree::FocusFirst()
{
	CPropTreeItem *pold;

	pold = m_pFocus;

	SetFocusedItem(m_pVisbleList);

	if (m_pFocus)
	{
		SelectItems(NULL, FALSE);
		m_pFocus->Select();
	}

	if (pold!=m_pFocus)
		SendNotify(PTN_SELCHANGE, m_pFocus);

	return m_pFocus;
}


CPropTreeItem* CPropTree::FocusLast()
{
	CPropTreeItem* pNext;
	CPropTreeItem* pChange;

	pChange = m_pFocus;

	pNext = m_pVisbleList;

	if (pNext)
	{
		while (pNext->GetNextVisible())
			pNext = pNext->GetNextVisible();

		SetFocusedItem(pNext);

		if (m_pFocus)
		{
			SelectItems(NULL, FALSE);
			m_pFocus->Select();
		}
	}

	if (pChange!=m_pFocus)
		SendNotify(PTN_SELCHANGE, m_pFocus);

	return pNext;
}


CPropTreeItem* CPropTree::FocusPrev()
{
	CPropTreeItem* pNext;
	CPropTreeItem* pChange;

	pChange = m_pFocus;

	if (m_pFocus==NULL)
	{
		// get the last visible item
		pNext = m_pVisbleList;
		while (pNext && pNext->GetNextVisible())
			pNext = pNext->GetNextVisible();
	}
	else
	{
		pNext = m_pVisbleList;
		while (pNext && pNext->GetNextVisible()!=m_pFocus)
			pNext = pNext->GetNextVisible();
	}

	if (pNext)
		SetFocusedItem(pNext);
	
	if (m_pFocus)
	{
		SelectItems(NULL, FALSE);
		m_pFocus->Select();
	}

	if (pChange!=m_pFocus)
		SendNotify(PTN_SELCHANGE, m_pFocus);

	return pNext;
}


CPropTreeItem* CPropTree::FocusNext()
{
	CPropTreeItem* pNext;
	CPropTreeItem* pChange;

	pChange = m_pFocus;

	if (m_pFocus==NULL)
		pNext = m_pVisbleList;
	else
	if (m_pFocus->GetNextVisible())
		pNext = m_pFocus->GetNextVisible();
	else
		pNext = NULL;

	if (pNext)
		SetFocusedItem(pNext);

	if (m_pFocus)
	{
		SelectItems(NULL, FALSE);
		m_pFocus->Select();
	}

	if (pChange!=m_pFocus)
		SendNotify(PTN_SELCHANGE, m_pFocus);

	return pNext;
}


void CPropTree::UpdateMoveAllItems()
{
	EnumItems(&m_Root, EnumMoveAll);
}


void CPropTree::RefreshItems(CPropTreeItem* pItem)
{
	if (!pItem)
		pItem = &m_Root;

	EnumItems(pItem, EnumRefreshAll);

	UpdatedItems();
}


BOOL CALLBACK CPropTree::EnumSelectAll(CPropTree*, CPropTreeItem* pItem, LPARAM lParam)
{
	if (!pItem)
		return FALSE;

	pItem->Select((BOOL)lParam);

	return TRUE;
}

BOOL CALLBACK CPropTree::EnumDeactivateAll(CPropTree*, CPropTreeItem* pItem, LPARAM)
{
	if (!pItem)
		return FALSE;

    if (pItem && pItem->IsActivated())
        pItem->CommitChanges();

	return TRUE;
}



BOOL CALLBACK CPropTree::EnumRefreshAll(CPropTree*, CPropTreeItem* pItem, LPARAM)
{
	if (!pItem)
		return FALSE;

	pItem->OnRefresh();

	return TRUE;
}


BOOL CALLBACK CPropTree::EnumMoveAll(CPropTree*, CPropTreeItem* pItem, LPARAM)
{
	if (!pItem)
		return FALSE;

	pItem->OnMove();

	return TRUE;
}


LRESULT CPropTree::SendNotify(UINT nNotifyCode, CPropTreeItem* pItem)
{
	if (!IsWindow(m_hWnd))
		return 0L;

	if (!(GetStyle() & PTS_NOTIFY))
		return 0L;

	NMPROPTREE nmmp;
	LPNMHDR lpnm;

	lpnm = NULL;

	switch (nNotifyCode)
	{
		case PTN_INSERTITEM:
		case PTN_DELETEITEM:
		case PTN_DELETEALLITEMS:
		case PTN_ITEMCHANGED:
		case PTN_ITEMBUTTONCLICK:
		case PTN_SELCHANGE:
		case PTN_ITEMEXPANDING:
		case PTN_COLUMNCLICK:
		case PTN_PROPCLICK:
		case PTN_CHECKCLICK:
			lpnm = (LPNMHDR)&nmmp;
			nmmp.pItem = pItem;
			break;
	}

	if (lpnm)
	{
		UINT_PTR id = (UINT_PTR)::GetMenu(m_hWnd);
		lpnm->code = nNotifyCode;
		lpnm->hwndFrom = m_hWnd;
		lpnm->idFrom = id;
	
		return GetParent()->SendMessage(WM_NOTIFY, (WPARAM)id, (LPARAM)lpnm);
	}

	return 0L;
}


void CPropTree::OnEnable(BOOL bEnable) 
{
	CWnd::OnEnable(bEnable);

	// (ak) A disabled tree draws its check boxes in the CBS_*DISABLED state and does not
	// highlight them, so a box that was lit when the tree went dark has to stop being - and
	// re-enabling does not put the cursor back over it, so nothing else would clear it.
	if (!bEnable)
		m_List.ClearHotCheckItem();

	Invalidate();
}


void CPropTree::OnSysColorChange() 
{
	CWnd::OnSysColorChange();
	
	Invalidate();	
}


// (ak) The user switched visual style, colour scheme or accent colour. The cached BUTTON
// handle was opened against the old one, so without this the check boxes would keep painting
// the previous look until the process ended. Dropping the handle lets the next paint reopen
// it; the repaint then picks up the new glyphs. The classic side of the same problem is
// OnSysColorChange() above.
LRESULT CPropTree::OnThemeChanged()
{
	ResetButtonTheme();
	ResetTreeTheme();

	Invalidate();

	if (IsWindow(m_List.m_hWnd))
		m_List.Invalidate();

	return 0L;
}


BOOL CPropTree::IsSingleSelection()
{
	// right now only support single selection
	return TRUE;
}