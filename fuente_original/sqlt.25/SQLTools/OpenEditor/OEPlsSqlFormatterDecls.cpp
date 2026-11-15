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
#include "OpenEditor/OEPlsSqlFormatterDecls.h"
#include "OpenEditor/OEPlsSqlFormatterExpr.h"
#include "OpenEditor/OEPlsSqlFormatterStmts.h"
#include "OpenEditor/OESettings.h"
#include <algorithm>

namespace OpenEditor {

	// Case-insensitive token-value comparison (used for DEFAULT, which is not
	// a distinct EToken value). Plain, non-keyword words are tokenized as
	bool isWord(const OpenEditor::Token& tk, const wchar_t* word)
	{
		if (tk.token != etUNKNOWN) return false;
		size_t n = wcslen(word);
		if (tk.value.size() != n) return false;
		for (size_t k = 0; k < n; ++k)
			// word[k] keeps operator[]: raw const wchar_t*, bounded by the
			// wcslen/size check above (no .at() on raw pointers).
			if (towupper(tk.value.at(k)) != word[k]) return false;
		return true;
	}

	// ------------------------------------------------------------------
	// Declaration-section formatting (variables/constants and cursors).
	// See md/plsql_formatter_rules.md for the declaration alignment rules.
	// ------------------------------------------------------------------

	// Parses tokens[pos..rangeEnd) as "name [CONSTANT] type [(:=|DEFAULT) expr];".
	// On success, fills decl and returns the index just past the ';' (and any
	// trailing EOLs). On failure returns -1 and decl is left unspecified.
	int tryParseVarDecl(
		const std::vector<OpenEditor::Token>& tokens,
		int pos, int rangeEnd,
		VarDecl& decl)
	{
		int i = skipEol(tokens, pos, rangeEnd);
		if (i >= rangeEnd || tokens.at(i).token != etUNKNOWN) return -1;
		decl.nameIdx = i;
		i = skipEol(tokens, i + 1, rangeEnd);
		if (i >= rangeEnd) return -1;

		decl.constantIdx = -1;
		if (tokens.at(i).token == etCONSTANT)
		{
			decl.constantIdx = i;
			i = skipEol(tokens, i + 1, rangeEnd);
		}

		// Embedded block comments (e.g. "c_max_rows /*constant*/ number := 100;")
		// remain part of the type/default token range and are reproduced by
		// joinTokensSingleSpaced() like any other token — column alignment still
		// works correctly. An end-of-line "-- ..." comment would silently swallow
		// everything placed after it on the same joined output line, so its
		// presence makes this declaration fail to parse structurally and fall
		// back to emitNormalizedStatement instead.
		decl.typeStart = i;
		int parenDepth = 0;
		while (i < rangeEnd)
		{
			EToken t = tokens.at(i).token;
			if (t == etEOL) { ++i; continue; }
			if (t == etCOMMENT && isEndLineComment(tokens.at(i))) return -1;
			if (t == etLEFT_ROUND_BRACKET)  { ++parenDepth; ++i; continue; }
			if (t == etRIGHT_ROUND_BRACKET) { if (parenDepth > 0) --parenDepth; ++i; continue; }
			if (parenDepth == 0
			&& (t == etASSIGN || t == etSEMICOLON || t == etEOS || isWord(tokens.at(i), L"DEFAULT")))
				break;
			++i;
		}
		if (i >= rangeEnd) return -1;
		decl.typeEnd = i;
		if (decl.typeEnd <= decl.typeStart) return -1; // must have a type

		decl.hasDefault = false;
		decl.assignIdx = -1;
		decl.defaultStart = decl.defaultEnd = -1;
		if (tokens.at(i).token == etASSIGN || isWord(tokens.at(i), L"DEFAULT"))
		{
			decl.hasDefault = true;
			decl.assignIdx = i;
			i = skipEol(tokens, i + 1, rangeEnd);
			decl.defaultStart = i;
			parenDepth = 0;
			while (i < rangeEnd)
			{
				EToken t = tokens.at(i).token;
				if (t == etEOL) { ++i; continue; }
				if (t == etCOMMENT && isEndLineComment(tokens.at(i))) return -1;
				if (t == etLEFT_ROUND_BRACKET)  { ++parenDepth; ++i; continue; }
				if (t == etRIGHT_ROUND_BRACKET) { if (parenDepth > 0) --parenDepth; ++i; continue; }
				if (parenDepth == 0 && (t == etSEMICOLON || t == etEOS)) break;
				++i;
			}
			decl.defaultEnd = i;
			if (decl.defaultEnd <= decl.defaultStart) return -1;
		}
		if (i >= rangeEnd || tokens.at(i).token != etSEMICOLON) return -1;
		decl.semiIdx = i;
		return skipEol(tokens, i + 1, rangeEnd);
	}

	// Emits one variable declaration's default value: everything between the
	// ":=" (or DEFAULT) token and the terminating ';'.
	//
	// The aligned prefix — name, optional CONSTANT, type, ":=" — is already on
	// the output line when this runs, so the expression emitters are entered
	// MID-LINE: FormatWriter::BeginWrappedLine() /
	// WriteCurrentIndentIfLineStart() skip the second indent and start the width
	// packing at the real column. Continuation lines are unaffected — they are
	// still "declaration indent + 1 level" (or the AlignParen column) — so a long
	// default now wraps exactly like the same expression in a BEGIN…END
	// statement instead of running on past the right margin:
	//
	//     DECLARE
	//       lv_var INTEGER := func(param, param, param, param, param, param, param,
	//         param, param, param, param, param
	//       );
	//     BEGIN
	//       lv_var := func(param, param, param, param, param, param, param, param, param,
	//         param, param, param
	//       );
	//
	// That used to be a flat joinTokensSingleSpaced() + WriteText(), which no
	// width limit applied to at all: the identical call wrapped in the body and
	// stayed on one 109-column line in the declaration section.
	//
	// Precedence is emitStatementSequence()'s: math/bool first (§9.1/§9.2 — it
	// owns its own wrapping and treats nested calls as opaque groups), then a
	// bare over-wide call (§9.3), then the '||' chain (§8). emitConcatAware-
	// Statement() always emits, so it is also the flat fallback for a default
	// with nothing wrappable in it; its flat path applies the same
	// needSpaceBetween() spacing as the join it replaces, byte for byte.
	//
	// `children` is the enclosing block's child list. It supplies the ParamList
	// scopes the §9.3 branch needs (buildTree records one for a call in a
	// declaration just as it does in a statement) and the BetweenExpr scopes
	// emitMathBoolAwareStatement() must not break at their AND.
	static void emitVarDeclDefault(
		const std::vector<OpenEditor::Token>& tokens,
		const VarDecl& d,
		const FormatterPolicy& pol,
		const std::vector<FormatContext>* children,
		int indentLevel,
		FormatWriter& fw)
	{
		const int start = d.defaultStart;
		const int end   = d.defaultEnd - 1; // inclusive; the ';' belongs to the caller
		if (start > end) return;

		// §9.1/§9.2 — top-level arithmetic / boolean operators.
		if (emitMathBoolAwareStatement(tokens, start, end, indentLevel, pol, fw, children))
			return;

		// §9.3 — a bare call whose parameter list is over-wide. The leftmost
		// ParamList child inside the default is that call's '('. A top-level '||'
		// before it defers to the concat emitter below, exactly as
		// emitStatementSequence() does (F4 in
		// md/sql_plsql_formatter_integration_plan.md): emitParamListStatement()
		// emits its prefix flat, so claiming such a default would leave the
		// chain in front of the call unwrapped.
		const FormatContext* innerParam = nullptr;
		if (children)
		{
			for (const auto& ch : *children)
			{
				if (ch.kind != FormatContext::ParamList) continue;
				if (ch.openSrcIdx < start || ch.openSrcIdx > end) continue;
				if (!innerParam || ch.openSrcIdx < innerParam->openSrcIdx)
					innerParam = &ch;
			}
		}
		if (innerParam)
		{
			int depth = 0;
			for (int j = start; j < innerParam->openSrcIdx; ++j)
			{
				EToken t = tokens.at(j).token;
				if      (t == etLEFT_ROUND_BRACKET)  ++depth;
				else if (t == etRIGHT_ROUND_BRACKET) { if (depth > 0) --depth; }
				else if (t == etCONCAT_OP && depth == 0)
				{
					innerParam = nullptr; // not a bare call — the concat emitter owns it
					break;
				}
			}
		}
		// The width test counts what the caller has already put on this line —
		// the aligned "name TYPE :=" prefix — because FmtPlsqlParamListMaxLen is
		// a line limit and emitParamListStatement() measures its own flatW the
		// same way. The two must agree, or a default that would wrap is never
		// offered to the emitter and stays on one over-wide line.
		if (innerParam && pol.plsqlParamListMaxLen > 0
		&&  measureFlat(tokens, start, end) + (int)fw.CurrentLineWidth()
			> pol.plsqlParamListMaxLen)
		{
			const int parenOpenIdx  = innerParam->openSrcIdx;
			const int parenCloseIdx = innerParam->closeSrcIdx;
			// Anything after ')' — the ".field" of "rec_tab(i).field", say — is
			// the emitter's suffix. Leaving it out of the call would drop those
			// tokens and trip EMIT_RANGES_GUARD.
			int suffixStart = (parenCloseIdx >= 0 && parenCloseIdx < end)
							? skipEol(tokens, parenCloseIdx + 1, end + 1)
							: end + 1;
			const int suffixEnd = end;
			if (suffixStart > suffixEnd) suffixStart = suffixEnd + 1; // empty

			emitParamListStatement(tokens, start, parenOpenIdx,
				(parenCloseIdx >= 0 ? parenCloseIdx : end),
				suffixStart, suffixEnd, indentLevel, pol, fw);
			return;
		}

		// §8 — a '||' chain, and the flat fallback for everything else.
		emitConcatAwareStatement(tokens, start, end, indentLevel, pol, fw);
	}

	// Emits a group of consecutive simple variable/constant declarations,
	// column-aligning the name, optional CONSTANT marker, type, and optional
	// ":= default" across the whole group (common PL/SQL style-guide layout).
	// The default value itself is emitted by emitVarDeclDefault(), so it wraps
	// under the same width limits as the same expression in a statement;
	// `children` is the enclosing block's child list, which that helper reads
	// the ParamList/BetweenExpr scopes off.
	void emitVarDeclGroup(
		const std::vector<OpenEditor::Token>& tokens,
		const std::vector<VarDecl>& group,
		const FormatterPolicy& pol,
		const std::vector<FormatContext>* children,
		int indentLevel,
		FormatWriter& fw)
	{
		size_t nameWidth = 0, typeWidth = 0, constantWidth = 0;
		bool anyConstant = false;
		// No `defaults` vector: the default value is emitted token by token by
		// emitVarDeclDefault() below, because it may have to wrap. Only the name
		// and the type take part in the column alignment, and both are single
		// physical lines by construction.
		std::vector<std::wstring> names(group.size()), types(group.size());

		for (size_t g = 0; g < group.size(); ++g)
		{
			const VarDecl& d = group.at(g);
			names.at(g) = tokens.at(d.nameIdx).value;
			types.at(g) = joinTokensSingleSpaced(tokens, d.typeStart, d.typeEnd);
			nameWidth = std::max(nameWidth, names.at(g).size());
			typeWidth = std::max(typeWidth, types.at(g).size());
			if (d.constantIdx >= 0)
			{
				anyConstant = true;
				constantWidth = std::max(constantWidth, tokens.at(d.constantIdx).value.size());
			}
		}

		for (size_t g = 0; g < group.size(); ++g)
		{
			const VarDecl& d = group.at(g);
			fw.WriteCurrentIndent();
			fw.WriteText(names.at(g));
			fw.WriteSpaces(nameWidth - names.at(g).size() + 1);
			if (anyConstant)
			{
				std::wstring marker = d.constantIdx >= 0 ? tokens.at(d.constantIdx).value : L"";
				fw.WriteText(marker);
				fw.WriteSpaces(constantWidth - marker.size() + 1);
			}
			fw.WriteText(types.at(g));
			if (d.hasDefault)
			{
				fw.WriteSpaces(typeWidth - types.at(g).size() + 1);
				fw.WriteToken(tokens.at(d.assignIdx));
				fw.WriteSpaces(1);
				// Entered mid-line: the aligned prefix above is already on this
				// output line, so the emitters must not write an indent of their
				// own. See emitVarDeclDefault().
				emitVarDeclDefault(tokens, d, pol, children, indentLevel, fw);
			}
			fw.WriteText(L";\n");
		}
	}

	// Parses tokens[pos..rangeEnd) as "CURSOR name [(params)] IS select...;".
	// On success returns the index just past the ';' (and trailing EOLs).
	int tryParseCursorDecl(
		const std::vector<OpenEditor::Token>& tokens,
		int pos, int rangeEnd,
		CursorDecl& decl)
	{
		decl.headerComments.clear();
		int i = skipEolAndComments(tokens, pos, rangeEnd, &decl.headerComments);
		if (i >= rangeEnd || tokens.at(i).token != etCURSOR) return -1;
		decl.cursorIdx = i;
		i = skipEolAndComments(tokens, i + 1, rangeEnd, &decl.headerComments);
		if (i >= rangeEnd || tokens.at(i).token != etUNKNOWN) return -1;
		decl.nameIdx = i;
		i = skipEolAndComments(tokens, i + 1, rangeEnd, &decl.headerComments);

		decl.paramStart = decl.paramEnd = -1;
		if (i < rangeEnd && tokens.at(i).token == etLEFT_ROUND_BRACKET)
		{
			decl.paramStart = i;
			int parenDepth = 0;
			while (i < rangeEnd)
			{
				if (tokens.at(i).token == etLEFT_ROUND_BRACKET) ++parenDepth;
				else if (tokens.at(i).token == etRIGHT_ROUND_BRACKET)
				{
					--parenDepth;
					if (parenDepth == 0) { ++i; break; }
				}
				++i;
			}
			decl.paramEnd = i;
			i = skipEolAndComments(tokens, i, rangeEnd, &decl.headerComments);
		}

		if (i >= rangeEnd || tokens.at(i).token != etIS) return -1;
		decl.isIdx = i;
		i = skipEolAndComments(tokens, i + 1, rangeEnd, &decl.headerComments);
		decl.selectStart = i;

		int parenDepth = 0;
		while (i < rangeEnd)
		{
			EToken t = tokens.at(i).token;
			if (t == etLEFT_ROUND_BRACKET) ++parenDepth;
			else if (t == etRIGHT_ROUND_BRACKET) { if (parenDepth > 0) --parenDepth; }
			else if (parenDepth == 0 && (t == etSEMICOLON || t == etEOS)) break;
			++i;
		}
		if (i >= rangeEnd || decl.selectStart >= i) return -1;
		decl.semiIdx = i;
		return skipEol(tokens, i + 1, rangeEnd);
	}

	int tryParseRecordTypeDecl(
		const std::vector<OpenEditor::Token>& tokens,
		int pos, int rangeEnd,
		RecordTypeDecl& decl)
	{
		decl = RecordTypeDecl{};
		int i = skipEolAndComments(tokens, pos, rangeEnd, &decl.headerComments);
		// isTypeKeyword(), not a bare etTYPE test: the attribute name of a
		// "%TYPE" datatype attribute reference lexes as etTYPE too, and it can
		// never open a "TYPE name IS RECORD (…)" declaration.
		if (i >= rangeEnd || !isTypeKeyword(tokens, i)) return -1;
		decl.typeIdx = i;
		i = skipEolAndComments(tokens, i + 1, rangeEnd, &decl.headerComments);
		if (i >= rangeEnd || tokens.at(i).token != etUNKNOWN) return -1;
		decl.nameIdx = i;
		i = skipEolAndComments(tokens, i + 1, rangeEnd, &decl.headerComments);
		if (i >= rangeEnd || tokens.at(i).token != etIS) return -1;
		decl.isIdx = i;
		i = skipEolAndComments(tokens, i + 1, rangeEnd, &decl.headerComments);
		if (i >= rangeEnd || tokens.at(i).token != etRECORD) return -1;
		decl.recordIdx = i;
		i = skipEolAndComments(tokens, i + 1, rangeEnd, &decl.headerComments);
		if (i >= rangeEnd || tokens.at(i).token != etLEFT_ROUND_BRACKET) return -1;
		decl.parenStart = i + 1; // first token inside '('

		int parenDepth = 1;
		++i;
		while (i < rangeEnd && parenDepth > 0)
		{
			if      (tokens.at(i).token == etLEFT_ROUND_BRACKET)  ++parenDepth;
			else if (tokens.at(i).token == etRIGHT_ROUND_BRACKET) { if (--parenDepth == 0) break; }
			++i;
		}
		if (i >= rangeEnd) return -1;
		decl.parenEnd = i; // index of the closing ')'

		i = skipEolAndComments(tokens, i + 1, rangeEnd);
		if (i >= rangeEnd || (tokens.at(i).token != etSEMICOLON && tokens.at(i).token != etEOS)) return -1;
		decl.semiIdx = i;

		// Split field list by top-level commas. The comments
		// skipEolAndComments() steps over are handed back through Field::lead
		// (its documented contract) instead of being discarded, and a chunk
		// that turns out to hold nothing but comments is still pushed - with an
		// empty [start,end] range - so those comments reach the emitter too.
		// Dropping them deleted every comment a caller wrote right after
		// "RECORD (" or between two fields:
		//
		//     type krec_typ is record(   -- $K row type
		//       docid number, textkey rowid);
		//
		// This mirrors splitAtTopLevelCommas(), which does the same for a
		// call's argument list.
		auto makeField = [&tokens](int rawStart, int end)
		{
			RecordTypeDecl::Field f;
			f.start = skipEolAndComments(tokens, rawStart, end, &f.lead);
			f.end   = end;
			return f;
		};

		int fStart = decl.parenStart;
		int depth  = 0;
		for (int j = decl.parenStart; j < decl.parenEnd; ++j)
		{
			EToken t = tokens.at(j).token;
			if      (t == etLEFT_ROUND_BRACKET)  ++depth;
			else if (t == etRIGHT_ROUND_BRACKET) --depth;
			else if (t == etCOMMA && depth == 0)
			{
				RecordTypeDecl::Field f = makeField(fStart, j);
				if (f.start < j || !f.lead.empty()) decl.fields.push_back(f);
				fStart = j + 1;
			}
		}
		{
			RecordTypeDecl::Field f = makeField(fStart, decl.parenEnd);
			if (f.start < decl.parenEnd || !f.lead.empty()) decl.fields.push_back(f);
		}
		if (decl.fields.empty()) return -1;

		return skipEol(tokens, i + 1, rangeEnd);
	}

	void emitRecordTypeDecl(
		const std::vector<OpenEditor::Token>& tokens,
		const RecordTypeDecl& decl,
		const FormatterPolicy& pol,
		int indentLevel,
		FormatWriter& fw)
	{
#ifdef _DEBUG
		// The whole declaration is this emitter's to reproduce. headerComments
		// all sit inside [typeIdx,semiIdx] - a comment before TYPE is a declare
		// section item of its own, emitted by the caller - so one contiguous
		// range is the oracle's expected text.
		EMIT_RANGE_GUARD(tokens, decl.typeIdx, decl.semiIdx, fw);
#endif

		fw.WriteCurrentIndent();
		const int headIndent = fw.CurrentIndentChars();

		// Comment placement, shared by the header, the field list and the tail
		// below: a comment sharing its source line with what precedes it stays
		// on that output line (the classic "RECORD (   -- $K row type" and
		// "docid number,   -- the id"); any other one gets a line of its own at
		// `indentChars`. Same rule emitSubprogramHeader() applies to a
		// parameter list.
		bool lineOpen    = true;
		int  prevSrcLine = tokens.at(decl.typeIdx).line;
		auto emitComment = [&](int ci, int indentChars)
		{
			const auto& ctk = tokens.at(ci);
			if (lineOpen && ctk.line == prevSrcLine)
				fw.WriteSpaces(1);
			else
			{
				fw.NewLine();
				fw.WriteSpaces(indentChars);
			}
			fw.WriteToken(ctk);
			prevSrcLine = ctk.line;
			lineOpen    = !isEndLineComment(ctk);
		};

		// "TYPE name IS RECORD" goes through the comment-aware joiner, so a
		// comment embedded in the header cannot swallow the token written after
		// it. Byte-identical to writing the four tokens by hand when the header
		// carries no comment: needSpaceBetween() puts exactly one space between
		// TYPE/name/IS/RECORD.
		lineOpen = emitJoinedKeepingComments(tokens, decl.typeIdx, decl.parenStart - 1,
											 headIndent, fw);
		// The '(' follows on the same line, unless a "--" comment closed that
		// line - appending it there would hide it inside the comment. It is
		// written by hand because needSpaceBetween() gives it no space (RECORD
		// carries reserved == 0, so the datatype-paren rule that keeps "IN ("
		// apart does not fire) while the house style for a RECORD header is
		// "RECORD (".
		if (lineOpen)
			fw.WriteSpaces(1);
		else
		{
			fw.NewLine();
			fw.WriteSpaces(headIndent);
		}
		fw.WriteToken(tokens.at(decl.parenStart - 1));

		lineOpen    = true;
		prevSrcLine = tokens.at(decl.parenStart - 1).line; // the '('

		{
			IndentGuard fieldGuard(fw); // fields are one level deeper than the RECORD header
			const int fieldIndent = fw.CurrentIndentChars();

			// Per-chunk facts for the emission pass below, plus the field-name
			// column width. A chunk holding a comment inside its own text cannot
			// go through the aligned name/type layout - column padding cannot
			// keep that comment in place - so it is replayed verbatim instead,
			// the same rawFallback emitSubprogramHeader() uses, and it must not
			// widen the column. A comment-only chunk (start >= end) holds no
			// field at all: no ',' and no width.
			struct Chunk { int nameIdx = -1, lastSig = -1; bool hasComment = false; };
			std::vector<Chunk> chunks(decl.fields.size());
			size_t fieldCount = 0, maxNameLen = 0;
			for (size_t fi = 0; fi < decl.fields.size(); ++fi)
			{
				const auto& f = decl.fields.at(fi);
				const int ni = skipEolAndComments(tokens, f.start, f.end);
				if (ni >= f.end) continue; // nothing but comments - they are in f.lead
				Chunk& ch  = chunks.at(fi);
				ch.nameIdx = ni;
				++fieldCount;
				for (int j = ni; j < f.end; ++j)
				{
					EToken t = tokens.at(j).token;
					if      (t == etEOL)     continue;
					else if (t == etCOMMENT) { ch.hasComment = true; continue; }
					ch.lastSig = j; // last significant token of the field's text
				}
				if (!ch.hasComment)
					maxNameLen = std::max(maxNameLen, tokens.at(ni).value.size());
			}

			size_t fieldsDone = 0;
			for (size_t fi = 0; fi < decl.fields.size(); ++fi)
			{
				const auto&  f  = decl.fields.at(fi);
				const Chunk& ch = chunks.at(fi);

				// 1) Comments preceding this field.
				for (int ci : f.lead) emitComment(ci, fieldIndent);
				if (ch.nameIdx < 0) continue; // comment-only chunk: its lead was all

				// 2) The field itself - one per line.
				fw.NewLine();
				fw.WriteCurrentIndent();
				lineOpen    = true;
				prevSrcLine = tokens.at(ch.lastSig).line;
				const bool needComma = (++fieldsDone < fieldCount);

				if (!ch.hasComment)
				{
					const std::wstring& name = tokens.at(ch.nameIdx).value;
					std::wstring rest = joinTokensSingleSpaced(tokens, ch.nameIdx + 1, f.end);
					std::wstring padding(maxNameLen - name.size(), L' ');
					fw.WriteText(name + padding + L' ' + rest);
					if (needComma) fw.WriteText(L",");
				}
				else
				{
					// Verbatim replay, comments included. The ',' goes right
					// after the last significant token - written after a
					// trailing "--" comment it would be swallowed by it and lost
					// on re-parse - and the comments that followed the field's
					// text then come after the ','.
					lineOpen = emitJoinedKeepingComments(tokens, ch.nameIdx, ch.lastSig + 1,
														 fieldIndent, fw);
					if (needComma) fw.WriteText(L",");
					for (int j = ch.lastSig + 1; j < f.end; ++j)
						if (tokens.at(j).token == etCOMMENT) emitComment(j, fieldIndent);
				}
			}
		}

		// Close the list, then the terminator. Comments between ')' and the
		// terminator are replayed as well, and the terminator goes on a line of
		// its own when a "--" comment closed the line it would otherwise attach
		// to. It is emitted from its token, never synthesized as L";" (see
		// emitForwardDeclSpan): a script '/' reaches the formatter as etEOS
		// while keeping its "/" value and must stand alone on its own line.
		fw.NewLine();
		fw.WriteCurrentIndent();
		fw.WriteText(L")");
		lineOpen    = true;
		prevSrcLine = tokens.at(decl.parenEnd).line;
		for (int j = decl.parenEnd + 1; j < decl.semiIdx; ++j)
			if (tokens.at(j).token == etCOMMENT) emitComment(j, headIndent);

		const auto& termTok = tokens.at(decl.semiIdx);
		if (!lineOpen || classifyTerminator(termTok) == FormatContext::TermSlash)
		{
			fw.NewLine();
			fw.WriteCurrentIndent();
		}
		fw.WriteToken(termTok);
		fw.NewLine();
	}

	// -----------------------------------------------------------------------
	// TYPE name IS TABLE OF <elem_type> [NOT NULL] [INDEX BY <idx_type>];
	// TYPE name IS VARRAY(n) OF <elem_type>;
	// -----------------------------------------------------------------------
	// Helper: return upper-cased copy of a wide string.
	static std::wstring toUpper(std::wstring s)
	{
		for (wchar_t& c : s) c = towupper(c);
		return s;
	}

	int tryParseCollectionTypeDecl(
		const std::vector<OpenEditor::Token>& tokens,
		int pos, int rangeEnd,
		CollectionTypeDecl& decl)
	{
		decl = CollectionTypeDecl{};
		int i = skipEolAndComments(tokens, pos, rangeEnd, &decl.headerComments);
		// isTypeKeyword(), not a bare etTYPE test — see tryParseRecordTypeDecl().
		if (i >= rangeEnd || !isTypeKeyword(tokens, i)) return -1;
		decl.typeIdx = i;
		i = skipEolAndComments(tokens, i + 1, rangeEnd, &decl.headerComments);
		if (i >= rangeEnd || tokens.at(i).token != etUNKNOWN) return -1;
		decl.nameIdx = i;
		i = skipEolAndComments(tokens, i + 1, rangeEnd, &decl.headerComments);
		if (i >= rangeEnd || tokens.at(i).token != etIS) return -1;
		decl.isIdx = i;
		i = skipEolAndComments(tokens, i + 1, rangeEnd, &decl.headerComments);
		if (i >= rangeEnd || tokens.at(i).token != etUNKNOWN) return -1;

		std::wstring kind = toUpper(tokens.at(i).value);
		if (kind != L"TABLE" && kind != L"VARRAY") return -1;
		decl.kindIdx = i;
		i = skipEolAndComments(tokens, i + 1, rangeEnd, &decl.headerComments);

		if (kind == L"VARRAY")
		{
			// VARRAY(n)
			if (i >= rangeEnd || tokens.at(i).token != etLEFT_ROUND_BRACKET) return -1;
			decl.vsizeStart = i + 1;
			int depth = 1; ++i;
			while (i < rangeEnd && depth > 0)
			{
				if      (tokens.at(i).token == etLEFT_ROUND_BRACKET)  ++depth;
				else if (tokens.at(i).token == etRIGHT_ROUND_BRACKET) { if (--depth == 0) break; }
				++i;
			}
			if (i >= rangeEnd) return -1;
			decl.vsizeEnd = i; // index of ')'
			i = skipEolAndComments(tokens, i + 1, rangeEnd, &decl.headerComments);
		}

		// Expect OF keyword (etUNKNOWN because it is not in the keyword map).
		if (i >= rangeEnd || toUpper(tokens.at(i).value) != L"OF") return -1;
		decl.ofIdx = i;
		i = skipEolAndComments(tokens, i + 1, rangeEnd, &decl.headerComments);
		decl.elemStart = i;

		// Consume tokens until top-level ';'.
		int depth = 0;
		while (i < rangeEnd)
		{
			EToken t = tokens.at(i).token;
			if      (t == etLEFT_ROUND_BRACKET)  ++depth;
			else if (t == etRIGHT_ROUND_BRACKET) { if (depth > 0) --depth; }
			else if ((t == etSEMICOLON || t == etEOS) && depth == 0) break;
			++i;
		}
		if (i >= rangeEnd || decl.elemStart >= i) return -1;
		decl.elemEnd  = i;
		decl.semiIdx  = i;
		return skipEol(tokens, i + 1, rangeEnd);
	}

	void emitCollectionTypeDecl(
		const std::vector<OpenEditor::Token>& tokens,
		const CollectionTypeDecl& decl,
		FormatWriter& fw,
		int indentLevel)
	{
		fw.WriteCurrentIndent();
		fw.WriteToken(tokens.at(decl.typeIdx));
		fw.WriteSpaces(1);
		fw.WriteToken(tokens.at(decl.nameIdx));
		fw.WriteSpaces(1);
		fw.WriteToken(tokens.at(decl.isIdx));
		fw.WriteSpaces(1);
		fw.WriteToken(tokens.at(decl.kindIdx));

		if (decl.vsizeStart >= 0)
		{
			// VARRAY(n)
			fw.WriteText(L"(");
			fw.WriteText(joinTokensSingleSpaced(tokens, decl.vsizeStart, decl.vsizeEnd));
			fw.WriteText(L")");
		}

		fw.WriteSpaces(1);
		fw.WriteToken(tokens.at(decl.ofIdx));
		fw.WriteSpaces(1);
		fw.WriteText(joinTokensSingleSpaced(tokens, decl.elemStart, decl.elemEnd));
		fw.WriteText(L";\n");
	}

	// Emits "CURSOR name [(params)] IS" followed by the query (delegated to
	// the existing, unmodified formatSQL()) and the closing ";".
	// Any comments collected in decl.headerComments that fall between IS and
	// the SELECT body are emitted on their own lines (at body indent) before
	// the formatted query, preserving them losslessly.
	void emitCursorDecl(
		const std::vector<OpenEditor::Token>& tokens,
		const CursorDecl& decl,
		const FormatterPolicy& pol,
		int indentLevel,
		FormatWriter& fw)
	{
		fw.WriteCurrentIndent();
		fw.WriteToken(tokens.at(decl.cursorIdx));
		fw.WriteSpaces(1);
		fw.WriteToken(tokens.at(decl.nameIdx));
		if (decl.paramStart >= 0)
		{
			fw.WriteSpaces(1);
			fw.WriteText(joinTokensSingleSpaced(tokens, decl.paramStart, decl.paramEnd));
		}
		fw.WriteSpaces(1);
		fw.WriteToken(tokens.at(decl.isIdx));
		fw.NewLine();

		// Emit any comments that appeared between IS and the SELECT body.
		// They were skipped by tryParseCursorDecl to find selectStart, but
		// must still appear in the output to preserve losslessness.
		{
			IndentGuard bodyGuard(fw); // comments sit at the query-body indent level
			for (int ci : decl.headerComments)
			{
				if (ci > decl.isIdx && ci < decl.selectStart)
				{
					fw.WriteCurrentIndent();
					fw.WriteToken(tokens.at(ci));
					fw.NewLine();
				}
			}
		}

		size_t subEnd = std::min((size_t)decl.semiIdx, tokens.size());
		std::vector<OpenEditor::Token> sub(tokens.begin() + decl.selectStart, tokens.begin() + subEnd);
		appendEosSentinel(sub);
		// E2b — token-run delegation. The hand-rolled trailing-whitespace erase
		// becomes trimTrailingWs()'s run overload; the two agree on every
		// non-empty run and differ only for an all-whitespace one, which cannot
		// occur here because `sub` always carries at least the SELECT.
		// SpliceInto() re-indents the run exactly as WriteIndentText() did, but
		// the nested formatter's token types survive instead of being re-split
		// into etUNKNOWN text by AppendText().
		//
		// concatTokenValues() is the guard's text oracle — the characters
		// formatSQL() used to return — and is inlined into its two debug-only
		// uses rather than bound to a local: SQL_SLICE_GUARD and TRACE both
		// compile away in Release, so a named local would be an unreferenced
		// variable there.
		std::vector<OpenEditor::Token> formatted = formatSQL(sub, pol);
		SQL_SLICE_GUARD(sub, concatTokenValues(formatted));
		trimTrailingWs(formatted);
#ifdef _DEBUG
		TRACE(L"emitCursorDecl %s", concatTokenValues(formatted).c_str());
#endif
		fw.SpliceInto(formatted, fw.GetNextIndentChars());

		fw.WriteToken(tokens.at(decl.semiIdx));
		fw.NewLine();
	}

	// Returns the index just past the header (past IS/AS, or past ';' for
	// forward decls).  Returns -1 if the tokens at pos don't form a valid
	// nested subprogram header.
	//
	// Step 2.1 note: buildTree() now records every PROCEDURE/FUNCTION header
	// that terminates at ';' or a script '/' without an IS/AS as an explicit
	// SubprogramForwardDecl node, and the emitters dispatch on that node first
	// (findForwardDeclChild + emitSubprogramForwardDecl). The isForwardDecl
	// path below is therefore only a *fallback* for spans where buildTree
	// produced no node — e.g. a header inside parentheses (pending-header
	// tracking requires parenDepth == 0), or malformed input where the pending
	// state was cleared by an intervening DECLARE/BEGIN. It must stay in sync
	// with emitForwardDeclSpan() so both routes produce identical output.
	int tryParseNestedSubprogramHeader(
		const std::vector<OpenEditor::Token>& tokens,
		int pos, int rangeEnd,
		NestedSubprogramHeader& hdr)
	{
		hdr = NestedSubprogramHeader{};
		int i = skipEolAndComments(tokens, pos, rangeEnd, &hdr.headerComments);
		if (i >= rangeEnd) return -1;

		int headerBegin = i; // remember true start, before CREATE [OR REPLACE]

		// Skip an optional "CREATE [OR REPLACE]" prefix — a standalone
		// top-level "CREATE [OR REPLACE] FUNCTION/PROCEDURE ..." selection
		// (or truncated/malformed body) must be recognized the same way as
		// a bare "FUNCTION/PROCEDURE ..." nested declaration.
		if (i < rangeEnd && tokens.at(i).token == etCREATE)
		{
			i = skipEolAndComments(tokens, i + 1, rangeEnd, &hdr.headerComments);
			if (i < rangeEnd && tokens.at(i).token == etOR)
			{
				i = skipEolAndComments(tokens, i + 1, rangeEnd, &hdr.headerComments);
				if (i >= rangeEnd || tokens.at(i).token != etREPLACE) return -1;
				i = skipEolAndComments(tokens, i + 1, rangeEnd, &hdr.headerComments);
			}
		}

		// Skip optional method-modifier prefix tokens:
		// OVERRIDING, MEMBER, STATIC, CONSTRUCTOR, MAP, ORDER, NOT, FINAL,
		// INSTANTIABLE — any of these may appear before PROCEDURE/FUNCTION.
		// Step 2.1: the predicate now lives in isSubprogramModifierWord()
		// (OEPlsSqlFormatter.h/.cpp) so buildTree()'s backward span extension
		// for SubprogramForwardDecl nodes and findForwardDeclChild() classify
		// modifiers exactly the same way — one source of truth.
		hdr.prefixStart = headerBegin; // header always starts at CREATE (or first modifier)
		while (i < rangeEnd && isSubprogramModifierWord(tokens.at(i)))
			i = skipEolAndComments(tokens, i + 1, rangeEnd, &hdr.headerComments);

		EToken kw = tokens.at(i).token;
		if (kw != etPROCEDURE && kw != etFUNCTION) return -1;
		hdr.kwIdx = i;
		i = skipEolAndComments(tokens, i + 1, rangeEnd, &hdr.headerComments);
		if (i >= rangeEnd || tokens.at(i).token != etUNKNOWN) return -1;
		hdr.nameIdx = i;
		i = skipEolAndComments(tokens, i + 1, rangeEnd, &hdr.headerComments);

		// Optional parameter list.
		if (i < rangeEnd && tokens.at(i).token == etLEFT_ROUND_BRACKET)
		{
			hdr.paramStart = i;
			int depth = 0;
			while (i < rangeEnd)
			{
				if      (tokens.at(i).token == etLEFT_ROUND_BRACKET)  ++depth;
				else if (tokens.at(i).token == etRIGHT_ROUND_BRACKET) { if (--depth == 0) { ++i; break; } }
				++i;
			}
			hdr.paramEnd = i; // first token after ')'
			i = skipEolAndComments(tokens, i, rangeEnd, &hdr.headerComments);
		}

		// Scan forward to find IS/AS or ';' (skipping RETURN type tokens).
		// Guard: "RETURN SELF AS RESULT" — the AS here is part of the
		// constructor return-type clause, not the body opener.  Skip any AS
		// whose *next* non-EOL/non-comment token is the word RESULT.
		while (i < rangeEnd)
		{
			EToken t = tokens.at(i).token;
			if (t == etAS)
			{
				// Look forward for RESULT.
				bool nextIsResult = false;
				for (int j = i + 1; j < rangeEnd; ++j)
				{
					if (tokens.at(j).token == etEOL || tokens.at(j).token == etCOMMENT) continue;
					if (tokens.at(j).token == etUNKNOWN
					&&  !_wcsicmp(tokens.at(j).value.c_str(), L"RESULT"))
						nextIsResult = true;
					break;
				}
				if (!nextIsResult) { hdr.isIdx = i; return i + 1; }
				// else: this AS belongs to "SELF AS RESULT" — skip it
			}
			else if (t == etIS)    { hdr.isIdx = i; return i + 1; }
			else if (t == etSEMICOLON || t == etEOS)
			{
				hdr.semiIdx = i;
				hdr.isForwardDecl = true;
				return skipEol(tokens, i + 1, rangeEnd);
			}
			++i;
		}
		return -1;
	}

	// ------------------------------------------------------------------
	// Comment-aware flat emission lives in OEPlsSqlFormatterCommon.cpp as the
	// shared emitJoinedKeepingComments(); emitSubprogramHeader() below and
	// emitParamListStatement() in OEPlsSqlFormatterStmts.cpp both use it. It
	// was local to this file until a call's argument list turned out to need
	// exactly the same "--"-comment line breaking.
	// ------------------------------------------------------------------

	// One parameter of a subprogram header's parameter list, as rebuilt by
	// emitSubprogramHeader()'s wrapped (one-parameter-per-line) layout.
	struct HeaderParam
	{
		int start = -1;        // first significant token of the parameter
		int end = -1;          // exclusive: index of the ',' or ')' closing it
		int typeStart = -1;
		int typeEnd = -1;
		std::wstring name, mode;
		std::vector<int> lead;    // comments before `start`, in source order
		bool commentOnly = false; // chunk holds comments but no parameter text
		bool rawFallback = false; // a comment sits inside the parameter text
		bool isEmpty = false;     // nothing at all (e.g. ",,") — skip entirely
	};

	// Parses the parameter chunk [rawStart,end) of a header's parameter list
	// (end = index of the ',' or ')' that closes it).
	//
	// Comments are never dropped. This layout rebuilds each parameter from
	// name/mode/type pieces instead of replaying source tokens, so the comments
	// skipEolAndComments() steps over are handed back for the caller to
	// reproduce (that is the documented contract of its skippedComments
	// out-parameter):
	//  - comments before the first significant token go to `lead`;
	//  - a comment anywhere inside the parameter's own text sets `rawFallback`,
	//    because column alignment cannot keep it in place — the chunk is then
	//    emitted verbatim instead, the same fallback tryParseVarDecl() uses
	//    when a "--" comment makes structural parsing unsafe.
	static HeaderParam parseHeaderParam(
		const std::vector<OpenEditor::Token>& tokens,
		int rawStart, int end)
	{
		HeaderParam pp;
		pp.end = end;
		pp.start = skipEolAndComments(tokens, rawStart, end, &pp.lead);
		if (pp.start >= end)
		{
			pp.commentOnly = !pp.lead.empty();
			pp.isEmpty = pp.lead.empty();
			return pp;
		}

		for (int j = pp.start; j < end; ++j)
			if (tokens.at(j).token == etCOMMENT) { pp.rawFallback = true; break; }

		auto isMode = [](const std::wstring& v) -> bool {
			return !_wcsicmp(v.c_str(), L"IN") || !_wcsicmp(v.c_str(), L"OUT");
		};

		pp.name = tokens.at(pp.start).value;
		int mi = skipEolAndComments(tokens, pp.start + 1, end);
		if (mi < end && isMode(tokens.at(mi).value))
		{
			pp.mode = tokens.at(mi).value;
			int mi2 = skipEolAndComments(tokens, mi + 1, end);
			if (mi2 < end && isMode(tokens.at(mi2).value))
			{
				pp.mode += L' ';
				pp.mode += tokens.at(mi2).value;
				pp.typeStart = skipEolAndComments(tokens, mi2 + 1, end);
			}
			else
				pp.typeStart = mi2;
		}
		else
			pp.typeStart = mi;
		pp.typeEnd = end;
		return pp;
	}


	// Shared helper: emits a subprogram header spanning tokens[headerStart..isIdx].
	// If isIdx >= 0 the IS/AS token is emitted on its own line at the end.
	// If isIdx == -1 no IS/AS line is emitted (used for forward declarations /
	// package spec signatures where the header ends with just ';').
	// Handles optional CREATE [OR REPLACE] prefix, PROCEDURE/FUNCTION/PACKAGE
	// keyword, name, optional parameter list (one param per aligned line),
	// optional RETURN type.
	// Returns true while the output line is still open (the caller may append a
	// terminator or a ',' directly); false when a "--" comment closed the line
	// and anything appended there would be swallowed by that comment.
	bool emitSubprogramHeader(
		const std::vector<OpenEditor::Token>& tokens,
		int headerStart,   // first meaningful token
		int isIdx,         // index of IS/AS token, or -1
		int indentLevel,
		const FormatterPolicy& pol,
		FormatWriter& fw,
		int scanEnd)  // upper bound for paren search; defaults to isIdx when >= 0
	{
		IndentLevelGuard indentGuard(fw, indentLevel);
		int searchEnd = (isIdx >= 0) ? isIdx : (scanEnd >= 0 ? scanEnd : (int)tokens.size());

		// Find the opening '(' of the parameter list (if any).
		int parenOpen  = -1;
		int parenClose = -1;
		{
			int depth = 0;
			for (int j = headerStart; j < searchEnd; ++j)
			{
				if (tokens.at(j).token == etLEFT_ROUND_BRACKET)
				{
					if (depth == 0) parenOpen = j;
					++depth;
				}
				else if (tokens.at(j).token == etRIGHT_ROUND_BRACKET)
				{
					--depth;
					if (depth == 0) { parenClose = j; break; }
				}
			}
		}

		// Tracks whether the output line is still open, i.e. whether the next
		// token may be appended to it. Only an end-of-line comment closes it.
		bool lineOpen = true;

		if (parenOpen < 0 || parenClose < 0)
		{
			// No parameter list — or a malformed one whose ')' never appears
			// before searchEnd (an unterminated string literal swallowed it,
			// truncated input, ...). The wrapped layout must never run with
			// parenClose == -1: parseHeaderParam would index tokens.at(-1)
			// ("invalid vector subscript"). Emit everything before IS/AS (or
			// end) on one line — comment-aware, so a "--" comment cannot
			// swallow IS/AS.
			fw.WriteCurrentIndent();
			lineOpen = emitJoinedKeepingComments(tokens, headerStart, searchEnd,
												fw.CurrentIndentChars(), fw);
		}
		else
		{
			const int  maxLen  = pol.plsqlParamListMaxLen;

			// Measure flat width: headerStart..searchEnd + possible IS/AS (indent excluded).
			// (searchEnd is isIdx, so excludes IS/AS itself — add IS/AS word later.)
			int flatW = measureFlat(tokens, headerStart, searchEnd - 1);
			// Also account for the IS/AS word on the same line if wrapping it flat.
			// For now, flat means everything before IS/AS on one line.

			// An end-of-line comment anywhere in the header span rules the flat
			// layout out: joinTokensSingleSpaced() would put that comment and
			// every token joined after it on the same physical line, so the
			// comment swallows them. Same rule as tryParseVarDecl() and the SQL
			// formatter's short paths (which bail out on hasComment): refuse the
			// compact layout, use the multi-line one that replays comments
			// explicitly. Losslessness outranks the length preference, so this
			// applies even when wrapping is disabled (ParamListMaxLen == 0).
			bool spanHasEolComment = false;
			for (int j = headerStart; j < searchEnd; ++j)
				if (tokens.at(j).token == etCOMMENT && isEndLineComment(tokens.at(j)))
				{ spanHasEolComment = true; break; }

			bool doWrap = spanHasEolComment || (maxLen > 0 && flatW > maxLen);

			if (!doWrap)
			{
				// Flat: everything on one line. joinTokensSingleSpaced() suppresses
				// the space before '(' (call-syntax rule), but a subprogram header's
				// parameter list must always have a space before '(' regardless of
				// whether the header is flat or wrapped — otherwise the same
				// declaration formats differently depending on incidental width
				// (e.g. presence/absence of a "CREATE OR REPLACE" prefix).
				fw.WriteCurrentIndent();
				fw.WriteText(joinTokensSingleSpaced(tokens, headerStart, parenOpen));
				fw.WriteSpaces(1);
				fw.WriteText(joinTokensSingleSpaced(tokens, parenOpen, searchEnd));
			}
			else
			{
				// Wrapped: emit prefix (name) + ' (' + one-param-per-line + ')'.
				// Header param lists always use one-parameter-per-line with
				// name/mode/type column alignment (§10.1).
				const bool alignParen = pol.plsqlParamListAlignParen;
				fw.WriteCurrentIndent();
				const int headIndent = fw.CurrentIndentChars();
				lineOpen = emitJoinedKeepingComments(tokens, headerStart, parenOpen,
													headIndent, fw);
				// The '(' follows the prefix on the same line, unless a "--"
				// comment closed that line — appending it there would hide it
				// inside the comment.
				if (lineOpen)
					fw.WriteSpaces(1);
				else
				{
					fw.NewLine();
					fw.WriteSpaces(headIndent);
				}
				fw.WriteToken(tokens.at(parenOpen));
				lineOpen = true;
				int prevSrcLine = tokens.at(parenOpen).line;

				// Split params by top-level commas.
				// When alignParen is set, continuation lines (and the closing
				// ')') line up directly under the column right after the '('
				// emitted above (matching emitParamListStatement's AlignParen
				// convention); otherwise they use a plain one-indent-step from
				// the header.
				int paramIndentChars;
				if (alignParen)
				{
					size_t lineStart = fw.Str().rfind(L'\n');
					lineStart = (lineStart == std::wstring::npos) ? 0 : lineStart + 1;
					paramIndentChars = (int)(fw.Str().size() - lineStart);
				}
				else
					paramIndentChars = fw.GetNextIndentChars();

				// Split at top-level commas and parse each chunk. parseHeaderParam
				// keeps the comments the split steps over, so none is lost.
				std::vector<HeaderParam> parsed;
				{
					int depth = 0, pStart = parenOpen + 1;
					for (int j = parenOpen + 1; j < parenClose; ++j)
					{
						EToken t = tokens.at(j).token;
						if      (t == etLEFT_ROUND_BRACKET)  ++depth;
						else if (t == etRIGHT_ROUND_BRACKET) --depth;
						else if (t == etCOMMA && depth == 0)
						{
							HeaderParam hp = parseHeaderParam(tokens, pStart, j);
							if (!hp.isEmpty) parsed.push_back(hp);
							pStart = j + 1;
						}
					}
					HeaderParam hp = parseHeaderParam(tokens, pStart, parenClose);
					if (!hp.isEmpty) parsed.push_back(hp);
				}

				// Column widths. Only parameters rebuilt from name/mode/type
				// pieces take part in the alignment; verbatim (rawFallback) and
				// comment-only chunks are not aligned, so they must not widen
				// the columns.
				size_t maxNameLen = 0, maxModeLen = 0;
				for (const auto& hp : parsed)
				{
					if (hp.commentOnly || hp.rawFallback) continue;
					maxNameLen = std::max(maxNameLen, hp.name.size());
					maxModeLen = std::max(maxModeLen, hp.mode.size());
				}

				for (size_t pi = 0; pi < parsed.size(); ++pi)
				{
					const auto& pp = parsed.at(pi);

					// 1) Comments preceding this parameter. One that shares its
					//    source line with the '(' or ',' before it stays on that
					//    output line (the classic "p_id IN NUMBER, -- the id");
					//    any other comment gets its own line at the param indent.
					for (int c : pp.lead)
					{
						const auto& ctk = tokens.at(c);
						if (lineOpen && ctk.line == prevSrcLine)
							fw.WriteSpaces(1);
						else
						{
							fw.NewLine();
							fw.WriteSpaces(paramIndentChars);
						}
						fw.WriteToken(ctk);
						prevSrcLine = ctk.line;
						lineOpen = !isEndLineComment(ctk);
					}

					// 2) The parameter itself — one per line, except the first in
					//    AlignParen mode, which shares the '(' line.
					if (!pp.commentOnly)
					{
						if (!(pi == 0 && alignParen && lineOpen))
						{
							fw.NewLine();
							fw.WriteSpaces(paramIndentChars);
						}
						if (pp.rawFallback)
						{
							// A comment sits inside the parameter's own text, which
							// column alignment cannot reproduce: replay the chunk
							// verbatim (comments included) instead.
							lineOpen = emitJoinedKeepingComments(tokens, pp.start, pp.end,
																paramIndentChars, fw);
						}
						else
						{
							fw.WriteText(pp.name);
							if (maxModeLen > 0)
							{
								fw.WriteSpaces((int)(maxNameLen - pp.name.size() + 1));
								fw.WriteText(pp.mode);
								if (!pp.mode.empty())
									fw.WriteSpaces((int)(maxModeLen - pp.mode.size() + 1));
								else
									fw.WriteSpaces((int)(maxModeLen + 1));
							}
							else
							{
								fw.WriteSpaces((int)(maxNameLen - pp.name.size() + 1));
							}
							if (pp.typeStart < pp.typeEnd)
								fw.WriteText(joinTokensSingleSpaced(tokens, pp.typeStart, pp.typeEnd));
							lineOpen = true;
						}
					}

					// 3) The ',' closing this parameter (verbatim token). When a
					//    "--" comment closed the line, the comma starts a new one:
					//    appending it to the comment would lose it.
					if (pi + 1 < parsed.size())
					{
						if (!lineOpen)
						{
							fw.NewLine();
							fw.WriteSpaces(paramIndentChars);
						}
						fw.WriteToken(tokens.at(pp.end));
						prevSrcLine = tokens.at(pp.end).line;
						lineOpen = true;
					}
				}
				// Closing ')': when alignParen is set, it directly follows the
				// last parameter on the same line (matching emitParamListStatement's
				// AlignParen convention); otherwise it goes on its own line at
				// the header's own indent. A line closed by a "--" comment always
				// takes the own-line form.
				if (alignParen && lineOpen)
					fw.WriteToken(tokens.at(parenClose));
				else
				{
					fw.NewLine();
					fw.WriteCurrentIndent();
					fw.WriteToken(tokens.at(parenClose));
				}
				prevSrcLine = tokens.at(parenClose).line;
				lineOpen = true;

				// RETURN type / attributes between ')' and IS/AS — comment-aware
				// for the same swallow reason as the parameter list.
				{
					std::vector<int> retComments;
					int retStart = skipEolAndComments(tokens, parenClose + 1, searchEnd, &retComments);
					for (int c : retComments)
					{
						if (lineOpen && tokens.at(c).line == prevSrcLine)
							fw.WriteSpaces(1);
						else
						{
							fw.NewLine();
							fw.WriteCurrentIndent();
						}
						fw.WriteToken(tokens.at(c));
						prevSrcLine = tokens.at(c).line;
						lineOpen = !isEndLineComment(tokens.at(c));
					}
					if (retStart < searchEnd)
					{
						if (lineOpen)
							fw.WriteSpaces(1);
						else
						{
							fw.NewLine();
							fw.WriteCurrentIndent();
						}
						lineOpen = emitJoinedKeepingComments(tokens, retStart, searchEnd,
															fw.CurrentIndentChars(), fw);
					}
				}
			}
		}

		// IS/AS placement — only when isIdx is valid. By default (FmtPlsqlIsOnNewLine
		// = false) IS/AS stays on the same line as the header; when true, it is
		// broken onto its own line.
		if (isIdx >= 0)
		{
			if (pol.plsqlIsOnNewLine)
			{
				fw.NewLine();
				fw.WriteCurrentIndent();
				fw.WriteToken(tokens.at(isIdx));
				fw.NewLine();
			}
			else
			{
				// A "--" comment may have closed the header line; IS/AS then
				// starts a line of its own instead of being swallowed by it.
				if (lineOpen)
					fw.WriteSpaces(1);
				else
				{
					fw.NewLine();
					fw.WriteCurrentIndent();
				}
				fw.WriteToken(tokens.at(isIdx));
				fw.NewLine();
			}
			// Both branches above close the line right after IS/AS.
			return false;
		}
		// No IS/AS (forward declaration / package spec signature / type-spec
		// member): the caller appends the terminator or the next ',' right after
		// this function returns, so it needs to know whether the output line is
		// still open — anything appended after a "--" comment would be swallowed
		// by the comment text and lost on re-parse.
		return lineOpen;
	}

	// -----------------------------------------------------------------------
	// emitForwardDeclSpan — shared forward-declaration emission core.
	//
	// One source of truth for "emit a subprogram header that has no body":
	// the header span [headerStart, termIdx) is normalized by
	// emitSubprogramHeader() (isIdx == -1, scanEnd == termIdx so the
	// terminator itself is never folded into the header), then the terminator
	// token at termIdx is written.
	//
	// Token fidelity: the terminator is always emitted from
	// tokens[termIdx].value — a ';' attaches directly to the header line, while
	// a script '/' (a lone '/' on its own line, reclassified by the lexer to
	// etEOS while keeping its "/" value) goes on its own line and is never
	// appended to the header. Nothing is synthesized as L";" or L"/".
	//
	// leadingComments (may be null) carries comment token indices that precede
	// headerStart; each is emitted on its own line before the header. Comments
	// *inside* the span are emitted inline by emitSubprogramHeader, exactly as
	// they always were.
	//
	// Used by emitNestedSubprogram()'s isForwardDecl fallback branch (span from
	// tryParseNestedSubprogramHeader) and by emitSubprogramForwardDecl() (span
	// from the SubprogramForwardDecl tree node built by buildTree).
	// -----------------------------------------------------------------------
	static void emitForwardDeclSpan(
		const std::vector<OpenEditor::Token>& tokens,
		int headerStart,
		int termIdx,
		FormatContext::Terminator termHint,
		const std::vector<int>* leadingComments,
		const FormatterPolicy& pol,
		int indentLevel,
		FormatWriter& fw)
	{
		if (leadingComments)
			for (int ci : *leadingComments)
				if (ci < headerStart)
				{
					fw.WriteCurrentIndent();
					fw.WriteToken(tokens.at(ci));
					fw.NewLine();
				}

		// Everything below emits exactly tokens[headerStart..termIdx]. Held in
		// a lambda so the debug fidelity guard can be installed conditionally —
		// it must not run for truncated input (termIdx < 0), where no
		// terminator token exists and the expected span is undefined — without
		// duplicating the emission code.
		auto emitSpan = [&]()
		{
			const bool lineOpen = emitSubprogramHeader(tokens, headerStart, -1, indentLevel, pol, fw, termIdx);

			if (termIdx < 0 || termIdx >= (int)tokens.size())
			{
				// No terminator token (truncated input): just close the header line.
				fw.NewLine();
				return;
			}
			// Step 2.2 — the ';' vs script '/' layout decision now comes from the
			// caller's tree node when there is one (emitSubprogramForwardDecl
			// passes fwd.terminator, recorded by buildTree). The raw-scan fallback
			// route has no node and passes TermNone, so the classification is
			// derived from the token itself — but through classifyTerminator(),
			// which is the single definition of "a lone '/' arrives as etEOS while
			// keeping its value". Token fidelity is unchanged: the character
			// written below is still tokens[termIdx].value, never a literal.
			const FormatContext::Terminator term = (termHint != FormatContext::TermNone)
				? termHint
				: classifyTerminator(tokens.at(termIdx));

			if (term == FormatContext::TermSlash)
			{
				fw.NewLine();
				fw.WriteCurrentIndent();
				fw.WriteToken(tokens.at(termIdx));
				fw.NewLine();
			}
			else
			{
				// A "--" comment at the end of the header closed the line, so the
				// terminator needs one of its own: appended to the comment it would
				// be swallowed by it and lost on re-parse.
				if (!lineOpen)
				{
					fw.NewLine();
					fw.WriteCurrentIndent();
				}
				fw.WriteToken(tokens.at(termIdx));
				fw.NewLine();
			}
		};

#ifdef _DEBUG
		// The leading comments replayed above sit *before* headerStart, so they
		// fall outside [headerStart..termIdx]; listing each as its own one-token
		// range keeps the oracle's expected text in emission order. Skipping
		// them would make every commented forward declaration a false positive.
		if (termIdx >= 0 && termIdx < (int)tokens.size())
		{
			std::vector<std::pair<int, int>> dbgRanges;
			if (leadingComments)
				for (int ci : *leadingComments)
					if (ci < headerStart)
						dbgRanges.push_back({ ci, ci });
			dbgRanges.push_back({ headerStart, termIdx });
			EMIT_RANGES_GUARD(tokens, dbgRanges, fw);
			emitSpan();
			return;
		}
#endif
		emitSpan();
	}

	// Step 2.1 — emits a forward-declared / body-less subprogram header from
	// its SubprogramForwardDecl tree node. The whole span
	// [openSrcIdx..closeSrcIdx] (closeSrcIdx being the ';' or script '/'
	// terminator itself) is owned by the node, so no raw-token re-scanning is
	// needed and the emitter cannot miss a CREATE [OR REPLACE] prefix.
	void emitSubprogramForwardDecl(
		const FormatContext& fwd,
		const std::vector<OpenEditor::Token>& tokens,
		const FormatterPolicy& pol,
		int indentLevel,
		FormatWriter& fw,
		int spanStart)
	{
		IndentLevelGuard indentGuard(fw, indentLevel);

		// spanStart defaults to the node's own span start; when the caller's
		// cursor sits earlier (a method modifier that buildTree's backward
		// extension could not absorb because a comment followed it), start
		// there so the modifier is not dropped.
		int headerStart = (fwd.headerStart >= 0) ? fwd.headerStart : fwd.openSrcIdx;
		if (spanStart >= 0 && spanStart < headerStart)
			headerStart = spanStart;

#ifdef _DEBUG
		// Whole-node coverage guard: the node owns its span outright. The
		// re-parented ParamList/Comment children are emitted inline as part of
		// the header text by emitSubprogramHeader, so the expected text is the
		// full contiguous range rather than the direct-owned subtraction the
		// structural nodes use (same convention as emitPackage's guard).
		//
		// Comments no longer disable the guard: emitSubprogramHeader() replays
		// every comment it meets (parseHeaderParam's `lead` list plus the
		// rawFallback for comments inside a parameter's own text), and the oracle
		// compares whitespace-stripped text, so a comment relocated onto its own
		// line still matches.
		EMIT_RANGE_GUARD(tokens, headerStart, fwd.closeSrcIdx, fw);
#endif

		emitForwardDeclSpan(tokens, headerStart, fwd.closeSrcIdx, fwd.terminator,
							nullptr, pol, indentLevel, fw);
	}

	// Emits a nested subprogram header up to (but not including) IS/AS.
	// Each param (between commas) is placed on its own indented line when
	// Emits a complete nested PROCEDURE/FUNCTION declaration: header + body
	// (or just the header line for a forward declaration).
	void emitNestedSubprogram(
		const FormatContext& declNode,
		const std::vector<OpenEditor::Token>& tokens,
		const NestedSubprogramHeader& hdr,
		int afterIsPos,
		const FormatterPolicy& pol,
		int indentLevel,
		FormatWriter& fw)
	{
		IndentLevelGuard indentGuard(fw, indentLevel);

		if (hdr.isForwardDecl)
		{
			// Spec-only / forward declaration: header without IS/AS, closing ';'.
			// Fallback route — see the Step 2.1 note on
			// tryParseNestedSubprogramHeader. Shares emitForwardDeclSpan with
			// the tree-node route so both produce identical output.
			// No tree node on this fallback route, so TermNone tells
			// emitForwardDeclSpan to classify tokens[hdr.semiIdx] itself.
			emitForwardDeclSpan(tokens, hdr.prefixStart, hdr.semiIdx,
								FormatContext::TermNone,
								&hdr.headerComments, pol, indentLevel, fw);
			return;
		}

		// The IS-opened Block child is looked up before emission because the
		// whole-node fidelity guard below needs its span end up front. This is
		// a pure lookup, so hoisting it cannot change the output.
		const FormatContext* body = findBlockChild(declNode, hdr.isIdx);

		auto emitDecl = [&]()
		{
			// Emit leading comments, then the shared header emitter.
			for (int ci : hdr.headerComments)
				if (ci < hdr.prefixStart)
				{
					fw.WriteCurrentIndent();
					fw.WriteToken(tokens.at(ci));
					fw.NewLine();
				}
			emitSubprogramHeader(tokens, hdr.prefixStart, hdr.isIdx, indentLevel, pol, fw);

			// Emit the IS-opened Block child.
			if (body)
				emitBlock(*body, tokens, pol, indentLevel, fw);
		};

#ifdef _DEBUG
		// Whole-node token-fidelity guard. emitSubprogramHeader emits
		// [prefixStart..isIdx] with IS/AS inclusive, and emitBlock picks up at
		// isIdx+1 through the END line's ';' (see its own guard), so the two
		// spans are contiguous: the whole declaration is [prefixStart..spanEnd].
		// With no Block child only the header line is emitted, so the span stops
		// at isIdx. Leading comments precede prefixStart and are listed as their
		// own one-token ranges to keep the oracle in emission order.
		//
		// Deliberately wider than emitBlock's own guard, which can only vouch
		// for the body: a token dropped from the header here (the CREATE
		// prefix, a parameter, a RETURN type) was previously invisible until
		// the whole-buffer re-tokenize check ran.
		{
			const int spanEnd = body ? endClauseGuardEnd(tokens, body->closeSrcIdx)
									 : hdr.isIdx;
			if (spanEnd >= hdr.prefixStart && spanEnd < (int)tokens.size())
			{
				std::vector<std::pair<int, int>> dbgRanges;
				for (int ci : hdr.headerComments)
					if (ci < hdr.prefixStart)
						dbgRanges.push_back({ ci, ci });
				dbgRanges.push_back({ hdr.prefixStart, spanEnd });
				EMIT_RANGES_GUARD(tokens, dbgRanges, fw);
				emitDecl();
				return;
			}
		}
#endif
		emitDecl();
	}

} // namespace OpenEditor
