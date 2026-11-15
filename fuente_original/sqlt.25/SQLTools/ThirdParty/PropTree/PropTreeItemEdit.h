#if !defined(AFX_PROPTREEITEMEDIT_H__642536B1_1162_4F99_B09D_9B1BD2CF88B6__INCLUDED_)
#define AFX_PROPTREEITEMEDIT_H__642536B1_1162_4F99_B09D_9B1BD2CF88B6__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
// PropTreeItemEdit.h : header file
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

#include "PropTreeItem.h"
#include <afxcmn.h>

/////////////////////////////////////////////////////////////////////////////
// CPropTreeSpinButton - buddy spin control that never keeps input focus;
// clicking it must not steal/keep focus away from the buddy edit control,
// otherwise the PropTree item commits (and hides itself) on kill focus.

class CPropTreeItemEdit;

class CPropTreeSpinButton : public CSpinButtonCtrl
{
public:
	CPropTreeItemEdit* m_pOwner;
	int m_nStep;

	CPropTreeSpinButton() : m_pOwner(NULL), m_nStep(1) {}
protected:
	afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnDeltaPos(NMHDR* pNMHDR, LRESULT* pResult);
	DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////
// CPropTreeItemEdit window

class CPropTreeItemEdit : public CEdit, public CPropTreeItem
{
	friend class CPropTreeSpinButton;
// Construction
public:
	CPropTreeItemEdit();
	virtual ~CPropTreeItemEdit();

// Attributes
public:
	// The attribute area needs drawing
	virtual void DrawAttribute(CDC* pDC, const RECT& rc);

	// Retrieve the item's attribute value
	LPCTSTR GetItemValue () const;
	void GetItemValue (int&) const;
	void GetItemValue (double&) const;

	// Set the item's attribute value
	void SetItemValue (LPCTSTR);
	void SetItemValue (int);
	void SetItemValue (double);

	// (ak) CPropTreeItem: contributes the value text to a settings search, and
	// NOTHING for a password row - see PropTreeItem.h for the contract.
	virtual void AppendSearchValue (CString& sText);

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

	
	enum ValueFormat
	{
		ValueFormatText,
		ValueFormatInteger,
		ValueFormatFloat
	};

	// Set to specifify format of SetItemValue/GetItemValue
	void SetValueFormat(ValueFormat nFormat);
		
	// Set to TRUE for to use a password edit control
	void SetAsPassword(BOOL bPassword);

	// Enable a buddy spin (up-down) control with the given range for this item.
	void SetSpinRange(int nLower, int nUpper);

	// Set the increment/decrement step for the spin control (default 1).
	void SetSpinStep(int nStep);

	// (ak) Limit the integer value of this item. Typed text is clamped to the
	// range when the item commits (this replaces the DDV_MinMax* validation of
	// the classic dialogs), and the edit control is created with ES_NUMBER when
	// the range starts at zero. Pair with SetSpinRange() for spin backed items.
	void SetItemLimit(int nLower, int nUpper);

	// (ak) Limit the number of characters the user can type - the tree cell
	// equivalent of the classic dialogs' DDV_MaxChars / SetLimitText. Applied
	// when the in-place edit is created; 0 (the default) means no limit.
	void SetTextLimit(int nChars);

protected:
	CString		m_sEdit;
	ValueFormat m_nFormat;
	BOOL		m_bPassword;

	BOOL		m_bSpin;
	int			m_nSpinLower;
	int			m_nSpinUpper;
	int			m_nSpinStep;
	CPropTreeSpinButton m_spin;
	BOOL		m_bSpinClicking;

	BOOL		m_bLimit;
	int			m_nLimitLower;
	int			m_nLimitUpper;

	int			m_nTextLimit;	// (ak) 0 = no limit, see SetTextLimit()

	// (ak) Push the current item value into the spin buddy: UDS_SETBUDDYINT makes
	// the spin overwrite the buddy text on every arrow click, so the spin position
	// must never diverge from the item value.
	void SyncSpinPos();

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CPropTreeItemEdit)
	//}}AFX_VIRTUAL

// Implementation
public:

	// Generated message map functions
protected:
	//{{AFX_MSG(CPropTreeItemEdit)
	afx_msg UINT OnGetDlgCode();
	afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
	afx_msg void OnKillfocus();
	//}}AFX_MSG

	DECLARE_MESSAGE_MAP()
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_PROPTREEITEMEDIT_H__642536B1_1162_4F99_B09D_9B1BD2CF88B6__INCLUDED_)
