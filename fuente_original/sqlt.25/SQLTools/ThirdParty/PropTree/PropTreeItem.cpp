// PropTreeItem.cpp
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

#include "PropTreeItem.h"

#include "XPTheme.h"

#include <gdiplus.h>

#include <math.h>

#define PROPTREEITEM_DEFHEIGHT			21			// default heigt of an item
#define PROPTREEITEM_SPACE				5			// default horz spacing
#define PROPTREEITEM_EXPANDBOX			12			// size of the expand box
#define PROPTREEITEM_CHECKBOX			14			// size of the check box
#define PROPTREEITEM_EXPANDCOLUMN		16			// width of the expand column
#define PNINDENT						16			// child level indent

#define PROPTREEITEM_EXPANDBOXHALF		(PROPTREEITEM_EXPANDBOX/2)

#define PROPTREEITEM_EXPANDANGLE		(53.13f + 25.0f)


static void _DotHLine(HDC hdc, LONG x, LONG y, LONG w)
{
	for (; w>0; w-=2, x+=2)
		SetPixel(hdc, x, y, GetSysColor(COLOR_BTNSHADOW));
}


static void _DrawExpand(HDC hdc, LONG x, LONG y, BOOL bExpand)
{
	using namespace Gdiplus;

	const REAL stroke = 1.55f;

	const REAL inset = PROPTREEITEM_EXPANDBOX * 0.194f;

	const REAL span = PROPTREEITEM_EXPANDBOX - 2 * inset;
	const REAL halfAngle = static_cast<REAL>(PROPTREEITEM_EXPANDANGLE / 2 * 3.14159265358979 / 180);
	const REAL depth = static_cast<REAL>(span / 2 / tan(halfAngle));

	const REAL x0 = static_cast<REAL>(x);
	const REAL y0 = static_cast<REAL>(y);

	const REAL tipOfs = (PROPTREEITEM_EXPANDBOX - span) / 2;		// == inset
	const REAL apexOfs = (PROPTREEITEM_EXPANDBOX - depth) / 2;
	const REAL centre = PROPTREEITEM_EXPANDBOX / 2.0f;

	PointF pts[3];

	if (bExpand)
	{
		pts[0] = PointF(x0 + tipOfs, y0 + apexOfs);					// left tip,
		pts[1] = PointF(x0 + centre, y0 + apexOfs + depth);			// the apex below the middle,
		pts[2] = PointF(x0 + tipOfs + span, y0 + apexOfs);			// the right tip
	}
	else
	{
		pts[0] = PointF(x0 + apexOfs, y0 + tipOfs);					// top tip,
		pts[1] = PointF(x0 + apexOfs + depth, y0 + centre);			// the apex right of the middle,
		pts[2] = PointF(x0 + apexOfs, y0 + tipOfs + span);			// the bottom tip
	}

	const COLORREF cr = GetSysColor(COLOR_BTNTEXT);

	Graphics g(hdc);
	g.SetSmoothingMode(SmoothingModeAntiAlias);

	Pen pen(Color(255, GetRValue(cr), GetGValue(cr), GetBValue(cr)), stroke);
	pen.SetLineJoin(LineJoinRound);
	pen.SetStartCap(LineCapRound);
	pen.SetEndCap(LineCapRound);

	g.DrawLines(&pen, pts, 3);
}

CPropTreeItem::CPropTreeItem() :
	m_pProp(NULL),
	m_sLabel(_T("")),
	m_sInfo(_T("")),
	m_loc(0,0),
	m_rc(0,0,0,0),
	m_lParam(0),
	m_nCtrlID(0),
	m_dwState(0),
	m_bCommitOnce(FALSE),
	m_nIndent(0),
	m_rcExpand(0,0,0,0),
	m_rcCheckbox(0,0,0,0),
	m_pParent(NULL),
	m_pSibling(NULL),
	m_pChild(NULL),
	m_pVis(NULL)
{
}


CPropTreeItem::~CPropTreeItem()
{
}


BOOL CPropTreeItem::IsExpanded()
{
	return (m_dwState & TreeItemExpanded) ? TRUE : FALSE;
}


BOOL CPropTreeItem::IsSelected()
{
	return (m_dwState & TreeItemSelected) ? TRUE : FALSE;
}


BOOL CPropTreeItem::IsChecked()
{
	return (m_dwState & TreeItemChecked) ? TRUE : FALSE;
}


BOOL CPropTreeItem::IsReadOnly()
{
	return (m_dwState & TreeItemReadOnly) ? TRUE : FALSE;
}


BOOL CPropTreeItem::IsActivated()
{
	return (m_dwState & TreeItemActivated) ? TRUE : FALSE;
}


void CPropTreeItem::Select(BOOL bSelect)
{
	if (bSelect)
		m_dwState |= TreeItemSelected;
	else
		m_dwState &= ~TreeItemSelected;
}


void CPropTreeItem::Expand(BOOL bExpand)
{
	if (bExpand)
		m_dwState |= TreeItemExpanded;
	else
		m_dwState &= ~TreeItemExpanded;
}


void CPropTreeItem::Check(BOOL bCheck)
{
	if (bCheck)
		m_dwState |= TreeItemChecked;
	else
		m_dwState &= ~TreeItemChecked;
}


void CPropTreeItem::ReadOnly(BOOL bReadOnly)
{
	if (bReadOnly)
		m_dwState |= TreeItemReadOnly;
	else
		m_dwState &= ~TreeItemReadOnly;
}


BOOL CPropTreeItem::IsCheckBox()
{
	return (m_dwState & TreeItemCheckbox) ? TRUE : FALSE;
}


void CPropTreeItem::HasCheckBox(BOOL bCheckbox)
{
	if (bCheckbox)
		m_dwState |= TreeItemCheckbox;
	else
		m_dwState &= ~TreeItemCheckbox;
}


BOOL CPropTreeItem::IsRadioButton()
{
	return (m_dwState & TreeItemRadioButton) ? TRUE : FALSE;
}


void CPropTreeItem::HasRadioButton(BOOL bRadio)
{
	if (bRadio)
		m_dwState |= TreeItemCheckbox | TreeItemRadioButton;
	else
		m_dwState &= ~(TreeItemCheckbox | TreeItemRadioButton);
}


void CPropTreeItem::SelectRadio()
{
	if (!IsRadioButton())
		return;

	CPropTreeItem* pParent = GetParent();

	if (pParent)
	{
		for (CPropTreeItem* p = pParent->GetChild(); p!=NULL; p = p->GetSibling())
		{
			if (p!=this && p->IsRadioButton())
				p->Check(FALSE);
		}
	}

	Check(TRUE);
}


BOOL CPropTreeItem::HitExpand(const POINT& pt)
{
	return m_rcExpand.PtInRect(pt);
}


BOOL CPropTreeItem::HitCheckBox(const POINT& pt)
{
	return m_rcCheckbox.PtInRect(pt);
}


BOOL CPropTreeItem::IsRootLevel()
{
	ASSERT(m_pProp!=NULL);
	return GetParent() == m_pProp->GetRootItem();
}


LONG CPropTreeItem::GetTotalHeight()
{
	CPropTreeItem* pItem;
	LONG nHeight;

	nHeight = GetHeight();

	if (IsExpanded())
	{
		for (pItem = GetChild(); pItem; pItem = pItem->GetSibling())
			nHeight += pItem->GetTotalHeight();
	}

	return nHeight;
}


void CPropTreeItem::SetLabelText(LPCTSTR sLabel)
{
	m_sLabel = sLabel;
}


LPCTSTR CPropTreeItem::GetLabelText()
{
	return m_sLabel;
}


void CPropTreeItem::SetInfoText(LPCTSTR sInfo)
{
	m_sInfo = sInfo;
}


LPCTSTR CPropTreeItem::GetInfoText()
{
	return m_sInfo;
}


void CPropTreeItem::SetIndent(int nIndent)
{
	m_nIndent = nIndent;
}


int CPropTreeItem::GetIndent() const
{
	return m_nIndent;
}


int CPropTreeItem::GetLevelIndent()
{
	return PNINDENT;
}


void CPropTreeItem::SetCtrlID(UINT nCtrlID)
{
	m_nCtrlID = nCtrlID;
}


UINT CPropTreeItem::GetCtrlID()
{
	return m_nCtrlID;
}


LONG CPropTreeItem::GetHeight()
{
	return PROPTREEITEM_DEFHEIGHT;
}


void CPropTreeItem::OnMove()
{
}


void CPropTreeItem::OnRefresh()
{
}


void CPropTreeItem::OnCommit()
{
}


void CPropTreeItem::Activate()
{
	m_dwState |= TreeItemActivated;
	m_bCommitOnce = FALSE;

	OnActivate();
}


void CPropTreeItem::CommitChanges()
{
	m_dwState &= ~TreeItemActivated;

	if (m_bCommitOnce)
		return;

	m_bCommitOnce = TRUE;

	ASSERT(m_pProp!=NULL);

	OnCommit();

	m_pProp->SendNotify(PTN_ITEMCHANGED, this);
	m_pProp->RefreshItems(this);
}

void CPropTreeItem::Cancel ()
{
	m_dwState &= ~TreeItemActivated;
	OnCancel();
}

void CPropTreeItem::OnActivate()
{
}


void CPropTreeItem::SetPropOwner(CPropTree* pProp)
{
	m_pProp = pProp;
}


const POINT& CPropTreeItem::GetLocation()
{
	return m_loc;
}


CPropTreeItem* CPropTreeItem::GetParent()
{
	return m_pParent;
}


CPropTreeItem* CPropTreeItem::GetSibling()
{
	return m_pSibling;
}


CPropTreeItem* CPropTreeItem::GetChild()
{
	return m_pChild;
}


CPropTreeItem* CPropTreeItem::GetNextVisible()
{
	return m_pVis;
}


void CPropTreeItem::SetParent(CPropTreeItem* pParent)
{
	m_pParent = pParent;
}


void CPropTreeItem::SetSibling(CPropTreeItem* pSibling)
{
	m_pSibling = pSibling;
}


void CPropTreeItem::SetChild(CPropTreeItem* pChild)
{
	m_pChild = pChild;
}


void CPropTreeItem::SetNextVisible(CPropTreeItem* pVis)
{
	m_pVis = pVis;
}


LONG CPropTreeItem::DrawItem(CDC* pDC, const RECT& rc, LONG x, LONG y)
{
	CPoint pt;
	LONG nTotal, nCol, ey;
	CRect drc, ir;

	ASSERT(m_pProp!=NULL);

	m_pProp->AddToVisibleList(this);

	m_loc = CPoint(x, y);

	m_rc.SetRect(m_pProp->GetOrigin().x + PROPTREEITEM_SPACE, m_loc.y, rc.right, m_loc.y + GetHeight()-1);
	m_rc.OffsetRect(0, -m_pProp->GetOrigin().y);

	nTotal = GetHeight();
	ey = (nTotal >> 1) - (PROPTREEITEM_EXPANDBOX >> 1) - 2;

	bool bCheck = false;

	pt = m_loc;
	pt.y -= m_pProp->GetOrigin().y;
	nCol = m_pProp->GetOrigin().x;

	if (IsRootLevel())
		drc.SetRect(pt.x + PROPTREEITEM_EXPANDCOLUMN, pt.y, rc.right, pt.y + nTotal);
	else
		drc.SetRect(pt.x + PROPTREEITEM_EXPANDCOLUMN, pt.y, nCol, pt.y + nTotal);

	if (m_nIndent)
		drc.left += m_nIndent;

	if (IsRootLevel())
	{
		HGDIOBJ hOld = pDC->SelectObject(GetSysColorBrush(COLOR_BTNFACE));
		pDC->PatBlt(rc.left, drc.top, rc.right - rc.left + 1, drc.Height(), PATCOPY);
		pDC->SelectObject(hOld);
	}

	if (GetChild())
	{
		m_rcExpand.left = m_loc.x + PROPTREEITEM_EXPANDCOLUMN/2 - PROPTREEITEM_EXPANDBOXHALF;
		m_rcExpand.top = m_loc.y + ey;
		m_rcExpand.right = m_rcExpand.left + PROPTREEITEM_EXPANDBOX - 1;
		m_rcExpand.bottom = m_rcExpand.top + PROPTREEITEM_EXPANDBOX - 1;

		ir = m_rcExpand;
		ir.OffsetRect(0, -m_pProp->GetOrigin().y);
		_DrawExpand(pDC->m_hDC, ir.left, ir.top, IsExpanded());
	}
	else
		m_rcExpand.SetRectEmpty();

	if (IsCheckBox())
	{
		bCheck = true;

		ir.left = drc.left + PROPTREEITEM_SPACE;
		ir.top = m_loc.y + ey + 2;

		ir.right = ir.left + PROPTREEITEM_CHECKBOX;
		ir.bottom = ir.top + PROPTREEITEM_CHECKBOX;

		m_rcCheckbox = ir;
	}
	else
		m_rcCheckbox.SetRectEmpty();

	HRGN hRgn = NULL;

	if (!IsRootLevel())
	{
		hRgn = CreateRectRgn(drc.left, drc.top, drc.right, drc.bottom);
		SelectClipRgn(pDC->m_hDC, hRgn);
	}

	ir = drc;
	ir.left += PROPTREEITEM_SPACE;
	ir.OffsetRect(0, 2);

	if (bCheck)
		OffsetRect(&ir, PROPTREEITEM_CHECKBOX + PROPTREEITEM_SPACE * 2, 0);

	if (!m_sLabel.IsEmpty())
	{
		if (IsRootLevel())
			pDC->SelectObject(CPropTree::GetBoldFont());
		else
			pDC->SelectObject(CPropTree::GetNormalFont());

		pDC->SetTextColor(GetSysColor(COLOR_BTNTEXT));
		pDC->SetBkMode(TRANSPARENT);
		pDC->DrawText(m_sLabel, &ir, DT_SINGLELINE|DT_VCENTER|DT_CALCRECT|DT_NOPREFIX);

		BOOL bSelThemed = FALSE;

		if (IsSelected())
		{
			
			CRect dr;
			dr = drc;
            dr.right += 1; // (ak)

			CXPTheme* pSelTheme = CPropTree::GetTreeTheme();

			if (pSelTheme && CXPTheme::IsActive() && CXPTheme::IsAppThemed())
				bSelThemed = SUCCEEDED(pSelTheme->DrawBackground(pDC->m_hDC,
					TVP_TREEITEM, TREIS_SELECTED, &dr, NULL));
			
			if (!bSelThemed)
			{
				HGDIOBJ oPen = pDC->SelectObject(GetStockObject(NULL_PEN));
				HGDIOBJ oBrush = pDC->SelectObject(GetSysColorBrush(COLOR_HIGHLIGHT));

				pDC->Rectangle(&dr);

				pDC->SelectObject(oPen);
				pDC->SelectObject(oBrush);

				pDC->SetTextColor(GetSysColor(COLOR_BTNHIGHLIGHT));
			}
		}

		if ((!m_pProp->IsWindowEnabled() || IsReadOnly()) && (!IsSelected() || bSelThemed))
			pDC->SetTextColor(GetSysColor(COLOR_GRAYTEXT));

		pDC->DrawText(m_sLabel, &ir, DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
	}

	if (IsCheckBox())
	{
		ir = m_rcCheckbox;
		ir.OffsetRect(0, -m_pProp->GetOrigin().y);

		const BOOL bDisabled = !m_pProp->IsWindowEnabled() || IsReadOnly();

		const BOOL bRadio = IsRadioButton();
		const int nPart = bRadio ? BP_RADIOBUTTON : BP_CHECKBOX;

		BOOL bDrawn = FALSE;

		CXPTheme* pTheme = CPropTree::GetButtonTheme();

		if (pTheme && CXPTheme::IsActive() && CXPTheme::IsAppThemed())
		{
			int nState;

			if (bDisabled)
				nState = IsChecked() ? (bRadio ? RBS_CHECKEDDISABLED : CBS_CHECKEDDISABLED)
					: (bRadio ? RBS_UNCHECKEDDISABLED : CBS_UNCHECKEDDISABLED);
			else if (m_pProp->GetHotCheckItem()==this)
			{
				if (m_pProp->IsCheckPressed())
					nState = IsChecked() ? (bRadio ? RBS_CHECKEDPRESSED : CBS_CHECKEDPRESSED)
						: (bRadio ? RBS_UNCHECKEDPRESSED : CBS_UNCHECKEDPRESSED);
				else
					nState = IsChecked() ? (bRadio ? RBS_CHECKEDHOT : CBS_CHECKEDHOT)
						: (bRadio ? RBS_UNCHECKEDHOT : CBS_UNCHECKEDHOT);
			}
			else
				nState = IsChecked() ? (bRadio ? RBS_CHECKEDNORMAL : CBS_CHECKEDNORMAL)
					: (bRadio ? RBS_UNCHECKEDNORMAL : CBS_UNCHECKEDNORMAL);

			CRect rcDraw = ir;
			SIZE sz = { 0, 0 };

			if (SUCCEEDED(pTheme->GetPartSize(pDC->m_hDC, nPart, nState, NULL, TS_TRUE, &sz))
				&& sz.cx > 0 && sz.cy > 0 && sz.cx <= ir.Width() && sz.cy <= ir.Height())
			{
				rcDraw.left = ir.left + (ir.Width() - sz.cx) / 2;
				rcDraw.top = ir.top + (ir.Height() - sz.cy) / 2;
				rcDraw.right = rcDraw.left + sz.cx;
				rcDraw.bottom = rcDraw.top + sz.cy;
			}

			bDrawn = SUCCEEDED(pTheme->DrawBackground(pDC->m_hDC, nPart, nState, &rcDraw, NULL));
		}

		if (!bDrawn)
		{
			pDC->DrawFrameControl(&ir, DFC_BUTTON, (bRadio ? DFCS_BUTTONRADIO : DFCS_BUTTONCHECK)
				| (IsChecked() ? DFCS_CHECKED : 0)
				| (bDisabled ? DFCS_INACTIVE : 0));
		}
	}

	if (hRgn)
	{
		SelectClipRgn(pDC->m_hDC, NULL);
		DeleteObject(hRgn);
	}

	_DotHLine(pDC->m_hDC, m_pProp->GetGutterWidth(), pt.y + nTotal - 1,
		rc.right - m_pProp->GetGutterWidth() + 1);

	if (!IsRootLevel())
	{
		CPen pn1(PS_SOLID, 1, GetSysColor(COLOR_BTNSHADOW));
		CPen* pOld;

		pOld = pDC->SelectObject(&pn1);
		pDC->MoveTo(nCol, drc.top);
		pDC->LineTo(nCol, drc.bottom);

		CPen pn2(PS_SOLID, 1, GetSysColor(COLOR_BTNHIGHLIGHT));
		pDC->SelectObject(&pn2);
		pDC->MoveTo(nCol + 1, drc.top);
		pDC->LineTo(nCol + 1, drc.bottom);

		pDC->SelectObject(pOld);
	}

	if (!IsRootLevel())
	{
		hRgn = CreateRectRgn(m_rc.left, m_rc.top, m_rc.right, m_rc.bottom);
		SelectClipRgn(pDC->m_hDC, hRgn);
		
		DrawAttribute(pDC, m_rc);

		SelectClipRgn(pDC->m_hDC, NULL);
		DeleteObject(hRgn);
	}

	if (GetChild() && IsExpanded())
	{
		y += nTotal;

		CPropTreeItem* pNext;

		for (pNext = GetChild(); pNext; pNext = pNext->GetSibling())
		{
			LONG nIndent = (IsRootLevel() && !m_pProp->GetIndentRootChildren()) ? 0 : PNINDENT;

			LONG nHeight = pNext->DrawItem(pDC, rc, x + nIndent, y);
			nTotal += nHeight;
			y += nHeight;
		}
	}

	return nTotal;
}


void CPropTreeItem::DrawAttribute(CDC*, const RECT&)
{
}
