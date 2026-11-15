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
#include "OpenEditor/OEPlsSqlFormatter.h"
// buildTree() records FormatContext::trailing through isTrailingComment(), and
// that predicate lives with the other token-stream helpers in Common.h (which
// itself includes OEPlsSqlFormatter.h, so this replaces nothing). Sharing the
// one definition with the emitter sites is what stops the tree and the emitter
// from disagreeing about whether a comment was trailing.
#include "OpenEditor/OEPlsSqlFormatterCommon.h"
#include <algorithm>
#include <climits>
#ifdef _DEBUG
#include <crtdbg.h>
#endif

namespace OpenEditor {

// ============================================================================
// Formatter diagnostics sink (declared in OEPlsSqlFormatter.h)
// ============================================================================

std::wstring* g_pFormatterDiagnostics = nullptr;

FormatterDiagnosticsScope::FormatterDiagnosticsScope(std::wstring& report)
	: m_prev(g_pFormatterDiagnostics)
	, m_prevAssertMode(-1)
	, m_prevWarnMode(-1)
{
	report.clear();
	g_pFormatterDiagnostics = &report;

#ifdef _DEBUG
	// Route CRT assert/warning reports to the debugger instead of a modal
	// dialog. EmitRangeGuard reports through the sink above, but any *other*
	// assert fired while formatting (MFC ASSERT, _ASSERTE in a helper) would
	// otherwise block the UI thread and deadlock the COM caller.
	m_prevAssertMode = _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG);
	m_prevWarnMode   = _CrtSetReportMode(_CRT_WARN,   _CRTDBG_MODE_DEBUG);
#endif
}

FormatterDiagnosticsScope::~FormatterDiagnosticsScope()
{
#ifdef _DEBUG
	if (m_prevAssertMode >= 0) _CrtSetReportMode(_CRT_ASSERT, m_prevAssertMode);
	if (m_prevWarnMode   >= 0) _CrtSetReportMode(_CRT_WARN,   m_prevWarnMode);
#endif
	g_pFormatterDiagnostics = m_prev;
}

// ============================================================================
// Formatter tree-trace sink (declared in OEPlsSqlFormatter.h)
// ============================================================================

std::wstring* g_pFormatterTreeTrace = nullptr;

void appendFormatterTreeTrace(const wchar_t* text)
{
	if (g_pFormatterTreeTrace != nullptr)
		*g_pFormatterTreeTrace += text;
}

FormatterTreeTraceScope::FormatterTreeTraceScope(std::wstring& trace)
	: m_prev(g_pFormatterTreeTrace)
{
	trace.clear();
	g_pFormatterTreeTrace = &trace;
}

FormatterTreeTraceScope::~FormatterTreeTraceScope()
{
	g_pFormatterTreeTrace = m_prev;
}

// ============================================================================
// Verbatim-source callback (declared in OEPlsSqlFormatter.h)
// ============================================================================

const VerbatimSourceFn* g_pFormatterVerbatimSource = nullptr;

FormatterVerbatimSourceScope::FormatterVerbatimSourceScope(const VerbatimSourceFn& fn)
	: m_prev(g_pFormatterVerbatimSource), m_fn(fn)
{
	g_pFormatterVerbatimSource = &m_fn;
}

FormatterVerbatimSourceScope::~FormatterVerbatimSourceScope()
{
	g_pFormatterVerbatimSource = m_prev;
}

// ============================================================================
// Progress callback (declared in OEPlsSqlFormatter.h)
// ============================================================================

const ProgressFn* g_pFormatterProgress = nullptr;

// See the declaration in OEPlsSqlFormatter.h for the full rationale.
bool g_formatterCancelling = false;

bool g_formatterParseProgress = false;

FormatterParseProgressScope::FormatterParseProgressScope()
	: m_prev(g_formatterParseProgress)
{
	g_formatterParseProgress = true;
}

FormatterParseProgressScope::~FormatterParseProgressScope()
{
	g_formatterParseProgress = m_prev;
}

FormatterProgressScope::FormatterProgressScope(const ProgressFn& fn)
	: m_prev(g_pFormatterProgress), m_fn(fn)
{
	g_pFormatterProgress = &m_fn;
}

FormatterProgressScope::~FormatterProgressScope()
{
	g_pFormatterProgress = m_prev;
	// A cancellation thrown from a checkpoint has finished unwinding by now —
	// every EmitRangeGuard destructor on the way out has already run, this scope
	// being the outermost frame of the format. Clear the flag so the next
	// format's guards are live again.
	g_formatterCancelling = false;
}

// ============================================================================
// getKeyword
// ============================================================================

ClauseEntry* getKeyword(std::vector<ClauseEntry>& v, EToken key, int srcIdx)
{
	for (auto& e : v)
		if (e.token == key && (srcIdx < 0 || e.srcIdx == srcIdx)) return &e;
	return nullptr;
}

const ClauseEntry* getKeyword(const std::vector<ClauseEntry>& v, EToken key, int srcIdx)
{
	for (const auto& e : v)
		if (e.token == key && (srcIdx < 0 || e.srcIdx == srcIdx)) return &e;
	return nullptr;
}

// ============================================================================
// findInTree
// ============================================================================

FormatContext* findInTree(FormatContext& node, int targetIdx)
{
	if (node.openSrcIdx == targetIdx) return &node;
	for (auto& child : node.children)
	{
		auto* p = findInTree(child, targetIdx);
		if (p) return p;
	}
	return nullptr;
}

// ============================================================================
// isSubprogramModifierWord
// ============================================================================

bool isSubprogramModifierWord(const OpenEditor::Token& tk)
{
	if (tk.token != etUNKNOWN) return false;
	const wchar_t* v = tk.value.c_str();
	return !_wcsicmp(v, L"OVERRIDING")
		|| !_wcsicmp(v, L"MEMBER")
		|| !_wcsicmp(v, L"STATIC")
		|| !_wcsicmp(v, L"CONSTRUCTOR")
		|| !_wcsicmp(v, L"MAP")
		|| !_wcsicmp(v, L"ORDER")
		|| !_wcsicmp(v, L"NOT")
		|| !_wcsicmp(v, L"FINAL")
		|| !_wcsicmp(v, L"INSTANTIABLE");
}

// ============================================================================
// isTypeKeyword
// ============================================================================

// See the declaration comment in OEPlsSqlFormatter.h. Single source of truth
// for "this etTYPE token is the TYPE keyword, not the name of a %TYPE
// datatype attribute", so buildTree() and the emitters cannot disagree.
bool isTypeKeyword(const std::vector<OpenEditor::Token>& tokens, int idx)
{
	if (idx < 0 || idx >= (int)tokens.size())
		return false;
	if (tokens.at(idx).token != etTYPE)
		return false;

	// Walk back to the previous significant token. A '%' there makes this the
	// attribute name of "%TYPE"; anything else is the real keyword. Falling off
	// the front of the stream (the input starts with TYPE) is the keyword too.
	for (int j = idx - 1; j >= 0; --j)
	{
		const EToken t = tokens.at(j).token;
		if (t == etEOL || t == etCOMMENT)
			continue;
		return t != etPERCENT_SIGN;
	}
	return true;
}

// ============================================================================
// classifyTerminator / terminatorIsScriptSlash  (Step 2.2)
// ============================================================================

// See the declaration comment in OEPlsSqlFormatter.h. This is the single
// definition of "a lone '/' on its own line reaches us as etEOS still holding
// the value \"/\"" — every former inline `etEOS && value == L"/"` check in the
// emitters now routes through here.
FormatContext::Terminator classifyTerminator(const OpenEditor::Token& tk)
{
	if (tk.token == etSEMICOLON)
		return FormatContext::TermSemicolon;

	if (tk.token == etEOS)
	{
		// A lone '/' is the script terminator and must go on a line of its
		// own. Any other etEOS (a SQL*Plus command end, or the zero-length
		// sentinel appendEosSentinel() appends to a sliced sub-vector) is
		// line-attaching exactly like a ';', which is how the emitters already
		// treated it before this classification existed.
		return (tk.value == L"/") ? FormatContext::TermSlash
								  : FormatContext::TermSemicolon;
	}

	return FormatContext::TermNone;
}

bool terminatorIsScriptSlash(
	const FormatContext& node,
	const std::vector<OpenEditor::Token>& tokens,
	int fallbackIdx)
{
	// The tree node is authoritative whenever buildTree() recorded anything.
	if (node.terminator != FormatContext::TermNone)
		return node.terminator == FormatContext::TermSlash;

	// No classification on the node (truncated input, or a construct that
	// never became a node): fall back to the token the emitter had already
	// located, which reproduces the pre-Step-2.2 behaviour exactly.
	if (fallbackIdx < 0 || fallbackIdx >= (int)tokens.size())
		return false;
	return classifyTerminator(tokens.at(fallbackIdx)) == FormatContext::TermSlash;
}

// SQL*Plus line-command detection for buildTree(). Line commands have no
// ';' or '/' terminator: the command ends at end of line (a trailing '-'
// continues it onto the next line). Recognizing them lets buildTree close
// their UnknownStmt at EOL instead of swallowing every following statement
// up to the next ';'/'/'. Only consulted at Root scope, so PL/SQL SET
// (inside UPDATE), EXECUTE IMMEDIATE and a block-level EXIT keep their
// structural handling.
// File-local: buildTree()'s line-bound UnknownStmt opener below is the only
// caller. This used to be exported for hasPlsqlTriggerToken(), the pre-scan
// that decided whether a document could skip buildTree() and take the pure-SQL
// fast path. formatSQL() cannot represent a line command at all and corrupts
// them ("REM a" -> "REMa"), so that path is gone and every document is parsed.
static bool isSqlPlusLineCommand(
	const std::vector<OpenEditor::Token>& tokens, int idx)
{
	const auto& tk = tokens.at(idx);

	// '@' / '@@' script invocation. The PL/SQL tokenizer hands a bare '@'
	// back as etAT_SIGN (the script-mode etAT is never produced here), and a
	// doubled '@@' may arrive either as two etAT_SIGN tokens or as one
	// value-matched etUNKNOWN, so both shapes are accepted. String literals
	// carry their quotes in value, so the value test cannot collide with
	// them. A database-link '@' ("FROM t@dblink") is never seen here: this
	// probe only runs at Root scope with no statement open, i.e. at a
	// statement start, where '@' can only be a script invocation.
	if (tk.token == etAT_SIGN)
		return true;

	if (tk.token == etUNKNOWN && (tk.value == L"@" || tk.value == L"@@"))
		return true;

	// SET has its own token kind. At Root scope a statement-leading SET can
	// only be a SQL*Plus command in practice (the SQL SET lives inside
	// UPDATE/MERGE scopes; SET TRANSACTION/SET ROLE are vanishingly rare in
	// scripts and stay lossless verbatim anyway).
	if (tk.token == etSET)
		return true;

	// Dedicated-kind words that are SQL*Plus commands at Root scope. The
	// script-mode tokenizer has always produced these kinds, and since the Tier 1
	// folding fix s_plsqlTokens produces them too (OEPlsSqlParser.cpp:348-360), so
	// the PL/SQL tokenizer used by OnEditFormatSql() no longer hands most of these
	// words back as etUNKNOWN. The kind test therefore has to run BEFORE the
	// etUNKNOWN early return below: without it, every word that gained a kind
	// stops being recognized here, buildTree goes back to leaving its UnknownStmt
	// open to the next ';', and the formatter regresses in exactly the way the
	// EXECUTE IMMEDIATE gates did. s_cmds below still carries both spellings for
	// the words that remain etUNKNOWN.
	//
	// HELP, SHUTDOWN and STARTUP are not in s_cmds, so listing them here is a
	// widening rather than a re-spelling: they are genuine line commands that this
	// probe used to miss when the PL/SQL tokenizer produced them.
	switch (tk.token)
	{
		case etEXIT:
		case etWHENEVER:
		// Tier 1 additions - see OEPlsSqlParser.cpp:348-360.
		case etARCHIVE:
		case etBTITLE:
		case etHELP:
		case etPAUSE:
		case etREPFOOTER:
		case etREPHEADER:
		case etSHUTDOWN:
		case etSPOOL:
		case etSTARTUP:
		case etTIMING:
		case etTTITLE:
		case etUNDEFINE:
			return true;
		default:
			break;
	}

	if (tk.token != etUNKNOWN && tk.token != etEXECUTE)
		return false;

	static const wchar_t* const s_cmds[] = {
		L"ACCEPT",    L"APPEND",    L"ARCHIVE",   L"ATTRIBUTE", L"BREAK",
		L"BTITLE",    L"CHANGE",    L"CLEAR",     L"COLUMN",    L"COL",
		L"COMPUTE",   L"CONNECT",   L"CONN",      L"COPY",      L"DEFINE",
		L"DEL",       L"DESCRIBE",  L"DESC",      L"EDIT",      L"EXEC",
		L"EXECUTE",   L"EXIT",      L"GET",       L"HOST",      L"INPUT",
		L"LIST",      L"PASSWORD",  L"PAUSE",     L"PRINT",     L"PROMPT",
		L"QUIT",      L"RECOVER",   L"REM",       L"REMARK",    L"REPFOOTER",
		L"REPHEADER", L"RUN",       L"SAVE",      L"SHOW",      L"SPOOL",
		L"START",     L"STORE",     L"TIMING",    L"TITLE",     L"TTITLE",
		L"UNDEFINE",  L"VARIABLE",  L"VAR",       L"WHENEVER",  L"XQUERY",
	};
	const wchar_t* w = tk.value.c_str();
	for (const wchar_t* c : s_cmds)
	{
		if (_wcsicmp(w, c) != 0)
			continue;
		// PL/SQL "EXECUTE IMMEDIATE …" is structural, not a line command.
		if (tk.token == etEXECUTE || !_wcsicmp(w, L"EXECUTE"))
		{
			for (int j = idx + 1; j < (int)tokens.size(); ++j)
			{
				const auto& tn = tokens.at(j);
				if (tn.token == etEOL || tn.token == etCOMMENT)
					continue;
				if ((tn.token == etUNKNOWN || tn.token == etEXECUTE)
				&&  !_wcsicmp(tn.value.c_str(), L"IMMEDIATE"))
					return false;
				break;
			}
		}
		return true;
	}
	return false;
}

// True when the previous meaningful token (skipping EOL/comments) is a set
// operator (UNION / INTERSECT / MINUS). buildTree closes the preceding
// Select at the set operator itself, so the trailing ALL of "UNION ALL"
// arrives at Root scope unattached — it must NOT open an opaque UnknownStmt
// that would swallow every remaining set-op branch.
static bool prevMeaningfulIsSetOp(const std::vector<OpenEditor::Token>& tokens, int i)
{
	for (int j = i - 1; j >= 0; --j)
	{
		const EToken t = tokens.at(j).token;
		if (t == etEOL || t == etCOMMENT)
			continue;
		return t == etUNION || t == etINTERSECT || t == etMINUS_SQL;
	}
	return false;
}

// Compound-trigger timing-point label (md/plsql_formatter_rules.md §12.1):
//
//     BEFORE STATEMENT      BEFORE EACH ROW
//     AFTER  STATEMENT      AFTER  EACH ROW
//
// `i` is the index of the BEFORE/AFTER keyword. Returns the index of the
// label's last token (STATEMENT or ROW), or -1 when the tokens at `i` do not
// spell a timing-point label. None of STATEMENT / EACH / ROW has an EToken of
// its own, so all three arrive as etUNKNOWN — but only by value: ROW is in the
// SQL reserved-word table (g_11g_reserved) and therefore carries reserved != 0,
// while STATEMENT and EACH are absent from it and carry reserved == 0. The
// match below is on the word text alone, deliberately ignoring `reserved`,
// because the label shape is fixed by the grammar and the whole point is to
// recognize these three specific words.
//
// Two callers need the identical recognition, which is why it lives here
// rather than inline in either:
//   * buildTree, to turn "BEFORE EACH ROW IS" into a pending unit header so
//     the IS handler opens a TimingPoint Block, and
//   * skipEndName, to consume the matching "END BEFORE EACH ROW;" label so it
//     cannot open a bogus sibling UnknownStmt inside the compound body.
static int timingPointLabelEnd(const std::vector<OpenEditor::Token>& tokens, int i)
{
	const int N = (int)tokens.size();
	if (i < 0 || i >= N)
		return -1;
	const EToken t0 = tokens.at(i).token;
	if (t0 != etBEFORE && t0 != etAFTER)
		return -1;

	// Next token index at or after k that is not a line break. Comments are
	// deliberately NOT skipped: a comment inside the label would already have
	// been recorded as a Comment child of the enclosing scope, and consuming
	// it here too would make the emitter replay it twice.
	auto nextNonEol = [&](int k) -> int
	{
		while (k < N && tokens.at(k).token == etEOL) ++k;
		return k;
	};
	auto labelWord = [&](int k, const wchar_t* w) -> bool
	{
		return k < N
			&& tokens.at(k).token == etUNKNOWN
			&& !_wcsicmp(tokens.at(k).value.c_str(), w);
	};

	const int j = nextNonEol(i + 1);
	if (labelWord(j, L"STATEMENT"))
		return j;
	if (labelWord(j, L"EACH"))
	{
		const int k = nextNonEol(j + 1);
		if (labelWord(k, L"ROW"))
			return k;
	}
	return -1;
}

// ============================================================================
// buildTree — structural pre-pass (no width estimation).
// ============================================================================

FormatContext buildTree(const std::vector<OpenEditor::Token>& tokens,
						const FormatterSeed& seed /*= FormatterSeed()*/)
{
	FormatContext root;
	root.kind       = FormatContext::Root;
	root.openSrcIdx = -1;

	// False for a seeded selection fragment: the Root-only decisions below all mean
	// "this is document level", which the enclosing unit the seed stands in disproves.
	const bool atDocumentLevel = (seed.scope == FormatFragmentScope::Document);

	struct Frame { FormatContext* node; };
	std::vector<Frame> path;
	path.push_back({ &root });

	auto cur = [&]() -> FormatContext& { return *path.back().node; };

	// ---- Progress checkpoints (ParseTree stage) ----
	// See the "Progress callback" section of OEPlsSqlFormatter.h. Total is the
	// document's last source line; the reported "current" is the line of the
	// token that opened the root node just appended, so the status line reads
	// "parsing tree (line N of M)". Only ROOT-level appends report: buildTree()
	// opens far more nested scopes than root nodes, and reporting those too
	// would make the line number jump backwards. The scan for the last line
	// runs only when a scope is installed, so the ordinary (non-UI, test) path
	// pays nothing for it. The nested buildTree() calls that formatSQL() makes
	// for delegated slices stay silent — see g_formatterParseProgress in
	// OEPlsSqlFormatter.h.
	const bool reportParseProgress = g_formatterParseProgress && formatterProgressActive();

	int totalLines = 0;
	if (reportParseProgress)
		for (const auto& t : tokens)
			if (t.line > totalLines) totalLines = t.line;

	auto noteLevelOneNode = [&](int idx)
	{
		if (!reportParseProgress)
			return;
		const int n = (int)tokens.size();
		const int line = (idx >= 0 && idx < n) ? tokens.at(idx).line : 0;
		reportFormatterProgress(FormatStage::ParseTree, line, totalLines);
	};

	// Open a child scope: append to cur().children, push a new frame.
	auto openScope = [&](FormatContext::Kind k, int idx)
	{
		FormatContext child;
		child.kind       = k;
		child.openSrcIdx = idx;
		cur().children.push_back(std::move(child));
		// path still holds the Root frame alone, so this append created a
		// root-level node (the frame push below has not happened yet).
		if (path.size() == 2)
			noteLevelOneNode(idx);
		path.push_back({ &cur().children.back() });
	};

	// Close top-of-stack scope: seal last keyword entry, set closeSrcIdx, pop frame.
	//
	// Step 2.2: `term`/`termIdx` additionally record *which* token terminated
	// the construct, so emitters can ask the node instead of re-testing the
	// token stream. They default to TermNone/-1 for the closers that are not
	// statement terminators at all (UNION/INTERSECT/MINUS between SELECT
	// branches, ')' for a Paren/ParamList, …), and for END-closed scopes —
	// whose closeSrcIdx is the END keyword, not the ';' or '/' after it. Those
	// go through closeEnderScope() below instead.
	auto closeScope = [&](int idx,
						  FormatContext::Terminator term = FormatContext::TermNone,
						  int termIdx = -1)
	{
		auto& kws = cur().keywords;
		if (!kws.empty() && kws.back().closeSrcIdx < 0)
			kws.back().closeSrcIdx = idx;
		cur().closeSrcIdx      = idx;
		cur().terminator       = term;
		cur().terminatorSrcIdx = termIdx;
		if (path.size() > 1) path.pop_back();
	};

	// Add a keyword entry; simultaneously closes the previous clause range.
	auto addKeyword = [&](EToken t, int idx)
	{
		auto& kws = cur().keywords;
		if (!kws.empty() && kws.back().closeSrcIdx < 0)
			kws.back().closeSrcIdx = idx;
		ClauseEntry ce;
		ce.token  = t;
		ce.srcIdx = idx;
		kws.push_back(ce);
	};

	int parenDepth = 0;

	// Generalized "closer" stack: tracks constructs whose closing keyword is END
	// (CaseExpr → END [CASE], Block → END [name];, IfStmt → END IF,
	// LoopStmt → END LOOP), in LIFO order so the innermost open construct's
	// END is matched first.
	struct Ender { FormatContext::Kind kind; int pathDepth; };
	std::vector<Ender> enderStack;

	// Tracks PROCEDURE/FUNCTION keywords seen inside a Block scope (i.e. in a
	// DECLARE section or implied declare section) whose IS/AS has not yet been
	// consumed. When IS/AS is consumed, a new nested Block scope is opened at
	// that IS/AS token; BEGIN then completes it normally. Reset on ';' so that
	// forward declarations (PROCEDURE foo;) never leave the counter non-zero.
	int pendingSubprogramIs = 0;

	// Set when TRIGGER is seen; cleared when IS/AS or BEGIN is consumed.
	// While true, INSERT/UPDATE/DELETE/MERGE in the header must not open
	// DML scopes (they are event names, not statements).
	bool pendingTriggerHeader = false;

	// Unit kind for the IS/AS Block about to be opened (set when the keyword
	// is first seen, consumed when IS/AS fires).
	FormatContext::UnitKind pendingUnitKind    = FormatContext::None;
	// Index of the first non-EOL/non-comment token of the pending header.
	int                     pendingHeaderStart = -1;

	// Index of the last non-EOL token seen — the closeSrcIdx candidate when
	// a line-bound SQL*Plus UnknownStmt reaches end of line.
	int  lastNonEolIdx    = -1;
	// True while the innermost open scope is an UnknownStmt for a SQL*Plus
	// line command: it closes at end of line (trailing '-' continues) and
	// ';'/'/' inside it are command text, not statement terminators.
	bool unknownLineBound = false;
	// True while the innermost open scope is an UnknownStmt for a
	// slash-terminated opaque statement (CREATE [OR REPLACE] [AND
	// COMPILE|RESOLVE] JAVA …): its body is foreign-language text studded
	// with ';' characters, so only a standalone script '/' (etEOS) closes
	// it — a ';' inside is content, not a terminator.
	bool unknownSlashBound = false;

	// Step 2.1 — backward span extension for a forward-declared subprogram
	// header. pendingHeaderStart is captured at CREATE (see below) or at the
	// PROCEDURE/FUNCTION keyword itself, but NOT at any method-modifier words
	// (MEMBER/STATIC/OVERRIDING/CONSTRUCTOR/MAP/ORDER/NOT/FINAL/INSTANTIABLE)
	// that may precede it. tryParseNestedSubprogramHeader() includes those in
	// its prefixStart, so the tree node has to as well — otherwise the
	// emitter's "child.openSrcIdx == pos" dispatch misses when pos sits on the
	// modifier. Walks back over EOL and modifier words only; a comment (or any
	// other token, e.g. the previous item's ';') stops the walk, so a leading
	// comment always remains a sibling Comment child and the ordered,
	// non-overlapping partition validateTreeCoverage() requires still holds.
	// Doing this at close time keeps the forward linear scan untouched.
	auto extendHeaderStartBackward = [&](int start) -> int
	{
		int s = start;
		for (int k = start - 1; k >= 0; --k)
		{
			if (tokens.at(k).token == etEOL) continue;
			if (!isSubprogramModifierWord(tokens.at(k))) break;
			s = k;
		}
		return s;
	};

	// Last non-EOL, non-comment token seen — used to distinguish expression
	// CASE (after := / RETURN) from statement CASE, and to classify ( as
	// a ParamList (after a plain identifier) vs a Paren (after a keyword).
	EToken prevToken         = etEOL;
	bool   prevTokenReserved = false; // true when prevToken was etUNKNOWN reserved word

	const int N = (int)tokens.size();

	// Step 2.2 — locate the statement terminator that follows an END-based
	// closer. For those nodes closeSrcIdx is the END keyword itself, so the
	// ';' or script '/' that actually terminates the statement is a later
	// token and has to be found separately. Skips EOL/comments plus the
	// mandatory qualifier (IF / LOOP / CASE) or the optional unit/label name
	// that may follow END, tracking paren depth so a terminator inside a
	// trailing expression cannot be mistaken for the statement's own.
	// Returns -1 on truncated input (no terminator at all).
	auto findTerminatorAfterEnd = [&](int endIdx) -> int
	{
		int depth = 0;
		for (int j = endIdx + 1; j < N; ++j)
		{
			const EToken t = tokens.at(j).token;
			if (t == etEOL || t == etCOMMENT)          continue;
			if (t == etLEFT_ROUND_BRACKET)             { ++depth; continue; }
			if (t == etRIGHT_ROUND_BRACKET)            { if (depth > 0) --depth; continue; }
			if (depth == 0 && (t == etSEMICOLON || t == etEOS)) return j;
		}
		return -1;
	};

	// Step 2.2 — close an END-terminated statement scope and record its
	// terminator. CaseExpr is deliberately NOT routed through here: an
	// expression-level "CASE … END" has no terminator of its own, the next
	// ';' belongs to the enclosing statement, and recording it on the CaseExpr
	// node would be wrong.
	auto closeEnderScope = [&](int endIdx)
	{
		const int tIdx = findTerminatorAfterEnd(endIdx);
		closeScope(endIdx,
				   (tIdx < 0) ? FormatContext::TermNone
							  : classifyTerminator(tokens.at(tIdx)),
				   tIdx);
	};

	// Step 2.2 — consume the optional name that may follow an END-based
	// closer: "END pkg_example;" on a package/unit Block, "END label;" on a
	// labeled block or "END LOOP loop_name;" after the mandatory LOOP
	// qualifier. Accepts plain identifiers (etUNKNOWN with reserved == 0),
	// quoted identifiers, and — defensively — a dotted chain between them;
	// the scan stops at the first token that cannot continue a name (';',
	// a comment, a reserved word, …), so it never reaches past the
	// statement's own terminator, matching findTerminatorAfterEnd()'s view.
	//
	// Without this the name token survives the Block close and, at Root
	// scope, falls into the unrecognized-statement opener below, which grows
	// a bogus "UnknownStmt [name ;]" sibling right after an already-closed
	// Block. Emitters never showed it (emitBlock renders the whole
	// close..terminator region verbatim and skipEnder() steps past it), but
	// the tree lied — and the tree is what the <<<SYNTAX-TREE>>> capture
	// baselines. The mandatory IF/LOOP/CASE qualifiers are consumed the same
	// way ("i = j") by their handlers below.
	//
	// Returns the index of the last consumed token, or endIdx when no name
	// follows (the caller assigns it back to the scan variable i).
	auto skipEndName = [&](int endIdx) -> int
	{
		int  last       = endIdx;
		int  j          = endIdx + 1;
		bool expectName = true;   // false while a '.' continuation is allowed
		while (j < N)
		{
			if (tokens.at(j).token == etEOL) { ++j; continue; }
			// Compound-trigger timing-point label — "END BEFORE STATEMENT;",
			// "END AFTER EACH ROW;" (§12.1). BEFORE/AFTER are reserved words,
			// so the plain-identifier branch below cannot consume them and the
			// label would survive the Block close, opening a bogus sibling
			// UnknownStmt inside the compound-trigger body (and duplicating
			// tokens the section's own emitEndClause already replays).
			if (tokens.at(j).token == etBEFORE || tokens.at(j).token == etAFTER)
			{
				const int lblEnd = timingPointLabelEnd(tokens, j);
				return (lblEnd >= 0) ? lblEnd : last;
			}
			const bool isNameToken =
				(tokens.at(j).token == etUNKNOWN && tokens.at(j).reserved == 0)
				|| tokens.at(j).token == etDOUBLE_QUOTED_STRING;
			if (expectName && isNameToken)
			{
				last       = j;
				expectName = false;
				++j;
				continue;
			}
			if (!expectName && tokens.at(j).token == etDOT)
			{
				expectName = true;
				++j;
				continue;
			}
			break;
		}
		return last;
	};

	for (int i = 0; i < N; ++i)
	{
		const auto& tk = tokens.at(i);
		if (tk.token == etEOL)
		{
			// A line-bound SQL*Plus UnknownStmt closes at end of line —
			// unless the line ends with the SQL*Plus continuation '-'.
			if (unknownLineBound)
			{
				bool continued = lastNonEolIdx >= 0
					&& (tokens.at(lastNonEolIdx).token == etMINUS
					 || (tokens.at(lastNonEolIdx).token == etUNKNOWN
					  && tokens.at(lastNonEolIdx).value == L"-"));
				if (!continued)
				{
					if (cur().kind == FormatContext::UnknownStmt)
						closeScope(lastNonEolIdx >= 0 ? lastNonEolIdx : i - 1);
					unknownLineBound = false;
				}
			}
			continue;
		}
		lastNonEolIdx = i;

		// prevToken holds the last non-EOL, non-comment token *before* this one.
		// Update it at the end of each iteration via a scope-guard so every
		// continue path keeps it in sync without explicit updates everywhere.
		struct Guard {
			EToken& prev; EToken cur;
			bool& prevRes; bool curRes;
			~Guard() { prev = cur; prevRes = curRes; }
		}
			_guard{ prevToken,
					(tk.token != etCOMMENT) ? tk.token : prevToken,
					prevTokenReserved,
					(tk.token != etCOMMENT) ? (tk.token == etUNKNOWN && tk.reserved != 0)
											: prevTokenReserved };

		// ---- opaque UnknownStmt content skip ----
		// The body of an UnknownStmt is emitted verbatim by the emitter's
		// UnknownStmt arm, so no token inside it may be structurally
		// interpreted: keywords there belong to unmodeled DDL dialects or
		// even to an embedded foreign language (Java inside CREATE JAVA
		// SOURCE), and any scope opened on them would either never close
		// (runaway IfStmt swallowing the rest of the script) or re-layout
		// the fragment. For a statement-bound UnknownStmt only ';'/'/' pass
		// through to the boundary handler that closes it; for a line-bound
		// SQL*Plus command even those are command text (it closes at EOL).
		if (cur().kind == FormatContext::UnknownStmt)
		{
			if (unknownLineBound)
				continue;
			if (unknownSlashBound && tk.token != etEOS)
				continue;   // ';' inside e.g. a Java body is content
			if (tk.token != etSEMICOLON && tk.token != etEOS)
				continue;
		}

		// ---- propagate hasComment to current scope and its innermost open keyword ----
		// Also record statement-level comments as explicit Comment children so the
		// emitter can dispatch them without ad-hoc etCOMMENT guards.
		if (tk.token == etCOMMENT)
		{
			// hasComment marks a scope as unmeasurable/must-wrap: only single-line
			// "--" comments force a line break, so only those set it (matching the
			// original behavior). Inline block comments like "/*+hint*/" must NOT
			// set hasComment, otherwise trySimpleStmt/tryShortParen (which check
			// hasComment directly) would wrongly reject short statements/parens
			// that merely contain a hint comment.
			bool isLineComment = tk.value.size() >= 2 && tk.value.at(0) == L'-' && tk.value.at(1) == L'-';
			if (isLineComment)
			{
				cur().hasComment = true;
				auto& kws = cur().keywords;
				if (!kws.empty() && kws.back().closeSrcIdx < 0)
					kws.back().hasComment = true;
			}
			// At statement level (parenDepth == 0, inside Root or Block/IfStmt/LoopStmt)
			// record as a Comment child so the emitter can handle it structurally.
			 if (parenDepth == 0
			&&  (cur().kind == FormatContext::Root
			||   cur().kind == FormatContext::Block
			||   cur().kind == FormatContext::IfStmt
			||   cur().kind == FormatContext::LoopStmt
			||   cur().kind == FormatContext::CaseStmt
			||   cur().kind == FormatContext::CondCompile))
			{
				FormatContext c;
				c.kind       = FormatContext::Comment;
				c.openSrcIdx = i;
				c.closeSrcIdx= i;
				// Trailing-vs-own-line is a fact in the token stream, not
				// something to infer later: record it here, once, so the emitter
				// sites cannot each derive their own (and disagree).
				c.trailing   = isTrailingComment(tokens, i);
				cur().children.push_back(std::move(c));
				// A root-level comment is a document-level item in its own
				// right (the emitter gives it one), so it advances the parse
				// progress line just like a structural node does.
				if (cur().kind == FormatContext::Root)
					noteLevelOneNode(i);
			}
			continue;
		}

		// ---- statement boundary ----
		if (tk.token == etSEMICOLON || tk.token == etEOS)
		{
			// Step 2.2 — classify this terminator once, here, so every scope it
			// closes carries the same node-level classification instead of each
			// emitter re-testing `etEOS && value == L"/"` later on.
			const FormatContext::Terminator term = classifyTerminator(tk);

			// Don't close an open PL/SQL Block/IfStmt/LoopStmt scope on an inner
			// statement's semicolon — only close scopes above the innermost
			// pending statement-container (all of these hold nested statements
			// terminated by ';').
			int floor = 1;
				for (auto it = enderStack.rbegin(); it != enderStack.rend(); ++it)
				{
					if (it->kind == FormatContext::Block
					||  it->kind == FormatContext::IfStmt
					||  it->kind == FormatContext::LoopStmt
					||  it->kind == FormatContext::CaseStmt
					||  it->kind == FormatContext::CondCompile)
					{
						floor = it->pathDepth;
						break;
					}
				}
			while ((int)path.size() > floor)
				closeScope(i, term, i);
			if (floor == 1)
				enderStack.clear();
			// A slash-bound opaque statement (CREATE JAVA …) just closed on
			// this etEOS; while the flag is set no ';' can reach here (the
			// opaque-skip guard treats it as content).
			unknownSlashBound = false;

			// ---- Step 2.1: forward-declared / body-less subprogram header ----
			// A PROCEDURE/FUNCTION header that reaches this statement terminator
			// without ever seeing the IS/AS that would open a body Block is a
			// forward declaration: a package/type spec signature, a nested
			// forward decl in a declare section, or a body-less standalone
			// "CREATE OR REPLACE FUNCTION …" selection terminated by a script
			// '/'. buildTree() used to simply discard the pending header here,
			// which is exactly why the emitters had to re-scan raw tokens
			// (tryParseNestedSubprogramHeader) — and why the historical
			// missed-CREATE-OR-REPLACE bug happened. Record it as an explicit
			// SubprogramForwardDecl node so the span has one source of truth.
			//
			// Safety valve: only Procedure/Function are covered (Package /
			// TypeSpec / TypeBody / Trigger headers keep their existing
			// handling), and pending-header tracking itself requires
			// parenDepth == 0 at the keyword, so in-paren constructs (object
			// type member signatures) can never reach here.
			if (pendingSubprogramIs > 0
			&&  pendingHeaderStart >= 0
			&&  parenDepth == 0
			&&  (pendingUnitKind == FormatContext::Procedure
			||   pendingUnitKind == FormatContext::Function))
			{
				const int hdrStart = extendHeaderStartBackward(pendingHeaderStart);

				// Refuse to build a node that would overlap a sibling span which
				// starts *before* the header but is only being closed right now
				// (reachable only on malformed input): validateTreeCoverage()
				// requires an ordered, non-overlapping partition, and skipping
				// the node leaves the old raw-fragment fallthrough in place.
				bool overlapsSibling = false;
				for (const auto& sib : cur().children)
					if (sib.openSrcIdx < hdrStart && sib.closeSrcIdx >= hdrStart)
					{ overlapsSibling = true; break; }

				if (!overlapsSibling)
				{
					FormatContext fwd;
					fwd.kind        = FormatContext::SubprogramForwardDecl;
					fwd.unitKind    = pendingUnitKind;
					fwd.openSrcIdx  = hdrStart;   // no opener — see the Kind comment
					fwd.headerStart = hdrStart;
					fwd.closeSrcIdx = i;          // the ';' / '/' terminator itself
					// Step 2.2 — the terminator travels on the node as well, so
					// emitForwardDeclSpan() reads fwd.terminator rather than
					// re-deriving "';' or '/'?" from tokens[fwd.closeSrcIdx].
					// For a ';'-closed node the two indexes coincide by
					// construction; both are recorded so the emitter never has
					// to know which closing convention the node used.
					fwd.terminator       = term;
					fwd.terminatorSrcIdx = i;

					// Re-parent every child already created inside the header
					// span — the parameter-list ParamList/Paren, plus any
					// Comment children recorded eagerly at parenDepth == 0 —
					// into the new node so the sibling partition stays ordered
					// and non-overlapping. Children are appended in scan order,
					// so moving them in iteration order preserves that order.
					auto& sibs = cur().children;
					for (auto it = sibs.begin(); it != sibs.end(); )
					{
						if (it->openSrcIdx >= hdrStart)
						{
							fwd.children.push_back(std::move(*it));
							it = sibs.erase(it);
						}
						else
							++it;
					}

					// A single-line "--" comment inside the span makes the
					// header unmeasurable/must-wrap — same convention buildTree
					// uses when it propagates hasComment to the enclosing scope.
					for (const auto& ch : fwd.children)
						if (ch.kind == FormatContext::Comment
						&&  ch.openSrcIdx >= 0 && ch.openSrcIdx < N
						&&  tokens.at(ch.openSrcIdx).value.size() >= 2
						&&  tokens.at(ch.openSrcIdx).value.at(0) == L'-'
						&&  tokens.at(ch.openSrcIdx).value.at(1) == L'-')
						{ fwd.hasComment = true; break; }

					cur().children.push_back(std::move(fwd));
					// Re-parenting above moved any already-reported children into
					// fwd, so report the forward declaration itself as the new
					// root-level item it has become.
					if (cur().kind == FormatContext::Root)
						noteLevelOneNode(hdrStart);
				}
			}

			parenDepth = 0;
			pendingSubprogramIs = 0; // forward decl (PROC/FUNC;) — no IS seen
			pendingUnitKind     = FormatContext::None;
			pendingHeaderStart  = -1;
			continue;
		}

		// ---- SELECT ----
		if (tk.token == etSELECT)
		{
			openScope(FormatContext::Select, i);
			addKeyword(etSELECT, i);
			continue;
		}

		// ---- CREATE — captures headerStart for the upcoming unit Block ----
		if (tk.token == etCREATE && parenDepth == 0
		&&  (cur().kind == FormatContext::Root || cur().kind == FormatContext::Block))
		{
			// Inside a trigger header: a DDL event name, not a new statement.
			//   CREATE TRIGGER t INSTEAD OF CREATE ON SCHEMA …
			//                            ^^^^^^ this one
			// The Block for the trigger body does not open until BEGIN, so cur()
			// is still Root here and the lookahead below finds ON next — not a
			// recognized unit keyword — and would open an UnknownStmt on the
			// event word, splitting the header in two. Same guard DELETE /
			// UPDATE / INSERT / MERGE already carry for DML event names.
			if (pendingTriggerHeader) continue;
			// Conservative DDL policy: only CREATE forms with dedicated tree
			// modeling (PROCEDURE / FUNCTION / PACKAGE [BODY] / TYPE [BODY] /
			// TRIGGER, each optionally preceded by OR REPLACE and
			// EDITIONABLE / NONEDITIONABLE) continue down the pending-header
			// path. Every other CREATE (TABLE, INDEX, VIEW, SEQUENCE,
			// SYNONYM, MATERIALIZED VIEW, JAVA, LIBRARY, DIRECTORY, CONTEXT,
			// …) is opaque: at Root scope it opens an UnknownStmt on the
			// CREATE token itself, so the whole statement — including
			// keywords of the unmodeled dialect or of an embedded language —
			// is preserved verbatim up to its ';' or script '/' terminator,
			// instead of leaking scopes (Java "if (…)") or being re-laid-out
			// by the raw-fragment fallback.
			bool recognizedUnit = false;
			bool javaForm       = false;
			for (int j = i + 1; j < N; ++j)
			{
				const auto& tj = tokens.at(j);
				if (tj.token == etEOL || tj.token == etCOMMENT)
					continue;
				// OR REPLACE / AND COMPILE / AND RESOLVE modifiers
				if (tj.token == etOR || tj.token == etREPLACE
				||  tj.token == etAND || tj.token == etCOMPILE)
					continue;
				if (tj.token == etUNKNOWN
				&&  (!_wcsicmp(tj.value.c_str(), L"OR")
				  || !_wcsicmp(tj.value.c_str(), L"REPLACE")
				  || !_wcsicmp(tj.value.c_str(), L"EDITIONABLE")
				  || !_wcsicmp(tj.value.c_str(), L"NONEDITIONABLE")
				  || !_wcsicmp(tj.value.c_str(), L"RESOLVE")))
					continue;
				if (tj.token == etJAVA
				|| (tj.token == etUNKNOWN && !_wcsicmp(tj.value.c_str(), L"JAVA")))
					javaForm = true;
				recognizedUnit = (tj.token == etPROCEDURE || tj.token == etFUNCTION
							   || tj.token == etPACKAGE   || tj.token == etTRIGGER
							   || isTypeKeyword(tokens, j));
				break;
			}
			if (!recognizedUnit && atDocumentLevel && cur().kind == FormatContext::Root)
			{
				openScope(FormatContext::UnknownStmt, i);
				// A CREATE JAVA body is foreign-language text containing
				// ';' characters — only the standalone script '/' after it
				// terminates the statement, so mark the node slash-bound.
				unknownSlashBound = javaForm;
				continue;
			}
			pendingHeaderStart = i;
			// fall through — CREATE is not a scope opener
		}

		// ---- DELETE ----
		if (tk.token == etDELETE)
		{
			// Inside a trigger header or Merge scope: not a statement scope.
			if (!pendingTriggerHeader && cur().kind != FormatContext::Merge)
			{
				openScope(FormatContext::Delete, i);
				addKeyword(etDELETE, i);
			}
			continue;
		}

		// ---- UPDATE ----
		if (tk.token == etUPDATE)
		{
			// Inside a trigger header or Merge scope: not a statement scope.
			if (!pendingTriggerHeader && cur().kind != FormatContext::Merge)
			{
				openScope(FormatContext::Update, i);
				addKeyword(etUPDATE, i);
			}
			continue;
		}

		// ---- INSERT ----
		if (tk.token == etINSERT)
		{
			// Inside a trigger header: event name, not a statement scope.
			if (pendingTriggerHeader) continue;
			// Inside a Merge scope: INSERT is a sub-clause keyword, not a new scope.
			if (cur().kind == FormatContext::Merge)
			{
				addKeyword(etINSERT, i);
				continue;
			}
			// Choose scope kind after peeking: if ALL or FIRST follows → conditional.
			// We open InsertSimple first; if we later see WHEN we can't easily change
			// the kind, so we use InsertConditional for all ALL/FIRST forms and
			// InsertSimple for the plain single-INTO form.  We'll refine on seeing
			// ALL/FIRST.
			openScope(FormatContext::InsertSimple, i);
			addKeyword(etINSERT, i);
			continue;
		}

		// ---- MERGE ----
		if (tk.token == etMERGE)
		{
			if (pendingTriggerHeader) continue;
			openScope(FormatContext::Merge, i);
			addKeyword(etMERGE, i);
			continue;
		}

		// ---- MERGE clause keywords ----
		if (cur().kind == FormatContext::Merge)
		{
			if (tk.token == etUSING
			||  tk.token == etON
			||  tk.token == etWHEN
			||  tk.token == etUPDATE
			||  tk.token == etSET
			||  tk.token == etDELETE
			||  tk.token == etVALUES
			||  tk.token == etWHERE
			||  tk.token == etRETURNING
			||  tk.token == etLOG)
			{
				addKeyword(tk.token, i);
				continue;
			}
			// THEN: only record as keyword so width spans cover MATCHED..THEN..body
			if (tk.token == etTHEN)
			{
				addKeyword(etTHEN, i);
				continue;
			}
			// INTO: record only after RETURNING (disambiguate from MERGE INTO / USING)
			if (tk.token == etINTO)
			{
				auto& kws = cur().keywords;
				if (!kws.empty() && kws.back().token == etRETURNING)
					addKeyword(etINTO, i);
				continue;
			}
		}

		// ---- INSERT clause keywords ----
		if (cur().kind == FormatContext::InsertSimple
		||  cur().kind == FormatContext::InsertConditional)
		{
			// ALL/FIRST: upgrade scope kind to InsertConditional
			if (tk.token == etALL
			|| (tk.token == etUNKNOWN && !_wcsicmp(tk.value.c_str(), L"FIRST")))
			{
				cur().kind = FormatContext::InsertConditional;
				continue;
			}
			// RETURNING and LOG are always clause boundaries.
			if (tk.token == etRETURNING
			||  tk.token == etLOG)
			{
				addKeyword(tk.token, i);
				continue;
			}
			// INTO: only add as a keyword entry when it follows RETURNING
			// (i.e. it is the RETURNING INTO, not the INSERT INTO table clause).
			// Non-RETURNING INTOs are handled directly by the formatter loop.
			if (tk.token == etINTO)
			{
				auto& kws = cur().keywords;
				if (!kws.empty() && kws.back().token == etRETURNING)
					addKeyword(etINTO, i);
				continue; // always consume — never falls through to child-scope handling
			}
			// VALUES: clause boundary for width measurement.
			if (tk.token == etVALUES)
			{
				addKeyword(tk.token, i);
				continue;
			}
			// Conditional-form only: WHEN, THEN, ELSE
			if (cur().kind == FormatContext::InsertConditional)
			{
				if (tk.token == etWHEN
				||  tk.token == etTHEN
				||  tk.token == etELSE)
				{
					addKeyword(tk.token, i);
					continue;
				}
			}
		}

		// ---- UPDATE clause keywords ----
		if (cur().kind == FormatContext::Update)
		{
			if (tk.token == etSET
			||  tk.token == etWHERE
			||  tk.token == etRETURNING
			||  tk.token == etINTO
			||  tk.token == etLOG)
			{
				addKeyword(tk.token, i);
				continue;
			}
		}

		// ---- DELETE clause keywords ----
		if (cur().kind == FormatContext::Delete)
		{
			// etFROM is optional and stays on the same line — not a clause boundary.
			if (tk.token == etFROM)
				continue;
			if (tk.token == etWHERE
			||  tk.token == etRETURNING
			||  tk.token == etINTO)
			{
				addKeyword(tk.token, i);
				continue;
			}
		}

		// ---- SELECT clause keywords ----
		if (cur().kind == FormatContext::Select)
		{
			// Set operators terminate the current SELECT scope but are NOT
			// clause entries inside it — they are statement-level connectors.
			// Close the last open clause range, then close the SELECT scope.
			if (tk.token == etUNION
			||  tk.token == etINTERSECT
			||  tk.token == etMINUS_SQL)
			{
				auto& kws = cur().keywords;
				if (!kws.empty() && kws.back().closeSrcIdx < 0)
					kws.back().closeSrcIdx = i;
				closeScope(i);
				continue;
			}

			if (tk.token == etFROM
			|| tk.token == etWHERE
			|| tk.token == etHAVING
			|| tk.token == etGROUP
			|| tk.token == etORDER)
			{
				addKeyword(tk.token, i);
				continue;
			}
			// INTO between SELECT col-list and FROM (PL/SQL SELECT INTO).
			// Only at paren depth 0; ignore INTO inside subexpressions.
			if (tk.token == etINTO && parenDepth == 0)
			{
				addKeyword(etINTO, i);
				continue;
			}
			if (tk.token == etBY)
			{
				bool afterGroupOrder = false;
				for (int k = i - 1; k >= 0; --k)
				{
					if (tokens.at(k).token == etEOL) continue;
					afterGroupOrder = (tokens.at(k).token == etGROUP
								   || tokens.at(k).token == etORDER);
					break;
				}
				if (afterGroupOrder)
				{
					addKeyword(etBY, i);
					continue;
				}
			}
		}

		// ---- CASE ----
		// At statement level (Block or Root scope, outside parens) CASE opens a
		// CaseStmt — each WHEN branch has a statement list and is closed by
		// END CASE. Inside an expression (inside parens, or in a DML scope)
		// CASE opens a CaseExpr as before.
		// Exception: even at parenDepth==0 inside a Block, CASE that follows
		// := or RETURN is an expression (e.g. v_name := CASE ... END;) — it
		// must be treated as CaseExpr so END (without CASE qualifier) closes it.
		if (tk.token == etCASE)
		{
			bool exprContext = (parenDepth > 0)
				|| (prevToken == etASSIGN)
				|| (prevToken == etRETURN)
				|| (cur().kind != FormatContext::Block
				&&  cur().kind != FormatContext::Root
				&&  cur().kind != FormatContext::CaseStmt);
			if (!exprContext)
			{
				openScope(FormatContext::CaseStmt, i);
				addKeyword(etCASE, i);
				enderStack.push_back({ FormatContext::CaseStmt, (int)path.size() });
			}
			else
			{
				openScope(FormatContext::CaseExpr, i);
				enderStack.push_back({ FormatContext::CaseExpr, (int)path.size() });
			}
			continue;
		}

		// ---- COMPOUND TRIGGER body opener (§12.1) ----
		//   CREATE [OR REPLACE] TRIGGER name FOR <event> ON <table>
		//     [DISABLE] COMPOUND TRIGGER
		//       <declarations and timing-point sections>
		//   END [name];
		//
		// A compound trigger has no IS/AS: the two words COMPOUND TRIGGER are
		// themselves the body opener, so the Block is opened here instead of by
		// the IS/AS handler below. Without this the pending trigger header
		// survives until the first ';' *inside* the body ("g_n NUMBER := 0;"),
		// where the statement-boundary handler resets it without creating any
		// node — so the whole "CREATE … COMPOUND TRIGGER <decls>" prefix ends
		// up owned by no scope and is silently dropped, and the first timing
		// point's BEGIN opens a bogus Root-level anonymous block.
		//
		// openSrcIdx is the closing TRIGGER keyword of the pair — the header's
		// last token — because the unit has no IS/AS token to point at and
		// emitTriggerBodyHeader()'s range is exclusive (see the CompoundTrigger
		// comment on FormatContext::UnitKind). The body is (openSrcIdx,
		// closeSrcIdx), emitted like a package body's declare section.
		if (tk.token == etCOMPOUND && parenDepth == 0
		&&  (cur().kind == FormatContext::Root || cur().kind == FormatContext::Block))
		{
			// Only line breaks may separate COMPOUND from TRIGGER. A comment
			// between them is recorded eagerly (above) as a sibling Comment
			// child of the enclosing scope, and the header emitter replays the
			// whole [headerStart, openSrcIdx] span — recognizing the pair
			// anyway would emit that comment twice.
			int j = i + 1;
			while (j < N && tokens.at(j).token == etEOL) ++j;
			if (j < N && tokens.at(j).token == etTRIGGER)
			{
				openScope(FormatContext::Block, j);
				cur().unitKind    = FormatContext::CompoundTrigger;
				// pendingHeaderStart is the CREATE (or, for a selection that
				// starts mid-DDL, the leading TRIGGER). Falling back to the
				// COMPOUND token itself keeps the span valid when neither was
				// tracked, which cannot happen for well-formed input.
				cur().headerStart = (pendingHeaderStart >= 0) ? pendingHeaderStart : i;
				pendingSubprogramIs  = 0;
				pendingTriggerHeader = false;
				pendingUnitKind      = FormatContext::None;
				pendingHeaderStart   = -1;
				enderStack.push_back({ FormatContext::Block, (int)path.size() });
				i = j;   // consume the TRIGGER of "COMPOUND TRIGGER"
				continue;
			}
		}

		// ---- Compound-trigger timing-point section header (§12.1) ----
		// "BEFORE|AFTER STATEMENT|EACH ROW IS" inside a compound-trigger body
		// opens a nested Block exactly like a nested subprogram header does:
		// record it as pending and let the IS/AS handler below open the scope,
		// so the section gets unitKind == TimingPoint, headerStart == the
		// BEFORE/AFTER keyword and openSrcIdx == its IS.
		//
		// The full three/four-token label shape plus the following IS/AS is
		// required, so an ordinary BEFORE/AFTER word anywhere else in the body
		// (and any BEFORE/AFTER outside a compound trigger — the ancestor
		// scan below) is left alone.
		if ((tk.token == etBEFORE || tk.token == etAFTER)
		&&  parenDepth == 0
		&&  pendingSubprogramIs == 0
		&&  cur().kind == FormatContext::Block)
		{
			bool inCompound = false;
			for (size_t p = path.size(); p-- > 0; )
				if (path.at(p).node->kind == FormatContext::Block
				&&  path.at(p).node->unitKind == FormatContext::CompoundTrigger)
				{ inCompound = true; break; }
			const int lblEnd = inCompound ? timingPointLabelEnd(tokens, i) : -1;
			if (lblEnd >= 0)
			{
				// Comments between the label and its IS stay where they are:
				// the eager Comment handler above records them as children of
				// the compound Block, and emitTimingPointSection()'s header
				// emit replays them (its range spans [headerStart, isIdx]).
				int k = lblEnd + 1;
				while (k < N && (tokens.at(k).token == etEOL
							  || tokens.at(k).token == etCOMMENT)) ++k;
				if (k < N && (tokens.at(k).token == etIS || tokens.at(k).token == etAS))
				{
					pendingSubprogramIs = 1;
					pendingUnitKind     = FormatContext::TimingPoint;
					pendingHeaderStart  = i;
					i = lblEnd;   // consume the label; the IS handler follows
					continue;
				}
			}
		}

		// ---- PROCEDURE / FUNCTION / PACKAGE / TYPE header tracking (nested and standalone) ----
		// When PROCEDURE, FUNCTION, PACKAGE, or TYPE is seen at Root or Block scope,
		// the subsequent IS/AS opens a new nested Block scope for the body.
		// pendingUnitKind remembers which keyword was most recently seen so the
		// IS/AS opener can tag the Block with the correct UnitKind.
		//
		// The TYPE arm goes through isTypeKeyword() rather than testing etTYPE
		// directly: "RETURN emp.employee_id%TYPE" in a subprogram header lexes
		// its attribute name as etTYPE too, and treating that as a unit keyword
		// overwrote the pendingUnitKind the preceding FUNCTION/PROCEDURE had set,
		// so the unit was built as a TypeSpec and emitTypeSpec() re-laid-out (and
		// dropped tokens from) its body.
		if ((tk.token == etPROCEDURE || tk.token == etFUNCTION || tk.token == etPACKAGE
		||   tk.token == etTRIGGER
		||  (isTypeKeyword(tokens, i) && pendingHeaderStart >= 0))  // TYPE only valid after CREATE
		&&   parenDepth == 0
		&&  (cur().kind == FormatContext::Block || cur().kind == FormatContext::Root))
		{
			++pendingSubprogramIs;
			// If no CREATE was seen before this keyword, this is a nested
			// subprogram — the header starts here.
			if (pendingHeaderStart < 0)
				pendingHeaderStart = i;
			if (tk.token == etTRIGGER)
			{
				pendingTriggerHeader = true;
				pendingUnitKind      = FormatContext::Trigger;
			}
			else if (tk.token == etPACKAGE)
				pendingUnitKind = FormatContext::Package; // may be upgraded to PackageBody on BODY
			else if (isTypeKeyword(tokens, i))
				pendingUnitKind = FormatContext::TypeSpec; // may be upgraded to TypeBody on BODY
			else if (tk.token == etPROCEDURE)
				pendingUnitKind = FormatContext::Procedure;
			else if (tk.token == etFUNCTION)
				pendingUnitKind = FormatContext::Function;
			// fall through — the token is not a scope opener itself
		}
		// BODY after PACKAGE or TYPE upgrades the pending unit kind.
		if (tk.token == etBODY && parenDepth == 0
		&&  (cur().kind == FormatContext::Block || cur().kind == FormatContext::Root))
		{
			if (pendingUnitKind == FormatContext::Package)
				pendingUnitKind = FormatContext::PackageBody;
			else if (pendingUnitKind == FormatContext::TypeSpec)
				pendingUnitKind = FormatContext::TypeBody;
			// fall through
		}
		if ((tk.token == etIS || tk.token == etAS) && pendingSubprogramIs > 0
		&&   parenDepth == 0)
		{
			// Guard: "RETURN SELF AS RESULT" — the AS here is part of the
			// constructor return-type clause, not the body opener.
			// Skip this AS when the *next* non-EOL/non-comment token is
			// the word RESULT (case-insensitive).
			if (tk.token == etAS)
			{
				bool nextIsResult = false;
				bool callSpec = false;
				for (int j = i + 1; j < N; ++j)
				{
					if (tokens.at(j).token == etEOL || tokens.at(j).token == etCOMMENT) continue;
					if (tokens.at(j).token == etUNKNOWN
					&&  !_wcsicmp(tokens.at(j).value.c_str(), L"RESULT"))
						nextIsResult = true;
					// Call-spec subprogram: "AS LANGUAGE JAVA NAME '…'" or
					// "AS EXTERNAL NAME '…'" (C callout). This AS does not
					// open a body — there is no BEGIN…END, the statement
					// ends at its ';'. Leaving pendingSubprogramIs intact
					// lets the terminator handler model it as a
					// SubprogramForwardDecl (body-less header) instead of
					// opening a Block that would never see its END and
					// swallow the rest of the script. LANGUAGE has its own
					// token kind (s_plsqlTokens); EXTERNAL arrives as a word.
					if (tokens.at(j).token == etLANGUAGE
					|| (tokens.at(j).token == etUNKNOWN
					 && (!_wcsicmp(tokens.at(j).value.c_str(), L"LANGUAGE")
					  || !_wcsicmp(tokens.at(j).value.c_str(), L"EXTERNAL"))))
						callSpec = true;
					break;
				}
				if (nextIsResult || callSpec) continue; // not the body opener — skip
			}

			--pendingSubprogramIs;
			pendingTriggerHeader = false;
			// Open a nested Block at IS/AS. addKeyword records it as the first
			// keyword so emitBlock can distinguish this from a DECLARE-opened block.
			openScope(FormatContext::Block, i);
			// Tag with the unit kind determined above, and record the header start.
			FormatContext::UnitKind unitKind = pendingUnitKind;
			cur().unitKind    = unitKind;
			cur().headerStart = pendingHeaderStart;
			pendingUnitKind    = FormatContext::None;
			pendingHeaderStart = -1;
			addKeyword(tk.token, i); // etIS or etAS
			// A TypeSpec (CREATE TYPE ... AS OBJECT/RECORD (...) [NOT FINAL];)
			// has no END — it is terminated directly by the statement's ';'.
			// Don't push it onto enderStack, or the semicolon handler will
			// treat it as still awaiting an END and never close it, silently
			// swallowing all following top-level statements as its children.
			if (unitKind != FormatContext::TypeSpec)
				enderStack.push_back({ FormatContext::Block, (int)path.size() });
			continue;
		}

		// ---- PL/SQL block: DECLARE / BEGIN ----
		// A Block scope covers an optional DECLARE section, the BEGIN body, and
		// an optional EXCEPTION section, closed by the matching END.
		// DECLARE opens a new nested Block unless we are already inside a Block
		// that has not yet reached its own BEGIN (i.e. still in the declare section
		// of an IS/AS- or DECLARE-opened block) — in that case DECLARE is a local
		// type/variable keyword, not a sub-block opener.
		// Once a Block has seen BEGIN, any inner DECLARE starts a fresh sub-block.
		if (tk.token == etDECLARE
		&&  !(cur().kind == FormatContext::Block
			  && !getKeyword(cur().keywords, etBEGIN)))
		{
			openScope(FormatContext::Block, i);
			// A trigger body may start with DECLARE (no IS/AS before the body).
			// Transfer the pending trigger kind so the emitter knows to emit the
			// trigger header tokens that precede this DECLARE.
			if (pendingUnitKind == FormatContext::Trigger)
			{
				cur().unitKind    = FormatContext::Trigger;
				cur().headerStart = pendingHeaderStart;
			}
			pendingSubprogramIs = 0;
			pendingTriggerHeader = false;
			pendingUnitKind     = FormatContext::None;
			pendingHeaderStart  = -1;
			addKeyword(etDECLARE, i);
			enderStack.push_back({ FormatContext::Block, (int)path.size() });
			continue;
		}
		if (tk.token == etBEGIN)
		{
			// A TRIGGER body may start with BEGIN directly (no IS/AS).
			// Reset the pending counter so it doesn't bleed into the body.
			pendingSubprogramIs = 0;
			pendingTriggerHeader = false;
			// A BEGIN completes the current Block's header (DECLARE…BEGIN or
			// IS…BEGIN) only if this Block hasn't already seen its own BEGIN.
			if (cur().kind == FormatContext::Block
			&&  !getKeyword(cur().keywords, etBEGIN))
			{
				addKeyword(etBEGIN, i);
				continue;
			}
			// New Block opened by BEGIN: could be a trigger body (no IS/AS) or
			// an anonymous block.
			openScope(FormatContext::Block, i);
			if (pendingUnitKind == FormatContext::Trigger)
			{
				cur().unitKind    = FormatContext::Trigger;
				cur().headerStart = pendingHeaderStart;
			}
			pendingUnitKind    = FormatContext::None;
			pendingHeaderStart = -1;
			addKeyword(etBEGIN, i);
			enderStack.push_back({ FormatContext::Block, (int)path.size() });
			continue;
		}
		if (tk.token == etEXCEPTION
		&&  cur().kind == FormatContext::Block
		&&  getKeyword(cur().keywords, etBEGIN))  // only valid after BEGIN, not as a type name in DECLARE
		{
			addKeyword(etEXCEPTION, i);
			continue;
		}

		// ---- PL/SQL statement: IF … ELSIF … ELSE … END IF ----
		// Always opens a new nested scope (unlike DECLARE…BEGIN/LOOP, an IF
		// token never completes an already-open IfStmt's header), so directly
		// nested IF-inside-IF is handled correctly.
		if (tk.token == etIF)
		{
			// Defensive: the "IF" qualifier of an "END IF" whose END was
			// left unclaimed by a desynchronized ender stack must not open
			// a fresh IfStmt — it would never find its THEN/END IF and its
			// header-line scan would run to EOF. Treat it as content.
			if (prevToken == etEND)
				continue;
			openScope(FormatContext::IfStmt, i);
			addKeyword(etIF, i);
			enderStack.push_back({ FormatContext::IfStmt, (int)path.size() });
			continue;
		}

		// ---- conditional compilation: $IF … $ELSIF … $ELSE … $END ----
		// Modeled on IfStmt: $IF opens a new nested scope; $ELSIF/$ELSE are
		// clause boundaries; $END closes it. $THEN is subordinate (like THEN)
		// and not stored as its own keyword entry.
		if (tk.token == etCC_IF)
		{
			openScope(FormatContext::CondCompile, i);
			addKeyword(etCC_IF, i);
			enderStack.push_back({ FormatContext::CondCompile, (int)path.size() });
			continue;
		}

		// ---- $ERROR '<message>' $END — inline directive, not a block opener ----
		// Its matching $END must not be mistaken for the closer of an
		// enclosing $IF … $END scope, so just pass both tokens through as
		// plain statement content.
		if (tk.token == etCC_ERROR)
		{
			int j = i + 1;
			while (j < N && tokens.at(j).token != etCC_END) ++j;
			if (j < N) i = j; // consume up to (but not past) the matching $END
			continue;
		}

		// ---- PL/SQL statement: [FOR|WHILE] … LOOP … END LOOP ----
		// FOR/WHILE open the scope; LOOP completes the header (mirrors the
		// DECLARE…BEGIN pattern). A bare LOOP with no preceding FOR/WHILE
		// opens its own scope directly. Always opens a new nested scope
		// (a FOR/WHILE token never completes an already-open LoopStmt's
		// header), so directly nested FOR/WHILE-inside-loop is handled
		// correctly.
		if (tk.token == etFOR || tk.token == etWHILE)
		{
			if (pendingTriggerHeader) continue;
			// "OPEN cursor FOR <query | query-string> [USING …];" is a
			// cursor-open statement, not a loop header. Letting this FOR
			// open a LoopStmt desynchronizes every following END IF /
			// END LOOP match — the orphaned IF of an "END IF" then opens a
			// runaway IfStmt that swallows the rest of the script. Scan
			// back over the (possibly qualified) cursor name for the OPEN.
			if (tk.token == etFOR)
			{
				int k = i - 1;
				while (k >= 0)
				{
					const EToken t = tokens.at(k).token;
					if (t == etEOL || t == etCOMMENT
					||  t == etUNKNOWN || t == etDOT || t == etDOUBLE_QUOTED_STRING)
					{ --k; continue; }
					break;
				}
				if (k >= 0 && tokens.at(k).token == etOPEN)
					continue; // OPEN … FOR — statement content, not a loop
			}
			openScope(FormatContext::LoopStmt, i);
			addKeyword(tk.token, i);
			enderStack.push_back({ FormatContext::LoopStmt, (int)path.size() });
			continue;
		}
		if (tk.token == etLOOP)
		{
			if (cur().kind == FormatContext::LoopStmt
			&&  !getKeyword(cur().keywords, etLOOP))
			{
				addKeyword(etLOOP, i);
				continue;
			}
			openScope(FormatContext::LoopStmt, i);
			addKeyword(etLOOP, i);
			enderStack.push_back({ FormatContext::LoopStmt, (int)path.size() });
			continue;
		}

		// ---- CASE / Block clause keywords and END (innermost construct first) ----
		if (!enderStack.empty()
		&&  (int)path.size() >= enderStack.back().pathDepth)
		{
			auto enderKind = enderStack.back().kind;

			if (enderKind == FormatContext::CaseStmt)
				{
					// WHEN and ELSE are clause boundaries; THEN is subordinate to WHEN
					// (part of its span) so WHEN formattedLen covers "cond THEN stmts".
					if (tk.token == etTHEN)
					{
						// Record THEN on the current WHEN entry so the emitter can find it.
						for (int p = (int)path.size() - 1; p >= 0; --p)
						{
							if (path.at(p).node->kind == FormatContext::CaseStmt)
							{
								auto& kws = path.at(p).node->keywords;
								if (!kws.empty() && kws.back().token == etWHEN)
								{
									ClauseEntry ce; ce.token = etTHEN; ce.srcIdx = i;
									kws.push_back(ce);
								}
								break;
							}
						}
						continue;
					}
					// Helper: close the last WHEN or ELSE entry (not a THEN entry).
					auto closeLastWhenElse = [](std::vector<ClauseEntry>& kws, int idx)
					{
						for (int r = (int)kws.size() - 1; r >= 0; --r)
						{
							if ((kws.at(r).token == etWHEN || kws.at(r).token == etELSE)
							&&   kws.at(r).closeSrcIdx < 0)
							{
								kws.at(r).closeSrcIdx = idx;
								break;
							}
						}
					};
					if (tk.token == etWHEN || tk.token == etELSE)
					{
						for (int p = (int)path.size() - 1; p >= 0; --p)
						{
							if (path.at(p).node->kind == FormatContext::CaseStmt)
							{
								closeLastWhenElse(path.at(p).node->keywords, i);
								ClauseEntry ce; ce.token = tk.token; ce.srcIdx = i;
								path.at(p).node->keywords.push_back(ce);
								break;
							}
						}
						continue;
					}
					if (tk.token == etEND)
					{
						// Only close if END CASE follows.
						int j = i + 1;
						while (j < N && tokens.at(j).token == etEOL) ++j;
						if (j < N && tokens.at(j).token == etCASE)
						{
							// Close last open WHEN/ELSE entry.
							for (int p = (int)path.size() - 1; p >= 0; --p)
								if (path.at(p).node->kind == FormatContext::CaseStmt)
								{ closeLastWhenElse(path.at(p).node->keywords, i); break; }
							while (path.size() > 1 && cur().kind != FormatContext::CaseStmt)
								closeScope(i);
							if (cur().kind == FormatContext::CaseStmt)
							{
								closeEnderScope(i);
								enderStack.pop_back();
							}
							i = j; // consume the CASE qualifier
							continue;
						}
					}
				}
			else if (enderKind == FormatContext::CaseExpr)
			{
				// THEN is subordinate to WHEN (part of its span), not a peer clause.
				// It must not close the open WHEN entry — the WHEN range must extend
				// all the way to the next WHEN/ELSE/END so that formattedLen covers
				// "cond THEN value" and the short-WHEN heuristic works correctly.
				if (tk.token == etTHEN)
					continue;

				if (tk.token == etWHEN || tk.token == etELSE)
				{
					for (int p = (int)path.size() - 1; p >= 0; --p)
					{
						if (path.at(p).node->kind == FormatContext::CaseExpr)
						{
							auto& kws = path.at(p).node->keywords;
							if (!kws.empty() && kws.back().closeSrcIdx < 0)
								kws.back().closeSrcIdx = i;
							ClauseEntry ce;
							ce.token  = tk.token;
							ce.srcIdx = i;
							kws.push_back(ce);
							break;
						}
					}
					continue;
				}
				if (tk.token == etEND)
				{
					while (path.size() > 1 && cur().kind != FormatContext::CaseExpr)
						closeScope(i);
					if (cur().kind == FormatContext::CaseExpr)
					{
						closeScope(i);
						enderStack.pop_back();
					}
					continue;
				}
			}
			else if (enderKind == FormatContext::IfStmt)
			{
				if (tk.token == etELSIF || tk.token == etELSE)
				{
					auto& kws = cur().keywords;
					if (!kws.empty() && kws.back().closeSrcIdx < 0)
						kws.back().closeSrcIdx = i;
					ClauseEntry ce;
					ce.token  = tk.token;
					ce.srcIdx = i;
					kws.push_back(ce);
					continue;
				}
				if (tk.token == etEND)
				{
					// Only claim this END as the IfStmt's own closer when the
					// mandatory "IF" qualifier actually follows. Otherwise this
					// END must belong to some outer construct (e.g. malformed/
					// incomplete PL/SQL where this IF was never properly closed) —
					// leave the IfStmt open and let the token fall through
					// unhandled so it can't be misattributed/reordered.
					int j = i + 1;
					while (j < N && tokens.at(j).token == etEOL) ++j;
					if (j < N && tokens.at(j).token == etIF)
					{
						while (path.size() > 1 && cur().kind != FormatContext::IfStmt)
							closeScope(i);
						if (cur().kind == FormatContext::IfStmt)
						{
							closeEnderScope(i);
							enderStack.pop_back();
							i = j; // consume the "IF" qualifier
						}
						continue;
					}
				}
			}
			else if (enderKind == FormatContext::CondCompile)
			{
				if (tk.token == etCC_ELSIF || tk.token == etCC_ELSE)
				{
					auto& kws = cur().keywords;
					if (!kws.empty() && kws.back().closeSrcIdx < 0)
						kws.back().closeSrcIdx = i;
					ClauseEntry ce;
					ce.token  = tk.token;
					ce.srcIdx = i;
					kws.push_back(ce);
					continue;
				}
				if (tk.token == etCC_END)
				{
					while (path.size() > 1 && cur().kind != FormatContext::CondCompile)
						closeScope(i);
					if (cur().kind == FormatContext::CondCompile)
					{
						closeEnderScope(i);
						enderStack.pop_back();
					}
					continue;
				}
			}
			else if (enderKind == FormatContext::LoopStmt)
			{
				if (tk.token == etEND)
				{
					// Only claim this END as the LoopStmt's own closer when the
					// mandatory "LOOP" qualifier actually follows. Otherwise this
					// END must belong to some outer construct (e.g. malformed/
					// incomplete PL/SQL where this LOOP was never properly
					// closed) — leave the LoopStmt open and let the token fall
					// through unhandled so it can't be misattributed/reordered.
					int j = i + 1;
					while (j < N && tokens.at(j).token == etEOL) ++j;
					if (j < N && tokens.at(j).token == etLOOP)
					{
						while (path.size() > 1 && cur().kind != FormatContext::LoopStmt)
							closeScope(i);
						if (cur().kind == FormatContext::LoopStmt)
						{
							closeEnderScope(i);
							enderStack.pop_back();
							// Consume the "LOOP" qualifier plus the optional
							// loop name — "END LOOP loop_name;" — see
							// skipEndName().
							i = skipEndName(j);
						}
						continue;
					}
				}
			}
			else if (enderKind == FormatContext::Block)
			{
				// WHEN/THEN inside the EXCEPTION section are recorded as clause
				// entries on the Block scope so width estimation covers each handler.
				if (tk.token == etWHEN || tk.token == etTHEN)
				{
					auto& kws = cur().keywords;
					if (!kws.empty() && kws.back().closeSrcIdx < 0)
						kws.back().closeSrcIdx = i;
					ClauseEntry ce;
					ce.token  = tk.token;
					ce.srcIdx = i;
					kws.push_back(ce);
					continue;
				}
				if (tk.token == etEND)
				{
					while (path.size() > 1 && cur().kind != FormatContext::Block)
						closeScope(i);
					if (cur().kind == FormatContext::Block)
					{
						closeEnderScope(i);
						enderStack.pop_back();
						// Consume the optional unit/label name — "END
						// pkg_example;", "END do_work;", "END lbl;" — so it
						// stays part of this closer's close..terminator gap
						// instead of opening a bogus Root-level UnknownStmt.
						// See skipEndName().
						i = skipEndName(i);
					}
					continue;
				}
			}
		}

		// ---- parentheses ----
		if (tk.token == etLEFT_ROUND_BRACKET)
		{
			++parenDepth;
			// CREATE TYPE name UNDER supertype ( ... ); — a subtype whose
			// member list follows the UNDER clause directly, with no IS/AS
			// in between (unlike "AS OBJECT/RECORD (...)"). Since no IS/AS
			// ever arrives to open the TypeSpec Block (see the IS/AS handler
			// below), open it here instead, at this '(', so the header
			// (CREATE...TYPE...UNDER...) and member list are captured under
			// one scope exactly like the AS OBJECT/RECORD form.
			if (parenDepth == 1 && pendingSubprogramIs > 0
			&&  pendingUnitKind == FormatContext::TypeSpec
			&&  (cur().kind == FormatContext::Block || cur().kind == FormatContext::Root))
			{
				--pendingSubprogramIs;
				openScope(FormatContext::Block, pendingHeaderStart); // virtual open — no IS/AS token
				cur().unitKind    = pendingUnitKind;
				cur().headerStart = pendingHeaderStart;
				pendingUnitKind    = FormatContext::None;
				pendingHeaderStart = -1;
				openScope(FormatContext::ParamList, i);
				continue;
			}
			// Classify the paren scope:
			//   ParamList — the ( directly follows a plain identifier (not a reserved
			//               word), i.e. a subprogram call or header parameter list.
			//               etRIGHT_ROUND_BRACKET covers chained calls f()(...).
			//   Paren     — everything else: IN (...), NOT (...), boolean/math grouping,
			//               FOR cursor IN (SELECT ...), IF/ELSIF/WHILE condition parens,
			//               operator RHS, etc.
			bool isParamList =
				(prevToken == etUNKNOWN && !prevTokenReserved)
			||   prevToken == etRIGHT_ROUND_BRACKET;
			openScope(isParamList ? FormatContext::ParamList : FormatContext::Paren, i);
			continue;
		}
		if (tk.token == etRIGHT_ROUND_BRACKET && parenDepth > 0)
		{
			--parenDepth;
			while (path.size() > 1
			&&     cur().kind != FormatContext::Paren
			&&     cur().kind != FormatContext::ParamList)
				closeScope(i);
			if (cur().kind == FormatContext::Paren
			||  cur().kind == FormatContext::ParamList)
				closeScope(i);
			while (!enderStack.empty()
			&&      enderStack.back().pathDepth > (int)path.size())
				enderStack.pop_back();
			continue;
		}

		// ---- BETWEEN…AND ----
		if (tk.token == etUNKNOWN
		&& !_wcsicmp(tk.value.c_str(), L"BETWEEN"))
		{
			openScope(FormatContext::BetweenExpr, i);
			continue;
		}
		if (tk.token == etAND && cur().kind == FormatContext::BetweenExpr)
		{
			closeScope(i);
			continue;
		}

		// ---- set-operation continuation guard ----
		// "UNION ALL": the preceding Select closed at UNION, so the trailing
		// ALL lands here at Root scope. Let it float — the SQL-side emitter
		// merges set-op chains from sibling Selects plus loose set-op tokens;
		// opening an UnknownStmt here would swallow every remaining branch.
		if (tk.token == etALL && prevMeaningfulIsSetOp(tokens, i))
			continue;

		// ---- SQL*Plus line commands (SET / SPOOL / EXEC / @ …) ----
		// Line-oriented commands have no ';' or '/' terminator; they end at
		// end of line (a trailing '-' continues). Opening them as a
		// line-bound UnknownStmt keeps them verbatim and — critically —
		// stops the statement-bound UnknownStmt opener below from swallowing
		// every following statement up to the next ';' or '/'.
		if (parenDepth == 0
		&&  atDocumentLevel     // inside a seeded body SET/EXEC/@ are ordinary words
		&&  cur().kind == FormatContext::Root
		&&  pendingHeaderStart < 0
		&&  pendingSubprogramIs == 0
		&&  !pendingTriggerHeader
		&&  isSqlPlusLineCommand(tokens, i))
		{
			openScope(FormatContext::UnknownStmt, i);
			unknownLineBound = true;
			continue;
		}

		// ---- root-level unrecognized statement ----
		// Any token that reaches here without being consumed by a known
		// statement opener (SELECT/DML/PL·SQL unit/DECLARE/BEGIN/IF/LOOP/
		// CASE/$IF/etc.), while sitting directly at the Root scope, starts
		// a new UnknownStmt child. This gives every top-level statement the
		// parser doesn't otherwise recognize (COMMIT, EXECUTE IMMEDIATE,
		// ALTER, GRANT, bare calls, …) an explicit tree node with known
		// [openSrcIdx, closeSrcIdx) bounds, matching how SELECT/DML/PL-SQL
		// units are represented. It is closed by the ordinary statement-
		// boundary handler above (';' or script '/'), exactly like Select/
		// DML scopes. Only fires at the Root scope (not inside a Block
		// body), and is suppressed while a CREATE/PROCEDURE/FUNCTION/
		// PACKAGE/TRIGGER/TYPE header is still pending, so it never
		// interferes with recognized-unit detection.
		if (parenDepth == 0
		&&  atDocumentLevel     // a seeded fragment is a section, not a document
		&&  cur().kind == FormatContext::Root
		&&  pendingHeaderStart < 0
		&&  pendingSubprogramIs == 0
		&&  !pendingTriggerHeader)
		{
			// A root-level WITH starts a CteDml statement, not an opaque
			// UnknownStmt: its body is real, formattable SQL (CTE parens +
			// a trailing SELECT/DML) and the emitter splices the whole
			// range through formatSQL(). An UnknownStmt here would emit the
			// statement verbatim and lose all formatting.
			openScope(tk.token == etWITH ? FormatContext::CteDml
										 : FormatContext::UnknownStmt, i);
			continue;
		}
	}
	while (path.size() > 1)
		closeScope(N > 0 ? N - 1 : 0);

	// Step 2.3 — classify each declare-section item once, now that every scope
	// is closed and all children are attached. The walk consults
	// findForwardDeclChild()/findBlockChild(), so it needs the finished tree and
	// cannot run inline during the token loop. Defined in
	// OEPlsSqlFormatterStmts.cpp (it needs skipEnder() and the tryParseXxx
	// probes); declared in OEPlsSqlFormatter.h, so no new include here.
	tagDeclareItems(root, tokens);

	return root;
}

// ============================================================================
// calcWidths — formatter-policy width pass.
// ============================================================================

static bool widthNeedsSpace(EToken prev, EToken cur)
{
	if (prev == etNONE)                        return false;
	if (prev == etDOT  || cur == etDOT)        return false;
	if (prev == etLEFT_ROUND_BRACKET)          return false;
	if (cur  == etLEFT_ROUND_BRACKET)          return false;
	if (cur  == etRIGHT_ROUND_BRACKET)         return false;
	if (cur  == etCOMMA || cur == etSEMICOLON) return false;
	return true;
}

#ifdef _DEBUG
// Phase A instrumentation — see the declaration block in OEPlsSqlFormatter.h.
// Counts every token accumWidth() actually examines, process-wide. The
// SQLFormatter constructor attributes the slice-measuring portion of it to
// g_plsqlPerf.remeasuredTokens by taking a delta around its own calcWidths().
unsigned long long g_accumWidthTokensScanned = 0;
#endif

struct ChildRange { int open; int close; int len; };

static int accumWidth(
	const std::vector<OpenEditor::Token>& tokens,
	int from, int to,
	const std::vector<ChildRange>& childRanges,
	EToken& prevToken,
	bool&   tainted)
{
	int width = 0;
	int i = from;
	while (i < to)
	{
		bool skipped = false;
		for (const auto& cr : childRanges)
		{
			if (i == cr.open)
			{
				if (cr.len == INT_MAX) { tainted = true; }
				else
				{
					if (widthNeedsSpace(prevToken, etLEFT_ROUND_BRACKET))
						width += 1;
					width += cr.len;
					prevToken = etRIGHT_ROUND_BRACKET;
				}
				i = cr.close + 1;
				skipped = true;
				break;
			}
		}
		if (skipped) continue;

		const auto& tk = tokens.at(i);
		++i;
#ifdef _DEBUG
		++g_accumWidthTokensScanned;
#endif
		if (tk.token == etEOL) continue;
		if (tk.token == etCOMMENT)
		{
			if (tk.value.size() >= 2 && tk.value.at(0) == L'-' && tk.value.at(1) == L'-')
				tainted = true;
			continue;
		}
		if (widthNeedsSpace(prevToken, tk.token))
			width += 1;
		width += (int)tk.value.size();
		prevToken = tk.token;
	}
	return width;
}

static int calcWidthsNode(
	FormatContext& node,
	const std::vector<OpenEditor::Token>& tokens)
{
	std::vector<ChildRange> childRanges;
	for (auto& child : node.children)
	{
		int cw = calcWidthsNode(child, tokens);
		if (child.openSrcIdx >= 0 && child.closeSrcIdx >= 0)
			childRanges.push_back({ child.openSrcIdx, child.closeSrcIdx, cw });
		if (child.hasComment)
			node.hasComment = true;
	}
	std::sort(childRanges.begin(), childRanges.end(),
		[](const ChildRange& a, const ChildRange& b){ return a.open < b.open; });

	if (node.openSrcIdx < 0 || node.closeSrcIdx < 0)
	{
		node.formattedLen = INT_MAX;
		return INT_MAX;
	}

	// measure each ClauseEntry range
	for (auto& ce : node.keywords)
	{
		if (ce.srcIdx < 0 || ce.closeSrcIdx < 0) continue;
		EToken prev  = etNONE;
		bool tainted = false;
		int w = accumWidth(tokens, ce.srcIdx + 1, ce.closeSrcIdx,
						   childRanges, prev, tainted);
		if (tainted) { ce.hasComment = true; ce.formattedLen = INT_MAX; }
		else         { ce.formattedLen = w; }
	}

	// measure the whole scope. Rely solely on accumWidth's own tainted result —
	// it already distinguishes single-line "--" comments (which force a line
	// break and make the scope unmeasurable) from inline block comments like
	// "/*+hint*/" (which don't taint the width). Do NOT short-circuit on the
	// blanket node.hasComment flag here: it is set for *any* comment kind
	// (see buildTree's etCOMMENT handling), which would incorrectly force
	// formattedLen = INT_MAX even for statements containing only inline
	// block-comment hints.
	EToken prev  = etNONE;
	bool tainted = false;
	int w = accumWidth(tokens, node.openSrcIdx, node.closeSrcIdx,
					   childRanges, prev, tainted);
	if (node.kind == FormatContext::Paren || node.kind == FormatContext::ParamList)
		w += 2;

	if (tainted) { node.hasComment = true; node.formattedLen = INT_MAX; return INT_MAX; }
	node.formattedLen = w;
	return w;
}

void calcWidths(FormatContext& root,
				const std::vector<OpenEditor::Token>& tokens)
{
	for (auto& child : root.children)
		calcWidthsNode(child, tokens);
}

} // namespace OpenEditor
