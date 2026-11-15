#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __OEPlsSqlOutlinePageTree_h__
#define __OEPlsSqlOutlinePageTree_h__

#include "resource.h"
#include "Common/PropTreePage.h"
#include "OpenEditor/OESettings.h"
#include "PropTree/PropTreeItemEdit.h"
#include "PropTree/PropTreeItemStatic.h"

	using OpenEditor::SettingsManager;

// The "Editor::Navigation and Outline" settings page. The content is a
// CPropTree built in BuildTree(); all the page plumbing - the shared empty
// dialog template, tree creation and layout, commit-before-read on Apply, the
// modified state - lives in Common::CPropTreePage. IDD below is the *help* id
// only: the template used is the shared IDD_PROPTREE_PAGE.
//
// CodeOutlineWnd::OnSettings() also shows this page on its own and then replaces
// the title with "Navigation and Outline" (no "Editor::" group).
class COEPlsSqlOutlinePageTree : public Common::CPropTreePage
{
public:
	COEPlsSqlOutlinePageTree (SettingsManager& manager);
	virtual ~COEPlsSqlOutlinePageTree ();

	enum { IDD = IDD_OE_PLSQL_OUTLINE_TREE };

protected:
	virtual void BuildTree ();
	virtual BOOL ApplyValues ();

private:
	SettingsManager& m_manager;

	// Navigation
	CPropTreeItemStatic* m_pKeywordNavigation;

	// Parser limits
	CPropTreeItemEdit*   m_pFileMaxTokens;
	CPropTreeItemEdit*   m_pFileMaxLines;

	// Outline
	CPropTreeItemStatic* m_pOutlineEnabled;
	CPropTreeItemStatic* m_pOutlineSelectEnabled;
	CPropTreeItemStatic* m_pOutlineSelectOnly;
	CPropTreeItemEdit*   m_pOutlineMaxTopItems;

	// Code folding
	CPropTreeItemStatic* m_pFoldingEnabled;
	CPropTreeItemStatic* m_pFoldingHighlightRegion;
};

#endif//__OEPlsSqlOutlinePageTree_h__
