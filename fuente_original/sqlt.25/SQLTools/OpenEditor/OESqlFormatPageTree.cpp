#include "stdafx.h"
#include "OpenEditor/OESqlFormatPageTree.h"
#include "OpenEditor/OEPropTreePageHelpers.h"

using namespace OEPropTreePage;

static BOOL s_bShortlineHeuristicsEdpanded = FALSE;
static BOOL s_bMiscellaneousEdpanded       = FALSE;
static BOOL s_bBrickListEdpanded           = FALSE;
static BOOL s_bPlSqlEdpanded               = TRUE;

COESqlFormatPageTree::COESqlFormatPageTree (SettingsManager& manager)
	: Common::CPropTreePage(_T("Editor::SQL Format"), COESqlFormatPageTree::IDD)
	//, m_recipient(nullptr)
	//, m_recipientCommand(0)
	, m_manager(manager)
	, m_pShortlineHeuristicsGroup(nullptr), m_pMiscellaneousGroup(nullptr), m_pBrickListGroup(nullptr), m_pPlSqlGroup(nullptr)
	, m_pSimpleStmt(nullptr)
	, m_pSelectList(nullptr)
	, m_pFromList(nullptr)
	, m_pGroupByList(nullptr)
	, m_pOrderByList(nullptr)
	, m_pSelectStmt(nullptr)
	, m_pBlock(nullptr)
	, m_pCaseWhen(nullptr)
	, m_pReturningMaxLen(nullptr)
	, m_pReturningMaxItems(nullptr)
	, m_pAlignAliases(nullptr)
	, m_pSetOpBlankLine(nullptr)
	, m_pAlignSetAssignments(nullptr)
	, m_pAlignCaseInSet(nullptr)
	, m_pLogErrorsMaxLen(nullptr)
	, m_pBrickListEnabled(nullptr)
	, m_pBrickListMaxLineLen(nullptr)
	, m_pBrickListMaxLineItems(nullptr)
	, m_pBrickListMinItems(nullptr)
	, m_pBrickListLinkedInsert(nullptr)
	, m_pBrickListLinkedSetOp(nullptr)
	, m_pPlsqlDeclBlankLineSeparator(nullptr)
	, m_pPlsqlBlankLineAroundFlowStmts(nullptr)
	, m_pPlsqlBlankLineAroundSubprograms(nullptr)
	, m_pPlsqlBlankLineAfterUnitHeader(nullptr)
	, m_pPlsqlDmlBlankLineSeparator(nullptr)
	, m_pPlsqlBlankLineBeforeComment(nullptr)
	, m_pPlsqlIsOnNewLine(nullptr)
	, m_pPlsqlExpressionMaxLen(nullptr)
	, m_pPlsqlParamListMaxLen(nullptr)
	, m_pPlsqlParamListAlignParen(nullptr)
	, m_pPlsqlConcatMaxLen(nullptr)
	, m_pPlsqlConcatKeepMixedPairTogether(nullptr)
	, m_pPlsqlConcatBreakBetweenLiterals(nullptr)
	, m_pPlsqlEndLineCommentKeepTrailing(nullptr)
	, m_pPlsqlEndLineCommentMaxCol(nullptr)
{
	// The page does have a help topic: Help/sqltools.hhp maps
	// hidd_oe_sql_format_tree to formatter.htm, and CPropTreePage enables
	// PSP_HASHELP for a non-zero help id.
}

COESqlFormatPageTree::~COESqlFormatPageTree ()
{
}

//void COESqlFormatPageTree::SetApplyRecipient (CWnd* recipient, UINT command)
//{
//	m_recipient = recipient;
//	m_recipientCommand = command;
//}

// Tree creation and layout, the PTN_* notifications, the resize handling, the
// commit-before-read on Apply and the expansion state on Cancel all come from
// Common::CPropTreePage - this file only holds the page content.

// The row builders (InsertGroup / InsertCheckedStatic / InsertCheckedEdit /
// InsertEdit / InsertNumberEdit / InsertComboText) are shared with the other
// CPropTree based settings pages: OpenEditor/OEPropTreePageHelpers.h

void COESqlFormatPageTree::BuildTree ()
{
	const OpenEditor::GlobalSettingsPtr s = m_manager.GetGlobalSettings();

	// ── Short-line heuristics ──────────────────────────────────────────────
	m_pShortlineHeuristicsGroup = m_tree.InsertItem(new CPropTreeItem());
	m_pShortlineHeuristicsGroup->SetLabelText(_T("Short-line heuristics"));
	m_pShortlineHeuristicsGroup->SetInfoText(_T("Controls when SQL clauses are kept on a single line. "
						   "Check to enable; the number is the maximum line length."));
	m_pShortlineHeuristicsGroup->Expand(s_bShortlineHeuristicsEdpanded);

	m_pSimpleStmt  = InsertCheckedEdit(m_tree, m_pShortlineHeuristicsGroup,
		_T("Simple statement"),
		_T("Keep complete short statements on one line, max length"),
		s->GetFmtSimpleStmtEnabled(),        s->GetFmtSimpleStmtMaxLen());

	m_pSelectList  = InsertCheckedEdit(m_tree, m_pShortlineHeuristicsGroup,
		_T("SELECT column list"),
		_T("Keep short SELECT column list on one line, max length"),
		s->GetFmtSelectShortListEnabled(),   s->GetFmtSelectShortListMaxLen());

	m_pFromList    = InsertCheckedEdit(m_tree, m_pShortlineHeuristicsGroup,
		_T("FROM table list"),
		_T("Keep short FROM table list on one line, max length"),
		s->GetFmtFromShortListEnabled(),     s->GetFmtFromShortListMaxLen());

	m_pGroupByList = InsertCheckedEdit(m_tree, m_pShortlineHeuristicsGroup,
		_T("GROUP BY list"),
		_T("Keep short GROUP BY list on one line, max length"),
		s->GetFmtGroupByShortListEnabled(),  s->GetFmtGroupByShortListMaxLen());

	m_pOrderByList = InsertCheckedEdit(m_tree, m_pShortlineHeuristicsGroup,
		_T("ORDER BY list"),
		_T("Keep short ORDER BY list on one line, max length"),
		s->GetFmtOrderByShortListEnabled(),  s->GetFmtOrderByShortListMaxLen());

	m_pSelectStmt  = InsertCheckedEdit(m_tree, m_pShortlineHeuristicsGroup,
		_T("SELECT subquery"),
		_T("Keep short SELECT subquery on one line, max length"),
		s->GetFmtSelectStmtEnabled(),        s->GetFmtSelectStmtMaxLen());

	m_pBlock       = InsertEdit(m_tree, m_pShortlineHeuristicsGroup,
		_T("Block max length"),
		_T("Max length for other parenthesised blocks"),
		s->GetFmtBlockShortMaxLen());

	m_pCaseWhen    = InsertEdit(m_tree, m_pShortlineHeuristicsGroup,
		_T("CASE WHEN max length"),
		_T("Keep short WHEN clause on one line (0 to disable)"),
		s->GetFmtCaseWhenShortMaxLen());

	m_pReturningMaxLen   = InsertEdit(m_tree, m_pShortlineHeuristicsGroup,
		_T("RETURNING max length"),
		_T("Maximum line length for a RETURNING clause kept on one line"),
		s->GetFmtReturningMaxLen());

	m_pReturningMaxItems = InsertEdit(m_tree, m_pShortlineHeuristicsGroup,
		_T("RETURNING max items"),
		_T("Maximum number of items in a RETURNING clause kept on one line"),
		s->GetFmtReturningMaxItems());

	// ── Miscellaneous
	m_pMiscellaneousGroup = m_tree.InsertItem(new CPropTreeItem());
	m_pMiscellaneousGroup->SetLabelText(_T("Miscellaneous"));
	m_pMiscellaneousGroup->SetInfoText(_T("Additional SQL formatting options"));
	m_pMiscellaneousGroup->Expand(s_bMiscellaneousEdpanded);

	m_pAlignAliases  = InsertCheckedStatic(m_tree, m_pMiscellaneousGroup,
		_T("Align column aliases"),
		_T("Align column aliases in SELECT lists"),
		s->GetFmtAlignColumnAliases());

	m_pSetOpBlankLine = InsertCheckedStatic(m_tree, m_pMiscellaneousGroup,
		_T("Blank line before set ops"),
		_T("Insert blank line before UNION / INTERSECT / MINUS"),
		s->GetFmtSetOpBlankLine());

	m_pAlignSetAssignments = InsertCheckedStatic(m_tree, m_pMiscellaneousGroup,
		_T("Align SET assignments"),
		_T("Align right-hand sides of assignments in SET clauses"),
		s->GetFmtAlignSetAssignments());

	m_pAlignCaseInSet = InsertCheckedStatic(m_tree, m_pMiscellaneousGroup,
		_T("Align CASE in SET"),
		_T("Align CASE expressions on the right-hand side of SET assignments"),
		s->GetFmtAlignCaseInSet());

	m_pLogErrorsMaxLen = InsertEdit(m_tree, m_pMiscellaneousGroup,
		_T("LOG ERRORS max length"),
		_T("Maximum line length for LOG ERRORS formatting (default 40)"),
		s->GetFmtLogErrorsMaxLen());

	// ── Brick list ────────────────────────────────────────────────────────
	m_pBrickListGroup = m_tree.InsertItem(new CPropTreeItem());
	m_pBrickListGroup->SetLabelText(_T("Brick list"));
	m_pBrickListGroup->SetInfoText(_T("Arranges long column lists in a compact rectangle (multiple items per line) "
						   "instead of one item per line. "
						   "Applies to SELECT column lists and INSERT column/VALUES lists."));
	m_pBrickListGroup->Expand(s_bBrickListEdpanded);

	m_pBrickListEnabled = InsertCheckedStatic(m_tree, m_pBrickListGroup,
		_T("Enabled"),
		_T("Enable brick-list layout for SELECT column lists and INSERT column/VALUES lists"),
		s->GetFmtBrickListEnabled());

	m_pBrickListMaxLineLen = InsertEdit(m_tree, m_pBrickListGroup,
		_T("Max line length"),
		_T("Maximum total line length before wrapping to the next brick row"),
		s->GetFmtBrickListMaxLineLen());

	m_pBrickListMaxLineItems = InsertEdit(m_tree, m_pBrickListGroup,
		_T("Max items per line"),
		_T("Maximum number of items placed on one brick row"),
		s->GetFmtBrickListMaxLineItems());

	m_pBrickListMinItems = InsertEdit(m_tree, m_pBrickListGroup,
		_T("Min items to activate"),
		_T("Minimum number of items in the list before brick layout is applied; shorter lists use normal one-per-line layout"),
		s->GetFmtBrickListMinItems());

	m_pBrickListLinkedInsert = InsertCheckedStatic(m_tree, m_pBrickListGroup,
		_T("Linked INSERT col / VALUES"),
		_T("Wrap the VALUES list at the same positions as the INSERT column list so both lists align vertically"),
		s->GetFmtBrickListLinkedInsert());

	m_pBrickListLinkedSetOp = InsertCheckedStatic(m_tree, m_pBrickListGroup,
		_T("Linked UNION / INTERSECT / MINUS"),
		_T("Wrap each SELECT column list in a compound query at the same positions as the first branch"),
		s->GetFmtBrickListLinkedSetOp());

	// ── PL/SQL formatter ────────────────────────────────────────────────
	m_pPlSqlGroup = m_tree.InsertItem(new CPropTreeItem());
	m_pPlSqlGroup->SetLabelText(_T("PL/SQL"));
	m_pPlSqlGroup->SetInfoText(_T("Formatting options specific to PL/SQL blocks, subprograms, and packages."));
	m_pPlSqlGroup->Expand(s_bPlSqlEdpanded);

	m_pPlsqlDeclBlankLineSeparator = InsertCheckedStatic(m_tree, m_pPlSqlGroup,
		_T("Blank lines around complex DECLARE items"),
		_T("Blank line beforeand after each cursor / type / nested-subprogram declaration inside a DECLARE section. Simple variables are not included."),
		s->GetFmtPlsqlDeclBlankLineSeparator());

	m_pPlsqlBlankLineAroundFlowStmts = InsertCheckedStatic(m_tree, m_pPlSqlGroup,
		_T("Blank line around flow-control statements"),
		_T("Blank line around IF / LOOP / CASE / nested block statements"),
		s->GetFmtPlsqlBlankLineAroundFlowStmts());

	m_pPlsqlBlankLineAroundSubprograms = InsertCheckedStatic(m_tree, m_pPlSqlGroup,
		_T("Blank line around subprograms"),
		_T("Blank line around consecutive subprogram bodies inside a package body / type body"),
		s->GetFmtPlsqlBlankLineAroundSubprograms());

	m_pPlsqlBlankLineAfterUnitHeader = InsertCheckedStatic(m_tree, m_pPlSqlGroup,
		_T("Blank line after unit header"),
		_T("Blank line after a package/type spec or body header line, and before its closing END"),
		s->GetFmtPlsqlBlankLineAfterUnitHeader());

	m_pPlsqlDmlBlankLineSeparator = InsertCheckedStatic(m_tree, m_pPlSqlGroup,
		_T("Blank line around DML statements"),
		_T("Blank line around consecutive SQL statements (or a SQL statement and the next PL/SQL statement) at the same block nesting level"),
		s->GetFmtPlsqlDmlBlankLineSeparator());

	m_pPlsqlBlankLineBeforeComment = InsertCheckedStatic(m_tree, m_pPlSqlGroup,
		_T("Blank line before comment"),
		_T("Blank line inserted before a comment, subject to the general comment/blank-line policy"),
		s->GetFmtPlsqlBlankLineBeforeComment());

	m_pPlsqlIsOnNewLine = InsertCheckedStatic(m_tree, m_pPlSqlGroup,
		_T("IS/AS on new line"),
		_T("When checked, IS/AS is broken onto its own line instead of staying on the same line as the header"),
		s->GetFmtPlsqlIsOnNewLine());

	m_pPlsqlExpressionMaxLen = InsertEdit(m_tree, m_pPlSqlGroup,
		_T("Expression max length"),
		_T("Max line width for a math or boolean expression before wrapping (0 disables wrapping)"),
		s->GetFmtPlsqlExpressionMaxLen());

	m_pPlsqlParamListMaxLen = InsertEdit(m_tree, m_pPlSqlGroup,
		_T("Param list max length"),
		_T("Max line width for a call/header parameter list before wrapping (0 disables wrapping)"),
		s->GetFmtPlsqlParamListMaxLen());

	m_pPlsqlParamListAlignParen = InsertCheckedStatic(m_tree, m_pPlSqlGroup,
		_T("Align param list to paren"),
		_T("Wrapped parameter-list / concatenation continuation lines align under the column following the opening '(' instead of one block-indent level"),
		s->GetFmtPlsqlParamListAlignParen());

	m_pPlsqlConcatMaxLen = InsertEdit(m_tree, m_pPlSqlGroup,
		_T("Concatenation max length"),
		_T("Max content width for a || concatenation chain before wrapping (0 disables wrapping)"),
		s->GetFmtPlsqlConcatMaxLen());

	m_pPlsqlConcatKeepMixedPairTogether = InsertCheckedStatic(m_tree, m_pPlSqlGroup,
		_T("Keep mixed concat pair together"),
		_T("Never break at a || that crosses a literal-variable boundary when wrapping a concatenation chain"),
		s->GetFmtPlsqlConcatKeepMixedPairTogether());

	m_pPlsqlConcatBreakBetweenLiterals = InsertCheckedStatic(m_tree, m_pPlSqlGroup,
		_T("Break between literal concat pairs"),
		_T("Always break a literal || literal transition onto a new continuation line regardless of width"),
		s->GetFmtPlsqlConcatBreakBetweenLiterals());

	m_pPlsqlEndLineCommentKeepTrailing = InsertCheckedStatic(m_tree, m_pPlSqlGroup,
		_T("Keep end-of-line comments on their code line"),
		_T("A -- comment that annotates a line of code stays on that line (v_x NUMBER; -- counter) instead of being moved onto its own line"),
		s->GetFmtPlsqlEndLineCommentKeepTrailing());

	m_pPlsqlEndLineCommentMaxCol = InsertEdit(m_tree, m_pPlSqlGroup,
		_T("End-of-line comment max column"),
		_T("Widest line that may still host a rejoined -- comment; longer lines demote the comment onto its own line. Zero or negative disables the width check"),
		s->GetFmtPlsqlEndLineCommentMaxCol());
}

// Called from Common::CPropTreePage::OnApply(), which has already committed the
// cell being edited and which reports exceptions.
BOOL COESqlFormatPageTree::ApplyValues ()
{
	if (!m_pSimpleStmt)
		return TRUE; // the page was never built

	OpenEditor::GlobalSettingsPtr s = m_manager.GetGlobalSettings();

	int v = 0;

	m_pSimpleStmt->GetItemValue(v);
	s->SetFmtSimpleStmtEnabled      (m_pSimpleStmt->IsChecked()   ? true : false, false);
	s->SetFmtSimpleStmtMaxLen       (v,                                            false);

	m_pSelectList->GetItemValue(v);
	s->SetFmtSelectShortListEnabled (m_pSelectList->IsChecked()   ? true : false, false);
	s->SetFmtSelectShortListMaxLen  (v,                                            false);

	m_pFromList->GetItemValue(v);
	s->SetFmtFromShortListEnabled   (m_pFromList->IsChecked()     ? true : false, false);
	s->SetFmtFromShortListMaxLen    (v,                                            false);

	m_pGroupByList->GetItemValue(v);
	s->SetFmtGroupByShortListEnabled(m_pGroupByList->IsChecked()  ? true : false, false);
	s->SetFmtGroupByShortListMaxLen (v,                                            false);

	m_pOrderByList->GetItemValue(v);
	s->SetFmtOrderByShortListEnabled(m_pOrderByList->IsChecked()  ? true : false, false);
	s->SetFmtOrderByShortListMaxLen (v,                                            false);

	m_pSelectStmt->GetItemValue(v);
	s->SetFmtSelectStmtEnabled      (m_pSelectStmt->IsChecked()   ? true : false, false);
	s->SetFmtSelectStmtMaxLen       (v,                                            false);

	m_pBlock->GetItemValue(v);
	s->SetFmtBlockShortMaxLen       (v,                                            false);

	m_pCaseWhen->GetItemValue(v);
	s->SetFmtCaseWhenShortMaxLen    (v,                                            false);

	m_pReturningMaxLen->GetItemValue(v);
	s->SetFmtReturningMaxLen        (v,                                            false);

	m_pReturningMaxItems->GetItemValue(v);
	s->SetFmtReturningMaxItems      (v,                                            false);

	s->SetFmtAlignColumnAliases     (m_pAlignAliases->IsChecked()        ? true : false, false);
	s->SetFmtSetOpBlankLine         (m_pSetOpBlankLine->IsChecked()      ? true : false, false);
	s->SetFmtAlignSetAssignments    (m_pAlignSetAssignments->IsChecked() ? true : false, false);
	s->SetFmtAlignCaseInSet         (m_pAlignCaseInSet->IsChecked()      ? true : false, false);

	m_pLogErrorsMaxLen->GetItemValue(v);
	s->SetFmtLogErrorsMaxLen        (v,                                                  false);

	s->SetFmtBrickListEnabled       (m_pBrickListEnabled->IsChecked()    ? true : false, false);

	m_pBrickListMaxLineLen->GetItemValue(v);
	s->SetFmtBrickListMaxLineLen    (v,                                            false);

	m_pBrickListMaxLineItems->GetItemValue(v);
	s->SetFmtBrickListMaxLineItems  (v,                                            false);

	m_pBrickListMinItems->GetItemValue(v);
	s->SetFmtBrickListMinItems      (v,                                            false);

	s->SetFmtBrickListLinkedInsert  (m_pBrickListLinkedInsert->IsChecked() ? true : false, false);
	s->SetFmtBrickListLinkedSetOp   (m_pBrickListLinkedSetOp->IsChecked()  ? true : false, false);

	s->SetFmtPlsqlDeclBlankLineSeparator     (m_pPlsqlDeclBlankLineSeparator->IsChecked()     ? true : false, false);
	s->SetFmtPlsqlBlankLineAroundFlowStmts   (m_pPlsqlBlankLineAroundFlowStmts->IsChecked()   ? true : false, false);
	s->SetFmtPlsqlBlankLineAroundSubprograms (m_pPlsqlBlankLineAroundSubprograms->IsChecked() ? true : false, false);
	s->SetFmtPlsqlBlankLineAfterUnitHeader   (m_pPlsqlBlankLineAfterUnitHeader->IsChecked()   ? true : false, false);
	s->SetFmtPlsqlDmlBlankLineSeparator      (m_pPlsqlDmlBlankLineSeparator->IsChecked()      ? true : false, false);
	s->SetFmtPlsqlBlankLineBeforeComment     (m_pPlsqlBlankLineBeforeComment->IsChecked()     ? true : false, false);
	s->SetFmtPlsqlIsOnNewLine                (m_pPlsqlIsOnNewLine->IsChecked()                ? true : false, false);

	m_pPlsqlExpressionMaxLen->GetItemValue(v);
	s->SetFmtPlsqlExpressionMaxLen           (v,                                                              false);

	m_pPlsqlParamListMaxLen->GetItemValue(v);
	s->SetFmtPlsqlParamListMaxLen            (v,                                                              false);

	s->SetFmtPlsqlParamListAlignParen        (m_pPlsqlParamListAlignParen->IsChecked()        ? true : false, false);

	m_pPlsqlConcatMaxLen->GetItemValue(v);
	s->SetFmtPlsqlConcatMaxLen                (v,                                                              false);

	s->SetFmtPlsqlConcatKeepMixedPairTogether(m_pPlsqlConcatKeepMixedPairTogether->IsChecked() ? true : false, false);
	s->SetFmtPlsqlConcatBreakBetweenLiterals (m_pPlsqlConcatBreakBetweenLiterals->IsChecked()  ? true : false, false);

	s->SetFmtPlsqlEndLineCommentKeepTrailing (m_pPlsqlEndLineCommentKeepTrailing->IsChecked() ? true : false, false);

	m_pPlsqlEndLineCommentMaxCol->GetItemValue(v);
	s->SetFmtPlsqlEndLineCommentMaxCol       (v,                                                              false);

//if (m_recipient)
//	m_recipient->OnCmdMsg(m_recipientCommand, CN_COMMAND, NULL, NULL);

	return TRUE;
}

// Called from Common::CPropTreePage::OnApply() and from ::OnCancel(), so the
// groups stay expanded the way the user left them - also when the sheet is
// cancelled.
void COESqlFormatPageTree::SaveTreeState ()
{
	// The page may never have been shown, hence the null check.
	if (!m_pShortlineHeuristicsGroup)
		return;

	s_bShortlineHeuristicsEdpanded = m_pShortlineHeuristicsGroup->IsExpanded();
	s_bMiscellaneousEdpanded       = m_pMiscellaneousGroup->IsExpanded();
	s_bBrickListEdpanded           = m_pBrickListGroup->IsExpanded();
	s_bPlSqlEdpanded               = m_pPlSqlGroup->IsExpanded();
}
