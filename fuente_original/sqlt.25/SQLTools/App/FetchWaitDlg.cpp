// FetchWaitDlg.cpp : implementation file
//

#include "stdafx.h"
#include "App/SQLTools.h"
#include "App/FetchWaitDlg.h"


// FetchWaitDlg dialog
FetchWaitDlg::FetchWaitDlg (CWnd* pParent, volatile bool& allRowsFetched)
	: CDialog(FetchWaitDlg::IDD, pParent)
    , m_allRowsFetched(allRowsFetched)
    , m_text(_T(""))
    , m_cancelled(false)
{

}

FetchWaitDlg::~FetchWaitDlg()
{
}

void FetchWaitDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialog::DoDataExchange(pDX);
    DDX_Text(pDX, IDC_WAIT_FOR_TEXT, m_text);
}


BEGIN_MESSAGE_MAP(FetchWaitDlg, CDialog)
    ON_WM_TIMER()
END_MESSAGE_MAP()


// FetchWaitDlg message handlers


BOOL FetchWaitDlg::OnInitDialog()
{
    CDialog::OnInitDialog();

    SetTimer(1, 250, NULL);

    return TRUE;
}


void FetchWaitDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 1)
    {
        if (m_cancelled)
        {
            // A tick queued before OnCancel() must not EndDialog(IDOK) over the cancel's result.
            KillTimer(nIDEvent);
        }
        else if (m_allRowsFetched)
        {
            KillTimer(nIDEvent);
            EndDialog(IDOK);
        }
    }

    CDialog::OnTimer(nIDEvent);
}

void FetchWaitDlg::OnCancel()
{
    m_cancelled = true;

    KillTimer(1);

    CDialog::OnCancel();
}
