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
// Statement-level emitters: expressions, statements, blocks and statement
// sequences.  Split out of OEPlsSqlFormatterBlocks.cpp - pure move, no logic
// changes.
// ============================================================================

// Emits statements over [rangeStart,rangeEnd) of the given scope node:
// IF/LOOP/CASE statements, nested BEGIN blocks, DML, calls, assignments,
// conditional compilation and verbatim bypass fragments.
void emitStatementSequence(const FormatContext& node, const std::vector<OpenEditor::Token>& tokens, int rangeStart, int rangeEnd, const FormatterPolicy& pol, int indentLevel, FormatWriter& fw);

// Emits the DECLARE-section contents over [rangeStart,rangeEnd): variable
// declarations (grouped by name), cursors, record/collection types, nested
// subprograms and conditional-compilation directives.
void emitDeclareSection(const FormatContext& node, const std::vector<OpenEditor::Token>& tokens, int rangeStart, int rangeEnd, int indentLevel, const FormatterPolicy& pol, FormatWriter& fw);

// Emits the EXCEPTION section of a block: WHEN/THEN handler clauses recorded
// in block.keywords.  exStart is the first token after EXCEPTION; blockEnd is
// block.closeSrcIdx.
void emitExceptionSection(const FormatContext& block, const std::vector<OpenEditor::Token>& tokens, int exStart, int blockEnd, int indentLevel, const FormatterPolicy& pol, FormatWriter& fw);

// Emits a BEGIN...END block: DECLARE section (if any), statement sequence,
// EXCEPTION section (if any) and the END clause.
void emitBlock(const FormatContext& block, const std::vector<OpenEditor::Token>& tokens, const FormatterPolicy& pol, int indentLevel, FormatWriter& fw);

} // namespace OpenEditor
