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

#include "OpenEditor/OEPlsSqlFormatterCommon.h"

namespace OpenEditor {

class Settings;

// ============================================================================
// Top-level named-unit emitters: CREATE TYPE spec/body, TRIGGER headers and
// PACKAGE/PACKAGE BODY.  Split out of OEPlsSqlFormatterBlocks.cpp - pure
// move, no logic changes.
// ============================================================================

// Emits the items of an object type's member list — the tokens strictly between
// the '(' of "AS OBJECT (" and its matching ')' — one per line at `memberLevel`
// (the paren's own level plus one): attributes joined flat, method signatures
// through emitSubprogramHeader, one blank line between the two groups.
// `first`/`last` are the half-open token range inside the parens.
//
// Declared here rather than kept file-local because it has two callers with
// different notions of where the range comes from: emitTypeSpec, which reads it
// off the member-list Paren child of a whole compilation unit, and formatPlsql,
// which hands it the entire token stream of a selection seeded with
// FormatFragmentScope::TypeMembers (md/plsql_formatter_selection_seed_plan.md).
void emitTypeMemberList(const std::vector<OpenEditor::Token>& tokens, int first, int last, int memberLevel, const FormatterPolicy& pol, FormatWriter& fw);
void emitTypeSpec(const FormatContext& block, const std::vector<OpenEditor::Token>& tokens, int headerStart, int isIdx, int indentLevel, const FormatterPolicy& pol, FormatWriter& fw);
void emitTypeBody(const FormatContext& block, const std::vector<OpenEditor::Token>& tokens, int headerStart, int isIdx, int indentLevel, const FormatterPolicy& pol, FormatWriter& fw);
void emitTriggerHeader(const std::vector<OpenEditor::Token>& tokens, int headerStart, int isIdx, int indentLevel, const FormatterPolicy& pol, FormatWriter& fw);
void emitTriggerBodyHeader(const std::vector<OpenEditor::Token>& tokens, int headerStart, int beginIdx, int indentLevel, const FormatterPolicy& pol, FormatWriter& fw);
void emitPackage(const FormatContext& block, const std::vector<OpenEditor::Token>& tokens, int headerStart, int isIdx, int indentLevel, const FormatterPolicy& pol, FormatWriter& fw);

// Emits a CREATE [OR REPLACE] TRIGGER … COMPOUND TRIGGER unit (§12.1): the
// trigger header clauses, then the compound-trigger body — ordinary
// declarations plus one BEFORE/AFTER timing-point section each — emitted like
// a package body's declare section, and finally the "END [name];" clause.
// `headerStart` is block.headerStart; block.openSrcIdx is the closing TRIGGER
// keyword of the "COMPOUND TRIGGER" pair (the header's last token).
void emitCompoundTrigger(const FormatContext& block, const std::vector<OpenEditor::Token>& tokens, int headerStart, int indentLevel, const FormatterPolicy& pol, FormatWriter& fw);

} // namespace OpenEditor
