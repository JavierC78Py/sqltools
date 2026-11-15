/*
	SQLTools is a tool for Oracle database developers and DBAs.
    Copyright (C) 1997-2014 Aleksey Kochetov

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

// 03.01.2005 (ak) b1095462 Empty bug report on disconnect if connection was already lost.
// 2014-05-11 bug fix, read-only connection asks about commit/rollback on close
// 2016-04-10 bug fix, keep foreground/backgrount connection read-only attribute in sync
// 2016-04-10 Improvement, added dbms_application_info.set_client_info into background connection

#include "stdafx.h"
#include "App/SQLTools.h"
#include "App/MainFrm.h"
#include "COMMON/AppGlobal.h"
#include "COMMON/AppUtilities.h"
#include "COMMON/ExceptionHelper.h"
#include "Settings/SQLToolsSettings.h"
#include "Dialogs/ConnectDlg.h"
#include "SQLWorksheet/StatView.h"
#include "DatabaseBrowser/DbSourceWnd.h"
#include "Utilities/ExtractSchema/ExtractSchemaDlg.h"
#include "Utilities/TableTransformer.h"
#include <OCI8/Statement.h>
#include <OCI8/ACursor.h>
#include <OCI8/BCursor.h>
#include "App/SessionCache.h"
#include "Threading/TaskQueue.h"
#include "Threading/Thread.h"
#include "App/ConnectionTasks.h"
#include "OpenEditor/OEWorkspaceManager.h"
#include <App/ActivePrimeExecutionNote.h>
#include "Common/StrHelpers.h"


using namespace OpenEditor;

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif
/*
    struct SrvBkConnect : Threading::Task
    {
        string m_uid, m_password, m_alias;
        OCI8::EConnectionMode m_mode;
        bool m_readOnly, m_slow;
        int m_keepAlivePeriod;

        SrvBkConnect (OciConnect& mainConnect) 
            : Threading::Task("Open BackgroundConnection", 0)
        {
            m_uid = mainConnect.GetUID();
            m_password = mainConnect.GetPassword();
            m_alias = mainConnect.GetAlias();
            m_mode = mainConnect.GetMode();
            m_readOnly = mainConnect.IsReadOnly();
            m_slow = mainConnect.IsSlow();
            m_keepAlivePeriod = GetSQLToolsSettings().GetKeepAlivePeriod();
        }

        virtual void DoInBackground (OciConnect& connect)
        {
            Threading::g_keepAlivePeriod = m_keepAlivePeriod;

            if (connect.IsOpen())
                connect.Close();
            // add a clone method to OciConnect 
            connect.Open(m_uid.c_str(), m_password.c_str(), m_alias.c_str(), m_mode, m_readOnly, m_slow);
            connect.ExecuteStatement("begin dbms_application_info.set_client_info('sqltools background session'); end;", true);
        }

        void ReturnInForeground ()
        {
        }
    };

                        
    struct SrvBkDisconnect : Threading::Task
    {
        SrvBkDisconnect () : Threading::Task("Close BackgroundConnection", 0) {}

        virtual void DoInBackground (OciConnect& connect)
        {
            if (connect.IsOpen())
                connect.Close();
        }

        void ReturnInForeground ()
        {
        }
    };

    struct SrvBkSetReadOnly : Threading::Task
    {
        bool m_readOnly;

        SrvBkSetReadOnly (bool readOnly) 
            : Threading::Task("Set read-only of BackgroundConnection", 0),
            m_readOnly(readOnly)
        {}

        virtual void DoInBackground (OciConnect& connect)
        {
            connect.SetReadOnly(m_readOnly);
        }

        void ReturnInForeground ()
        {
        }
    };
 */

    static
    void make_substitution (map<string, string> substitutions, const char* str, int len, string& buffer)
    {
        const char* ptr = str, *end = str + len;

        while (ptr != end)
        {
            if (*ptr == '&')
            {
                bool save = false;

                if (*++ptr == '&')
                {
                    ptr++; // &&
                    save = true;
                }

                string var, val;

                while (ptr != end && (isalnum(*ptr) || *ptr == '_'))
                    var += *ptr++;

                if (ptr != end && *ptr == '.') ptr++;
                Common::to_upper_str(var.c_str(), var);

                std::map<std::string,std::string>::const_iterator
                    it = substitutions.find(var);

                if (it != substitutions.end())
                    val = it->second;

                buffer += val;
            }
            else
            {
                buffer += *ptr++;
            }
        }
    }

struct OnLoginPlSqlBlockTask : Threading::Task
{
    string m_block, m_user, m_currentSchema;

    OnLoginPlSqlBlockTask (const string& block) 
        : Threading::Task("Execute on-login pl/sql block", 0)
        ,m_block(block)
    {
    }

    virtual void DoInBackground (OciConnect& connect)
    {
        string buffer;
        map<string, string> substitutions;
        substitutions["_USER"] = connect.GetUID();
        substitutions["_CONNECT_IDENTIFIER"] = connect.GetConnectIdentifier();
        make_substitution(substitutions, m_block.c_str(), m_block.length(), buffer);
        m_block = buffer; 
        connect.ExecuteStatement(buffer.c_str());
        connect.GetCurrentUserAndSchema(m_user, m_currentSchema);
    }

    virtual void ReturnInForeground ()
    {
        theApp.SetCurrentSchema(m_currentSchema);
        ((CMDIMainFrame*)AfxGetMainWnd())->EvOnConnect();
    }

    virtual void ReturnErrorInForeground ()
    {
        m_error += string(80, '-') + '\n' + m_block;
    }
};


void CSQLToolsApp::AfterOpeningConnect ()
{
    //Threading::BkgdRequestQueue::Get().Push(Threading::TaskPtr(new SrvBkConnect(*m_connect)));

    COLORREF color = GetConnectReadOnly() ? RGB(255, 0, 0) : RGB(0, 0, 0);

    m_displayConnectionString = GetConnectDisplayString().c_str();

    UpdateApplicationTitle();

    CString buffer = m_displayConnectionString;
    buffer += ' ';
    buffer += '[';
    buffer += GetServerVersionStr().c_str();
    buffer += ']';

    ((CMDIMainFrame*)m_pMainWnd)->GetConnectionBar().SetConnectionDescription(buffer, color);
    ((CMDIMainFrame*)m_pMainWnd)->EvOnConnect();

    std::string block;
    if (CConnectDlg::GetOnLoginPlSqlBlock(block))
        Threading::BkgdRequestQueue::Get().Push(Threading::TaskPtr(new OnLoginPlSqlBlockTask(block)));

    if (GetSQLToolsSettings().GetSessionStatistics())
        CStatView::OpenAll();

    SessionCache::OnConnect();
}


void CSQLToolsApp::AfterClosingConnect ()
{
    m_displayConnectionString = m_displayNotConnected;

    UpdateApplicationTitle();

    if (m_pMainWnd)
        ((CMDIMainFrame*)m_pMainWnd)->GetConnectionBar()
            .SetConnectionDescription(m_displayConnectionString, RGB(0, 0, 0));

    ((CMDIMainFrame*)m_pMainWnd)->EvOnDisconnect();

    //if (GetSQLToolsSettings().GetSessionStatistics())
    //{
    //    try { EXCEPTION_FRAME;

    //        CStatView::CloseAll();
    //    }
    //    _DEFAULT_HANDLER_
    //}

    SessionCache::OnDisconnect();

    //Threading::BkgdRequestQueue::Get().Push(Threading::TaskPtr(new SrvBkDisconnect));
}

void CSQLToolsApp::AfterToggleReadOnly ()
{
    COLORREF color = GetConnectReadOnly() ? RGB(255, 0, 0) : RGB(0, 0, 0);
    ((CMDIMainFrame*)m_pMainWnd)->GetConnectionBar().SetColor(color);
}

void CSQLToolsApp::OnSqlReconnect()
{
    if (GetCanReconnect())
        ConnectionTasks::SubmitReconnectTask();
} 

void CSQLToolsApp::OnSqlConnect()
{
    try { EXCEPTION_FRAME;

        static CConnectDlg connectDlg(m_pMainWnd);

        while (connectDlg.DoModal() == IDOK)
        {
            const ConnectEntry& entry = connectDlg.GetConnectEntry();

            if (!entry.m_Direct)
                ConnectionTasks::SubmitConnectTask(Common::str(entry.m_User), Common::str(entry.m_DecryptedPassword), Common::str(entry.m_Alias), 
                    ConnectionTasks::ToConnectionMode(entry.m_Mode), entry.m_Safety ? true : false, entry.m_Slow
                );
            else
                ConnectionTasks::SubmitConnectTask(Common::str(entry.m_User), Common::str(entry.m_DecryptedPassword), 
                    Common::str(entry.m_Host), Common::str(entry.m_Port), Common::str(entry.m_Sid), entry.m_UseService, 
                    ConnectionTasks::ToConnectionMode(entry.m_Mode), 
                    entry.m_Safety ? true : false, entry.m_Slow
                );
            break;
        }
    }
    catch (CUserException*) {}
     _DEFAULT_HANDLER_
}

    struct AppCommitTask : Threading::Task
    {
        AppCommitTask () : Threading::Task("Commit", 0) {}
        virtual void DoInBackground (OciConnect& connect) 
        { 
            if (connect.IsOpen()) 
            {
                ActivePrimeExecutionOnOff onOff; // can i do that here?
                connect.Commit(); 
            }
        }
    };

void CSQLToolsApp::OnSqlCommit()
{
    try { EXCEPTION_FRAME;
        Threading::FrgdRequestQueue::Get()
            .Push(Threading::TaskPtr(new AppCommitTask));
    }
    _DEFAULT_HANDLER_
}

    struct AppRollbackTask : Threading::Task
    {
        AppRollbackTask () : Threading::Task("Rollback", 0) {}
        virtual void DoInBackground (OciConnect& connect) 
        { 
            if (connect.IsOpen()) 
            {
                ActivePrimeExecutionOnOff onOff; // can i do that here?
                connect.Rollback(); 
            }
        }
    };

void CSQLToolsApp::OnSqlRollback()
{
    try { EXCEPTION_FRAME;
        Threading::FrgdRequestQueue::Get()
            .Push(Threading::TaskPtr(new AppRollbackTask));
    }
    _DEFAULT_HANDLER_
}

void CSQLToolsApp::OnSqlDisconnect()
{
    try { EXCEPTION_FRAME;

        ConnectionTasks::SubmitDisconnectTask();
    }
    _DEFAULT_HANDLER_
}

void CSQLToolsApp::OnSqlDbmsOutput()
{
    GetSQLToolsSettingsForUpdate().SetOutputEnable(!GetSQLToolsSettings().GetOutputEnable());
}

void CSQLToolsApp::OnSqlSubstitutionVariables()
{
    GetSQLToolsSettingsForUpdate().SetScanForSubstitution(!GetSQLToolsSettings().GetScanForSubstitution());
}

void CSQLToolsApp::OnUpdate_SqlSubstitutionVariables (CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(GetSQLToolsSettings().GetScanForSubstitution() ?  1 : 0);
    pCmdUI->Enable(!theApp.GetActivePrimeExecution() ? TRUE : FALSE);
}

void CSQLToolsApp::OnSqlHaltOnError ()
{
    GetSQLToolsSettingsForUpdate().SetExecutionHaltOnError(!GetSQLToolsSettings().GetExecutionHaltOnError());
}

void CSQLToolsApp::OnUpdate_SqlHaltOnError (CCmdUI* pCmdUI)
{
    pCmdUI->SetCheck(GetSQLToolsSettings().GetExecutionHaltOnError() ?  1 : 0);
    pCmdUI->Enable(!theApp.GetActivePrimeExecution() ? TRUE : FALSE);
}

void CSQLToolsApp::OnUpdate_SqlGroup (CCmdUI* pCmdUI)
{
    ASSERT(ID_SQL_TABLE_TRANSFORMER - ID_SQL_CONNECT == 10);

    switch (pCmdUI->m_nID)
    {
    case ID_SQL_CONNECT: 
        pCmdUI->Enable(!theApp.GetActivePrimeExecution() ? TRUE : FALSE);
        break;
    case ID_SQL_RECONNECT:
        pCmdUI->Enable(GetCanReconnect() && !theApp.GetActivePrimeExecution() ? TRUE : FALSE);
        break;
    case ID_SQL_DBMS_OUTPUT:
        if (GetSQLToolsSettings().GetOutputEnable())
            pCmdUI->SetCheck(1);
        else
            pCmdUI->SetCheck(0);
        pCmdUI->Enable(GetConnectOpen() && !theApp.GetActivePrimeExecution() ? TRUE : FALSE);
        break;
    case ID_SQL_SESSION_STATISTICS:
        if (GetSQLToolsSettings().GetSessionStatistics())
            pCmdUI->SetCheck(1);
        else
            pCmdUI->SetCheck(0);
        pCmdUI->Enable(GetConnectOpen() && !theApp.GetActivePrimeExecution() ? TRUE : FALSE);
        break;
    case ID_SQL_READ_ONLY:
        if (GetConnectReadOnly())
            pCmdUI->SetCheck(1);
        else
            pCmdUI->SetCheck(0);
        pCmdUI->Enable(GetConnectOpen() && !theApp.GetActivePrimeExecution() ? TRUE : FALSE);
        break;
    default:
        pCmdUI->Enable(GetConnectOpen() && !theApp.GetActivePrimeExecution() ? TRUE : FALSE);
    }
}

void CSQLToolsApp::OnSqlSessionStatistics()
{
    try { EXCEPTION_FRAME;

        GetSQLToolsSettingsForUpdate().SetSessionStatistics(!GetSQLToolsSettings().GetSessionStatistics());

        if (GetSQLToolsSettings().GetSessionStatistics())
            CStatView::OpenAll();
        else
            CStatView::CloseAll();
    }
    _COMMON_DEFAULT_HANDLER_
}

void CSQLToolsApp::OnSqlRefreshObjectCache()
{
    try { EXCEPTION_FRAME;

        if (GetConnectSlow())
        {
            if (AfxMessageBox(L"This operation may take some time on a slow network. "
                L"\r\n\r\nDo you want to continue? "
                L"\r\n\r\nIf your network is not actually slow, "
                L"\r\nyou can uncheck the \"Slow network\" connection option.", MB_YESNO|MB_ICONEXCLAMATION) != IDYES
            )
                return;
            
            SessionCache::Reload(true, true);
        }
        else
            SessionCache::Reload(true);
    }
    _DEFAULT_HANDLER_
}

void CSQLToolsApp::OnSqlToggleReadOnly()
{
    try { EXCEPTION_FRAME;

        ConnectionTasks::SubmitToggleReadOnlyTask();
    }
    _DEFAULT_HANDLER_
}

void CSQLToolsApp::OnSqlExtractSchema()
{
    CExtractSchemaDlg(m_pMainWnd).DoModal();
}

void CSQLToolsApp::OnSqlTableTransformer()
{
    CTableTransformer(m_pMainWnd).DoModal();
}

