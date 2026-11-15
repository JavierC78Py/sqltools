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

#include "OpenEditor/OEPlsSqlParser.h"
#include <vector>
#include <string>
#include <functional>
#include <climits>

// ---------------------------------------------------------------------------
// Indexing convention (crash-hardening) — these sources were the first
// retrofit of the REPO-WIDE rule (maintainer decision 2026-09-16; see
// .github/copilot-instructions.md §"C++ Indexing" and 1st-readme.md
// §"Indexing convention"):
// use bounds-checked .at() for every container/std::wstring subscript whose
// index is not *obviously* bounded within a few lines (loop condition or an
// adjacent range check). operator[] is retained only for:
//   - raw pointers / C arrays guarded in the same expression (word[k] in
//     isWord(), the kindNames/unitKindNames/termNames/tagNames lookup tables,
//     the WriteSpaces stack buffer), and
//   - std::map operator[] where insert-on-miss is the intended idiom
//     (setOpBrickByDepth, marked at each site).
// Rationale: a stale srcIdx or an off-by-one used to read out of bounds and
// crash SQLTools outright (corpus stress run, 2026-09); .at() converts that
// into a catchable std::out_of_range, which SqlFormatterHandler::Execute
// reports (what() + Common::StackTracer trace) instead of a dead UI process.
// ---------------------------------------------------------------------------

namespace OpenEditor {

	class Settings;
	struct FormatterPolicy;
	// Phase B — the SQL formatter's input is read-only. formatSQL() used to take
	// a non-const vector because one comment-alignment site mutated it in place;
	// that is fixed (see OEPlsSqlFormatterSqlSide.cpp, "B1"), so callers may now pass
	// their own token vector without it being silently rewritten.
	//
	// E2b — and its *output* is read-only too, in the same sense: formatSQL()
	// returns the SQL formatter's output token run rather than a std::wstring.
	// It used to return text, which forced every PL/SQL delegation site to
	// re-absorb a nested statement as an opaque blob: FormatWriter bound to the
	// caller's StringSink appended the characters, and a token-backed sink would
	// have had to re-split them into etUNKNOWN runs (see TokenStreamSink::
	// AppendText), erasing the token types the nested formatter had just
	// established. Returning the run lets a caller splice it with
	// FormatWriter::SpliceInto(), which appends each token through
	// IFormatSink::AppendToken() so its type survives the boundary.
	//
	// The run is the concatenation-equal image of the old string: flush() writes
	// exactly these tokens' values, and asserts in debug that they agree with
	// the sink's character mirror, so concatTokenValues(formatSQL(...)) is the
	// text formatSQL() used to return.
	std::vector<Token> formatSQL (const std::vector<Token>& tokens, const FormatterPolicy& pol);

// ============================================================================
// ClauseEntry — one keyword boundary within a scope.
//
// For a SELECT scope:
//   etSELECT (col-list SELECT→FROM), etFROM (table-list),
//   etWHERE, etGROUP, etBY, etHAVING, …
// For a CaseExpr scope:
//   etWHEN (one per clause, range covers "cond THEN value" — up to next WHEN/ELSE/END)
//   etELSE (range covers the else-value — up to END)
//
//   NOTE: etTHEN is intentionally NOT stored as a keyword entry.
//   THEN is subordinate to the preceding WHEN, so the WHEN entry's range spans
//   the full "cond THEN value" fragment.  formattedLen therefore covers that
//   whole fragment and the short-WHEN heuristic works correctly.
//
// srcIdx      — index of this keyword in input tokens[].
// closeSrcIdx — index of the next clause's first token (the terminator); -1 until set.
// formattedLen — estimated single-line visual width of the list after this keyword.
// hasComment   — any -- comment in that list.
// ============================================================================
struct ClauseEntry
{
	EToken token        = etNONE;
	int    srcIdx       = -1;
	int    closeSrcIdx  = -1;
	int    formattedLen = INT_MAX;
	bool   hasComment   = false;
};

// ============================================================================
// FormatContext — one node in the SQL syntax tree.
//
// Built by buildTree(), widths filled by calcWidths().
// ============================================================================
struct FormatContext
{
	enum Kind {
		Root,             // invisible document root (openSrcIdx == -1)
		Paren,            // ( … ) sub-expression or subquery block
		Select,           // SELECT … ; or SELECT … UNION branch
		CaseExpr,         // CASE … END
		BetweenExpr,      // BETWEEN … AND
		Delete,           // DELETE … ;
		Update,           // UPDATE … ;
		InsertSimple,     // reserved
		InsertConditional,// reserved
		Merge,            // MERGE … ;

		// ---- PL/SQL kinds (see md/plsql_formatter_rules.md §2) ----
		Block,            // DECLARE…BEGIN…EXCEPTION…END, or named-unit body
		IfStmt,           // IF … ELSIF … ELSE … END IF
		LoopStmt,         // LOOP / FOR…LOOP / WHILE…LOOP … END LOOP
		CaseStmt,         // CASE … WHEN … THEN stmts … END CASE (statement context)
		ParamList,        // ( … ) subprogram header/call parameter list
		ConcatExpr,       // chain of || operands
		MathExpr,         // chain of low-priority +/- operands
		CondCompile,      // $IF … $THEN … $ELSIF … $ELSE … $END
		Verbatim,         // unrecognized construct — bypass fallback
		ExceptionHandler, // one WHEN <exception> THEN … clause
		Comment,          // standalone -- or /* */ comment at statement level
		UnknownStmt,      // root-level unrecognized statement, terminated by ; or /

		// Forward-declared / body-less subprogram header (Step 2.1 of
		// md/plsql_formatter_buildtree_migration_plan.md):
		//   PROCEDURE|FUNCTION name [(params)] [RETURN type] ;
		//   CREATE [OR REPLACE] FUNCTION f RETURN NUMBER DETERMINISTIC /
		// i.e. a header that reaches its statement terminator without ever
		// seeing the IS/AS that would open a body Block.
		//
		// NOTE — this kind deliberately deviates from the "openSrcIdx is the
		// scope opener (IS/AS/BEGIN/DECLARE)" convention every other kind
		// follows: a forward declaration has no opener. Instead
		//   openSrcIdx == headerStart == first header token (including any
		//                 CREATE [OR REPLACE] prefix and method modifiers),
		//   closeSrcIdx == the terminating ';' or script '/' token *itself*
		//                 (so it is part of the node's owned span),
		//   unitKind    == Procedure or Function,
		//   keywords    == empty (there is no clause structure),
		//   children    == the parameter-list ParamList/Paren and any Comment
		//                  children that fall inside the header span
		//                  (re-parented here by buildTree so the sibling
		//                  partition stays ordered and non-overlapping).
		SubprogramForwardDecl,

		// Root-level "WITH <cte-list> SELECT|DML …" compound statement.
		// Structurally parsed like Select/DML — the parenthesized CTE bodies
		// become Paren children (with their own Select scopes inside) and the
		// trailing query becomes a Select (or DML) child — and closed by the
		// ordinary statement-boundary handler at ';' or script '/'. The
		// emitter treats it as a DML kind (isDmlKind) and splices the whole
		// [openSrcIdx, closeSrcIdx] token range through formatSQL(), which
		// owns the WITH layout. Deliberately NOT opaque like UnknownStmt:
		// an UnknownStmt body is emitted verbatim, which would bypass all
		// formatting for the statement.
		CteDml,
	};

	// Sub-classification for Block nodes opened by IS/AS (named program units).
	// UnitKind::None covers anonymous blocks (DECLARE/BEGIN) and inner blocks.
	enum UnitKind {
		None,          // anonymous block or non-unit block
		Procedure,     // PROCEDURE … IS/AS
		Function,      // FUNCTION … IS/AS
		Package,       // PACKAGE (spec) … IS/AS
		PackageBody,   // PACKAGE BODY … IS/AS
		TypeSpec,      // CREATE TYPE … AS OBJECT/UNDER (…)
		TypeBody,      // CREATE TYPE BODY … AS/IS
		Trigger,       // TRIGGER … IS/AS  or  TRIGGER … BEGIN

		// CREATE [OR REPLACE] TRIGGER … COMPOUND TRIGGER … END [name];
		//
		// A compound trigger has no IS/AS body opener, so this deviates from
		// the "openSrcIdx is the scope opener" convention the same way
		// SubprogramForwardDecl does: openSrcIdx is the *closing* TRIGGER
		// keyword of the "COMPOUND TRIGGER" pair — the last token of the
		// header — and the body is everything in (openSrcIdx, closeSrcIdx).
		// headerStart is the CREATE (or, without one, the leading TRIGGER).
		// The body is emitted exactly like a package body's declare section
		// (md/plsql_formatter_rules.md §12.1): ordinary declarations plus one
		// TimingPoint section per BEFORE/AFTER clause, blank-line separated.
		// keywords holds no IS/AS/BEGIN entry of its own — every BEGIN inside
		// belongs to a nested TimingPoint or subprogram Block.
		CompoundTrigger,

		// One timing-point section of a compound trigger:
		//   BEFORE|AFTER STATEMENT|EACH ROW IS [decls] BEGIN … END <label>;
		// Opened by its IS like a nested subprogram body, so openSrcIdx is
		// that IS token and headerStart is the BEFORE/AFTER keyword. The
		// trailing "END BEFORE STATEMENT;" label tokens sit between
		// closeSrcIdx (the END) and terminatorSrcIdx (the ';'), exactly like
		// a block label — skipEndName() consumes them so they cannot start a
		// spurious UnknownStmt.
		TimingPoint,
	};

	Kind     kind         = Root;
	UnitKind unitKind     = None;

	int  openSrcIdx   = -1;       // index in tokens[] of the opener (IS/AS/BEGIN/DECLARE)
	int  closeSrcIdx  = -1;       // index in tokens[] of the closer
	int  headerStart  = -1;       // first non-EOL/non-comment token of the unit header
								  // (valid when unitKind != None; -1 otherwise)
	bool hasComment   = false;    // any -- comment between opener and closer
	int  formattedLen = INT_MAX;  // estimated single-line visual width

	// Meaningful on a Comment child only. True when the author wrote the
	// comment on the same source line as the code before it, so it annotates
	// that code rather than introducing whatever follows. buildTree() reads it
	// once through isTrailingComment() instead of letting each emitter re-derive
	// it, and the emitter then keeps such a comment on the line it annotates —
	// own-lining it silently re-attaches it to the *next* statement, which is a
	// correctness defect, not a style choice. Subject to the width veto and the
	// swallow / script-'/' invariants; see
	// md/plsql_formatter_endline_comments_proposal.md rules 1-11.
	bool trailing     = false;

	// ---- Step 2.2: statement-terminator classification ----
	// Which token terminated this node, so the emitters no longer each
	// re-derive "a plain ';' or a script '/'?" from raw tokens (see
	// md/plsql_formatter_buildtree_migration_plan.md Step 2.2).
	//
	//   TermNone      — nothing recorded: the node is unclosed/truncated, or
	//                   its kind has no statement terminator of its own (Root,
	//                   Comment, ParamList, Paren, CaseExpr, …).
	//   TermSemicolon — a *line-attaching* terminator: a real ';', or any
	//                   etEOS that is not a lone '/' (a SQL*Plus command end,
	//                   or the zero-length sentinel appendEosSentinel() adds).
	//                   Emitted directly after the preceding text.
	//   TermSlash     — a script terminator: a lone '/' sitting on its own
	//                   line. The lexer reclassifies etSLASH → etEOS while
	//                   keeping the "/" value, so this is the one case that
	//                   must go on a line of its own and can never be
	//                   appended to a header or body.
	//
	// terminatorSrcIdx is the index of that token. It is deliberately NOT
	// folded into closeSrcIdx: for a ';'-closed node (Select/DML scopes,
	// SubprogramForwardDecl, a collection-type TypeSpec Block) the two hold
	// the same index, but for an END-closed node (Block, IfStmt, LoopStmt,
	// CaseStmt, CondCompile) closeSrcIdx is the END keyword and the real
	// terminator is a later token. Keeping them separate is what lets an
	// emitter read the classification without first knowing which of the two
	// closing conventions the node used.
	//
	// Token fidelity still applies (md/plsql_formatter_rules.md §1a): emit
	// tokens[terminatorSrcIdx].value, never a literal L";" or L"/".
	enum Terminator {
		TermNone,
		TermSemicolon,
		TermSlash,
	};
	Terminator terminator       = TermNone;
	int        terminatorSrcIdx = -1;

	// ------------------------------------------------------------------------
	// Step 2.3 — declare-section item classification
	// (md/plsql_formatter_buildtree_migration_plan.md).
	//
	// emitDeclareSection() identifies each declaration in a DECLARE…BEGIN or
	// IS…BEGIN section by running the tryParseXxx probes in sequence until one
	// succeeds, so a late-chain item pays for every probe before it: a
	// collection type is fourth, and reaching it costs a failed
	// tryParseVarDecl, tryParseCursorDecl and tryParseRecordTypeDecl first.
	// buildTree() now walks each declare section once and records the verdict
	// here, so the emitter dispatches on a lookup instead of by trial.
	//
	// These are deliberately NOT tree children. Where a declaration has real
	// structure, that structure is already a child (a cursor's query Select, a
	// nested subprogram's body Block, ParamList/Paren, Comment), so a per-item
	// child spanning the whole declaration would overlap its own descendants
	// and trip validateTreeCoverage(). declItems is a side table on the owning
	// Block: it carries classification only. Field spans still come from the
	// one tryParseXxx the emitter calls after dispatching.
	//
	// DeclItemOther means "recognized, but not taggable": SUBTYPE, PRAGMA and
	// the generic-statement fallback advance the emitter's cursor through paths
	// the tagging walk does not replicate, so on reaching one it records Other
	// and stops, leaving the remainder of that section untagged. Untagged
	// positions and DeclItemOther both read back as "run the original full
	// probe chain", so nothing loses coverage.
	// ------------------------------------------------------------------------
	enum DeclItemKind {
		DeclItemNone = 0,      // no tag at this position — run the full chain
		DeclItemComment,       // standalone -- or /* */ comment
		DeclItemForwardDecl,   // SubprogramForwardDecl child (Step 2.1)
		DeclItemNestedSubprog, // PROCEDURE/FUNCTION … IS/AS <body>
		DeclItemVar,           // name [CONSTANT] type [:= expr] ;
		DeclItemCursor,        // CURSOR name [(params)] IS <query> ;
		DeclItemRecordType,    // TYPE name IS RECORD ( … ) ;
		DeclItemCollectionType,// TYPE name IS TABLE OF … / VARRAY(n) OF … ;
		DeclItemOther,         // untaggable item — run the full chain
	};
	struct DeclItem
	{
		int          startIdx = -1; // first token of the item (after skipEol)
		DeclItemKind kind     = DeclItemNone;
	};
	// Ordered by startIdx. Empty unless buildTree() found a declare section.
	std::vector<DeclItem> declItems;
	// The rangeEnd declItems was classified against. declItemKindAt() honours
	// the tags only when the caller's range matches this exactly, so a declare
	// range derived differently — emitPackage omits the EXCEPTION fallback that
	// emitBlock applies — can never dispatch on a tag computed for another
	// range. -1 means "no tags".
	int declItemsEnd = -1;

	std::vector<ClauseEntry>  keywords; // clause boundaries owned by this scope
	std::vector<FormatContext> children; // nested scopes
};

// ============================================================================
// FormatFragmentScope / FormatterSeed — what a token stream represents.
//
// A whole-file format is always Document. A *selection* format carries the
// section the selection sits in, so buildTree() and the emit entry point treat
// the fragment the way the same tokens are treated when the enclosing unit is
// present. See md/plsql_formatter_selection_seed_plan.md.
// ============================================================================
enum class FormatFragmentScope {
	Document,      // top level: script / compilation unit (today's only behaviour)
	Declarations,  // DECLARE…BEGIN or IS…BEGIN section  -> emitDeclareSection()
	Body,          // BEGIN…END[/EXCEPTION] statement run -> emitStatementSequence()
	Exception,     // EXCEPTION…END handler list          -> emitExceptionSection() (P2)
	TypeMembers,   // CREATE TYPE … AS OBJECT (…) member list -> emitTypeMemberList()
};

struct FormatterSeed {
	FormatFragmentScope   scope = FormatFragmentScope::Document;
	FormatContext::UnitKind owner = FormatContext::None;  // enclosing construct
	int                     depth = 0;  // formatter indent levels the fragment sits at

	bool seeded() const { return scope != FormatFragmentScope::Document; }
};

// ============================================================================
// Helpers
// ============================================================================

// Returns a pointer to the ClauseEntry matching key (and optionally srcIdx).
// Pass srcIdx >= 0 to disambiguate duplicate tokens (e.g. multiple WHEN entries).
ClauseEntry*       getKeyword(std::vector<ClauseEntry>& v,
							   EToken key, int srcIdx = -1);
const ClauseEntry* getKeyword(const std::vector<ClauseEntry>& v,
							   EToken key, int srcIdx = -1);

// Returns a pointer to the FormatContext node whose openSrcIdx == targetIdx,
// searching the whole subtree rooted at node.  Returns nullptr when not found.
FormatContext* findInTree(FormatContext& node, int targetIdx);

// Returns true when tk is one of the method-modifier words that may precede a
// PROCEDURE/FUNCTION keyword in an object-type member declaration:
//   OVERRIDING, MEMBER, STATIC, CONSTRUCTOR, MAP, ORDER, NOT, FINAL,
//   INSTANTIABLE
// They are lexed as plain (non-reserved) etUNKNOWN words.
//
// Single source of truth shared by buildTree() (backward span extension when
// it creates a SubprogramForwardDecl node), findForwardDeclChild() and
// tryParseNestedSubprogramHeader(). Declared here — rather than in
// OEPlsSqlFormatterCommon.h — because buildTree() lives in this translation
// unit and OEPlsSqlFormatterCommon.h already includes this header, so putting
// it here avoids a layering inversion without adding any new include.
bool isSubprogramModifierWord(const OpenEditor::Token& tk);

// Returns true when tokens[idx] is a genuine TYPE *keyword* — the one that
// opens "CREATE TYPE …" or a declare-section "TYPE name IS …" — rather than
// the attribute name of a "%TYPE" datatype attribute reference such as
// "emp.employee_id%TYPE".
//
// The lexer maps both spellings onto etTYPE. That makes TYPE the one PL/SQL
// attribute whose name collides with a reserved word: %ROWTYPE, %BULK_ROWCOUNT,
// %FOUND, %ISOPEN and friends are not in g_11g_reserved[] (OEPlsSqlParser.cpp)
// and so arrive as plain etUNKNOWN identifiers, which is why they never needed
// special handling. Every structural decision keyed on etTYPE must ask this
// first. Without it a %TYPE sitting inside a subprogram header —
// "RETURN emp.employee_id%TYPE" — re-arms buildTree()'s pending-unit tracking,
// overwrites the pendingUnitKind a preceding FUNCTION/PROCEDURE keyword set,
// and the whole unit is misclassified as a TypeSpec. emitTypeSpec() then
// re-lays-out the body and silently drops tokens.
//
// EOL and comment tokens between '%' and TYPE are skipped, so "…id%\n TYPE"
// and "…id%/*c*/TYPE" are recognized as the same attribute reference (the
// lexer treats both as whitespace-equivalent, and so does Oracle).
// An out-of-range idx returns false.
//
// Declared here — rather than in OEPlsSqlFormatterCommon.h — for the same
// layering reason as isSubprogramModifierWord(): buildTree() lives in
// OEPlsSqlFormatterTree.cpp and OEPlsSqlFormatterCommon.h already includes
// this header, so putting it here avoids a layering inversion without adding
// any new include.
bool isTypeKeyword(const std::vector<OpenEditor::Token>& tokens, int idx);

// ============================================================================
// Step 2.2 — statement-terminator classification (single source of truth).
//
// classifyTerminator() maps ONE token onto FormatContext::Terminator. It is
// the only place that knows a lone '/' reaches the formatter as etEOS while
// still carrying the value "/", so buildTree() and every emitter agree on the
// ';' vs '/' layout decision instead of each re-checking
// `token == etEOS && value == L"/"` inline. Four such sites existed before
// this step: OEPlsSqlFormatterDecls.cpp (emitForwardDeclSpan),
// OEPlsSqlFormatterStmts.cpp (emitStatementSequence's compilation-unit
// terminator arm) and twice in OEPlsSqlFormatterUnits.cpp (emitTypeSpec's
// no-member-list and member-list branches).
//
// terminatorIsScriptSlash() is the node-first read for emitters that hold a
// FormatContext: it returns the classification buildTree() recorded on `node`,
// and only falls back to classifying tokens[fallbackIdx] when the node
// recorded TermNone (truncated input, or a construct buildTree() produced no
// node for). The fallback keeps the result identical to the pre-Step-2.2
// inline checks, so no layout can silently change.
//
// Declared here — rather than in OEPlsSqlFormatterCommon.h — for the same
// layering reason as isSubprogramModifierWord(): buildTree() lives in this
// translation unit and OEPlsSqlFormatterCommon.h already includes this header.
// ============================================================================
FormatContext::Terminator classifyTerminator(const OpenEditor::Token& tk);

bool terminatorIsScriptSlash(const FormatContext& node,
							 const std::vector<OpenEditor::Token>& tokens,
							 int fallbackIdx);

// ============================================================================
// buildTree — structural pre-pass (no width estimation).
//
// Returns a Root node whose children[] own the complete syntax tree for all
// statements in tokens[].  Comments are noted (hasComment); lengths are not.
//
// `seed` says what the stream represents. A seeded (selection) run keeps the
// Root kind but suppresses the decisions that only mean something at document
// level - above all the root-level UnknownStmt opener, which would otherwise
// swallow a body or declare fragment whole. Default = Document = today's path.
// ============================================================================
FormatContext buildTree(const std::vector<OpenEditor::Token>& tokens,
						const FormatterSeed& seed = FormatterSeed());

// ============================================================================
// Step 2.3 — declare-section item tagging.
//
// tagDeclareItems() walks the finished tree and fills FormatContext::declItems
// on every Block that owns a declare section. It classifies with exactly the
// probes emitDeclareSection() uses, in exactly its order, over exactly its
// range, and advances its cursor exactly as the emitter does — that identity is
// what makes the recorded verdict trustworthy, since there is one
// classification implementation (the tryParseXxx probes) rather than two that
// could drift apart. buildTree() calls it as its last act, so tags are ready
// before any emission and are recomputed whenever the tree is. Note the tree is
// rebuilt on every format call, so this removes failed probes from a single
// pass; it is not a cross-format cache.
//
// declItemKindAt() is the emitter-side read. It returns DeclItemNone unless
// `node` carries tags classified for this exact rangeEnd AND one of them starts
// at `pos`. A tag therefore can only ever skip probes that were guaranteed to
// fail — it never selects a different emitter than the untagged chain would
// have, so output is unchanged by construction.
//
// Both are defined in OEPlsSqlFormatterStmts.cpp rather than here: the walk
// advances past a nested subprogram body with skipEnder(), which is internal to
// that translation unit, and it needs the tryParseXxx probes declared in
// OEPlsSqlFormatterDecls.h, which this header must not depend on. Declared here
// for the same layering reason as isSubprogramModifierWord() — buildTree()'s
// translation unit already includes this header, so it needs no new include.
// ============================================================================
void tagDeclareItems(FormatContext& root,
					 const std::vector<OpenEditor::Token>& tokens);

FormatContext::DeclItemKind declItemKindAt(const FormatContext& node,
										   int pos, int rangeEnd);

// ============================================================================
// calcWidths — formatter-policy width pass.
//
// Walks the tree produced by buildTree() and fills in formattedLen on every
// FormatContext node and every ClauseEntry.  hasComment is propagated upward.
// Must be called after buildTree() and before the formatting loop.
// ============================================================================
void calcWidths(FormatContext& root,
				const std::vector<OpenEditor::Token>& tokens);

// ============================================================================
// checkScriptSlashLayout — debug-only whole-buffer layout check.
//
// Verifies layout invariant L2: every line consisting solely of the script
// terminator '/' must be followed by a blank line (or be the last content in
// the buffer). Returns a report naming the offending output line numbers;
// empty when the invariant holds. Defined in OEPlsSqlFormatterCommon.cpp.
//
// This covers a class the token-fidelity guards structurally cannot see:
// EmitRangeGuard strips whitespace before comparing (expectedRangeText also
// skips etEOL), so "right tokens, wrong line" passes it silently. See the
// layout-invariant block in OEPlsSqlFormatterCommon.h, which documents both
// invariants and why line-break count is *not* one of them (the formatter
// reflows lines freely, so line structure is not something it preserves).
// ============================================================================
#ifdef _DEBUG
std::wstring checkScriptSlashLayout(const std::wstring& formatted);
#endif

// ============================================================================
// Formatter diagnostics sink — automated-testing support.
//
// The PL/SQL formatter has self-checks that report a failure through a MODAL
// dialog:
//
//   • EmitRangeGuard (OEPlsSqlFormatterCommon.h) — _ASSERTE() on a token
//     fidelity violation. In a Debug build with no debugger attached the CRT
//     pops its own modal "Debug Assertion Failed" dialog.
//   • verifyFormatterLosslessness() and verifyOperatorTokenization()
//     (OEView_7.cpp) — CErrorDlg::DoModal().
//   • checkScriptSlashLayout() (OEView_7.cpp) — CErrorDlg::DoModal().
//   • reportLayoutViolation() (OEPlsSqlFormatterCommon.cpp) — _ASSERTE() on a
//     script-'/' line-isolation violation.
//
// Either one deadlocks COM-driven automation (SQLTools.Proxy.TestSqlFormatter
// → WM_COPYDATA → COEditorView::OnEditFormatSql): the UI thread waits for a
// click that never comes, the proxy call times out after 10 s, and the mismatch
// text — the only useful diagnostic — is lost with it.
//
// While g_pFormatterDiagnostics is non-null those checks append their report to
// the pointed-to string instead of showing a dialog. Install it through
// FormatterDiagnosticsScope (RAII). The scope additionally redirects the CRT
// assert report mode, so an assert raised anywhere else during a test cannot
// pop a dialog either.
//
// Only ever touched from the UI thread, so no synchronization is required.
// ============================================================================
extern std::wstring* g_pFormatterDiagnostics;

// True while a FormatterDiagnosticsScope is active.
inline bool formatterDiagnosticsActive() { return g_pFormatterDiagnostics != nullptr; }

// True while a format runs with no user present to see or answer anything.
//
// That is exactly the COM-driven automated-testing path (WM_COPYDATA →
// TestController → SqlFormatterHandler), which is also the only installer of
// FormatterDiagnosticsScope, so today the two predicates coincide. Keeping them
// distinct by name matters anyway: every *user-facing* side effect of a format
// keys off this one — the progress status line, the "still working" prompt (see
// the "Progress callback" section below) and the self-check dialogs — so each
// site reads as "am I headless?" instead of "am I capturing asserts?". If a
// dedicated automation flag ever appears (a command-line switch, a
// TestController "am I under test" query), this is the single place to change.
inline bool formatterHeadless() { return formatterDiagnosticsActive(); }

class FormatterDiagnosticsScope
{
public:
	explicit FormatterDiagnosticsScope(std::wstring& report);
	~FormatterDiagnosticsScope();

	FormatterDiagnosticsScope(const FormatterDiagnosticsScope&) = delete;
	FormatterDiagnosticsScope& operator=(const FormatterDiagnosticsScope&) = delete;

private:
	std::wstring* m_prev;
	int           m_prevAssertMode; // _CRT_ASSERT report mode saved by the ctor
	int           m_prevWarnMode;   // _CRT_WARN   report mode saved by the ctor
};

// ============================================================================
// Formatter tree-trace sink — automated-testing support.
//
// formatPlsql() dumps the parsed syntax tree with traceSyntaxTree()
// (OEPlsSqlFormatterBlocks.cpp). In Debug builds that dump always goes to the
// MSVC Output window via TRACE. This sink additionally makes the dump
// capturable in ANY build, the same way g_pFormatterDiagnostics captures
// self-check reports: while g_pFormatterTreeTrace is non-null, the capture
// variant of traceSyntaxTree() appends its lines to the pointed-to string.
//
// SqlFormatterHandler (AutomatedTesting) installs the scope when a test
// payload carries ".FmtTraceTree=1" and returns the captured dump wrapped in
// <<<SYNTAX-TREE>>> ... <<<END-SYNTAX-TREE>>> markers ahead of the formatted
// text, so a regression suite can baseline the *tree shape* (e.g. that a
// package's "END pkg_name;" produces no stray UnknownStmt sibling) and not
// just the rendered output.
//
// The captured dump deliberately omits the "w=" formattedLen field: in Debug
// calcWidths() has already run when the tree is traced, in Release it has
// not, so including it would make test baselines build-configuration
// dependent. The structure (kind, spans, unit, terminator, keywords) is
// identical in both configurations.
//
// Only ever touched from the UI thread, so no synchronization is required.
// ============================================================================
extern std::wstring* g_pFormatterTreeTrace;

// True while a FormatterTreeTraceScope is active.
inline bool formatterTreeTraceActive() { return g_pFormatterTreeTrace != nullptr; }

// Appends one pre-formatted line/section header to the capture buffer when a
// FormatterTreeTraceScope is active; a no-op otherwise.
void appendFormatterTreeTrace(const wchar_t* text);

class FormatterTreeTraceScope
{
public:
	explicit FormatterTreeTraceScope(std::wstring& trace);
	~FormatterTreeTraceScope();

	FormatterTreeTraceScope(const FormatterTreeTraceScope&) = delete;
	FormatterTreeTraceScope& operator=(const FormatterTreeTraceScope&) = delete;

private:
	std::wstring* m_prev;
};

// ============================================================================
// Verbatim-source callback — exact source text for opaque statements.
//
// Root-level UnknownStmt nodes (SQL*Plus commands, CREATE TABLE/VIEW/JAVA
// SOURCE, and any other statement the parser has no structural knowledge of)
// are emitted by emitVerbatimFragment(). Its token-reconstruction fallback
// cannot round-trip such source faithfully: it enforces a minimum one-space
// gap between adjacent tokens (System.out → "System . out") and re-anchors
// every continuation line to the fragment's first-line indent, which corrupts
// layout-sensitive embedded languages (Java source above all).
//
// When the caller owns the original text of the formatted selection it
// installs a VerbatimSourceFn through FormatterVerbatimSourceScope (RAII).
// emitVerbatimFragment() — only when its caller explicitly allows it
// (today: root-level UnknownStmt) — asks the callback for the exact slice
// spanning the fragment's [first .. last] token and, when the returned text
// passes the same whitespace-insensitive token-fidelity check the
// EMIT_RANGE_GUARD performs, writes it out as-is: first line at the current
// indent, every following line at its original column (pure-verbatim policy).
// When no scope is installed, or the callback yields empty/mismatching text
// (e.g. a multi-line token whose line/offset/length cannot be mapped back to
// the captured source), the emitter silently falls back to reconstruction.
//
// COEditorView::OnEditFormatSql() (OEView_7.cpp) is the only installer: it
// captures the exact text fed to PlSqlParserEx and maps Token::line /
// Token::offset / Token::length (which are relative to that fed text) back to
// the source slice. The callback takes token *identities*, not vector
// indices, so it stays valid for re-lexed sub-slices too.
//
// Only ever touched from the UI thread, so no synchronization is required.
// ============================================================================
using VerbatimSourceFn = std::function<std::wstring(const Token& beg, const Token& end)>;

extern const VerbatimSourceFn* g_pFormatterVerbatimSource;

// True while a FormatterVerbatimSourceScope is active.
inline bool formatterVerbatimSourceActive() { return g_pFormatterVerbatimSource != nullptr; }

class FormatterVerbatimSourceScope
{
public:
	explicit FormatterVerbatimSourceScope(const VerbatimSourceFn& fn);
	~FormatterVerbatimSourceScope();

	FormatterVerbatimSourceScope(const FormatterVerbatimSourceScope&) = delete;
	FormatterVerbatimSourceScope& operator=(const FormatterVerbatimSourceScope&) = delete;

private:
	const VerbatimSourceFn* m_prev;
	VerbatimSourceFn        m_fn; // owned copy; the global points at this
};

// ============================================================================
// Progress callback — long-running format operations.
//
// A format is a two-pass walk over the document (buildTree(), then the emit
// pass), and both passes advance root node by root node, which gives two
// natural checkpoint streams. COEditorView::OnEditFormatSql() installs a
// FormatterProgressScope whose callback does two things:
//
//   1. publishes a throttled status line — "parsing tree (line N of M)" then
//      "writing (statement N of M)" — through Global::SetStatusText(..., TRUE),
//      at most about once per second. The checkpoints themselves fire per root
//      node (thousands of times on a big script), and an unthrottled
//      SetWindowText + UpdateWindow pair would come to dominate the format.
//
//   2. enforces a wall-clock budget. Neither pass can be interrupted below a
//      root-node boundary, so a pathological document would otherwise run
//      unbounded with no way out. Once the budget (1 minute) elapses the user
//      is asked — CTaskDialog — to continue for 1 / 3 / 10 more minutes or to
//      cancel. Continuing re-arms the budget from *now*, so elapsing again asks
//      again. Cancelling throws a user exception (AfxThrowUserException), which
//      unwinds the format and is reported silently, like every other cancelled
//      operation in SQLTools.
//
// Stage semantics:
//   ParseTree — current/total are *source line* numbers: the line of the token
//       that opened the root node just appended, and the document's last line.
//       Monotonic, because buildTree() only ever scans forward.
//   Write — current/total are *root-level item* counters: the iteration of
//       emitStatementSequence()'s root invocation and the root child count.
//       The two are not in lockstep (a lone script '/' or a flat-token
//       fallback consumes an iteration without owning a child), so current is
//       clamped to total rather than allowed to run past it.
//
// Both are approximations of work done, which is all a progress line needs.
//
// The whole mechanism is skipped under automated testing: OnEditFormatSql()
// does not install the scope at all while formatterHeadless(), so the
// checkpoints cost their single null-pointer branch and nothing else. There is
// no user to read the status line, the prompt would hang the COM caller until
// its own 10 s proxy timeout, and the corpus stress harness formats thousands
// of files in one run — a status-bar SetWindowText + UpdateWindow pair per file
// is pure overhead inside a handler that is being timed. Suppression lives at
// the installation site rather than in the checkpoints, because it is a
// property of the run and not of any individual format.
//
// Only ever touched from the UI thread, so no synchronization is required.
// ============================================================================
enum class FormatStage { ParseTree, Write };

using ProgressFn = std::function<void(FormatStage stage, int current, int total)>;

extern const ProgressFn* g_pFormatterProgress;

// True while a FormatterProgressScope is active.
inline bool formatterProgressActive() { return g_pFormatterProgress != nullptr; }

// Reports one checkpoint. Inlined so the (overwhelmingly common) no-scope
// installed case costs a single predictable branch, not a call.
inline void reportFormatterProgress(FormatStage stage, int current, int total)
{
	if (g_pFormatterProgress != nullptr)
		(*g_pFormatterProgress)(stage, current, total);
}

class FormatterProgressScope
{
public:
	explicit FormatterProgressScope(const ProgressFn& fn);
	~FormatterProgressScope();

	FormatterProgressScope(const FormatterProgressScope&) = delete;
	FormatterProgressScope& operator=(const FormatterProgressScope&) = delete;

private:
	const ProgressFn* m_prev;
	ProgressFn        m_fn; // owned copy; the global points at this
};

// ParseTree checkpoints fire only for the document-level buildTree() that
// formatPlsql() runs, which raises this flag around that one call.
//
// buildTree() is also called by formatSQL()/SQLFormatter for every nested SQL
// slice the emitters delegate to — the re-parse duplication the "Phase A
// instrumentation" section below measures. Those calls happen *after* the
// document parse has already returned, so a re-entrancy depth counter cannot
// tell them apart from the outermost one; an explicit enable flag can.
//
// Without it a nested slice — two lines of a several-thousand-line document —
// reports its own tiny "line 2 of 2" in the middle of the write pass, and the
// status line jumps backwards from "writing (statement 412 of 500)" to
// "parsing tree (line 2 of 2)" once per delegated slice, which reads as the
// format restarting from scratch.
extern bool g_formatterParseProgress;

class FormatterParseProgressScope
{
public:
	FormatterParseProgressScope();
	~FormatterParseProgressScope();

	FormatterParseProgressScope(const FormatterParseProgressScope&) = delete;
	FormatterParseProgressScope& operator=(const FormatterParseProgressScope&) = delete;

private:
	bool m_prev;
};

// True while a user-initiated cancellation is unwinding through the emitters.
//
// The cancel path throws from *inside* buildTree()/emitStatementSequence(), at
// a root-node checkpoint, so the emission is deliberately incomplete. Debug
// builds install an EmitRangeGuard over every emitter's token range, and its
// destructor compares the text appended so far against the *whole* expected
// span — a cancelled format always fails that comparison. In the automated-
// testing path the guard reports through g_pFormatterDiagnostics instead of
// asserting, but an interactive format has no diagnostics scope, so it would
// pop the modal CRT assert dialog (one per live guard) on top of the very
// cancellation the user just asked for.
//
// FormatterProgressMonitor sets this immediately before AfxThrowUserException();
// ~FormatterProgressScope() clears it. Unwinding destroys the innermost frames
// first, so every guard destructor on the way out still sees it set, and it is
// cleared exactly once the format has fully unwound. Release builds compile the
// guards out entirely, so the flag is inert there.
extern bool g_formatterCancelling;

// ============================================================================
// Phase A instrumentation — duplicate-work counters (Debug only).
//
// formatPlsql() parses and measures the whole document, then delegates each
// nested DML slice to formatSQL(), whose SQLFormatter constructor parses and
// measures that slice *again* from a deep copy (see
// md/sql_plsql_formatter_integration_plan.md §0.2). The slices are disjoint, so
// this is a constant-factor (~2x) waste rather than an asymptotic one — which
// is exactly why it needs measuring before anyone spends risk budget removing
// it (phases C and D).
//
//   sqlFormatterCalls  formatSQL() delegations in one format operation.
//   reparsedTokens     tokens those calls handed to buildTree().
//   remeasuredTokens   tokens accumWidth() actually examined while measuring
//                      those slices. Deliberately NOT the slice size:
//                      calcWidths() invokes accumWidth() once per tree node, so
//                      a token inside a nested scope is visited both by the
//                      enclosing node's pass and by its own. The per-pass
//                      child-range skip shrinks each individual walk but not
//                      the sum, so this counter runs ABOVE reparsedTokens
//                      (measured ~1.28x of it on the PL/SQL corpus, ~1.48x on
//                      pure SQL). That spread is itself useful data — it is the
//                      nested-scope overhead of the width pass.
//
// g_plsqlPerf and resetPlsqlPerfCounters() are defined in
// OEPlsSqlFormatterSqlSide.cpp, next to the SQLFormatter constructor that increments
// them. g_accumWidthTokensScanned is defined in OEPlsSqlFormatterTree.cpp and
// counts *every* accumWidth() visit including the parent document pass;
// remeasuredTokens is the delta of it taken across each SQLFormatter's own
// calcWidths(), which is what attributes the work to the right slice.
//
// formatPlsql() (OEPlsSqlFormatterBlocks.cpp) is the only top-level entry point
// — COEditorView::OnEditFormatSql calls it and every formatSQL() call is nested
// inside it — so it resets on entry and TRACEs a summary on exit, scoping the
// counters to exactly one format operation.
//
// Decision gate: if the ratios stay a small fraction of the document size
// across the real corpus, close C and D as "not worth the risk" and go straight
// to E. If they approach 1.0 on large packages, proceed with C.
//
// Zero Release impact: no counters, no reset, no TRACE.
// ============================================================================
#ifdef _DEBUG
struct PlsqlPerfCounters
{
	unsigned long long sqlFormatterCalls;
	unsigned long long reparsedTokens;
	unsigned long long remeasuredTokens;
};

extern PlsqlPerfCounters  g_plsqlPerf;
extern unsigned long long g_accumWidthTokensScanned;

void resetPlsqlPerfCounters();
#endif


} // namespace OpenEditor
