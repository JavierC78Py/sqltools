/* 
    Copyright (C) 2002 Aleksey Kochetov

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA 
*/

// 04/02/2002 it has been reimplemented with Shell API.

#include "stdafx.h"
#include <Shlobj.h>
#include <Common/DirSelectDlg.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace Common 
{

#if !(_MFC_VER >= 0x0600)

DWORD CDirSelectDlg::c_dwFlags = OFN_SHOWHELP | OFN_HIDEREADONLY | 
                                 OFN_OVERWRITEPROMPT | OFN_ENABLETEMPLATE;
const char* CDirSelectDlg::c_pcszFilter = "(none)|NONE.$$$||";


/////////////////////////////////////////////////////////////////////////////
// CDirSelectDlg

CDirSelectDlg::CDirSelectDlg(LPCTSTR lpcszCaption, CWnd* pWndParent, LPCTSTR lpcszInitialDir) :
    CFileDialog(FALSE, NULL, NULL, c_dwFlags, c_pcszFilter, pWndParent)
{
    m_ofn.lpstrTitle      = lpcszCaption;
    m_ofn.lpstrInitialDir = lpcszInitialDir;
    m_ofn.hInstance = AfxFindResourceHandle(MAKEINTRESOURCE(IDD_DFP_DIRSELECT), RT_DIALOG);
    m_ofn.lpTemplateName  = MAKEINTRESOURCE(IDD_DFP_DIRSELECT);
    m_ofn.Flags &= ~OFN_EXPLORER;
}

void CDirSelectDlg::GetPath(CString& strPath) const
{
    CString strTemp(m_ofn.lpstrFile);
    strPath = strTemp.Left(m_ofn.nFileOffset);
}

/*
BEGIN_MESSAGE_MAP(CDirSelectDlg, CFileDialog)
    //{{AFX_MSG_MAP(CDirSelectDlg)
    //}}AFX_MSG_MAP
END_MESSAGE_MAP()
*/

BOOL CDirSelectDlg::OnInitDialog()
{
    CFileDialog::OnInitDialog();

    GetDlgItem(IDC_DFP_STATIC_FILE_NAME)->ShowWindow(SW_HIDE);
    GetDlgItem(IDC_DFP_FILE_NAME)->ShowWindow(SW_HIDE);
    GetDlgItem(IDC_DFP_FILE_LIST)->ShowWindow(SW_HIDE);
    GetDlgItem(IDC_DFP_STATIC_TYPE)->ShowWindow(SW_HIDE);
    GetDlgItem(IDC_DFP_TYPE_LIST)->ShowWindow(SW_HIDE);

    // If the file name field is empty, or has a wildcard in it when the 
    // user clicks OK, the dialog will not close.  Thus, we need to put
    // some dummy text in there.
    
    SetDlgItemText(IDC_DFP_FILE_NAME, "none");
    GetDlgItem(IDC_DFP_DIR_LIST)->SetFocus();
    return FALSE;
}

#else

    CDirSelectDlg::CDirSelectDlg (LPCTSTR lpcszCaption, CWnd* pWndParent, LPCTSTR lpcszInitialDir)
        : m_folder(lpcszInitialDir), m_caption(lpcszCaption), m_pWndParent(pWndParent)
    {
    }

    int CDirSelectDlg::DoModal ()
    {
        // 2026-09-25 SHBrowseForFolder replaced by IFileOpenDialog + FOS_PICKFOLDERS, the folder
        // picker Microsoft recommends in its place. SHBrowseForFolder is a legacy shell API: its
        // dialog is frozen at the pre-Vista shape, it cannot be extended, and its activation is
        // fragile enough that a picker opened from a control nested inside another window could
        // come up deaf to the keyboard - it sat there clickable while Tab, Enter and Escape all
        // beeped, because the focus never left the window SHBrowseForFolder had just disabled.
        // IFileDialog runs the same modal loop as every other common dialog in the application.
        //
        // The public API, and therefore every caller, is unchanged: caption, parent and initial
        // folder in, IDOK/IDCANCEL out, the picked path back through GetPath().
        int retVal = IDCANCEL;

        // COM is already up on this application's UI thread - InitInstance() calls AfxOleInit() -
        // but the class sits in Common and cannot assume which thread reaches it. CoInitializeEx()
        // answers S_FALSE on a thread already initialised in this model, which still wants the
        // matching CoUninitialize(), and RPC_E_CHANGED_MODE on one up in the other model, which
        // does not and must not be torn down from here.
        const HRESULT hrCom   = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        const bool    bUninit = (SUCCEEDED(hrCom) != FALSE);

        IFileOpenDialog* pDialog = NULL;

        if (SUCCEEDED(::CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                         IID_IFileOpenDialog, (void**)&pDialog)) && pDialog != NULL)
        {
            // FOS_PICKFOLDERS is what turns the open dialog into a folder picker.
            // FOS_FORCEFILESYSTEM is the old BIF_RETURNONLYFSDIRS: a library, a search folder or
            // any other virtual item with no path of its own cannot be handed back, so GetPath()
            // always returns something the settings can store.
            DWORD dwOptions = 0;

            if (SUCCEEDED(pDialog->GetOptions(&dwOptions)))
                pDialog->SetOptions(dwOptions|FOS_PICKFOLDERS|FOS_FORCEFILESYSTEM);

            if (!m_caption.IsEmpty())
                pDialog->SetTitle(m_caption);

            // The replacement for the BFFM_SETSELECTION this class used to send from its
            // BrowseCallbackProc, and sent only when there is a folder to send: with no
            // SetFolder() the dialog restores the location this application last picked. That is
            // what the static g_initialFolder was reproducing by hand, and it is why an unset
            // setting ("Use default location", stored as the empty string) does not drop the user
            // on the desktop. An initial folder that no longer exists fails to parse and is
            // dropped, leaving that same last-used location.
            if (!m_folder.IsEmpty())
            {
                IShellItem* pInitialFolder = NULL;

                if (SUCCEEDED(::SHCreateItemFromParsingName(m_folder, NULL, IID_IShellItem,
                                                            (void**)&pInitialFolder))
                    && pInitialFolder != NULL)
                {
                    pDialog->SetFolder(pInitialFolder);
                    pInitialFolder->Release();
                }
            }

            // IModalWindow::Show() wants a top level owner. Callers hand over whatever CWnd they
            // happen to be - a property page, or in the settings tree the CPropTree control, whose
            // own child holds the keyboard focus at this moment. Normalising once here rather
            // than at each of the four call sites is what keeps the dialog properly modal to,
            // centred on and activated over the window the user is actually looking at.
            HWND hOwner = m_pWndParent ? m_pWndParent->GetSafeHwnd() : NULL;

            if (hOwner != NULL)
                hOwner = ::GetAncestor(hOwner, GA_ROOT);

            if (SUCCEEDED(pDialog->Show(hOwner)))
            {
                IShellItem* pResult = NULL;

                if (SUCCEEDED(pDialog->GetResult(&pResult)) && pResult != NULL)
                {
                    // SIGDN_FILESYSPATH rather than SIGDN_DESKTOPABSOLUTEPARSING: the settings
                    // want a plain path, which is the form SHGetPathFromIDList() used to fill in.
                    LPWSTR pszPath = NULL;

                    if (SUCCEEDED(pResult->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)) && pszPath != NULL)
                    {
                        if (pszPath[0] != L'\0')
                        {
                            m_folder = pszPath;
                            retVal   = IDOK;
                        }

                        ::CoTaskMemFree(pszPath);
                    }

                    pResult->Release();
                }
            }

            pDialog->Release();
        }
        else
        {
            if (pDialog != NULL)
                pDialog->Release();

            MessageBeep((UINT)-1);
            AfxMessageBox(L"Cannot open \"Browse For Folder\" dialog.", MB_OK|MB_ICONSTOP);
        }

        if (bUninit)
            ::CoUninitialize();

        return retVal;
    }

#endif

};//namespace Common