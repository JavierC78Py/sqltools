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

// 2026-09-24 replaces COEFilePage AND OEWorkspacePage, see the header

#include "stdafx.h"
#include "OpenEditor/OEFilesWorkspacePageTree.h"
#include "Common/PropTreePageHelpers.h"

	using namespace Common::PropTreeRows;

// Group expansion state, kept across dialog re-openings (the pattern used by
// COEGeneralPageTree): written by SaveTreeState(), read by BuildTree().
static BOOL s_bFilesExpanded           = TRUE;
static BOOL s_bChangeDetectionExpanded = TRUE;
static BOOL s_bWorkspaceExpanded       = TRUE;

// The bounds of the memory mapped threshold. The classic page had them twice -
// DDV_MinMaxUInt(pDX, m_MemMapThreshold, 1, 999999999) for what is accepted and
// SendDlgItemMessage(IDC_OE_FILE_SPIN1, UDM_SETRANGE32, 1, 999999999) for the
// spin - and they are the reason the value is clamped rather than merely
// formatted: SetItemLimit() only clamps what the user TYPES, so a threshold that
// arrived out of bounds from a hand edited configuration file would otherwise be
// written straight back out.
static const int s_memMapThresholdMin = 1;
static const int s_memMapThresholdMax = 999999999;

COEFilesWorkspacePageTree::COEFilesWorkspacePageTree (SettingsManager& manager)
	// The title is what CTreePropSheet splits on "::" to hang the page under the
	// "Editor" node of the sheet tree. The two classic pages were "Editor::Files"
	// and "Editor::Workspace"; the merged page names both halves, because neither
	// on its own covers the rows - someone looking for "relative path for files
	// stored in workspace" would not think to look under "Files".
	: Common::CPropTreePage(_T("Editor::Files and Workspace"), COEFilesWorkspacePageTree::IDD)
	, m_manager(manager)
	, m_pFilesGroup(NULL)
	, m_pChangeDetectionGroup(NULL)
	, m_pWorkspaceGroup(NULL)
	, m_pLocking(NULL)
	, m_pSaveOnSwitch(NULL)
	, m_pOverwriteReadonly(NULL)
	, m_pMemMapForBig(NULL)
	, m_pDetectFileChanges(NULL)
	, m_pReloadChangedFiles(NULL)
	, m_pAutoscrollAfterReload(NULL)
	, m_pWorkspaceDetectChangesOnOpen(NULL)
	, m_pWorkspaceSaveOnExit(NULL)
	, m_pWorkspaceRelativePath(NULL)
	, m_pWorkspaceNameInTitleBar(NULL)
	, m_pWorkspaceNameInStatusBar(NULL)
{
}

COEFilesWorkspacePageTree::~COEFilesWorkspacePageTree ()
{
}

BEGIN_MESSAGE_MAP (COEFilesWorkspacePageTree, Common::CPropTreePage)
END_MESSAGE_MAP ()

int COEFilesWorkspacePageTree::ClampMemMapThreshold (int value)
{
	if (value < s_memMapThresholdMin)
		return s_memMapThresholdMin;

	if (value > s_memMapThresholdMax)
		return s_memMapThresholdMax;

	return value;
}

// Called from Common::CPropTreePage::OnInitDialog() once the tree exists.
void COEFilesWorkspacePageTree::BuildTree ()
{
	// Neither classic page had a help topic - both cleared PSP_HASHELP - so the
	// sheet's Help button goes off while this page is active. Without it, Help
	// would report "topic not found" for the id assigned above.
	SetPageHelp(FALSE);

	const OpenEditor::GlobalSettingsPtr settings = m_manager.GetGlobalSettings();

	// A root level item is drawn as a group HEADER - shaded, bold, full width, and
	// with the attribute cell skipped under "if (!IsRootLevel()) DrawAttribute(...)"
	// in CPropTreeItem::DrawItem - so no row here is a root level one: every row
	// lives in a group, or it would paint its label and no value cell at all.

	////////////////////////////////////////////////////////////////////////////
	// Files: how an open file is held and written.
	m_pFilesGroup = InsertGroup(m_tree, _T("Files"),
		_T("How a file is held and written while it is open in the editor."),
		s_bFilesExpanded);

	m_pLocking = InsertCheckedStatic(m_tree, m_pFilesGroup,
		_T("Keep files locked while editing them"),
		_T("Keep files locked while editing them, so no other program - and no other SQLTools window - can change one until it is closed here. Locking also turns off change detection below: a locked file cannot change outside the editor."),
		settings->GetFileLocking());

	m_pSaveOnSwitch = InsertCheckedStatic(m_tree, m_pFilesGroup,
		_T("Save files when switching tasks"),
		_T("Save files when switching tasks: write an edited file out when the focus moves to another application."),
		settings->GetFileSaveOnSwith());

	m_pOverwriteReadonly = InsertCheckedStatic(m_tree, m_pFilesGroup,
		_T("Overwrite read-only files without confirmation"),
		_T("Overwrite read-only files without confirmation when saving."),
		settings->GetFileOverwriteReadonly());

	// The classic page had this as FOUR controls - the "Using memory mapped file
	// access for files bigger than:" check box, the IDC_OE_FILE_MEMMAP_THRESHOLD
	// edit, its spin and a "Mb" static - and enabled the edit from the check box
	// (GetDlgItem(...)->EnableWindow(m_MemMapForBig)). One row does the same job:
	// its check box is the switch and its number cell is the value, so there is
	// nothing left to enable. The unit moved into the label, as DDLPageTree does
	// for "Preload DB dictionary with (%)".
	m_pMemMapForBig = InsertCheckedEdit(m_tree, m_pFilesGroup,
		_T("Memory mapped access for files bigger than (Mb)"),
		_T("Using memory mapped file access for files bigger than this size, in megabytes. Untick to read every file into memory instead. The threshold is 1 to 999999999 Mb, as the classic page's spin allowed."),
		settings->GetFileMemMapForBig(),
		ClampMemMapThreshold(settings->GetFileMemMapThreshold()));
	m_pMemMapForBig->SetSpinRange(s_memMapThresholdMin, s_memMapThresholdMax);
	m_pMemMapForBig->SetItemLimit(s_memMapThresholdMin, s_memMapThresholdMax);

	////////////////////////////////////////////////////////////////////////////
	// Change detection: the three dependent rows of the classic Editor::Files
	// page plus the one row of the classic Editor::Workspace page that asked the
	// same question. This group is why the two pages are now one.
	m_pChangeDetectionGroup = InsertGroup(m_tree, _T("Change detection"),
		_T("Every switch that asks whether a file changed on disk behind the editor's back. The first three were on the classic Editor::Files page and the fourth on Editor::Workspace; they belong together because they are the same question."),
		s_bChangeDetectionExpanded);

	m_pDetectFileChanges = InsertCheckedStatic(m_tree, m_pChangeDetectionGroup,
		_T("Detect when file is changed outside the editor"),
		_T("Detect when a file is changed outside the editor. Unavailable while files are kept locked, because a locked file cannot change. Turning this off also turns off the two rows below it."),
		settings->GetFileDetectChanges());

	m_pReloadChangedFiles = InsertCheckedStatic(m_tree, m_pChangeDetectionGroup,
		_T("Auto-load changed files (if not modified here)"),
		_T("Auto-load changed files, if the file is not currently modified in the editor: a changed file is re-read without asking, but only when there are no unsaved edits to lose."),
		settings->GetFileReloadChanges());

	// Dependent row: it is only ever consulted once a reload actually happens, so
	// it qualifies "Auto-load changed files" above rather than standing beside it
	// as a peer. SetIndent() marks the dependency, UpdateEnabledState() grays the
	// row out; the stored value is left alone (see ApplyValues).
	m_pAutoscrollAfterReload = InsertCheckedStatic(m_tree, m_pChangeDetectionGroup,
		_T("Autoscroll after reload (if cursor is on EOF)"),
		_T("Autoscroll after reload, if the cursor position is on the end of the file: keep the last line in view when a file that has grown is re-read."),
		settings->GetFileAutoscrollAfterReload());
	m_pAutoscrollAfterReload->SetIndent(CPropTreeItem::GetLevelIndent()); // dependent row

	// NOT dependent on "Keep files locked" or on the row above, even though it
	// reads like a sibling of both: the classic Editor::Workspace page had no
	// enable logic at all, and tying this row to them would be a behaviour change
	// rather than a conversion.
	m_pWorkspaceDetectChangesOnOpen = InsertCheckedStatic(m_tree, m_pChangeDetectionGroup,
		_T("Detect if file changed when opened from workspace"),
		_T("Detect if the file changed after opening a modified version from a workspace. This was the Editor::Workspace page's only change-detection switch, and it sits here rather than under Workspace because it is the same question as the three above."),
		settings->GetWorkspaceDetectChangesOnOpen());

	////////////////////////////////////////////////////////////////////////////
	// Workspace: the four rows of the classic Editor::Workspace page that are
	// about a workspace itself rather than about change detection.
	m_pWorkspaceGroup = InsertGroup(m_tree, _T("Workspace"),
		_T("What a workspace saves, how it stores the paths of its files, and where its name is shown."),
		s_bWorkspaceExpanded);

	m_pWorkspaceSaveOnExit = InsertCheckedStatic(m_tree, m_pWorkspaceGroup,
		_T("Save files in the active workspace on exit"),
		_T("Automatically save the files in the active workspace on exit."),
		settings->GetWorkspaceFileSaveInActiveOnExit());

	m_pWorkspaceRelativePath = InsertCheckedStatic(m_tree, m_pWorkspaceGroup,
		_T("Use relative path for files stored in workspace"),
		_T("Use a relative path for the files stored in a workspace, so the workspace still finds them when it is moved, or opened from a different folder."),
		settings->GetWorkspaceUseRelativePath());

	m_pWorkspaceNameInTitleBar = InsertCheckedStatic(m_tree, m_pWorkspaceGroup,
		_T("Show workspace name in the application title bar"),
		_T("Show the name of the open workspace in the application title bar."),
		settings->GetWorkspaceShowNameInTitleBar());

	m_pWorkspaceNameInStatusBar = InsertCheckedStatic(m_tree, m_pWorkspaceGroup,
		_T("Show workspace name in the application status bar"),
		_T("Show the name of the open workspace in the application status bar."),
		settings->GetWorkspaceShowNameInStatusBar());

	// The enable chain has to be applied HERE as well as on change. The classic
	// page drove it from DoDataExchange(), which runs on the initial UpdateData()
	// too - so a saved configuration that had "Keep files locked" on came up with
	// the three change-detection rows already grayed. Wiring only OnItemChanged()
	// would leave them live until the user touched something.
	UpdateEnabledState();
}

// The enable chain of the classic COEFilePage::DoDataExchange(), translated from
// EnableWindow() to the tree's ReadOnly() - which is what blocks the click
// (CPropTreeList tests !IsReadOnly() before toggling a check box, for the mouse
// and for the space bar alike) and what grays the label:
//
//   IDC_OE_FILE_DETECT_CHANGES           EnableWindow(!m_Locking)
//   IDC_OE_FILE_RELOAD_CHANGED           EnableWindow(!m_Locking && m_DetectFileChanges)
//   IDC_OE_FILE_AUTOSCROLL_AFTER_RELOAD  EnableWindow(!m_Locking && m_DetectFileChanges && m_ReloadChangedFiles)
//
// The fourth line of the classic chain, IDC_OE_FILE_MEMMAP_THRESHOLD
// EnableWindow(m_MemMapForBig), has no counterpart: that check box and that edit
// are one row here, and a row's own check box is its switch.
//
// ReadOnly() does not clear a check box, which matches EnableWindow(): the
// classic page grayed "Detect when file is changed outside the editor" while
// files were locked and still wrote its stored value out on Apply, so the
// setting survives a round trip through locking untouched.
void COEFilesWorkspacePageTree::UpdateEnabledState ()
{
	if (!m_pDetectFileChanges)
		return;

	const BOOL bLocking = m_pLocking->IsChecked();
	const BOOL bDetect  = m_pDetectFileChanges->IsChecked();
	const BOOL bReload  = m_pReloadChangedFiles->IsChecked();

	m_pDetectFileChanges->ReadOnly(bLocking ? TRUE : FALSE);
	m_pReloadChangedFiles->ReadOnly((bLocking || !bDetect) ? TRUE : FALSE);
	m_pAutoscrollAfterReload->ReadOnly((bLocking || !bDetect || !bReload) ? TRUE : FALSE);

	// The rows are painted by the child CPropTreeList window, so its children have to
	// be invalidated too - see Common::CPropTreePage::RedrawTree().
	RedrawTree();
}

void COEFilesWorkspacePageTree::OnItemChanged (CPropTreeItem* pItem)
{
	// Only the three rows the chain reads can change it. Everything else on the
	// page feeds nothing, and "Detect if file changed when opened from workspace"
	// is deliberately independent: the classic page gave it no enable logic, so
	// giving it one here would change behaviour rather than convert it.
	//
	// Note the chain now spans two groups - "Keep files locked" lives in Files and
	// grays three rows of Change detection out.
	if (pItem == m_pLocking || pItem == m_pDetectFileChanges || pItem == m_pReloadChangedFiles)
		UpdateEnabledState();
}

// Called from Common::CPropTreePage::OnApply(), which has already committed the cell
// being edited and which reports exceptions.
BOOL COEFilesWorkspacePageTree::ApplyValues ()
{
	if (!m_pFilesGroup)
		return TRUE; // the page was never built

	OpenEditor::GlobalSettingsPtr settings = m_manager.GetGlobalSettings();

	// The two classic pages wrote these from two separate OnApply() calls, the
	// Files ones first and the Workspace ones second. None of the setters notifies
	// and none reads another, so the order between them does not matter; they are
	// grouped here the way the tree groups them, which is the easier thing to
	// check line by line against BuildTree().
	settings->SetFileLocking(           m_pLocking->IsChecked()           ? true : false, false /*notify*/);
	settings->SetFileSaveOnSwith(       m_pSaveOnSwitch->IsChecked()      ? true : false, false /*notify*/);
	settings->SetFileOverwriteReadonly( m_pOverwriteReadonly->IsChecked() ? true : false, false /*notify*/);
	settings->SetFileMemMapForBig(      m_pMemMapForBig->IsChecked()      ? true : false, false /*notify*/);

	// Clamped rather than read straight out. SetItemLimit() clamps what the user
	// types and BuildTree() clamped what was loaded, so this is the last line of
	// defence for a value that arrived some other way - the job the classic page's
	// DDV_MinMaxUInt did, at the same distance from the setting.
	int threshold = 0;
	m_pMemMapForBig->GetItemValue(threshold);
	settings->SetFileMemMapThreshold(ClampMemMapThreshold(threshold), false /*notify*/);

	settings->SetFileDetectChanges(         m_pDetectFileChanges->IsChecked()     ? true : false, false /*notify*/);
	settings->SetFileReloadChanges(         m_pReloadChangedFiles->IsChecked()    ? true : false, false /*notify*/);
	settings->SetFileAutoscrollAfterReload( m_pAutoscrollAfterReload->IsChecked() ? true : false, false /*notify*/);

	settings->SetWorkspaceDetectChangesOnOpen   (m_pWorkspaceDetectChangesOnOpen->IsChecked(), false /*notify*/);
	settings->SetWorkspaceFileSaveInActiveOnExit(m_pWorkspaceSaveOnExit->IsChecked(),         false /*notify*/);
	settings->SetWorkspaceUseRelativePath       (m_pWorkspaceRelativePath->IsChecked(),      false /*notify*/);
	settings->SetWorkspaceShowNameInTitleBar    (m_pWorkspaceNameInTitleBar->IsChecked(),    false /*notify*/);
	settings->SetWorkspaceShowNameInStatusBar   (m_pWorkspaceNameInStatusBar->IsChecked(),   false /*notify*/);

	return TRUE;
}

// Called from Common::CPropTreePage::OnApply() and from ::OnCancel(), so the groups
// stay expanded the way the user left them - also when the sheet is cancelled.
void COEFilesWorkspacePageTree::SaveTreeState ()
{
	// The page may never have been shown, hence the null check.
	if (!m_pFilesGroup || !m_pChangeDetectionGroup || !m_pWorkspaceGroup)
		return;

	s_bFilesExpanded           = m_pFilesGroup->IsExpanded();
	s_bChangeDetectionExpanded = m_pChangeDetectionGroup->IsExpanded();
	s_bWorkspaceExpanded       = m_pWorkspaceGroup->IsExpanded();
}
