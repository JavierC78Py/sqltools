#include "stdafx.h"
#include "OpenEditor/OEPlsSqlOutlinePageTree.h"

COEPlsSqlOutlinePageTree::COEPlsSqlOutlinePageTree (SettingsManager& manager)
	: Common::CPropTreePage(_T("Editor::Navigation and Outline"), COEPlsSqlOutlinePageTree::IDD)
	, m_manager(manager)
	, m_pKeywordNavigation(nullptr)
	, m_pFileMaxTokens(nullptr)
	, m_pFileMaxLines(nullptr)
	, m_pOutlineEnabled(nullptr)
	, m_pOutlineSelectEnabled(nullptr)
	, m_pOutlineSelectOnly(nullptr)
	, m_pOutlineMaxTopItems(nullptr)
	, m_pFoldingEnabled(nullptr)
	, m_pFoldingHighlightRegion(nullptr)
{
}

COEPlsSqlOutlinePageTree::~COEPlsSqlOutlinePageTree ()
{
}

// Tree creation and layout, the PTN_* notifications, the resize handling, the
// commit-before-read on Apply and the modified state all come from
// Common::CPropTreePage - this file only holds the page content.

void COEPlsSqlOutlinePageTree::BuildTree ()
{
	const OpenEditor::GlobalSettingsPtr s = m_manager.GetGlobalSettings();

	// ── Navigation ────────────────────────────────────────────────────────
	CPropTreeItem* pRoot1 = m_tree.InsertItem(new CPropTreeItem());
	pRoot1->SetLabelText(_T("Navigation"));
	pRoot1->SetInfoText(_T("PL/SQL keyword navigation and outline settings."));
	pRoot1->Expand();

	m_pKeywordNavigation = (CPropTreeItemStatic*)m_tree.InsertItem(new CPropTreeItemStatic(), pRoot1);
	m_pKeywordNavigation->SetLabelText(_T("Keyword navigation"));
	m_pKeywordNavigation->SetInfoText(
		_T("Enable navigation between SELECT/FROM/WHERE and IF/ELSE/END IF keywords. ")
		_T("Also required for the PL/SQL Outline pane."));
	m_pKeywordNavigation->HasCheckBox(TRUE);
	m_pKeywordNavigation->Check(s->GetPlSqlKeywordNavigation() ? TRUE : FALSE);

	// ── Outline ───────────────────────────────────────────────────────────
	CPropTreeItem* pRoot3 = m_tree.InsertItem(new CPropTreeItem());
	pRoot3->SetLabelText(_T("Outline"));
	pRoot3->SetInfoText(_T("Controls what appears in the PL/SQL Outline pane."));
	pRoot3->Expand();

	m_pOutlineEnabled = (CPropTreeItemStatic*)m_tree.InsertItem(new CPropTreeItemStatic(), pRoot3);
	m_pOutlineEnabled->SetLabelText(_T("Enabled"));
	m_pOutlineEnabled->SetInfoText(_T("Enable the PL/SQL Outline pane. Requires keyword navigation to be on."));
	m_pOutlineEnabled->HasCheckBox(TRUE);
	m_pOutlineEnabled->Check(s->GetPlSqlOutlineEnabled() ? TRUE : FALSE);

	m_pOutlineSelectEnabled = (CPropTreeItemStatic*)m_tree.InsertItem(new CPropTreeItemStatic(), pRoot3);
	m_pOutlineSelectEnabled->SetLabelText(_T("Show SELECT statements"));
	m_pOutlineSelectEnabled->SetInfoText(_T("Include top-level SELECT statements in the outline."));
	m_pOutlineSelectEnabled->HasCheckBox(TRUE);
	m_pOutlineSelectEnabled->Check(s->GetPlSqlOutlineSelectEnabled() ? TRUE : FALSE);

	m_pOutlineSelectOnly = (CPropTreeItemStatic*)m_tree.InsertItem(new CPropTreeItemStatic(), pRoot3);
	m_pOutlineSelectOnly->SetLabelText(_T("SELECT only (no FROM/WHERE)"));
	m_pOutlineSelectOnly->SetInfoText(
		_T("When enabled, only the SELECT leg label is shown; FROM and WHERE clause nodes are hidden."));
	m_pOutlineSelectOnly->HasCheckBox(TRUE);
	m_pOutlineSelectOnly->Check(s->GetPlSqlOutlineSelectOnly() ? TRUE : FALSE);

	m_pOutlineMaxTopItems = (CPropTreeItemEdit*)m_tree.InsertItem(new CPropTreeItemEdit(), pRoot3);
	m_pOutlineMaxTopItems->SetLabelText(_T("Max top-level items"));
	m_pOutlineMaxTopItems->SetInfoText(
		_T("Stop adding top-level outline items after this many have been collected (0 = unlimited, default = 1000)."));
	m_pOutlineMaxTopItems->SetValueFormat(CPropTreeItemEdit::ValueFormatInteger);
	m_pOutlineMaxTopItems->SetItemValue(s->GetPlSqlOutlineMaxTopItems());

	// ── Code folding ──────────────────────────────────────────────────────
	// The stage-7 folding feature (md/CodeFolding.md). PlSqlOutlineFoldingEnabled
	// is the same setting the gutter context menu's "Folding Enabled" item flips;
	// this group is what makes it - and the current-region tint switch - reachable
	// from the settings dialog (ToDoNotes CodeFolding item 3, manual row M16).
	CPropTreeItem* pRoot4 = m_tree.InsertItem(new CPropTreeItem());
	pRoot4->SetLabelText(_T("Code folding"));
	pRoot4->SetInfoText(_T("In-editor code folding driven by the PL/SQL outline."));
	pRoot4->Expand();

	m_pFoldingEnabled = (CPropTreeItemStatic*)m_tree.InsertItem(new CPropTreeItemStatic(), pRoot4);
	m_pFoldingEnabled->SetLabelText(_T("Enabled"));
	m_pFoldingEnabled->SetInfoText(
		_T("Show fold glyphs in the editor gutter and enable the Edit / Outlining commands. ")
		_T("Requires keyword navigation and the outline to be on."));
	m_pFoldingEnabled->HasCheckBox(TRUE);
	m_pFoldingEnabled->Check(s->GetPlSqlOutlineFoldingEnabled() ? TRUE : FALSE);

	m_pFoldingHighlightRegion = (CPropTreeItemStatic*)m_tree.InsertItem(new CPropTreeItemStatic(), pRoot4);
	m_pFoldingHighlightRegion->SetLabelText(_T("Highlight the current region"));
	m_pFoldingHighlightRegion->SetInfoText(
		_T("Tint the fold band of the innermost region spanning the caret. ")
		_T("Painting only - folding and its commands are unaffected."));
	m_pFoldingHighlightRegion->HasCheckBox(TRUE);
	m_pFoldingHighlightRegion->Check(s->GetPlSqlOutlineFoldingHighlightCurrentRegion() ? TRUE : FALSE);

	// ── Parser limits ─────────────────────────────────────────────────────
	CPropTreeItem* pRoot2 = m_tree.InsertItem(new CPropTreeItem());
	pRoot2->SetLabelText(_T("Parser limits"));
	pRoot2->SetInfoText(_T("Limits applied to background PL/SQL parsing to avoid slowdowns on very large files."));
	pRoot2->Expand();

	m_pFileMaxTokens = (CPropTreeItemEdit*)m_tree.InsertItem(new CPropTreeItemEdit(), pRoot2);
	m_pFileMaxTokens->SetLabelText(_T("Max tokens"));
	m_pFileMaxTokens->SetInfoText(_T("Stop parsing after this many tokens have been processed (0 = unlimited, default = 50000)."));
	m_pFileMaxTokens->SetValueFormat(CPropTreeItemEdit::ValueFormatInteger);
	m_pFileMaxTokens->SetItemValue(s->GetPlSqlFileMaxTokens());

	m_pFileMaxLines = (CPropTreeItemEdit*)m_tree.InsertItem(new CPropTreeItemEdit(), pRoot2);
	m_pFileMaxLines->SetLabelText(_T("Max lines"));
	m_pFileMaxLines->SetInfoText(_T("Stop parsing after this many lines have been read (0 = unlimited, default = 3000)."));
	m_pFileMaxLines->SetValueFormat(CPropTreeItemEdit::ValueFormatInteger);
	m_pFileMaxLines->SetItemValue(s->GetPlSqlFileMaxLines());
}

// Called from Common::CPropTreePage::OnApply(), which has already committed the
// cell being edited and which reports exceptions. This page does not persist
// its group expansion state - all groups are always expanded in BuildTree().
BOOL COEPlsSqlOutlinePageTree::ApplyValues ()
{
	if (!m_pKeywordNavigation)
		return TRUE; // the page was never built

	OpenEditor::GlobalSettingsPtr s = m_manager.GetGlobalSettings();

	s->SetPlSqlKeywordNavigation(m_pKeywordNavigation->IsChecked() ? true : false, false);

	int v = 0;

	m_pFileMaxTokens->GetItemValue(v);
	s->SetPlSqlFileMaxTokens(v, false);

	m_pFileMaxLines->GetItemValue(v);
	s->SetPlSqlFileMaxLines(v, false);

	s->SetPlSqlOutlineEnabled      (m_pOutlineEnabled->IsChecked()       ? true : false, false);
	s->SetPlSqlOutlineSelectEnabled(m_pOutlineSelectEnabled->IsChecked() ? true : false, false);
	s->SetPlSqlOutlineSelectOnly   (m_pOutlineSelectOnly->IsChecked()    ? true : false, false);

	m_pOutlineMaxTopItems->GetItemValue(v);
	s->SetPlSqlOutlineMaxTopItems(v, false);

	s->SetPlSqlOutlineFoldingEnabled               (m_pFoldingEnabled->IsChecked()         ? true : false, false);
	s->SetPlSqlOutlineFoldingHighlightCurrentRegion(m_pFoldingHighlightRegion->IsChecked() ? true : false, false);

	return TRUE;
}
