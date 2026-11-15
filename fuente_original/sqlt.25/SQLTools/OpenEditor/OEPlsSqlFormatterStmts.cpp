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
#include "OpenEditor/OEPlsSqlFormatterStmts.h"
#include "OpenEditor/OEPlsSqlFormatterExpr.h"
#include "OpenEditor/OEPlsSqlFormatterDecls.h"
#include "OpenEditor/OEPlsSqlFormatterUnits.h"
#include "OpenEditor/OESettings.h"
#include <algorithm>

namespace OpenEditor {

	// Forward declarations of helpers defined later in this namespace.
	//
	// continuationSameLevel: after a "--" comment forces a line break, the next
	// token is normally emitted one indent level deeper than the statement (see
	// the definition). A caller whose range ends in a keyword that belongs at
	// the statement's own indent — a loop header's trailing LOOP — passes true
	// to keep it flush with the statement instead.
	void emitNormalizedStatement(
		const std::vector<OpenEditor::Token>& tokens,
		int start, int end,
		int indentLevel,
		FormatWriter& fw,
		bool continuationSameLevel = false);

	// FormatterPolicy::FromSettings — declared in OEPlsSqlFormatterCommon.h
	// (see md/FormatterPolicy_proposal.md). This supersedes the former
	// ExprWrapPolicy::FromPlsql(s)/FromSql(s), which both read the same four
	// FmtPlsql* settings (the "late decision" was reusing PL/SQL settings for
	// the SQL side rather than adding dedicated FmtSql* ones — see the old
	// FromSql comment, preserved in git history). This is now the single
	// place, for both SQL and PL/SQL, that reads s.GetFmt*()/GetIndentSpacing().
	//
	// Defined here rather than in the header because the header only
	// forward-declares Settings — that lightweight dependency is precisely what
	// lets OEPlsSqlFormatterSqlSide.cpp include it without a cycle — and calling
	// Settings::Get… requires the complete type.
	FormatterPolicy FormatterPolicy::FromSettings(const Settings& s)
	{
		FormatterPolicy pol;

		pol.indentSpacing                    = s.GetIndentSpacing();

		pol.simpleStmtEnabled                = s.GetFmtSimpleStmtEnabled();
		pol.simpleStmtMaxLen                 = s.GetFmtSimpleStmtMaxLen();
		pol.selectShortListEnabled           = s.GetFmtSelectShortListEnabled();
		pol.selectShortListMaxLen            = s.GetFmtSelectShortListMaxLen();
		pol.fromShortListEnabled             = s.GetFmtFromShortListEnabled();
		pol.fromShortListMaxLen              = s.GetFmtFromShortListMaxLen();
		pol.alignColumnAliases               = s.GetFmtAlignColumnAliases();
		pol.blockShortMaxLen                 = s.GetFmtBlockShortMaxLen();
		pol.selectStmtEnabled                = s.GetFmtSelectStmtEnabled();
		pol.selectStmtMaxLen                 = s.GetFmtSelectStmtMaxLen();
		pol.groupByShortListEnabled          = s.GetFmtGroupByShortListEnabled();
		pol.groupByShortListMaxLen           = s.GetFmtGroupByShortListMaxLen();
		pol.orderByShortListEnabled          = s.GetFmtOrderByShortListEnabled();
		pol.orderByShortListMaxLen           = s.GetFmtOrderByShortListMaxLen();
		pol.setOpBlankLine                   = s.GetFmtSetOpBlankLine();
		pol.caseWhenShortMaxLen              = s.GetFmtCaseWhenShortMaxLen();
		pol.returningMaxLen                  = s.GetFmtReturningMaxLen();
		pol.returningMaxItems                = s.GetFmtReturningMaxItems();
		pol.logErrorsMaxLen                  = s.GetFmtLogErrorsMaxLen();
		pol.alignSetAssignments              = s.GetFmtAlignSetAssignments();
		pol.alignCaseInSet                   = s.GetFmtAlignCaseInSet();
		pol.brickListEnabled                 = s.GetFmtBrickListEnabled();
		pol.brickListMaxLineLen              = s.GetFmtBrickListMaxLineLen();
		pol.brickListMaxLineItems            = s.GetFmtBrickListMaxLineItems();
		pol.brickListMinItems                = s.GetFmtBrickListMinItems();
		pol.brickListLinkedInsert            = s.GetFmtBrickListLinkedInsert();
		pol.brickListLinkedSetOp             = s.GetFmtBrickListLinkedSetOp();

		pol.plsqlDeclBlankLineSeparator      = s.GetFmtPlsqlDeclBlankLineSeparator();
		pol.plsqlBlankLineAroundFlowStmts    = s.GetFmtPlsqlBlankLineAroundFlowStmts();
		pol.plsqlBlankLineAroundSubprograms  = s.GetFmtPlsqlBlankLineAroundSubprograms();
		pol.plsqlBlankLineAfterUnitHeader    = s.GetFmtPlsqlBlankLineAfterUnitHeader();
		pol.plsqlDmlBlankLineSeparator       = s.GetFmtPlsqlDmlBlankLineSeparator();
		pol.plsqlBlankLineBeforeComment      = s.GetFmtPlsqlBlankLineBeforeComment();
		pol.plsqlIsOnNewLine                 = s.GetFmtPlsqlIsOnNewLine();
		pol.plsqlParamListMaxLen             = s.GetFmtPlsqlParamListMaxLen();
		pol.plsqlParamListAlignParen         = s.GetFmtPlsqlParamListAlignParen();
		pol.plsqlEndLineCommentKeepTrailing  = s.GetFmtPlsqlEndLineCommentKeepTrailing();
		pol.plsqlEndLineCommentMaxCol        = s.GetFmtPlsqlEndLineCommentMaxCol();

		// Expression wrapping (formerly ExprWrapPolicy) — same four FmtPlsql*
		// values for both SQL and PL/SQL callers (the late decision noted above).
		pol.expressionMaxLen                 = s.GetFmtPlsqlExpressionMaxLen();
		pol.concatMaxLen                     = s.GetFmtPlsqlConcatMaxLen();
		pol.concatKeepMixedPairTogether      = s.GetFmtPlsqlConcatKeepMixedPairTogether();
		pol.concatBreakBetweenLiterals       = s.GetFmtPlsqlConcatBreakBetweenLiterals();

		// SQL-side expression/concat widths — the late decision reuses the
		// same PL/SQL settings (see comment above).
		pol.sqlExpressionMaxLen              = s.GetFmtPlsqlExpressionMaxLen();
		pol.sqlConcatMaxLen                  = s.GetFmtPlsqlConcatMaxLen();

		return pol;
	}



	// Emits tokens[start..end] (inclusive) with spacing *normalized*:
	// single space between tokens with the usual no-space-before/after rules
	// for ','/';'/')'/'('/'.'/'%'. An end-of-line "--" comment forces a line
	// break (re-indented) before emitting the next token, so nothing after it
	// is silently swallowed under the comment. This is the standard fallback
	// for not-yet-structurally-understood constructs (assignments, procedure
	// calls, declarations with embedded comments, etc.); it intentionally
	// does NOT reproduce the original layout — that is the point.

	// ---------------------------------------------------------------
	// §9.3 ParamList emission helpers
	// ---------------------------------------------------------------

	// ---------------------------------------------------------------
	// Emits tokens[start..end] (inclusive, EOLs skipped) with math/boolean
	// operator-aware line-wrapping according to §9.1 and §9.2.
	//
	// Math split (§9.1): breaks at low-priority ops (+/-) when present at
	// the top level (parenDepth==0); falls back to high-priority ops (*/)
	// only when no low-priority ops exist at top level.
	// Paren groups are opaque — never split inside them at this level.
	//
	// Boolean split (§9.2): breaks before each top-level AND/OR; THEN/LOOP
	// stays attached to the last condition line (not placed on its own line).
	//
	// If the flat length fits within pol.expressionMaxLen, no wrapping.
	// If pol.expressionMaxLen == 0, no wrapping.
	//
	// Returns true if it emitted something (operators were found and wrapping
	// applied), false if the caller should fall through to plain normalized emit.
	//
	// Declared in OEPlsSqlFormatterExpr.h, which owns the default arguments for
	// the last three parameters; repeating them here would be a redefinition
	// (C2572).
	bool emitMathBoolAwareStatement(
		const std::vector<OpenEditor::Token>& tokens,
		int start, int end, /*inclusive*/
		int indentLevel,
		const FormatterPolicy& pol,
		FormatWriter& fw,
		const std::vector<FormatContext>* children,
		bool continuationSameLevel,
		int indentCharsOverride)
	{
		IndentLevelGuard indentGuard(fw, indentLevel);
		// AlignParen-style callers (emitParamListStatement) need the "current
		// indent" to be an absolute column rather than indentLevel*step; see
		// FormatWriter::SetIndentChars.
		if (indentCharsOverride >= 0)
			fw.SetIndentChars(indentCharsOverride);
		// Characters the caller has already put on this output line. Zero for
		// every caller that starts a statement or a scratch buffer — the indent
		// written below is the first thing on the line — and the width of a
		// declaration's aligned "name TYPE :=" prefix for emitVarDeclDefault().
		// The flat-fit test adds it, because pol.expressionMaxLen is a *line*
		// limit: without the prefix a default whose expression just fits would
		// still be emitted as one over-wide line.
		const int leadingChars = (int)fw.CurrentLineWidth();
		// Continuation lines normally indent one step deeper than the first
		// emitted line (e.g. "v_x := a\n  + b" — the first line shares the
		// assignment's indent, continuations go one level in). But when this
		// call is emitting the *interior* of an already-opened paren group
		// (recursive call below, after "(\n"), `indent` already IS the
		// correct level for both the first content line and any further
		// continuation breaks inside that group — adding another step here
		// would over-indent continuations relative to the first line.
		// contIndentChars mirrors what fw.WriteContinuationIndent() emits, for
		// colPos bookkeeping in this function.
		const int contExtraChars = continuationSameLevel ? 0 : fw.GetIndentSpacing();
		const int contIndentChars = fw.CurrentIndentChars() + contExtraChars;
		const int maxLen = pol.expressionMaxLen;

		// Collect non-EOL token indices.
		std::vector<int> idx;
		for (int i = start; i <= end; ++i)
			if (tokens.at(i).token != etEOL) idx.push_back(i);
		if (idx.empty()) return false;

		// DDL statements (CREATE …) must never be split by math/bool wrapping:
		// OR in "CREATE OR REPLACE" is not a boolean operator.
		if (!idx.empty() && tokens.at(idx.at(0)).token == etCREATE)
			return false;

		// Returns true if token at index ti is the closing AND of a BetweenExpr
		// child — it must not be treated as a boolean break point.
		auto isBetweenAnd = [&](int ti) -> bool {
			if (!children) return false;
			for (const auto& ch : *children)
				if (ch.kind == FormatContext::BetweenExpr && ch.closeSrcIdx == ti)
					return true;
			return false;
		};

		// Scan top-level operators (parenDepth==0).
		// Also flag whether any top-level ( ) group exists — even when all
		// operators are inside parens the group itself may need wrapping.
		int parenD = 0;
		bool hasLowArith = false, hasHighArith = false, hasBool = false;
		bool hasTopLevelParen = false;
		for (int ti : idx)
		{
			const auto& tk = tokens.at(ti);
			if (tk.token == etLEFT_ROUND_BRACKET)  { if (parenD == 0) hasTopLevelParen = true; ++parenD; continue; }
			if (tk.token == etRIGHT_ROUND_BRACKET) { if (parenD > 0) --parenD; continue; }
			if (parenD > 0) continue;
			if (isLowPrioArith(tk))           hasLowArith  = true;
			if (isHighPrioArith(tk))          hasHighArith = true;
			if (isBoolOp(tk) && !isBetweenAnd(ti)) hasBool = true;
		}

		// Determine which operator class to split on.
		// Priority: bool > low-arith > high-arith (only if nothing lower present).
		bool splitBool     = hasBool;
		bool splitLowArith = !hasBool && hasLowArith;
		bool splitHighArith= !hasBool && !hasLowArith && hasHighArith;

		if (!splitBool && !splitLowArith && !splitHighArith && !hasTopLevelParen)
			return false; // nothing to split on

		// If the only reason to enter here is a top-level paren and it belongs
		// to a ParamList child, let the innerParam handler (§9.3) take over.
		if (!splitBool && !splitLowArith && !splitHighArith && hasTopLevelParen && children)
		{
			for (const auto& ch : *children)
			{
				if (ch.kind == FormatContext::ParamList
				&&  ch.openSrcIdx >= start && ch.openSrcIdx <= end)
					return false; // defer to §9.3 param-list handler
			}
		}

		// Test flat length.
		if (maxLen > 0)
		{
			int flatLen = 0;
			int prevIdx = -1;
			for (int ti : idx)
			{
				const auto& tk = tokens.at(ti);
				if (prevIdx >= 0 && needSpaceBetween(tokens.at(prevIdx), tk))
					++flatLen;
				flatLen += (int)tk.value.size();
				prevIdx = ti;
			}
			if (flatLen + leadingChars <= maxLen) return false; // fits — let caller emit flat
		}
		else
		{
			return false; // wrapping disabled
		}

		// Emit with breaks.
		// Boolean: break before every top-level AND/OR (unconditional).
		// Math: break before operator only when current line would exceed maxLen
		//       (greedy width-based packing, same as || chain).
		//
		// BeginWrappedLine() rather than WriteCurrentIndent(): the caller may
		// already own the head of this line — emitVarDeclDefault() writes a
		// declaration's aligned "name TYPE :=" prefix before handing the default
		// expression over. The packing column then has to be the real one, not
		// the indent. Continuation lines are unaffected either way: they are
		// still WriteContinuationIndent() = m_indentChars + 1 level, so a wrapped
		// default lands exactly where the same expression wraps in a statement.
		int colPos = fw.BeginWrappedLine();
		bool firstOnLine = true;
		int  prevTi = -1; // index of last emitted token (for space-before-( logic)
		parenD = 0;
		int N = (int)idx.size();

		for (int k = 0; k < N; ++k)
		{
			int ti = idx.at(k);
			const auto& tk = tokens.at(ti);

			// ---- Handle a top-level ( … ) group as a unit ----
			if (tk.token == etLEFT_ROUND_BRACKET && parenD == 0)
			{
				// Locate the matching ')' within idx.
				int closeK = -1;
				{
					int d = 1;
					for (int j = k + 1; j < N; ++j)
					{
						if      (tokens.at(idx.at(j)).token == etLEFT_ROUND_BRACKET)  ++d;
						else if (tokens.at(idx.at(j)).token == etRIGHT_ROUND_BRACKET) { --d; if (d == 0) { closeK = j; break; } }
					}
				}
				if (closeK < 0) closeK = N - 1; // malformed — fall back to flat

				// Measure flat width of the group including the parens.
				int groupW = 2; // '(' + ')'
				int pv2 = ti; // prev = '(' for no-space-after logic
				for (int j = k + 1; j < closeK; ++j)
				{
					const auto& ptk = tokens.at(idx.at(j));
					if (needSpaceBetween(tokens.at(pv2), ptk)) ++groupW;
					groupW += (int)ptk.value.size();
					pv2 = idx.at(j);
				}

				// Space needed before '(' depends on the previous token:
				// no space when preceded by a plain identifier (call syntax)
				// or a datatype name (VARCHAR2(100)); space when preceded by
				// an operator-like reserved keyword (IN, NOT, …) or operator.
				bool spaceBeforeParen = !firstOnLine
					&& !(prevTi >= 0
						&& (isDatatypeWord(tokens.at(prevTi))
							|| (tokens.at(prevTi).token == etUNKNOWN
								&& tokens.at(prevTi).reserved == 0)));

				if (maxLen == 0 || colPos + (spaceBeforeParen ? 1 : 0) + groupW <= maxLen)
				{
					// Fits on current line — emit flat.
					if (spaceBeforeParen) { fw.WriteSpaces(1); ++colPos; }
					fw.WriteToken(tk); ++colPos; // '('
					int pv3 = ti;
					for (int j = k + 1; j <= closeK; ++j)
					{
						const auto& ptk = tokens.at(idx.at(j));
						if (needSpaceBetween(tokens.at(pv3), ptk)) { fw.WriteSpaces(1); ++colPos; }
						fw.WriteToken(ptk);
						colPos += (int)ptk.value.size();
						pv3 = idx.at(j);
					}
					firstOnLine = false;
					prevTi = idx.at(closeK); // ')'
				}
				else
				{
					// Too wide — emit '(' then recurse for the interior.
					if (spaceBeforeParen) { fw.WriteSpaces(1); ++colPos; }
					fw.WriteToken(tk);
					fw.NewLine(); // '(\n'

				// Inner range: first..last tokens inside the parens.
				if (closeK > k + 1)
				{
					int innerStart = idx.at(k + 1);
					int innerEnd   = idx.at(closeK - 1);
					// First content line inside the parens is one level
					// deeper than the closing ')' (contIndentChars), matching
					// the template (e.g. ')' at 4 spaces, first content
					// line at 6). But continuation breaks *within* that
					// interior must stay at that SAME level, not indent
					// yet another step — so continuationSameLevel=true
					// is passed to the recursive call below.
					// innerIndent's string is only used for flat-width
					// measurement inside the recursive call/emitNormalizedStatement;
					// its actual emitted indent comes from indentCharsOverride below.
					const int innerIndentChars = contIndentChars + fw.GetIndentSpacing();
					std::wstring innerIndent(innerIndentChars, L' ');
					// The numeric level passed to the recursive call is only
					// used by IndentLevelGuard as a fallback/base; the real
					// column comes from indentCharsOverride=innerIndentChars,
					// which always matches innerIndent's string depth exactly.
					int innerIndentLevel = indentLevel + (continuationSameLevel ? 1 : 2);

					// Find the Paren child node so we can pass its children
					// (e.g. nested BetweenExpr) into the recursive call.
					const std::vector<FormatContext>* innerChildren = nullptr;
					if (children)
					{
						for (const auto& ch : *children)
							if (ch.kind == FormatContext::Paren && ch.openSrcIdx == ti)
							{ innerChildren = &ch.children; break; }
					}

					bool innerWrapped = emitMathBoolAwareStatement(
						tokens, innerStart, innerEnd,
						innerIndentLevel, pol, fw, innerChildren,
						/*continuationSameLevel=*/true, /*indentCharsOverride=*/innerIndentChars);
					if (!innerWrapped)
						emitNormalizedStatement(tokens, innerStart, innerEnd, innerIndentLevel, fw);
				}

				// Closing ')' on its own line at contIndentChars.
					fw.NewLine();
					fw.WriteContinuationIndent(contExtraChars);
					fw.WriteToken(tokens.at(idx.at(closeK)));
					colPos = contIndentChars + 1;
					firstOnLine = false;
					prevTi = idx.at(closeK); // ')'
				}

				k = closeK; // skip past the entire group
				continue;
			}

			// ---- existing depth tracking and break logic ----
			if (tk.token == etLEFT_ROUND_BRACKET)  ++parenD;
			if (tk.token == etRIGHT_ROUND_BRACKET) { if (parenD > 0) --parenD; }

			bool breakHere = false;
			if (parenD == 0 && !firstOnLine)
			{
				if (splitBool && isBoolOp(tk) && !isBetweenAnd(ti))
				{
					breakHere = true; // always break before AND/OR
				}
				else if (splitLowArith && isLowPrioArith(tk))
				{
					// Measure tokens from here up to the next top-level low-prio
					// op (tracking paren depth so ops inside parens are skipped).
					int nextLen = (int)tk.value.size() + 1; // space + operator
					int ld = 0;
					for (int j = k + 1; j < N; ++j)
					{
						EToken jt = tokens.at(idx.at(j)).token;
						if      (jt == etLEFT_ROUND_BRACKET)  ++ld;
						else if (jt == etRIGHT_ROUND_BRACKET) { if (ld > 0) --ld; }
						if (ld == 0 && isLowPrioArith(tokens.at(idx.at(j)))) break;
						nextLen += 1 + (int)tokens.at(idx.at(j)).value.size();
					}
					breakHere = (colPos + nextLen > maxLen);
				}
				else if (splitHighArith && isHighPrioArith(tk))
				{
					int nextLen = (int)tk.value.size() + 1;
					int ld = 0;
					for (int j = k + 1; j < N; ++j)
					{
						EToken jt = tokens.at(idx.at(j)).token;
						if      (jt == etLEFT_ROUND_BRACKET)  ++ld;
						else if (jt == etRIGHT_ROUND_BRACKET) { if (ld > 0) --ld; }
						if (ld == 0 && isHighPrioArith(tokens.at(idx.at(j)))) break;
						nextLen += 1 + (int)tokens.at(idx.at(j)).value.size();
					}
					breakHere = (colPos + nextLen > maxLen);
				}
			}

				if (breakHere)
				{
					fw.NewLine();
					fw.WriteContinuationIndent(contExtraChars);
					colPos = contIndentChars;
					firstOnLine = true;
				}

				if (!firstOnLine && (prevTi < 0 || needSpaceBetween(tokens.at(prevTi), tk)))
				{
					fw.WriteSpaces(1);
					++colPos;
				}

				fw.WriteToken(tk);
					colPos += (int)tk.value.size();
					firstOnLine = false;
					prevTi = ti;

					if (isEndLineComment(tk))
					{
						fw.NewLine();
						fw.WriteContinuationIndent(contExtraChars);
						colPos = contIndentChars;
						firstOnLine = true;
					}
				}
				return true;
			}

	// Emits tokens[start..end] (inclusive, EOLs skipped) with `||`-aware
	// line-wrapping according to §8:
	//
	//  - If FmtPlsqlConcatMaxLen == 0, or the flat length fits within the
	//    limit, everything is emitted on one line (= emitNormalizedStatement).
	//  - Otherwise: first token(s) go on the first line (prefixed by `indent`);
	//    each break-eligible `||` that would push the line over the limit
	//    starts a new continuation line at `contIndent`.
	//  - Break eligibility (FmtPlsqlConcatKeepMixedPairTogether = true, default):
	//    a || that crosses a literal<->variable boundary (lit||var or var||lit)
	//    is a "fallback" break point, used only when no preferred (non-mixed)
	//    break point exists before the line would overflow maxLen — this keeps
	//    "keep mixed pairs together" a preference rather than an absolute rule,
	//    so a long run of alternating lit/var operands still wraps instead of
	//    being dumped on one unbroken line. When keepMixed is false: any ||
	//    is an equally preferred break point (no fallback distinction).
	//
	// "operand to the left/right" means the nearest non-EOL non-|| token.
	//
	// The function is called by the raw-fragment fallback in
	// emitStatementSequence; it handles the full statement including a trailing
	// ';' if present.
	//
	// Declared in OEPlsSqlFormatterExpr.h, which owns the default argument for
	// indentCharsOverride; repeating it here would be a redefinition (C2572).
	void emitConcatAwareStatement(
		const std::vector<OpenEditor::Token>& tokens,
		int start, int end /*inclusive*/,
		int indentLevel,
		const FormatterPolicy& pol,
		FormatWriter& fw,
		int indentCharsOverride)
	{
		IndentLevelGuard indentGuard(fw, indentLevel);
		// AlignParen-style callers (emitParamListStatement) need the "current
		// indent" to be an absolute column rather than indentLevel*step; see
		// FormatWriter::SetIndentChars. Continuation lines below still just
		// use WriteContinuationIndent() = current indent + 1 level, so this
		// single override is enough to make both the first line and every
		// continuation line line up with the aligned column.
		if (indentCharsOverride >= 0)
			fw.SetIndentChars(indentCharsOverride);
		EMIT_RANGE_GUARD(tokens, start, end, fw);

		const int maxLen        = pol.concatMaxLen;
		const bool keepMixed    = pol.concatKeepMixedPairTogether;
		const bool breakLitLit  = pol.concatBreakBetweenLiterals;
		// See emitMathBoolAwareStatement(): what the caller already put on this
		// line counts towards the limit. Zero for a statement, the width of the
		// aligned "name TYPE :=" prefix for a declaration default.
		const int leadingChars  = (int)fw.CurrentLineWidth();

		// Collect non-EOL token indices in range.
		std::vector<int> idx;
		for (int i = start; i <= end; ++i)
			if (tokens.at(i).token != etEOL) idx.push_back(i);

		if (idx.empty()) return;

		// If no || in range or wrapping disabled, fall back to normalized emit.
		bool hasConcatOp = false;
		for (int ti : idx)
			if (tokens.at(ti).token == etCONCAT_OP) { hasConcatOp = true; break; }

		if (!hasConcatOp || maxLen == 0)
		{
			// Mid-line safe: a declaration default reaches this flat path with
			// its aligned "name TYPE :=" prefix already on the line.
			fw.WriteCurrentIndentIfLineStart();
			bool first = true;
			int prevTi = -1;
			for (int ti : idx)
			{
				const auto& tk = tokens.at(ti);
				// needSpaceBetween() — not the raw noSpaceBefore/noSpaceAfter
				// pair — so '(' after a reserved word (IN, NOT, …) gets its
				// space, matching emitFlatTokens()/the SQL formatter's rule.
				if (!first && needSpaceBetween(tokens.at(prevTi), tk))
					fw.WriteSpaces(1);
				fw.WriteToken(tk);
				if (isEndLineComment(tk)) { fw.NewLine(); fw.WriteContinuationIndent(); first = true; prevTi = -1; continue; }
				first = false;
				prevTi = ti;
			}
			return;
		}

		// Test whether the whole thing fits on one line.
		{
			int flatLen = 0;
			int prevTi = -1;
			for (int ti : idx)
			{
				const auto& tk = tokens.at(ti);
				if (prevTi >= 0 && needSpaceBetween(tokens.at(prevTi), tk))
					++flatLen;
				flatLen += (int)tk.value.size();
				prevTi = ti;
			}
			if (flatLen + leadingChars <= maxLen)
			{
				fw.WriteCurrentIndentIfLineStart(); // see the flat path above
				bool first = true;
				int prev = -1;
				for (int ti : idx)
				{
					const auto& tk = tokens.at(ti);
					if (!first && needSpaceBetween(tokens.at(prev), tk))
						fw.WriteSpaces(1);
					fw.WriteToken(tk);
					first = false; prev = ti;
				}
				return;
			}
		}

		int N = (int)idx.size();

		// Returns {leftLit, rightLit} for the || at position k.
		auto litSides = [&](int k) -> std::pair<bool,bool> {
			bool leftLit = false;
			for (int j = k - 1; j >= 0; --j)
				if (tokens.at(idx.at(j)).token != etCONCAT_OP)
				{ leftLit = isStringLiteral(tokens.at(idx.at(j))); break; }
			bool rightLit = false;
			for (int j = k + 1; j < N; ++j)
				if (tokens.at(idx.at(j)).token != etCONCAT_OP)
				{ rightLit = isStringLiteral(tokens.at(idx.at(j))); break; }
			return {leftLit, rightLit};
		};

		// Rank a || as a break candidate: 0 = preferred (non-mixed, or
		// keepMixed disabled — all || are equally preferred), 1 = fallback
		// (mixed lit<->var pair). Preferred breaks are always honored by the
		// normal width-based algorithm; fallback breaks are only used inside
		// a run of consecutive mixed operators that doesn't fit on one line
		// even when started fresh (see "segments" below).
		auto breakRank = [&](int k) -> int {
			if (!keepMixed) return 0; // all || are equally preferred
			auto [l, r] = litSides(k);
			return (l == r) ? 0 : 1; // mixed pairs are a fallback break point
		};

		// Returns true when this || must always break (lit||lit forced break).
		auto isForcedBreak = [&](int k) -> bool {
			if (!breakLitLit) return false;
			auto [l, r] = litSides(k);
			return l && r;
		};

		// Flat length of tokens[idx[a]..idx[b-1]] (positions a..b-1 in idx),
		// with the same spacing rules used when emitting.
		auto segFlatLen = [&](int a, int b) -> int {
			int len = 0;
			int prevTi = -1;
			for (int j = a; j < b; ++j)
			{
				const auto& tk = tokens.at(idx.at(j));
				if (prevTi >= 0 && needSpaceBetween(tokens.at(prevTi), tk))
					++len;
				len += (int)tk.value.size();
				prevTi = idx.at(j);
			}
			return len;
		};

		// Split the chain into segments at preferred (rank 0) break points.
		// segments[i] = [segStart, segEnd) in idx-space. Segment 0 starts at
		// token 0; every later segment starts at a rank-0 || (the break, if
		// taken, happens right before that operator, which becomes the first
		// token of the new line). Within a segment, all || (if any) are
		// fallback (rank 1) — keepMixed keeps them together unless the whole
		// segment doesn't fit on one line by itself.
		std::vector<int> segStarts;
		segStarts.push_back(0);
		for (int k = 0; k < N; ++k)
			if (tokens.at(idx.at(k)).token == etCONCAT_OP && breakRank(k) == 0)
				segStarts.push_back(k);
		segStarts.push_back(N);

		// Multi-line emit. BeginWrappedLine() for the mid-line caller (a
		// declaration default); identical to the old pair at line start.
		int colPos       = fw.BeginWrappedLine();
		bool firstOnLine = true;
		int prev = -1;

		// Continuation lines are always "current indent + 1 level", regardless
		// of whether the current indent is a normal level-based indent or an
		// AlignParen-style absolute column (see FormatWriter::SetIndentChars/
		// WriteContinuationIndent). contIndentChars mirrors what
		// WriteContinuationIndent() emits, for colPos bookkeeping.
		const int contIndentChars = fw.CurrentIndentChars() + fw.GetIndentSpacing();

		auto emitToken = [&](int ti)
		{
			const auto& tk = tokens.at(ti);
			if (!firstOnLine && prev >= 0 && needSpaceBetween(tokens.at(prev), tk))
			{
				fw.WriteSpaces(1);
				++colPos;
			}
			fw.WriteToken(tk);
			colPos += (int)tk.value.size();
			if (isEndLineComment(tk))
			{
				fw.NewLine();
				fw.WriteContinuationIndent();
				colPos = contIndentChars;
				firstOnLine = true; prev = -1;
				return;
			}
			firstOnLine = false;
			prev = ti;
		};

		auto breakLine = [&]()
		{
			fw.NewLine();
			fw.WriteContinuationIndent();
			colPos = contIndentChars;
			firstOnLine = true;
			prev = -1;
		};

		for (size_t si = 0; si + 1 < segStarts.size(); ++si)
		{
			int segStart = segStarts.at(si);
			int segEnd   = segStarts.at(si + 1); // exclusive

			if (si > 0) // segStart is a rank-0 || — decide whether to break before it.
			{
				bool forced = isForcedBreak(segStart) && !firstOnLine;
				bool overflow = (colPos + 3 + segFlatLen(segStart, segEnd) > maxLen) && !firstOnLine;
				if (forced || overflow)
					breakLine();
			}

			// Does the whole segment fit starting from the current column?
			// (Note: firstOnLine must NOT bypass this check — otherwise a
			// segment that is the very first thing emitted, e.g. an entirely
			// mixed chain with no rank-0 breaks at all, would never be
			// sub-wrapped below.)
			int wouldBeLen = colPos + (firstOnLine ? 0 : 1) + segFlatLen(segStart, segEnd);
			bool fitsHere = wouldBeLen <= maxLen;

			if (fitsHere)
			{
				for (int k = segStart; k < segEnd; ++k)
					emitToken(idx.at(k));
				continue;
			}

			// Segment itself doesn't fit even fresh: sub-wrap internally using
			// its own (rank-1, mixed) || operators via plain greedy packing.
			// Only break to a new line first if we're not already at the start
			// of one (avoids emitting a spurious blank line).
			if (!firstOnLine)
				breakLine();
			for (int k = segStart; k < segEnd; ++k)
			{
				int ti = idx.at(k);
				if (tokens.at(ti).token == etCONCAT_OP)
				{
					int nextOpLen = 3; // " ||" itself
					for (int j = k + 1; j < segEnd; ++j)
					{
						if (tokens.at(idx.at(j)).token == etCONCAT_OP) break;
						nextOpLen += 1 + (int)tokens.at(idx.at(j)).value.size();
					}
					if ((colPos + nextOpLen > maxLen) && !firstOnLine)
						breakLine();
				}
				emitToken(ti);
			}
		}
	}

	// Emits a ParamList scope (§9.3):
	//   prefixStart..parenOpen-1  — tokens before '(' (function name etc.)
	//   parenOpen                 — '('
	//   parenOpen+1..parenClose-1 — parameters
	//   parenClose                — ')'
	//   suffixStart..suffixEnd    — tokens after ')' (rest of statement incl. ';')
	//
	// indent       : indent for the first line
	// Continuation indent is always "current indent + 1 level" (see
	// FormatWriter::WriteContinuationIndent); in AlignParen mode "current
	// indent" is re-anchored to the absolute column after '(' via
	// fw.SetIndentChars() rather than being indentLevel*step.
	// The caller is responsible for emitting a trailing '\n'.
	//
	// COMMENTS. Every comment inside the range is reproduced. splitAtTopLevelCommas()
	// hands back in ParamSpan::lead the comments it stepped over before each
	// argument, and the loops below emit them with emitSubprogramHeader()'s
	// placement rule. An end-of-line "--" comment also rules out the flat
	// layout outright (spanHasEolComment): joining a range onto one physical
	// line puts everything after such a comment on that comment's line, where
	// the lexer swallows it on re-parse. Losslessness outranks the length
	// preference, so that holds even when wrapping is disabled.
	void emitParamListStatement(
		const std::vector<OpenEditor::Token>& tokens,
		int prefixStart, int parenOpen, int parenClose,
		int suffixStart, int suffixEnd,
		int indentLevel,
		const FormatterPolicy& pol,
		FormatWriter& fw)
	{
#ifdef _DEBUG
		std::vector<std::pair<int, int>> _guardRanges = { { prefixStart, parenClose } };
		if (suffixStart <= suffixEnd) _guardRanges.push_back({ suffixStart, suffixEnd });
		EMIT_RANGES_GUARD(tokens, _guardRanges, fw);
#endif

		IndentLevelGuard indentGuard(fw, indentLevel);
		const int  maxLen     = pol.plsqlParamListMaxLen;
		const bool alignParen = pol.plsqlParamListAlignParen;

		// Measure full flat width: prefix + ( + params + ) + suffix, plus
		// whatever the caller has already put on this line (its own indent is
		// excluded — that is written below). The extra term is zero for a
		// statement and is a declaration's aligned "name TYPE :=" prefix for
		// emitVarDeclDefault(); pol.plsqlParamListMaxLen is a line limit, so both
		// have to count. See emitMathBoolAwareStatement()'s leadingChars.
		int flatW = (int)fw.CurrentLineWidth();
		flatW += measureFlat(tokens, prefixStart, parenOpen);   // name + (
		flatW += measureFlat(tokens, parenOpen + 1, parenClose - 1); // params
		flatW += 1; // )
		if (suffixStart <= suffixEnd)
			flatW += 1 + measureFlat(tokens, suffixStart, suffixEnd);

		// An end-of-line comment anywhere inside the parens rules the flat
		// layout out: emitFlatTokens() would put that comment and every token
		// joined after it on the same physical line, so the comment swallows
		// them. Same rule as emitSubprogramHeader()'s spanHasEolComment — refuse
		// the compact layout and use the wrapped one below, which replays
		// comments explicitly. Losslessness outranks the length preference, so
		// this applies even when wrapping is disabled (maxLen == 0).
		const bool spanHasEolComment =
			rangeHasEndLineComment(tokens, prefixStart, parenClose + 1);

		// Suffix emission shared by the two flat layouts below. Comment-aware so
		// a "--" comment after ')' cannot swallow the ';'.
		auto emitSuffixFlat = [&](int indentChars, bool lineOpen)
		{
			if (suffixStart > suffixEnd) return;
			if (!lineOpen)
			{
				fw.NewLine();
				fw.WriteSpaces(indentChars);
			}
			else if (!noSpaceBeforeToken(tokens.at(suffixStart).token)) fw.WriteSpaces(1);
			emitJoinedKeepingComments(tokens, suffixStart, suffixEnd + 1, indentChars, fw);
		};

		// Fits on one line or wrapping disabled?
		if (!spanHasEolComment && (maxLen == 0 || flatW <= maxLen))
		{
			// Mid-line safe: a declaration default whose call fits stays on the
			// line its aligned "name TYPE :=" prefix opened. `ind` remains the
			// level-based indent — it is the continuation column a "--" comment
			// inside the range would break to, not this line's start.
			fw.WriteCurrentIndentIfLineStart();
			const int ind = fw.CurrentIndentChars();
			const bool lineOpen = emitJoinedKeepingComments(
				tokens, prefixStart, parenClose + 1, ind, fw);
			emitSuffixFlat(ind, lineOpen);
			return;
		}

		// Needs wrapping — split parameters.
		auto params = splitAtTopLevelCommas(tokens, parenOpen, parenClose);
		if (params.empty())
		{
			// No params at all (empty parens): emit flat regardless. Still
			// comment-aware — the prefix itself may carry a "--" comment, which
			// is what spanHasEolComment caught above.
			fw.WriteCurrentIndentIfLineStart(); // see the flat path above
			const int ind = fw.CurrentIndentChars();
			const bool lineOpen = emitJoinedKeepingComments(
				tokens, prefixStart, parenClose + 1, ind, fw);
			emitSuffixFlat(ind, lineOpen);
			return;
		}

		// Emit prefix + '(' on first line. The prefix goes through the
		// comment-aware joiner so a "--" comment before '(' cannot swallow the
		// '(' itself ("pkg.proc  -- why\n(a => 1)"); the '(' then starts a fresh
		// line in that case, exactly as emitSubprogramHeader() does.
		// Mid-line safe for a declaration default; headIndent stays the
		// level-based indent because it is only ever used to start a *new* line
		// (after a "--" comment) and to indent the prefix's own continuations.
		fw.WriteCurrentIndentIfLineStart();
		const int headIndent = fw.CurrentIndentChars();
		bool lineOpen = emitJoinedKeepingComments(tokens, prefixStart, parenOpen,
												  headIndent, fw);
		if (!lineOpen)
		{
			fw.NewLine();
			fw.WriteSpaces(headIndent);
			lineOpen = true;
		}
		else if (prefixStart < parenOpen
		&&       needSpaceBetween(tokens.at(parenOpen - 1), tokens.at(parenOpen)))
			fw.WriteSpaces(1);
		fw.WriteToken(tokens.at(parenOpen));

		// Absolute output column right after '('. Read from the buffer rather
		// than accumulated from the emitted width: the prefix may have wrapped
		// onto a continuation line above, so the '(' no longer sits at
		// headIndent + measureFlat(prefix). Same probe emitSubprogramHeader()
		// uses for its AlignParen column.
		int col;
		{
			const std::wstring& buf = fw.Str();
			size_t lineStart = buf.rfind(L'\n');
			lineStart = (lineStart == std::wstring::npos) ? 0 : lineStart + 1;
			col = (int)(buf.size() - lineStart);
		}

		// Determine continuation indent for the param-list's own line-wraps:
		// normally "indent + 1 level"; AlignParen mode re-anchors it to the
		// absolute column right after '(' instead. This becomes the "current
		// indent" as far as nested emitters (emitConcatAwareStatement /
		// emitMathBoolAwareStatement) are concerned — their own further
		// continuation (e.g. a wrapped `||` chain within one parameter) is
		// "this + 1 level", which they compute themselves via
		// fw.WriteContinuationIndent() — so no extra step is added here.
		int contIndChars;
		if (alignParen)
		{
			fw.SetIndentChars(col);
			contIndChars = col;
		}
		else
		{
			fw.PushIndent();
			contIndChars = fw.CurrentIndentChars();
		}
		std::wstring contInd(contIndChars, L' '); // width-only; used for nested emitters' flat-width measurement

		// One argument per line. The loop below tests each argument against
		// maxLen before appending it to the current line, but it deliberately
		// skips that test for the first argument on a line — it is already at
		// the start of one. That exemption is wrong for the argument sitting
		// right after '(' : with a maxLen small enough to force one-per-line,
		// every argument but the first moved to its own line and the first
		// stayed glued to the '(' —
		//
		//     audit_change(p_name => v_name,
		//       p_amount => v_amt,
		//       p_note => v_note
		//     );
		//
		// So apply the same width test here, hoisted to the first argument —
		// but only when the call really is a list. With a single argument there
		// is nothing to lay out one-per-line; the argument simply overflows and
		// the expression wrapper deals with it, so breaking after '(' would
		// only add a line for no gain (PUT_LINE('very long ...') keeps its
		// argument on the '(' line).
		// AlignParen mode is exempt: that style anchors the first argument at
		// the absolute column after '(' by design, so it always "fits".
		// An argument with a leading comment is left to the loop's own comment
		// placement rule, which may already open a fresh line.
		bool brokeBeforeFirstArg = false;
		if (!alignParen && maxLen > 0 && params.size() > 1
		&&  params.at(0).lead.empty()
		&&  params.at(0).start <= params.at(0).end)
		{
			const int firstW = measureFlat(tokens, params.at(0).start, params.at(0).end) + 1; // + its ','
			if (col + firstW > maxLen)
			{
				fw.NewLine();
				fw.WriteSpaces(contIndChars);
				brokeBeforeFirstArg = true;
			}
		}

		// Greedy brick-packing: add params to current line until they
		// don't fit, then start a new line.
		//
		// Comments are replayed explicitly. splitAtTopLevelCommas() hands back
		// in ParamSpan::lead every comment it stepped over before an argument;
		// emitting only the argument text silently deletes them, which is what
		// used to happen to every comment inside a call's argument list once the
		// call was too wide to stay flat:
		//
		//     p_x => 1,          p_x => 1,
		//     -- default    =>   p_y => 2
		//     p_y => 2
		//
		// The placement rule is emitSubprogramHeader()'s for a declaration's
		// parameter list: a comment that shares its source line with the '(' or
		// ',' before it stays on that output line (the classic "p_x => 1,
		// -- default"); any other comment gets its own line at the argument
		// indent.
		bool firstParam = true;
		int  lineCol    = brokeBeforeFirstArg ? contIndChars : col;
		// Source line of the last token written, for the rule above.
		int  prevSrcLine = tokens.at(parenOpen).line;

		for (size_t pi = 0; pi < params.size(); ++pi)
		{
			const auto& p = params.at(pi);
			// A comment-only chunk (e.g. "f(a, --x\n)") has an empty range.
			const bool hasText = (p.start <= p.end);
			const bool isLast  = (pi + 1 == params.size());
			const int  paramW  = hasText ? measureFlat(tokens, p.start, p.end) : 0;
			const int  neededW = paramW + (isLast ? 0 : 1); // +comma

			// 1) Comments preceding this argument.
			for (int c : p.lead)
			{
				const auto& ctk = tokens.at(c);
				if (lineOpen && ctk.line == prevSrcLine)
				{
					fw.WriteSpaces(1);
					++lineCol;
				}
				else
				{
					fw.NewLine();
					fw.WriteSpaces(contIndChars);
					lineCol    = contIndChars;
					firstParam = true;
				}
				fw.WriteToken(ctk);
				lineCol     += (int)ctk.value.size();
				prevSrcLine  = ctk.line;
				lineOpen     = !isEndLineComment(ctk);
				firstParam   = false;
			}

			// 2) The argument itself.
			if (hasText)
			{
				// A "--" comment closed the line, so the argument must start a
				// fresh one: appended where it is, it would become part of the
				// comment text and be gone on re-parse.
				if (!lineOpen)
				{
					fw.NewLine();
					fw.WriteSpaces(contIndChars);
					lineCol    = contIndChars;
					firstParam = true;
					lineOpen   = true;
				}

				if (!firstParam)
				{
					int hypothetical = lineCol + 2 + neededW;
					if (hypothetical > maxLen)
					{
						fw.NewLine();
						fw.WriteSpaces(contIndChars);
						lineCol = contIndChars;
						firstParam = true;
					}
					else
					{
						fw.WriteSpaces(1);
						++lineCol;
					}
				}

				// If param is too wide to fit flat, try concat-aware wrapping
				// (handles '||' chains) then math/bool-aware wrapping. A param
				// carrying a "--" comment is excluded: neither sub-emitter
				// replays comments, so both would drop it.
				bool paramWrapped = false;
				if (maxLen > 0 && contIndChars + paramW > maxLen
				&&  !rangeHasEndLineComment(tokens, p.start, p.end + 1))
				{
					std::wstring paramOut;
					FormatWriter paramFw(paramOut, 2, /*layoutChecks=*/false);
					emitConcatAwareStatement(
						tokens, p.start, p.end,
						indentLevel + 1, pol, paramFw, contIndChars);
					// emitConcatAwareStatement always emits something; check
					// whether it actually wrapped (contains a newline).
					paramWrapped = (paramOut.find(L'\n') != std::wstring::npos);

					if (!paramWrapped)
					{
						// Concat didn't wrap — try math/bool.
						paramFw.Clear();
						paramWrapped = emitMathBoolAwareStatement(
							tokens, p.start, p.end,
							indentLevel + 1, pol, paramFw,
							/*children=*/nullptr, /*continuationSameLevel=*/false,
							/*indentCharsOverride=*/contIndChars);
					}
					if (paramWrapped)
					{
						// Both emitters prefix the indent; when we're already on the
						// continuation line (firstParam) that indent was already emitted
						// by the break above — strip the leading spaces.
						if (firstParam)
						{
							size_t nonWs = paramOut.find_first_not_of(L' ');
							if (nonWs != std::wstring::npos) paramOut.erase(0, nonWs);
						}
						fw.WriteText(paramOut);
						lineCol = contIndChars + paramW; // approximate
					}
				}
				if (!paramWrapped)
				{
					// Comment-aware: an argument may end with a "--" comment of
					// its own ("a  -- note" then the ',' on the next source
					// line), which must not swallow the ',' emitted below.
					lineOpen = emitJoinedKeepingComments(tokens, p.start, p.end + 1,
														 contIndChars, fw);
					lineCol += paramW;
				}
				else
					lineOpen = !isEndLineComment(tokens.at(p.end));

				firstParam  = false;
				prevSrcLine = tokens.at(p.end).line;
			}

			// 3) The ',' closing this argument (verbatim token). When a "--"
			//    comment closed the line, the comma starts a new one.
			if (!isLast)
			{
				if (!lineOpen)
				{
					fw.NewLine();
					fw.WriteSpaces(contIndChars);
					lineCol  = contIndChars;
					lineOpen = true;
				}
				for (int ci = p.end + 1; ci < params.at(pi+1).start; ++ci)
					if (tokens.at(ci).token == etCOMMA)
					{
						fw.WriteToken(tokens.at(ci));
						++lineCol;
						prevSrcLine = tokens.at(ci).line;
						break;
					}
			}
		}
		fw.PopIndent();


		// Closing ')'. AlignParen mode keeps it on the last argument's line —
		// unless a "--" comment closed that line, in which case appending it
		// would hide it inside the comment and take the own-line form instead.
		// Same rule as emitSubprogramHeader()'s closing paren.
		if (alignParen && lineOpen)
		{
			fw.WriteToken(tokens.at(parenClose));
		}
		else
		{
			fw.NewLine();
			fw.WriteCurrentIndent();
			fw.WriteToken(tokens.at(parenClose));
		}

		// Suffix (e.g. ';'). Comment-aware for the same swallow reason.
		emitSuffixFlat(fw.CurrentIndentChars(), /*lineOpen=*/true);
	}

	// Normalized emit: single-space joining with "--"-safe line breaks.
	// After a "--" comment forces a line break, the next token is emitted
	// one indent level deeper than the statement (not flush with its start),
	// so it is visually indented under the statement — unless the caller passes
	// continuationSameLevel, which keeps continuations flush with the statement
	// itself (a loop header's trailing LOOP belongs at the loop's own indent,
	// exactly where every other branch of emitLoopStmt() writes it).
	void emitNormalizedStatement(
		const std::vector<OpenEditor::Token>& tokens,
		int start, int end /*inclusive*/,
		int indentLevel,
		FormatWriter& fw,
		bool continuationSameLevel)
	{
		IndentLevelGuard indentGuard(fw, indentLevel);
		if (start > end) return;
		EMIT_RANGE_GUARD(tokens, start, end, fw);

		fw.WriteCurrentIndent();
		if (!continuationSameLevel) fw.PushIndent();

		bool first = true;
		bool needIndent = false;
		int prevIdx = -1;
		for (int i = start; i <= end; ++i)
		{
			const auto& tk = tokens.at(i);
			if (tk.token == etEOL) continue;
			if (needIndent)
			{
				fw.WriteCurrentIndent();
				needIndent = false;
			}
			else if (!first)
			{
				// needSpaceBetween() — the canonical rule (also gives '('
				// its space after a reserved word such as IN/NOT/RETURN).
				if (prevIdx >= 0 && needSpaceBetween(tokens.at(prevIdx), tk))
					fw.WriteSpaces(1);
			}
			fw.WriteToken(tk);
			if (isEndLineComment(tk)) { fw.NewLine(); needIndent = true; }
			prevIdx = i;
			first = false;
		}
	}

	// Verbatim fragment: reproduces the original source layout as closely as
	// possible — original line breaks are kept and each line is re-anchored
	// to the current nesting indent, with the token's original column offset
	// relative to the fragment's first token preserved on top of that indent.
	// NOTE: the tokenizer (PlSqlParserEx) strips whitespace and never emits
	// etWHITESPACE tokens — only significant tokens and etEOL survive — so
	// intra-line spacing must be reconstructed from each token's offset/length
	// rather than replayed from whitespace token values.
	// Use this for constructs the formatter has no structural knowledge of
	// (e.g. CREATE SNAPSHOT, PRAGMA, or any future DDL not yet handled), so
	// the author's manual formatting is not destroyed. In _DEBUG builds the
	// leading indent is tabified to make these fragments visually obvious.
	//
	// allowVerbatimSource — opt-in for callers that know the fragment is a
	// whole opaque statement the formatter must not reinterpret at all (today:
	// the root-level UnknownStmt arm of emitStatementSequence). When it is set
	// and the caller installed a FormatterVerbatimSourceScope, the *exact*
	// source slice spanning [start..end] is written out instead of the
	// reconstruction below — see the "Verbatim-source callback" section of
	// OEPlsSqlFormatter.h. The reconstruction cannot round-trip such text: it
	// enforces a one-space minimum gap between adjacent tokens (so
	// "SPOOL install.log" comes back as "SPOOL install . log") and re-anchors
	// every continuation line, which destroys layout-sensitive embedded
	// languages such as a CREATE … JAVA SOURCE body.
	void emitVerbatimFragment(
		const std::vector<OpenEditor::Token>& tokens,
		int start, int end /*inclusive*/,
		int indentLevel,
		FormatWriter& fw,
		bool allowVerbatimSource = false)
	{
		if (start > end) return;
		EMIT_RANGE_GUARD(tokens, start, end, fw);

		IndentLevelGuard indentGuard(fw, indentLevel);

		if (allowVerbatimSource && formatterVerbatimSourceActive())
		{
			const std::wstring exact =
				(*g_pFormatterVerbatimSource)(tokens.at(start), tokens.at(end));

			// Fidelity gate: the slice must carry exactly the same token text
			// as the range it replaces, whitespace apart — the same oracle
			// EMIT_RANGE_GUARD applies to reconstructed output, now compiled
			// into every configuration. Empty text (no source captured, an
			// unmappable multi-line token, a zero-length synthetic sentinel)
			// or any mismatch falls through to the reconstruction below, so
			// this can only ever improve fidelity, never lose a token.
			if (!exact.empty()
			&&  stripWhitespace(exact) == expectedRangeText(tokens, { { start, end } }))
			{
				// Pure-verbatim policy: the first line is anchored at the
				// current indent, every following line keeps its original
				// column exactly as the author typed it. Nothing is
				// re-indented, re-spaced or re-cased.
				fw.WriteCurrentIndent();
				fw.WriteText(exact);
				return;
			}
		}

		fw.WriteCurrentIndent();

		int baseLine   = tokens.at(start).line;
		int baseOffset = tokens.at(start).offset;
		int prevLine   = baseLine;
		int prevEndCol = tokens.at(start).offset;

		for (int i = start; i <= end; ++i)
		{
			const auto& tk = tokens.at(i);
			if (tk.token == etEOL) continue;

			if (i > start)
			{
				if (tk.line != prevLine)
				{
					fw.NewLine();
					int col = tk.offset - baseOffset;
					if (col < 0) col = 0;
					fw.WriteCurrentIndent();
					fw.WriteSpaces(col);
				}
				else
				{
					int gap = (int)tk.offset - prevEndCol;
					if (gap < 1) gap = (tk.token == etCOMMA || tk.token == etSEMICOLON) ? 0 : 1;
					fw.WriteSpaces(gap);
				}
			}

			fw.WriteToken(tk);
			prevLine   = tk.line;
			prevEndCol = tk.offset + (int)tk.length;
		}
	}

	// Joins tokens[start..end) with single spaces, ignoring EOLs — used for
	// short fragments (declaration types/defaults/cursor headers) that are
	// not expected to need multi-line wrapping yet.
	std::wstring joinTokensSingleSpaced(
		const std::vector<OpenEditor::Token>& tokens,
		int start, int end /*exclusive*/);

	// forward declarations (defined further down in this file)

	// Emits tokens[pos..semiIdx) normalized then ";\n".
	// semiIdx is the exclusive upper bound (index of the ';' token itself,
	// or rangeEnd when no ';' was found). Returns the new pos (past ';' +
	// trailing EOLs). This eliminates the recurring inline scan+emit+advance
	// pattern and the double-semicolon bug it caused.
	int emitNormalizedDecl(
		const std::vector<OpenEditor::Token>& tokens,
		int pos, int semiIdx,
		int indentLevel,
		int rangeEnd,
		FormatWriter& fw)
	{
		if (semiIdx > pos)
			emitNormalizedStatement(tokens, pos, semiIdx - 1, indentLevel, fw);
		else
			fw.WriteCurrentIndent();

		fw.WriteToken(tokens.at(semiIdx));
		fw.NewLine();
		return skipEol(tokens, (semiIdx < rangeEnd ? semiIdx + 1 : rangeEnd), rangeEnd);
	}

	void emitStatementSequence(
		const FormatContext& node,
		const std::vector<OpenEditor::Token>& tokens,
		int rangeStart, int rangeEnd,
		const FormatterPolicy& pol,
		int indentLevel,
		FormatWriter& fw);

	int skipEnder(
		const std::vector<OpenEditor::Token>& tokens,
		int closeSrcIdx, int rangeEnd);

	void emitBlock(
		const FormatContext& block,
		const std::vector<OpenEditor::Token>& tokens,
		const FormatterPolicy& pol,
		int indentLevel,
		FormatWriter& fw);

	void emitCaseStmt(
		const FormatContext& caseNode,
		const std::vector<OpenEditor::Token>& tokens,
		const FormatterPolicy& pol,
		int indentLevel,
		FormatWriter& fw);

	// Emits a CaseExpr used in an assignment/return context:
	//   prefix := case [selector]
	//                 when cond then val
	//                 else val
	//             end
	// where prefix is the tokens before CASE (e.g. "v_name :=").
	// prefixStart..prefixEnd (inclusive, non-EOL tokens) is the assignment prefix.
	void emitCaseExpr(
		const FormatContext& caseNode,
		const std::vector<OpenEditor::Token>& tokens,
		int prefixStart, int prefixEnd,
		const FormatterPolicy& pol,
		FormatWriter& fw,
		bool trailingSemicolon = true,
		bool forceMultiLine = false);

	void emitDeclareSection(
		const FormatContext& node,
		const std::vector<OpenEditor::Token>& tokens,
		int rangeStart, int rangeEnd,
		int indentLevel,
		const FormatterPolicy& pol,
		FormatWriter& fw);

	void emitExceptionSection(
		const FormatContext& block,
		const std::vector<OpenEditor::Token>& tokens,
		int exStart, int blockEnd,
		int indentLevel,
		const FormatterPolicy& pol,
		FormatWriter& fw);

	void emitPackage(
		const FormatContext& block,
		const std::vector<OpenEditor::Token>& tokens,
		int headerStart,
		int isIdx,
		int indentLevel,
		const FormatterPolicy& pol,
		FormatWriter& fw);

	// ========================================================================
	// Step 2.3 — declare-section item tagging. Declared in
	// OEPlsSqlFormatter.h, defined here because the walk needs skipEnder()
	// (internal to this translation unit) plus the tryParseXxx probes from
	// OEPlsSqlFormatterDecls.h.
	//
	// A tag is a dispatch hint, never an authority: emitDeclareSection still
	// calls the one matching tryParseXxx to obtain field spans, and any
	// untagged position runs the original full probe chain. The walk below must
	// therefore agree with that chain exactly — same probes, same order, same
	// range, same cursor advance — which is why it calls those very functions
	// instead of reimplementing recognition that could drift apart.
	// ========================================================================
	namespace {

	// Finds the compound-trigger timing-point section whose header starts
	// exactly at `pos`, or nullptr when pos is not on a timing-point label.
	//
	// Tree-driven, mirroring findForwardDeclChild(): buildTree tags each
	// "BEFORE|AFTER STATEMENT|EACH ROW IS … END <label>;" section as a Block
	// with UnitKind::TimingPoint whose headerStart is the BEFORE/AFTER
	// keyword, so the emitter never re-scans raw tokens for the label shape.
	// An exact headerStart match is enough — emitDeclareSection always lands
	// on a non-EOL token — and it is what stops `pos` on an unrelated
	// declaration from claiming a section further ahead. A section that does
	// not fit inside [pos, rangeEnd) belongs to an enclosing range and is
	// rejected, again like findForwardDeclChild.
	const FormatContext* findTimingPointChild(
		const FormatContext& node, int pos, int rangeEnd)
	{
		for (const auto& ch : node.children)
		{
			if (ch.kind != FormatContext::Block) continue;
			if (ch.unitKind != FormatContext::TimingPoint) continue;
			if (ch.closeSrcIdx >= rangeEnd) continue;
			const int hs = (ch.headerStart >= 0) ? ch.headerStart : ch.openSrcIdx;
			if (hs == pos) return &ch;
		}
		return nullptr;
	}

	// Derives a Block's declare-section range [declStart, declEnd) the way
	// emitBlock does: anchored at DECLARE when present, otherwise at the IS/AS
	// that opened the unit body, and ending at BEGIN (or, with no BEGIN, at
	// EXCEPTION / END). Returns false when the node owns no declare section.
	//
	// emitPackage derives the same range but omits the EXCEPTION fallback. That
	// difference is harmless: the computed declEnd is recorded in declItemsEnd,
	// and declItemKindAt() honours tags only when the caller's rangeEnd matches
	// it exactly, so a range derived another way simply runs the legacy chain.
	bool declareRangeOf(
		const FormatContext& block,
		const std::vector<OpenEditor::Token>& tokens,
		int& declStart, int& declEnd)
	{
		if (block.kind != FormatContext::Block) return false;
		// A TypeSpec's member list sits between AS and ';' but is emitted by
		// emitTypeSpec, never by emitDeclareSection — tagging it is dead work.
		if (block.unitKind == FormatContext::TypeSpec) return false;

		const ClauseEntry* declKw = getKeyword(block.keywords, etDECLARE);
		const ClauseEntry* isAsKw = getKeyword(block.keywords, etIS);
		if (!isAsKw) isAsKw = getKeyword(block.keywords, etAS);
		const ClauseEntry* beginKw  = getKeyword(block.keywords, etBEGIN);
		const ClauseEntry* exceptKw = getKeyword(block.keywords, etEXCEPTION);

		// DECLARE wins over IS/AS, matching emitBlock's if / else-if order.
		const int anchor = declKw ? declKw->srcIdx : (isAsKw ? isAsKw->srcIdx : -1);
		if (anchor < 0) return false;

		const int bodyEnd = exceptKw ? exceptKw->srcIdx : block.closeSrcIdx;
		declStart = skipEol(tokens, anchor + 1, (int)tokens.size());
		declEnd   = beginKw ? beginKw->srcIdx : bodyEnd;
		return declStart < declEnd;
	}

	// Classifies the single declare item starting at pos and reports where the
	// emitter's cursor lands after emitting it. Probe order is copied from
	// emitDeclareSection; the comments below carry its step numbers.
	//
	// Returns false for an item this walk cannot advance past with certainty —
	// SUBTYPE, PRAGMA and the generic-statement fallback all compute their next
	// position through paths not replicated here. The caller then records
	// DeclItemOther and stops tagging that section, so a wrong boundary can
	// never be recorded.
	bool classifyDeclItem(
		const FormatContext& block,
		const std::vector<OpenEditor::Token>& tokens,
		int pos, int rangeEnd,
		FormatContext::DeclItemKind& kind, int& nextPos)
	{
		if (pos < 0 || pos >= rangeEnd) return false;

		// (1) Standalone comment, '--' or '/* */'.
		if (tokens.at(pos).token == etCOMMENT)
		{
			kind    = FormatContext::DeclItemComment;
			nextPos = skipEol(tokens, pos + 1, rangeEnd);
			return true;
		}

		// (1b) Compound-trigger timing-point section (§12.1). declareRangeOf()
		//      never tags a compound-trigger body — the unit has no DECLARE/IS
		//      keyword of its own — so this is a safety net only: report
		//      "cannot advance with certainty" instead of letting the probes
		//      below measure the label as if it were a declaration.
		if (findTimingPointChild(block, pos, rangeEnd))
			return false;

		// (2) Forward-declared / body-less subprogram header (Step 2.1 node).
		if (const FormatContext* fwd = findForwardDeclChild(block, tokens, pos, rangeEnd))
		{
			kind    = FormatContext::DeclItemForwardDecl;
			nextPos = skipEol(tokens, fwd->closeSrcIdx + 1, rangeEnd);
			return true;
		}

		// (3) Nested subprogram with a real body. Probed before the variable
		//     declaration because method modifiers such as CONSTRUCTOR are
		//     etUNKNOWN and would otherwise read as a variable name.
		{
			NestedSubprogramHeader nsh;
			const int afterIs = tryParseNestedSubprogramHeader(tokens, pos, rangeEnd, nsh);
			if (afterIs >= 0)
			{
				const FormatContext* bodyBlock = findBlockChild(block, nsh.isIdx);
				kind = FormatContext::DeclItemNestedSubprog;
				nextPos = bodyBlock
					? skipEnder(tokens, bodyBlock->closeSrcIdx, rangeEnd)
					: skipEol(tokens, afterIs, rangeEnd);
				return true;
			}
		}

		// (4) Variable / constant declaration.
		{
			VarDecl vd;
			const int next = tryParseVarDecl(tokens, pos, rangeEnd, vd);
			if (next >= 0) { kind = FormatContext::DeclItemVar; nextPos = next; return true; }
		}

		// (5) Cursor declaration — with or without header comments; both
		//     branches of the emitter advance to the same `next`.
		{
			CursorDecl cd;
			const int next = tryParseCursorDecl(tokens, pos, rangeEnd, cd);
			if (next >= 0) { kind = FormatContext::DeclItemCursor; nextPos = next; return true; }
		}

		// (6) TYPE name IS RECORD ( field… );
		{
			RecordTypeDecl rtd;
			const int next = tryParseRecordTypeDecl(tokens, pos, rangeEnd, rtd);
			if (next >= 0) { kind = FormatContext::DeclItemRecordType; nextPos = next; return true; }
		}

		// (7) TYPE name IS TABLE OF … ; / VARRAY(n) OF … ;
		{
			CollectionTypeDecl ctd;
			const int next = tryParseCollectionTypeDecl(tokens, pos, rangeEnd, ctd);
			if (next >= 0) { kind = FormatContext::DeclItemCollectionType; nextPos = next; return true; }
		}

		return false;
	}

	void tagDeclareItemsNode(
		FormatContext& node,
		const std::vector<OpenEditor::Token>& tokens)
	{
		int declStart = -1, declEnd = -1;
		if (declareRangeOf(node, tokens, declStart, declEnd))
		{
			// buildTree() calls this as its last act, so the tree is complete
			// and findForwardDeclChild()/findBlockChild() see exactly the
			// children they will see at emission time.
			int pos = skipEol(tokens, declStart, declEnd);
			while (pos < declEnd)
			{
				FormatContext::DeclItem item;
				item.startIdx = pos;
				item.kind     = FormatContext::DeclItemOther;

				int next = -1;
				if (!classifyDeclItem(node, tokens, pos, declEnd, item.kind, next))
				{
					// Untaggable item. Record it as Other so the emitter takes
					// the full chain at this position too, then stop: the
					// advance past SUBTYPE / PRAGMA / fallback is not
					// replicated, and guessing it could misalign every
					// following tag.
					node.declItems.push_back(item);
					break;
				}
				if (next <= pos)
					break; // defensive: never spin on malformed input

				node.declItems.push_back(item);
				pos = next;
			}
			// Publish the range only once at least one tag exists, so an
			// all-Other section leaves declItemsEnd at -1 and costs the emitter
			// nothing but one comparison.
			if (!node.declItems.empty())
				node.declItemsEnd = declEnd;
		}

		for (auto& child : node.children)
			tagDeclareItemsNode(child, tokens);
	}

	} // anonymous namespace

	void tagDeclareItems(
		FormatContext& root,
		const std::vector<OpenEditor::Token>& tokens)
	{
		tagDeclareItemsNode(root, tokens);
	}

	FormatContext::DeclItemKind declItemKindAt(
		const FormatContext& node, int pos, int rangeEnd)
	{
		// Tags are valid only for the exact range they were classified against;
		// a mismatch (or no tags at all) means "run the legacy probe chain".
		if (node.declItemsEnd != rangeEnd || node.declItems.empty())
			return FormatContext::DeclItemNone;

		// declItems is ordered by startIdx — binary search for an exact hit.
		size_t lo = 0, hi = node.declItems.size();
		while (lo < hi)
		{
			const size_t mid = lo + (hi - lo) / 2;
			const int startIdx = node.declItems.at(mid).startIdx;
			if (startIdx == pos) return node.declItems.at(mid).kind;
			if (startIdx <  pos) lo = mid + 1;
			else                 hi = mid;
		}
		return FormatContext::DeclItemNone;
	}

	// -----------------------------------------------------------------------
	// Compound-trigger timing-point section (§12.1)
	// -----------------------------------------------------------------------
	//   before statement is
	//     v_cnt number;
	//   begin
	//     v_cnt := 0;
	//   end before statement;
	//
	// Structurally this is a nested subprogram whose "header" happens to be
	// the two-to-four-token timing-point label, so the emission mirrors
	// emitNestedSubprogram: the header line, then emitBlock for the IS-opened
	// body — implied declare section, BEGIN, statements, optional EXCEPTION
	// and the closing "END <label>;" clause. emitEndClause already replays
	// every token between END and ';', which is what carries the label back
	// out, so no label-specific closer handling is needed here.
	//
	// tp.headerStart is the BEFORE/AFTER keyword and tp.openSrcIdx the IS that
	// opened the section (both recorded by buildTree).
	void emitTimingPointSection(
		const FormatContext& tp,
		const std::vector<OpenEditor::Token>& tokens,
		const FormatterPolicy& pol,
		int indentLevel,
		FormatWriter& fw)
	{
		IndentLevelGuard indentGuard(fw, indentLevel);

		const ClauseEntry* isAsKw = getKeyword(tp.keywords, etIS);
		if (!isAsKw) isAsKw = getKeyword(tp.keywords, etAS);
		const int isIdx    = isAsKw ? isAsKw->srcIdx : tp.openSrcIdx;
		// A truncated/malformed section may have lost its headerStart; falling
		// back to isIdx keeps the span ordered (hdrStart <= isIdx) so the
		// header emit below degrades to "is" instead of reading before it.
		const int hdrStart = (tp.headerStart >= 0 && tp.headerStart <= isIdx)
			? tp.headerStart : isIdx;

		// The whole section: header line, then emitBlock's own span
		// [isIdx+1 .. END-line ';']. The two are contiguous, so one guard over
		// [hdrStart .. ';'] covers the section exactly.
		EMIT_RANGE_GUARD(tokens, hdrStart,
						 endClauseGuardEnd(tokens, tp.closeSrcIdx), fw);

		// "before each row is" — a single line. continuationSameLevel keeps a
		// post-comment continuation flush with the label, which is where the
		// IS belongs.
		emitNormalizedStatement(tokens, hdrStart, isIdx, indentLevel, fw,
								/*continuationSameLevel=*/true);
		fw.NewLine();

		emitBlock(tp, tokens, pol, indentLevel, fw);
	}

	// Emits the contents of a DECLARE section:
	// constant declarations are column-aligned as a group; cursor
	// declarations are emitted with their query delegated to formatSQL();
	// anything else (TYPE/PROCEDURE/FUNCTION/SUBTYPE, or unrecognized
	// syntax) falls back to the generic statement-sequence handling used
	// elsewhere (nested Block bodies, verbatim fragments, etc.).
	void emitDeclareSection(
		const FormatContext& node,
		const std::vector<OpenEditor::Token>& tokens,
		int rangeStart, int rangeEnd,
		int indentLevel,
		const FormatterPolicy& pol,
		FormatWriter& fw)
	{
		IndentLevelGuard indentGuard(fw, indentLevel);
		int pos = skipEol(tokens, rangeStart, rangeEnd);
		const bool blankSep = pol.plsqlDeclBlankLineSeparator;
		const bool blankBeforeComment = pol.plsqlBlankLineBeforeComment;
		bool firstDeclItem = true;
		bool prevWasComment = false;
		// Mirrors emitStatementSequence()'s flag of the same name: an end-of-line
		// comment rejoined to its declaration's line ("v_id NUMBER; -- pk") is
		// part of that declaration, not the head of an own-line comment run, so
		// it must not suppress the FmtPlsqlBlankLineBeforeComment blank that the
		// *next* own-line comment is entitled to. Read only where prevWasComment
		// is true, which every non-comment arm below clears, so a stale value is
		// unreachable.
		bool trailingOnClosedStmt = false;

		while (pos < rangeEnd)
		{
			// Emit any standalone comment at pos (both -- line comments recorded as
			// Comment children, and /* */ block comments not in the tree) on their
			// own line before attempting declaration parsing. Only the first of a
			// run of consecutive comments gets a blank-line separator before it,
			// and only when the previous item is a closed declaration (the last
			// non-blank output ends with ';'); never insert a blank line after a
			// comment (that is handled by the blankSep logic below only firing
			// when the last emitted item was not itself a comment via
			// prevWasComment being reset appropriately).
			if (tokens.at(pos).token == etCOMMENT)
			{
				const OpenEditor::Token& ctk = tokens.at(pos);
				// A trailing comment on a declaration ("v_id NUMBER; -- pk")
				// annotates that declaration. Own-lining it puts it above the
				// *next* one, which reads as a comment on the wrong item, so
				// rejoin it — subject to the width veto and the swallow
				// invariant inside ResumeLineForEndComment(). Tried before the
				// blank-separator and indent writes below, which would
				// otherwise land between the declaration and its comment.
				//
				// closedBefore is captured *before* the join attempt: once the
				// comment text sits on the line, EndsWithSemicolon() can no
				// longer see the declaration's ';' underneath it.
				const bool closedBefore = fw.EndsWithSemicolon();
				if (!tryEmitTrailingEndComment(fw, ctk, isTrailingComment(tokens, pos), pol))
				{
					const bool prevWasTrailingJoin   = prevWasComment && trailingOnClosedStmt;
					const bool prevWasOwnLineComment = prevWasComment && !trailingOnClosedStmt;
					bool prevEndsWithSemicolon = fw.EndsWithSemicolon();
					if (blankBeforeComment && !firstDeclItem && !prevWasOwnLineComment
					&&  (prevEndsWithSemicolon || prevWasTrailingJoin)
					&&  !fw.EndsWithBlankLine())
						fw.NewLine();
					fw.WriteCurrentIndent();
					fw.WriteToken(ctk);
					fw.NewLine();
					trailingOnClosedStmt = false;
				}
				else
					// Captured before the join: with the comment text now on the
					// line, EndsWithSemicolon() can no longer see the ';'.
					trailingOnClosedStmt = closedBefore;
				pos = skipEol(tokens, pos + 1, rangeEnd);
				firstDeclItem = false; prevWasComment = true;
				continue;
			}

			// Step 2.3: the classification buildTree() recorded for this exact
			// position, used below to skip probes already proven to fail.
			// DeclItemNone (no tag here, or tags computed for a different
			// rangeEnd) and DeclItemOther both mean "run every probe", i.e.
			// precisely the pre-Step-2.3 behaviour.
			const FormatContext::DeclItemKind declTag = declItemKindAt(node, pos, rangeEnd);
			const bool probeAll = declTag == FormatContext::DeclItemNone
							   || declTag == FormatContext::DeclItemOther;

			// Try nested subprogram (PROCEDURE/FUNCTION, or method with
			// CONSTRUCTOR/MEMBER/STATIC/OVERRIDING prefix) first — before
			// tryParseVarDecl, because modifier keywords like CONSTRUCTOR are
			// etUNKNOWN and would otherwise be misidentified as variable names.
			//
			// Step 2.1: a *forward declaration* (header terminated by ';' or a
			// script '/' with no IS/AS) is now an explicit
			// SubprogramForwardDecl node built by buildTree(), so its span is
			// read from the tree here instead of being re-scanned from raw
			// tokens. This covers declare sections, package specs and package
			// bodies alike (emitPackage routes its spec/body declare sections
			// through this same function). Only a header with a real body still
			// needs tryParseNestedSubprogramHeader, to locate the IS/AS token
			// and the body Block child.
			const FormatContext* fwd =
				(probeAll || declTag == FormatContext::DeclItemForwardDecl)
				? findForwardDeclChild(node, tokens, pos, rangeEnd)
				: nullptr;
			if (fwd)
			{
				if (blankSep && !firstDeclItem && !prevWasComment && !fw.EndsWithBlankLine())
					fw.NewLine();
				emitSubprogramForwardDecl(*fwd, tokens, pol, indentLevel, fw, pos);
				pos = skipEol(tokens, fwd->closeSrcIdx + 1, rangeEnd);
				if (blankSep && skipEol(tokens, pos, rangeEnd) < rangeEnd) fw.NewLine();
				firstDeclItem = false; prevWasComment = false;
				continue;
			}
			// Compound-trigger timing-point section (§12.1):
			// "BEFORE|AFTER STATEMENT|EACH ROW IS … BEGIN … END <label>;".
			// Recognized from the tree (UnitKind::TimingPoint), before the
			// nested-subprogram probe: the label starts with a reserved
			// BEFORE/AFTER, so no tryParseXxx probe matches it and the generic
			// fallback below would emit the label as a stray fragment and then
			// re-emit the section body.
			//
			// Deliberately not gated on declTag. A compound-trigger body has no
			// DECLARE/IS keyword of its own, so declareRangeOf() declines to
			// tag it and declTag is always DeclItemNone (probeAll) here; and a
			// TimingPoint child can only exist inside a compound trigger, so
			// the lookup is a no-op for every other declare section.
			if (const FormatContext* tp = findTimingPointChild(node, pos, rangeEnd))
			{
				if (blankSep && !firstDeclItem && !prevWasComment && !fw.EndsWithBlankLine())
					fw.NewLine();
				emitTimingPointSection(*tp, tokens, pol, indentLevel, fw);
				pos = skipEnder(tokens, tp->closeSrcIdx, rangeEnd);
				if (blankSep && skipEol(tokens, pos, rangeEnd) < rangeEnd) fw.NewLine();
				firstDeclItem = false; prevWasComment = false;
				continue;
			}
			if (probeAll || declTag == FormatContext::DeclItemNestedSubprog)
			{
				NestedSubprogramHeader nsh;
				int afterIs = tryParseNestedSubprogramHeader(tokens, pos, rangeEnd, nsh);
				if (afterIs >= 0)
				{
					if (blankSep && !firstDeclItem && !prevWasComment && !fw.EndsWithBlankLine())
						fw.NewLine();
					const FormatContext* bodyBlock = findBlockChild(node, nsh.isIdx);
					emitNestedSubprogram(node, tokens, nsh, afterIs, pol, indentLevel, fw);
					if (bodyBlock)
						pos = skipEnder(tokens, bodyBlock->closeSrcIdx, rangeEnd);
					else
						pos = skipEol(tokens, afterIs, rangeEnd);
					if (blankSep && skipEol(tokens, pos, rangeEnd) < rangeEnd) fw.NewLine();
					firstDeclItem = false; prevWasComment = false;
					continue;
				}
			}

			std::vector<VarDecl> group;
			VarDecl vd;
			int next = -1;
			// Consecutive variable declarations are collected into one group so
			// emitVarDeclGroup can column-align them. Reading declTag once
			// before the loop is safe: the loop only keeps matching while the
			// tag still applies, and a non-empty group always ends in
			// `continue` below, so a stale tag is never consulted again.
			if (probeAll || declTag == FormatContext::DeclItemVar)
				while ((next = tryParseVarDecl(tokens, pos, rangeEnd, vd)) >= 0)
				{
					group.push_back(vd);
					pos = next;
				}
			if (!group.empty())
			{
				// node.children carries the ParamList scopes buildTree found
				// inside these declarations, which emitVarDeclGroup needs to
				// wrap an over-wide default value the way a statement wraps.
				emitVarDeclGroup(tokens, group, pol, &node.children, indentLevel, fw);
				firstDeclItem = false; prevWasComment = false;
				continue;
			}

			CursorDecl cd;
			next = (probeAll || declTag == FormatContext::DeclItemCursor)
				 ? tryParseCursorDecl(tokens, pos, rangeEnd, cd) : -1;
			if (next >= 0 && !cd.headerComments.empty())
			{
				// A comment inside the CURSOR/name/params/IS header:
				// emit the header normalized (spacing cleaned up, "--" comment
				// gets a safe line break), still delegating the query body to
				// formatSQL() for consistent DML formatting.
				emitNormalizedStatement(tokens, pos, cd.isIdx, indentLevel, fw);
				fw.NewLine();

				IndentLevelGuard indentGuard(fw, indentLevel+1);
				// Emit any comments between IS and the SELECT body.
				for (int ci : cd.headerComments)
					if (ci > cd.isIdx && ci < cd.selectStart)
					{
						fw.WriteCurrentIndent();
						fw.WriteToken(tokens.at(ci));
						fw.NewLine();
					}
				size_t subEnd = std::min((size_t)cd.semiIdx, tokens.size());
				std::vector<OpenEditor::Token> sub(tokens.begin() + cd.selectStart, tokens.begin() + subEnd);
				appendEosSentinel(sub);
				// E2b — token-run delegation: SpliceInto() re-indents the nested
				// formatter's run exactly as WriteIndentText() did, keeping its
				// token types. concatTokenValues() is the guard's text oracle,
				// inlined because SQL_SLICE_GUARD and TRACE compile away in
				// Release, where a named local would be unreferenced.
				std::vector<OpenEditor::Token> formatted = formatSQL(sub, pol);
				SQL_SLICE_GUARD(sub, concatTokenValues(formatted));
				trimTrailingWs(formatted);
#ifdef _DEBUG
				TRACE(L"emitDeclareSection(Cursor(Decl) %s", concatTokenValues(formatted).c_str());
#endif
				fw.SpliceInto(formatted, fw.CurrentIndentChars());
				fw.WriteToken(tokens.at(cd.semiIdx));
				fw.NewLine();
				pos = next;
				if (blankSep && skipEol(tokens, pos, rangeEnd) < rangeEnd) fw.NewLine();
				firstDeclItem = false; prevWasComment = false;
				continue;
			}

			if (next >= 0)
			{
				emitCursorDecl(tokens, cd, pol, indentLevel, fw);
				pos = next;
				if (blankSep && skipEol(tokens, pos, rangeEnd) < rangeEnd) fw.NewLine();
				firstDeclItem = false; prevWasComment = false;
				continue;
			}

			RecordTypeDecl rtd;
			next = (probeAll || declTag == FormatContext::DeclItemRecordType)
				 ? tryParseRecordTypeDecl(tokens, pos, rangeEnd, rtd) : -1;
			if (next >= 0)
			{
				emitRecordTypeDecl(tokens, rtd, pol, indentLevel, fw);
				pos = next;
				if (blankSep && skipEol(tokens, pos, rangeEnd) < rangeEnd) fw.NewLine();
				firstDeclItem = false; prevWasComment = false;
				continue;
			}

			CollectionTypeDecl ctd;
			next = (probeAll || declTag == FormatContext::DeclItemCollectionType)
				 ? tryParseCollectionTypeDecl(tokens, pos, rangeEnd, ctd) : -1;
			if (next >= 0)
			{
				emitCollectionTypeDecl(tokens, ctd, fw, indentLevel);
				pos = next;
				if (blankSep && skipEol(tokens, pos, rangeEnd) < rangeEnd) fw.NewLine();
				firstDeclItem = false; prevWasComment = false;
				continue;
			}

			// Step 2.3 self-check. Every gated probe above has now been either
			// skipped or tried and failed. Arriving here with a *specific* tag
			// therefore means the probe that tag selected did not match: the
			// classification made in buildTree() has drifted from what the
			// emitter recognizes, and the output would silently differ from the
			// pre-Step-2.3 behaviour. Report it the way EmitRangeGuard does —
			// through the diagnostics sink while a test is running (an _ASSERTE
			// would DebugBreak and deadlock the COM caller), assert otherwise.
			//
			// One check here covers all six taggable kinds, because a
			// DeclItemNone/DeclItemOther tag sets probeAll and never trips it.
#ifdef _DEBUG
			if (!probeAll)
			{
				static const wchar_t* const tagNames[] = {
					L"None", L"Comment", L"ForwardDecl", L"NestedSubprog",
					L"Var", L"Cursor", L"RecordType", L"CollectionType", L"Other"
				};
				const int k = (int)declTag;
				const wchar_t* tagName =
					(k >= 0 && k < (int)(sizeof(tagNames) / sizeof(tagNames[0])))
					? tagNames[k] : L"?";

				if (formatterDiagnosticsActive())
				{
					std::wstring& rep = *g_pFormatterDiagnostics;
					rep += L"[DECL_ITEM_TAG] classification mismatch: tag ";
					rep += tagName;
					rep += L" but no probe matched at token ";
					rep += std::to_wstring(pos);
					rep += L"\n";
				}
				else
				{
					TRACE("emitDeclareSection: DeclItem tag %ls matched no probe at token %d\n",
						  tagName, pos);
					_ASSERTE(!"Step 2.3: declare-item tag disagrees with the probe it gated");
				}
				// Deliberately fall through to the SUBTYPE / PRAGMA / generic
				// chain below so output stays as close to correct as possible
				// even when the guard fires. It is not bit-identical to the
				// legacy chain (which would also have retried the other probes
				// here), which is exactly why the guard reports instead of
				// staying quiet.
			}
#endif

			// SUBTYPE name IS basetype [RANGE lo..hi | NOT NULL];
			if (tokens.at(pos).token == etSUBTYPE)
			{
				pos = emitNormalizedDecl(tokens, pos,
					scanToSemicolon(tokens, pos, rangeEnd), indentLevel, rangeEnd, fw);
				firstDeclItem = false; prevWasComment = false;
				continue;
			}

			// PRAGMA ... ; — arrives as etUNKNOWN with value "PRAGMA" (case-insensitive).
			if (tokens.at(pos).token == etUNKNOWN
			&&  !_wcsicmp(tokens.at(pos).value.c_str(), L"PRAGMA"))
			{
				pos = emitNormalizedDecl(tokens, pos,
					scanToSemicolon(tokens, pos, rangeEnd), indentLevel, rangeEnd, fw);
				firstDeclItem = false; prevWasComment = false;
				continue;
			}

					// Fall back to the generic single-statement handling
					// (Block/IfStmt/LoopStmt scopes, DML, or verbatim fragment) for
					// everything not yet structurally understood.
					int stmtEnd = rangeEnd;
					for (const auto& child : node.children)
					{
						if (child.openSrcIdx > pos && child.openSrcIdx < stmtEnd
						&&  child.kind != FormatContext::Paren
						&&  child.kind != FormatContext::ParamList)
							stmtEnd = child.openSrcIdx;
					}
					// Also stop at the next top-level ';' if it comes first.
					int semi = scanToSemicolon(tokens, pos, stmtEnd);
					if (semi < stmtEnd) stmtEnd = semi + 1;
					emitStatementSequence(node, tokens, pos, stmtEnd, pol, indentLevel, fw);
					pos = skipEol(tokens, stmtEnd, rangeEnd);
					firstDeclItem = false; prevWasComment = false;
				}
			}

	// Advances past a scope's closing "END" token, its mandatory trailing
	// qualifier (e.g. IF for IfStmt, LOOP for LoopStmt — absent for Block),
	// and the terminating ';', mirroring what emitIfStmt/emitLoopStmt/
	// emitBlock already emit for the closer. Returns the index of the first
	// token after the closer (still within end).
	int skipEnder(const std::vector<OpenEditor::Token>& tokens, int closeSrcIdx, int rangeEnd)
	{
		int i = closeSrcIdx + 1;
		while (i < rangeEnd
			&& tokens.at(i).token != etEOL
			&& tokens.at(i).token != etEOS)
		{
			bool isSemicolon = tokens.at(i).token == etSEMICOLON;
			++i;
			if (isSemicolon) break;
		}
		return skipEol(tokens, i, rangeEnd);
	}

	void emitStatementSequence(
		const FormatContext& node,          // scope owning the children in this range
		const std::vector<OpenEditor::Token>& tokens,
		int rangeStart, int rangeEnd,       // [start, end) token index range
		const FormatterPolicy& pol,
		int indentLevel,
		FormatWriter& fw);

	// Emits the "IF <cond> THEN" / "ELSIF <cond> THEN" / "ELSE" header line for
	// one IfStmt clause, joining the condition tokens (between the clause
	// keyword and its THEN, if any) onto a single line — condition wrapping
	// is a later refinement (see md/plsql_formatter_rules.md).
	void emitIfHeaderLine(
		const FormatContext& ifStmt,
		const std::vector<OpenEditor::Token>& tokens,
		const ClauseEntry& ce,
		int indentLevel,
		const FormatterPolicy& pol,
		FormatWriter& fw)
	{
		IndentLevelGuard indentGuard(fw, indentLevel);
		if (ce.token == etIF || ce.token == etELSIF)
		{
			// Find THEN token index.
			int thenIdx = ce.srcIdx + 1;
			while (thenIdx < (int)tokens.size() && tokens.at(thenIdx).token != etTHEN)
				++thenIdx;
			// Find last non-EOL token before THEN.
			int condEnd = thenIdx - 1;
			while (condEnd > ce.srcIdx && tokens.at(condEnd).token == etEOL) --condEnd;

			int condStart = skipEol(tokens, ce.srcIdx, (int)tokens.size());
			// Pass range excluding THEN — if wrapping fires, THEN goes on its own line.
			bool wrapped = emitMathBoolAwareStatement(
				tokens, condStart, condEnd, indentLevel, pol, fw, &ifStmt.children);
			if (!wrapped)
			{
				// Flat emit: IF/ELSIF cond THEN all on one line.
				fw.WriteCurrentIndent();
				fw.WriteToken(tokens.at(ce.srcIdx));
				int prev = ce.srcIdx;
				for (int i = ce.srcIdx + 1; i <= thenIdx; ++i)
				{
					if (tokens.at(i).token == etEOL) continue;
					if (needSpaceBetween(tokens.at(prev), tokens.at(i))) fw.WriteSpaces(1);
					fw.WriteToken(tokens.at(i));
					prev = i;
				}
			}
			else
			{
				// Condition was wrapped — THEN on its own line at base indent.
				fw.NewLine();
				fw.WriteCurrentIndent();
				fw.WriteToken(tokens.at(thenIdx));
			}
		}
		else
		{
			// ELSE — just the keyword.
			fw.WriteCurrentIndent();
			fw.WriteToken(tokens.at(ce.srcIdx));
		}
		fw.NewLine();
	}

	// Emits an IfStmt scope: IF/ELSIF/ELSE clause headers with their bodies
	// recursively formatted, closed by "END IF;".
	void emitIfStmt(
		const FormatContext& ifStmt,
		const std::vector<OpenEditor::Token>& tokens,
		const FormatterPolicy& pol,
		int indentLevel,
		FormatWriter& fw)
	{
		IndentLevelGuard indentGuard(fw, indentLevel);

		EMIT_RANGE_GUARD(tokens, ifStmt.openSrcIdx,
						 endClauseGuardEnd(tokens, ifStmt.closeSrcIdx), fw);

		for (size_t k = 0; k < ifStmt.keywords.size(); ++k)
		{
			const ClauseEntry& ce = ifStmt.keywords.at(k);
			emitIfHeaderLine(ifStmt, tokens, ce, indentLevel, pol, fw);

			// Body range: after this clause's header (after THEN for IF/ELSIF,
			// right after ELSE) up to the clause's recorded close (next
			// ELSIF/ELSE/END).
			int bodyStart = ce.srcIdx + 1;
			if (ce.token == etIF || ce.token == etELSIF)
			{
				while (bodyStart < (int)tokens.size() && tokens.at(bodyStart).token != etTHEN)
					++bodyStart;
				++bodyStart; // past THEN
			}
			bodyStart = skipEol(tokens, bodyStart, (int)tokens.size());
			int bodyEnd = ce.closeSrcIdx >= 0 ? ce.closeSrcIdx : ifStmt.closeSrcIdx;
			emitStatementSequence(ifStmt, tokens, bodyStart, bodyEnd, pol, indentLevel + 1, fw);
		}

		emitEndClause(tokens, ifStmt.closeSrcIdx, indentLevel, fw);
	}

	// Emits the "$IF <cond> $THEN" / "$ELSIF <cond> $THEN" / "$ELSE" header
	// line for one CondCompile clause. Mirrors emitIfHeaderLine but uses the
	// $-prefixed directive tokens.
	void emitCondCompileHeaderLine(
		const FormatContext& condCompile,
		const std::vector<OpenEditor::Token>& tokens,
		const ClauseEntry& ce,
		int indentLevel,
		const FormatterPolicy& pol,
		FormatWriter& fw)
	{
		IndentLevelGuard indentGuard(fw, indentLevel);
		if (ce.token == etCC_IF || ce.token == etCC_ELSIF)
		{
			// Find $THEN token index.
			int thenIdx = ce.srcIdx + 1;
			while (thenIdx < (int)tokens.size() && tokens.at(thenIdx).token != etCC_THEN)
				++thenIdx;
			// Find last non-EOL token before $THEN.
			int condEnd = thenIdx - 1;
			while (condEnd > ce.srcIdx && tokens.at(condEnd).token == etEOL) --condEnd;

			int condStart = skipEol(tokens, ce.srcIdx, (int)tokens.size());
			// Pass range excluding $THEN — if wrapping fires, $THEN goes on its own line.
			bool wrapped = emitMathBoolAwareStatement(
				tokens, condStart, condEnd, indentLevel, pol, fw, &condCompile.children);
			if (!wrapped)
			{
				// Flat emit: $IF/$ELSIF cond $THEN all on one line.
				fw.WriteCurrentIndent();
				fw.WriteToken(tokens.at(ce.srcIdx));
				int prev = ce.srcIdx;
				for (int i = ce.srcIdx + 1; i <= thenIdx; ++i)
				{
					if (tokens.at(i).token == etEOL) continue;
					if (needSpaceBetween(tokens.at(prev), tokens.at(i))) fw.WriteSpaces(1);
					fw.WriteToken(tokens.at(i));
					prev = i;
				}
			}
			else
			{
				// Condition was wrapped — $THEN on its own line at base indent.
				fw.NewLine();
				fw.WriteCurrentIndent();
				fw.WriteToken(tokens.at(thenIdx));
			}
		}
		else
		{
			// $ELSE — just the keyword.
			fw.WriteCurrentIndent();
			fw.WriteToken(tokens.at(ce.srcIdx));
		}
		fw.NewLine();
	}

	// Emits a CondCompile scope: $IF/$ELSIF/$ELSE clause headers with their
	// bodies recursively formatted, closed by "$END".
	void emitCondCompile(
		const FormatContext& condCompile,
		const std::vector<OpenEditor::Token>& tokens,
		const FormatterPolicy& pol,
		int indentLevel,
		FormatWriter& fw)
	{
		IndentLevelGuard indentGuard(fw, indentLevel);

		EMIT_RANGE_GUARD(tokens, condCompile.openSrcIdx,
						 endClauseGuardEnd(tokens, condCompile.closeSrcIdx), fw);

		for (size_t k = 0; k < condCompile.keywords.size(); ++k)
		{
			const ClauseEntry& ce = condCompile.keywords.at(k);
			emitCondCompileHeaderLine(condCompile, tokens, ce, indentLevel, pol, fw);

			// Body range: after this clause's header (after $THEN for $IF/$ELSIF,
			// right after $ELSE) up to the clause's recorded close (next
			// $ELSIF/$ELSE/$END).
			int bodyStart = ce.srcIdx + 1;
			if (ce.token == etCC_IF || ce.token == etCC_ELSIF)
			{
				while (bodyStart < (int)tokens.size() && tokens.at(bodyStart).token != etCC_THEN)
					++bodyStart;
				++bodyStart; // past $THEN
			}
			bodyStart = skipEol(tokens, bodyStart, (int)tokens.size());
			int bodyEnd = ce.closeSrcIdx >= 0 ? ce.closeSrcIdx : condCompile.closeSrcIdx;
			emitStatementSequence(condCompile, tokens, bodyStart, bodyEnd, pol, indentLevel + 1, fw);
		}

		// $END has no "IF"/"LOOP" qualifier and no trailing ';' — emit the
		// bare keyword, but keep any comment that follows it on the same
		// source line (e.g. "$END -- selection directive ends") attached
		// to this line instead of losing it.
		fw.WriteCurrentIndent();
		fw.WriteToken(tokens.at(condCompile.closeSrcIdx));
		int i = condCompile.closeSrcIdx + 1;
		while (i < (int)tokens.size()
			&& tokens.at(i).token != etEOL
			&& tokens.at(i).token != etEOS)
		{
			fw.WriteSpaces(1);
			fw.WriteToken(tokens.at(i));
			++i;
		}
		fw.NewLine();
	}

	// Emits a LoopStmt scope: the FOR/WHILE/LOOP header line, the body, and
	// the closing "END LOOP;". The header may be a bare LOOP, a
	// FOR <var> IN [REVERSE] <lo>..<hi> LOOP, or a WHILE <cond> LOOP —
	// reproduced verbatim (wrapping is a later refinement).
	//
	// Routed through a single FormatWriter instance (see OEPlsSqlFormatterCommon.h /
	// md/plsql_formatter_trace_writer_proposal.md) so every token and structural
	// whitespace flows through named methods. The FormatWriter& fw signature is
	// kept unchanged for now (compatibility-shim step).
	void emitLoopStmt(
		const FormatContext& loopStmt,
		const std::vector<OpenEditor::Token>& tokens,
		const FormatterPolicy& pol,
		int indentLevel,
		FormatWriter& fw)
	{
		IndentLevelGuard indentGuard(fw, indentLevel);

		EMIT_RANGE_GUARD(tokens, loopStmt.openSrcIdx,
						 endClauseGuardEnd(tokens, loopStmt.closeSrcIdx), fw);

		const ClauseEntry* loopKw = getKeyword(loopStmt.keywords, etLOOP);
		int headerEnd = loopKw ? loopKw->srcIdx : loopStmt.openSrcIdx;

		// ---- FOR … IN (subquery) LOOP — cursor FOR loop ----
		// Detect a Paren child whose open falls within the header range.
		// This is always a SELECT subquery (the tree-builder classifies it as
		// Paren because the preceding token is IN, a reserved word).
		const FormatContext* inParen = nullptr;
		if (getKeyword(loopStmt.keywords, etFOR))
		{
			for (const auto& ch : loopStmt.children)
			{
				if (ch.kind == FormatContext::Paren
				&&  ch.openSrcIdx > loopStmt.openSrcIdx
				&&  ch.openSrcIdx < headerEnd)
				{ inParen = &ch; break; }
			}
		}

		if (inParen && inParen->closeSrcIdx >= 0)
		{
			int poIdx = inParen->openSrcIdx;   // '('
			int pcIdx = inParen->closeSrcIdx;  // ')'

			// Emit "FOR buff IN " (tokens before the '(').
			fw.WriteCurrentIndent();
			int prev = -1;
			for (int i = loopStmt.openSrcIdx; i < poIdx; ++i)
			{
				const auto& tk = tokens.at(i);
				if (tk.token == etEOL) continue;
				if (prev >= 0 && needSpaceBetween(tokens.at(prev), tk)) fw.WriteSpaces(1);
				fw.WriteToken(tk);
				prev = i;
			}

				// Format the subquery inside the parens via the DML formatter.
			int selectStart = pcIdx; // will scan forward from poIdx+1
			// Find first non-EOL token inside the paren.
			for (int i = poIdx + 1; i < pcIdx; ++i)
				if (tokens.at(i).token != etEOL) { selectStart = i; break; }

			if (selectStart < pcIdx)
			{
				std::vector<OpenEditor::Token> sub(
					tokens.begin() + selectStart, tokens.begin() + pcIdx);
				appendEosSentinel(sub);
				// E2b — token-run delegation. concatTokenValues() is the guard's
				// text oracle, inlined because SQL_SLICE_GUARD and TRACE compile
				// away in Release, where a named local would be unreferenced.
				std::vector<OpenEditor::Token> formatted = formatSQL(sub, pol);
				SQL_SLICE_GUARD(sub, concatTokenValues(formatted));
				trimTrailingWs(formatted);
#ifdef _DEBUG
				TRACE(L"emitLoopStmt %s", concatTokenValues(formatted).c_str());
#endif

				// Count newlines in formatted output to decide single vs. multi-line.
				// tokenRunIsMultiLine() scans token *values*, so a multi-line block
				// comment still counts as multi-line — an etEOL test would miss it,
				// because such a comment is one token whose interior newlines are
				// ordinary characters of its value.
				bool multiLine = tokenRunIsMultiLine(formatted);

				if (!multiLine)
				{
					// Single-line subquery: try to keep the full header flat.
					// Build: "FOR buff IN (single-line-sql) LOOP"
					// and check length.
					// prefix already partially built — rebuild fully for measurement.
					std::wstring prefixPart;
					int pv = -1;
					for (int i = loopStmt.openSrcIdx; i < poIdx; ++i)
					{
						const auto& tk = tokens.at(i);
						if (tk.token == etEOL) continue;
						if (pv >= 0 && needSpaceBetween(tokens.at(pv), tk)) prefixPart += L' ';
						prefixPart += tk.value;
						pv = i;
					}
					// space between last prefix token (usually IN) and '('
					bool spaceBeforeParen = (pv >= 0 && needSpaceBetween(tokens.at(pv), tokens.at(poIdx)));
					int maxLen = pol.plsqlParamListMaxLen;
					int flatLen = (int)prefixPart.size()
								+ (spaceBeforeParen ? 1 : 0)
								+ 1 /*(*/ + (int)tokenRunCharLength(formatted) + 1 /*)*/ // E2b: run char count == the old formatted.size()
								+ 1 + (int)tokens.at(headerEnd).value.size(); // " LOOP"
					if (maxLen == 0 || flatLen <= maxLen)
					{
						// Flat: reuse the prefix already started in `out`.
						if (prev >= 0 && needSpaceBetween(tokens.at(prev), tokens.at(poIdx))) fw.WriteSpaces(1);
						fw.WriteToken(tokens.at(poIdx)); // '('
						// E2b — flat splice. An indentSize of 0 makes WriteSpaces()
						// a no-op, and this branch is single-line by construction
						// (multiLine is false), so every token is appended verbatim:
						// the characters are exactly what WriteText(formatted)
						// wrote, but the run keeps its own token boundaries instead
						// of arriving as one etUNKNOWN blob.
						fw.SpliceInto(formatted, 0);
						fw.WriteToken(tokens.at(pcIdx)); // ')'
					}
					else
					{
						// Doesn't fit even though single-line: wrap anyway.
						multiLine = true;
					}
				}

				if (multiLine)
				{
					// Multi-line: open paren on header line, SQL indented, ')' at base.
					// The prefix tokens up to '(' are already in `out`.
					if (prev >= 0 && needSpaceBetween(tokens.at(prev), tokens.at(poIdx))) fw.WriteSpaces(1);
					fw.WriteToken(tokens.at(poIdx));
					fw.NewLine();
					fw.SpliceInto(formatted, fw.GetNextIndentChars()); // E2b: was WriteIndentText(formatted, …)
					fw.NewLine();
					fw.WriteCurrentIndent();
					fw.WriteToken(tokens.at(pcIdx)); // "  )"
				}
			}
			else
			{
				// Empty parens (shouldn't happen but handle gracefully).
				if (prev >= 0 && needSpaceBetween(tokens.at(prev), tokens.at(poIdx))) fw.WriteSpaces(1);
				fw.WriteToken(tokens.at(poIdx));
				fw.WriteToken(tokens.at(pcIdx));
			}

			// Comments between ')' and LOOP. The tree builder keeps them as
			// Comment children of the LoopStmt and the body emitter starts after
			// the LOOP keyword, so nothing else reproduces them — writing
			// " LOOP" unconditionally here deleted every one:
			//
			//     FOR rec IN (SELECT ...)              FOR rec IN (SELECT ...)
			//     --  WHERE x = 1     =>  lost         LOOP
			//     LOOP
			bool lineOpen = true;
			int  prevLine = tokens.at(pcIdx).line;
			for (int i = pcIdx + 1; i < headerEnd; ++i)
			{
				const auto& tk = tokens.at(i);
				if (tk.token != etCOMMENT) continue;
				// A comment sharing its source line with ')' stays on that
				// output line; any other one gets its own line at the loop's
				// indent, so the author's layout survives.
				if (lineOpen && tk.line == prevLine)
					fw.WriteSpaces(1);
				else
				{
					fw.NewLine();
					fw.WriteCurrentIndent();
				}
				fw.WriteToken(tk);
				prevLine = tk.line;
				lineOpen = !isEndLineComment(tk);
			}

			// Emit " LOOP\n" (the keyword at headerEnd). A "--" comment closes
			// the line it sits on, so LOOP must then start a fresh one —
			// appended to that line it would be swallowed by the comment.
			if (lineOpen)
				fw.WriteSpaces(1);
			else
			{
				fw.NewLine();
				fw.WriteCurrentIndent();
			}
			fw.WriteToken(tokens.at(headerEnd));
			fw.NewLine();
		}
		else
		{
			// ---- WHILE condition LOOP — try boolean/math-aware wrapping ----
			bool wrapped = false;
			if (getKeyword(loopStmt.keywords, etWHILE))
			{
				int condEnd = headerEnd - 1;
				while (condEnd > loopStmt.openSrcIdx && tokens.at(condEnd).token == etEOL) --condEnd;

				wrapped = emitMathBoolAwareStatement(
					tokens, loopStmt.openSrcIdx, condEnd, indentLevel, pol, fw, &loopStmt.children);
				if (wrapped)
				{
					fw.NewLine();
					fw.WriteCurrentIndent();
					fw.WriteToken(tokens.at(headerEnd));
					fw.NewLine();
				}
			}
			// ---- bare LOOP / numeric FOR … LOOP ----
			if (!wrapped)
			{
				// continuationSameLevel: the range ends in the LOOP keyword, and
				// a "--" comment in the header ("FOR i IN 1 .. 10  -- count")
				// forces a line break before it. LOOP belongs at the loop's own
				// indent, flush with FOR/WHILE — not one level under it, which
				// is what the default continuation indent would give.
				emitNormalizedStatement(tokens, loopStmt.openSrcIdx, headerEnd, indentLevel, fw,
					/*continuationSameLevel=*/true);
				fw.NewLine();
			}
		}

			int bodyStart = skipEol(tokens, headerEnd + 1, (int)tokens.size());
			emitStatementSequence(loopStmt, tokens, bodyStart, loopStmt.closeSrcIdx, pol, indentLevel + 1, fw);

			emitEndClause(tokens, loopStmt.closeSrcIdx, indentLevel, fw);
		}

	// Emits the EXCEPTION section of a block:
	//
	//   exception
	//     when no_data_found then
	//       <statements>
	//     when dup_val_on_index or others then
	//       <statements>
	//
	// The Block's keywords list holds alternating WHEN/THEN entries after
	// the EXCEPTION entry, recorded by the tree builder during its scan.
	// exStart is the first token after EXCEPTION; blockEnd is block.closeSrcIdx.
	void emitExceptionSection(
		const FormatContext& block,
		const std::vector<OpenEditor::Token>& tokens,
		int exStart, int blockEnd,
		int indentLevel,
		const FormatterPolicy& pol,
		FormatWriter& fw)
	{
		IndentLevelGuard indentGuard(fw, indentLevel);

		// Collect WHEN/THEN pairs from the block's keyword list.
		// The tree builder records them in order: WHEN@w1, THEN@t1, WHEN@w2, ...
		struct Handler { int whenIdx; int thenIdx; int bodyEnd; };
		std::vector<Handler> handlers;
		{
			const auto& kws = block.keywords;
			int i = 0;
			// Skip entries before EXCEPTION.
			for (; i < (int)kws.size(); ++i)
				if (kws.at(i).token == etEXCEPTION) { ++i; break; }
			while (i < (int)kws.size())
			{
				if (kws.at(i).token == etWHEN)
				{
					int wi = kws.at(i).srcIdx;
					int ti = -1;
					if (i + 1 < (int)kws.size() && kws.at(i+1).token == etTHEN)
					{
						ti = kws.at(i+1).srcIdx;
						i += 2;
					}
					else ++i;
					handlers.push_back({ wi, ti, -1 });
				}
				else ++i;
			}
		}

		// Fill in bodyEnd for each handler (start of next WHEN, or blockEnd).
		for (int h = 0; h < (int)handlers.size(); ++h)
			handlers.at(h).bodyEnd = (h + 1 < (int)handlers.size())
				? handlers.at(h + 1).whenIdx
				: blockEnd;

		if (handlers.empty())
		{
			// No structured WHEN/THEN found — fall back to flat emit.
			emitStatementSequence(block, tokens, exStart, blockEnd, pol, indentLevel, fw);
			return;
		}

		// Everything between EXCEPTION and the first WHEN. Nothing else covers
		// that gap — the loop below starts at each WHEN token and a handler's
		// body range ends at the *next* WHEN — so a comment written right after
		// EXCEPTION used to vanish outright:
		//
		//     exception                  exception
		//     -- TODO partition    =>      when others then
		//       when others then
		//
		// Routed through emitStatementSequence() rather than replayed by hand so
		// a malformed section carrying real tokens is still emitted in full.
		// indentLevel + 1 is the handler indent IndentGuard gives WHEN below.
		{
			const int gapEnd = handlers.front().whenIdx;
			if (skipEol(tokens, exStart, gapEnd) < gapEnd)
				emitStatementSequence(block, tokens, exStart, gapEnd, pol, indentLevel + 1, fw);
		}

		for (const auto& h : handlers)
		{
			// "when <exception-list> then"
			if (h.thenIdx >= 0)
			{
				// Emit "when ... then" on one line.
				{
					IndentGuard handlerGuard(fw); // WHEN...THEN is one level deeper than EXCEPTION
					fw.WriteCurrentIndent();
					emitFlatTokens(tokens, h.whenIdx, h.thenIdx, fw);
				}
				fw.NewLine();
				// Handler body: statements between THEN+1 and bodyEnd.
				int bodyStart = skipEol(tokens, h.thenIdx + 1, h.bodyEnd);
				emitStatementSequence(block, tokens, bodyStart, h.bodyEnd, pol, indentLevel + 2, fw);
			}
			else
			{
				// Malformed: WHEN without THEN — emit flat.
				int bodyStart = skipEol(tokens, h.whenIdx, h.bodyEnd);
				emitStatementSequence(block, tokens, bodyStart, h.bodyEnd, pol, indentLevel + 1, fw);
			}
		}
	}

	// Emits DECLARE/BEGIN/EXCEPTION/END skeleton for one Block scope, recursing
	// into nested Blocks and delegating individual DML statements to the
	// existing, unmodified formatSQL().
	void emitBlock(
		const FormatContext& block,
		const std::vector<OpenEditor::Token>& tokens,
		const FormatterPolicy& pol,
		int indentLevel,
		FormatWriter& fw)
	{
		IndentLevelGuard indentGuard(fw, indentLevel);

		const ClauseEntry* declKw   = getKeyword(block.keywords, etDECLARE);
		const ClauseEntry* beginKw  = getKeyword(block.keywords, etBEGIN);
		const ClauseEntry* exceptKw = getKeyword(block.keywords, etEXCEPTION);

		// IS/AS-opened Block: a nested subprogram body (opened at IS/AS rather
		// than at DECLARE/BEGIN). The IS line was already emitted by
		// emitNestedSubprogramHeader; here we emit the implied declare section
		// (any nested declarations between IS and BEGIN) exactly like an
		// explicit DECLARE section, then fall through to emit BEGIN/body/END.
		const ClauseEntry* isAsKw = getKeyword(block.keywords, etIS);
		if (!isAsKw) isAsKw = getKeyword(block.keywords, etAS);

		int bodyEnd = exceptKw ? exceptKw->srcIdx : block.closeSrcIdx;

#ifdef _DEBUG
		// Whole-node coverage guard. For an IS/AS-opened Block the caller
		// (emitSubprogramHeader/emitTriggerHeader) already emitted the header
		// including the IS/AS token (== openSrcIdx), so this emitter's own span
		// starts at openSrcIdx+1; for a DECLARE/BEGIN-opened Block it starts at
		// openSrcIdx. The end extends past closeSrcIdx to the END line's ';'.
		int _guardStart = isAsKw ? isAsKw->srcIdx + 1 : block.openSrcIdx;
		EMIT_RANGE_GUARD(tokens, _guardStart,
						 endClauseGuardEnd(tokens, block.closeSrcIdx), fw);
#endif

		// FmtPlsqlBlankLineAfterUnitHeader only applies to package/type spec
		// and body headers (§11), handled in emitPackage. Standalone/nested
		// procedure and function bodies (§10.2) always follow the plain
		// block skeleton rules with no extra blank line here.

		if (declKw)
		{
			fw.WriteCurrentIndent();
			fw.WriteToken(tokens.at(declKw->srcIdx));
			fw.NewLine();
			int declStart = skipEol(tokens, declKw->srcIdx + 1, (int)tokens.size());
			int declEnd   = beginKw ? beginKw->srcIdx : bodyEnd;
			emitDeclareSection(block, tokens, declStart, declEnd, indentLevel + 1, pol, fw);
		}
		else if (isAsKw)
		{
			// IS/AS-opened: emit implied declare section if there is anything
			// between IS and BEGIN (nested subprograms, cursors, type decls, etc.).
			int declStart = skipEol(tokens, isAsKw->srcIdx + 1, (int)tokens.size());
			int declEnd   = beginKw ? beginKw->srcIdx : bodyEnd;
			if (declStart < declEnd)
				emitDeclareSection(block, tokens, declStart, declEnd, indentLevel + 1, pol, fw);
		}

		// A Block scope normally has a BEGIN keyword recorded (it's what
		// completes the header, whether opened by DECLARE or by BEGIN itself).
		// Fallback (no BEGIN in range — e.g. only a subprogram header was
		// selected/formatted): start right after the DECLARE/IS/AS token
		// that opened this scope, never at the opener token itself (using
		// block.openSrcIdx directly here would re-emit the IS/AS token as
		// if it were a body statement).
		int bodyStart = declKw  ? skipEol(tokens, declKw->srcIdx + 1, (int)tokens.size())
					  : isAsKw  ? skipEol(tokens, isAsKw->srcIdx + 1, (int)tokens.size())
					  :           block.openSrcIdx;
		if (beginKw)
		{
			fw.WriteCurrentIndent();
			fw.WriteToken(tokens.at(beginKw->srcIdx));
			fw.NewLine();
			bodyStart = skipEol(tokens, beginKw->srcIdx + 1, (int)tokens.size());
		}
		// When there is no BEGIN and a DECLARE/IS/AS "implied declare section"
		// was emitted above, that call already consumed the entire
		// [bodyStart..bodyEnd) range (declEnd == bodyEnd in that case).
		// Calling emitStatementSequence again over the same range would
		// re-emit those tokens (e.g. duplicating a truncated function's
		// trailing "/" terminator). Only emit the statement sequence here
		// when there is a real BEGIN body, or when no declare section was
		// emitted at all (plain fallback with no DECLARE/IS/AS handling above).
		if (beginKw || (!declKw && !isAsKw))
			emitStatementSequence(block, tokens, bodyStart, bodyEnd, pol, indentLevel + 1, fw);

		if (exceptKw)
		{
			fw.WriteCurrentIndent();
			fw.WriteToken(tokens.at(exceptKw->srcIdx));
			fw.NewLine();
			int exStart = skipEol(tokens, exceptKw->srcIdx + 1, (int)tokens.size());
			emitExceptionSection(block, tokens, exStart, block.closeSrcIdx, indentLevel, pol, fw);
		}

		// block.closeSrcIdx only points at a genuine "END" token for
		// well-formed input. When the source is malformed/truncated (e.g. a
		// subprogram header with no BEGIN/END body at all), buildTree's
		// final catch-all closes every still-open scope at the very last
			// token in the stream regardless of what it is — which may be the
			// script's trailing "/" terminator (already emitted verbatim by
			// emitStatementSequence's terminator handling above). Emitting it
			// again here as if it were "END" would duplicate it. Only emit the
			// closer when it is actually an END token.
			if (tokens.at(block.closeSrcIdx).token == etEND)
				emitEndClause(tokens, block.closeSrcIdx, indentLevel, fw);
		}

	// Emits a CaseExpr used in an assignment/return context:
	//
	//   v_name := case
	//               when cond then val
	//               else val
	//             end;
	//
	// CASE stays on the same line as the prefix.
	// WHEN/ELSE align one indent step past CASE's column.
	// END aligns at CASE's column.
	// Total-short form: if the entire statement fits on one line (governed by
	// FmtCaseWhenShortMaxLen) it is emitted as a single statement.
	//
	// prefixStart..prefixEnd (inclusive, skipping EOL) are the tokens before
	// CASE (e.g. "v_name :=").  caseNode is the CaseExpr scope.
	void emitCaseExpr(
		const FormatContext& caseNode,
		const std::vector<OpenEditor::Token>& tokens,
		int prefixStart, int prefixEnd,
		const FormatterPolicy& pol,
		FormatWriter& fw,
		bool trailingSemicolon /*= true*/,
		bool forceMultiLine /*= false*/)
	{
		const int shortMax = pol.caseWhenShortMaxLen;

		// Build prefix string (normalized spacing). Empty when prefixEnd < prefixStart.
		std::wstring prefix = (prefixEnd >= prefixStart)
			? joinTokensSingleSpaced(tokens, prefixStart, prefixEnd + 1)
			: std::wstring();

		// Build the full flat CASE expression (everything from CASE to END inclusive).
		std::wstring flat = joinTokensSingleSpaced(tokens, caseNode.openSrcIdx, caseNode.closeSrcIdx + 1);

		// Locate the ';' token after END (used verbatim when trailingSemicolon is true).
		int semiIdx = trailingSemicolon ? nextSemicolon(tokens, caseNode.closeSrcIdx) : -1;

		// Total-short: emit "indent [prefix ]case…end[;]" on one line.
		// Suppressed when forceMultiLine is set (nested case inside outer WHEN clause).
		// totalLen measures content only (excluding leading indent) so the short-form
		// decision is indent-independent — same rule regardless of nesting depth.
		std::wstring prefixSep = prefix.empty() ? L"" : L" ";
		int totalLen = (int)(prefix.size() + prefixSep.size() + flat.size()
			+ (semiIdx >= 0 ? 1 : 0));
		if (!forceMultiLine && shortMax > 0 && totalLen <= shortMax && !caseNode.hasComment)
		{
			fw.WriteCurrentIndent();
			fw.WriteText(prefix);
			if (!prefixSep.empty()) fw.WriteSpaces(1);
			fw.WriteText(flat);
			if (semiIdx >= 0) fw.WriteToken(tokens.at(semiIdx));
			fw.NewLine();
			return;
		}

		// Multi-line form.
		// caseCol = column where CASE starts (0-based).
		// whenPad = caseCol + sp  (one indent step past CASE).
		// endPad  = caseCol       (aligned under CASE).
		// When there is no prefix, CASE starts at the current indent column.
		int caseCol = prefix.empty()
			? (int)fw.CurrentIndentChars()
			: (int)(fw.CurrentIndentChars() + prefix.size() + 1); // +1 for the space before "case"
		int whenPadChars     = caseCol + fw.GetIndentSpacing();
		int whenNextPadChars = caseCol + 2 * fw.GetIndentSpacing();
		int endPadChars      = caseCol;

		// Find first WHEN/ELSE clause to know where the CASE header ends.
		const ClauseEntry* firstClause = nullptr;
		for (const auto& ce : caseNode.keywords)
			if (ce.token == etWHEN || ce.token == etELSE)
				{ firstClause = &ce; break; }

		int headerEnd = firstClause ? firstClause->srcIdx : caseNode.closeSrcIdx;
		std::wstring header = joinTokensSingleSpaced(tokens, caseNode.openSrcIdx, headerEnd);

		// First line: "indent [prefix ]case [selector]"
		{
			fw.WriteCurrentIndent();
			fw.WriteText(prefix);
			if (!prefixSep.empty()) fw.WriteSpaces(1);
			fw.WriteText(header);
			fw.NewLine();
		}

		// WHEN/ELSE clauses.
		// For each clause: emit "whenPad tokens…" but if the clause value itself
		// is (or contains) a nested CaseExpr child, recurse into emitCaseExpr so
		// the nested CASE gets proper column alignment instead of being flattened.
		for (const auto& ce : caseNode.keywords)
		{
			if (ce.token == etCASE) continue;
			int clauseEnd = ce.closeSrcIdx >= 0 ? ce.closeSrcIdx : caseNode.closeSrcIdx;

			// Check for a nested CaseExpr child whose open token falls inside
			// this clause's value range.
			const FormatContext* nestedCase = nullptr;
			for (const auto& child : caseNode.children)
			{
				if (child.kind == FormatContext::CaseExpr
				&&  child.openSrcIdx > ce.srcIdx
				&&  child.openSrcIdx < clauseEnd)
					nestedCase = &child;
			}

			if (nestedCase)
			{
				// Emit tokens before the nested CASE (e.g. "when cond") on its own line.
				int prefEnd = nestedCase->openSrcIdx - 1;
				while (prefEnd > ce.srcIdx && tokens.at(prefEnd).token == etEOL) --prefEnd;
				fw.WriteSpaces(whenPadChars);
				fw.WriteText(joinTokensSingleSpaced(tokens, ce.srcIdx, prefEnd + 1));
				fw.NewLine();
				// Recurse for the nested CASE expression.
				{
					IndentLevelGuard indentGuard(fw, -1); // only for restore
					fw.SetIndentChars(whenNextPadChars);
					emitCaseExpr(*nestedCase, tokens, prefEnd, prefEnd - 1 /*empty prefix*/,
						pol, fw, false /*trailingSemicolon*/, true /*forceMultiLine*/);
				}
				// Emit any tokens after the nested CASE close (e.g. "then 'y'")
				// on a continuation line at whenPad level.
				int afterClose = skipEol(tokens, nestedCase->closeSrcIdx + 1, clauseEnd);
				if (afterClose < clauseEnd)
				{
					fw.WriteSpaces(whenPadChars);
					fw.WriteText(joinTokensSingleSpaced(tokens, afterClose, clauseEnd));
					fw.NewLine();
				}
			}
			else
			{
				fw.WriteSpaces(whenPadChars);
				fw.WriteText(joinTokensSingleSpaced(tokens, ce.srcIdx, clauseEnd));
				fw.NewLine();
			}
		}

		// END[;] aligned at CASE column.
		fw.WriteSpaces(endPadChars);
		fw.WriteToken(tokens.at(caseNode.closeSrcIdx));
		if (semiIdx >= 0) fw.WriteToken(tokens.at(semiIdx));
		fw.NewLine();
	}

	// Emits a CASE statement scope:
	//   CASE [selector]
	//     WHEN cond THEN
	//       stmts
	//     ELSE
	//       stmts
	//   END CASE;
	// Short forms (governed by FmtCaseWhenShortMaxLen, same as SQL formatter):
	//   - Total-short: entire CASE…END CASE on one line when formattedLen fits.
	//   - Per-WHEN-short: each WHEN…THEN stmts on one line when that clause fits.
	//
	// Routed through a single FormatWriter instance (see OEPlsSqlFormatterCommon.h /
	// md/plsql_formatter_trace_writer_proposal.md) so every token and structural
	// whitespace flows through named methods. The FormatWriter& fw signature is
	// kept unchanged for now (compatibility-shim step).
	void emitCaseStmt(
		const FormatContext& caseNode,
		const std::vector<OpenEditor::Token>& tokens,
		const FormatterPolicy& pol,
		int indentLevel,
		FormatWriter& fw)
	{
		IndentLevelGuard indentGuard(fw, indentLevel);
		const int shortMax = pol.caseWhenShortMaxLen;

		EMIT_RANGE_GUARD(tokens, caseNode.openSrcIdx,
						 endClauseGuardEnd(tokens, caseNode.closeSrcIdx), fw);

		// Total-short: emit entire CASE…END CASE; on one line.
		if (shortMax > 0
		&&  caseNode.formattedLen <= shortMax
		&&  !caseNode.hasComment
		&&  caseNode.closeSrcIdx >= 0)
		{
			// Find END CASE ; — closeSrcIdx is END, next non-EOL should be CASE then ;
			int endIdx = caseNode.closeSrcIdx;
			int j = endIdx + 1;
			while (j < (int)tokens.size() && tokens.at(j).token == etEOL) ++j;
			int caseQualIdx = (j < (int)tokens.size() && tokens.at(j).token == etCASE) ? j : -1;
			int semiIdx2 = nextSemicolon(tokens, caseQualIdx >= 0 ? caseQualIdx : endIdx);
			// emit CASE…END joined, add the CASE qualifier and ;
			fw.WriteCurrentIndent();
			fw.WriteText(joinTokensSingleSpaced(tokens, caseNode.openSrcIdx, endIdx + 1));
			if (caseQualIdx >= 0) { fw.WriteSpaces(1); fw.WriteToken(tokens.at(caseQualIdx)); }
			if (semiIdx2 >= 0) fw.WriteToken(tokens.at(semiIdx2));
			fw.NewLine();
			return;
		}

		// Multi-line form.
		// Find first real WHEN or ELSE clause (skip the etCASE keyword entry).
		const ClauseEntry* firstClause = nullptr;
		for (const auto& ce : caseNode.keywords)
			if (ce.token == etWHEN || ce.token == etELSE)
				{ firstClause = &ce; break; }

		// Emit CASE [selector] header line.
		int headerEnd = firstClause ? firstClause->srcIdx : caseNode.closeSrcIdx;
		emitNormalizedStatement(tokens, caseNode.openSrcIdx, headerEnd - 1, indentLevel, fw);
		fw.NewLine();

		for (size_t k = 0; k < caseNode.keywords.size(); ++k)
		{
			const ClauseEntry& ce = caseNode.keywords.at(k);
			if (ce.token == etCASE || ce.token == etTHEN) continue;

			// Find associated THEN (for WHEN clauses).
			const ClauseEntry* thenCe = nullptr;
			if (ce.token == etWHEN)
			{
				for (size_t m = k + 1; m < caseNode.keywords.size(); ++m)
					if (caseNode.keywords.at(m).token == etTHEN)
						{ thenCe = &caseNode.keywords.at(m); break; }
			}

			// Body range: after THEN (or ELSE) up to this clause's closeSrcIdx.
			int bodyStart = (thenCe ? thenCe->srcIdx : ce.srcIdx) + 1;
			int bodyEnd   = ce.closeSrcIdx >= 0 ? ce.closeSrcIdx : caseNode.closeSrcIdx;

			// Per-WHEN/ELSE short: entire clause on one line when it fits.
			bool doShort = (shortMax > 0
				&& !ce.hasComment
				&& ce.closeSrcIdx >= 0
				&& ce.formattedLen != INT_MAX
				&& ce.formattedLen <= shortMax);

			if (doShort)
			{
				// Emit WHEN cond THEN stmts (or ELSE stmts) all on one line.
				// ce covers srcIdx..(closeSrcIdx-1), strip trailing EOL.
				int lineEnd = bodyEnd - 1;
				while (lineEnd > bodyStart && tokens.at(lineEnd).token == etEOL) --lineEnd;
				fw.PushIndent(); fw.WriteCurrentIndent(); fw.PopIndent();
				fw.WriteText(joinTokensSingleSpaced(tokens, ce.srcIdx, lineEnd + 1));
				fw.NewLine();
			}
			else
			{
				// Header line: WHEN cond THEN  or  ELSE
				int headerLineEnd = thenCe ? thenCe->srcIdx + 1 : ce.srcIdx + 1;
				emitNormalizedStatement(tokens, ce.srcIdx, headerLineEnd - 1, indentLevel + 1, fw);
				fw.NewLine();
				// Body statements (indented one extra level).
				int bstart = skipEol(tokens, bodyStart, bodyEnd);
				emitStatementSequence(caseNode, tokens, bstart, bodyEnd, pol, indentLevel + 2, fw);
			}
		}

		// END CASE;
		emitEndClause(tokens, caseNode.closeSrcIdx, indentLevel, fw);
	}

	// Slices [rangeStart, rangeEnd) into statements:
	// recursed into, DML children (Select/Insert/Update/Delete/Merge) are
	// extracted as a subrange and formatted via the existing formatSQL(), and
	// any other raw PL/SQL fragment (assignment, procedure call, IF/LOOP/
	// exception-handler header, etc. — not yet structurally understood) is
	// emitted as a single joined line.
	void emitStatementSequence(
		const FormatContext& node,
		const std::vector<OpenEditor::Token>& tokens,
		int rangeStart, int rangeEnd,
		const FormatterPolicy& pol,
		int indentLevel,
		FormatWriter& fw)
	{
		IndentLevelGuard indentGuard(fw, indentLevel);

#ifdef _DEBUG
		// Whole-range token-fidelity guard over the statement sequence this
		// dispatcher owns. rangeEnd is exclusive, so the guarded span ends at
		// rangeEnd - 1. Structural children (Block/IfStmt/LoopStmt/CaseStmt)
		// carry their own narrower guards; this one additionally covers the
		// loose statements *between* structures, which no child guard sees.
		//
		// Recursive invocations install their own guards over their sub-ranges.
		// That is not double-counting: each guard compares only the text
		// appended during its own scope against its own expected span.
		EMIT_RANGE_GUARD(tokens, rangeStart, rangeEnd - 1, fw);
#endif

		int pos = skipEol(tokens, rangeStart, rangeEnd);

		// We insert a blank line between consecutive SQL/PL-SQL statements at
		// the same block-body indent level (§7, FmtPlsqlDmlBlankLineSeparator),
		// but NOT between a comment and the statement that immediately follows
		// it.
		const bool blankBeforeComment = pol.plsqlBlankLineBeforeComment;
		const bool blankAroundFlow = pol.plsqlBlankLineAroundFlowStmts;
		const bool blankAroundDml = pol.plsqlDmlBlankLineSeparator;
		bool firstItem    = true;  // no item emitted yet in this sequence
		bool prevWasComment = false; // last emitted item was a comment
		bool prevWasFlowStmt = false; // last emitted item was a flow-control construct (§6.5)
		bool prevWasDml = false; // last emitted item was a DML statement (§7)
		bool prevWasNamedUnit = false; // last emitted item was a named unit (subprogram/package/type/trigger body)
		// True when the last item emitted was an end-of-line comment that
		// tryEmitTrailingEndComment() rejoined onto a line which had already
		// closed a statement (';' or a lone script '/'). Such a comment belongs
		// to that statement — it does not start an own-line comment run — so
		// FmtPlsqlBlankLineBeforeComment must still be able to fire for the
		// *next* own-line comment, which does follow a closed statement. Without
		// this, switching on FmtPlsqlEndLineCommentKeepTrailing silently deleted
		// the blank line the comment policy is supposed to insert (the author's
		// own paragraph break after "SELECT ...; -- note" was lost).
		bool trailingOnClosedStmt = false;

		// Helper: emit blank separator before a statement item when appropriate.
		// Comments are separated per FmtPlsqlBlankLineBeforeComment (at any
		// indent level, but never after another comment or as the first item,
		// and only when the previous item is a *closed* statement — i.e. the
		// last non-blank output ends with ';' or with a script '/' standing
		// alone on its own line); flow-control constructs (IF/
		// LOOP/CASE statements and nested anonymous DECLARE/BEGIN blocks) are
		// separated per FmtPlsqlBlankLineAroundFlowStmts (§6.5) regardless of
		// what precedes/follows them; DML statements (SELECT INTO/INSERT/
		// UPDATE/DELETE/MERGE) are separated from other statements (DML or
		// plain PL/SQL) per FmtPlsqlDmlBlankLineSeparator (§7) whenever
		// *either* this item or the previous one is DML — two consecutive
		// plain PL/SQL statements (e.g. "null; null;") are NOT separated by
		// this rule. A named unit (subprogram/package/type/trigger body) —
		// e.g. two top-level "CREATE TYPE ...;" statements with no "/" between
		// them — is always separated from whatever precedes/follows it,
		// unconditionally (mirrors the blank line already forced after the
		// script terminator "/"; not governed by any setting). Never produce
		// a double blank line.
		//
		// All blank-line decisions below route through a local FormatWriter
		// (OEPlsSqlFormatterCommon.h) wrapping the same `out` buffer, so the
		// blank-line collapsing rule lives in one place (EmptyLine()) instead
		// of being hand-rolled at each of these three call sites — see
		// md/plsql_formatter_trace_writer_proposal.md for the motivating bug
		// (this exact duplication contributed to the CREATE TYPE
		// misclassification bug being harder to trace than necessary).
		// "Is the previous item closed?" — the gate for putting a blank line
		// before a comment. ';' is the ordinary closer, but a compilation unit
		// may instead be closed by a script '/' standing alone on its line.
		//
		// That '/' reaches the output by one of two routes. When no tree node
		// owns it (the usual case: the unit ends with ';' and the '/' sits
		// between two top-level units) the compilation-unit-terminator arm
		// below emits it and appends the separating blank line itself. When the
		// '/' *is* the node's own terminator (TermSlash — a body-less header or
		// a CREATE TYPE with no trailing ';'), the node's emitter consumes it
		// and that arm never runs, so the blank line has to come from here
		// instead. Without it, a unit terminated by '/' alone was glued to the
		// comment following it.
		auto prevIsClosedStmt = [&]()
		{
			return fw.EndsWithSemicolon() || fw.EndsWithScriptSlash();
		};
		auto blankBefore = [&](bool isComment, bool isDml = false)
		{
			// A blank line before *any* item is also forced when the previous
			// item was a flow-control construct (§6.5's "blank line after"
			// half), as long as this item is not a comment being suppressed
			// by its own rule and not the first item.
			//
			// A comment that tryEmitTrailingEndComment() rejoined to its
			// statement's line is part of that statement, not the head of an
			// own-line comment run. It must therefore neither trip the "never
			// blank after another comment" suppression, nor fail the "previous
			// item is a closed statement" test — the output line now ends with
			// comment text, so EndsWithSemicolon() cannot see the ';' under it.
			// Both reads are gated on prevWasComment, which every non-comment
			// arm below clears, so a stale trailingOnClosedStmt is unreachable.
			const bool prevWasTrailingJoin   = prevWasComment && trailingOnClosedStmt;
			const bool prevWasOwnLineComment = prevWasComment && !trailingOnClosedStmt;
			bool want = isComment
				? (blankBeforeComment && !firstItem && !prevWasOwnLineComment
				   && (prevIsClosedStmt() || prevWasTrailingJoin))
				: (!firstItem && !prevWasComment && (prevWasNamedUnit || (blankAroundDml && (isDml || prevWasDml)) || (blankAroundFlow && prevWasFlowStmt)));
			if (want)
				fw.EmptyLine();
		};
		// Separator for named units (standalone subprogram/package/type/
		// trigger bodies) — unconditional, fires on both sides whenever
		// adjacent to any other item, regardless of nesting level or setting.
		auto blankAroundNamedUnit = [&]()
		{
			if (!firstItem && !prevWasComment)
				fw.EmptyLine();
		};
		// Separator for flow-control constructs (IfStmt/LoopStmt/CaseStmt and
		// nested anonymous blocks) — governed solely by FmtPlsqlBlankLineAroundFlowStmts,
		// fires on both sides of the construct (i.e. whenever it is adjacent to
		// any other item), regardless of nesting level.
		auto blankAroundFlowStmt = [&]()
		{
			if (blankAroundFlow && !firstItem && !prevWasComment)
				fw.EmptyLine();
		};
		// True when the source carried at least one *empty* line immediately
		// before token index `idx`. PlSqlParserEx::PutLine() hands the tokenizer
		// one physical line at a time and each contributes its own etEOL, so a
		// blank line between two statements arrives as two consecutive etEOL
		// tokens — one closing the statement's line, one for the empty line
		// itself. Bounded by rangeStart so a nested sequence never reads EOLs
		// that belong to its parent.
		auto srcBlankLineBefore = [&](int idx)
		{
			int eols = 0;
			for (int j = idx - 1; j >= rangeStart && tokens.at(j).token == etEOL; --j)
				++eols;
			return eols >= 2;
		};

		// ---- Progress checkpoints (Write stage) ----
		// One iteration of the loop below consumes exactly one root-level item,
		// so the iteration counter is this pass's statement number and the root
		// child count is the closest available total. They are not in lockstep —
		// a lone script '/' or a flat-token fallback consumes an iteration
		// without owning a child — so current is clamped to total rather than
		// allowed to run past it. Nested invocations stay silent: a statement
		// inside a BEGIN body is not a document-level statement, and reporting
		// it would make the counter jump around. See OEPlsSqlFormatter.h.
		const bool isRootSeq   = (node.kind == FormatContext::Root);
		const int  rootItemTot = isRootSeq ? (int)node.children.size() : 0;
		int        rootItemNo  = 0;

		while (pos < rangeEnd)
		{
			if (isRootSeq && formatterProgressActive())
			{
				++rootItemNo;
				reportFormatterProgress(FormatStage::Write,
					rootItemNo < rootItemTot ? rootItemNo : rootItemTot,
					rootItemTot);
			}

			// ---- Compilation-unit terminator: "/" on its own line ----
			// The lexer (PlsSqlParser::Tokenize) reclassifies a lone '/' that
			// sits alone on its line from etSLASH to etEOS (while keeping its
			// "/" value), so it is NOT matched as etSLASH here — etSLASH never
			// reaches this point for a real script terminator.
			// Step 2.2: that lexer quirk is now encoded once, inside
			// classifyTerminator(), so this arm asks for TermSlash instead of
			// re-testing `etEOS && value == "/"` inline. No tree node owns a
			// bare script '/' — it sits *between* top-level units, so buildTree
			// never creates a child for it — which is why the token, rather
			// than a node field, is still the source at this one site.
			// (Zero-length synthetic etEOS sentinels appended by
			// appendEosSentinel() have an empty value, classify as
			// TermSemicolon, and are therefore excluded here as before.)
			// Must be checked first, before the header lookahead below (which
			// scans forward for the next PROCEDURE/FUNCTION/... Block and
			// would otherwise silently swallow everything between pos and
			// that header, including this '/', when it sits between two
			// top-level units). Emit it verbatim at the current indent, with
			// no blank line before or after it, and advance past it.
			if (classifyTerminator(tokens.at(pos)) == FormatContext::TermSlash)
			{
				// Layout invariant L1: a script '/' stands alone on its own
				// line, at column 0. The preceding item may have been a
				// spliced DML run or a flat-token fallback run that ended
				// mid-line (no trailing newline), so close the open line
				// first — otherwise the '/' is appended to the statement
				// text, the output lexer reclassifies it from etEOS to
				// etSLASH on re-parse, and the losslessness check fails on
				// the changed token identity. WriteToken's L1 guard verifies
				// this in debug builds.
				if (!fw.AtLineStart())
					fw.NewLine();
				fw.WriteToken(tokens.at(pos)); // always put EOS at the beginning of the line
				fw.NewLine();
				pos = skipEol(tokens, pos + 1, rangeEnd);
				// If another top-level unit follows this terminator, always
				// separate it with a blank line — matching the spacing
				// already produced between two sequential anonymous PL/SQL
				// blocks — regardless of FmtPlsqlDmlBlankLineSeparator/
				// FmtPlsqlBlankLineAroundFlowStmts/prevWasFlowStmt, which
				// govern unrelated statement-separation rules, not the
				// script terminator itself.
				if (pos < rangeEnd)
					fw.NewLine();
				// Keep firstItem/prevWasComment/prevWasFlowStmt unchanged so
				// the '/' itself never affects the blank-line separator rules
				// applied to the statements before/after it.
				continue;
			}

			// ---- Label: <<name>> ----
			// Emitted on its own line at the current indent level.
			if (tokens.at(pos).token == etLABEL_BEGIN)
			{
				blankBefore(false);
				// Consume: <<  name  >>
				fw.WriteCurrentIndent();
				fw.WriteToken(tokens.at(pos)); // <<
				int i = pos + 1;
				while (i < rangeEnd && tokens.at(i).token == etEOL) ++i;
				if (i < rangeEnd && tokens.at(i).token != etLABEL_END)
				{
					fw.WriteToken(tokens.at(i)); // label name
					++i;
				}
				while (i < rangeEnd && tokens.at(i).token == etEOL) ++i;
				if (i < rangeEnd && tokens.at(i).token == etLABEL_END)
				{
					fw.WriteToken(tokens.at(i)); // >>
					++i;
				}
				fw.NewLine();
				pos = skipEol(tokens, i, rangeEnd);
				firstItem = false; prevWasComment = false; prevWasFlowStmt = false; prevWasDml = false; prevWasNamedUnit = false;
				continue;
			}

			const FormatContext* match = nullptr;
			for (const auto& child : node.children)
			{
				if (child.openSrcIdx == pos) { match = &child; break; }
			}

			// Structural comment node. buildTree() recorded whether the author
			// wrote it at the end of a line of code (FormatContext::trailing);
			// if so it is rejoined to that line, because own-lining it moves it
			// in front of the *next* statement and silently re-attaches it
			// there — a meaning change, not a style one. When the join is
			// refused (the width veto, the swallow invariant, a lone script
			// '/', a sink that cannot retract, or the setting being off) it
			// falls back to the original own-line emission below, so a
			// demotion costs layout only and never the comment itself.
			//
			// tryEmitTrailingEndComment() has to be tried *before*
			// blankBefore()/WriteCurrentIndent(): both would land between the
			// code and its comment.
			if (match && match->kind == FormatContext::Comment)
			{
				const OpenEditor::Token& ctk = tokens.at(match->openSrcIdx);
				// Captured *before* the join: ResumeLineForEndComment() puts the
				// comment text where prevIsClosedStmt() would otherwise still
				// see the statement's ';', so asking afterwards always says no.
				const bool closedBefore = prevIsClosedStmt();
				if (!tryEmitTrailingEndComment(fw, ctk, match->trailing, pol))
				{
					// blankBefore() is evaluated while trailingOnClosedStmt
					// still describes the *preceding* item: this own-line
					// comment may be exactly the one that follows a rejoined
					// trailing comment and is entitled to the policy blank.
					blankBefore(true);
					fw.WriteCurrentIndent();
					fw.WriteToken(ctk);
					fw.NewLine();
					trailingOnClosedStmt = false;
				}
				else
					trailingOnClosedStmt = closedBefore;
				pos = skipEol(tokens, pos + 1, rangeEnd);
				firstItem = false; prevWasComment = true; prevWasFlowStmt = false; prevWasDml = false; prevWasNamedUnit = false;
				continue;
			}

			// Root-level unrecognized statement: emitted verbatim (layout-
			// preserving), using the tree node's own [openSrcIdx, closeSrcIdx]
			// bounds rather than falling through to the generic raw-fragment
			// scanner below. closeSrcIdx is the terminating ';'/'/' token
			// itself (set by the ordinary statement-boundary handler in
			// buildTree), so it is included in the emitted range.
			if (match && match->kind == FormatContext::UnknownStmt)
			{
				blankBefore(false);
				// Root level only: an opaque statement is separated from its
				// neighbours exactly where the author separated it. None of the
				// setting-driven rules in blankBefore() can fire for an
				// UnknownStmt — it is neither DML, nor a flow construct, nor a
				// named unit — so without this the paragraph structure of a
				// script (the blank lines grouping SET/SPOOL/@ runs apart from
				// the statements they configure) was silently collapsed into
				// one dense block. Reproducing the author's own breaks is the
				// same "do not reinterpret what you do not understand" policy
				// that makes the body verbatim; inventing a separator after
				// every line command would blow up the tight SET/SPOOL runs
				// install scripts are written with.
				//
				// EmptyLine() collapses, so a separator already written by
				// blankBefore() or by the script-'/' arm is never doubled.
				if (node.kind == FormatContext::Root
				&&  !firstItem
				&&  srcBlankLineBefore(pos))
					fw.EmptyLine();
				int stmtEnd = match->closeSrcIdx >= pos ? match->closeSrcIdx : pos;
				// A script '/' terminator is not part of the verbatim body:
				// the terminator arm at the top of this loop emits it alone
				// at line start (L1 invariant) with the blank-line policy.
				// Emitting it inside the fragment would place it mid-line,
				// where the losslessness re-lex reads it as division.
				// ';' — and line-bound SQL*Plus commands, which carry no
				// terminator at all — stay inside the verbatim range.
				if (stmtEnd > pos && tokens.at(stmtEnd).token == etEOS)
				{
					emitVerbatimFragment(tokens, pos, stmtEnd - 1, indentLevel, fw, true);
					fw.NewLine();
					pos = skipEol(tokens, stmtEnd, rangeEnd); // land on the '/'
				}
				else
				{
					emitVerbatimFragment(tokens, pos, stmtEnd, indentLevel, fw, true);
					fw.NewLine();
					pos = skipEol(tokens, stmtEnd + 1, rangeEnd);
				}
				firstItem = false; prevWasComment = false; prevWasFlowStmt = false; prevWasDml = false; prevWasNamedUnit = false;
				continue;
			}

			// Forward-declared / body-less subprogram header (Step 2.1 of
			// md/plsql_formatter_buildtree_migration_plan.md):
			//   PROCEDURE foo(...);            PROCEDURE foo;
			//   CREATE OR REPLACE FUNCTION f RETURN NUMBER DETERMINISTIC
			//   /
			// buildTree records the whole span — including any CREATE
			// [OR REPLACE] prefix and method modifiers — as a
			// SubprogramForwardDecl node whose closeSrcIdx is the terminating
			// ';' or script '/' itself, so the header is emitted straight from
			// the tree with no raw-token re-scanning (this is the arm that
			// retires the old tryParseNestedSubprogramHeader probe below, and
			// with it the historical missed-CREATE-OR-REPLACE bug).
			//
			// Dispatched this early on purpose: it must run before the
			// IS/AS-Block lookahead below, which would otherwise treat the
			// forward decl's tokens as a stray header prefix of a *later*
			// subprogram body and silently drop them. A named unit is always
			// separated from its neighbours by a blank line, exactly like the
			// body-carrying Block path.
			if (const FormatContext* fwd = findForwardDeclChild(node, tokens, pos, rangeEnd))
			{
				blankAroundNamedUnit();
				emitSubprogramForwardDecl(*fwd, tokens, pol, indentLevel, fw, pos);
				pos = skipEol(tokens, fwd->closeSrcIdx + 1, rangeEnd);
				firstItem = false; prevWasComment = false; prevWasFlowStmt = false; prevWasDml = false; prevWasNamedUnit = true;
				continue;
			}

			// FORALL header:
			// The fragment scanner would cut the range at the DML child's openSrcIdx,
			// so we must handle this before the fragment scanner runs.
			if (tokens.at(pos).token == etUNKNOWN
			&&  !_wcsicmp(tokens.at(pos).value.c_str(), L"FORALL"))
			{
				// Find the first DML or Block child that starts after pos — that is
				// the FORALL target statement.
				const FormatContext* dmlChild = nullptr;
				for (const auto& ch : node.children)
				{
					if (ch.openSrcIdx <= pos) continue;
					if (isDmlKind(ch.kind) || ch.kind == FormatContext::Block)
					{ dmlChild = &ch; break; }
				}
				if (dmlChild)
				{
					blankBefore(false, isDmlKind(dmlChild->kind));
					// Emit forall header: tokens from pos up to (not including) the DML.
					int headerEnd = dmlChild->openSrcIdx - 1;
					while (headerEnd > pos && tokens.at(headerEnd).token == etEOL) --headerEnd;
					fw.WriteCurrentIndent();
					emitFlatTokens(tokens, pos, headerEnd, fw);
					fw.NewLine();
					// Emit the DML child indented.
					if (isDmlKind(dmlChild->kind))
					{
						int dEnd = dmlChild->closeSrcIdx >= 0 ? dmlChild->closeSrcIdx : rangeEnd;
						size_t subEnd = std::min((size_t)dEnd + 1, tokens.size());
						std::vector<OpenEditor::Token> sub(
							tokens.begin() + dmlChild->openSrcIdx,
							tokens.begin() + subEnd);
						// E2b — token-run delegation: SpliceInto() reproduces
						// WriteIndentText()'s per-line indenting while preserving the
						// nested formatter's token types. concatTokenValues() is the
						// guard's text oracle, inlined because SQL_SLICE_GUARD and
						// TRACE compile away in Release.
						std::vector<OpenEditor::Token> formatted = formatSQL(sub, pol);
						SQL_SLICE_GUARD(sub, concatTokenValues(formatted));
						trimTrailingWs(formatted);
#ifdef _DEBUG
						TRACE(L"emitStatementSequence(forall) %s", concatTokenValues(formatted).c_str());
#endif
						fw.SpliceInto(formatted, fw.GetNextIndentChars());
						fw.NewLine();
						pos = skipEol(tokens, dEnd + 1, rangeEnd);
					}
					else
					{
						// Nested block (e.g. FORALL … EXECUTE IMMEDIATE).
						emitBlock(*dmlChild, tokens, pol, indentLevel + 1, fw);
						pos = skipEnder(tokens, dmlChild->closeSrcIdx, rangeEnd);
					}
					firstItem = false; prevWasComment = false; prevWasFlowStmt = false;
					prevWasDml = isDmlKind(dmlChild->kind);
					prevWasNamedUnit = false;
					continue;
				}
				// No DML child found — fall through to normal fragment handling.
			}

			// Lookahead: if no direct match at pos, check whether the next
			// child ahead is an IS/AS-opened Block (standalone PROCEDURE /
			// FUNCTION whose header precedes the IS token). If so, treat the
			// tokens [pos, isIdx) as the header and the Block as the body.
			if (!match)
			{
				for (const auto& child : node.children)
				{
					if (child.kind != FormatContext::Block) continue;
					if (child.openSrcIdx <= pos) continue;

					// The lookahead may only claim a Block whose header really
					// starts here. buildTree records headerStart as the "first
					// non-EOL/non-comment token of the unit header"; if a *real*
					// token sits in [pos, headerStart) then those tokens are a
					// preceding statement in their own right, and accepting the
					// Block drops them — every emitter reached below begins
					// writing at headerStart, so nothing ever emits the range
					// before it.
					//
					// A root-level CREATE TYPE forward declaration is exactly
					// that case: it gets no node of its own, so pos lands on its
					// CREATE, and the hasProcFunc scan below then sees its
					// etTYPE and accepts the *next* unit's Block instead:
					//
					//     CREATE TYPE fwd_t;      <-- silently dropped
					//     /                       <-- silently dropped
					//     -- comment              <-- silently dropped
					//     CREATE OR REPLACE PACKAGE p AS ... END p;
					//     /
					//
					// Both EMIT_RANGE_GUARD and the losslessness check report
					// it. This is the same failure mode findForwardDeclChild()
					// above exists to prevent for subprogram forward
					// declarations, extended to the CREATE TYPE spelling.
					//
					// Comment/EOL-only gaps still pass: buildTree promotes
					// root-level comments to Comment children, which the direct
					// match above consumes before pos ever reaches here, and the
					// nested-scope cases that rely on the lenient fallback keep
					// their existing behaviour. headerStart < 0 means buildTree
					// recorded no header (anonymous block), where the fallback
					// below legitimately starts at pos.
					bool headerStartsHere = true;
					for (int j = pos; child.headerStart > pos && j < child.headerStart; ++j)
					{
						const EToken tj = tokens.at(j).token;
						if (tj != etEOL && tj != etCOMMENT)
						{ headerStartsHere = false; break; }
					}
					if (!headerStartsHere) continue;

					const ClauseEntry* isAsKw = getKeyword(child.keywords, etIS);
					if (!isAsKw) isAsKw = getKeyword(child.keywords, etAS);
					// Accept both IS/AS-opened blocks AND BEGIN-opened blocks
					// that have a TRIGGER token in their header range.
					bool hasTriggerInHeader = false;
					if (!isAsKw)
					{
						// BEGIN-opened block: check header tokens [pos, openSrcIdx).
						for (int j = pos; j < child.openSrcIdx; ++j)
							if (tokens.at(j).token == etTRIGGER)
							{ hasTriggerInHeader = true; break; }
						if (!hasTriggerInHeader) continue;
					}
					// Verify the header tokens [pos, isIdx) belong to this
					// statement: there must be no other child scope between pos
					// and isIdx (no stray Block/If/Loop in the header).
					bool clear = true;
					for (const auto& other : node.children)
					{
						if (&other == &child) continue;
						if (other.openSrcIdx > pos && other.openSrcIdx < child.openSrcIdx
						&&  other.kind != FormatContext::Paren
						&&  other.kind != FormatContext::ParamList
						&&  other.kind != FormatContext::Comment)
						{ clear = false; break; }
					}
					if (!clear) continue;
					// Also check there's a PROCEDURE/FUNCTION/PACKAGE/TRIGGER token in [pos, isIdx).
					// isTypeKeyword() keeps a "%TYPE" datatype attribute in the
					// header ("RETURN emp.employee_id%TYPE") from counting as the
					// TYPE keyword — it lexes as etTYPE too.
					bool hasProcFunc = false;

					for (int j = pos; j < child.openSrcIdx; ++j)
					{
						if (tokens.at(j).token == etPROCEDURE || tokens.at(j).token == etFUNCTION
						||  tokens.at(j).token == etPACKAGE   || tokens.at(j).token == etTRIGGER
						||  isTypeKeyword(tokens, j))
						{ hasProcFunc = true; break; }
					}
					if (hasProcFunc) { match = &child; break; }
				}
			}

			if (match && match->kind == FormatContext::Block)
			{
				// A Block with unitKind == None is a nested anonymous
				// DECLARE/BEGIN...END block — a "flow-control construct" per
				// §6.5, separated via FmtPlsqlBlankLineAroundFlowStmts. Named
				// units (subprogram/package/type/trigger bodies) keep the
				// existing top-level-only separator rule.
				if (match->unitKind == FormatContext::None)
					blankAroundFlowStmt();
				else
					blankAroundNamedUnit();
				// Distinguish IS/AS-opened Block (standalone PROCEDURE/FUNCTION
				// or top-level nested subprogram) from DECLARE/BEGIN-opened Block.
				const ClauseEntry* isAsKw = getKeyword(match->keywords, etIS);
				if (!isAsKw) isAsKw = getKeyword(match->keywords, etAS);

				if (isAsKw)
				{
					// headerStart is stored by the tree builder; fall back to
					// skipEol from pos for anonymous blocks (unitKind == None).
					int headerStart = (match->headerStart >= 0)
						? match->headerStart
						: skipEol(tokens, pos, isAsKw->srcIdx);

						switch (match->unitKind)
						{
						case FormatContext::Package:
						case FormatContext::PackageBody:
							emitPackage(*match, tokens, headerStart, isAsKw->srcIdx, indentLevel, pol, fw);
							break;
						case FormatContext::TypeBody:
							emitTypeBody(*match, tokens, headerStart, isAsKw->srcIdx, indentLevel, pol, fw);
							break;
						case FormatContext::TypeSpec:
							emitTypeSpec(*match, tokens, headerStart, isAsKw->srcIdx, indentLevel, pol, fw);
							break;
						case FormatContext::Trigger:
							emitTriggerHeader(tokens, headerStart, isAsKw->srcIdx, indentLevel, pol, fw);
							emitBlock(*match, tokens, pol, indentLevel, fw);
							break;
						default:
							// Standalone PROCEDURE/FUNCTION with a body.
							emitSubprogramHeader(tokens, headerStart, isAsKw->srcIdx, indentLevel, pol, fw);
							emitBlock(*match, tokens, pol, indentLevel, fw);
							break;
						}
					}
					else
					{
						// No IS/AS: trigger body opened with BEGIN, compound
						// trigger opened by the "COMPOUND TRIGGER" keyword
						// pair, TypeSpec opened directly at "UNDER supertype ("
						// (subtype with no AS OBJECT/RECORD clause), or
						// anonymous block.
						int headerStart = (match->headerStart >= 0)
							? match->headerStart
							: skipEol(tokens, pos, match->openSrcIdx);
						if (match->unitKind == FormatContext::Trigger)
						{
							emitTriggerBodyHeader(tokens, headerStart, match->openSrcIdx, indentLevel, pol, fw);
							emitBlock(*match, tokens, pol, indentLevel, fw);
						}
						else if (match->unitKind == FormatContext::CompoundTrigger)
						{
							// openSrcIdx is the header's *last* token here (the
							// TRIGGER of "COMPOUND TRIGGER"), not an opener, so
							// the body is emitted from the node itself — see
							// emitCompoundTrigger.
							emitCompoundTrigger(*match, tokens, headerStart, indentLevel, pol, fw);
						}
						else if (match->unitKind == FormatContext::TypeSpec)
						{
							emitTypeSpec(*match, tokens, headerStart, -1, indentLevel, pol, fw);
						}
						else
						{
							emitBlock(*match, tokens, pol, indentLevel, fw);
						}
					}
				pos = skipEnder(tokens, match->closeSrcIdx, rangeEnd);
				firstItem = false; prevWasComment = false; prevWasDml = false;
				prevWasFlowStmt = (match->unitKind == FormatContext::None);
				prevWasNamedUnit = (match->unitKind != FormatContext::None);
				continue;
			}

			if (match && match->kind == FormatContext::IfStmt)
			{
				blankAroundFlowStmt();
				emitIfStmt(*match, tokens, pol, indentLevel, fw);
				pos = skipEnder(tokens, match->closeSrcIdx, rangeEnd);
				firstItem = false; prevWasComment = false; prevWasFlowStmt = true; prevWasDml = false; prevWasNamedUnit = false;
				continue;
			}

			if (match && match->kind == FormatContext::CondCompile)
			{
				blankAroundFlowStmt();
				emitCondCompile(*match, tokens, pol, indentLevel, fw);
				pos = skipEnder(tokens, match->closeSrcIdx, rangeEnd);
				firstItem = false; prevWasComment = false; prevWasFlowStmt = true; prevWasDml = false; prevWasNamedUnit = false;
				continue;
			}

			if (match && match->kind == FormatContext::LoopStmt)
			{
				blankAroundFlowStmt();
				emitLoopStmt(*match, tokens, pol, indentLevel, fw);
				pos = skipEnder(tokens, match->closeSrcIdx, rangeEnd);
				firstItem = false; prevWasComment = false; prevWasFlowStmt = true; prevWasDml = false; prevWasNamedUnit = false;
				continue;
			}

			if (match && match->kind == FormatContext::CaseStmt)
			{
				blankAroundFlowStmt();
				emitCaseStmt(*match, tokens, pol, indentLevel, fw);
				pos = skipEnder(tokens, match->closeSrcIdx, rangeEnd);
				firstItem = false; prevWasComment = false; prevWasFlowStmt = true; prevWasDml = false; prevWasNamedUnit = false;
				continue;
			}

			if (match && isDmlKind(match->kind))
			{
				blankBefore(false, true);
				int stmtEnd = match->closeSrcIdx >= pos ? match->closeSrcIdx : pos;

				// Coalesce a chain of "SELECT ... UNION|INTERSECT|MINUS [ALL]
				// SELECT ..." fragments into ONE formatSQL() call. The tree
				// builder closes each Select scope right at the set operator
				// (without consuming it), so consecutive branches of a
				// compound query are separate sibling Select nodes. Formatting
				// each branch with its own formatSQL() call independently
				// causes extra/duplicated blank lines (each call pads its own
				// output), so instead we extend stmtEnd across the whole
				// chain and format it as a single statement.
				if (match->kind == FormatContext::Select)
				{
					const FormatContext* chainCur = match;
					for (;;)
					{
						// The tree builder's closeScope() for a Select is called
						// AT the set-operator token itself (UNION/INTERSECT/
						// MINUS), so closeSrcIdx IS that operator's index — not
						// the token before it.
						int opIdx = chainCur->closeSrcIdx;
						if (opIdx < 0 || opIdx >= rangeEnd) break;
						EToken opTok = tokens.at(opIdx).token;
						if (opTok != etUNION && opTok != etINTERSECT && opTok != etMINUS_SQL)
							break;
						int afterOp = skipEol(tokens, opIdx + 1, rangeEnd);
						if (afterOp < rangeEnd && tokens.at(afterOp).token == etALL)
							afterOp = skipEol(tokens, afterOp + 1, rangeEnd);
						const FormatContext* nextSel = nullptr;
						for (const auto& ch : node.children)
						{
							if (ch.kind == FormatContext::Select && ch.openSrcIdx == afterOp)
							{ nextSel = &ch; break; }
						}
						if (!nextSel) break;
						chainCur = nextSel;
						stmtEnd = chainCur->closeSrcIdx >= pos ? chainCur->closeSrcIdx : stmtEnd;
					}
				}

				size_t subEnd = std::min((size_t)stmtEnd + 1, tokens.size());
				std::vector<OpenEditor::Token> sub(tokens.begin() + pos, tokens.begin() + subEnd);
				// E2b — token-run delegation: SpliceInto() reproduces
				// WriteIndentText()'s per-line indenting while preserving the
				// nested formatter's token types. concatTokenValues() is the
				// guard's text oracle, inlined because SQL_SLICE_GUARD and TRACE
				// compile away in Release.
				std::vector<OpenEditor::Token> formatted = formatSQL(sub, pol);
				SQL_SLICE_GUARD(sub, concatTokenValues(formatted));
				trimTrailingWs(formatted);
#ifdef _DEBUG
				TRACE(L"emitStatementSequence(dml) %s", concatTokenValues(formatted).c_str());
#endif
				fw.SpliceInto(formatted, fw.CurrentIndentChars());
				fw.NewLine();
				pos = skipEol(tokens, stmtEnd + 1, rangeEnd);
				firstItem = false; prevWasComment = false; prevWasFlowStmt = false; prevWasDml = true; prevWasNamedUnit = false;
				continue;
			}

			// CaseExpr at pos with no prefix (bare "case ... end;" selection).
			if (match && match->kind == FormatContext::CaseExpr)
			{
				blankBefore(false);
				emitCaseExpr(*match, tokens, pos, pos - 1 /*empty prefix*/, pol, fw);
				int next = skipEol(tokens, match->closeSrcIdx + 1, rangeEnd);
				if (next < rangeEnd && tokens.at(next).token == etSEMICOLON) ++next;
				pos = skipEol(tokens, next, rangeEnd);
				firstItem = false; prevWasComment = false; prevWasFlowStmt = false; prevWasDml = false; prevWasNamedUnit = false;
				continue;
			}

			// NOTE (Step 2.1): the standalone PROCEDURE/FUNCTION forward-decl
			// probe that used to live here — a tryParseNestedSubprogramHeader()
			// re-scan gated on tokens[pos] being PROCEDURE/FUNCTION/CREATE —
			// has been retired. Forward declarations are now SubprogramForwardDecl
			// tree nodes dispatched near the top of this loop (right after the
			// UnknownStmt arm), which also covers method modifiers and a
			// CREATE [OR REPLACE] prefix that the old token-kind gate missed.

			// raw fragment:
			// stopping early if we reach the start of a known *statement-level*
			// child scope (Block/IfStmt/LoopStmt/DML) so that unrelated
			// constructs are never merged into one bogus fragment (e.g. when
			// unexpected syntax, such as a stray comment, prevents the normal
			// ';' from being found first). Paren and CaseExpr children are
			// transparent to this scan — they are sub-expressions within the
			// current statement, not separate statements.
			// ---- Standalone comment: "-- ..." or "/* ... */" ----
			// Emit it on its own line at the current indent and advance.
			// Must be detected before the fragment scanner so the comment is
			// never merged into a following statement's fragment range.
			//
			// This arm has no tree node to read FormatContext::trailing from,
			// so it asks the same shared predicate buildTree() used. Keeping
			// one definition is what stops this site and the Comment-node site
			// above from disagreeing about the same comment.
			if (tokens.at(pos).token == etCOMMENT)
			{
				const OpenEditor::Token& ctk = tokens.at(pos);
				// Same closed-statement capture, and same ordering of
				// blankBefore() against the flag update, as the Comment-node
				// arm above — the two arms must not disagree about the blank
				// line that follows a rejoined end-of-line comment.
				const bool closedBefore = prevIsClosedStmt();
				if (!tryEmitTrailingEndComment(fw, ctk, isTrailingComment(tokens, pos), pol))
				{
					blankBefore(true);
					fw.WriteCurrentIndent();
					fw.WriteToken(ctk);
					fw.NewLine();
					trailingOnClosedStmt = false;
				}
				else
					trailingOnClosedStmt = closedBefore;
				pos = skipEol(tokens, pos + 1, rangeEnd);
				firstItem = false; prevWasComment = true; prevWasFlowStmt = false; prevWasNamedUnit = false;
				continue;
			}

			// ---- Compilation-unit terminator: "/" on its own line ----
			// Already handled at the top of the loop
			// this defensive fallback covers the case where an actual etSLASH
			// token somehow reaches here unclassified.
			if (tokens.at(pos).token == etSLASH)
			{
				fw.WriteCurrentIndent();
				fw.WriteToken(tokens.at(pos));
				fw.NewLine();
				pos = skipEol(tokens, pos + 1, rangeEnd);
				continue;
			}

			int fragStart = pos;
			int fragEnd   = fragStart;
			int parenDepth = 0;
			bool found = false;
			bool fragWasDml = false; // set true below if this fragment turns out to be DML (e.g. FORALL's inline target)
			for (int i = fragStart; i < rangeEnd; ++i)
			{
				if (i > fragStart)
				{
					bool isChildStart = false;
					for (const auto& child : node.children)
					{
						if (child.openSrcIdx == i
							&&  child.kind != FormatContext::Paren
							&&  child.kind != FormatContext::ParamList
							&&  child.kind != FormatContext::CaseExpr
							&&  child.kind != FormatContext::BetweenExpr)
						{
							isChildStart = true;
							break;
						}
					}
					if (isChildStart)
					{
						fragEnd = i - 1;
						found = true;
						break;
					}
				}
				if (tokens.at(i).token == etLEFT_ROUND_BRACKET) ++parenDepth;
				else if (tokens.at(i).token == etRIGHT_ROUND_BRACKET) { if (parenDepth > 0) --parenDepth; }
				else if (tokens.at(i).token == etSEMICOLON || tokens.at(i).token == etEOS)
				{
					if (parenDepth == 0 || !found)
					{
						fragEnd = i;
						found = true;
						if (parenDepth == 0) break;
					}
				}
			}
			if (!found) fragEnd = rangeEnd - 1;

			// Never let the raw fragment swallow the script '/' terminator:
			// the terminator arm at the top of the loop emits '/' alone at
			// line start, while a fragment would emit it mid-line where the
			// losslessness re-lex reads it as division. Shrinking the range
			// leaves pos on the '/' (the loop tail advances to fragEnd+1),
			// handing it to that arm on the next iteration.
			if (fragEnd >= fragStart && tokens.at(fragEnd).token == etEOS)
				--fragEnd;

			// If a CaseExpr child falls within this fragment, it is an expression
			// CASE used in an assignment (e.g. "v_name := case ... end;").
			// Split at the CaseExpr: emit the prefix tokens, then recurse.
			const FormatContext* innerCase = nullptr;
			for (const auto& child : node.children)
			{
				if (child.kind == FormatContext::CaseExpr
				&&  child.openSrcIdx > fragStart
				&&  child.openSrcIdx <= fragEnd)
				{ innerCase = &child; break; }
			}

			if (innerCase)
			{
				// Only when the CASE expression *is* the whole remaining
				// expression. emitCaseExpr() emits prefix + CASE…END + the next
				// ';' it can find, so handing it a fragment with significant
				// tokens after END makes everything in between disappear:
				//
				//   l_return := l_return
				//      || case when … then CHR(10) end
				//      || LPAD(…) || utl_call_stack.owner(indx) || …;
				//
				// lost the whole concat tail. Declining the split lets the
				// generic fragment path below own it, which is lossless and
				// already wraps '||' chains (emitConcatAwareStatement).
				bool ownsFragment = true;
				bool sawSemi      = false;
				for (int j = innerCase->closeSrcIdx + 1; j <= fragEnd; ++j)
				{
					EToken t = tokens.at(j).token;
					if (t == etEOL) continue;
					if (t == etSEMICOLON && !sawSemi) { sawSemi = true; continue; }
					ownsFragment = false; // trailing '||' operand, comment, …
					break;
				}
				if (!ownsFragment) innerCase = nullptr;
			}

			if (innerCase)
			{
				blankBefore(false);
				int prefixEnd = innerCase->openSrcIdx - 1;
				while (prefixEnd > fragStart && tokens.at(prefixEnd).token == etEOL) --prefixEnd;
				int firstNonEol = skipEol(tokens, fragStart, prefixEnd + 1);
				emitCaseExpr(*innerCase, tokens, firstNonEol, prefixEnd, pol, fw);
				pos = skipEol(tokens, fragEnd + 1, rangeEnd);
				firstItem = false; prevWasComment = false; prevWasFlowStmt = false; prevWasDml = false; prevWasNamedUnit = false;
				continue;
			}

			// Skip a completely blank fragment range
			int firstNonEol = skipEol(tokens, fragStart, fragEnd + 1);
			if (firstNonEol <= fragEnd)
			{
				// Don't insert a blank line before a bare '/' or EOS terminator.
				EToken firstTok = tokens.at(firstNonEol).token;
				bool isTerminator = (firstTok == etSLASH || firstTok == etEOS);

				// ---- CREATE TYPE … (member-list); with no IS/AS/UNDER Block ----
				// detected up front (before the generic separator below) so that
				// it is treated as a named unit — always separated from its
				// neighbours, just like the Block-based TypeSpec/TypeBody path,
				// even though it terminates with ';' rather than a script '/'.
				bool fragIsTypeSpec = false;
				const FormatContext* fragTypeSpecParamList = nullptr;
				if (!isTerminator && tokens.at(firstNonEol).token == etCREATE)
				{
					// Only the CREATE ... TYPE header itself qualifies — a TYPE
					// token appearing later in the fragment must not be mistaken
					// for "CREATE TYPE". So we only scan up to the first child
					// (the member-list ParamList/Paren), never past it. Inside
					// that range isTypeKeyword() additionally filters out a
					// "%TYPE" datatype attribute reference, whose attribute name
					// lexes as etTYPE just like the keyword does.
					const FormatContext* firstChild = nullptr;
					for (const auto& child : node.children)
					{
						if ((child.kind == FormatContext::ParamList || child.kind == FormatContext::Paren)
						&&   child.openSrcIdx > firstNonEol
						&&   child.openSrcIdx <= fragEnd)
						{
							if (!firstChild || child.openSrcIdx < firstChild->openSrcIdx)
								firstChild = &child;
						}
					}
					if (firstChild)
					{
						bool hasTypeToken = false;
						for (int j = firstNonEol; j < firstChild->openSrcIdx; ++j)
							if (isTypeKeyword(tokens, j)) { hasTypeToken = true; break; }
						if (hasTypeToken)
						{
							fragTypeSpecParamList = firstChild;
							fragIsTypeSpec = true;
						}
					}
				}

				if (!isTerminator)
				{
					if (fragIsTypeSpec)
						blankAroundNamedUnit();
					else
						blankBefore(false);
				}

				// ---- WITH <cte> SELECT … ----
				// The fragment scanner stops at the DML SELECT child that follows
				// the CTE list, splitting WITH from its own SELECT.  Detect this:
				// if the fragment starts with WITH and the very next DML child
				// immediately follows fragEnd+1, extend to that child's closeSrcIdx
				// and route the whole thing through formatSQL.
				if (firstTok == etWITH)
				{
					// Find the DML child that immediately follows this fragment.
					const FormatContext* cteSelect = nullptr;
					for (const auto& child : node.children)
					{
						if (!isDmlKind(child.kind)) continue;
						int childStart = skipEol(tokens, fragEnd + 1, rangeEnd);
						if (child.openSrcIdx == childStart)
						{ cteSelect = &child; break; }
					}
					if (cteSelect)
					{
						int fullEnd = cteSelect->closeSrcIdx >= 0
									? cteSelect->closeSrcIdx : fragEnd;
						size_t subEnd = std::min((size_t)fullEnd + 1, tokens.size());
						std::vector<OpenEditor::Token> sub(
							tokens.begin() + firstNonEol, tokens.begin() + subEnd);
						// E2b — token-run delegation: SpliceInto() reproduces
						// WriteIndentText()'s per-line indenting while preserving
						// the nested formatter's token types. concatTokenValues()
						// is the guard's text oracle, inlined because
						// SQL_SLICE_GUARD and TRACE compile away in Release.
						std::vector<OpenEditor::Token> formatted = formatSQL(sub, pol);
						SQL_SLICE_GUARD(sub, concatTokenValues(formatted));
						trimTrailingWs(formatted);
#ifdef _DEBUG
						TRACE(L"emitStatementSequence(cteSelect) %s", concatTokenValues(formatted).c_str());
#endif
						fw.SpliceInto(formatted, fw.CurrentIndentChars());
						fw.NewLine();
						pos = skipEol(tokens, fullEnd + 1, rangeEnd);
						firstItem = false; prevWasComment = false; prevWasFlowStmt = false; prevWasDml = true;
						prevWasNamedUnit = false;
						continue;
					}
				}

				// Math/bool wrapping has highest priority: if there are top-level
				// operators (+/-/*/AND/OR) the expression owns its own wrapping and
				// any ParamList children inside it are handled recursively as opaque
				// groups (they either fit flat or wrap their own interior).
				bool emitted = emitMathBoolAwareStatement(tokens, firstNonEol, fragEnd, indentLevel, pol, fw, &node.children);
				if (emitted)
					fw.NewLine();

				if (!emitted)
				{
					// CREATE TYPE … (member-list) — no IS/AS block in tree.
					// The member list is a bare ParamList child of the root node.
					// Route it through emitTypeSpec by building a synthetic block
					// that has the correct closeSrcIdx (the ';' / end of fragment).
					bool handledAsTypeSpec = false;
					if (fragIsTypeSpec && fragTypeSpecParamList)
					{
						const FormatContext* pl = fragTypeSpecParamList;
						// Build a minimal synthetic FormatContext so emitTypeSpec
						// can scan up to the ';' for suffix constraints.
						FormatContext synth;
						synth.kind         = FormatContext::Block;
						synth.openSrcIdx   = pl->openSrcIdx;
						synth.closeSrcIdx  = fragEnd;
						synth.children.push_back(*pl);
						// isIdx = one token before '(' so the ParamList is found.
						int isIdx = pl->openSrcIdx - 1;
						emitTypeSpec(synth, tokens, firstNonEol, isIdx, indentLevel, pol, fw);
						pos = skipEol(tokens, fragEnd + 1, rangeEnd);
						firstItem = false; prevWasComment = false; prevWasFlowStmt = false; prevWasDml = false;
						prevWasNamedUnit = true;
						handledAsTypeSpec = true;
					}
					if (handledAsTypeSpec) continue;

					// No top-level math/bool operators (or statement fits flat).
					// Now check whether the statement is a bare call whose param
					// list needs §9.3 wrapping.
					const FormatContext* innerParam = nullptr;
					for (const auto& child : node.children)
					{
						if (child.kind == FormatContext::ParamList
						&&  child.openSrcIdx > fragStart
						&&  child.openSrcIdx <= fragEnd)
						{
							if (!innerParam || child.openSrcIdx < innerParam->openSrcIdx)
								innerParam = &child;
						}
					}

					// The precedence rule above ("the expression owns its own
					// wrapping; any ParamList children are opaque") is enforced for
					// +/-/*/AND/OR by emitMathBoolAwareStatement, but '||' is not
					// one of that emitter's operators — it appears only inside
					// emitConcatAwareStatement. A concat-only statement therefore
					// falls through to here, where the param-list branch claims it:
					// emitParamListStatement() emits everything before '(' with
					// emitFlatTokens() and only ever offers the per-parameter
					// fragments to emitConcatAwareStatement(), so a concat chain
					// that precedes a call is never wrapped:
					//
					//     v_a := 'one ' || … || TO_CHAR(1, 'FM00');
					//
					// keeps a >100-column prefix and splits solely on the call's
					// comma. Extend the precedence rule to cover '||' by declining
					// to treat such a statement as a bare call, letting the
					// concat-aware fallback below own it. See F4 in
					// md/sql_plsql_formatter_integration_plan.md.
					if (innerParam)
					{
						int depth = 0;
						for (int j = firstNonEol; j < innerParam->openSrcIdx; ++j)
						{
							EToken t = tokens.at(j).token;
							if      (t == etLEFT_ROUND_BRACKET)  ++depth;
							else if (t == etRIGHT_ROUND_BRACKET) { if (depth > 0) --depth; }
							else if (t == etCONCAT_OP && depth == 0)
							{
								innerParam = nullptr; // not a bare call — defer
								break;
							}
						}
					}

					if (innerParam)
					{
						int maxLen = pol.plsqlParamListMaxLen;
						int flatW  = measureFlat(tokens, firstNonEol, fragEnd);
						if (maxLen > 0 && flatW > maxLen)
						{
							int parenOpenIdx  = innerParam->openSrcIdx;
							int parenCloseIdx = innerParam->closeSrcIdx;
							int suffixStart = (parenCloseIdx >= 0 && parenCloseIdx < fragEnd)
											  ? skipEol(tokens, parenCloseIdx + 1, fragEnd + 1)
											  : fragEnd + 1;
							int suffixEnd = fragEnd;
							if (suffixStart > suffixEnd) suffixStart = suffixEnd + 1;

							emitParamListStatement(tokens, firstNonEol, parenOpenIdx,
								(parenCloseIdx >= 0 ? parenCloseIdx : fragEnd),
								suffixStart, suffixEnd, indentLevel, pol, fw);
							fw.NewLine();
							pos = skipEol(tokens, fragEnd + 1, rangeEnd);
							continue;
						}
					}

					// ---- EXECUTE IMMEDIATE ----
					// execute immediate <expr>
					//   [into <vars>]
					//   [bulk collect into <vars>]
					//   [using [in|out|in out] <args>]
					//   [returning into <vars>];
					if (!emitted && firstNonEol <= fragEnd
					// Both spellings: the script-mode tokenizer has always produced
					// etEXECUTE, and s_plsqlTokens does too since the EXEC folding fix
					// (OEPlsSqlParser.cpp:300). Matching the word alone is not enough -
					// the kind has to be allowed as well, or this emitter is skipped and
					// the statement falls through to the generic one-line path, losing
					// the INTO/USING clause breaks md/plsql_formatter_rules.md:520-529
					// requires. Same dual test as OEPlsSqlFormatterTree.cpp:372.
					&&  (tokens.at(firstNonEol).token == etUNKNOWN
					     || tokens.at(firstNonEol).token == etEXECUTE)
					&&  !_wcsicmp(tokens.at(firstNonEol).value.c_str(), L"EXECUTE"))
					{
						// Confirm the next non-EOL token is IMMEDIATE.
						int nxt = skipEol(tokens, firstNonEol + 1, fragEnd + 1);
						bool isImmediate = (nxt <= fragEnd
							&& tokens.at(nxt).token == etUNKNOWN
							&& !_wcsicmp(tokens.at(nxt).value.c_str(), L"IMMEDIATE"));
						if (isImmediate)
						{
							int baseChars = fw.CurrentIndentChars();
							int clauseChars = baseChars + fw.GetIndentSpacing();
							// Collect non-EOL token indices in the fragment.
							std::vector<int> idx;
							for (int j = firstNonEol; j <= fragEnd; ++j)
								if (tokens.at(j).token != etEOL) idx.push_back(j);

							// Split into segments at top-level clause keywords:
							// INTO, BULK (COLLECT INTO), USING, RETURNING.
							// Segment 0 = "execute immediate <expr>", rest get clauseIndent.
							auto isExecClause = [&](int ti) -> bool {
								const auto& t = tokens.at(ti);
								if (t.token == etINTO || t.token == etUSING || t.token == etRETURNING)
									return true;
								if (t.token == etUNKNOWN
								&&  !_wcsicmp(t.value.c_str(), L"BULK"))
									return true;
								return false;
							};

							int parenD = 0;
							int segStart = 0; // index into idx[]
							bool firstSeg = true;
							auto flushSeg = [&](int segEnd /*exclusive index into idx*/)
							{
								if (segEnd <= segStart) return;
								fw.WriteSpaces(firstSeg ? baseChars : clauseChars);
								firstSeg = false;
								bool sp = false;
								for (int k = segStart; k < segEnd; ++k)
								{
									int ti = idx.at(k);
									if (sp && needSpaceBetween(tokens.at(idx.at(k-1)), tokens.at(ti)))
										fw.WriteSpaces(1);
									fw.WriteToken(tokens.at(ti));
									sp = true;
								}
								fw.NewLine();
								segStart = segEnd;
							};

							for (int k = 0; k < (int)idx.size(); ++k)
							{
								int ti = idx.at(k);
								if (tokens.at(ti).token == etLEFT_ROUND_BRACKET)  { ++parenD; continue; }
								if (tokens.at(ti).token == etRIGHT_ROUND_BRACKET) { if (parenD > 0) --parenD; continue; }
								if (parenD == 0 && k > 0 && isExecClause(ti))
								{
									flushSeg(k);
								}
							}
							flushSeg((int)idx.size());
							emitted = true;
						}
					}

					// ---- FORALL ----
					// forall i in <range>
					//   <dml-statement>;
					if (!emitted && firstNonEol <= fragEnd
					&&  tokens.at(firstNonEol).token == etUNKNOWN
					&&  !_wcsicmp(tokens.at(firstNonEol).value.c_str(), L"FORALL"))
					{
						// Find the DML verb: INSERT/UPDATE/DELETE/MERGE/EXECUTE after
						// the index variable and IN range (ends at the DML keyword).
						// Strategy: scan forward for the first INSERT/UPDATE/DELETE/MERGE/
						// EXECUTE token at parenDepth 0.
						int dmlStart = -1;
						int parenD = 0;
						for (int j = firstNonEol + 1; j <= fragEnd; ++j)
						{
							if (tokens.at(j).token == etEOL) continue;
							if (tokens.at(j).token == etLEFT_ROUND_BRACKET)  { ++parenD; continue; }
							if (tokens.at(j).token == etRIGHT_ROUND_BRACKET) { if (parenD > 0) --parenD; continue; }
							if (parenD > 0) continue;
							EToken t = tokens.at(j).token;
							if (t == etINSERT || t == etUPDATE || t == etDELETE
							||  t == etMERGE
							// Both spellings - see the EXECUTE IMMEDIATE gate above.
							// Without etEXECUTE a FORALL whose body is a dynamic
							// statement finds no DML verb and loses its clause breaks.
							|| ((t == etUNKNOWN || t == etEXECUTE)
								&& !_wcsicmp(tokens.at(j).value.c_str(), L"EXECUTE")))
							{ dmlStart = j; break; }
						}

						if (dmlStart > firstNonEol)
						{
							// Emit "forall i in <range>" (header up to DML) on one line.
							fw.WriteCurrentIndent();
							int headerEnd = dmlStart - 1;
							while (headerEnd > firstNonEol && tokens.at(headerEnd).token == etEOL)
								--headerEnd;
							emitFlatTokens(tokens, firstNonEol, headerEnd, fw);
							fw.NewLine();

							// Route through existing DML/statement formatter.
							// Find the child DML scope if any, or use flat emit.
							const FormatContext* dmlChild = nullptr;
							for (const auto& ch : node.children)
							{
								if (ch.openSrcIdx >= dmlStart && ch.openSrcIdx <= fragEnd
								&& (isDmlKind(ch.kind) || ch.kind == FormatContext::Block))
								{ dmlChild = &ch; break; }
							}
							if (dmlChild && isDmlKind(dmlChild->kind))
							{
								int dEnd = dmlChild->closeSrcIdx >= 0 ? dmlChild->closeSrcIdx : fragEnd;
								size_t subEnd = std::min((size_t)dEnd + 1, tokens.size());
								std::vector<OpenEditor::Token> sub(
									tokens.begin() + dmlStart, tokens.begin() + subEnd);
								// E2b — token-run delegation: SpliceInto() reproduces
								// WriteIndentText()'s per-line indenting while
								// preserving the nested formatter's token types.
								// concatTokenValues() is the guard's text oracle,
								// inlined because SQL_SLICE_GUARD and TRACE compile
								// away in Release.
								std::vector<OpenEditor::Token> formatted = formatSQL(sub, pol);
								SQL_SLICE_GUARD(sub, concatTokenValues(formatted));
								trimTrailingWs(formatted);
#ifdef _DEBUG
								TRACE(L"emitStatementSequence(dml child) %s", concatTokenValues(formatted).c_str());
#endif
								fw.SpliceInto(formatted, fw.GetNextIndentChars());
								fw.NewLine();
							}
							else
							{
								// Flat DML or EXECUTE IMMEDIATE target.
								fw.PushIndent(); fw.WriteCurrentIndent(); fw.PopIndent();
								emitFlatTokens(tokens, dmlStart, fragEnd, fw);
								fw.NewLine();
							}
							emitted = true;
							fragWasDml = true;
						}
					}

						// Plain emit: || concat-aware (or flat normalized fallback).
						if (!emitted)
							emitConcatAwareStatement(tokens, firstNonEol, fragEnd, indentLevel, pol, fw);
					}

					// Don't add an extra '\n' when:
					// - the last token was an end-of-line comment, or
					// - the handler already wrote its own trailing newline (emitted).
					if (!emitted)
					{
						int lastNonEol = fragEnd;
						while (lastNonEol >= firstNonEol && tokens.at(lastNonEol).token == etEOL) --lastNonEol;
						if (lastNonEol < firstNonEol || !isEndLineComment(tokens.at(lastNonEol)))
							fw.NewLine();
					}
			}

			firstItem = false; prevWasComment = false; prevWasFlowStmt = false; prevWasDml = fragWasDml;
			prevWasNamedUnit = false;
			pos = skipEol(tokens, fragEnd + 1, rangeEnd);
		}
	}

} // namespace OpenEditor
