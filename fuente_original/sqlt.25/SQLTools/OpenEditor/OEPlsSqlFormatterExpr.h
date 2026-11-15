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
// Expression-level emitters: §9.1/§9.2 math and boolean wrapping, §8 concat
// (||) wrapping, §9.3 parameter-list wrapping.
//
// Phase F1 of md/sql_plsql_formatter_integration_plan.md. These are DEFINED in
// OEPlsSqlFormatterStmts.cpp at namespace OpenEditor scope, outside that file's
// single anonymous namespace, so they have always had external linkage — they
// were simply not declared in any header. Nothing was moved to expose them.
//
// Why a separate header rather than adding them to OEPlsSqlFormatterStmts.h:
// that header is documented as statement/block-level, and the SQL formatter
// (OEPlsSqlFormatterSqlSide.cpp) does not include it. Pulling it in there would make
// the SQL side depend on the whole PL/SQL statement layer for two functions,
// which is the dependency-cycle risk F1 exists to avoid. This header depends
// only on OEPlsSqlFormatterCommon.h, which OEPlsSqlFormatterSqlSide.cpp already
// includes.
//
// NOTE ON DEFAULT ARGUMENTS: this header is the single owner of them. The
// definitions in OEPlsSqlFormatterStmts.cpp deliberately repeat the parameter
// lists WITHOUT defaults, because C++ forbids redefining a default argument by
// a later declaration in the same scope (C2572). Add or change a default here,
// never there.
//
// The summaries below are the caller-facing contract. The definitions in
// OEPlsSqlFormatterStmts.cpp keep their original §8 / §9.1-§9.3 comment blocks,
// which carry more implementation detail (break-point selection, column
// bookkeeping, the AlignParen re-anchoring); read those before changing
// behaviour.
//
// PHASE F3 — INJECTED POLICY. F1 left these emitters reading FmtPlsql* straight
// off `const Settings&`. That was the coupling the plan targets: "a small
// parameter object replacing the direct s.GetFmtPlsql…() reads inside the two
// emitters — preferred, and it makes them genuinely shared." They now take an
// ExprWrapPolicy instead, so the wrapping widths and || options come from the
// caller. This is what lets the SQL side reuse them without inheriting PL/SQL
// limits — the plan forbids reusing FmtPlsqlConcatMaxLen / FmtPlsqlExpressionMaxLen
// for SQL contexts, and the five SQL suites would flag it.
//
// F3 is behaviour-neutral: every current caller passes
// ExprWrapPolicy::FromPlsql(s), which reads exactly the four settings the
// emitters used to read themselves, so no document changes.
// ============================================================================

// ---------------------------------------------------------------------------
// ExprWrapPolicy — the injected wrapping policy (Phase F3)
//
// One field per knob the two expression emitters actually read. Plain scalars,
// so a policy is cheap to build once at the top of an enclosing emit function
// and forward down through recursion.
//
// Deliberately NOT here: FmtPlsqlParamListMaxLen / FmtPlsqlParamListAlignParen.
// Param-list wrapping is its own policy family (§9.3), the plan still calls its
// SQL-side shape unresolved, and those two settings are read from three other
// places (OEPlsSqlFormatterDecls.cpp, plus two more sites in Stmts.cpp) that F3
// does not touch. Folding them in would widen F3 past "the two emitters" and
// pre-empt that decision — so emitParamListStatement keeps reading them from
// Settings and merely forwards the expression policy to its inner calls.
// ---------------------------------------------------------------------------
// ExprWrapPolicy was folded into FormatterPolicy (see
// md/FormatterPolicy_proposal.md and OEPlsSqlFormatterCommon.h) — its four
// fields (expressionMaxLen, concatMaxLen, concatKeepMixedPairTogether,
// concatBreakBetweenLiterals) now live there under the same names, and
// FormatterPolicy::FromSettings(s) supersedes both FromPlsql(s)/FromSql(s),
// which used to read the same four FmtPlsql* settings for both call sites.
// ---------------------------------------------------------------------------

// Emits tokens[start..end] (inclusive, EOLs skipped) with math/boolean
// operator-aware line-wrapping per §9.1 and §9.2.
//
// Math split (§9.1): breaks at low-priority ops (+/-) when present at the top
// level (parenDepth==0); falls back to high-priority ops (*/) only when no
// low-priority ops exist at top level. Paren groups are opaque — never split
// inside them at this level.
//
// Boolean split (§9.2): breaks before each top-level AND/OR; THEN/LOOP stays
// attached to the last condition line (not placed on its own line).
//
// If the flat length fits within pol.expressionMaxLen, no wrapping; if that is
// 0, no wrapping. PL/SQL callers get it from FmtPlsqlExpressionMaxLen via
// ExprWrapPolicy::FromPlsql.
//
// `children`, when supplied, is used to suppress a BetweenExpr's closing AND as
// a boolean break point. `continuationSameLevel` keeps continuation lines at the
// current indent instead of one step deeper — needed when emitting the interior
// of an already-opened paren group. `indentCharsOverride` re-anchors the
// "current indent" to an absolute column (AlignParen mode) rather than
// indentLevel*step; see FormatWriter::SetIndentChars.
//
// Returns true if it emitted something (operators were found and wrapping
// applied), false if the caller should fall through to plain normalized emit.
bool emitMathBoolAwareStatement(
	const std::vector<OpenEditor::Token>& tokens,
	int start, int end, /*inclusive*/
	int indentLevel,
	const FormatterPolicy& pol,
	FormatWriter& fw,
	const std::vector<FormatContext>* children = nullptr,
	bool continuationSameLevel = false,
	int indentCharsOverride = -1);

// Emits tokens[start..end] (inclusive) with ||-aware line-wrapping per §8,
// driven by pol.concatMaxLen, pol.concatKeepMixedPairTogether and
// pol.concatBreakBetweenLiterals. PL/SQL callers fill those from
// FmtPlsqlConcatMaxLen / FmtPlsqlConcatKeepMixedPairTogether /
// FmtPlsqlConcatBreakBetweenLiterals via ExprWrapPolicy::FromPlsql.
//
// With pol.concatMaxLen == 0, or when the flat length fits, everything is
// emitted on one line. Otherwise each break-eligible || that would push the
// line over the limit starts a continuation line. Break eligibility depends on
// keepMixed: a || crossing a literal<->variable boundary is only a *fallback*
// break point, used when no preferred break point exists before overflow — a
// preference, not an absolute rule, so an alternating lit/var run still wraps.
//
// Unlike emitMathBoolAwareStatement() this ALWAYS emits: with no || in range,
// or with the limit at 0, it falls back to a normalized flat emit rather than
// returning false. Callers therefore do not test a return value.
// `indentCharsOverride` means the same as above.
void emitConcatAwareStatement(
	const std::vector<OpenEditor::Token>& tokens,
	int start, int end /*inclusive*/,
	int indentLevel,
	const FormatterPolicy& pol,
	FormatWriter& fw,
	int indentCharsOverride = -1);

// Emits a call/parameter list per §9.3, driven by FmtPlsqlParamListMaxLen and
// FmtPlsqlParamListAlignParen. The range is given as four indices because the
// paren group and its trailing suffix wrap differently:
//
//   prefixStart..parenOpen    — the callable name, then '(' at parenOpen
//   parenOpen+1..parenClose-1 — the parameters
//   suffixStart..suffixEnd    — tokens after ')' (rest of statement incl. ';')
//
// `indentLevel` is the indent for the first line; continuation indent is always
// "current indent + 1 level" (FormatWriter::WriteContinuationIndent), and in
// AlignParen mode the "current indent" is re-anchored to the absolute column
// after '(' via fw.SetIndentChars(). The caller is responsible for emitting a
// trailing '\n'.
//
// Exposed alongside the two above because Phase F4 showed the three interact:
// this emitter's claim-the-statement rule had to learn to decline a statement
// whose prefix carries a top-level ||, so that control falls through to
// emitConcatAwareStatement(). Any new caller must answer that precedence
// question the same way — see F4 in the plan before wiring this into SQL.
//
// Now takes a single FormatterPolicy: its own §9.3 param-list wrapping is
// driven by pol.plsqlParamListMaxLen/plsqlParamListAlignParen, and the same
// pol is forwarded to the emitConcatAwareStatement / emitMathBoolAwareStatement
// calls it makes for an over-wide individual parameter. This collapses the
// former dual (Settings, ExprWrapPolicy) signature into one parameter.
void emitParamListStatement(
	const std::vector<OpenEditor::Token>& tokens,
	int prefixStart, int parenOpen, int parenClose,
	int suffixStart, int suffixEnd,
	int indentLevel,
	const FormatterPolicy& pol,
	FormatWriter& fw);

} // namespace OpenEditor
