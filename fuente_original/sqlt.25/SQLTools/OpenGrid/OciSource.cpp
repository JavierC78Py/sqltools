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

/*
    09.02.2004 bug fix, bug-reporting activation for oracle errors on grid fetching
	16.12.2004 (Ken Clubok) Add IsStringField method, to support CSV prefix option
    03.01.2005 (ak) Rename IsStringField to IsTextField
    2017-11-28 implemented range selection
    2017-11-28 added 3 new export formats
*/

#include "stdafx.h"
#include "OciSource.h"
#include "OciGridView.h"
#include "GridRowIdEdit.h"
#include "OCI8/ACursor.h"
#include <COMMON/AppGlobal.h>
#include "COMMON/StrHelpers.h" // for print_time

#include "App/SQLTools.h"
#include "App/SQLUtilities.h"
#include "App/ActivePrimeExecutionNote.h"
#include "Threading/TaskQueue.h"
#include "Threading/MessageOnlyWindow.h"
#include "App/FetchWaitDlg.h"

using namespace Threading;

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

    using namespace OG2;
    using namespace OCI8;

    const std::string OciGridSource::m_emptyString;

namespace {

bool IsNumericKind (Common::SqlValueKind kind)
{
    switch (kind)
    {
    case Common::SqlValueKind::Number:
    case Common::SqlValueKind::BinaryFloat:
    case Common::SqlValueKind::BinaryDouble:
        return true;
    default:
        return false;
    }
}

bool IsDatetimeKind (Common::SqlValueKind kind)
{
    switch (kind)
    {
    case Common::SqlValueKind::Date:
    case Common::SqlValueKind::Timestamp:
    case Common::SqlValueKind::TimestampTz:
    case Common::SqlValueKind::TimestampLtz:
    case Common::SqlValueKind::IntervalYm:
    case Common::SqlValueKind::IntervalDs:
        return true;
    default:
        return false;
    }
}

} // anonymous namespace

namespace {

void RaiseActivePrimeExecution (const std::shared_ptr<OciGridFetchControl>& control)
{
    if (!control.get())
        return;

    InterlockedExchange(&control->m_busyRaised, 1L);
    ActivePrimeExecutionNote::SendActivePrimeExecution(true);
}

void LowerActivePrimeExecution (const std::shared_ptr<OciGridFetchControl>& control, bool nothrow)
{
    if (!control.get())
        return;

    if (!InterlockedExchange(&control->m_busyRaised, 0L))
        return;

    try {
        ActivePrimeExecutionNote::SendActivePrimeExecution(false);
    } catch (...) {
        if (!nothrow) throw;
    }
}

} // anonymous namespace

OciGridSource::OciGridSource ()
{
    m_bytesAllocated = 0;
    m_fetchPortions = 0;
    m_allRowsFetched = true;
    m_headersInLowerCase = true;
    m_fetchTime = 0.0;
    m_rowIdColumn = OG2::ercNone;
    m_pendingRow = -1;
}

    // Gives up the cursor's last reference off the UI thread.
    struct BackgroundTask_DestroyCursor : Task
    {
        std::shared_ptr<OCI8::AutoCursor> m_cursor;

        BackgroundTask_DestroyCursor ()
            : Task("DestroyCursor", 0)
        {
            SetSilent(true);
        }

        void DoInBackground (OciConnect&)
        {
            m_cursor.reset();
        }
    };

    // Closes a cursor without giving up the reference to it.
    struct BackgroundTask_CloseCursor : Task
    {
        std::shared_ptr<OCI8::AutoCursor> m_cursor;

        BackgroundTask_CloseCursor ()
            : Task("CloseCursor", 0)
        {
            SetSilent(true);
        }

        void DoInBackground (OciConnect&)
        {
            try { if (m_cursor.get()) m_cursor->Close(); } catch (...) {}
        }
    };

void OciGridSource::StopActiveFetch (bool forgetSource)
{
    if (!m_fetchControl.get())
        return;

    InterlockedExchange(&m_fetchControl->m_stop, 1L);

    if (forgetSource)
        m_fetchControl->m_source = 0;

    LowerActivePrimeExecution(m_fetchControl, true/*nothrow*/);

    m_fetchControl.reset();
}

void OciGridSource::CancelActiveFetch ()
{
    if (!m_fetchControl.get())
        return;

    InterlockedExchange(&m_fetchControl->m_cancelled, 1L);
}

bool OciGridSource::ReleaseFetchSlot (const std::shared_ptr<OciGridFetchControl>& control)
{
    if (!m_fetchControl.get() || m_fetchControl.get() != control.get())
        return false;

    m_fetchControl.reset();
    return true;
}

bool OciGridSource::ReleaseEditSlot (const std::shared_ptr<OciGridEditControl>& control)
{
    if (!m_editControl.get() || m_editControl.get() != control.get())
        return false;

    m_editControl.reset();
    return true;
}

OciGridSource::~OciGridSource ()
{
    StopActiveFetch(true);

    // Before anything is queued: a write-back task already in flight must find this slot empty
    // rather than a source being destroyed, which is what StopActiveFetch(true) does for the fetch.
    if (m_editControl.get())
        m_editControl->m_source = 0;
    m_editControl.reset();

    TaskPtr task(new BackgroundTask_DestroyCursor);
    static_cast<BackgroundTask_DestroyCursor*>(task.get())->m_cursor.swap(m_cursor);
    FrgdRequestQueue::Get().Push(task);
}

    // Reads the cursor's field names and kinds in the background, then publishes them to the grid.
    struct BackgroundTask_SetCursor : Task
    {
        HWND m_hWnd;
        OciGridView& m_view;
        OciGridSource& m_source;
        std::shared_ptr<OCI8::AutoCursor> m_cursor;
        vector<string> m_headers;
        vector<Common::SqlValueKind> m_kinds;

        BackgroundTask_SetCursor (OciGridView& view, OciGridSource& source)
            : Task("SetCursor", 0),
            m_view(view),
            m_hWnd(view),
            m_source(source),
            m_cursor(source.m_cursor)
        {
            SetSilent(true);
        }

        void DoInBackground (OciConnect&)
        {
            if (m_cursor.get() && m_cursor->IsOpen())
            {
                int ncols = m_cursor->GetFieldCount();
                m_headers.resize(ncols);
                m_kinds.resize(ncols);

                for (int i = 0; i < ncols; i++)
                {
                    m_headers.at(i) = m_cursor->GetFieldName(i);
                    m_kinds.at(i) = m_cursor->GetFieldKind(i);
                }
            }
        }

        void ReturnInForeground ()
        {
            if (::IsWindow(m_hWnd) && !m_headers.empty())
            {
                m_source.SetMaxShowLines(GetSQLToolsSettings().GetGridMultilineCount());
                m_source.m_headersInLowerCase = GetSQLToolsSettings().GetGridHeadersInLowerCase();

                int ncols = m_headers.size();

                m_source.SetNullText(m_cursor->GetStringNull());

                m_source.SetCols(ncols);
                for (int i = 0; i < ncols; i++)
                {
                    if (m_source.m_headersInLowerCase)
                        m_source.SetColHeader(i, SQLUtilities::GetSafeDatabaseObjectName(m_headers.at(i), true).c_str());
                    else
                        m_source.SetColHeader(i, m_headers.at(i));

                    m_source.SetColKind(i, m_kinds.at(i));
                    m_source.SetColAlignment(i, IsNumericKind(m_kinds.at(i)) ? ecaRight : ecaLeft);
                }

                // Detection runs on these raw names, before the transform above
                // rewrites them into something a ROWID need not still match and SET could not name.
                m_source.m_rawHeaders = m_headers;
                m_source.ApplyRowIdEditState();

                // From here on every cell's display text is rendered from the typed payload, so the
                // format context must be in place before the first fetch paints anything.
                m_source.ApplyGridFormatContext();

                if (m_source.IsTableOrientation())
                    m_source.Notify_ChangedColsQuantity(0, ncols);
                else
                    m_source.Notify_ChangedRowsQuantity(0, ncols);

                m_view.SetExpandLastCol(!IsNumericKind(*m_kinds.rbegin()));
            }
        }
    };

    // A closed connection answers from the error handle Close() already freed, so the OCI throw that
    // follows arrives with no text at all and the caller has nothing to show the user.
    static void RequireOpenConnection (const OciConnect& connect, const char* operation)
    {
        if (!connect.IsOpen())
            throw OciException(0, (std::string("The connection is closed, so the ") + operation
                                   + " was never sent to the database.").c_str());
    }

    // What locking a row before writing it answered.
    enum ERowLock { rlLockTaken, rlLockBusy, rlRowGone };

    // A read-only connection refuses the write that follows at Statement::Execute, and cannot release
    // the lock either - Commit refuses on it, and so does ROLLBACK, neither being a SELECT. Refuse now.
    static void RequireWritableConnection (const OciConnect& connect, const char* operation)
    {
        if (connect.IsReadOnly())
            throw OciException(0, (std::string("The connection is read-only, so the ") + operation
                                   + " was never sent to the database.").c_str());
    }

    // NOWAIT, so a contended row answers ORA-00054 at once instead of parking the one shared
    // background connection. Fetched: FOR UPDATE locks only fetched rows, and so tells gone from held.
    static ERowLock LockRowForWrite (OciConnect& connect, const std::string& lockSql,
                                     const std::string& rowId)
    {
        try
        {
            // Declared before the cursor so it outlives the handle that points into its buffer.
            OCI8::StringVar rid(rowId.c_str(), static_cast<int>(rowId.size()));
            OCI8::AutoCursor cur(connect, lockSql.c_str());

            // Off whatever the setting says: under OCI_COMMIT_ON_SUCCESS this SELECT would commit
            // itself and release the lock before the write ran. Committed once, after the write.
            cur.EnableAutocommit(false);
            cur.Bind(&rid, "rid");
            cur.Execute();

            return cur.Fetch() ? rlLockTaken : rlRowGone;
        }
        catch (const OciUserCancel&)
        {
            throw;
        }
        catch (const OciException& x)
        {
            if (54 == int(x))
                return rlLockBusy;

            throw;
        }
    }

    // With autocommit off the same session may carry the user's own uncommitted work, and losing that
    // to release one row lock is the worse harm - so undo only where lock and write are the whole of it.
    static void RollbackIfAutocommit (OciConnect& connect, bool autocommit)
    {
        if (!autocommit)
            return;

        try { connect.Rollback(); }
        catch (const OciUserCancel&) { throw; }
        catch (...) { }   // the error already recorded is the one worth showing
    }

    // One cell's UPDATE. Runs here because every OCI call runs off the UI thread, and reports
    // through the task's error channel, which the caller already turns into a dialog.
    struct BackgroundTask_GridUpdate : Task
    {
        std::shared_ptr<OciGridEditControl> m_control;

        std::string m_sql;
        std::string m_lockSql;        // FOR UPDATE NOWAIT, run first to claim the row
        std::string m_newValue;
        std::string m_rowId;          // the text the row was addressed by, and the publish guard

        int m_physicalRow;            // PHYSICAL, translated before the task was queued
        int m_tableCol;
        int m_rowIdCol;

        // Read on the UI thread, the way ExecuteCtx copies the settings it passes in: the settings
        // singleton is not something a worker thread may reach for.
        bool m_autocommit;

        int         m_affected;
        std::string m_newRowId;

        BackgroundTask_GridUpdate (const std::shared_ptr<OciGridEditControl>& control,
                                   const std::string& sql,
                                   const std::string& lockSql,
                                   const std::string& newValue,
                                   const std::string& rowId,
                                   int physicalRow, int tableCol, int rowIdCol)
            : Task("GridUpdate", 0)
            , m_control(control)
            , m_sql(sql)
            , m_lockSql(lockSql)
            , m_newValue(newValue)
            , m_rowId(rowId)
            , m_physicalRow(physicalRow)
            , m_tableCol(tableCol)
            , m_rowIdCol(rowIdCol)
            , m_autocommit(GetSQLToolsSettings().GetAutocommit())
            , m_affected(-1)
        {
        }

        void DoInBackground (OciConnect& connect)
        {
            RequireOpenConnection(connect, "update");
            RequireWritableConnection(connect, "update");

            // All three outlive Execute(). newRid is left unassigned, which is what makes it an OUT
            // bind; an empty newValue binds as OCI_IND_NULL, so clearing a cell writes NULL.
            OCI8::StringVar newVal(m_newValue.c_str(), static_cast<int>(m_newValue.size()));
            OCI8::StringVar rid(m_rowId.c_str(), static_cast<int>(m_rowId.size()));
            OCI8::StringVar newRid(64);

            // Claimed before it is written, so a row another session holds is reported rather than
            // waited for. Neither refusal publishes, and not publishing is the whole of the revert.
            switch (LockRowForWrite(connect, m_lockSql, m_rowId))
            {
            case rlLockBusy:
                SetError("The row is locked by another session, so the update was not made and the "
                         "grid was left as it was.\n\n"
                         "Another session has this row open and uncommitted. Please try again once "
                         "it commits or rolls back.\n\n" + m_sql);
                return;

            case rlRowGone:
                SetError("The row no longer exists, so the update was not made and the grid was "
                         "left as it was.\n\n"
                         "It was most likely deleted by another session since it was fetched.\n\n"
                         + m_sql);
                return;

            case rlLockTaken:
                break;
            }

            OCI8::AutoCursor cur(connect, m_sql.c_str());

            // Not m_autocommit: OCI_COMMIT_ON_SUCCESS would end the transaction the lock above was
            // taken in before the count below was read. The commit is issued once, after that read.
            cur.EnableAutocommit(false);

            cur.Bind(&newVal, "newval");
            cur.Bind(&rid,    "rid");
            cur.Bind(&newRid, "newrid");
            cur.Execute();

            m_affected = static_cast<int>(cur.GetRowCount());

            // Not exactly one row is reported, never published: a dead ROWID answers 0 rows rather
            // than ORA-01410, so this check is all that catches a lost edit.
            if (1 != m_affected)
            {
                SetError("The update changed " + std::to_string(m_affected)
                    + " row(s), not exactly one, so the grid was left as it was.\n\n"
                    "The row was most likely moved or deleted since it was fetched.\n\n"
                    + m_sql);
                RollbackIfAutocommit(connect, m_autocommit);
                return;
            }

            newRid.GetString(m_newRowId);

            // The commit the setting asks for, issued once now that neither statement commits itself.
            // Off, the transaction stays open as before and the lock stays with it until the user does.
            if (m_autocommit)
                connect.Commit();
        }

        void ReturnInForeground ()      { Publish(); }
        void ReturnErrorInForeground () { Publish(); }

        void Publish ()
        {
            OciGridSource* const source = m_control->m_source;
            if (!source || !source->ReleaseEditSlot(m_control))
                return;

            if (!IsOk() || !::IsWindow(m_control->m_hWnd))
                return;

            source->PublishCellUpdate(m_physicalRow, m_tableCol, m_rowIdCol,
                                      m_rowId, m_newValue, m_newRowId);
        }
    };

    // One row's DELETE. Same queue and same slot as the update, and the same rule that a rowcount
    // other than exactly one is reported and never published - which here also keeps the row shown.
    struct BackgroundTask_GridDelete : Task
    {
        std::shared_ptr<OciGridEditControl> m_control;

        std::string m_sql;
        std::string m_lockSql;        // FOR UPDATE NOWAIT, run first to claim the row
        std::string m_rowId;          // the text the row was addressed by, and the publish guard

        int m_physicalRow;            // PHYSICAL, translated before the task was queued
        int m_rowIdCol;

        // Read on the UI thread, the way ExecuteCtx copies the settings it passes in: the settings
        // singleton is not something a worker thread may reach for.
        bool m_autocommit;

        int m_affected;

        BackgroundTask_GridDelete (const std::shared_ptr<OciGridEditControl>& control,
                                   const std::string& sql,
                                   const std::string& lockSql,
                                   const std::string& rowId,
                                   int physicalRow, int rowIdCol)
            : Task("GridDelete", 0)
            , m_control(control)
            , m_sql(sql)
            , m_lockSql(lockSql)
            , m_rowId(rowId)
            , m_physicalRow(physicalRow)
            , m_rowIdCol(rowIdCol)
            , m_autocommit(GetSQLToolsSettings().GetAutocommit())
            , m_affected(-1)
        {
        }

        void DoInBackground (OciConnect& connect)
        {
            RequireOpenConnection(connect, "delete");
            RequireWritableConnection(connect, "delete");

            // No RETURNING here: a deleted row has no refreshed address to publish back.
            OCI8::StringVar rid(m_rowId.c_str(), static_cast<int>(m_rowId.size()));

            // Claimed before it is deleted, for the reason the update claims: a contended row would
            // otherwise park the one shared background connection. Neither refusal publishes.
            switch (LockRowForWrite(connect, m_lockSql, m_rowId))
            {
            case rlLockBusy:
                SetError("The row is locked by another session, so the delete was not made and the "
                         "grid was left as it was.\n\n"
                         "Another session has this row open and uncommitted. Please try again once "
                         "it commits or rolls back.\n\n" + m_sql);
                return;

            case rlRowGone:
                SetError("The row no longer exists, so the delete was not made and the grid was "
                         "left as it was.\n\n"
                         "It was most likely already deleted by another session.\n\n" + m_sql);
                return;

            case rlLockTaken:
                break;
            }

            OCI8::AutoCursor cur(connect, m_sql.c_str());

            // Not m_autocommit, as in the update: the commit is issued once, after the count below is
            // read, so a delete that removed other than one row can still be rolled back.
            cur.EnableAutocommit(false);

            cur.Bind(&rid, "rid");
            cur.Execute();

            m_affected = static_cast<int>(cur.GetRowCount());

            // A rowcount other than exactly one is an error, never a success: a dead ROWID answers 0
            // rows rather than ORA-01410, so without this a row someone else took looks deleted.
            if (1 != m_affected)
            {
                SetError("The delete removed " + std::to_string(m_affected)
                    + " row(s), not exactly one, so the grid was left as it was.\n\n"
                    "The row was most likely moved or deleted since it was fetched.\n\n"
                    + m_sql);
                RollbackIfAutocommit(connect, m_autocommit);
                return;
            }

            if (m_autocommit)
                connect.Commit();
        }

        void ReturnInForeground ()      { Publish(); }
        void ReturnErrorInForeground () { Publish(); }

        void Publish ()
        {
            OciGridSource* const source = m_control->m_source;
            if (!source || !source->ReleaseEditSlot(m_control))
                return;

            if (!IsOk() || !::IsWindow(m_control->m_hWnd))
                return;

            source->PublishRowDelete(m_physicalRow, m_rowIdCol, m_rowId);
        }
    };

    // One row's INSERT, on the same queue and slot as the other two writes. RETURNING rather than a
    // refetch: the new row need not satisfy the query the grid was filled from, and could be lost.
    struct BackgroundTask_GridInsert : Task
    {
        std::shared_ptr<OciGridEditControl> m_control;

        std::string              m_sql;
        std::vector<std::string> m_values;  // one per bind, in the order the SQL names them

        int m_physicalRow;                  // PHYSICAL, translated before the task was queued
        int m_rowIdCol;

        // Read on the UI thread, the way ExecuteCtx copies the settings it passes in: the settings
        // singleton is not something a worker thread may reach for.
        bool m_autocommit;

        int         m_affected;
        std::string m_newRowId;

        BackgroundTask_GridInsert (const std::shared_ptr<OciGridEditControl>& control,
                                   const std::string& sql,
                                   const std::vector<std::string>& values,
                                   int physicalRow, int rowIdCol)
            : Task("GridInsert", 0)
            , m_control(control)
            , m_sql(sql)
            , m_values(values)
            , m_physicalRow(physicalRow)
            , m_rowIdCol(rowIdCol)
            , m_autocommit(GetSQLToolsSettings().GetAutocommit())
            , m_affected(-1)
        {
        }

        void DoInBackground (OciConnect& connect)
        {
            RequireOpenConnection(connect, "insert");

            // By pointer, not by value: OCI keeps the address each Bind is handed, so the variables
            // must not move once bound.
            std::vector<std::unique_ptr<OCI8::StringVar>> vars;
            vars.reserve(m_values.size());

            // Left unassigned, which is what makes it an OUT bind - the same trick the update uses.
            OCI8::StringVar newRid(64);

            OCI8::AutoCursor cur(connect, m_sql.c_str());
            cur.EnableAutocommit(m_autocommit);

            // :v1..:vN in the order BuildInsertSql named the columns, which is the order the caller
            // put the values in. Bind by name, as the live probe confirmed the server accepts.
            for (size_t i = 0; i < m_values.size(); ++i)
            {
                vars.push_back(std::unique_ptr<OCI8::StringVar>(
                    new OCI8::StringVar(m_values[i].c_str(), static_cast<int>(m_values[i].size()))));

                // Built per column rather than written as a literal, because the binds are numbered
                // and AutoCursor takes the name by reference.
                const std::string name = "v" + std::to_string(i + 1);
                cur.Bind(vars.back().get(), name);
            }

            cur.Bind(&newRid, "newrid");
            cur.Execute();

            m_affected = static_cast<int>(cur.GetRowCount());

            // Not exactly one row is reported, never published. An INSERT that an instead-of rule
            // or a trigger turned into something else would otherwise look like it worked.
            if (1 != m_affected)
            {
                SetError("The insert reported " + std::to_string(m_affected)
                    + " row(s), not exactly one, so the grid was left as it was.\n\n"
                    + m_sql);
                return;
            }

            newRid.GetString(m_newRowId);

            // Without an address the row would sit in the grid uneditable and undeletable, which is
            // worse than an error the user can act on.
            if (m_newRowId.empty())
            {
                SetError("The insert did not hand back a ROWID, so the new row cannot be addressed "
                    "and the grid was left as it was.\n\n"
                    + m_sql);
            }
        }

        void ReturnInForeground ()      { Publish(); }
        void ReturnErrorInForeground () { Publish(); }

        void Publish ()
        {
            OciGridSource* const source = m_control->m_source;
            if (!source || !source->ReleaseEditSlot(m_control))
                return;

            if (!IsOk() || !::IsWindow(m_control->m_hWnd))
                return;

            source->PublishRowInsert(m_physicalRow, m_rowIdCol, m_newRowId);
        }
    };

void OciGridSource::SetCursor (OciGridView& view, std::unique_ptr<OCI8::AutoCursor>& cursor)
{
    m_allRowsFetched = false;
    m_bytesAllocated = 0;
    m_fetchPortions = 0;

    StopActiveFetch(false);

    Clear();

    {
        TaskPtr task(new BackgroundTask_DestroyCursor);
        static_cast<BackgroundTask_DestroyCursor*>(task.get())->m_cursor.swap(m_cursor);
        FrgdRequestQueue::Get().Push(task);
    }

    m_cursor = std::shared_ptr<OCI8::AutoCursor>(cursor.release());
    m_fetchTime = 0;

    // A new result set invalidates every edit answer and drops the slot a queued write-back
    // would publish into. The SQL comes off the cursor, so it cannot drift from what was prepared.
    m_editControl.reset();
    m_rawHeaders.clear();
    m_editTable.clear();
    m_rowIdColumn = OG2::ercNone;
    m_editSql = m_cursor.get() ? m_cursor->GetText() : std::string();

    // Clear() above took the table the staged row lived in, so the index would name a row that is
    // not there. Forgotten here rather than posted into the new result set.
    ClearPendingState();

    // Ahead of the task below: OciGridView::SetCursor queues the first Fetch() as soon as this
    // returns, and that worker snapshots Table().LazyRender(), which only this call turns on.
    ApplyGridFormatContext();

    FrgdRequestQueue::Get().Push(TaskPtr(new BackgroundTask_SetCursor(view, *this)));
}

void OciGridSource::ApplyGridFormatContext ()
{
    const SQLToolsSettings& settings = GetSQLToolsSettings();
    Common::SqlFormat& fmt = Table().Format();

    // Number: an empty setting means the same TM9 default the eager display path falls back to,
    // and SqlFormat's constructor already carries that default.
    if (!settings.GetGridNlsNumberFormat().empty())
        fmt.SetNumberFormat(settings.GetGridNlsNumberFormat());

    if (m_cursor.get())
    {
        OciConnect& connect = m_cursor->GetConnect();

        // Date/Timestamp/TimestampTz: an empty setting means the session's NLS picture, which is
        // what the eager display path used. SqlFormat's own defaults remain the last resort.
        fmt.SetDateFormat      (!settings.GetGridNlsDateFormat().empty()
                                ? settings.GetGridNlsDateFormat() : connect.GetNlsDateFormat());
        fmt.SetTimestampFormat (!settings.GetGridNlsTimestampFormat().empty()
                                ? settings.GetGridNlsTimestampFormat() : connect.GetNlsTimestampFormat());
        fmt.SetTimestampTzFormat(!settings.GetGridNlsTimestampTzFormat().empty()
                                 ? settings.GetGridNlsTimestampTzFormat() : connect.GetNlsTimestampTzFormat());

        // The two NLS numeric characters come from the session rather than a grid setting, so the
        // lazy renderer matches the NLS_NUMERIC_CHARACTERS handback the eager path applies.
        const std::string& nls = connect.GetNlsNumericCharacters();
        if (nls.size() >= 2)
        {
            fmt.SetDecimalChar(nls[0]);
            fmt.SetGroupChar(nls[1]);
        }
    }

    // Intervals get one picture each, matching Oracle's two interval types; an empty setting keeps
    // SqlFormat's full-precision default rather than a shared TIME picture, which resolves no YM token.
    if (!settings.GetGridNlsIntervalYmFormat().empty())
        fmt.SetIntervalYmFormat(settings.GetGridNlsIntervalYmFormat());
    if (!settings.GetGridNlsIntervalDsFormat().empty())
        fmt.SetIntervalDsFormat(settings.GetGridNlsIntervalDsFormat());

    // An empty marker is legal and blanks NULLs, so this is never skipped.
    fmt.SetNullText(settings.GetGridNullRepresentation());
    fmt.SetBlobHexRowLength(settings.GetGridBlobHexRowLength());

    Table().SetLazyRender(true);
}

// The other half of ApplyGridFormatContext, and the reason a write-back never binds :newval bare into a
// non-character column. Two faults share one cure:
//
//   - The editor holds what SqlFormat rendered under the grid's own pictures, so a bare bind lets the
//     server parse that text with the session's format instead: ORA-01861 for a date, ORA-01722 or a
//     silently wrong value for a number.
//   - The bind is an OCI8::StringVar, which is SQLT_LVC: a length-prefixed LONG VARCHAR. Oracle will not
//     coerce one straight into a NUMBER, DATE or INTERVAL column and answers ORA-01459 - which is why a
//     bare bind fails even when the two pictures happen to agree, and why the empty-setting cases below
//     wrap all the same. As a function's character argument the same bind is fine.
//
// Text is the exception and binds bare, because there the LONG VARCHAR lands in a character column and
// nothing has to be coerced.
std::string OciGridSource::ValueExprFor (int tableCol, const char* bind) const
{
    const Common::SqlFormat& fmt = Table().Format();

    // Whichever picture SqlFormat rendered with is the one named back, so display and parse cannot
    // drift apart; the setting-or-session choice was already settled in ApplyGridFormatContext.
    switch (GetColKind(tableCol))
    {
    case Common::SqlValueKind::Date:
        return OG2::BuildValueExpr(bind, "TO_DATE", fmt.DateFormat());

    case Common::SqlValueKind::Timestamp:
        return OG2::BuildValueExpr(bind, "TO_TIMESTAMP", fmt.TimestampFormat());

    case Common::SqlValueKind::TimestampTz:
    case Common::SqlValueKind::TimestampLtz:  // rendered through the Tz picture, so parsed by it too
        return OG2::BuildValueExpr(bind, "TO_TIMESTAMP_TZ", fmt.TimestampTzFormat());

    case Common::SqlValueKind::Number:
        // A minimal picture renders the shortest canonical text in the session's own decimal character,
        // which is exactly what a picture-less TO_NUMBER reads back, so no format model is named and
        // none can disagree. TM is not an input model TO_NUMBER accepts in any case. The wrapper still
        // has to be there: it is what gives the SQLT_LVC bind a character position to sit in.
        if (fmt.IsMinimalNumberFormat())
            return OG2::BuildValueExpr(bind, "TO_NUMBER", std::string());

        // Any real picture has to be named, with the decimal and group characters the display used,
        // because the session's own NLS_NUMBER_FORMAT is a different picture. Both characters came from
        // the session's NLS_NUMERIC_CHARACTERS, which is where fmt got them.
        return OG2::BuildValueExpr(bind, "TO_NUMBER", fmt.NumberFormat(),
            std::string("NLS_NUMERIC_CHARACTERS='") + fmt.DecimalChar() + fmt.GroupChar() + "'");

    // Rendered by %g in the session's decimal character, which is what these two read back.
    case Common::SqlValueKind::BinaryFloat:
        return OG2::BuildValueExpr(bind, "TO_BINARY_FLOAT", std::string());
    case Common::SqlValueKind::BinaryDouble:
        return OG2::BuildValueExpr(bind, "TO_BINARY_DOUBLE", std::string());

    // Neither takes a format model, so wrapping is only right while the picture is the empty default,
    // which renders Oracle's own canonical text: +1-06 and +1 12:00:00.000000000. A custom picture
    // renders text no function can read back, and a bare bind of it is ORA-01459 - there is no third
    // option, so the canonical case is the one worth getting right.
    case Common::SqlValueKind::IntervalYm:
        return fmt.IntervalYmFormat().empty()
             ? OG2::BuildValueExpr(bind, "TO_YMINTERVAL", std::string()) : std::string(bind);
    case Common::SqlValueKind::IntervalDs:
        return fmt.IntervalDsFormat().empty()
             ? OG2::BuildValueExpr(bind, "TO_DSINTERVAL", std::string()) : std::string(bind);

    // Text binds bare, and so does a RAW. A RAW could be wrapped in HEXTORAW, but its hex dump is capped
    // by GridBlobHexRowLength, so the write-back could store a truncated value without saying so - and a
    // guaranteed ORA-01459 is the better of those two outcomes.
    default:
        return bind;
    }
}

void OciGridSource::OnGridSettingsChanged ()
{
    ApplyGridFormatContext();

    // The NULL marker is parked on the shared Null payload rather than rendered through m_format, so
    // it has to be re-parked for the new representation. One small leaked store per change.
    SetNullText(GetSQLToolsSettings().GetGridNullRepresentation());

    // Lazy rendering re-reads each cell through the new context on demand, so a repaint notification
    // is the whole invalidation - no per-cell textOffset clearing over a 20 M-row result set.
    const int nrows = GetCount(IsTableOrientation() ? edVert : edHorz);
    if (0 < nrows)
        Notify_ChangedRows(0, nrows - 1);
}

    // One chunk of a grid fetch, and the only task here that talks to the server.
    struct BackgroundTask_GridFetch : Task
    {
        enum
        {
            MAX_FETCH_ROWS    = 1000,   // rows per chunk; the old MAX_FETCH_PORTION
            MAX_FETCH_SECONDS = 3,      // or seconds, so one slow chunk still shows something
            SAFE_SIZE_MB      = 500     // the grid's real-memory ceiling, checked on the UI thread
        };

        std::shared_ptr<OciGridFetchControl> m_control;

        std::shared_ptr<OCI8::AutoCursor> m_cursor;

        OciGridView* m_view;

        int    m_rowsToFetch;    // this chunk's budget, in rows
        int    m_rowsFetched;    // rows the buffers below actually hold
        bool   m_allRowsFetched; // the cursor is exhausted, or nothing could ever be fetched
        bool   m_stopped;        // told to stop: discard the buffers and end the chain
        bool   m_userCancelled;  // told to stop by the user: KEEP the buffers and end the chain
        bool   m_closeCursor;    // the size limit stopped us; the cursor still needs closing
        double m_lastFetchTime;

        Common::SqlArena                         m_scratch;
        std::vector<OG2::ValueTable::CellOffset> m_offsets;
        std::vector<std::string>                 m_text;   // filled only on the eager fallback path
        bool                                     m_lazyRender;

        BackgroundTask_GridFetch (OciGridView& view,
                                  const std::shared_ptr<OciGridFetchControl>& control,
                                  const std::shared_ptr<OCI8::AutoCursor>& cursor,
                                  int rowsToFetch,
                                  bool lazyRender)
            : Task("Fetch", 0)
            , m_control(control)
            , m_cursor(cursor)
            , m_view(&view)
            , m_rowsToFetch(rowsToFetch)
            , m_rowsFetched(0)
            , m_allRowsFetched(false)
            , m_stopped(false)
            , m_userCancelled(false)
            , m_closeCursor(false)
            , m_lastFetchTime(0.0)
            , m_lazyRender(lazyRender)
        {
            SetSilent(true);
        }

        static size_t ScratchReserve ()
        {
        #ifdef _WIN64
            return static_cast<size_t>(64) * 1024 * 1024;
        #else
            return static_cast<size_t>(32) * 1024 * 1024;
        #endif
        }

        bool Stopped () const
        {
            return 0 != m_control->m_stop || !::IsWindow(m_control->m_hWnd);
        }

        bool Cancelled () const
        {
            return 0 != m_control->m_cancelled;
        }

        void DoInBackground (OciConnect& connect)
        {
            try { EXCEPTION_FRAME;

                if (!connect.IsOpen() || !m_cursor.get() || !m_cursor->IsOpen())
                {
                    m_allRowsFetched = true;
                }
                else if (Stopped())
                {
                    m_stopped = true;
                    AbandonCursor();
                }
                else if (Cancelled())
                {
                    m_userCancelled = true;
                    m_allRowsFetched = true;
                    AbandonCursor();
                }
                else
                {
                    RaiseActivePrimeExecution(m_control);

                    FetchChunk();

                    if (m_userCancelled)
                        AbandonCursor();
                    else if (m_allRowsFetched)
                        m_cursor->Close();
                    else if (m_stopped || !::IsWindow(m_control->m_hWnd))
                        AbandonCursor();
                }
            }
            catch (const OciException& x)
            {
                m_allRowsFetched = true;
                AbandonCursor();

                if (x == 24374)
                    THROW_APP_EXCEPTION("The result of the query is not shown"
                        "\nbecause all columns types are currently not supported.");
                else if (x == 1013)
                    return;      // the user broke the fetch: keep what was fetched, say it is over
                else
                    throw;
            }
            catch (const std::exception&)
            {
                m_allRowsFetched = true;
                AbandonCursor();
                throw;
            }
            catch (...)
            {
                m_allRowsFetched = true;
                AbandonCursor();
                throw;
            }
        }

        void AbandonCursor ()
        {
            try { if (m_cursor.get()) m_cursor->Close(true); } catch (...) {}
            m_cursor.reset();
        }

        void FetchChunk ()
        {
            if (!m_scratch.Reserve(ScratchReserve()))
                THROW_APP_EXCEPTION("Out of memory: cannot allocate a fetch buffer.");

            const int cols = m_cursor->GetFieldCount();

            const size_t budget = m_scratch.Reserved() / 2;

            m_offsets.reserve(static_cast<size_t>(cols) * MAX_FETCH_ROWS);
            if (!m_lazyRender)
                m_text.reserve(static_cast<size_t>(cols) * MAX_FETCH_ROWS);

            clock64_t t0 = clock64();

            for (int i = 0; i < m_rowsToFetch; ++i)
            {
                if (Stopped())
                {
                    m_stopped = true;
                    break;
                }

                if (Cancelled())
                {
                    m_userCancelled = true;
                    m_allRowsFetched = true;
                    break;
                }

                if (!m_cursor->Fetch())
                {
                    m_allRowsFetched = true;
                    break;
                }

                const size_t from = m_offsets.size();
                m_offsets.resize(from + cols);
                if (!m_lazyRender)
                    m_text.resize(from + cols);

                for (int j = 0; j < cols; ++j)
                {
                    m_offsets.at(from + j) = m_cursor->GetValue(j, m_scratch);

                    if (!m_lazyRender)
                        m_cursor->GetString(j, m_text.at(from + j));
                }

                ++m_rowsFetched;

                if (MAX_FETCH_ROWS <= m_rowsFetched
                || budget < m_scratch.Used()
                || double(clock64() - t0)/CLOCKS_PER_SEC > MAX_FETCH_SECONDS)
                    break;
            }

            m_lastFetchTime = double(clock64() - t0)/CLOCKS_PER_SEC;
        }

        void ReturnInForeground ()
        {
            Publish(false);
        }

        void ReturnErrorInForeground ()
        {
            Publish(true);
        }

        void Publish (bool error)
        {
            OciGridSource* const source = m_control->m_source;
            const bool wanted = source && source->ReleaseFetchSlot(m_control);

            if (!wanted)
                return;

            // Lowers the busy note on any exit from Publish(), including an exception.
            struct EndChainOnExit
            {
                const std::shared_ptr<OciGridFetchControl>& m_control;
                bool m_live;

                EndChainOnExit (const std::shared_ptr<OciGridFetchControl>& control)
                    : m_control(control), m_live(true) {}

                ~EndChainOnExit ()
                {
                    if (m_live)
                        LowerActivePrimeExecution(m_control, true/*nothrow*/);
                }

                void Disarm () { m_live = false; }
            } endChain(m_control);

            if (m_stopped || !::IsWindow(m_control->m_hWnd))
                return;

            const int rowsBefore = source->GetCount(edVert);

            source->AppendFetched(m_text, m_offsets, m_scratch, m_allRowsFetched, m_lastFetchTime,
                                  m_control->m_ignoreSizeLimit, m_closeCursor);

            if (m_closeCursor)
            {
                m_allRowsFetched = true;

                TaskPtr close(new BackgroundTask_CloseCursor);
                static_cast<BackgroundTask_CloseCursor*>(close.get())->m_cursor = m_cursor;
                FrgdRequestQueue::Get().Push(close);
            }

            if (source->IsTableOrientation())
                m_view->AutofitColumns(-1, rowsBefore);
            // Once per result set, never per block; a short result completes before the sample.
            else if (!m_view->IsFormFitted()
                 && (m_allRowsFetched || source->GetCount(edHorz) >= OciGridView::kFormFitSample))
                m_view->AutofitColumns();

            const int remaining = m_control->m_fetchAll ? INT_MAX : m_rowsToFetch - m_rowsFetched;

            if (!error && !m_allRowsFetched && m_cursor.get() && 0 < remaining)
            {
                source->m_fetchControl = m_control;

                endChain.Disarm();

                FrgdRequestQueue::Get().Push(TaskPtr(new BackgroundTask_GridFetch(
                    *m_view, m_control, m_cursor, remaining, m_lazyRender)));
            }
        }
    };

void OciGridSource::Fetch (OciGridView& view, int rowsToFetch)
{
    if (m_allRowsFetched)
        return;

    if (FetchIsActive())
    {
        if (INT_MAX == rowsToFetch)
            m_fetchControl->m_fetchAll = true;
        return;
    }

    if (theApp.GetActivePrimeExecution())
    {
        AfxMessageBox(L"The connection is busy.\n\nPlease try again later.", MB_OK|MB_ICONASTERISK);
        AfxThrowUserException();
    }

    std::shared_ptr<OciGridFetchControl> control(new OciGridFetchControl(this, view.GetSafeHwnd()));
    control->m_fetchAll = (INT_MAX == rowsToFetch);
    m_fetchControl = control;

    FrgdRequestQueue::Get().Push(TaskPtr(
        new BackgroundTask_GridFetch(view, control, m_cursor, rowsToFetch, Table().LazyRender())));
}

void OciGridSource::FetchAll () // will wait until it is done
{
    if (!m_allRowsFetched)
    {
        OciGridSource* This = const_cast<OciGridSource*>(this);
        if (GridManager* manager = dynamic_cast<GridManager*>(*m_GridManagers.begin()))
            if (OciGridView* view = dynamic_cast<OciGridView*>(manager->m_pClientWnd))
            {
                This->Fetch(*view, INT_MAX);

                FetchWaitDlg dlg(AfxGetMainWnd(), This->m_allRowsFetched);
                dlg.m_text = "Fetching data, please wait...";

                dlg.DoModal();

                if (dlg.WasCancelled())
                {
                    // Ahead of CancelActiveFetch(): DoCancel() needs GetActivePrimeExecution() still up.
                    theApp.DoCancel(false/*silent*/);

                    This->CancelActiveFetch();

                    AfxThrowUserException();
                }
            }
    }
}

void OciGridSource::AppendFetched (const std::vector<std::string>& text,
                                   std::vector<OG2::ValueTable::CellOffset>& offsets,
                                   const Common::SqlArena& scratch,
                                   bool allRowsFetched,
                                   double lastFetchTime,
                                   bool& ignoreSizeLimit,
                                   bool& closeCursor)
{
    // Stated on what the worker delivered, not on a flag read later: the worker snapshotted the lazy
    // flag when its chunk was queued, so only its output says which of the two shapes arrived.
    const bool useText = !text.empty();

    ASSERT(!useText || offsets.size() == text.size());
    ASSERT(useText || offsets.empty() || Table().LazyRender());

    m_fetchTime += lastFetchTime;

    if (allRowsFetched)
        m_allRowsFetched = allRowsFetched;

    if (!offsets.empty())
    {
        CWaitCursor wait;

        OciGridSource::NotificationDisabler disabler(this);

        int nrows = GetCount(IsTableOrientation() ? edVert : edHorz);
        int ncols = GetCount(IsTableOrientation() ? edHorz : edVert);

        if (0 >= ncols || (useText && offsets.size() != text.size()) || 0 != offsets.size() % ncols)
        {
            ASSERT(FALSE);
            m_allRowsFetched = true;    // and no further chunk onto a table this cannot vouch for
            return;
        }

        const int rowsFetched = static_cast<int>(offsets.size() / ncols);
        const size_t bytesBefore = Table().BytesAllocated();

        ReserveMore(rowsFetched);

        for (int i = 0; i < rowsFetched; ++i)
        {
            const int row = Append();

            for (int col = 0; col < ncols; ++col)
            {
                const size_t idx = static_cast<size_t>(i) * ncols + col;

                OG2::ValueTable::CellOffset off = Table().AdoptCell(col, scratch, offsets.at(idx));
                if (Common::SqlArena::NullOffset == off)
                    off = Table().NullPayload();

                SetCell(row, col, off);
                if (useText)
                    SetText(row, col, text.at(idx));
            }
        }

        m_bytesAllocated = Table().BytesAllocated();
        size_t size = m_bytesAllocated - bytesBefore;

        if (!ignoreSizeLimit
        && m_bytesAllocated > static_cast<size_t>(BackgroundTask_GridFetch::SAFE_SIZE_MB) * 1024 * 1024)
        {
            if (AfxMessageBox(L"The size of fetched data reached the safe limit 500 Mb!"
                "\n\nPlease choose one of the following options: "
                "\n      press <Yes> to stop fetching and close the cursor, "
                "\n      press <No> to continue at your own risk",
                MB_YESNO|MB_ICONEXCLAMATION) == IDYES)
            {
                m_allRowsFetched = true;
                closeCursor = true;
            }
            else
                ignoreSizeLimit = true;
        }

        disabler.Enable();

        if (IsTableOrientation())
            Notify_ChangedRowsQuantity(nrows, GetCount(edVert) - nrows); // indirectly calls autofit
        else
            Notify_ChangedColsQuantity(nrows, GetCount(edHorz) - nrows);

        std::ostringstream o;

        o << "Fetch #" << ++m_fetchPortions << ", packet " << rowsFetched << " row(s)";

        if (m_fetchPortions > 1)
        {
            o << ", size = ";

            if (size < 1024)
                o << size << "bytes";
            else if (size / 1024 < 1024)
                o << size / 1024 << "Kb";
            else
                o << size / 1024 / 1024 << "Mb";

            o << ", total size = ";

            if (m_bytesAllocated < 1024)
                o << m_bytesAllocated << "bytes";
            else if (m_bytesAllocated / 1024 < 1024)
                o << m_bytesAllocated / 1024 << "Kb.";
            else
                o << m_bytesAllocated / 1024 / 1024 << "Mb.";
        }

        o << ", exec " << Common::print_time(m_cursor->GetExecutionTime());
        o << ", fetch " << Common::print_time(m_fetchTime);
        o << ", total " << Common::print_time(m_cursor->GetExecutionTime() + m_fetchTime);

        Global::SetStatusText(o.str().c_str());
    }
}

int OciGridSource::ExportText (std::ostream& of, int row, int nrows, int col, int ncols, int format) const
{
    try
    {
        if (!m_allRowsFetched
        && ((row == -1 && IsTableOrientation()) || (col == -1 && !IsTableOrientation())))
        {
            OciGridSource* This = const_cast<OciGridSource*>(this);
            This->FetchAll();
        }
        return IGridTextSource::ExportText(of, row, nrows, col, ncols, format);
    }
    catch (const OciException& x)
    {
        MessageBeep(MB_OK|MB_ICONHAND);
        AfxMessageBox(x);
    }

    return etfPlainText;
}

void OciGridSource::GetCellStrForExport (string& str,  int line, int col, int subformat) const
{
    IGridTextSource::GetCellStrForExport(str, line, col, subformat);

    const int r = IsTableOrientation() ? line : col;
    const int c = IsTableOrientation() ? col  : line;

    if (subformat > 0)
    {
        const unsigned literalOptions = GetSQLToolsSettings().GetGridExpAnsiLiterals()
                                      ? Common::evlAnsiTemporal : Common::evlNone;
        str = Table().GetValue(r, c).ToSqlLiteral(literalOptions);
        return;
    }

    if (Table().IsNullCell(r, c))
        str = m_emptyString;
}

bool OciGridSource::IsTextField (int col) const
{
    const Common::SqlValueKind kind = GetColKind(col);

    return !IsNumericKind(kind) && !IsDatetimeKind(kind);
}

void OciGridSource::CopyColumnNames (std::string& buff, int from, int to) const
{

    if (!m_headersInLowerCase)
    {
        int nCols = m_ColumnInfo.size();
        for (int i = from; i < nCols && i <= to; i++)
        {
            buff += SQLUtilities::GetSafeDatabaseObjectName(GetColHeader(i));
            if (i != nCols - 1 && i != to)
                buff += ", ";
        }
    }
    else
        __super::CopyColumnNames(buff, from, to);
}

void OciGridSource::ApplyRowIdEditState ()
{
    // The ROWID column then the single table, each a refusal rather than a fallback: either answer
    // alone leaves the grid read-only.
    m_rowIdColumn = OG2::DetectRowIdColumn(m_rawHeaders);
    m_editTable   = OG2::ResolveSingleTable(m_editSql);
}

bool OciGridSource::CanModify () const
{
    // And no write while a fetch is still filling the table a write would index into.
    return m_rowIdColumn >= 0 && !m_editTable.empty() && !FetchIsActive();
}

bool OciGridSource::IsCellEditable (int row, int col) const
{
    if (!CanModify())
        return false;

    // Grid space to the table's own column: the swap every other accessor here performs, and getting
    // it backwards would offer editing on the wrong column in form orientation.
    const int c = IsTableOrientation() ? col : row;

    if (c < 0 || c >= static_cast<int>(m_rawHeaders.size()))
        return false;

    // The ROWID column is the address, not data. IsUpdatableColumnName refuses a header that is not one
    // bare identifier, so `UPPER(x)` and `t.COL` fail; a named alias passes and the server rules on it.
    return c != m_rowIdColumn && OG2::IsUpdatableColumnName(m_rawHeaders.at(c));
}

void OciGridSource::CommitCellEdit (int row, int col, const std::string& utf8)
{
    if (!IsCellEditable(row, col))
        return;

    const int tr = IsTableOrientation() ? row : col;
    const int tc = IsTableOrientation() ? col : row;

    // A staged row is filled in locally and posted as one INSERT, so an edit here records and shows
    // the text and stops. It carries no ROWID yet, which the write path below would refuse on.
    if (m_pendingRow >= 0 && m_pendingRow == Table().ToPhysical(tr))
    {
        // Refused rather than staged while a write is in flight: the values were copied into that
        // task when it was queued, so anything typed now would be shown and never sent.
        if (m_editControl.get())
        {
            AfxMessageBox(L"The previous change has not come back from the server yet.\n\n"
                          L"Please try again in a moment.", MB_OK|MB_ICONASTERISK);
            return;
        }

        if (tc >= 0 && tc < static_cast<int>(m_pendingValues.size()))
            m_pendingValues[tc] = utf8;

        // OverwriteText, not SetText: a staged cell starts out sharing the NULL payload, on which
        // SetText parks nothing. Logical == physical here, BeginPendingRow having dropped the sort.
        Table().OverwriteText(m_pendingRow, tc, utf8);

        // Inclusive range, not (row, count); Notify_ChangedRowsQuantity is the count-based sibling.
        Notify_ChangedRows(m_pendingRow, m_pendingRow);

        return;
    }

    std::string rowId;
    Table().GetText(tr, m_rowIdColumn, rowId);

    if (rowId.empty())
    {
        AfxMessageBox(L"This row carries no ROWID to update by.\n\nPlease re-run the query.",
                      MB_OK|MB_ICONHAND);
        return;
    }

    const std::string sql = OG2::BuildUpdateSql(m_editTable, m_rawHeaders.at(tc), true,
                                                ValueExprFor(tc, ":newval"));
    // Built from the same table as the write, so lock and UPDATE cannot disagree which row is meant.
    const std::string lockSql = OG2::BuildLockSql(m_editTable);
    if (sql.empty() || lockSql.empty())
        return;

    // One write in flight per grid. A second would take over the slot the first publishes through,
    // so the first UPDATE would land on the server while the grid went on showing the old value.
    if (m_editControl.get())
    {
        AfxMessageBox(L"The previous change has not come back from the server yet.\n\n"
                      L"Please try again in a moment.", MB_OK|MB_ICONASTERISK);
        return;
    }

    if (theApp.GetActivePrimeExecution())
    {
        AfxMessageBox(L"The connection is busy.\n\nPlease try again later.", MB_OK|MB_ICONASTERISK);
        return;
    }

    HWND hWnd = 0;
    if (!m_GridManagers.empty())
        if (GridManager* manager = dynamic_cast<GridManager*>(*m_GridManagers.begin()))
            if (manager->m_pClientWnd)
                hWnd = manager->m_pClientWnd->GetSafeHwnd();

    // Physical, because it stays put across the write; publish translates it back to a logical row.
    const int physicalRow = Table().ToPhysical(tr);

    std::shared_ptr<OciGridEditControl> control(new OciGridEditControl(this, hWnd));
    m_editControl = control;

    FrgdRequestQueue::Get().Push(TaskPtr(new BackgroundTask_GridUpdate(
        control, sql, lockSql, utf8, rowId, physicalRow, tc, m_rowIdColumn)));
}

bool OciGridSource::CanDeleteRow () const
{
    return CanModify() && IsTableOrientation();
}

void OciGridSource::CommitRowDelete (int row)
{
    if (!CanDeleteRow())
        return;

    std::string rowId;
    Table().GetText(row, m_rowIdColumn, rowId);

    if (rowId.empty())
    {
        AfxMessageBox(L"This row carries no ROWID to delete by.\n\nPlease re-run the query.",
                      MB_OK|MB_ICONHAND);
        return;
    }

    const std::string sql = OG2::BuildDeleteSql(m_editTable);
    // Built from the same table as the write, so lock and DELETE cannot disagree which row is meant.
    const std::string lockSql = OG2::BuildLockSql(m_editTable);

    if (sql.empty() || lockSql.empty())
        return;

    // One write in flight per grid, shared with the cell update: a delete taking over the slot would
    // leave the UPDATE published into nothing while the grid went on showing the old value.
    if (m_editControl.get())
    {
        AfxMessageBox(L"The previous change has not come back from the server yet.\n\n"
                      L"Please try again in a moment.", MB_OK|MB_ICONASTERISK);
        return;
    }

    if (theApp.GetActivePrimeExecution())
    {
        AfxMessageBox(L"The connection is busy.\n\nPlease try again later.", MB_OK|MB_ICONASTERISK);
        return;
    }

    HWND hWnd = 0;
    if (!m_GridManagers.empty())
        if (GridManager* manager = dynamic_cast<GridManager*>(*m_GridManagers.begin()))
            if (manager->m_pClientWnd)
                hWnd = manager->m_pClientWnd->GetSafeHwnd();

    // Translated here, on the UI thread and before the sort can change again. A row removed between
    // queueing and publishing is caught by the ROWID comparison below, not by this index.
    const int physicalRow = Table().ToPhysical(row);

    std::shared_ptr<OciGridEditControl> control(new OciGridEditControl(this, hWnd));
    m_editControl = control;

    FrgdRequestQueue::Get().Push(TaskPtr(new BackgroundTask_GridDelete(
        control, sql, lockSql, rowId, physicalRow, m_rowIdColumn)));
}

bool OciGridSource::CanInsertRow () const
{
    // CanDeleteRow's orientation rule, since the row is named by the same vertical ruler, plus one
    // of its own: a second staged row would share the first's values and post the wrong thing.
    return CanDeleteRow() && m_pendingRow < 0;
}

int OciGridSource::BeginPendingRow (int copyFrom)
{
    if (!CanInsertRow())
        return -1;

    // Indexed by table column, as m_pendingValues is. Read ahead of ClearSort: the permutation is
    // what resolves copyFrom, and once it is dropped that row could be anywhere.
    std::vector<std::string> seed(m_rawHeaders.size());

    if (copyFrom >= 0 && copyFrom < Table().GetRowCount())
    {
        const int cols = std::min(Table().GetColumnCount(), static_cast<int>(seed.size()));

        for (int tc = 0; tc < cols; ++tc)
        {
            // The address comes from the server, so a copied one would show the new row wearing the
            // old row's ROWID until the post replaced it.
            if (tc == m_rowIdColumn)
                continue;

            // SelectInsertColumns drops these at post time, so seeding one would show a value the
            // INSERT never names - a duplicate that looks complete and posts short.
            if (!OG2::IsUpdatableColumnName(m_rawHeaders[tc]))
                continue;

            // A NULL stays unfilled. GetText would answer the grid's null marker, which is display
            // text and would go in literally; blank posts as the column's own DEFAULT.
            if (Table().IsNullCell(copyFrom, tc))
                continue;

            Table().GetText(copyFrom, tc, seed[tc]);
        }
    }

    // Dropped before the append, not after: Append() extends a permutation of the fetched rows, and
    // the staged index has to mean the same thing to the post, the discard and every paint between.
    ClearSort();

    // Sized to the headers, so an edit to any column has a slot. Blank is "not filled in", which
    // SelectInsertColumns reads as "leave this column to its DEFAULT".
    m_pendingValues = seed;

    const int row = Append();     // notifies, so the ruler grows and the row paints at once
    if (row < 0)
    {
        ClearPendingState();
        return -1;
    }

    m_pendingRow = Table().ToPhysical(row);

    // Shown as well as staged, as CommitCellEdit does for a typed cell: the grid paints from the
    // table, so a value held only in m_pendingValues would post without ever appearing.
    for (size_t tc = 0; tc < seed.size(); ++tc)
        if (!seed[tc].empty())
            Table().OverwriteText(m_pendingRow, static_cast<int>(tc), seed[tc]);

    Notify_ChangedRows(m_pendingRow, m_pendingRow);

    return row;
}

void OciGridSource::CommitRowInsert ()
{
    // FetchIsActive for the reason CanModify gives: no write while a fetch is still filling the
    // table the write would index into. A fetch can start after the row was staged, by scrolling.
    if (!HasPendingRow() || !IsTableOrientation() || FetchIsActive())
        return;

    // The row could have been taken by a Clear Records between staging and posting.
    if (m_pendingRow >= Table().GetRowCount())
    {
        ClearPendingState();
        return;
    }

    const std::vector<int> cols =
        OG2::SelectInsertColumns(m_rawHeaders, m_rowIdColumn, m_pendingValues);

    if (cols.empty())
    {
        AfxMessageBox(L"There is nothing to insert yet.\n\n"
                      L"Fill in at least one cell, then press Ctrl+Enter again.\n"
                      L"Press Esc to throw the row away.", MB_OK|MB_ICONASTERISK);
        return;
    }

    std::vector<std::string> names;
    std::vector<std::string> values;
    std::vector<std::string> exprs;
    names.reserve(cols.size());
    values.reserve(cols.size());
    exprs.reserve(cols.size());

    // Zipped in the order SelectInsertColumns answered, which is the order the binds are named in.
    for (size_t i = 0; i < cols.size(); ++i)
    {
        names.push_back(m_rawHeaders.at(cols[i]));
        values.push_back(m_pendingValues.at(cols[i]));

        // Per column, because the staged text is in whatever picture that column was displayed in -
        // the same mismatch the single-cell UPDATE guards against in ValueExprFor.
        exprs.push_back(ValueExprFor(cols[i], (":v" + std::to_string(i + 1)).c_str()));
    }

    const std::string sql = OG2::BuildInsertSql(m_editTable, names, exprs);
    if (sql.empty())
        return;

    // One write in flight per grid, shared with the update and the delete: an insert taking over
    // the slot would leave the earlier write published into nothing.
    if (m_editControl.get())
    {
        AfxMessageBox(L"The previous change has not come back from the server yet.\n\n"
                      L"Please try again in a moment.", MB_OK|MB_ICONASTERISK);
        return;
    }

    if (theApp.GetActivePrimeExecution())
    {
        AfxMessageBox(L"The connection is busy.\n\nPlease try again later.", MB_OK|MB_ICONASTERISK);
        return;
    }

    HWND hWnd = 0;
    if (!m_GridManagers.empty())
        if (GridManager* manager = dynamic_cast<GridManager*>(*m_GridManagers.begin()))
            if (manager->m_pClientWnd)
                hWnd = manager->m_pClientWnd->GetSafeHwnd();

    const int physicalRow = m_pendingRow;

    std::shared_ptr<OciGridEditControl> control(new OciGridEditControl(this, hWnd));
    m_editControl = control;

    FrgdRequestQueue::Get().Push(TaskPtr(new BackgroundTask_GridInsert(
        control, sql, values, physicalRow, m_rowIdColumn)));
}

void OciGridSource::DiscardPendingRow ()
{
    if (!HasPendingRow())
        return;

    // Never while the INSERT is in flight: the row would go from under the task about to publish
    // into it, leaving the server holding a row the grid no longer shows.
    if (m_editControl.get())
    {
        AfxMessageBox(L"The insert has not come back from the server yet.\n\n"
                      L"Please try again in a moment.", MB_OK|MB_ICONASTERISK);
        return;
    }

    const int row = m_pendingRow;
    ClearPendingState();

    // Delete() and not Table().Delete(): the notification is what shrinks the ruler and clamps the
    // current row, without which the grid would keep painting over a row that is no longer there.
    if (row < Table().GetRowCount())
        Delete(row);
}

bool OciGridSource::IsEditingPendingRow () const
{
    if (m_pendingRow < 0 || m_editRow < 0 || m_editCol < 0)
        return false;

    // CommitCellEdit's own translation, so the two never disagree about which row is the staged one.
    const int tr = IsTableOrientation() ? m_editRow : m_editCol;
    return m_pendingRow == Table().ToPhysical(tr);
}

bool OciGridSource::IsPendingRow (int row) const
{
    return m_pendingRow >= 0 && IsTableOrientation() && m_pendingRow == Table().ToPhysical(row);
}

void OciGridSource::ClearPendingState ()
{
    m_pendingRow = -1;
    m_pendingValues.clear();
}

void OciGridSource::DeleteAll ()
{
    // First, because the base takes every row with it and the index would then name nothing.
    ClearPendingState();
    ValueGridSource::DeleteAll();
}

void OciGridSource::PublishCellUpdate (int physicalRow, int tableCol, int rowIdCol,
                                       const std::string& addressedBy,
                                       const std::string& newValue, const std::string& newRowId)
{
    // Order kept, glyph dropped: restoring fetch order would move the edited row off the cursor.
    MarkSortStale();

    const int row = Table().ToLogical(physicalRow);   // accessors take a logical row
    if (row < 0)
        return;

    // Guarded on the ROWID, not the index: rows inserted or deleted between commit and publish move
    // physical positions, and a stale one would write a row the server never touched.
    std::string rowIdNow;
    Table().GetText(row, rowIdCol, rowIdNow);
    if (rowIdNow != addressedBy)
        return;

    // A failed store would leave the grid showing a value the server no longer has - the divergence
    // the rowcount check exists to prevent - so it is reported rather than left to look like success.
    const bool valueWritten = Table().OverwriteText(row, tableCol, newValue);

    // The refreshed address, which is mandatory rather than tidy: a partition move changes the
    // ROWID, and keeping the old text would make the next edit address nothing and be refused.
    const bool rowIdWritten = newRowId.empty()
        || Table().OverwriteText(row, rowIdCol, newRowId);

    Notify_ChangedRows(row, row);

    if (!valueWritten || !rowIdWritten)
        AfxMessageBox(L"The change was applied to the database, but the grid could not be updated\n"
                      L"because it is out of memory.\n\nPlease re-run the query.",
                      MB_OK|MB_ICONEXCLAMATION);
}

void OciGridSource::PublishRowDelete (int physicalRow, int rowIdCol, const std::string& addressedBy)
{
    // A delete leaves the sort standing, so physicalRow is translated instead of being used as a
    // logical index. ToLogical as PublishCellUpdate does; -1 is a row the permutation never held.
    const int row = Table().ToLogical(physicalRow);
    if (row < 0 || row >= Table().GetRowCount())
        return;

    // The guard PublishCellUpdate applies: rows added or removed between commit and publish move
    // physical positions, and a stale one would drop a row the server never touched.
    std::string rowIdNow;
    Table().GetText(row, rowIdCol, rowIdNow);
    if (rowIdNow != addressedBy)
        return;

    // Delete() and not Table().Delete(): the notification is what shrinks the ruler and clamps the
    // current row, and without it the grid would keep painting over a row that is no longer there.
    Delete(row);
}

void OciGridSource::PublishRowInsert (int physicalRow, int rowIdCol, const std::string& newRowId)
{
    // Nothing to publish for a row the user threw away, or one a Clear Records took, while the
    // INSERT was in flight. ReleaseEditSlot has already answered for SetCursor replacing the set.
    if (m_pendingRow != physicalRow)
    {
        ClearPendingState();
        return;
    }

    if (physicalRow < 0 || physicalRow >= Table().GetRowCount() || newRowId.empty())
    {
        ClearPendingState();
        return;
    }

    // The row's address, in the column every later UPDATE and DELETE reads, which makes it an
    // ordinary editable row - a refetch of the grid's query need not return it at all.
    Table().OverwriteText(physicalRow, rowIdCol, newRowId);

    ClearPendingState();

    // Inclusive range, exactly as PublishCellUpdate passes it above.
    Notify_ChangedRows(physicalRow, physicalRow);
}

