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

#include "stdafx.h"
#include "OpenEditor/OEPlsSqlFormatterUnits.h"
#include "OpenEditor/OEPlsSqlFormatterDecls.h"
#include "OpenEditor/OEPlsSqlFormatterStmts.h"
#include "OpenEditor/OESettings.h"
#include <algorithm>

namespace OpenEditor {

	// Returns true if the header token range [headerStart, isIdx) contains
	// a PACKAGE keyword (used to distinguish package specs/bodies from
	// standalone procedures/functions at the emitter level).
	// Returns true if the token is a trigger header clause-boundary keyword
	// (the words that each start a new indented line in the trigger header).
	bool isTriggerClauseBoundary(const OpenEditor::Token& tk)
	{
		if (tk.token == etBEFORE
		||  tk.token == etAFTER
		||  tk.token == etINSTEAD
		||  tk.token == etON
		||  tk.token == etFOR
		||  tk.token == etWHEN
		||  tk.token == etCOMPOUND)   // "COMPOUND TRIGGER" body opener (§12.1)
			return true;
		// etUNKNOWN words that are clause boundaries
		if (tk.token == etUNKNOWN)
		{
			const wchar_t* v = tk.value.c_str();
			return !_wcsicmp(v, L"REFERENCING")
				|| !_wcsicmp(v, L"FOLLOWS")
				|| !_wcsicmp(v, L"PRECEDES")
				|| !_wcsicmp(v, L"ENABLE")
				|| !_wcsicmp(v, L"DISABLE");
		}
		return false;
	}

	// Returns true if token is a member-method modifier keyword in a type spec:
	// MEMBER, STATIC, CONSTRUCTOR, OVERRIDING, MAP, ORDER, FINAL, INSTANTIABLE.
	bool isTypeMemberKeyword(const OpenEditor::Token& tk)
	{
		if (tk.token == etUNKNOWN)
		{
			const wchar_t* v = tk.value.c_str();
			return !_wcsicmp(v, L"MEMBER")
				|| !_wcsicmp(v, L"STATIC")
				|| !_wcsicmp(v, L"CONSTRUCTOR")
				|| !_wcsicmp(v, L"OVERRIDING")
				|| !_wcsicmp(v, L"MAP")
				|| !_wcsicmp(v, L"ORDER")
				|| !_wcsicmp(v, L"FINAL")
				|| !_wcsicmp(v, L"INSTANTIABLE")
				|| !_wcsicmp(v, L"NOT");   // NOT FINAL / NOT INSTANTIABLE
		}
		return tk.token == etPROCEDURE || tk.token == etFUNCTION;
	}

	// Returns true if the token introduces a collection-type body:
	//   TABLE OF <elem>            [NOT NULL] [INDEX BY <idx>]
	//   VARRAY(n) OF <elem>
	//   VARYING ARRAY(n) OF <elem>
	// None of the four words is in g_11g_reserved's EToken map, so all of them
	// arrive as etUNKNOWN (TABLE carries reserved=1 but still no EToken kind).
	// The same list short-circuits the outliner's type state machine
	// (TypeNode::PutToken, OEPlsSqlSyntaxImpl.cpp) and tryParseCollectionTypeDecl()
	// narrows it to TABLE/VARRAY because a declaration-section TYPE can never
	// use the VARYING ARRAY spelling.
	bool isCollectionTypeLead(const OpenEditor::Token& tk)
	{
		if (tk.token != etUNKNOWN) return false;
		const wchar_t* v = tk.value.c_str();
		return !_wcsicmp(v, L"TABLE")
			|| !_wcsicmp(v, L"VARRAY")
			|| !_wcsicmp(v, L"VARYING")
			|| !_wcsicmp(v, L"ARRAY");
	}

	// True when the body right after AS/IS is a collection type rather than an
	// object/record member list. Decides which of emitTypeSpec's two layouts
	// applies, and must be read from the *tokens* rather than from the node's
	// children: a collection type's parens are children of the Block too
	// (VARRAY(5) becomes a ParamList because VARRAY is not an isDatatypeWord,
	// VARCHAR2(100) a Paren), so a child scan alone cannot tell the two apart.
	bool isCollectionTypeBody(
		const std::vector<OpenEditor::Token>& tokens, int isIdx)
	{
		if (isIdx < 0) return false;          // forward declaration: no AS/IS
		int i = skipEolAndComments(tokens, isIdx + 1, (int)tokens.size(), nullptr);
		return i < (int)tokens.size() && isCollectionTypeLead(tokens.at(i));
	}

	// Emits the items of an object type's member list — the tokens strictly
	// between the '(' of "AS OBJECT (" and its matching ')' — one per line:
	//
	//     id      number,
	//     name    varchar2(100),
	//
	//     constructor function ty_employee (…) return self as result,
	//     member function get_annual_salary return number
	//
	// Attribute declarations are joined flat; method signatures go through
	// emitSubprogramHeader so their parameter names/modes/types are column-aligned
	// exactly as a package spec's are (§9.3 / §10.1); one blank line separates the
	// attribute group from the method group.
	//
	// `first`/`last` are the half-open token range INSIDE the parens and
	// `memberLevel` is the level the members themselves sit at — the paren's level
	// plus one. Split out of emitTypeSpec so that a selection holding only the
	// member list (FormatFragmentScope::TypeMembers, seeded from TypeNode's
	// shTypeMembers) is laid out by the same code at the level the whole-file run
	// would have used. The emitter reads tokens only, never the FormatContext tree,
	// which is what makes it usable as a seeded entry point.
	void emitTypeMemberList(
		const std::vector<OpenEditor::Token>& tokens,
		int first, int last,
		int memberLevel,
		const FormatterPolicy& pol,
		FormatWriter& fw)
	{
		// Absolute, not the relative IndentGuard emitTypeSpec used before this was
		// split out: a seeded fragment has no enclosing paren level to push from,
		// and at the whole-file call site memberLevel is indentLevel + 1, i.e.
		// exactly what pushing produced.
		IndentLevelGuard levelGuard(fw, memberLevel);

		// Token fidelity over exactly the range this emitter owns. Nested inside
		// emitTypeSpec's whole-unit guard on the whole-file path, and the only guard
		// covering the range when a seeded fragment enters here directly. An empty
		// list (first == last) expects and emits nothing, so it passes too.
		EMIT_RANGE_GUARD(tokens, first, last - 1, fw);

		// Split member list at top-level commas.
		// Each member entry carries any comments that preceded it (between the
		// previous comma and the first non-comment token of this member).
		struct MemberEntry { int start, end; std::vector<int> leadingComments; };
		int depth = 0, mStart = first;
		std::vector<MemberEntry> members;
		for (int j = first; j < last; ++j)
		{
			EToken t = tokens.at(j).token;
			if      (t == etLEFT_ROUND_BRACKET)  ++depth;
			else if (t == etRIGHT_ROUND_BRACKET) --depth;
			else if (t == etCOMMA && depth == 0)
			{
				std::vector<int> leading;
				int ms = skipEolAndComments(tokens, mStart, j, &leading);
				if (ms < j) members.push_back({ms, j - 1, leading});
				mStart = j + 1;
			}
		}
		{
			std::vector<int> leading;
			int ms = skipEolAndComments(tokens, mStart, last, &leading);
			if (ms < last) members.push_back({ms, last - 1, leading});
		}

		// Separate attribute declarations from method signatures.
		size_t firstMethod = members.size();
		for (size_t mi = 0; mi < members.size(); ++mi)
		{
			std::vector<int> dummy;
			int fs = skipEolAndComments(tokens, members.at(mi).start, members.at(mi).end + 1, &dummy);
			if (fs <= members.at(mi).end && isTypeMemberKeyword(tokens.at(fs)))
			{ firstMethod = mi; break; }
		}

		// Emit members one per line, with leading comments on their own lines.
		// `cursor` is the first token no member has written yet, so the tail pass
		// below knows exactly what is left over.
		int cursor = first;
		for (size_t mi = 0; mi < members.size(); ++mi)
		{
			// Blank line between attribute group and method group.
			if (mi == firstMethod && mi > 0)
				fw.NewLine();

			const auto& m = members.at(mi);

			// Leading comments (sitting between previous comma and this member).
			for (int ci : m.leadingComments)
			{
				fw.WriteCurrentIndent();
				fw.WriteToken(tokens.at(ci));
				fw.NewLine();
			}

			// The trailing comma, when this member has one. In a whole type spec
			// every member but the last has one, and the scan finds nothing after
			// that last one — so the whole-file behaviour is unchanged. A seeded
			// fragment, though, can end with the comma of a member whose successor
			// was not selected; skipping the write there loses a token the range
			// guard above expects, so the comma is emitted whenever it is present.
			int commaIdx = -1;
			for (int ci = m.end + 1; ci < last; ++ci)
				if (tokens.at(ci).token == etCOMMA) { commaIdx = ci; break; }

			cursor = (commaIdx >= 0 ? commaIdx : m.end) + 1;

			if (mi >= firstMethod)
			{
				// Method signature: use the same emitter as package spec so
				// param names/modes/types are column-aligned (§9.3 / §10.1).
				// emitSubprogramHeader scans up to scanEnd for the paren, so
				// pass m.end+1 as scanEnd and isIdx=-1 (no IS/AS here).
				const bool lineOpen = emitSubprogramHeader(tokens, m.start, -1, memberLevel, pol, fw, m.end + 1);
				if (commaIdx >= 0)
				{
					// The ',' must never be appended to a line that a "--"
					// comment closed — it would vanish inside the comment.
					if (!lineOpen)
					{
						fw.NewLine();
						fw.WriteCurrentIndent();
					}
					fw.WriteToken(tokens.at(commaIdx));
				}
				fw.NewLine();
			}
			else
			{
				// Attribute declaration: emit flat (no param list).
				fw.WriteCurrentIndent();
				emitFlatTokens(tokens, m.start, m.end, fw);
				if (commaIdx >= 0)
					fw.WriteToken(tokens.at(commaIdx));
				fw.NewLine();
			}
		}

		// Tail: everything after the last member, which by construction is only
		// EOLs and comments — a comment between the final member and ')' in a
		// whole type spec, or one left at the end of a seeded selection. The split
		// above collects comments that PRECEDE a member only, so without this pass
		// those are written by nobody and the range guard above reports the loss.
		for (int j = cursor; j < last; ++j)
		{
			if (tokens.at(j).token != etCOMMENT)
				continue;
			fw.WriteCurrentIndent();
			fw.WriteToken(tokens.at(j));
			fw.NewLine();
		}
	}


	// Emits a CREATE [OR REPLACE] TYPE t AS OBJECT|UNDER base (…) spec:
	//
	//   create or replace type ty_employee as object (
	//     id      number,
	//     name    varchar2(100),
	//     salary  number,
	//
	//     constructor function ty_employee (...) return self as result,
	//     member function get_annual_salary return number,
	//     member procedure give_raise (p_amount in number),
	//     static function create_default return ty_employee
	//   ) not final;
	//
	// headerStart..isIdx-1 : CREATE … AS/IS tokens
	// isIdx                : AS/IS token
	// block                : the IS/AS-opened Block (contains the Paren child
	//                        with the member list, if any)
	// The Paren child holds the member list tokens between ( and ).
	// After ')' there may be NOT FINAL / NOT INSTANTIABLE / FINAL constraints
	// followed by ';'.
	void emitTypeSpec(
		const FormatContext& block,
		const std::vector<OpenEditor::Token>& tokens,
		int headerStart, int isIdx,
		int indentLevel,
		const FormatterPolicy& pol,
		FormatWriter& fw)
	{
		IndentLevelGuard indentGuard(fw, indentLevel);

#ifdef _DEBUG
		// Whole-node token-fidelity guard. All three branches emit a single
		// contiguous span from headerStart through the unit's terminator:
		//   • member list     — header, '(' … ')', qualifiers, ';'/script '/'
		//   • collection type — header, AS/IS, TABLE OF …/VARRAY(…) OF …, term.
		//   • forward decl    — header, then emitEndClause's "END … ;"
		// Only the forward-decl branch routes through emitEndClause, which emits
		// past closeSrcIdx (the END qualifier and its ';') and therefore needs
		// endClauseGuardEnd; the other two write the terminator token itself,
		// i.e. exactly block.closeSrcIdx. isIdx < 0 distinguishes them.
		//
		// Comments that a branch's skipEolAndComments() steps over still lie
		// inside the span, so the oracle expects them — a comment silently
		// dropped here is reported instead of passing unnoticed.
		EMIT_RANGE_GUARD(tokens, headerStart,
						 (isIdx < 0) ? endClauseGuardEnd(tokens, block.closeSrcIdx)
									 : block.closeSrcIdx,
						 fw);
#endif

		// Find the Paren child that holds the member list.
		// It is the first Paren/ParamList child of the block.
		//
		// Collection types are excluded first. Their parens land in the child
		// list too - VARRAY(5) as a ParamList, TABLE OF VARCHAR2(100)'s element
		// precision as a Paren - and either one would be mistaken for an object
		// member list and emitted one entry per line:
		//
		//     CREATE TYPE phone_list_type AS VARRAY (      CREATE TYPE phone_list_type
		//                                                      AS VARRAY(5) OF VARCHAR2(15);
		//       5
		//
		//     ) OF VARCHAR2(15);
		//
		// Skipping the search leaves memberParen null, which routes the body to
		// the flat branch below - it already writes "AS/IS <body> <term>" on the
		// header line, and emitFlatTokens() keeps every paren tight because
		// needSpaceBetween() gives no space before '(' after a non-reserved
		// VARRAY/ARRAY or after an isDatatypeWord element type.
		const FormatContext* memberParen = nullptr;
		if (!isCollectionTypeBody(tokens, isIdx))
		{
			for (const auto& ch : block.children)
			{
				if ((ch.kind == FormatContext::Paren || ch.kind == FormatContext::ParamList)
				&&   ch.openSrcIdx > isIdx)
				{ memberParen = &ch; break; }
			}
		}

		if (!memberParen || memberParen->closeSrcIdx < 0)
		{
			// No member list. Two distinct cases share this branch:
			//
			//  1. True forward declaration: "CREATE TYPE t;" — isIdx is -1
			//     (no AS/IS seen at all), block.closeSrcIdx is the ';'.
			//
			//  2. Collection type with no parenthesized member list, e.g.
			//     "CREATE TYPE token_list AS TABLE OF token_t;" or
			//     "... AS VARRAY(10) OF NUMBER;" — isIdx is the AS/IS token,
			//     and everything between it and the terminator (TABLE OF …,
			//     VARRAY(…) OF …, etc.) must still be emitted on the same
			//     line as AS/IS; there is no real END keyword, so
			//     emitEndClause() must not be used here (it would treat the
			//     terminator token as if it were END and silently drop the
			//     TABLE OF/VARRAY OF clause).
			if (isIdx >= 0)
			{
				const bool lineOpen = emitSubprogramHeader(tokens, headerStart, -1, indentLevel, pol, fw, isIdx);
				// AS/IS is appended to the header line — unless a "--" comment
				// closed it, in which case it starts a line of its own.
				if (lineOpen)
					fw.WriteSpaces(1);
				else
				{
					fw.NewLine();
					fw.WriteCurrentIndent();
				}
				fw.WriteToken(tokens.at(isIdx));
				int suffixStart = skipEolAndComments(tokens, isIdx + 1, (int)tokens.size(), nullptr);
				int stmtEnd = block.closeSrcIdx;
				for (int j = suffixStart; j <= stmtEnd; ++j)
					if (tokens.at(j).token == etSEMICOLON || tokens.at(j).token == etEOS)
					{ stmtEnd = j; break; }
				// Step 2.2 — the Block node carries its own terminator
				// classification (buildTree closed it at this very ';'/EOS),
				// so the layout decision is read from the node; stmtEnd is
				// only the fallback for a node left unclassified because the
				// input was truncated.
				const bool endIsSlash = terminatorIsScriptSlash(block, tokens, stmtEnd);
				// The body (TABLE OF …/VARRAY(…) OF …) excludes the trailing
				// terminator in BOTH cases: a script "/" terminator goes on a
				// line of its own (emitted below), while a ';' attaches
				// directly to the body (emitted by the append below). The
				// body range must therefore stop before stmtEnd — emitFlatTokens
				// is inclusive of its end index, so including stmtEnd here
				// wrote the terminator twice ("NUMBER;;").
				int bodyEnd = stmtEnd - 1;
				while (bodyEnd >= suffixStart
				&&    (tokens.at(bodyEnd).token == etEOL || tokens.at(bodyEnd).token == etCOMMENT))
					--bodyEnd;
				if (suffixStart <= bodyEnd
				&&  tokens.at(suffixStart).token != etSEMICOLON
				&&  tokens.at(suffixStart).token != etEOS)
				{
					fw.WriteSpaces(1);
					emitFlatTokens(tokens, suffixStart, bodyEnd, fw);
				}
				if (!endIsSlash && suffixStart <= stmtEnd
				&& (tokens.at(stmtEnd).token == etSEMICOLON || tokens.at(stmtEnd).token == etEOS))
				{
					fw.WriteToken(tokens.at(stmtEnd));
				}
				fw.NewLine();
				if (endIsSlash)
				{
					fw.WriteCurrentIndent();
					fw.WriteToken(tokens.at(stmtEnd));
					fw.NewLine();
				}
			}
			else
			{
				emitSubprogramHeader(tokens, headerStart, isIdx, indentLevel, pol, fw);
				fw.NewLine();
				emitEndClause(tokens, block.closeSrcIdx, indentLevel, fw);
			}
			return;
		}

		int poIdx = memberParen->openSrcIdx;   // '('
		int pcIdx = memberParen->closeSrcIdx;  // ')'

		// Emit: "create or replace type ty_emp as object ("
		fw.WriteCurrentIndent();
		fw.WriteText(joinTokensSingleSpaced(tokens, headerStart, poIdx));
		fw.WriteSpaces(1);
		fw.WriteToken(tokens.at(poIdx));
		fw.NewLine(); // " (\n"

		if (pol.plsqlBlankLineAfterUnitHeader)
			appendBlankLineIfNeeded(fw);

		// The members themselves: split at top-level commas, attributes joined flat
		// and method signatures column-aligned, one per line at indentLevel + 1.
		// emitTypeMemberList is also the entry point a seeded member-list fragment
		// (FormatFragmentScope::TypeMembers) is emitted through, so the two paths
		// cannot drift apart.
		emitTypeMemberList(tokens, poIdx + 1, pcIdx, indentLevel + 1, pol, fw);

		// Closing ')'.
		if (pol.plsqlBlankLineAfterUnitHeader)
			appendBlankLineIfNeeded(fw);
		fw.WriteCurrentIndent();
		fw.WriteToken(tokens.at(pcIdx)); // ')'

		// Constraints / qualifiers after ')' up to the closing END / ';'.
		// (NOT FINAL, NOT INSTANTIABLE, FINAL, etc.)
		int afterParen = skipEolAndComments(tokens, pcIdx + 1, (int)tokens.size(), nullptr);
		// Scan to end of statement (semicolon or the block's closeSrcIdx).
		int stmtEnd = block.closeSrcIdx;
		for (int j = afterParen; j <= stmtEnd; ++j)
			if (tokens.at(j).token == etSEMICOLON || tokens.at(j).token == etEOS)
			{ stmtEnd = j; break; }

		// Step 2.2 — as in the no-member-list branch above, the ';' vs script
		// '/' decision is read from the node's recorded classification, with
		// the raw token used only as the unclassified-node fallback.
		//
		// This is deliberately NOT gated on afterParenIsTerminator. Gating it
		// that way meant a qualified body classified as endIsSlash=false, so
		// the '/' fell into emitFlatTokens — which is inclusive of its end
		// index — and was written mid-line:
		//
		//     CREATE TYPE food_ot AS OBJECT (
		//       name VARCHAR2(100)
		//     ) NOT FINAL /
		//
		// That is a meaning change, not just a layout one: a '/' that does not
		// stand alone re-lexes as a division operator (etDIVIDE) instead of the
		// script terminator (etEOS), so both the LayoutGuard and the
		// losslessness check reject the output. The unqualified form
		// ("…) NOT FINAL;" and "…)\n/") happened to work, which is why only the
		// qualifier + '/' combination was broken.
		const bool endIsSlash = terminatorIsScriptSlash(block, tokens, stmtEnd);
		// True when there are no qualifier tokens at all, i.e. afterParen
		// already lands on the terminating ';'/EOS itself. Such a terminator
		// attaches straight to ')' — routing it through the qualifier run
		// would emit a stray space (") ;" instead of ");").
		bool afterParenIsTerminator = afterParen <= stmtEnd
			&& (tokens.at(afterParen).token == etSEMICOLON || tokens.at(afterParen).token == etEOS);
		// The qualifier run excludes the trailing terminator when it is a
		// script '/'; that '/' is written on a line of its own below. A ';'
		// stays inside the run so it follows the last qualifier directly.
		const int qualEnd = endIsSlash ? stmtEnd - 1 : stmtEnd;
		if (afterParen <= qualEnd && !afterParenIsTerminator)
		{
			fw.WriteSpaces(1);
			emitFlatTokens(tokens, afterParen, qualEnd, fw);
		}
		else if (afterParenIsTerminator && !endIsSlash)
		{
			fw.WriteToken(tokens.at(afterParen));
		}
		fw.NewLine();
		if (endIsSlash)
		{
			fw.WriteCurrentIndent();
			fw.WriteToken(tokens.at(stmtEnd));
			fw.NewLine();
		}
	}

	// Emits a CREATE [OR REPLACE] TYPE BODY … AS … END; —
	// identical layout to emitPackage (§11.2 body rules apply).
	void emitTypeBody(
		const FormatContext& block,
		const std::vector<OpenEditor::Token>& tokens,
		int headerStart, int isIdx,
		int indentLevel,
		const FormatterPolicy& pol,
		FormatWriter& fw)
	{
		// Reuse the package emitter — identical layout.
		emitPackage(block, tokens, headerStart, isIdx, indentLevel, pol, fw);
	}

	// Emits a CREATE [OR REPLACE] [EDITIONABLE] TRIGGER header:
	//
	//   create or replace trigger trg_name
	//     before insert or update on emp
	//     referencing new as new old as old
	//     for each row
	//     when (new.employee_id is not null)
	//   is | as       ← or nothing if trigger uses BEGIN directly (no IS/AS)
	//
	// tokens[headerStart..isIdx) is the trigger name + clause tokens.
	// tokens[isIdx] is IS or AS (emitted on its own line).
	void emitTriggerHeader(
		const std::vector<OpenEditor::Token>& tokens,
		int headerStart, int isIdx,
		int indentLevel,
		const FormatterPolicy& pol,
		FormatWriter& fw)
	{
		int i = headerStart;

		// Emit line 1: "create [or replace] [editionable] trigger name"
		// — everything up to (but not including) the first clause boundary.
		// Skip paren groups inline (e.g. schema.name is not a paren, but
		// WHEN (...) has one — handled below as a clause).
		fw.WriteCurrentIndent();
		bool firstOnLine = true;
		int prev = -1;
		int parenD = 0;
		while (i < isIdx)
		{
			const auto& tk = tokens.at(i);
			if (tk.token == etEOL) { ++i; continue; }
			if (parenD == 0 && isTriggerClauseBoundary(tk)) break;
			if (tk.token == etLEFT_ROUND_BRACKET)  ++parenD;
			if (tk.token == etRIGHT_ROUND_BRACKET) { if (parenD > 0) --parenD; }
			// needSpaceBetween() — the canonical spacing rule: '(' after a
			// reserved word (WHEN, …) gets its space, no space after '.'.
			if (!firstOnLine && prev >= 0 && needSpaceBetween(tokens.at(prev), tk)) fw.WriteSpaces(1);
			fw.WriteToken(tk);
			firstOnLine = false;
			prev = i;
			++i;
		}
		fw.NewLine();

		// Emit each clause on its own indented line.
		{
			IndentGuard clauseGuard(fw);
			while (i < isIdx)
			{
				const auto& tk = tokens.at(i);
				if (tk.token == etEOL) { ++i; continue; }
				// Each clause boundary starts a new line.
				fw.WriteCurrentIndent();
				firstOnLine = true;
				prev = -1;
				parenD = 0;
				while (i < isIdx)
				{
					const auto& tk2 = tokens.at(i);
					if (tk2.token == etEOL) { ++i; continue; }
					// Next clause boundary — stop (outer loop will start a new line).
					if (parenD == 0 && !firstOnLine && isTriggerClauseBoundary(tk2)) break;
					if (tk2.token == etLEFT_ROUND_BRACKET)  ++parenD;
					if (tk2.token == etRIGHT_ROUND_BRACKET) { if (parenD > 0) --parenD; }
					if (!firstOnLine && prev >= 0 && needSpaceBetween(tokens.at(prev), tk2)) fw.WriteSpaces(1);
					fw.WriteToken(tk2);
					firstOnLine = false;
					prev = i;
					++i;
				}
				fw.NewLine();
			}
		}

		// Emit IS/AS on its own line at base indent.
		fw.WriteCurrentIndent();
		fw.WriteToken(tokens.at(isIdx));
		fw.NewLine();
	}

	// Emits a CREATE [OR REPLACE] TRIGGER header for triggers that use BEGIN
	// directly (no IS/AS).  tokens[headerStart..beginIdx) are the header
	// tokens; beginIdx is the BEGIN token (not emitted here — emitBlock does).
	void emitTriggerBodyHeader(
		const std::vector<OpenEditor::Token>& tokens,
		int headerStart, int beginIdx,
		int indentLevel,
		const FormatterPolicy& pol,
		FormatWriter& fw)
	{
		// Delegate to emitTriggerHeader with a fake "isIdx" = beginIdx.
		// We must NOT emit tokens[beginIdx] here (it's BEGIN, not IS/AS).
		// So we write the same clause-splitting loop but skip the final IS/AS emit.
		int i = headerStart;

		// First line: up to first clause boundary.
		fw.WriteCurrentIndent();
		bool firstOnLine = true;
		int prev = -1;
		int parenD = 0;
		while (i < beginIdx)
		{
			const auto& tk = tokens.at(i);
			if (tk.token == etEOL) { ++i; continue; }
			if (parenD == 0 && isTriggerClauseBoundary(tk)) break;
			if (tk.token == etLEFT_ROUND_BRACKET)  ++parenD;
			if (tk.token == etRIGHT_ROUND_BRACKET) { if (parenD > 0) --parenD; }
			if (!firstOnLine && prev >= 0 && needSpaceBetween(tokens.at(prev), tk)) fw.WriteSpaces(1);
			fw.WriteToken(tk);
			firstOnLine = false;
			prev = i;
			++i;
		}
		fw.NewLine();

		// Remaining clauses, each on its own indented line.
		{
			IndentGuard clauseGuard(fw);
			while (i < beginIdx)
			{
				const auto& tk = tokens.at(i);
				if (tk.token == etEOL) { ++i; continue; }
				fw.WriteCurrentIndent();
				firstOnLine = true;
				prev = -1;
				parenD = 0;
				while (i < beginIdx)
				{
					const auto& tk2 = tokens.at(i);
					if (tk2.token == etEOL) { ++i; continue; }
					if (parenD == 0 && !firstOnLine && isTriggerClauseBoundary(tk2)) break;
					if (tk2.token == etLEFT_ROUND_BRACKET)  ++parenD;
					if (tk2.token == etRIGHT_ROUND_BRACKET) { if (parenD > 0) --parenD; }
					if (!firstOnLine && prev >= 0 && needSpaceBetween(tokens.at(prev), tk2)) fw.WriteSpaces(1);
					fw.WriteToken(tk2);
					firstOnLine = false;
					prev = i;
					++i;
				}
				fw.NewLine();
			}
		}
		// BEGIN is emitted by emitBlock.
	}

	// For a spec (no BEGIN inside the block): the entire content between IS
	// and END is treated as a declaration section.
	// For a body (BEGIN present): declarations before BEGIN are a declare
	// section; the init block BEGIN … END follows with normal body emission.
	void emitPackage(
		const FormatContext& block,           // IS-opened Block scope
		const std::vector<OpenEditor::Token>& tokens,
		int headerStart,                      // first token of CREATE/PACKAGE
		int isIdx,                            // IS/AS token index
		int indentLevel,
		const FormatterPolicy& pol,
		FormatWriter& fw)
	{
		IndentLevelGuard indentGuard(fw, indentLevel);
		// The emitted span runs from the header (headerStart) through the
		// END-line ';' (past closeSrcIdx, emitted by emitEndClause).
		EMIT_RANGE_GUARD(tokens, headerStart,
						 endClauseGuardEnd(tokens, block.closeSrcIdx), fw);

		// Emit CREATE [OR REPLACE] PACKAGE [BODY] name [AUTHID …] IS/AS
		emitSubprogramHeader(tokens, headerStart, isIdx, indentLevel, pol, fw);

		const bool blankAfterHeader = pol.plsqlBlankLineAfterUnitHeader;

		const ClauseEntry* beginKw  = getKeyword(block.keywords, etBEGIN);
		const ClauseEntry* exceptKw = getKeyword(block.keywords, etEXCEPTION);
		int bodyEnd = exceptKw ? exceptKw->srcIdx : block.closeSrcIdx;

		// Declare section: tokens between IS and BEGIN (or END for spec-only).
		int declStart = skipEol(tokens, isIdx + 1, (int)tokens.size());
		int declEnd   = beginKw ? beginKw->srcIdx : block.closeSrcIdx;
		bool hasDecl = declStart < declEnd;
		if (hasDecl)
		{
			if (blankAfterHeader) appendBlankLineIfNeeded(fw);
			emitDeclareSection(block, tokens, declStart, declEnd, indentLevel + 1, pol, fw);
		}

		// Optional init block (package body only).
		if (beginKw)
		{
			appendBlankLineIfNeeded(fw);
			fw.WriteCurrentIndent();
			fw.WriteToken(tokens.at(beginKw->srcIdx));
			fw.NewLine();
			int bodyStart = skipEol(tokens, beginKw->srcIdx + 1, (int)tokens.size());
			emitStatementSequence(block, tokens, bodyStart, bodyEnd, pol, indentLevel + 1, fw);
		}
		if (exceptKw)
		{
			appendBlankLineIfNeeded(fw);
			fw.WriteCurrentIndent();
			fw.WriteToken(tokens.at(exceptKw->srcIdx));
			fw.NewLine();
			int exStart = skipEol(tokens, exceptKw->srcIdx + 1, (int)tokens.size());
			emitExceptionSection(block, tokens, exStart, block.closeSrcIdx, indentLevel, pol, fw);
		}

			if (blankAfterHeader && !beginKw && !exceptKw)
				appendBlankLineIfNeeded(fw);

			emitEndClause(tokens, block.closeSrcIdx, indentLevel, fw);
		}

	// -----------------------------------------------------------------------
	// CREATE [OR REPLACE] TRIGGER … COMPOUND TRIGGER … END [name];
	// -----------------------------------------------------------------------
	// §12.1. A compound-trigger body has no BEGIN of its own: it is a
	// declare-section-like sequence of ordinary declarations (types,
	// variables, cursors, nested subprograms) followed by one timing-point
	// section per BEFORE/AFTER clause. emitDeclareSection already lays that
	// out exactly the way a package body's declare section is laid out
	// (blank-line separated items, column-aligned variables, nested
	// subprogram bodies), and its timing-point arm — emitTimingPointSection
	// in OEPlsSqlFormatterStmts.cpp — routes each
	// "BEFORE|AFTER STATEMENT|EACH ROW IS … BEGIN … END <label>;" through
	// emitBlock, so a timing point is formatted like a nested subprogram.
	//
	// block.openSrcIdx is the closing TRIGGER keyword of the "COMPOUND
	// TRIGGER" pair — the header's last token, since the unit has no IS/AS —
	// so the header span is [headerStart, openSrcIdx] *inclusive* and the
	// body is (openSrcIdx, closeSrcIdx).
	void emitCompoundTrigger(
		const FormatContext& block,
		const std::vector<OpenEditor::Token>& tokens,
		int headerStart,
		int indentLevel,
		const FormatterPolicy& pol,
		FormatWriter& fw)
	{
		IndentLevelGuard indentGuard(fw, indentLevel);
		// The emitted span runs from the header through the END-line ';'
		// (past closeSrcIdx, emitted by emitEndClause) — the same convention
		// emitPackage uses. The script '/' after it belongs to the caller.
		EMIT_RANGE_GUARD(tokens, headerStart,
						 endClauseGuardEnd(tokens, block.closeSrcIdx), fw);

		// "create or replace trigger name
		//    for <event> on <table>
		//    compound trigger"
		// emitTriggerBodyHeader's range is exclusive, and openSrcIdx is the
		// header's last token, hence the +1.
		emitTriggerBodyHeader(tokens, headerStart, block.openSrcIdx + 1,
							  indentLevel, pol, fw);

		const int bodyStart = skipEol(tokens, block.openSrcIdx + 1,
									  block.closeSrcIdx);
		if (bodyStart < block.closeSrcIdx)
		{
			if (pol.plsqlBlankLineAfterUnitHeader)
				appendBlankLineIfNeeded(fw);
			emitDeclareSection(block, tokens, bodyStart, block.closeSrcIdx,
							   indentLevel + 1, pol, fw);
		}

		emitEndClause(tokens, block.closeSrcIdx, indentLevel, fw);
	}


} // namespace OpenEditor
