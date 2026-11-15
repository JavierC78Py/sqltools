/* 
	SQLTools is a tool for Oracle database developers and DBAs.
    Copyright (C) 1997-2004 Aleksey Kochetov

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

#include "stdafx.h"
#include "App/SQLTools.h"
#include "App/MainFrm.h"
#include "COMMON/AppGlobal.h"
#include "COMMON/AppUtilities.h"
#include "COMMON/ExceptionHelper.h"
#include "COMMON/PropertySheetMem.h"
#include "Dialogs/PropServerScriptPageTree.h"
#include "Dialogs/GridPageTree.h"
#include "Dialogs/DDLPageTree.h"
#include "Dialogs/ObjectViewerPageTree.h"
#include "OpenEditor/OEDocument.h"
#include "Settings/SQLToolsSettingsXmlStreamer.h"
#include "OpenEditor/OEDocument.h"
#include "App/SessionCache.h"
#include "Threading/TaskQueue.h"
#include "Threading/Thread.h"
#include <App/ActivePrimeExecutionNote.h>
#include "Dialogs/PropAutocompletePageTree.h"
#include "OpenEditor/VisualAttributesPageTree.h"

/*
    31.03.2003 bug fix, redundant sqltools settings saving
*/
    
#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

    static bool g_isLoading = false;

    struct OnSettingsChangedTask : Threading::Task
    {
        int  m_keepAlivePeriod;
        bool m_outputEnabled;
        int  m_outputSize;
        string m_dateFormat;

        OnSettingsChangedTask () 
            : Threading::Task("OnSettingsChanged", 0)
        {
            m_keepAlivePeriod = GetSQLToolsSettings().GetKeepAlivePeriod();
            m_outputEnabled   = GetSQLToolsSettings().GetOutputEnable();
            m_outputSize      = GetSQLToolsSettings().GetOutputSize();
            m_dateFormat      = GetSQLToolsSettings().GetDateFormat();
        }

        virtual void DoInBackground (OciConnect& connect)
        {
            ActivePrimeExecutionOnOff onOff;
            Threading::g_keepAlivePeriod = m_keepAlivePeriod;
            connect.EnableOutput(m_outputEnabled, m_outputSize);
            connect.SetNlsDateFormat(m_dateFormat);
            connect.AlterSessionNlsParams();
        }
    };

                        
void CSQLToolsApp::OnSettingsChanged ()
{
    try { EXCEPTION_FRAME;
        Threading::FrgdRequestQueue::Get()
            .Push(Threading::TaskPtr(new OnSettingsChangedTask));

        if (GetConnectOpen()
        && GetSQLToolsSettings().GetObjectCache())
        {
            SessionCache::InitIfNecessary();

            if (!GetConnectSlow())
                SessionCache::Reload(false);
        }

        if (m_pMainWnd)
        {
            UpdateApplicationTitle();
            ((CMDIMainFrame*)m_pMainWnd)->SetIndicators();
            ((CMDIMainFrame*)m_pMainWnd)->SetupMDITabbedGroups(); //FRM2
            ((CMDIMainFrame*)m_pMainWnd)->EvOnSettingsChanged();
        }
    }
    _DEFAULT_HANDLER_

    if (!g_isLoading)
        SaveSettings();
}

void CSQLToolsApp::LoadSettings ()
{
    SQLToolsSettingsXmlStreamer(L"sqltools.xml") >> getSettings();

    // 31.03.2003 bug fix, redundant sqltools settings saving
    g_isLoading = true;
    try 
    { 
        OnSettingsChanged(); 
    }
    catch (...) 
    { 
        g_isLoading = false; throw; 
    }
    g_isLoading = false;
}

void CSQLToolsApp::SaveSettings ()
{
    static bool s_isBackupDone = false;
    SQLToolsSettingsXmlStreamer(L"sqltools.xml", !s_isBackupDone) << getSettings();
    s_isBackupDone = true;
}


struct SettingsDialogCallback : COEDocument::SettingsDialogCallback
{
    SQLToolsSettings    m_settings;
    CPropServerScriptPageTree m_serverScriptPage;   // was: CPropServerPage + CPropScriptPage + CPropHistoryPage
    CDDLPageTree        m_ddlPage;              // was: CDBSCommonPage + CDBSTablePage
    CObjectViewerPageTree m_objViewerPage;  // was: CObjectViewerPage
    CPropAutocompletePageTree m_autocompletePage; // was: CPropAutocompletePage
    CGridPageTree       m_gridPage;         // was: CPropGridPage1 + CPropGridPage2 + CPropGridOutputPage1 + CPropGridOutputPage2
    CVisualAttributesPageTree m_vaPage;     // was: CVisualAttributesPage
    bool                m_ok;

    SettingsDialogCallback (const SQLToolsSettings& settings)
        : m_settings(settings),
        m_serverScriptPage(m_settings),
        m_ddlPage(m_settings),
        m_objViewerPage(m_settings),
        m_autocompletePage(m_settings),
        m_gridPage(m_settings),
        m_vaPage(m_settings.GetVASets()),
        m_ok(false)
    {
        // m_vaPage writes into the attributes of m_settings - the copy OnAppSettings()
        // pushes into the global settings after OK - through the pointers GetVASets()
        // returns, so m_settings is initialized first and has to outlive the page. Its
        // title, "SQLTools::Font and Color", is the constructor default of
        // CVisualAttributesPageTree and is the one the classic page was given here
        // (m_psp.pszTitle + PSP_USETITLE).
    }

    virtual void BeforePageAdding (Common::CPropertySheetMem& sheet)
    {
        sheet.SetTreeWidth(300);

        // The order of AddPage() is the order of the pages in the sheet's tree
        // (CTreePropSheet groups them by the part of the title before "::", and
        // keeps the adding order inside a group).
        //
        // "SQLTools::History" is gone from here: its settings are the "Execution
        // history" group of m_serverScriptPage now. Autocomplete moved up, next to
        // the other editor-ish page, instead of sitting behind Font and Color.
        sheet.AddPage(&m_serverScriptPage);
        sheet.AddPage(&m_ddlPage);
        sheet.AddPage(&m_objViewerPage);
        sheet.AddPage(&m_autocompletePage);
        sheet.AddPage(&m_gridPage);
        sheet.AddPage(&m_vaPage);
    }

    virtual void AfterPageAdding  (Common::CPropertySheetMem& /*sheet*/)
    {
    }

    virtual void OnOk ()
    {
        m_ok = true;
    }

    virtual void OnCancel () 
    {
    }
};

void CSQLToolsApp::OnAppSettings ()
{
    SettingsDialogCallback callback(GetSQLToolsSettings());
    COEDocument::ShowSettingsDialog(&callback);
    
    if (callback.m_ok)
    {
        GetSQLToolsSettingsForUpdate() = callback.m_settings;
        GetSQLToolsSettingsForUpdate().NotifySettingsChanged();

        setlocale(LC_ALL, COEDocument::GetSettingsManager().GetGlobalSettings()->GetLocale().c_str());
        UpdateAccelAndMenu();

        SetupFileManager((CMDIMainFrame*)m_pMainWnd);
    }
}


bool ShowDDLPreferences (SQLToolsSettings& settings, bool bLocal)
{
    BOOL showOnShiftOnly = AfxGetApp()->GetProfileInt(L"showOnShiftOnly", L"DDLPreferences",  FALSE);

    static UINT gStartPage = 0;
    Common::CPropertySheetMem sheet(bLocal ? L"Local DDL Preferences" : L"Global DDL Preferences", gStartPage);
    sheet.SetTreeViewMode(/*bTreeViewMode =*/TRUE, /*bPageCaption =*/FALSE, /*bTreeImages =*/FALSE);
    sheet.m_psh.dwFlags |= PSH_NOAPPLYNOW;
    sheet.m_psh.dwFlags &= ~PSH_HASHELP;
	sheet.SetTreeWidth(180);

    // (ak) Ctrl+F / F3 / Shift+F3 search this sheet, as they do the main Settings one -
    // through the same CPropTreeSearchBar above the page tree, this sheet being in tree
    // view mode too.
    sheet.EnableSearch();

    if (!bLocal || !showOnShiftOnly || HIBYTE(GetKeyState(VK_SHIFT))) 
    {
        CDDLPageTree ddlPage(settings);          // was: CDBSCommonPage + CDBSTablePage

        // The "Only show this dialog when the Shift key is down" row is built for the
        // local sheet only; its value is written to the profile after OK, below.
        ddlPage.m_bShowShowOnShiftOnly = bLocal ? TRUE : FALSE;
        ddlPage.m_bShowOnShiftOnly = showOnShiftOnly;

        sheet.AddPage(&ddlPage);
        
        if (sheet.DoModal() == IDOK) 
        {
            if (!bLocal)
            {
                GetSQLToolsSettingsForUpdate() = settings;
                GetSQLToolsSettingsForUpdate().NotifySettingsChanged();
            }
            else
                AfxGetApp()->WriteProfileInt(L"showOnShiftOnly", L"DDLPreferences",  ddlPage.m_bShowOnShiftOnly);

            return true;
        } 
        else
            return false;
    } 
    else
        return true;
}

    using namespace OpenEditor;

void CSQLToolsApp::SetupFileManager (CMDIMainFrame* pMainWnd)
{
    CStringArray names, filters;
    names.Add("All files (*.*)");
    filters.Add("*.*");

    const SettingsManager& settingMgrl = COEDocument::GetSettingsManager();

    for (int i(0), count(settingMgrl.GetClassCount()); i < count; i++)
    {
        const ClassSettingsPtr classSettings = settingMgrl.GetClassByPos(i);
        CString extensions = classSettings->GetExtensions().c_str();
        CString name = classSettings->GetName().c_str();
        CString filter;

        int curPos = 0;
        CString token = extensions.Tokenize(L" ",curPos);
        while (!token.IsEmpty())
        {
            if (!filter.IsEmpty()) filter += ';';
            filter += "*.";
            filter += token;
            token = extensions.Tokenize(L" ",curPos);
        }
        name += " ("; name += filter; name += ")";
        names.Add(name);
        filters.Add(filter);
    }

    int currentFilter = pMainWnd->GetFilePanelWnd().GetSelectedFilter();
    FileExplorerWnd& fileManager = pMainWnd->GetFilePanelWnd();
    fileManager.SetFileCategoryFilter(names, filters, currentFilter);

    GlobalSettingsPtr globalSerringsPtr = COEDocument::GetSettingsManager().GetGlobalSettings();
    fileManager.SetTooltips              (globalSerringsPtr->GetFileManagerTooltips             ());
    fileManager.SetPreviewInTooltips     (globalSerringsPtr->GetFileManagerPreviewInTooltips    ());
    fileManager.SetPreviewLines          (globalSerringsPtr->GetFileManagerPreviewLines         ());

    fileManager.SetShellContexMenuFileProperties (globalSerringsPtr->GetFileManagerShellContexMenuProperties());
    fileManager.SetShellContexMenuTortoiseGit    (globalSerringsPtr->GetFileManagerShellContexMenuTortoiseGIT());
    fileManager.SetShellContexMenuTortoiseSvn    (globalSerringsPtr->GetFileManagerShellContexMenuTortoiseSVN());

    pMainWnd->SetShellContexMenuFileProperties (globalSerringsPtr->GetFileManagerShellContexMenuProperties());
    pMainWnd->SetShellContexMenuTortoiseGit    (globalSerringsPtr->GetFileManagerShellContexMenuTortoiseGIT());
    pMainWnd->SetShellContexMenuTortoiseSvn    (globalSerringsPtr->GetFileManagerShellContexMenuTortoiseSVN());

    pMainWnd->GetOpenFilesWnd().SetShellContexMenuFileProperties (globalSerringsPtr->GetFileManagerShellContexMenuProperties());
    pMainWnd->GetOpenFilesWnd().SetShellContexMenuTortoiseGit    (globalSerringsPtr->GetFileManagerShellContexMenuTortoiseGIT());
    pMainWnd->GetOpenFilesWnd().SetShellContexMenuTortoiseSvn    (globalSerringsPtr->GetFileManagerShellContexMenuTortoiseSVN());
}

void CSQLToolsApp::SetupRecentFileList (CMDIMainFrame* pMainWnd)
{
    if (m_pRecentFileList.get())
        pMainWnd->GetRecentFilesListCtrl().Link(m_pRecentFileList); // FRM
}

