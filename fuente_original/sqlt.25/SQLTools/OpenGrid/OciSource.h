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
// 16.12.2004 (Ken Clubok) Add IsStringField method, to support CSV prefix option
// 03.01.2005 (ak) Rename IsStringField to IsTextField

#ifndef __OCISOURCE_H__
#define __OCISOURCE_H__

#include <memory>
#include <OCI8/ACursor.h>
#include "GridSourceBase.h"
#include "Threading/TaskQueue.h"

    class OciGridView;
    class OciGridSource;

// Fetch state shared between the UI thread and the one fetch worker that owns it.
struct OciGridFetchControl
{
    OciGridFetchControl (OciGridSource* source, HWND hWnd)
        : m_source(source)
        , m_hWnd(hWnd)
        , m_stop(0)
        , m_cancelled(0)
        , m_busyRaised(0)
        , m_fetchAll(false)
        , m_ignoreSizeLimit(false)
    {
    }

    OciGridSource*     m_source;  // UI thread only. 0 from ~OciGridSource() on: the source is gone.
    HWND               m_hWnd;    // the grid this fetch fills, captured when the task was queued
    volatile LONG      m_stop;    // UI thread sets, worker reads per row: stop and discard

    volatile LONG      m_cancelled;

    volatile LONG      m_busyRaised;

    bool               m_fetchAll;
    bool               m_ignoreSizeLimit;
};

// Same shape as the fetch control and for the same reason: a write-back task outlives the call that
// queued it, so the source it would publish to is reached through a slot the source can empty.
struct OciGridEditControl
{
    OciGridEditControl (OciGridSource* source, HWND hWnd)
        : m_source(source)
        , m_hWnd(hWnd)
    {
    }

    OciGridSource*     m_source;  // UI thread only. 0 from ~OciGridSource() on: the source is gone.
    HWND               m_hWnd;    // the grid this write-back repaints, captured when queued
};

// Grid source over a live OCI cursor; every fetch runs on the background queue.
class OciGridSource : public OG2::ValueGridSource
{
    friend struct BackgroundTask_SetCursor;
    friend struct BackgroundTask_GridFetch;
    friend struct BackgroundTask_GridUpdate;
    friend struct BackgroundTask_GridDelete;
    friend struct BackgroundTask_GridInsert;

public:
    OciGridSource ();
    ~OciGridSource ();

    void SetCursor (OciGridView&, std::unique_ptr<OCI8::AutoCursor>& cursor);

    virtual int  ExportText (std::ostream&, int row, int nrows, int col, int ncols, int format) const;
    virtual void GetCellStrForExport (std::string&, int line, int col, int subformat) const;

    virtual bool CanExportSqlLiterals () const { return true; }
    bool IsTextField (int col) const; 

    void Fetch (OciGridView&, int rowsToFetch);
    void FetchAll (); // will wait until it is done

    // Adopts the worker's typed payloads and, on the eager fallback path only, the display text it
    // built. On the lazy path `text` stays empty and GetText renders each cell on demand.
    void AppendFetched (const std::vector<std::string>& text,
                        std::vector<OG2::ValueTable::CellOffset>& offsets,
                        const Common::SqlArena& scratch,
                        bool allRowsFetched, double lastFetchTime,
                        bool& ignoreSizeLimit, bool& closeCursor);

    // Loads the grid NLS/format context and turns lazy rendering on. Must run before
    // the first Fetch(), which snapshots that flag into the worker it creates. Idempotent.
    void ApplyGridFormatContext ();

    // Re-applies the format context and repaints every fetched row. Cheap on the lazy path, where
    // GetText re-renders each cell, so no per-cell text invalidation is needed.
    void OnGridSettingsChanged ();

    bool AllRowsFetched () const { return m_allRowsFetched; }
    bool FetchIsActive  () const { return 0 != m_fetchControl.get(); }

    // Single-cell UPDATE addressed by ROWID. All-or-nothing: a query the single-table test refuses
    // is read-only, never partly editable.
    virtual bool CanModify () const;

    // Grid-space pair, as BeginEdit hands it. Refuses the ROWID column itself and any header that is
    // not one bare identifier; a named alias passes this test and is caught by the server instead.
    virtual bool IsCellEditable (int row, int col) const;

    // Queues the UPDATE on the background queue and returns at once, so the cell keeps showing the
    // old value until the task publishes. Every OCI call in this app runs off the UI thread.
    virtual void CommitCellEdit (int row, int col, const std::string& utf8);

    // Table orientation only: rotated, the record axis is horizontal, so the manager's current
    // vertical position would name a field rather than a record.
    virtual bool CanDeleteRow () const;

    // Queues the DELETE and returns at once. The row leaves the grid in PublishRowDelete and never
    // here, because a rowcount other than exactly one is an error rather than a success.
    virtual void CommitRowDelete (int row);

    // Row INSERT staged in the grid. CanInsertRow adds one rule to CanDeleteRow's: a second staged
    // row would share the first's values, so there is never more than one.
    virtual bool CanInsertRow () const;

    virtual bool HasPendingRow () const { return m_pendingRow >= 0; }

    // Appends the staged row and answers its index, or -1. Drops the sort first, so that index is
    // logical and physical at once and nothing can move it before the post or the discard reads it.
    // copyFrom seeds the row from that logical row, and is read before the sort is dropped.
    virtual int  BeginPendingRow (int copyFrom);

    // Queues the INSERT and returns at once. The row becomes an ordinary editable one when the task
    // publishes the ROWID the server handed back - no refetch, which could lose the row.
    virtual void CommitRowInsert ();

    // Throws the staged row away without touching the server.
    virtual void DiscardPendingRow ();

    // Whether the open editor is on the staged row itself, which is what tells the grid that a cell
    // crossed on the way to the next column is being filled in rather than left behind.
    virtual bool IsEditingPendingRow () const;

    // Whether row, taken in the ruler's grid space, is the staged one. CanInsertRow requires table
    // orientation, so a staged row is named by the row ruler and no other orientation can answer yes.
    virtual bool IsPendingRow (int row) const;

    // A staged row is a write in progress, and a sort would move it out from under the one index the
    // post and the discard both name it by. PrepareSort agrees, so the rows are not fetched either.
    virtual bool CanSort () const
    {
        return m_allRowsFetched && !FetchIsActive() && m_pendingRow < 0;
    }

    virtual void PrepareSort () { if (m_pendingRow < 0) FetchAll(); }

    // Clearing the records takes the staged row with it, so the index has to go too or the next
    // post would name a row that is not there.
    virtual void DeleteAll ();

    void CopyColumnNames (std::string& buff, int from, int to) const;

protected:
    std::shared_ptr<OCI8::AutoCursor> m_cursor;
    size_t m_bytesAllocated;
    int  m_fetchPortions;

    void StopActiveFetch (bool forgetSource);

    // The write-back expression for one bind, by TABLE column. ApplyGridFormatContext decided which
    // picture the cell was displayed in, so this is the other half of that decision: the conversion
    // that reads the typed text back with the same picture instead of the session's.
    std::string ValueExprFor (int tableCol, const char* bind) const;

    bool ReleaseFetchSlot (const std::shared_ptr<OciGridFetchControl>& control);

    // The write-back's equivalent: answers false once SetCursor has replaced the result set the edit
    // was queued against, so a task returning late publishes into nothing at all.
    bool ReleaseEditSlot (const std::shared_ptr<OciGridEditControl>& control);

    // Detects the ROWID column and resolves the single table over what SetCursor captured. Called
    // once per result set, by the define task.
    void ApplyRowIdEditState ();

    // Applies a write the server accepted. `physicalRow` is physical and `addressedBy` is the ROWID
    // text the UPDATE used, which together are what stops a late publish landing on the wrong row.
    void PublishCellUpdate (int physicalRow, int tableCol, int rowIdCol,
                            const std::string& addressedBy,
                            const std::string& newValue, const std::string& newRowId);

    // Removes a row the server confirmed deleted, under the same late-publish guard.
    void PublishRowDelete (int physicalRow, int rowIdCol, const std::string& addressedBy);

    // Writes an INSERT's returned ROWID into the staged row, making it an ordinary editable one. No
    // earlier ROWID exists to compare against, so the pending index is the whole late-publish guard.
    void PublishRowInsert (int physicalRow, int rowIdCol, const std::string& newRowId);

    // Forgets the staged row and its values. The row itself is the caller's business: it stays in
    // the grid once posted, and is removed when discarded.
    void ClearPendingState ();

public:
    void CancelActiveFetch ();

protected:

    std::shared_ptr<OciGridFetchControl> m_fetchControl;

    bool m_allRowsFetched;
    bool m_headersInLowerCase;
    double m_fetchTime;

    // The names exactly as the cursor reported them, before GetSafeDatabaseObjectName
    // and the lower-case option rewrite them - after which a ROWID need not still spell ROWID.
    std::vector<std::string> m_rawHeaders;
    std::string m_editSql;      // the cursor's own prepared text, which ResolveSingleTable reads
    std::string m_editTable;    // the single table; empty is the refusal
    int m_rowIdColumn;          // the ROWID column, an index into m_rawHeaders; -1 is the refusal

    std::shared_ptr<OciGridEditControl> m_editControl;

    // The staged INSERT row. Both are reset together by ClearPendingState, and by SetCursor, which
    // replaces the result set the row was staged into.
    int                          m_pendingRow;    // PHYSICAL index; -1 is "no row staged"
    std::vector<std::string>     m_pendingValues; // staged text by table column, empty is unfilled

    static const std::string m_emptyString;
};

#endif//__OCISOURCE_H__
