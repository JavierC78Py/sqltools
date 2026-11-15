/* 
	Copyright (C) 2018-2026 Aleksey Kochetov

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

// ============================================================================
// OEPlsSqlFormatterBlocks — PL/SQL-aware extension of the SQL formatter.
//
// This file pair holds all new logic required to format PL/SQL block-level
// constructs (DECLARE/BEGIN/EXCEPTION/END, IF/LOOP, CREATE PROCEDURE/
// FUNCTION/PACKAGE/TRIGGER/TYPE, etc.) as specified in
// md/plsql_formatter_rules.md.
//
// It builds on top of OEPlsSqlFormatter.h/.cpp (FormatContext, buildTree,
// calcWidths) which remain the single source of truth for DML scopes
// (Select/Insert*/Update/Delete/Merge/CaseExpr/Paren/BetweenExpr). A SQL
// statement nested inside a PL/SQL block is formatted as a leaf using the
// existing DML emission functions; a PL/SQL block is never a leaf inside a
// SQL DML tree — the dependency is one-directional (PL/SQL -> DML only).
//
// Implementation status: scaffolding only (Phase 1). See
// md/plsql_formatter_rules.md for the full specification and
// md/refactor_formatcontext.md for prior FormatContext history. Subsequent
// phases will fill in buildTreePlsql()/calcWidthsPlsql() and the emission
// logic that walks the combined PL/SQL+DML tree.
// ============================================================================

#include "OpenEditor/OEPlsSqlFormatter.h"

namespace OpenEditor {

	class Settings;

// Placeholder entry point for the PL/SQL-aware structural pre-pass.
// Currently delegates to the existing DML-only buildTree() unchanged;
// PL/SQL Kind detection (Block/IfStmt/LoopStmt/... — see FormatContext::Kind
// in OEPlsSqlFormatter.h) will be added here in subsequent phases.
FormatContext buildTreePlsql(const std::vector<OpenEditor::Token>& tokens);

// ============================================================================
// formatPlsql — PL/SQL-aware entry point.
//
// Uses buildTree()'s FormatContext::Block scopes purely to locate
// DECLARE/BEGIN/EXCEPTION/END skeleton boundaries and nested anonymous-block
// boundaries. Everything that is not part of that skeleton (declarations,
// statements, exception handlers) is sliced into individual top-level
// ';'-terminated fragments and formatted by calling the existing, unmodified
// formatSQL() on each fragment as a black box, then re-indented to the
// current block nesting level. When tokens contain no Block scope at all
// (plain DML), this delegates straight to formatSQL() with no overhead or
// behavior change.
// ============================================================================
// ============================================================================
// `seed` (see FormatterSeed in OEPlsSqlFormatter.h) tells formatPlsql which part
// of the document `tokens` came from. The default is a whole-document run and is
// the only value every existing caller passes; a seeded run picks the emit entry
// point of the enclosing section and starts it at the enclosing indent level,
// then de-indents the result so the fragment still starts at column 0.
// ============================================================================
std::wstring formatPlsql(std::vector<OpenEditor::Token>& tokens, const FormatterPolicy& pol,
						 const FormatterSeed& seed = FormatterSeed());

} // namespace OpenEditor
