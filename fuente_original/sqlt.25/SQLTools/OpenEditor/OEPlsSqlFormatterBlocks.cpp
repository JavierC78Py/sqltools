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
#include "OpenEditor/OEPlsSqlFormatterBlocks.h"
#include "OpenEditor/OEPlsSqlFormatterCommon.h"
#include "OpenEditor/OEPlsSqlFormatterStmts.h"
#include "OpenEditor/OEPlsSqlFormatterUnits.h"
#include "OpenEditor/OESettings.h"

namespace OpenEditor {

#ifdef _DEBUG
// ----------------------------------------------------------------------------
// validateTreeCoverage — debug-only structural invariant check (#5/#8 support).
//
// Verifies the FormatContext tree produced by buildTree() is a well-formed
// partition of the token stream so downstream emitters and the per-emitter
// losslessness guards can trust node spans:
//   * every child span [openSrcIdx..closeSrcIdx] is fully inside its parent's,
//   * sibling spans are ordered and non-overlapping,
//   * open/close indices are set (>= 0) for every non-root node.
// A violation trips an assert at tree-build time (the earliest possible point),
// long before the whole-buffer re-tokenize check in verifyFormatterLosslessness.
// ----------------------------------------------------------------------------
static void validateTreeCoverageNode(
	const FormatContext& node,
	const std::vector<OpenEditor::Token>& tokens)
{
	const int N = (int)tokens.size();
	int prevClose = -1;
	for (const auto& child : node.children)
	{
		// Comment children legitimately have open == close (a single token).
		_ASSERTE(child.openSrcIdx >= 0 && "tree node has no opener index");
		_ASSERTE(child.closeSrcIdx >= 0 && "tree node has no closer index");
		_ASSERTE(child.openSrcIdx <= child.closeSrcIdx && "tree node span inverted");
		_ASSERTE(child.closeSrcIdx < N && "tree node span past end of tokens");

		// Child must sit within the parent's span (root spans the whole stream).
		if (node.openSrcIdx >= 0)
			_ASSERTE(child.openSrcIdx >= node.openSrcIdx && "child starts before parent");
		if (node.closeSrcIdx >= 0)
			_ASSERTE(child.closeSrcIdx <= node.closeSrcIdx && "child ends after parent");

		// Siblings must be ordered and non-overlapping.
		_ASSERTE(child.openSrcIdx > prevClose && "sibling spans overlap or are out of order");
		prevClose = child.closeSrcIdx;

		validateTreeCoverageNode(child, tokens);
	}
}

void validateTreeCoverage(
	const FormatContext& root,
	const std::vector<OpenEditor::Token>& tokens)
{
	validateTreeCoverageNode(root, tokens);
}
#endif

// ----------------------------------------------------------------------------
// getDirectOwnedRanges — per-node consumed-span bookkeeping (#5 convenience).
//
// A structural node (Block/IfStmt/LoopStmt/CaseStmt/...) covers the whole span
// [openSrcIdx..closeSrcIdx]. Part of that span is delegated to child scopes;
// the remainder is the node's own skeleton — the keyword/punctuation tokens the
// node's emitter is directly responsible for (DECLARE/BEGIN/EXCEPTION/END, clause
// keywords, ELSIF/ELSE, END IF/LOOP, etc.).
//
// This helper returns those direct-owned sub-ranges (inclusive, in order) by
// subtracting the child spans from the node span. It gives emitters a single
// source of truth for "which tokens do I emit myself vs delegate", and lets a
// per-node losslessness guard assert full coverage:
//     stripped(node emitter output)
//         == stripped(direct-owned tokens) merged with stripped(child outputs)
// Because children are ordered and non-overlapping (validateTreeCoverage), the
// subtraction is a simple linear walk.
std::vector<std::pair<int, int>> getDirectOwnedRanges(const FormatContext& node)
{
	std::vector<std::pair<int, int>> ranges;
	if (node.openSrcIdx < 0 || node.closeSrcIdx < 0)
		return ranges;

	int cursor = node.openSrcIdx;
	for (const auto& child : node.children)
	{
		if (child.openSrcIdx < 0 || child.closeSrcIdx < 0)
			continue;
		if (child.openSrcIdx > cursor)
			ranges.push_back({ cursor, child.openSrcIdx - 1 });
		if (child.closeSrcIdx + 1 > cursor)
			cursor = child.closeSrcIdx + 1;
	}
	if (cursor <= node.closeSrcIdx)
		ranges.push_back({ cursor, node.closeSrcIdx });
	return ranges;
}

// buildTreePlsql — authoritative structural pre-pass for PL/SQL formatting.
//
// The shared buildTree() (OEPlsSqlFormatterTree.cpp) already performs full PL/SQL
// structural detection: Block (DECLARE/BEGIN/EXCEPTION/END), IfStmt, LoopStmt,
// CaseStmt vs CaseExpr, CondCompile ($IF/$END), ParamList vs Paren, subprogram
// IS/AS block opening, CREATE header capture, TypeSpec/UNDER handling and the
// enderStack-based END matching. The tree it returns therefore already fully
// describes PL/SQL structure; this entry point simply names that intent and,
// in debug builds, validates that the resulting tree is a well-formed partition
// of the token stream (see validateTreeCoverage) so downstream emitters — and
// the per-emitter losslessness guards (#8) — can rely on the node spans.
FormatContext buildTreePlsql(const std::vector<OpenEditor::Token>& tokens)
{
	FormatContext root = buildTree(tokens);
#ifdef _DEBUG
	validateTreeCoverage(root, tokens);
#endif
	return root;
}

namespace {

	// Prints the complete syntax tree, one node per line:
	//   Kind openSrcIdx..closeSrcIdx open='…' close='…' w=N unit=… term=…
	// Two sinks, selected by `capture`:
	//   capture == false — the historical Debug behaviour: every line goes to
	//       the MSVC Output window through TRACE. Compiled in all builds but
	//       only ever called under #ifdef _DEBUG.
	//   capture == true — the automated-testing sink (FormatterTreeTraceScope,
	//       OEPlsSqlFormatter.h): lines are appended to g_pFormatterTreeTrace
	//       and the "w=" field is omitted, because formattedLen is only
	//       measured at this point in Debug builds — omitting it keeps test
	//       baselines byte-identical across build configurations.
	void traceSyntaxTree(
		const FormatContext& node,
		const std::vector<OpenEditor::Token>& tokens,
		int depth = 0,
		bool capture = false)
	{
		// Kind name table — must match FormatContext::Kind enum order exactly.
		static const wchar_t* const kindNames[] = {
			L"Root",
			L"Paren",
			L"Select",
			L"CaseExpr",
			L"BetweenExpr",
			L"Delete",
			L"Update",
			L"InsertSimple",
			L"InsertConditional",
			L"Merge",
			L"Block",
			L"IfStmt",
			L"LoopStmt",
			L"CaseStmt",
			L"ParamList",
			L"ConcatExpr",
			L"MathExpr",
			L"CondCompile",
			L"Verbatim",
			L"ExceptionHandler",
			L"Comment",
			L"UnknownStmt",
			L"SubprogramForwardDecl",
			L"CteDml",
		};
		static const wchar_t* const unitKindNames[] = {
			L"", L"Procedure", L"Function", L"Package", L"PackageBody",
			L"TypeSpec", L"TypeBody", L"Trigger",
			L"CompoundTrigger", L"TimingPoint",
		};
		static const int kindCount = (int)(sizeof(kindNames)/sizeof(kindNames[0]));

		std::wstring pad(depth * 2, L' ');

		const wchar_t* kindStr =
			(node.kind >= 0 && node.kind < kindCount) ? kindNames[node.kind] : L"?";

		auto tokenVal = [&](int idx) -> std::wstring {
			if (idx < 0 || idx >= (int)tokens.size()) return L"-";
			std::wstring v = tokens.at(idx).value;
			if (v.size() > 30) { v.resize(30); v += L"\u2026"; }
			return v;
		};

		// Keyword list: "VALUE@idx ..."
		std::wstring kws;
		for (const auto& ce : node.keywords)
		{
			if (!kws.empty()) kws += L' ';
			kws += tokenVal(ce.srcIdx);
			kws += L'@';
			kws += std::to_wstring(ce.srcIdx);
		}

		// formattedLen: show as "w=N" or "w=MAX" when uncalculated. Left out
		// of the captured dump entirely — see the function header comment.
		std::wstring wstr;
		if (!capture)
			wstr = (node.formattedLen == INT_MAX)
				? L"w=?" : (L"w=" + std::to_wstring(node.formattedLen));

		std::wstring chstr = node.children.empty()
			? std::wstring() : (L" ch=" + std::to_wstring(node.children.size()));

		static const int unitKindCount = (int)(sizeof(unitKindNames)/sizeof(unitKindNames[0]));
		const wchar_t* unitStr = (node.unitKind > 0 && node.unitKind < unitKindCount)
			? unitKindNames[node.unitKind] : L"";
		std::wstring unitTag = (node.unitKind != FormatContext::None)
			? (std::wstring(L" unit=") + unitStr
			   + (node.headerStart >= 0 ? (L" hs=" + std::to_wstring(node.headerStart)) : L""))
			: std::wstring();

		// Step 2.2 — show the recorded terminator classification, so a
		// divergence between closeSrcIdx and terminatorSrcIdx (the same token
		// for a ';'-closed node, but NOT for an END-closed one, where
		// closeSrcIdx is the END keyword) is visible in the trace instead of
		// only showing up later as misformatted output.
		static const wchar_t* const termNames[] = { L"", L";", L"/" };
		static const int termCount = (int)(sizeof(termNames)/sizeof(termNames[0]));
		std::wstring termTag = (node.terminator != FormatContext::TermNone)
			? (std::wstring(L" term=")
			   + ((node.terminator >= 0 && node.terminator < termCount)
					  ? termNames[node.terminator] : L"?")
			   + L"@" + std::to_wstring(node.terminatorSrcIdx))
			: std::wstring();

		// One line per node; assembled once, then routed to the selected
		// sink. Field order matches the historical TRACE format exactly, so
		// Output-window readers see no change in Debug.
		std::wstring line;
		line += pad;
		line += kindStr;
		line += L" [" + std::to_wstring(node.openSrcIdx)
			 +  L".." + std::to_wstring(node.closeSrcIdx) + L"]";
		line += L" open='" + tokenVal(node.openSrcIdx) + L"'";
		const std::wstring closeVal = tokenVal(node.closeSrcIdx);
		line += L" close='" + (closeVal != L"\n" ? closeVal : std::wstring(L"\\n")) + L"'";
		if (!wstr.empty()) line += L" " + wstr;
		line += unitTag;
		line += termTag;
		if (node.hasComment) line += L" cmt";
		line += chstr;
		if (!kws.empty()) line += L" kw:" + kws;
		line += L"\n";

		if (capture)
			appendFormatterTreeTrace(line.c_str());
		else
		{
#ifdef _DEBUG
			TRACE(L"%s", line.c_str());
#endif
		}

		for (const auto& child : node.children)
			traceSyntaxTree(child, tokens, depth + 1, capture);
	}

#ifdef _DEBUG
	// ------------------------------------------------------------------
	// Phase A instrumentation — TRACE the duplicate-work summary for one
	// format operation. The counters are declared in OEPlsSqlFormatter.h and
	// reset by formatPlsql() on entry, so everything reported here covers
	// exactly this call.
	//
	// The two ratios are the decision gate for phases C/D. Note that
	// remeasuredTokens runs ABOVE reparsedTokens rather than below it:
	// calcWidths() invokes accumWidth() once per tree node, so a token in a
	// nested scope is walked both by the enclosing node's pass and by its own.
	// The spread between the two ratios is therefore the nested-scope overhead
	// of the width pass, and it widens as slices nest more deeply.
	// ------------------------------------------------------------------
	void tracePlsqlPerfSummary(size_t docTokens, const wchar_t* path)
	{
		const unsigned long long doc      = (unsigned long long)docTokens;
		const unsigned long long calls    = g_plsqlPerf.sqlFormatterCalls;
		const unsigned long long parsed   = g_plsqlPerf.reparsedTokens;
		const unsigned long long measured = g_plsqlPerf.remeasuredTokens;
		const unsigned long long total    = g_accumWidthTokensScanned;

		const double parseRatio   = doc ? (double)parsed   / (double)doc : 0.0;
		const double measureRatio = doc ? (double)measured / (double)doc : 0.0;

		TRACE(L"[PLSQL-PERF] %s docTokens=%I64u formatSQLcalls=%I64u "
			  L"reparsedTokens=%I64u (%.2fx) remeasuredTokens=%I64u (%.2fx) "
			  L"accumWidthTotal=%I64u\n",
			  path, doc, calls, parsed, parseRatio, measured, measureRatio, total);
	}
#endif // _DEBUG

} // anonymous namespace

// ============================================================================
// formatPlsql
// ============================================================================

// Strip up to `levels * indentSpacing` leading spaces from every line, so a seeded
// fragment returns to column 0 while keeping the shape its indent level produced.
static std::wstring dedentBy (const std::wstring& text, int levels, int indentSpacing)
{
	if (levels <= 0 || indentSpacing <= 0 || text.empty())
		return text;

	const size_t strip = (size_t)levels * (size_t)indentSpacing;
	std::wstring out;
	out.reserve(text.size());

	size_t pos = 0;
	while (pos < text.size())
	{
		const size_t nl   = text.find(L'\n', pos);
		const size_t end  = (nl == std::wstring::npos) ? text.size() : nl;
		const size_t last = (end - pos > strip) ? pos + strip : end;

		size_t cut = pos;
		while (cut < last && text[cut] == L' ')
			++cut;

		out.append(text, cut, end - cut);

		if (nl == std::wstring::npos)
			break;

		out += L'\n';
		pos = nl + 1;
	}

	return out;
}

std::wstring formatPlsql(std::vector<OpenEditor::Token>& tokens, const FormatterPolicy& pol,
						 const FormatterSeed& seed /*= FormatterSeed()*/)
{
#ifdef _DEBUG
	// Phase A instrumentation: formatPlsql() is the only top-level entry point,
	// so resetting here scopes the counters to exactly one format operation.
	resetPlsqlPerfCounters();
#endif

	// An Exception seed is not consumable yet: emitExceptionSection needs the
	// EXCEPTION token, which a fragment lacks. Degrade it to a document run.
	const FormatterSeed effective = (seed.scope == FormatFragmentScope::Exception)
								  ? FormatterSeed() : seed;

	// Say so in the trace. The decision line OEView_7 writes reports what the *gates*
	// produced, which for a handler fragment reads scope=Exception decline=none; left
	// alone the trace would claim a seeding this emit pass then does not perform, and a
	// handler list kept on one line instead of split like the whole file would look like
	// a gate bug rather than the known limit it is.
	if (seed.scope == FormatFragmentScope::Exception && formatterTreeTraceActive())
		appendFormatterTreeTrace(
			L"[SelectionSeed] degraded Exception->Document: fragment carries no EXCEPTION token\n");

	// ParseTree progress checkpoints are enabled for this buildTree() only:
	// formatSQL() re-enters buildTree() for every SQL slice the emit pass
	// delegates, and those must not report — see g_formatterParseProgress in
	// OEPlsSqlFormatter.h. Scoped so a cancellation thrown from inside the parse
	// still lowers the flag on the way out.
	FormatContext tree;
	{
		FormatterParseProgressScope parseProgress;
		tree = buildTree(tokens, effective);
	}

	// calcWidths() has two consumers: traceSyntaxTree() below, which prints
	// formattedLen, and the emit pass, which needs every node measured. It is
	// idempotent (formattedLen is overwritten with the same value and
	// hasComment only propagates monotonically), so one unconditional call up
	// front serves both. It used to be split across an #ifdef _DEBUG here and
	// an #ifndef _DEBUG further down, because Release could still bail out to
	// the pure-SQL fast path and skip the width pass entirely; with that exit
	// gone every document reaches the emit pass, so the split bought nothing.
	calcWidths(tree, tokens);

#ifdef _DEBUG
	TRACE(L"\n\n");
	TRACE(L"=== PL/SQL syntax tree ===\n");
	traceSyntaxTree(tree, tokens);
	TRACE(L"=== end tree ===\n\n");
#endif

	// ---- Automated-testing capture (works in ANY build configuration) ----
	// SqlFormatterHandler's ".FmtTraceTree=1" installs a
	// FormatterTreeTraceScope; the tree is then dumped into it *without* the
	// "w=" field. calcWidths() above now measures formattedLen in every build
	// configuration, so the omission is a deliberate choice rather than a
	// consequence of Debug-only measurement: leaving "w=" out is what keeps
	// <<<SYNTAX-TREE>>> test baselines byte-identical across configurations.
	// When a capture is active in Debug the tree is walked twice: once for the
	// Output window, once for the test buffer.
	if (formatterTreeTraceActive())
	{
		appendFormatterTreeTrace(L"=== PL/SQL syntax tree ===\n");
		traceSyntaxTree(tree, tokens, 0, true);
		appendFormatterTreeTrace(L"=== end tree ===\n");
	}

	std::wstring out;
	FormatWriter fw(out, pol.indentSpacing);

	// A seeded fragment is emitted at the level its section sits at in a whole-file
	// run, so every width decision matches; dedentBy() below removes that level.
	const int base = effective.seeded() ? effective.depth : 0;

	if (effective.scope == FormatFragmentScope::Declarations)
		emitDeclareSection(tree, tokens, 0, (int)tokens.size(), base, pol, fw);
	else if (effective.scope == FormatFragmentScope::TypeMembers)
		// The whole stream IS the member list: the '(' and ')' that bracket it in
		// the document sit outside the selection, because a caret on either is a
		// section boundary and declines to seed (GetSectionAt). emitTypeMemberList
		// reads tokens and never the FormatContext tree, so the tree buildTree()
		// made of this fragment is simply not consulted here.
		emitTypeMemberList(tokens, 0, (int)tokens.size(), base, pol, fw);
	else
		emitStatementSequence(tree, tokens, 0, (int)tokens.size(), pol, base, fw);

#ifdef _DEBUG
	tracePlsqlPerfSummary(tokens.size(), L"plsqlPath");
#endif
	return dedentBy(out, base, pol.indentSpacing);
}

} // namespace OpenEditor
