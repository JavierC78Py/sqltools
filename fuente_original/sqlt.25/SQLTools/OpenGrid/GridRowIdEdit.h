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

#pragma once
#ifndef __GridRowIdEdit_h__
#define __GridRowIdEdit_h__

#include <string>
#include <vector>

namespace OG2 /* OG = OpenGrid */
{

// The ROWID write-back path. No MFC and no oci.h, so every rule can be exercised offline - which
// matters because these are the rules that fail silently.

enum ERowIdColumn
{
    ercNone      = -1,
    ercAmbiguous = -2,
};

// Over the raw names the cursor reported, before GetSafeDatabaseObjectName rewrites them. Answers with
// the column index, or an ERowIdColumn negative when there is none or more than one.
int DetectRowIdColumn (const std::vector<std::string>& rawHeaders);

// The one table a query reads, kept verbatim; empty is the refusal and there is no partial answer.
// Accepts a single-table SELECT only - a join, a comma list or an inline view leaves the grid read-only.
std::string ResolveSingleTable (const std::string& sql);

// Whether a raw column header can appear in `SET <column> = :newval`. Refuses anything that is not one
// bare identifier (`UPPER(x)`, `t.COL`) plus ROWID; a named alias passes and the server must rule on it.
bool IsUpdatableColumnName (const std::string& rawHeader);

// The value expression a write-back binds. Two faults, one cure. The editor holds what SqlFormat
// rendered under the grid's own NLS pictures, so a bare bind lets the server parse that text with the
// session's format instead: a date shown as DD.MM.YYYY against a DD-MON-RR session is ORA-01861,
// quoting a picture the user never chose. And the bind is an OCI8::StringVar, which is SQLT_LVC - a
// LONG VARCHAR Oracle will not coerce straight into a NUMBER, DATE or INTERVAL column, answering
// ORA-01459 instead. As a function's character argument the very same bind is welcome.
//
// `func` names the conversion, `picture` its format model, `nlsParam` the third argument TO_NUMBER
// needs for the decimal and group characters. An empty `func` leaves the bind bare, which is right for
// a character column and for nothing else; an empty `picture` still wraps, emitting `func(bind)`.
std::string BuildValueExpr (const std::string& bind, const std::string& func,
                            const std::string& picture, const std::string& nlsParam = std::string());

// Addressed by CHARTOROWID(:rid) because the grid holds the ROWID as display text. `returningRowId`
// appends the ROWIDTOCHAR clause, which the live probe confirms binds and reads back as 18 characters.
// An empty `valueExpr` binds `:newval` bare, as it always has.
std::string BuildUpdateSql (const std::string& table, const std::string& column, bool returningRowId,
                            const std::string& valueExpr = std::string());

std::string BuildDeleteSql (const std::string& table);

// Locks the row a write is about to address, so one another session holds answers ORA-00054 at once
// instead of blocking the shared background connection. NOWAIT: WAIT n needs 9i, 7.x is still opened.
std::string BuildLockSql (const std::string& table);

// Empty `columns` returns empty rather than emitting `INSERT INTO t () VALUES ()`. Values bind as
// `:v1..:vN`; the clause returns the new ROWID, so an inserted row need not be refetched. `valueExprs`
// runs parallel to `columns`, so a shorter vector leaves the columns it does not reach binding bare.
std::string BuildInsertSql (const std::string& table, const std::vector<std::string>& columns,
                            const std::vector<std::string>& valueExprs = std::vector<std::string>());

// Columns that INSERT names, as indices into `rawHeaders` so the caller can zip them with `staged`.
// A blank staged cell is skipped, not bound NULL, so the column's own DEFAULT answers. Empty = none.
std::vector<int> SelectInsertColumns (const std::vector<std::string>& rawHeaders,
                                      int rowIdColumn,
                                      const std::vector<std::string>& staged);

}

#endif // __GridRowIdEdit_h__
