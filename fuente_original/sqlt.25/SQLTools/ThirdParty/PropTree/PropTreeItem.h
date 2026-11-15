// PropTreeItem.h
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

#ifndef _PROPTREEITEM_H
#define _PROPTREEITEM_H

class CPropTree;

class CPropTreeItem
{
public:
	CPropTreeItem();
	virtual ~CPropTreeItem();

public:
	BOOL IsExpanded();
	BOOL IsSelected();
	BOOL IsChecked();
	BOOL IsReadOnly();
	BOOL IsActivated();

	void Select(BOOL bSelect = TRUE);
	void Expand(BOOL bExpand = TRUE);
	void Check(BOOL bCheck = TRUE);
	void ReadOnly(BOOL bReadOnly = TRUE);

	BOOL IsCheckBox();

	void HasCheckBox(BOOL bCheckbox = TRUE);

	BOOL IsRadioButton();

	void HasRadioButton(BOOL bRadio = TRUE);

	void SelectRadio();

	BOOL HitExpand(const POINT& pt);

	BOOL HitCheckBox(const POINT& pt);

	BOOL IsRootLevel();

	LONG GetTotalHeight();

	void SetLabelText(LPCTSTR sLabel);

	LPCTSTR GetLabelText();

	void SetInfoText(LPCTSTR sInfo);

	LPCTSTR GetInfoText();

	void SetIndent(int nIndent);

	int GetIndent() const;

	static int GetLevelIndent();

	void SetCtrlID(UINT nCtrlID);

	UINT GetCtrlID();

	LONG DrawItem(CDC* pDC, const RECT& rc, LONG x, LONG y);

	void CommitChanges();

	void Activate();

    void Cancel ();

	virtual void DrawAttribute(CDC* pDC, const RECT& rc);

	virtual LONG GetHeight();

	virtual void OnMove();

	virtual void OnRefresh();

	virtual void OnCommit();

	virtual void OnActivate();

    virtual void OnCancel() {};

	virtual void AppendSearchValue(CString&) {}

	virtual void AppendSearchChoices(CString&) {}


	void SetPropOwner(CPropTree* pProp);

	const POINT& GetLocation();

	CPropTreeItem* GetParent();
	CPropTreeItem* GetSibling();
	CPropTreeItem* GetChild();
	CPropTreeItem* GetNextVisible();

	void SetParent(CPropTreeItem* pParent);
	void SetSibling(CPropTreeItem* pSibling);
	void SetChild(CPropTreeItem* pChild);
	void SetNextVisible(CPropTreeItem* pVis);

protected:
	CPropTree*			m_pProp;

	CString				m_sLabel;

	CString				m_sInfo;

	CPoint				m_loc;

	CRect				m_rc;

	LPARAM				m_lParam;

	UINT				m_nCtrlID;

private:
	enum TreeItemStates
	{
		TreeItemSelected =		0x00000001,
		TreeItemExpanded =		0x00000002,
		TreeItemCheckbox =		0x00000004,
		TreeItemChecked =		0x00000008,
		TreeItemActivated =		0x00000010,
		TreeItemReadOnly =		0x00000020,

		TreeItemRadioButton =	0x00000040,
	};

	DWORD				m_dwState;

	BOOL				m_bCommitOnce;

	int					m_nIndent;

	CRect				m_rcExpand;

	CRect				m_rcCheckbox;

	CPropTreeItem*		m_pParent;
	CPropTreeItem*		m_pSibling;
	CPropTreeItem*		m_pChild;
	CPropTreeItem*		m_pVis;
};

#endif // _PROPTREEITEM_H
