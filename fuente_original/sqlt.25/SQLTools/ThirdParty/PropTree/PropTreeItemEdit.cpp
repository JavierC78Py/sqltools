// PropTreeItemEdit.cpp : implementation file
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
#include "proptree.h"
#include "PropTreeItemEdit.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CPropTreeSpinButton
//
// The spin buddy control must not take/keep keyboard focus - doing so
// generates EN_KILLFOCUS on the buddy edit control which commits (and
// hides) the PropTree item, so a second click on the spin arrows would
// hit a hidden/destroyed window. Keep focus on the buddy edit control
// for every mouse click.

BEGIN_MESSAGE_MAP(CPropTreeSpinButton, CSpinButtonCtrl)
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_NOTIFY_REFLECT(UDN_DELTAPOS, OnDeltaPos)
END_MESSAGE_MAP()

void CPropTreeSpinButton::OnLButtonDown(UINT nFlags, CPoint point)
{
	if (m_pOwner)
		m_pOwner->m_bSpinClicking = TRUE;
	CWnd* pBuddy = GetBuddy();
	CSpinButtonCtrl::OnLButtonDown(nFlags, point);
	if (pBuddy && ::IsWindow(pBuddy->m_hWnd))
		pBuddy->SetFocus();
	if (m_pOwner)
		m_pOwner->m_bSpinClicking = FALSE;
}

void CPropTreeSpinButton::OnLButtonUp(UINT nFlags, CPoint point)
{
	if (m_pOwner)
		m_pOwner->m_bSpinClicking = TRUE;
	CWnd* pBuddy = GetBuddy();
	CSpinButtonCtrl::OnLButtonUp(nFlags, point);
	if (pBuddy && ::IsWindow(pBuddy->m_hWnd))
		pBuddy->SetFocus();
	if (m_pOwner)
		m_pOwner->m_bSpinClicking = FALSE;
}

void CPropTreeSpinButton::OnDeltaPos(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMUPDOWN* pNMUpDown = (NMUPDOWN*)pNMHDR;

	if (m_nStep > 1)
	{
		int nCur = pNMUpDown->iPos;
		int nDir = (pNMUpDown->iDelta > 0) ? 1 : -1;
		int nLower, nUpper;
		GetRange32(nLower, nUpper);

		int nNew = nCur + nDir * m_nStep;
		if (nNew > nUpper) nNew = nUpper;
		if (nNew < nLower) nNew = nLower;

		SetPos32(nNew);
		*pResult = TRUE; // suppress default delta application
		return;
	}

	*pResult = FALSE;
}

/////////////////////////////////////////////////////////////////////////////
// CPropTreeItemEdit

CPropTreeItemEdit::CPropTreeItemEdit() :
	m_sEdit(_T("")),
	m_nFormat(ValueFormatText),
	m_bPassword(FALSE),
	m_bSpin(FALSE),
	m_nSpinLower(0),
	m_nSpinUpper(100),
	m_nSpinStep(1),
	m_bSpinClicking(FALSE),
	m_bLimit(FALSE),
	m_nLimitLower(0),
	m_nLimitUpper(0),
	m_nTextLimit(0)
{
}

CPropTreeItemEdit::~CPropTreeItemEdit()
{
	if (IsWindow(m_spin.m_hWnd))
		m_spin.DestroyWindow();
}


BEGIN_MESSAGE_MAP(CPropTreeItemEdit, CEdit)
	//{{AFX_MSG_MAP(CPropTreeItemEdit)
	ON_WM_GETDLGCODE()
	ON_WM_KEYDOWN()
	ON_WM_CHAR()
	ON_CONTROL_REFLECT(EN_KILLFOCUS, OnKillfocus)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CPropTreeItemEdit message handlers

void CPropTreeItemEdit::DrawAttribute(CDC* pDC, const RECT& rc)
{
	ASSERT(m_pProp!=NULL);


    // (ak)
	//pDC->SelectObject(IsReadOnly() ? m_pProp->GetNormalFont() : m_pProp->GetBoldFont());
	//pDC->SetTextColor(RGB(0,0,0));
    pDC->SelectObject(m_pProp->GetNormalFont());
    pDC->SetTextColor(GetSysColor(!IsReadOnly() ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT));
    // (ak)
	pDC->SetBkMode(TRANSPARENT);

	CRect r = rc;
    r.InflateRect(-2,-2); // (ak)

	TCHAR ch;

	// can't use GetPasswordChar(), because window may not be created yet
	ch = (m_bPassword) ? '*' : '\0';

	if (ch)
	{
		CString s;

		s = m_sEdit;
		for (LONG i=0; i<s.GetLength();i++)
			s.SetAt(i, ch);

		pDC->DrawText(s, r, DT_SINGLELINE|DT_TOP|DT_NOPREFIX);
	}
	else
	{
		pDC->DrawText(m_sEdit, r, DT_SINGLELINE|DT_TOP|DT_NOPREFIX);
	}
}



void CPropTreeItemEdit::SetAsPassword(BOOL bPassword)
{
	m_bPassword = bPassword;
}


void CPropTreeItemEdit::SetSpinRange(int nLower, int nUpper)
{
	m_bSpin = TRUE;
	m_nSpinLower = nLower;
	m_nSpinUpper = nUpper;

	if (IsWindow(m_spin.m_hWnd))
		m_spin.SetRange32(nLower, nUpper);
}


void CPropTreeItemEdit::SetSpinStep(int nStep)
{
	m_nSpinStep = nStep;

	if (IsWindow(m_spin.m_hWnd))
		m_spin.m_nStep = nStep;
}


// (ak) see SetItemLimit() in the header: typed text is clamped on commit.
void CPropTreeItemEdit::SetItemLimit(int nLower, int nUpper)
{
	m_bLimit = TRUE;
	m_nLimitLower = nLower;
	m_nLimitUpper = nUpper;
}


// (ak) see SetTextLimit() in the header: the limit belongs to the in-place edit
// control, which is created lazily in OnActivate(), so it is stored here and
// applied both to an already created control and to the next created one.
void CPropTreeItemEdit::SetTextLimit(int nChars)
{
	m_nTextLimit = nChars;

	if (IsWindow(m_hWnd) && m_nTextLimit > 0)
		SetLimitText(m_nTextLimit);
}


// (ak) UDS_SETBUDDYINT makes the spin write its position back into the buddy
// edit on every arrow click, so a stale spin position silently overwrites a
// value the user typed. Keep the two in sync whenever the item shows its value.
void CPropTreeItemEdit::SyncSpinPos()
{
	if (!m_bSpin || !IsWindow(m_spin.m_hWnd))
		return;

	int nPos = _ttoi(m_sEdit);

	if (m_bLimit)
	{
		if (nPos < m_nLimitLower) nPos = m_nLimitLower;
		if (nPos > m_nLimitUpper) nPos = m_nLimitUpper;
	}

	m_spin.SetPos32(nPos);
}


void CPropTreeItemEdit::SetValueFormat(ValueFormat nFormat)
{
	m_nFormat = nFormat;
}


// Retrieve the item's attribute value
LPCTSTR CPropTreeItemEdit::GetItemValue () const
{
	return m_sEdit;
}

void CPropTreeItemEdit::GetItemValue (int& val) const
{
	ASSERT(m_nFormat == ValueFormatInteger);
    val = _ttoi(m_sEdit);

	// (ak) clamp to the configured limits - the caller has no DDV_MinMax* to
	// fall back on once the value is edited in a tree cell.
	if (m_bLimit)
	{
		if (val < m_nLimitLower) val = m_nLimitLower;
		if (val > m_nLimitUpper) val = m_nLimitUpper;
	}
}

void CPropTreeItemEdit::GetItemValue (double& val) const
{
	ASSERT(m_nFormat == ValueFormatFloat);
    val = _tstof(m_sEdit);
}

// Set the item's attribute value
void CPropTreeItemEdit::SetItemValue (LPCTSTR str)
{
	m_nFormat = ValueFormatText;
    m_sEdit = str;
}

void CPropTreeItemEdit::SetItemValue (int val)
{
	m_nFormat = ValueFormatInteger;
    m_sEdit.Format(_T("%d"), val);
}

void CPropTreeItemEdit::SetItemValue (double val)
{
	m_nFormat = ValueFormatFloat;
    m_sEdit.Format(_T("%f"), val);
}

void CPropTreeItemEdit::AppendSearchValue(CString& sText)
{
	// A password row contributes NOTHING. Its value must not land in a haystack
	// where a typed term could confirm it one character at a time.
	if (m_bPassword)
		return;

	// While the cell is active the edit control holds keystrokes that OnCommit()
	// has not folded into m_sEdit yet, so read the control then - a search should
	// find what is on screen. For an inactive cell m_sEdit is the committed truth;
	// the control is either absent or stale from the last activation.
	CString sValue;

	if (IsActivated() && GetSafeHwnd() != NULL)
		GetWindowText(sValue);
	else
		sValue = m_sEdit;

	if (!sValue.IsEmpty())
	{
		sText += sValue;
		sText += _T(' ');
	}
}



void CPropTreeItemEdit::OnMove()
{
	if (IsWindow(m_hWnd))
	{
		LONG nSpinWidth = (m_bSpin && IsWindow(m_spin.m_hWnd)) ? 16 : 0;
		// (ak)
		SetWindowPos(NULL, m_rc.left+2, m_rc.top+2, m_rc.Width()-4-nSpinWidth, m_rc.Height()-4, SWP_NOZORDER|SWP_NOACTIVATE);
	}

	if (m_bSpin && IsWindow(m_spin.m_hWnd))
		m_spin.SetWindowPos(NULL, m_rc.right-16, m_rc.top+2, 16, m_rc.Height()-4, SWP_NOZORDER|SWP_NOACTIVATE);
}


void CPropTreeItemEdit::OnRefresh()
{
	if (IsWindow(m_hWnd))
		SetWindowText(m_sEdit);

	// (ak) keep the spin buddy on the value that is now displayed
	if (!IsActivated())
		SyncSpinPos();
}


void CPropTreeItemEdit::OnCommit()
{
	// hide edit control
	ShowWindow(SW_HIDE);
	if (IsWindow(m_spin.m_hWnd))
		m_spin.ShowWindow(SW_HIDE);

	// store edit text for GetItemValue
	GetWindowText(m_sEdit);

	// (ak) normalize out of range text so the attribute area displays the value
	// that is really stored (typed text has no DDV_MinMax* to reject it).
	if (m_bLimit && m_nFormat == ValueFormatInteger)
	{
		int nVal = _ttoi(m_sEdit);

		if (nVal < m_nLimitLower || nVal > m_nLimitUpper)
		{
			if (nVal < m_nLimitLower) nVal = m_nLimitLower;
			if (nVal > m_nLimitUpper) nVal = m_nLimitUpper;

			m_sEdit.Format(_T("%d"), nVal);
		}
	}
}


void CPropTreeItemEdit::OnActivate()
{
	// Check if the edit control needs creation
	if (!IsWindow(m_hWnd))
	{
		DWORD dwStyle;

		dwStyle = WS_CHILD|ES_AUTOHSCROLL;

		// (ak) limited non-negative integer items take digits only, like the
		// ES_NUMBER edits of the classic dialogs this control replaces.
		if (m_bLimit && m_nFormat == ValueFormatInteger && m_nLimitLower >= 0)
			dwStyle |= ES_NUMBER;

		Create(dwStyle, m_rc, m_pProp->GetCtrlParent(), GetCtrlID());
		SendMessage(WM_SETFONT, (WPARAM)m_pProp->GetNormalFont()->m_hObject);

		// (ak) the character limit of a text item - what DDV_MaxChars was for the
		// classic dialogs this cell replaces.
		if (m_nTextLimit > 0)
			SetLimitText(m_nTextLimit);
	}

	if (m_bSpin && !IsWindow(m_spin.m_hWnd))
	{
		m_spin.m_pOwner = this;
		m_spin.m_nStep = m_nSpinStep;
		m_spin.Create(WS_CHILD|WS_VISIBLE|UDS_ARROWKEYS|UDS_NOTHOUSANDS|UDS_SETBUDDYINT,
			CRect(m_rc.right-16, m_rc.top+2, m_rc.right, m_rc.bottom-2),
			m_pProp->GetCtrlParent(), GetCtrlID()+10000);
		m_spin.SetBuddy(this);
		m_spin.SetRange32(m_nSpinLower, m_nSpinUpper);
	}

	SetMargins(0, 0); // (ak)
	SetPasswordChar((TCHAR)(m_bPassword ? '*' : 0));
	SetWindowText(m_sEdit);
	SetSel(0, -1);

	// (ak)
	LONG nSpinWidth = (m_bSpin && IsWindow(m_spin.m_hWnd)) ? 16 : 0;
	SetWindowPos(NULL, m_rc.left+2, m_rc.top+2, m_rc.Width()-4-nSpinWidth, m_rc.Height()-4, SWP_NOZORDER|SWP_SHOWWINDOW);
	if (m_bSpin && IsWindow(m_spin.m_hWnd))
		m_spin.SetWindowPos(NULL, m_rc.right-16, m_rc.top+2, 16, m_rc.Height()-4, SWP_NOZORDER|SWP_SHOWWINDOW);

	// (ak) the spin is only positioned when it is created, so re-sync it here on
	// every activation: otherwise the first arrow click after a typed (committed)
	// value jumps back to the stale position and overwrites what was typed.
	SyncSpinPos();

	SetFocus();
}

// ak
void CPropTreeItemEdit::OnCancel()
{
	// hide edit control
	if (m_hWnd) ShowWindow(SW_HIDE);
	if (IsWindow(m_spin.m_hWnd)) m_spin.ShowWindow(SW_HIDE);
}

UINT CPropTreeItemEdit::OnGetDlgCode() 
{
	// Release Tab so IsDialogMessage can move focus out (KillFocus will commit).
	const MSG* pMsg = GetCurrentMessage();
	if (pMsg && pMsg->message == WM_GETDLGCODE && pMsg->wParam == VK_TAB)
		return CEdit::OnGetDlgCode();
	return CEdit::OnGetDlgCode()|DLGC_WANTALLKEYS;
}


void CPropTreeItemEdit::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) 
{
	if (nChar == VK_RETURN)
	{
		CommitChanges();
		m_pProp->GetCtrlParent()->SetFocus();
		return;  // do NOT fall through to CEdit — single-line edit beeps on Enter
	}
	if (nChar == VK_ESCAPE)
	{
		Cancel();
		m_pProp->GetCtrlParent()->SetFocus();
		return;  // do NOT fall through to CEdit — it would beep
	}
	CEdit::OnKeyDown(nChar, nRepCnt, nFlags);
}


void CPropTreeItemEdit::OnKillfocus() 
{
	// Don't commit (and thus hide the controls) if focus is temporarily
	// moving away because the user clicked our own spin buddy control.
	if (m_bSpin && m_bSpinClicking)
		return;

	// Only commit if the item is still in activated state.
	// After Cancel() the activated flag is already cleared, so we
	// must not overwrite the stored value with the discarded edit text.
	if (IsActivated())
		CommitChanges();
}

void CPropTreeItemEdit::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	// WM_CHAR follows WM_KEYDOWN for Enter (0x0D) and Escape (0x1B).
	// A single-line CEdit beeps on these — swallow them here since
	// OnKeyDown already handled the action.
	if (nChar == VK_RETURN || nChar == VK_ESCAPE)
		return;
	CEdit::OnChar(nChar, nRepCnt, nFlags);
}
