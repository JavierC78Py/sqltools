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
#include "OpenEditor/OEPlsSqlFormatterCommon.h"
#include "Common/StackTracer.h"

// FormatWriter is currently fully inline (see OEPlsSqlFormatterCommon.h).
// This translation unit exists so the header has a home .cpp to grow into
// as generic token-stream helpers (skipEol, indentText, noSpaceBeforeToken,
// etc. — see md/plsql_formatter_decoupling_plan.md, group (a)) migrate here
// from OEPlsSqlFormatterBlocks.cpp during later steps of the FormatWriter
// rollout (md/plsql_formatter_trace_writer_proposal.md).

namespace OpenEditor {

	bool noSpaceBeforeToken(EToken t)
	{
		return t == etCOMMA || t == etSEMICOLON || t == etRIGHT_ROUND_BRACKET
			|| t == etDOT || t == etPERCENT_SIGN || t == etLEFT_ROUND_BRACKET
			|| t == etRANGE;
	}

	bool noSpaceAfterToken(EToken t)
	{
		return t == etLEFT_ROUND_BRACKET || t == etDOT
			|| t == etPERCENT_SIGN || t == etRANGE || t == etCOLON;
	}
	bool noSpaceAfterToken(int t) { return noSpaceAfterToken((EToken)t); }

	// True for reserved words that are datatype names — they keep their
	// precision/scale parens tight (VARCHAR2(100), NUMBER(10,2)), unlike
	// operator-like reserved words (IN, NOT, EXISTS, …) which get a space
	// before '(' (IN (10, 20), NOT (x), EXISTS (select …)).
	// The list mirrors the datatype entries of g_11g_reserved[]
	// (OEPlsSqlParser.cpp); datatypes not listed there (NVARCHAR2, CLOB,
	// TIMESTAMP, …) carry reserved == 0 and never reach this check.
	bool isDatatypeWord(const OpenEditor::Token& tk)
	{
		static const wchar_t* const datatypeWords[] = {
			L"CHAR", L"DATE", L"DECIMAL", L"FLOAT", L"INTEGER", L"LONG",
			L"MLSLABEL", L"NUMBER", L"RAW", L"ROWID", L"SMALLINT",
			L"VARCHAR", L"VARCHAR2",
		};
		for (const wchar_t* w : datatypeWords)
			if (_wcsicmp(tk.value.c_str(), w) == 0) return true;
		return false;
	}

	bool needSpaceBetween(const OpenEditor::Token& prev, const OpenEditor::Token& tk)
	{
		if (noSpaceAfterToken(prev.token)) return false;
		if (noSpaceBeforeToken(tk.token))
			return tk.token == etLEFT_ROUND_BRACKET
				&& prev.reserved != 0 && !isDatatypeWord(prev);
		return true;
	}

	bool isEndLineComment(const OpenEditor::Token& tk)
	{
		return tk.token == etCOMMENT
			&& tk.value.size() >= 2
			&& tk.value.at(0) == L'-' && tk.value.at(1) == L'-';
	}

	// See the declaration comment in OEPlsSqlFormatterCommon.h. The two
	// equivalent formulations, and why this one is used:
	//
	//   tokens.at(i - 1).token != etEOL     — cheapest, and exactly right given
	//                                          PlSqlParserEx::PutLine() feeds
	//                                          one physical line at a time and
	//                                          emitEol() terminates it;
	//   tokens.at(i).line == <prev code>.line — survives any token the lexer
	//                                          may leave between the two, and
	//                                          is the idiom the working
	//                                          precedents already use
	//                                          (Decls.cpp's emitComment lambda,
	//                                          emitParamList()'s prevSrcLine).
	//
	// The `.line` comparison is preferred. It must skip etEOL rather than
	// compare against it: emitEol() stamps the terminator with the line it
	// *ends*, so an own-line comment on line 6 would compare equal to the etEOL
	// of line 5 and every comment would read as trailing.
	bool isTrailingComment(const std::vector<OpenEditor::Token>& tokens, int i)
	{
		if (i <= 0 || i >= (int)tokens.size())
			return false;
		if (tokens.at(i).token != etCOMMENT)
			return false;

		const int line = tokens.at(i).line;
		for (int k = i - 1; k >= 0; --k)
		{
			const OpenEditor::Token& tk = tokens.at(k);
			if (tk.token == etEOL || tk.token == etWHITESPACE)
				continue;
			return tk.line == line;
		}
		return false;   // nothing but line breaks in front of it: a lead-in
	}

	// One space between the code and a rejoined comment — the house style, and
	// the width the proposal settled on (Resolved question 1): the SQL side's
	// alignStatementComments() pads to `maxContentWidth + 1`
	// (OEPlsSqlFormatterSqlSide.cpp:567), and the two emitters that already
	// preserve a trailing comment both write fw.WriteSpaces(1)
	// (Decls.cpp:341, Stmts.cpp:922). The width veto below is computed with
	// this same constant, because measuring the reserve with a different
	// separator than the one actually emitted makes rule 6 off by one at the
	// boundary.
	static const int kEndCommentSep = 1;

	bool tryEmitTrailingEndComment(
		FormatWriter& fw,
		const OpenEditor::Token& tk,
		bool trailing,
		const FormatterPolicy& pol)
	{
		if (!trailing || !pol.plsqlEndLineCommentKeepTrailing)
			return false;
		if (!isEndLineComment(tk))
			return false;

		// Measured against the line as it will read after the join, separator
		// included — the width veto has to see the same characters the reader
		// will (proposal Q3).
		if (!fw.ResumeLineForEndComment(
				(size_t)kEndCommentSep + tk.value.size(),
				pol.plsqlEndLineCommentMaxCol))
			return false;

		fw.WriteSpaces(kEndCommentSep);
		fw.WriteToken(tk);
		fw.NewLine();
		return true;
	}

	bool isStringLiteral(const OpenEditor::Token& tk)
	{
		return tk.token == etQUOTED_STRING;
	}

	void appendEosSentinel(std::vector<OpenEditor::Token>& sub)
	{
		OpenEditor::Token eos;
		eos.token  = etEOS;
		eos.length = 0;
		sub.push_back(eos);
	}

	bool isLowPrioArith(const OpenEditor::Token& tk)
	{
		if (tk.token == etMINUS) return true;
		if (tk.token == etUNKNOWN && tk.value == L"+") return true;
		return false;
	}

	bool isHighPrioArith(const OpenEditor::Token& tk)
	{
		return tk.token == etSTAR || tk.token == etSLASH;
	}

	bool isBoolOp(const OpenEditor::Token& tk)
	{
		return tk.token == etAND || tk.token == etOR;
	}

	bool isCondTerminator(const OpenEditor::Token& tk)
	{
		return tk.token == etTHEN || tk.token == etLOOP;
	}

	int emitFlatTokens(
		const std::vector<OpenEditor::Token>& tokens,
		int start, int end,
		FormatWriter& fw)
	{
		int w = 0;
		int prev = -1;
		for (int i = start; i <= end; ++i)
		{
			const auto& tk = tokens.at(i);
			if (tk.token == etEOL) continue;
			if (prev >= 0 && needSpaceBetween(tokens.at(prev), tk)) { fw.WriteSpaces(1); ++w; }
			fw.WriteToken(tk);
			w += (int)tk.value.size();
			prev = i;
		}
		return w;
	}

	int measureFlat(
		const std::vector<OpenEditor::Token>& tokens,
		int start, int end)
	{
		int w = 0;
		int prev = -1;
		for (int i = start; i <= end; ++i)
		{
			const auto& tk = tokens.at(i);
			if (tk.token == etEOL) continue;
			if (prev >= 0 && needSpaceBetween(tokens.at(prev), tk)) ++w;
			w += (int)tk.value.size();
			prev = i;
		}
		return w;
	}

	int skipEolAndComments(
		const std::vector<OpenEditor::Token>& tokens, int i, int end,
		std::vector<int>* skippedComments)
	{
		while (i < end && (tokens.at(i).token == etEOL || tokens.at(i).token == etCOMMENT))
		{
			if (tokens.at(i).token == etCOMMENT && skippedComments)
				skippedComments->push_back(i);
			++i;
		}
		return i;
	}

	bool rangeHasEndLineComment(
		const std::vector<OpenEditor::Token>& tokens,
		int start, int end /*exclusive*/)
	{
		for (int i = start; i < end && i < (int)tokens.size(); ++i)
			if (isEndLineComment(tokens.at(i))) return true;
		return false;
	}

	// emitFlatTokens() joins a token range onto ONE physical line, so an
	// end-of-line "--" comment inside that range silently swallows every token
	// joined after it: they become part of the comment text and are gone on
	// re-parse. This applies exactly the same spacing rules — its output is
	// byte-identical to emitFlatTokens() when the range holds no end-of-line
	// comment — but breaks the output line after each "--" comment, the way
	// emitNormalizedStatement() already does for statement token streams.
	bool emitJoinedKeepingComments(
		const std::vector<OpenEditor::Token>& tokens,
		int start, int end /*exclusive*/,
		int indentChars,
		FormatWriter& fw)
	{
		bool lineOpen = true;
		int prev = -1;
		for (int i = start; i < end; ++i)
		{
			const auto& tk = tokens.at(i);
			if (tk.token == etEOL) continue;
			if (!lineOpen)
			{
				fw.NewLine();
				fw.WriteSpaces(indentChars);
				prev = -1;
				lineOpen = true;
			}
			else if (prev >= 0 && needSpaceBetween(tokens.at(prev), tk))
				fw.WriteSpaces(1);
			fw.WriteToken(tk);
			prev = i;
			if (isEndLineComment(tk)) lineOpen = false;
		}
		return lineOpen;
	}

	std::vector<ParamSpan> splitAtTopLevelCommas(
		const std::vector<OpenEditor::Token>& tokens,
		int parenOpen, int parenClose)
	{
		std::vector<ParamSpan> result;
		int depth = 0;
		int pStart = parenOpen + 1;

		// The comments skipEolAndComments() steps over are handed back through
		// its skippedComments out-parameter (its documented contract) instead of
		// being discarded. A chunk that turns out to hold nothing but comments
		// is still pushed — with an empty [start,end] range — so those comments
		// reach the emitter too.
		auto makeSpan = [&tokens](int rawStart, int end)
		{
			ParamSpan sp;
			sp.start = skipEolAndComments(tokens, rawStart, end, &sp.lead);
			sp.end   = end - 1;
			return sp;
		};

		for (int j = parenOpen + 1; j < parenClose; ++j)
		{
			EToken t = tokens.at(j).token;
			if      (t == etLEFT_ROUND_BRACKET)  ++depth;
			else if (t == etRIGHT_ROUND_BRACKET) --depth;
			else if (t == etCOMMA && depth == 0)
			{
				ParamSpan sp = makeSpan(pStart, j);
				if (sp.start < j || !sp.lead.empty()) result.push_back(sp);
				pStart = j + 1;
			}
		}
		{
			ParamSpan sp = makeSpan(pStart, parenClose);
			if (sp.start < parenClose || !sp.lead.empty()) result.push_back(sp);
		}
		return result;
	}

	int nextSemicolon(
		const std::vector<OpenEditor::Token>& tokens,
		int idx)
	{
		int n = (int)tokens.size();
		for (int i = idx + 1; i < n; ++i)
		{
			if (tokens.at(i).token == etEOL) continue;
			if (tokens.at(i).token == etSEMICOLON) return i;
			break; // non-EOL, non-semicolon — stop
		}
		return -1;
	}

	int scanToSemicolon(
		const std::vector<OpenEditor::Token>& tokens,
		int pos, int rangeEnd)
	{
		int depth = 0;
		int firstSemiAnyDepth = rangeEnd;
		for (int i = pos; i < rangeEnd; ++i)
		{
			if      (tokens.at(i).token == etLEFT_ROUND_BRACKET)  ++depth;
			else if (tokens.at(i).token == etRIGHT_ROUND_BRACKET) { if (depth > 0) --depth; }
			else if (tokens.at(i).token == etSEMICOLON || tokens.at(i).token == etEOS)
			{
				if (depth == 0) return i;
				if (firstSemiAnyDepth == rangeEnd) firstSemiAnyDepth = i;
			}
		}
		return firstSemiAnyDepth; // malformed: use first ';' regardless of depth
	}

	int skipEol(const std::vector<OpenEditor::Token>& tokens, int i, int end)
	{
		while (i < end && tokens.at(i).token == etEOL) ++i;
		return i;
	}

	std::wstring indentText(const std::wstring& text, int indentSize)
	{
		if (text.empty()) return text;
		std::wstring indent = std::wstring(indentSize, L' ');
		std::wstring result;
		std::wstring::size_type pos = 0;
		while (pos <= text.size())
		{
			auto nl = text.find(L'\n', pos);
			std::wstring::size_type stop = (nl == std::wstring::npos) ? text.size() : nl;
			std::wstring line = text.substr(pos, stop - pos);
			if (!line.empty() && line.back() == L'\r') line.pop_back();
			if (!line.empty())
				result += indent + line;
			if (nl == std::wstring::npos) break;
			result += L'\n';
			pos = nl + 1;
		}
		return result;
	}

	bool isDmlKind(FormatContext::Kind k)
	{
		switch (k)
		{
		case FormatContext::Select:
		case FormatContext::Delete:
		case FormatContext::Update:
		case FormatContext::InsertSimple:
		case FormatContext::InsertConditional:
		case FormatContext::Merge:
		case FormatContext::CteDml:   // WITH … SELECT/DML — spliced like DML
			return true;
		default:
			return false;
		}
	}

	void emitEndClause(
		const std::vector<OpenEditor::Token>& tokens,
		int closeSrcIdx,
		int indentLevel,
		FormatWriter& fw)
	{
		IndentLevelGuard indentGuard(fw, indentLevel);
		fw.WriteCurrentIndent();
		fw.WriteToken(tokens.at(closeSrcIdx));
		int i = closeSrcIdx + 1;
		while (i < (int)tokens.size()
			&& tokens.at(i).token != etEOL
			&& tokens.at(i).token != etEOS)
		{
			if (tokens.at(i).token != etSEMICOLON) fw.WriteSpaces(1);
			fw.WriteToken(tokens.at(i));
			if (tokens.at(i).token == etSEMICOLON) { ++i; break; }
			++i;
		}
		fw.NewLine();
	}

	void appendBlankLineIfNeeded(FormatWriter& fw)
	{
		fw.EmptyLine();
	}

	void trimTrailingWs(std::wstring& s)
	{
		auto p = s.find_last_not_of(L" \n\r\t");
		if (p != std::wstring::npos) s.erase(p + 1);
		else s.clear();
	}

	// ---- E2b: token-run views (see the declarations in OEPlsSqlFormatterCommon.h) ----

	std::wstring concatTokenValues(const std::vector<OpenEditor::Token>& run)
	{
		size_t total = 0;
		for (const auto& tk : run) total += tk.value.size();

		std::wstring s;
		s.reserve(total);
		for (const auto& tk : run) s += tk.value;
		return s;
	}

	bool tokenRunIsMultiLine(const std::vector<OpenEditor::Token>& run)
	{
		for (const auto& tk : run)
			if (tk.value.find(L'\n') != std::wstring::npos) return true;
		return false;
	}

	size_t tokenRunCharLength(const std::vector<OpenEditor::Token>& run)
	{
		size_t n = 0;
		for (const auto& tk : run) n += tk.value.size();
		return n;
	}

	void trimTrailingWs(std::vector<OpenEditor::Token>& run)
	{
		auto isWsChar = [](wchar_t c)
		{
			return c == L' ' || c == L'\n' || c == L'\r' || c == L'\t';
		};

		while (!run.empty())
		{
			OpenEditor::Token& last = run.back();
			size_t keep = last.value.size();
			while (keep > 0 && isWsChar(last.value.at(keep - 1))) --keep;

			if (keep > 0 && keep == last.value.size())
				break; // last token ends in real content — nothing to trim

			if (keep == 0)
			{
				run.pop_back(); // whitespace-only or zero-length: drop entirely
				continue;
			}

			last.value.resize(keep);
			last.length = static_cast<OpenEditor::Token::size_pos>(keep);
			break;
		}
	}

	const FormatContext* findBlockChild(
		const FormatContext& node, int openIdx)
	{
		for (const auto& child : node.children)
			if (child.kind == FormatContext::Block && child.openSrcIdx == openIdx)
				return &child;
		return nullptr;
	}

	const FormatContext* findForwardDeclChild(
		const FormatContext& node,
		const std::vector<OpenEditor::Token>& tokens,
		int pos, int rangeEnd)
	{
		const int n = (int)tokens.size();
		if (rangeEnd > n) rangeEnd = n;
		for (const auto& child : node.children)
		{
			if (child.kind != FormatContext::SubprogramForwardDecl) continue;
			if (child.openSrcIdx < pos)        continue; // already emitted
			if (child.openSrcIdx >= rangeEnd)  continue; // not owned by this range
			if (child.closeSrcIdx >= rangeEnd) continue; // terminator not owned by this range
			// Only EOL and method-modifier words may sit between pos and the
			// node's own span start (see the header comment). A comment is
			// deliberately NOT allowed: a standalone comment at pos must keep
			// falling through to the caller's own comment handling so it stays
			// on its own line instead of being joined onto the header line.
			bool gapOk = true;
			for (int j = pos; j < child.openSrcIdx; ++j)
			{
				if (tokens.at(j).token == etEOL) continue;
				if (isSubprogramModifierWord(tokens.at(j))) continue;
				gapOk = false;
				break;
			}
			if (gapOk) return &child;
		}
		return nullptr;
	}

	// Joins tokens[start..end) into a single-spaced string, skipping EOLs,
	// using FormatWriter for consistent token-fidelity emission.
	std::wstring joinTokensSingleSpaced(
		const std::vector<OpenEditor::Token>& tokens,
		int start, int end /*exclusive*/)
	{
		std::wstring out;
		FormatWriter fw(out, 2, /*layoutChecks=*/false);
		int prev = -1;
		for (int i = start; i < end; ++i)
		{
			if (tokens.at(i).token == etEOL) continue;
			if (prev >= 0 && needSpaceBetween(tokens.at(prev), tokens.at(i)))
				fw.WriteSpaces(1);
			fw.WriteToken(tokens.at(i));
			prev = i;
		}
		return out;
	}

#ifdef _DEBUG
	// ------------------------------------------------------------------------
	// Layout-invariant checks. See the declaration block in
	// OEPlsSqlFormatterCommon.h for why these exist *alongside* EmitRangeGuard
	// rather than inside it: the guard strips whitespace before comparing, so
	// it cannot observe "right tokens, wrong line".
	// ------------------------------------------------------------------------

	// Narrow-copy helper for report text. Keeps this TU free of an ATL
	// conversion-macro dependency; diagnostics are ASCII in practice.
	static std::string narrowForReport(const std::wstring& s, size_t maxChars = 60)
	{
		std::string r;
		for (size_t i = 0; i < s.size() && r.size() < maxChars; ++i)
			r += (s.at(i) < 128) ? (char)s.at(i) : '?';
		return r;
	}

	void reportLayoutViolation(const wchar_t* what, const std::wstring& detail)
	{
		TRACE("PL/SQL formatter layout violation: %ls\n  %ls\n", what, detail.c_str());

		if (formatterDiagnosticsActive())
		{
			// Automated test in progress: report through the diagnostics sink.
			// _ASSERTE would pop the modal CRT assert dialog and deadlock the
			// COM caller — same reasoning as EmitRangeGuard's destructor.
			std::wstring& rep = *g_pFormatterDiagnostics;
			rep += L"[LayoutGuard] ";
			rep += what;
			if (!detail.empty())
			{
				rep += L"\n  ";
				rep += detail;
			}
			rep += L"\n";

			// Name the responsible emitter: capture the current stack into the
			// report. The TRACE above only reaches an attached debugger; the
			// artifact file must identify the violating WriteToken frame after
			// the fact. Debug-only diagnostics path, so the symbol-walk cost is
			// irrelevant here.
			std::string stack;
			Common::StackTracer::Trace(stack);
			if (!stack.empty())
			{
				rep += L"[STACK TRACE at violation point]\n";
				for (const char c : stack)
					rep += static_cast<wchar_t>(static_cast<unsigned char>(c));
				if (rep.back() != L'\n')
					rep += L'\n';
			}
		}
		else
		{
			_ASSERTE(!"PL/SQL formatter layout invariant violated (see TRACE output)");
		}
	}

	std::wstring checkScriptSlashLayout(const std::wstring& formatted)
	{
		std::wstring report;

		auto trimOf = [](const std::wstring& s) -> std::wstring
		{
			const size_t b = s.find_first_not_of(L" \t\r");
			if (b == std::wstring::npos)
				return std::wstring();
			const size_t e = s.find_last_not_of(L" \t\r");
			return s.substr(b, e - b + 1);
		};

		// A line consisting of nothing but '/' is only a script terminator
		// when that '/' lies in code. The same bare '/' text can appear
		// *inside* a string literal, a q-quote, a block comment or a double-
		// quoted identifier — pathological input (e.g. an unterminated quote
		// that swallows half the script into one string token, verbatim-
		// preserved by the raw-fragment fallback) can embed a '/' line that
		// is pure string content. Track the lex state across the whole text
		// and mark every line that does not start in code state; those lines
		// take no part in the L2 check below.
		std::vector<bool> lineNonCode;
		{
			bool inStr = false, inQ = false, inDQ = false, inBlk = false;
			wchar_t qClose = 0;
			lineNonCode.push_back(false);
			const size_t n = formatted.size();
			for (size_t p = 0; p < n; ++p)
			{
				const wchar_t c = formatted[p];
				if (c == L'\n')
				{
					lineNonCode.push_back(inStr || inQ || inDQ || inBlk);
					continue;
				}
				if (inStr)
				{
					if (c == L'\'')
					{
						if (p + 1 < n && formatted[p + 1] == L'\'')
							++p;            // '' escape — still inside
						else
							inStr = false;  // closing quote
					}
					continue;
				}
				if (inQ)
				{
					// q'X … X' — closing delimiter followed by the quote
					if (c == qClose && p + 1 < n && formatted[p + 1] == L'\'')
					{
						inQ = false;
						++p;
					}
					continue;
				}
				if (inDQ)
				{
					if (c == L'"')
						inDQ = false;
					continue;
				}
				if (inBlk)
				{
					if (c == L'*' && p + 1 < n && formatted[p + 1] == L'/')
					{
						inBlk = false;
						++p;
					}
					continue;
				}
				// ---- code state ----
				if (c == L'\'')
				{
					inStr = true;
					continue;
				}
				if ((c == L'q' || c == L'Q') && p + 2 < n && formatted[p + 1] == L'\'')
				{
					const wchar_t d = formatted[p + 2];
					qClose = (d == L'[') ? L']'
						   : (d == L'{') ? L'}'
						   : (d == L'(') ? L')'
						   : (d == L'<') ? L'>'
						   : d;
					inQ = true;
					p += 2;
					continue;
				}
				if (c == L'"')
				{
					inDQ = true;
					continue;
				}
				if (c == L'-' && p + 1 < n && formatted[p + 1] == L'-')
				{
					// line comment: quotes in it are inert — skip to EOL and
					// let the '\n' branch record the state for the next line
					while (p < n && formatted[p] != L'\n')
						++p;
					if (p == n)
						break;
					--p; // loop's ++p lands back on the '\n'
					continue;
				}
				if (c == L'/' && p + 1 < n && formatted[p + 1] == L'*')
				{
					inBlk = true;
					++p;
					continue;
				}
			}
		}

		size_t pos        = 0;
		int    lineNo     = 1;
		size_t lineIdx    = 0;
		bool   prevWasSlash = false;
		int    prevLineNo = 0;

		while (pos <= formatted.size())
		{
			const size_t nl  = formatted.find(L'\n', pos);
			const size_t end = (nl == std::wstring::npos) ? formatted.size() : nl;
			const std::wstring trimmed = trimOf(formatted.substr(pos, end - pos));
			const bool nonCode = (lineIdx < lineNonCode.size()) ? lineNonCode[lineIdx] : false;

			// L2 — a line holding only the script terminator must be followed
			// by a blank line, or be the last content in the buffer. Anything
			// else means a '/'-terminated unit got glued to whatever followed
			// it (the bug fixed by commit 2bc0661a). Only '/' lines that start
			// in code state can be real terminators — see the lex-state scan
			// above.
			if (prevWasSlash && !trimmed.empty() && !nonCode)
			{
				wchar_t buf[256];
				swprintf(buf, _countof(buf),
						 L"line %d: script '/' is not followed by a blank line "
						 L"(next line %d = '%hs')\n",
						 prevLineNo, lineNo, narrowForReport(trimmed).c_str());
				report += buf;
			}

			prevWasSlash = (!nonCode && trimmed == L"/");
			prevLineNo   = lineNo;
			++lineNo;
			++lineIdx;

			if (nl == std::wstring::npos)
				break;
			pos = nl + 1;
		}

		return report;
	}
#endif // _DEBUG

} // namespace OpenEditor
