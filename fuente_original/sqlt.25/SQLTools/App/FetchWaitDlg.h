#pragma once


// FetchWaitDlg dialog

class FetchWaitDlg : public CDialog
{
public:
	FetchWaitDlg (CWnd* pParent, volatile bool& allRowsFetched);
	virtual ~FetchWaitDlg();

// Dialog Data
	enum { IDD = IDD_WAIT };

    // The watched flag goes true on a cancelled fetch too, so DoModal()'s IDOK cannot report one.
    bool WasCancelled () const { return m_cancelled; }

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
    virtual void OnCancel();

	DECLARE_MESSAGE_MAP()
public:
    CString m_text;
    volatile bool& m_allRowsFetched;
    virtual BOOL OnInitDialog();
    afx_msg void OnTimer(UINT_PTR nIDEvent);

private:
    bool m_cancelled;
};
