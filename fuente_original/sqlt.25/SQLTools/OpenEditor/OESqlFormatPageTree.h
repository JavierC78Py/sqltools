#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __OESqlFormatPageTree_h__
#define __OESqlFormatPageTree_h__

#include "resource.h"
#include "Common/PropTreePage.h"
#include "OpenEditor/OESettings.h"
#include "PropTree/PropTreeItemEdit.h"
#include "PropTree/PropTreeItemStatic.h"

	using OpenEditor::SettingsManager;

// The "Editor::SQL Format" settings page. The content is a CPropTree built in
// BuildTree(); all the page plumbing - the shared empty dialog template, tree
// creation and layout, commit-before-read on Apply, the modified state - lives
// in Common::CPropTreePage. IDD below is the *help* id only: the template used
// is the shared IDD_PROPTREE_PAGE.
class COESqlFormatPageTree : public Common::CPropTreePage
{
public:
	COESqlFormatPageTree (SettingsManager& manager);
	virtual ~COESqlFormatPageTree ();

	enum { IDD = IDD_OE_SQL_FORMAT_TREE };

	//void SetApplyRecipient (CWnd* recipient, UINT command);

protected:
	virtual void BuildTree ();
	virtual BOOL ApplyValues ();
	virtual void SaveTreeState ();

private:
	//CWnd* m_recipient;
	//UINT m_recipientCommand;

	SettingsManager& m_manager;

	CPropTreeItem* m_pShortlineHeuristicsGroup, *m_pMiscellaneousGroup, *m_pBrickListGroup, *m_pPlSqlGroup;

	// Short-line heuristics
	CPropTreeItemEdit*   m_pSimpleStmt;
	CPropTreeItemEdit*   m_pSelectList;
	CPropTreeItemEdit*   m_pFromList;
	CPropTreeItemEdit*   m_pGroupByList;
	CPropTreeItemEdit*   m_pOrderByList;
	CPropTreeItemEdit*   m_pSelectStmt;
	CPropTreeItemEdit*   m_pBlock;
	CPropTreeItemEdit*   m_pCaseWhen;
	CPropTreeItemEdit*   m_pReturningMaxLen;
	CPropTreeItemEdit*   m_pReturningMaxItems;

	// Miscellaneous
	CPropTreeItemStatic* m_pAlignAliases;
	CPropTreeItemStatic* m_pSetOpBlankLine;
	CPropTreeItemStatic* m_pAlignSetAssignments;
	CPropTreeItemStatic* m_pAlignCaseInSet;
	CPropTreeItemEdit*   m_pLogErrorsMaxLen;

	// Brick list
	CPropTreeItemStatic* m_pBrickListEnabled;
	CPropTreeItemEdit*   m_pBrickListMaxLineLen;
	CPropTreeItemEdit*   m_pBrickListMaxLineItems;
	CPropTreeItemEdit*   m_pBrickListMinItems;
	CPropTreeItemStatic* m_pBrickListLinkedInsert;
	CPropTreeItemStatic* m_pBrickListLinkedSetOp;

	// PL/SQL formatter
	CPropTreeItemStatic* m_pPlsqlDeclBlankLineSeparator;
	CPropTreeItemStatic* m_pPlsqlBlankLineAroundFlowStmts;
	CPropTreeItemStatic* m_pPlsqlBlankLineAroundSubprograms;
	CPropTreeItemStatic* m_pPlsqlBlankLineAfterUnitHeader;
	CPropTreeItemStatic* m_pPlsqlDmlBlankLineSeparator;
	CPropTreeItemStatic* m_pPlsqlBlankLineBeforeComment;
	CPropTreeItemStatic* m_pPlsqlIsOnNewLine;
	CPropTreeItemEdit*   m_pPlsqlExpressionMaxLen;
	CPropTreeItemEdit*   m_pPlsqlParamListMaxLen;
	CPropTreeItemStatic* m_pPlsqlParamListAlignParen;
	CPropTreeItemEdit*   m_pPlsqlConcatMaxLen;
	CPropTreeItemStatic* m_pPlsqlConcatKeepMixedPairTogether;
	CPropTreeItemStatic* m_pPlsqlConcatBreakBetweenLiterals;
	CPropTreeItemStatic* m_pPlsqlEndLineCommentKeepTrailing;
	CPropTreeItemEdit*   m_pPlsqlEndLineCommentMaxCol;
};

#endif//__OESqlFormatPageTree_h__
