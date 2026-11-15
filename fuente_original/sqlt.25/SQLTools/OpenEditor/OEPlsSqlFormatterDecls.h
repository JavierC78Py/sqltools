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
// PL/SQL declaration-item structs, parsers and emitters.
// Split out of OEPlsSqlFormatterBlocks.cpp - pure move, no logic changes.
// ============================================================================

	struct VarDecl
	{
		int nameIdx      = -1;
		int constantIdx  = -1; // index of the CONSTANT token, or -1 if absent
		int typeStart    = -1, typeEnd    = -1; // [typeStart, typeEnd)
		bool hasDefault  = false;
		int assignIdx    = -1; // index of the ":=" or "DEFAULT" token
		int defaultStart = -1, defaultEnd = -1; // [defaultStart, defaultEnd)
		int semiIdx      = -1;
		std::vector<int> comments; // indices of comments embedded between significant tokens
	};

	struct CursorDecl
	{
		int nameIdx    = -1;
		int cursorIdx  = -1;
		int paramStart = -1, paramEnd = -1; // [paramStart, paramEnd) incl. parens, or -1 if none
		int isIdx      = -1;
		int selectStart = -1;
		int semiIdx    = -1;
		std::vector<int> headerComments; // comments embedded in the CURSOR...IS header (before selectStart)
	};

	// -----------------------------------------------------------------------
	// TYPE name IS RECORD ( field... );
	// -----------------------------------------------------------------------
	struct RecordTypeDecl
	{
		int typeIdx   = -1;
		int nameIdx   = -1;
		int isIdx     = -1;
		int recordIdx = -1;
		int parenStart = -1, parenEnd = -1; // index of first token inside '(' and index of ')'
		int semiIdx   = -1;
		std::vector<int> headerComments;
		// One field of the record's field list. [start,end) is the field's own
		// token range with EOLs/comments stripped from the front; `lead` holds
		// the comments the split stepped over to get to `start`, in source order.
		// A chunk that is nothing but comments is kept with an empty range
		// (start >= end) and a non-empty `lead`, so those comments survive.
		struct Field
		{
			int start = 0, end = 0;
			std::vector<int> lead;
		};
		std::vector<Field> fields;
	};

	struct CollectionTypeDecl
	{
		int typeIdx    = -1; // TYPE
		int nameIdx    = -1; // type name
		int isIdx      = -1; // IS
		int kindIdx    = -1; // TABLE or VARRAY
		// For VARRAY: the size expression tokens sit between '(' and ')'.
		int vsizeStart = -1, vsizeEnd = -1; // -1 if TABLE
		// ofIdx: the OF keyword token index.
		int ofIdx      = -1;
		// Everything from ofIdx+1 to semiIdx is the element-type + optional modifiers.
		int elemStart  = -1, elemEnd = -1;
		int semiIdx    = -1;
		std::vector<int> headerComments;
	};

	// -----------------------------------------------------------------------
	// Nested PROCEDURE / FUNCTION in a DECLARE section
	//
	// Header tokens: PROCEDURE|FUNCTION name [(params)] [RETURN type] IS|AS
	// Followed by either:
	//   - a Block child (BEGIN ... END;)  → full body
	//   - a ';'                           → forward declaration (spec only)
	// IS/AS is always placed on its own line; the body follows at the same
	// indent level as the header (matching top-level procedure/function style).
	// -----------------------------------------------------------------------
	struct NestedSubprogramHeader
	{
		int  prefixStart = -1; // first modifier token (OVERRIDING/MEMBER/STATIC/CONSTRUCTOR…)
							   // or same as kwIdx when no modifiers present
		int  kwIdx    = -1;  // PROCEDURE or FUNCTION token index
		int  nameIdx  = -1;  // subprogram name token index
		// [paramStart, paramEnd) covers the '(' … ')' inclusive, or both -1
		int  paramStart = -1, paramEnd = -1;
		int  isIdx    = -1;  // IS or AS token index (-1 for forward decl)
		int  semiIdx  = -1;  // ';' for forward decl (-1 for full body)
		bool isForwardDecl = false;
		std::vector<int> headerComments;
	};

// ---------------------------------------------------------------------------
// tryParse*() return the index just past the parsed item (past its
// terminating semicolon), or -1 when tokens[pos] does not start that item.
// ---------------------------------------------------------------------------
int  tryParseVarDecl(const std::vector<OpenEditor::Token>& tokens, int pos, int rangeEnd, VarDecl& decl);
void emitVarDeclGroup(const std::vector<OpenEditor::Token>& tokens, const std::vector<VarDecl>& group, const FormatterPolicy& pol, const std::vector<FormatContext>* children, int indentLevel, FormatWriter& fw);
int  tryParseCursorDecl(const std::vector<OpenEditor::Token>& tokens, int pos, int rangeEnd, CursorDecl& decl);
void emitCursorDecl(const std::vector<OpenEditor::Token>& tokens, const CursorDecl& decl, const FormatterPolicy& pol, int indentLevel, FormatWriter& fw);
int  tryParseRecordTypeDecl(const std::vector<OpenEditor::Token>& tokens, int pos, int rangeEnd, RecordTypeDecl& decl);
void emitRecordTypeDecl(const std::vector<OpenEditor::Token>& tokens, const RecordTypeDecl& decl, const FormatterPolicy& pol, int indentLevel, FormatWriter& fw);
int  tryParseCollectionTypeDecl(const std::vector<OpenEditor::Token>& tokens, int pos, int rangeEnd, CollectionTypeDecl& decl);
void emitCollectionTypeDecl(const std::vector<OpenEditor::Token>& tokens, const CollectionTypeDecl& decl, FormatWriter& fw, int indentLevel);
int  tryParseNestedSubprogramHeader(const std::vector<OpenEditor::Token>& tokens, int pos, int rangeEnd, NestedSubprogramHeader& hdr);
// Returns true when the output line is still open, i.e. when the caller may
// append text (a terminator, a ',') directly to it. False means a "--" comment
// closed the last line, so anything appended there would be swallowed by the
// comment and lost on re-parse — the caller must break the line first. Callers
// that only ever start a new line after the header (IS/AS headers followed by a
// block body) may ignore the result.
bool emitSubprogramHeader(const std::vector<OpenEditor::Token>& tokens, int headerStart, int isIdx, int indentLevel, const FormatterPolicy& pol, FormatWriter& fw, int scanEnd = -1);
void emitNestedSubprogram(const FormatContext& declNode, const std::vector<OpenEditor::Token>& tokens, const NestedSubprogramHeader& hdr, int afterIsPos, const FormatterPolicy& pol, int indentLevel, FormatWriter& fw);

// ---------------------------------------------------------------------------
// Emits a forward-declared / body-less subprogram header straight from its
// SubprogramForwardDecl tree node (Step 2.1 of
// md/plsql_formatter_buildtree_migration_plan.md) — no raw-token re-scanning.
//
//   fwd        — the SubprogramForwardDecl node. openSrcIdx == headerStart is
//                the first header token (CREATE [OR REPLACE] / method
//                modifiers / PROCEDURE / FUNCTION) and closeSrcIdx is the
//                terminating ';' or script '/' token itself.
//   spanStart  — first token to emit. Pass the caller's cursor so that any
//                method modifier sitting just before the recorded span start
//                (possible when a comment between the modifier and the
//                keyword stopped buildTree's backward extension) is emitted
//                too. Defaults to -1 == fwd.openSrcIdx.
//
// Output is byte-identical to emitNestedSubprogram's isForwardDecl branch:
// the header normalized by emitSubprogramHeader, then the terminator written
// from tokens[closeSrcIdx].value (';' appended to the header line, a script
// '/' placed on its own line) — token fidelity, never a synthesized literal.
// ---------------------------------------------------------------------------
void emitSubprogramForwardDecl(const FormatContext& fwd, const std::vector<OpenEditor::Token>& tokens, const FormatterPolicy& pol, int indentLevel, FormatWriter& fw, int spanStart = -1);

} // namespace OpenEditor
