// PropTreeList.cpp : implementation file
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
#include "PropTree.rh"
#include "PropTreeList.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define PROPTREEITEM_EXPANDCOLUMN		16			// width of the expand column
#define PROPTREEITEM_COLRNG				5			// width of splitter
#define PROPTREEITEM_DEFHEIGHT			21			// default heigt of an item

extern HINSTANCE ghInst;

CPropTreeList::CPropTreeList() :
	m_pProp(NULL),
	m_BackBufferSize(0,0),
	m_bColDrag(FALSE),
	m_nPrevCol(0),
	m_pHotCheckItem(NULL),
	m_bCheckPressed(FALSE),
	m_bCheckCapture(FALSE),
	m_bTrackingLeave(FALSE)
{
}

CPropTreeList::~CPropTreeList()
{
}


BEGIN_MESSAGE_MAP(CPropTreeList, CWnd)
	//{{AFX_MSG_MAP(CPropTreeList)
	ON_WM_SIZE()
	ON_WM_PAINT()
	ON_WM_SETCURSOR()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_MOUSEWHEEL()
	ON_WM_KEYDOWN()
	ON_WM_GETDLGCODE()
	ON_WM_VSCROLL()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()


void CPropTreeList::SetPropOwner(CPropTree* pProp)
{
	m_pProp = pProp;
}


BOOL CPropTreeList::Create(DWORD dwStyle, const RECT& rect, CWnd* pParentWnd, UINT nID)
{
	CWnd* pWnd = this;

	LPCTSTR pszCreateClass = AfxRegisterWndClass(CS_HREDRAW|CS_VREDRAW|CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW));

	return pWnd->Create(pszCreateClass, _T(""), dwStyle, rect, pParentWnd, nID);
}


void CPropTreeList::OnSize(UINT nType, int cx, int cy) 
{
	CWnd::OnSize(nType, cx, cy);

	RecreateBackBuffer(cx, cy);

	ClearHotCheckItem();

	if (m_pProp)
	{
		UpdateResize();
		Invalidate();
		UpdateWindow();

		m_pProp->UpdateMoveAllItems();
	}
}


void CPropTreeList::RecreateBackBuffer(int cx, int cy)
{
	if (m_BackBufferSize.cx<cx || m_BackBufferSize.cy<cy)
	{
		m_BackBufferSize = CSize(cx, cy);

		CWindowDC dc(NULL);

		int nPlanes = dc.GetDeviceCaps(PLANES);
		int nBitCount = dc.GetDeviceCaps(BITSPIXEL);

		m_BackBuffer.DeleteObject();
		m_BackBuffer.CreateBitmap(cx, cy, nPlanes, nBitCount, NULL);
	}
}


void CPropTreeList::UpdateResize()
{
	SCROLLINFO si;
	LONG nHeight;
	CRect rc;

	ASSERT(m_pProp!=NULL);

	GetClientRect(rc);
	nHeight = rc.Height() + 1;

	ZeroMemory(&si, sizeof(SCROLLINFO));
	si.cbSize = sizeof(SCROLLINFO);
	si.fMask = SIF_RANGE|SIF_PAGE;
	si.nMin = 0;
	// (ak) The range is the height OnPaint() actually lays out. OnPaint() starts at
	// GetRootItem()->GetChild() with y = 0, so the root row is never drawn and its own
	// height is not content - GetRootItem()->GetTotalHeight() counts it, which left the
	// list able to scroll one row past the last item into blank space.
	si.nMax = m_pProp->GetContentHeight();
	si.nPage = nHeight;

	if ((int)si.nPage>si.nMax)
		m_pProp->SetOriginOffset(0);

	SetScrollInfo(SB_VERT, &si, TRUE);

	m_pProp->SetColumn(m_pProp->GetColumn());
}


void CPropTreeList::OnPaint() 
{
	CPaintDC dc(this);
	CDC memdc;
	CBitmap* pOldBitmap;

	ASSERT(m_pProp!=NULL);

	m_pProp->ClearVisibleList();

	memdc.CreateCompatibleDC(&dc);
	pOldBitmap = memdc.SelectObject(&m_BackBuffer);

	CRect rc;
	GetClientRect(rc);

	memdc.SelectObject(GetSysColorBrush(COLOR_BTNFACE));
	memdc.PatBlt(rc.left, rc.top, rc.Width(), rc.Height(), PATCOPY);

	rc.DeflateRect(PROPTREELIST_BORDER,PROPTREELIST_BORDER);
	memdc.PatBlt(rc.left, rc.top, rc.Width(), rc.Height(), m_pProp->IsWindowEnabled() ? WHITENESS : PATCOPY);
	rc.InflateRect(PROPTREELIST_BORDER,PROPTREELIST_BORDER);

	memdc.SelectObject(GetSysColorBrush(COLOR_BTNFACE));
	memdc.PatBlt(0, 0, m_pProp->GetGutterWidth(), rc.Height(), PATCOPY);

	memdc.DrawEdge(&rc, BDR_SUNKENOUTER, BF_RECT);

	CPropTreeItem* pItem;
	LONG nTotal = 0;

	ASSERT(m_pProp->GetRootItem()!=NULL);

	rc.DeflateRect(PROPTREELIST_BORDER,PROPTREELIST_BORDER);	// the rows are clipped to this

	HRGN hRgn = CreateRectRgn(rc.left, rc.top, rc.right, rc.bottom);
	SelectClipRgn(memdc.m_hDC, hRgn);

	for (pItem = m_pProp->GetRootItem()->GetChild(); pItem; pItem = pItem->GetSibling())
	{
		LONG nHeight = pItem->DrawItem(&memdc, rc, 0, nTotal);
		nTotal += nHeight;
	}

	SelectClipRgn(memdc.m_hDC, NULL);
	DeleteObject(hRgn);

	dc.GetClipBox(&rc);
	dc.BitBlt(rc.left, rc.top, rc.Width(), rc.Height(), &memdc, rc.left, rc.top, SRCCOPY);
	memdc.DeleteDC();

	m_pProp->UpdateMoveAllItems();
}


BOOL CPropTreeList::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message) 
{
	if (nHitTest==HTCLIENT)
	{
		CPoint pt;

		ASSERT(m_pProp!=NULL);

		GetCursorPos(&pt);
		ScreenToClient(&pt);

		switch (m_pProp->HitTest(pt))
		{
			case HTCOLUMN:
				SetCursor(AfxGetApp()->LoadCursor(_T("IDC_PROPTREE_SPLITTER"))); // (ak)
				return TRUE;
		}
	}

	return CWnd::OnSetCursor(pWnd, nHitTest, message);
}


    BOOL CALLBACK EnumDeactivateItems (CPropTree* pPropTree, CPropTreeItem* pItem, LPARAM param)
    {
        if (pItem && pItem->IsActivated())
            pItem->CommitChanges();

        return TRUE;
    }


void CPropTreeList::OnLButtonDown(UINT, CPoint point) 
{
	ASSERT(m_pProp!=NULL);

	if (m_pProp->IsDisableInput())
		return;

	m_pProp->SendNotify(NM_CLICK);

	if (!m_pProp->IsWindowEnabled())
		return;

	SetFocus();

	LONG nHit = m_pProp->HitTest(point);

	CPropTreeItem* pItem;
	CRect rc;
	CDC* pDC;

	switch (nHit)
	{
		case HTCOLUMN:
			if (m_pProp->SendNotify(PTN_COLUMNCLICK))
				break;

			m_bColDrag = TRUE;
			SetCapture();

			m_nPrevCol = m_pProp->GetOrigin().x;

			pDC = GetDC();
			GetClientRect(rc);
			pDC->PatBlt(m_nPrevCol - PROPTREEITEM_COLRNG/2, 0, PROPTREEITEM_COLRNG, rc.bottom, PATINVERT);
			ReleaseDC(pDC);
			break;

		case HTCHECKBOX:
			if ((pItem = m_pProp->FindItem(point))!=NULL && !pItem->IsReadOnly())
			{
				m_pHotCheckItem = pItem;
				m_bCheckPressed = TRUE;
				m_bCheckCapture = TRUE;
				SetCapture();

				if (pItem->IsRadioButton())
				{
					if (m_pProp->GetFocusedItem() != pItem)
					{
						m_pProp->SelectItems(NULL, FALSE);
						m_pProp->SetFocusedItem(pItem);
						pItem->Select();
						m_pProp->SendNotify(PTN_SELCHANGE, pItem);
					}

					pItem->SelectRadio();
				}
				else
					pItem->Check(!pItem->IsChecked());

				m_pProp->SendNotify(PTN_CHECKCLICK, pItem);
				Invalidate();
			}
			break;

		case HTEXPAND:
			if ((pItem = m_pProp->FindItem(point))!=NULL)
			{
				if (pItem->GetChild() && !m_pProp->SendNotify(PTN_ITEMEXPANDING, pItem))
				{
					pItem->Expand(!pItem->IsExpanded());

					UpdateResize();
					Invalidate();
					UpdateWindow();
					CheckVisibleFocus();
				}
			}
			break;

		default:
			if ((pItem = m_pProp->FindItem(point))!=NULL)
			{
				CPropTreeItem* pOldFocus = m_pProp->GetFocusedItem();

				m_pProp->SelectItems(NULL, FALSE);
				m_pProp->SetFocusedItem(pItem);

				pItem->Select();

				Invalidate();

				if (pItem!=pOldFocus)
					m_pProp->SendNotify(PTN_SELCHANGE, pItem);

				if (nHit==HTLABEL && pItem->IsRadioButton() && !pItem->IsReadOnly())
				{
					pItem->SelectRadio();
					m_pProp->SendNotify(PTN_CHECKCLICK, pItem);
					Invalidate();
				}
				else if (nHit==HTATTRIBUTE && !pItem->IsRootLevel())
				{
	                m_pProp->EnumItems(m_pProp->GetRootItem(), EnumDeactivateItems, NULL);

					if (!m_pProp->SendNotify(PTN_PROPCLICK, pItem) && !pItem->IsReadOnly())
						pItem->Activate();
				}
			}
			else
			{
				m_pProp->SelectItems(NULL, FALSE);
				m_pProp->SetFocusedItem(NULL);
				m_pProp->SendNotify(PTN_SELCHANGE);
				Invalidate();
			}
			break;
	}
}


void CPropTreeList::OnLButtonUp(UINT, CPoint point) 
{
	if (m_bColDrag)
	{
		CDC* pDC = GetDC();
		CRect rc;

		GetClientRect(rc);
		pDC->PatBlt(m_nPrevCol - PROPTREEITEM_COLRNG/2, 0, PROPTREEITEM_COLRNG, rc.bottom, PATINVERT);
		ReleaseDC(pDC);

		m_bColDrag = FALSE;
		ReleaseCapture();

		m_pProp->SetColumn(point.x);
		m_pProp->UpdateMoveAllItems();
		Invalidate();
	}

	if (m_bCheckPressed || m_bCheckCapture)
	{
		m_bCheckPressed = FALSE;
		m_bCheckCapture = FALSE;

		InvalidateItemRow(m_pHotCheckItem);
	}

	if (GetCapture()==this)
		ReleaseCapture();
}


void CPropTreeList::OnLButtonDblClk(UINT, CPoint point)
{
	ASSERT(m_pProp!=NULL);

	m_pProp->SendNotify(NM_DBLCLK);

	CPropTreeItem* pItem;
	CPropTreeItem* pOldFocus;

	if ((pItem = m_pProp->FindItem(point))!=NULL && pItem->GetChild())
	{
		switch (m_pProp->HitTest(point))
		{
			case HTCOLUMN:
				break;

			case HTCHECKBOX:
				if (!pItem->IsReadOnly())
				{
					if (pItem->IsRadioButton())
						pItem->SelectRadio();
					else
						pItem->Check(!pItem->IsChecked());

					m_pProp->SendNotify(PTN_CHECKCLICK, pItem);
					Invalidate();
				}
				break;

			case HTATTRIBUTE:
				if (!pItem->IsRootLevel())
					break;

			default:
				pOldFocus = m_pProp->GetFocusedItem();
				m_pProp->SelectItems(NULL, FALSE);
				m_pProp->SetFocusedItem(pItem);
				pItem->Select();

				if (pItem!=pOldFocus)
					m_pProp->SendNotify(PTN_SELCHANGE, pItem);

			case HTEXPAND:
				if (!m_pProp->SendNotify(PTN_ITEMEXPANDING, pItem))
				{
					pItem->Expand(!pItem->IsExpanded());

					UpdateResize();
					Invalidate();
					UpdateWindow();
					CheckVisibleFocus();
				}
				break;
		}
	}
}


void CPropTreeList::OnMouseMove(UINT, CPoint point)
{
	if (m_bColDrag)
	{
		CDC* pDC = GetDC();
		CRect rc;

		GetClientRect(rc);
		pDC->PatBlt(m_nPrevCol - PROPTREEITEM_COLRNG/2, 0, PROPTREEITEM_COLRNG, rc.bottom, PATINVERT);
		pDC->PatBlt(point.x - PROPTREEITEM_COLRNG/2, 0, PROPTREEITEM_COLRNG, rc.bottom, PATINVERT);
		m_nPrevCol = point.x;
		ReleaseDC(pDC);
	}
	else
	{
		TrackLeave();
		UpdateHotCheckItem(point);
	}
}


void CPropTreeList::TrackLeave()
{
	if (m_bTrackingLeave)
		return;

	TRACKMOUSEEVENT tme;
	tme.cbSize = sizeof(TRACKMOUSEEVENT);
	tme.dwFlags = TME_LEAVE;
	tme.hwndTrack = m_hWnd;
	tme.dwHoverTime = 0;

	if (_TrackMouseEvent(&tme))
		m_bTrackingLeave = TRUE;
}


void CPropTreeList::OnMouseLeave()
{
	m_bTrackingLeave = FALSE;

	CPropTreeItem* pOld = m_pHotCheckItem;

	m_pHotCheckItem = NULL;

	m_bCheckPressed = FALSE;
	m_bCheckCapture = FALSE;

	if (GetCapture()==this)
		ReleaseCapture();

	InvalidateItemRow(pOld);
}


void CPropTreeList::UpdateHotCheckItem(const CPoint& pt)
{
	ASSERT(m_pProp!=NULL);

	CPropTreeItem* pHot = NULL;

	if (!m_bColDrag && m_pProp->IsWindowEnabled() && !m_pProp->IsDisableInput())
	{
		if (m_pProp->HitTest(pt)==HTCHECKBOX)
		{
			CPropTreeItem* pItem = m_pProp->FindItem(pt);

			if (pItem && !pItem->IsReadOnly())
				pHot = pItem;
		}
	}

	if (pHot==m_pHotCheckItem)
		return;

	CPropTreeItem* pOld = m_pHotCheckItem;

	m_pHotCheckItem = pHot;

	if (!pHot)
		m_bCheckPressed = FALSE;

	InvalidateItemRow(pOld);
	InvalidateItemRow(pHot);
}


void CPropTreeList::InvalidateItemRow(CPropTreeItem* pItem)
{
	if (!pItem || !m_pProp || !IsWindow(m_hWnd))
		return;

	const POINT& loc = pItem->GetLocation();

	CRect rc;
	GetClientRect(rc);

	CRect rr(rc.left, loc.y - m_pProp->GetOrigin().y, rc.right,
		loc.y - m_pProp->GetOrigin().y + pItem->GetHeight());

	rr.IntersectRect(&rr, &rc);

	if (rr.IsRectEmpty())
		Invalidate();
	else
		InvalidateRect(&rr, FALSE);
}


void CPropTreeList::ClearHotCheckItem()
{
	m_pHotCheckItem = NULL;
	m_bCheckPressed = FALSE;
	m_bCheckCapture = FALSE;
}


BOOL CPropTreeList::OnMouseWheel(UINT, short zDelta, CPoint) 
{
	SCROLLINFO si;

	ZeroMemory(&si, sizeof(SCROLLINFO));
	si.cbSize = sizeof(SCROLLINFO);
	si.fMask = SIF_RANGE;

	GetScrollInfo(SB_VERT, &si);

	CRect rc;
	GetClientRect(rc);

	if (si.nMax - si.nMin < rc.Height())
		return TRUE;

	SetFocus();
	OnVScroll(zDelta < 0 ? SB_LINEDOWN : SB_LINEUP, 0, NULL);

	return TRUE;
}


void CPropTreeList::OnKeyDown(UINT nChar, UINT, UINT) 
{

	CPropTreeItem* pItem;

	ASSERT(m_pProp!=NULL);

	if (m_pProp->IsDisableInput() || !m_pProp->IsWindowEnabled())
		return;

	switch (nChar)
	{
		case VK_RETURN:
			if ((pItem = m_pProp->GetFocusedItem())!=NULL && !pItem->IsRootLevel() && !pItem->IsReadOnly())
			{
				const BOOL bWasActivated = pItem->IsActivated();

				m_pProp->EnumItems(m_pProp->GetRootItem(), EnumDeactivateItems, NULL);

				if (!bWasActivated)
					pItem->Activate();
			}
			break;

		case VK_SPACE:
			if ((pItem = m_pProp->GetFocusedItem())!=NULL && pItem->IsCheckBox() && !pItem->IsReadOnly())
			{
				if (pItem->IsRadioButton())
					pItem->SelectRadio();
				else
					pItem->Check(!pItem->IsChecked());

				m_pProp->SendNotify(PTN_CHECKCLICK, pItem);
				Invalidate();
			}
			break;

		case VK_HOME:
			if (m_pProp->FocusFirst())
				Invalidate();
			break;

		case VK_END:
			if (m_pProp->FocusLast())
				Invalidate();
			break;

		case VK_LEFT:
			if ((pItem = m_pProp->GetFocusedItem())!=NULL)
			{
				if (!m_pProp->SendNotify(PTN_ITEMEXPANDING, pItem))
				{
					if (pItem->GetChild() && pItem->IsExpanded())
					{
						pItem->Expand(FALSE);
						UpdateResize();
						Invalidate();
						UpdateWindow();
						CheckVisibleFocus();
						break;
					}
				}
			}
			else
				break;
		case VK_UP:
			if (m_pProp->FocusPrev())
				Invalidate();
			break;

		case VK_RIGHT:
			if ((pItem = m_pProp->GetFocusedItem())!=NULL)
			{
				if (!m_pProp->SendNotify(PTN_ITEMEXPANDING, pItem))
				{
					if (pItem->GetChild() && !pItem->IsExpanded())
					{
						pItem->Expand();
						UpdateResize();
						Invalidate();
						UpdateWindow();
						CheckVisibleFocus();
						break;
					}
				}
			}
			else
				break;
		case VK_DOWN:
			if (m_pProp->FocusNext())
				Invalidate();
			break;
	}
}


UINT CPropTreeList::OnGetDlgCode() 
{
	const MSG* pMsg = GetCurrentMessage();
	if (pMsg && pMsg->message == WM_GETDLGCODE)
	{
		UINT nChar = (UINT)pMsg->wParam;
		if (nChar == VK_TAB || nChar == VK_ESCAPE)
			return DLGC_WANTARROWS|DLGC_WANTCHARS;
	}
	return DLGC_WANTARROWS|DLGC_WANTCHARS|DLGC_WANTALLKEYS;
}


void CPropTreeList::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar*) 
{
	SCROLLINFO si;
	CRect rc;
	LONG nHeight;

	switch (nSBCode)
	{
		case SB_LINEUP:
		case SB_LINEDOWN:
		case SB_PAGEUP:
		case SB_PAGEDOWN:
		case SB_THUMBTRACK:
			break;

		default:
			return;
	}

	SetFocus();

	GetClientRect(rc);
	nHeight = rc.Height() + 1;

	ZeroMemory(&si, sizeof(SCROLLINFO));
	si.cbSize = sizeof(SCROLLINFO);
	si.fMask = SIF_RANGE;

	GetScrollInfo(SB_VERT, &si);

	LONG ny = m_pProp->GetOrigin().y;

	switch (nSBCode)
	{
		case SB_LINEDOWN:
			ny += PROPTREEITEM_DEFHEIGHT;
			break;

		case SB_LINEUP:
			ny -= PROPTREEITEM_DEFHEIGHT;
			break;

		case SB_PAGEDOWN:
			ny += nHeight;
			break;

		case SB_PAGEUP:
			ny -= nHeight;
			break;

		case SB_THUMBTRACK:
			ny = nPos;
			break;
	}

	// (ak) The furthest useful offset is the one that brings the last content pixel into
	// the band the rows are clipped to. OnPaint() deflates the client rect by the border
	// and clips to it, so that band is two pixels shorter than the client area: measured
	// against the full height the list stops two pixels short of the end and the last row
	// stays permanently clipped, which in turn makes EnsureVisible() ask for an offset this
	// clamp rejects on every focus change that lands on that row.
	//
	// nHeight carries the +1 that keeps the scrollbar hidden while the content exactly
	// fits, so it is not the measure to clamp against here. While the content does fit,
	// UpdateResize() hides the bar and puts the offset back to zero, so the offset must not
	// be allowed to grow here behind its back.
	LONG nMaxPos;

	if (si.nMax > rc.Height())
		nMaxPos = si.nMax - (rc.Height() - PROPTREELIST_BORDER);
	else
		nMaxPos = si.nMin;

	if (nMaxPos < si.nMin)
		nMaxPos = si.nMin;

	ny = __min(__max(ny, si.nMin), nMaxPos);

	m_pProp->SetOriginOffset(ny);
	si.fMask = SIF_POS;
	si.nPos = ny;

	SetScrollInfo(SB_VERT, &si, TRUE);
	Invalidate();
}


void CPropTreeList::CheckVisibleFocus()
{
	ASSERT(m_pProp!=NULL);

	CPropTreeItem* pItem;
	
	if ((pItem = m_pProp->GetFocusedItem())==NULL)
		return;

	if (!m_pProp->IsItemVisible(pItem))
	{
		if (m_pProp->IsSingleSelection())
			pItem->Select(FALSE);

		m_pProp->SetFocusedItem(NULL);
		m_pProp->SendNotify(PTN_SELCHANGE, NULL);

		Invalidate();
	}
}
