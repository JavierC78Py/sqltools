/*
	Copyright (C) 2002 Aleksey Kochetov

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

// 2026-09-25 replaces COEFileManagerPage, see the header

#include "stdafx.h"
#include "OpenEditor/OEFileManagerPageTree.h"
#include "Common/PropTreePageHelpers.h"

	using namespace Common::PropTreeRows;

// Group expansion state, kept across dialog re-openings (the pattern used by
// COEFilesWorkspacePageTree): written by SaveTreeState(), read by BuildTree().
static BOOL s_bExplorerExpanded = TRUE;
static BOOL s_bHistoryExpanded  = TRUE;

// The bounds of the six edit-plus-spin pairs of the classic page, each of which
// had them twice - a DDV_MinMaxUInt for what is accepted and a UDM_SETRANGE32 on
// the _SPIN buddy for what the spin offers - over the same numbers. Five of the
// six become an InsertNumberEdit() row, which applies the pair in one call; the
// sixth, the preview count, is a check box plus number row and applies them
// itself (see BuildTree). Either way the range is BOTH the spin range and the
// value limit (SetItemLimit), so typed text is clamped on commit the way
// DDV_MinMaxUInt clamped it on exchange.
//
// The preview count, CFileExplorerWnd2::make_tooltip_text()'s limit on the ascii
// preview it appends to a tooltip.
static const int s_previewLinesMin = 1;
static const int s_previewLinesMax = 20;

// How much the persistent history stores: RecentFileList::Load() reads
// GetHistoryFiles() and GetHistoryWorkspaces() as its nMaxFiles and
// nMaxWorkspaces, and drops the oldest entries past them.
static const int s_historyFilesMin      = 100;
static const int s_historyFilesMax      = 5000;
static const int s_historyWorkspacesMin = 20;
static const int s_historyWorkspacesMax = 1000;

// How much of that the three menus show, read as the display size by
// RecentFileList::UpdateFileMenu(), ::UpdateWorkspaceMenu() and the Quick-Save
// snapshot menu of SQLTools.cpp. 0 leaves the menu empty.
static const int s_recentMenuMin = 0;
static const int s_recentMenuMax = 16;

// The spin step of the two history SIZE rows. The classic spin incremented by 1
// over a 4900 wide (and a 980 wide) range, which made it all but unusable; the
// step is a UI affordance with no effect on what is stored, and taking the lower
// bound for it keeps every value the spin reaches a round one. The three menu
// sizes keep a step of 1 - their range is narrow enough for it, and 1 is what the
// classic spin did.
static const int s_historyFilesStep      = s_historyFilesMin;
static const int s_historyWorkspacesStep = s_historyWorkspacesMin;
static const int s_smallStep             = 1;

COEFileManagerPageTree::COEFileManagerPageTree (SettingsManager& manager)
	// The title is what CTreePropSheet splits on "::" to hang the page under the
	// "Editor" node of the sheet tree. It is the classic page's own caption,
	// unchanged: this is a one to one conversion and not a merge, so nothing here
	// needs a new name to cover rows from elsewhere.
	: Common::CPropTreePage(_T("Editor::File Manager"), COEFileManagerPageTree::IDD)
	, m_manager(manager)
	, m_pExplorerGroup(NULL)
	, m_pHistoryGroup(NULL)
	, m_pTooltips(NULL)
	, m_pPreviewInTooltips(NULL)
	, m_pTortoiseGit(NULL)
	, m_pTortoiseSvn(NULL)
	, m_pFileProperties(NULL)
	, m_pRestoreEditorState(NULL)
	, m_pHistoryFiles(NULL)
	, m_pHistoryWorkspaces(NULL)
	, m_pRecentFilesInMenu(NULL)
	, m_pRecentWorkspacesInMenu(NULL)
	, m_pRecentQSWorkspacesInMenu(NULL)
	, m_pOnlyFileNameInMenu(NULL)
	, m_pOnlyWorkspaceNameInMenu(NULL)
{
}

COEFileManagerPageTree::~COEFileManagerPageTree ()
{
}

BEGIN_MESSAGE_MAP (COEFileManagerPageTree, Common::CPropTreePage)
END_MESSAGE_MAP ()

int COEFileManagerPageTree::ClampPreviewLines (int value)
{
	if (value < s_previewLinesMin)
		return s_previewLinesMin;

	if (value > s_previewLinesMax)
		return s_previewLinesMax;

	return value;
}

// Called from Common::CPropTreePage::OnInitDialog() once the tree exists.
void COEFileManagerPageTree::BuildTree ()
{
	// The classic page cleared PSP_HASHELP in its constructor and Help/sqltools.hhp
	// has no [ALIAS] entry for it, so the sheet's Help button goes off while this
	// page is active. Without it, Help would report "topic not found" for the id
	// assigned above.
	SetPageHelp(FALSE);

	const OpenEditor::GlobalSettingsPtr settings = m_manager.GetGlobalSettings();

	// A root level item is drawn as a group HEADER - shaded, bold, full width, and
	// with the attribute cell skipped under "if (!IsRootLevel()) DrawAttribute(...)"
	// in CPropTreeItem::DrawItem - so no row here is a root level one: every row
	// lives in a group, or it would paint its label and no value cell at all.

	////////////////////////////////////////////////////////////////////////////
	// Explorer: the CFileExplorerWnd panel. The classic group box was
	// "  Explorer  "; the rows keep its order, which is also the order of
	// DoDataExchange() and of OnApply().
	m_pExplorerGroup = InsertGroup(m_tree, _T("Explorer"),
		_T("The file explorer panel: what its tooltips say about a directory or a file, and which extra entries appear in a file's context menu."),
		s_bExplorerExpanded);

	// CFileExplorerWnd::SetTooltips(); CFileExplorerWnd2::make_tooltip_text()
	// builds the text and returns FALSE when tooltips are off.
	m_pTooltips = InsertCheckedStatic(m_tree, m_pExplorerGroup,
		_T("Provide directory/file information in tooltips"),
		_T("Provide directory and file information in the explorer's tooltips. Turning this off also turns off the preview row below it: with no tooltip there is nothing to show a preview in."),
		settings->GetFileManagerTooltips());

	// CFileExplorerWnd::SetPreviewInTooltips() and ::SetPreviewLines(), the third
	// and fourth arguments of make_tooltip_text().
	//
	// The classic template had FOUR controls for this on ONE line - the "Enable
	// ascii file &preview in tooltips" check box at y=28, the
	// IDC_OE_FLMGR_PREVIEWLINES edit at y=27, its spin and a "line(s)" static -
	// and enabled the edit from the two check boxes above it
	// (m_FileManagerTooltips && m_FileManagerPreviewInTooltips). One row does the
	// same job: its check box is the switch and its number cell is the value, so
	// the second half of that condition is the row's own check box and there is
	// nothing left to enable for it. What remains is the dependency on the
	// tooltips row, which SetIndent() marks and UpdateEnabledState() grays out -
	// ReadOnly() is what stops the cell being edited, because CPropTreeList tests
	// !IsReadOnly() before it activates one (for a PTN_PROPCLICK and for the
	// keyboard alike), the tree equivalent of EnableWindow(FALSE) on the classic
	// edit box and its spin buddy. The unit moved into the label, because a row
	// has nowhere to put a trailing static.
	//
	// InsertCheckedEdit() does not set a range - it is the plain check box plus
	// number row - so the classic DDV_MinMaxUInt(1, 20) and the matching
	// UDM_SETRANGE32 on IDC_OE_FLMGR_PREVIEWLINES_SPIN are applied here, exactly
	// as COEFilesWorkspacePageTree applies them to its memory mapped threshold.
	// SetItemLimit() clamps what the user TYPES, and BuildTree() clamped what was
	// loaded, so no out-of-bounds value can reach the settings.
	m_pPreviewInTooltips = InsertCheckedEdit(m_tree, m_pExplorerGroup,
		_T("Ascii file preview in tooltips, line(s)"),
		_T("Enable an ascii file preview in the explorer's tooltips - the first lines of a text file are appended to its tooltip - and say how many lines it shows, 1 to 20 as the classic page's spin allowed. Unavailable while tooltips are off, because there would be nothing to show the preview in."),
		settings->GetFileManagerPreviewInTooltips(),
		ClampPreviewLines(settings->GetFileManagerPreviewLines()));
	m_pPreviewInTooltips->SetSpinRange(s_previewLinesMin, s_previewLinesMax);
	m_pPreviewInTooltips->SetItemLimit(s_previewLinesMin, s_previewLinesMax);
	m_pPreviewInTooltips->SetIndent(CPropTreeItem::GetLevelIndent()); // dependent row

	// The three shell context menu switches. They are NOT dependent on anything:
	// the classic template left them always enabled and DoDataExchange() had no
	// EnableWindow() for them (only the two commented out lines for the abandoned
	// IDC_OE_FLMGR_SHELLCONTEXMENU / _SHELLCONTEXMENUFILTER pair, which had no
	// setting behind them any more and so get no row here either).
	//
	// SQLTools_4.cpp pushes all three to CFileExplorerWnd, CMainFrame and
	// COpenFilesWnd, and COEView reads them straight from the settings, so they
	// reach every context menu CustomShellContextMenu builds - not just the
	// explorer's, which is why the labels say "file context menu" and not
	// "explorer context menu".
	m_pTortoiseGit = InsertCheckedStatic(m_tree, m_pExplorerGroup,
		_T("Tortoise GIT in the file context menu"),
		_T("Add the Tortoise GIT context menu(s) to the file context menu: the entries TortoiseGIT registers for a file are copied into the menu SQLTools shows."),
		settings->GetFileManagerShellContexMenuTortoiseGIT());

	m_pTortoiseSvn = InsertCheckedStatic(m_tree, m_pExplorerGroup,
		_T("Tortoise SVN in the file context menu"),
		_T("Add the Tortoise SVN context menu(s) to the file context menu: the entries TortoiseSVN registers for a file are copied into the menu SQLTools shows."),
		settings->GetFileManagerShellContexMenuTortoiseSVN());

	m_pFileProperties = InsertCheckedStatic(m_tree, m_pExplorerGroup,
		_T("Windows file property in the context menu"),
		_T("Add the Windows file property sheet to the file context menu: the \"Properties\" entry Windows shows for a file."),
		settings->GetFileManagerShellContexMenuProperties());

	////////////////////////////////////////////////////////////////////////////
	// History: the recent file and workspace lists. The classic group box was
	// "  History  "; the rows keep its order, which is also the order of
	// DoDataExchange() and of OnApply(). Note this is RecentFileList's history of
	// FILES and WORKSPACES - not the worksheet's history of executed statements,
	// which is the "Execution history" group of the "SQLTools::Session and
	// Execution" page.
	m_pHistoryGroup = InsertGroup(m_tree, _T("History"),
		_T("The recent file and workspace lists: how much they store, how much of it the menus show, and what reopening an entry restores."),
		s_bHistoryExpanded);

	// RecentFileList::OpenFile() reads this before it restores an entry's saved
	// codepage, bookmarks, cursor and scrolling position.
	m_pRestoreEditorState = InsertCheckedStatic(m_tree, m_pHistoryGroup,
		_T("Keep cursor, scroll position and bookmarks"),
		_T("Keep the editor cursor position, the scrolling position and the bookmarks in the history, so that reopening a file from the recent list restores where you left it. The classic label was \"Keep editor cursor / scrolling position / bookmarks in the history\"."),
		settings->GetHistoryRestoreEditorState());

	// The two sizes of the persistent list, read by RecentFileList::Load() as
	// nMaxFiles and nMaxWorkspaces; the oldest entries past them are dropped.
	m_pHistoryFiles = InsertNumberEdit(m_tree, m_pHistoryGroup,
		_T("Number of files stored in the history"),
		_T("How many files the history list stores, 100 to 5000 as the classic page's spin allowed. The oldest entries are dropped first."),
		settings->GetHistoryFiles(), s_historyFilesStep, s_historyFilesMin, s_historyFilesMax);

	m_pHistoryWorkspaces = InsertNumberEdit(m_tree, m_pHistoryGroup,
		_T("Number of workspaces stored in the history"),
		_T("How many workspaces the history list stores, 20 to 1000 as the classic page's spin allowed. The oldest entries are dropped first."),
		settings->GetHistoryWorkspaces(), s_historyWorkspacesStep, s_historyWorkspacesMin, s_historyWorkspacesMax);

	// The three menu sizes, read by RecentFileList::UpdateFileMenu(),
	// ::UpdateWorkspaceMenu() and the Quick-Save snapshot menu of SQLTools.cpp.
	// Each is a plain prefix of its list - the menus are sorted most recent
	// first - so 0 shows no entries at all.
	m_pRecentFilesInMenu = InsertNumberEdit(m_tree, m_pHistoryGroup,
		_T("Recent files in menu"),
		_T("How many of the most recently used files the Recent Files menu lists, 0 to 16 as the classic page's spin allowed. 0 leaves the menu empty."),
		settings->GetHistoryFilesInRecentMenu(), s_smallStep, s_recentMenuMin, s_recentMenuMax);

	m_pRecentWorkspacesInMenu = InsertNumberEdit(m_tree, m_pHistoryGroup,
		_T("Recent workspaces in menu"),
		_T("How many of the most recently used workspaces the Recent Workspaces menu lists, 0 to 16 as the classic page's spin allowed. 0 leaves the menu empty."),
		settings->GetHistoryWorkspacesInRecentMenu(), s_smallStep, s_recentMenuMin, s_recentMenuMax);

	m_pRecentQSWorkspacesInMenu = InsertNumberEdit(m_tree, m_pHistoryGroup,
		_T("Recent \"Quick-Save\" snapshots in menu"),
		_T("How many of the most recent \"Quick-Save\" snapshots the menu lists, 0 to 16 as the classic page's spin allowed. 0 leaves the menu empty."),
		settings->GetHistoryQSWorkspacesInRecentMenu(), s_smallStep, s_recentMenuMin, s_recentMenuMax);

	// Both feed m_nMaxDisplayLength of their menu handler: 0 while the box is
	// ticked, 64 otherwise. So ticking it shows the bare name and clearing it
	// shows the path, abbreviated to 64 characters. Neither is dependent on
	// anything - the classic page had no enable logic for them, and they keep
	// working with a menu size of 0 (the menu is then simply empty).
	m_pOnlyFileNameInMenu = InsertCheckedStatic(m_tree, m_pHistoryGroup,
		_T("Show only names in recent files menu"),
		_T("Show only the file name in the Recent Files menu. Untick it to show the path instead, abbreviated to 64 characters."),
		settings->GetHistoryOnlyFileNameInRecentMenu());

	m_pOnlyWorkspaceNameInMenu = InsertCheckedStatic(m_tree, m_pHistoryGroup,
		_T("Show only name in recent workspace menu"),
		_T("Show only the workspace name in the Recent Workspaces menu. Untick it to show the path instead, abbreviated to 64 characters."),
		settings->GetHistoryOnlyWorkspaceNameInRecentMenu());

	// The enable chain has to be applied HERE as well as on change. The classic
	// page drove it from DoDataExchange(), which runs on the initial UpdateData()
	// too - so a saved configuration with tooltips off came up with the preview
	// row already grayed. Wiring only OnItemChanged() would leave it live until
	// the user touched something.
	UpdateEnabledState();
}

// The enable chain of the classic COEFileManagerPage::DoDataExchange(), translated
// from EnableWindow() to the tree's ReadOnly() - which is what blocks the click
// (CPropTreeList tests !IsReadOnly() before it toggles a check box or activates a
// cell, for the mouse and for the keyboard alike) and what grays the row out:
//
//   IDC_OE_FLMGR_PREVIEWINTOOLTIPS  EnableWindow(m_FileManagerTooltips)
//   IDC_OE_FLMGR_PREVIEWLINES       EnableWindow(m_FileManagerTooltips && m_FileManagerPreviewInTooltips)
//
// The second line has no counterpart of its own: that check box and that edit are
// ONE row here, and a row's own check box is its switch - so "&& preview is on"
// is what the row already means. What survives of the chain is therefore a single
// dependency on the tooltips row, the classic page's whole enable logic.
//
// ReadOnly() does not clear a check box and does not zero a number, which matches
// EnableWindow(): the classic page grayed the preview row while tooltips were off
// and still exchanged both of its values, so the setting survives a round trip
// through "tooltips off" untouched. That is why ApplyValues() writes the row
// unconditionally rather than skipping it while it is read only.
void COEFileManagerPageTree::UpdateEnabledState ()
{
	if (!m_pTooltips || !m_pPreviewInTooltips)
		return;

	m_pPreviewInTooltips->ReadOnly(!m_pTooltips->IsChecked());

	// The rows are painted by the child CPropTreeList window, so its children
	// have to be invalidated too - see Common::CPropTreePage::RedrawTree().
	RedrawTree();
}

void COEFileManagerPageTree::OnItemChanged (CPropTreeItem* pItem)
{
	// Only the tooltips row has a dependent row. The three shell context menu
	// switches and all eight rows of the History group feed nothing else on the
	// page - the classic DoDataExchange() had no EnableWindow() for any of them -
	// so they are deliberately absent here.
	if (pItem == m_pTooltips)
		UpdateEnabledState();
}

// Called from Common::CPropTreePage::OnApply(), which has already committed the cell
// being edited and which reports exceptions.
BOOL COEFileManagerPageTree::ApplyValues ()
{
	if (!m_pExplorerGroup)
		return TRUE; // the page was never built

	OpenEditor::GlobalSettingsPtr settings = m_manager.GetGlobalSettings();

	// The same setters, in the same order, with the same "do not notify" flag the
	// classic OnApply() used - nothing here is re-read by another setter, and
	// SQLTools_4.cpp pushes the values out to the windows once, from
	// OnSettingsChanged() after the sheet closed.
	settings->SetFileManagerTooltips         (m_pTooltips->IsChecked() ? true : false, false /*notify*/);

	// Both halves of the one row. Read the number through the clamping
	// GetItemValue() overload rather than straight out: SetItemLimit() clamps what
	// the user types and BuildTree() clamped what was loaded, so this is the last
	// line of defence for a value that arrived some other way - the job the classic
	// page's DDV_MinMaxUInt did, at the same distance from the setting. Written
	// even while the row is read only, exactly as the classic DDX_Text exchanged
	// the value of a disabled edit box.
	settings->SetFileManagerPreviewInTooltips(m_pPreviewInTooltips->IsChecked() ? true : false, false /*notify*/);

	int previewLines = 0;
	m_pPreviewInTooltips->GetItemValue(previewLines);
	settings->SetFileManagerPreviewLines     (ClampPreviewLines(previewLines), false /*notify*/);

	settings->SetFileManagerShellContexMenuProperties (m_pFileProperties->IsChecked() ? true : false, false /*notify*/);
	settings->SetFileManagerShellContexMenuTortoiseGIT(m_pTortoiseGit->IsChecked()    ? true : false, false /*notify*/);
	settings->SetFileManagerShellContexMenuTortoiseSVN(m_pTortoiseSvn->IsChecked()    ? true : false, false /*notify*/);

	settings->SetHistoryRestoreEditorState(m_pRestoreEditorState->IsChecked(), false /*notify*/);

	// The five InsertNumberEdit() rows. GetItemValue(int&) already clamps to the
	// item limit InsertNumberEdit() installed from the classic DDV_MinMaxUInt
	// bounds, so - unlike the preview count above, whose InsertCheckedEdit() row
	// had to be given its limit by hand - there is nothing left to clamp here. One
	// local is reused across them, the way PropServerScriptPageTree reads its
	// number rows.
	int nValue = 0;

	m_pHistoryFiles->GetItemValue(nValue);
	settings->SetHistoryFiles(nValue, false /*notify*/);

	m_pHistoryWorkspaces->GetItemValue(nValue);
	settings->SetHistoryWorkspaces(nValue, false /*notify*/);

	m_pRecentFilesInMenu->GetItemValue(nValue);
	settings->SetHistoryFilesInRecentMenu(nValue, false /*notify*/);

	m_pRecentWorkspacesInMenu->GetItemValue(nValue);
	settings->SetHistoryWorkspacesInRecentMenu(nValue, false /*notify*/);

	m_pRecentQSWorkspacesInMenu->GetItemValue(nValue);
	settings->SetHistoryQSWorkspacesInRecentMenu(nValue, false /*notify*/);

	settings->SetHistoryOnlyFileNameInRecentMenu     (m_pOnlyFileNameInMenu->IsChecked(),      false /*notify*/);
	settings->SetHistoryOnlyWorkspaceNameInRecentMenu(m_pOnlyWorkspaceNameInMenu->IsChecked(), false /*notify*/);

	return TRUE;
}

// Called from Common::CPropTreePage::OnApply() and from ::OnCancel(), so the groups
// stay expanded the way the user left them - also when the sheet is cancelled.
void COEFileManagerPageTree::SaveTreeState ()
{
	// The page may never have been shown, hence the null check.
	if (!m_pExplorerGroup || !m_pHistoryGroup)
		return;

	s_bExplorerExpanded = m_pExplorerGroup->IsExpanded();
	s_bHistoryExpanded  = m_pHistoryGroup->IsExpanded();
}
