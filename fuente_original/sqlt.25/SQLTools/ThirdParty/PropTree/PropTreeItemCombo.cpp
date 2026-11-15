// PropTreeItemCombo.cpp : implementation file
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
#include "PropTreeItemCombo.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define DROPDOWN_HEIGHT			100

/////////////////////////////////////////////////////////////////////////////
// CPropTreeItemCombo

CPropTreeItemCombo::CPropTreeItemCombo() :
	m_lComboData(0),
	m_nDropHeight(DROPDOWN_HEIGHT),
	m_bTextMode(FALSE)
{
}

CPropTreeItemCombo::~CPropTreeItemCombo()
{
}


BEGIN_MESSAGE_MAP(CPropTreeItemCombo, CComboBox)
	//{{AFX_MSG_MAP(CPropTreeItemCombo)
	ON_CONTROL_REFLECT(CBN_KILLFOCUS, OnKillfocus)
	//}}AFX_MSG_MAP
    ON_WM_CHAR()
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CPropTreeItemCombo message handlers

void CPropTreeItemCombo::DrawAttribute(CDC* pDC, const RECT& rc)
{
	ASSERT(m_pProp!=NULL);

	// verify the window has been created
	if (!IsWindow(m_hWnd))
	{
		TRACE0("CPropTreeItemCombo::DrawAttribute() - The window has not been created\n");
		return;
	}

	pDC->SelectObject(m_pProp->GetNormalFont());
	pDC->SetTextColor(GetSysColor(!IsReadOnly() ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT));
	pDC->SetBkMode(TRANSPARENT);

	CRect r = rc;
	r.InflateRect(-2,-2); // (ak)
	CString s;
	LONG idx;

	if (m_bTextMode)
		s = m_sText;
	else if ((idx = GetCurSel())!=CB_ERR)
		GetLBText(idx, s);
	else
		s = _T("");

	pDC->DrawText(s, r, DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
}


LPARAM CPropTreeItemCombo::GetItemValue()
{
	return m_lComboData;
}


void CPropTreeItemCombo::SetItemValue(LPARAM lParam)
{
	m_lComboData = lParam;
	OnRefresh();
}


void CPropTreeItemCombo::SetItemValueText(LPCTSTR sText)
{
	m_bTextMode = TRUE;
	m_sText = sText;
	OnRefresh();
}


void CPropTreeItemCombo::OnMove()
{
	if (IsWindow(m_hWnd) && IsWindowVisible())
		SetWindowPos(NULL, m_rc.left-5, m_rc.top, m_rc.Width() + 5, m_rc.Height(), SWP_NOZORDER|SWP_SHOWWINDOW);
}


void CPropTreeItemCombo::OnRefresh()
{
	if (m_bTextMode)
	{
		if (IsWindow(m_hWnd))
			SetWindowText(m_sText);
		return;
	}

	LONG idx = FindCBData(m_lComboData);

	if (idx!=CB_ERR)
		SetCurSel(idx);
}


void CPropTreeItemCombo::OnCommit()
{
	if (m_bTextMode)
	{
		GetWindowText(m_sText);
		ShowWindow(SW_HIDE);
		return;
	}

	LONG idx;

	// store combo box item data
	if ((idx = GetCurSel())==CB_ERR)
		m_lComboData = 0;
	else
		m_lComboData = (LPARAM)GetItemData(idx);

	ShowWindow(SW_HIDE);
}


void CPropTreeItemCombo::OnActivate()
{
	// activate the combo box
	// (ak)
	if (m_bTextMode)
	{
		SetWindowText(m_sText);
	}
	else
	{
		LONG idx = FindCBData(m_lComboData);

		if (idx!=CB_ERR)
			SetCurSel(idx);
	}

	SetWindowPos(NULL, m_rc.left-5, m_rc.top, m_rc.Width()+5, m_rc.Height() + m_nDropHeight, SWP_NOZORDER|SWP_SHOWWINDOW);

	SetFocus();

	// (ak) CBEM_SETEDITSEL is ignored while the control does not have the focus,
	// so select the text only after SetFocus() above.
	if (m_bTextMode)
		SetEditSel(0, -1);

	// ak
	//if (GetCount())
	//	ShowDropDown(TRUE);
}


// ak
void CPropTreeItemCombo::OnCancel()
{
	// hide edit control
	if (m_hWnd) ShowWindow(SW_HIDE);
}

BOOL CPropTreeItemCombo::CreateComboBox(DWORD dwStyle)
{
	ASSERT(m_pProp!=NULL);

	if (IsWindow(m_hWnd))
		DestroyWindow();

	// force as not visible child window
	dwStyle = (WS_CHILD|WS_VSCROLL|dwStyle) & ~WS_VISIBLE;

	if (!Create(dwStyle, CRect(0,0,0,0), m_pProp->GetCtrlParent(), GetCtrlID()))
	{
		TRACE0("CPropTreeItemCombo::CreateComboBox() - failed to create combo box\n");
		return FALSE;
	}

	SendMessage(WM_SETFONT, (WPARAM)m_pProp->GetNormalFont()->m_hObject);

	return TRUE;
}


BOOL CPropTreeItemCombo::CreateComboBoxBool()
{
	ASSERT(m_pProp!=NULL);

	if (IsWindow(m_hWnd))
		DestroyWindow();

	// force as a non-visible child window
	DWORD dwStyle = WS_CHILD|WS_VSCROLL|CBS_SORT|CBS_DROPDOWNLIST;

	if (!Create(dwStyle, CRect(0,0,0,0), m_pProp->GetCtrlParent(), GetCtrlID()))
	{
		TRACE0("CPropTreeItemCombo::CreateComboBoxBool() - failed to create combo box\n");
		return FALSE;
	}

	SendMessage(WM_SETFONT, (WPARAM)m_pProp->GetNormalFont()->m_hObject);

	// file the combo box
	LONG idx;
	CString s;

	//s.LoadString(IDS_TRUE);
    s = "True";
	idx = AddString(s);
	SetItemData(idx, TRUE);

	//s.LoadString(IDS_FALSE);
    s = "False";
	idx = AddString(s);
	SetItemData(idx, FALSE);

	return TRUE;
}


LONG CPropTreeItemCombo::FindCBData(LPARAM lParam)
{
	LONG idx;
	
	for (idx = 0; idx < GetCount(); idx++)
	{
		if (GetItemData(idx)==(DWORD)lParam)
			return idx;
	}

	return CB_ERR;
}


void CPropTreeItemCombo::AppendSearchValue(CString& sText)
{
	CString sValue;

	if (m_bTextMode)
	{
		// Free-text combo: the value IS the text the user typed.
		sValue = m_sText;
	}
	else
	{
		// Drop-down-list combo: the value is an entry's item data, so resolve it
		// back to the entry the user actually sees. FindCBData() is the control's
		// own lookup - SetItemValue() and OnRefresh() go through it too.
		LONG idx = FindCBData(m_lComboData);

		if (idx != CB_ERR)
			GetLBText((int)idx, sValue);
	}

	if (!sValue.IsEmpty())
	{
		sText += sValue;
		sText += _T(' ');
	}
}


void CPropTreeItemCombo::AppendSearchChoices(CString& sText)
{
	// The drop down list is a real child combo that CreateComboBox() creates at
	// build time and the host fills before the row is ever shown, so its entries
	// stay readable while the cell is inactive. A search must not activate cells
	// to read them: activating would hide whichever cell is active now, move the
	// focus and raise PTN_ITEMCHANGED - a page would look modified by being
	// searched.
	for (int i = 0; i < GetCount(); i++)
	{
		CString sEntry;
		GetLBText(i, sEntry);

		if (!sEntry.IsEmpty())
		{
			sText += sEntry;
			sText += _T(' ');
		}
	}
}



void CPropTreeItemCombo::OnKillfocus() 
{
	CommitChanges();
}


void CPropTreeItemCombo::SetDropDownHeight(LONG nDropHeight)
{
	m_nDropHeight = nDropHeight;
}


LONG CPropTreeItemCombo::GetDropDownHeight()
{
	return m_nDropHeight;
}


void CPropTreeItemCombo::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	if (nChar==VK_RETURN)
		CommitChanges();
	
    CComboBox::OnChar(nChar, nRepCnt, nFlags);
}
