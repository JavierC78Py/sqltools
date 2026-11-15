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

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __OEFileManagerPageTree_h__
#define __OEFileManagerPageTree_h__

#include "resource.h"
#include "Common/PropTreePage.h"
#include "OpenEditor/OESettings.h"
#include "PropTree/PropTreeItemStatic.h"
#include "PropTree/PropTreeItemEdit.h"

	using OpenEditor::SettingsManager;

// The "Editor::File Manager" settings page, which replaces COEFileManagerPage
// (IDD_OE_FILE_MANAGER). All the page plumbing - the shared empty dialog
// template, tree creation and layout, commit-before-read on Apply, the modified
// state - lives in Common::CPropTreePage. IDD below is the *help* id only; the
// template used is the shared IDD_PROPTREE_PAGE.
//
// A ONE TO ONE CONVERSION, not a merge: the page keeps its classic title and its
// two classic group boxes become its two tree groups, because they already were
// a clean split by subject -
//
//   Explorer - the CFileExplorerWnd panel: what its tooltips say, and which
//              extra entries appear in a file's shell context menu
//   History  - the recent file / workspace lists: how much they store, how much
//              of it the menus show, and what reopening an entry restores
//
// The name clash with the worksheet's "Execution history" group (see
// md/proptree_settings_page_conversion.md) is the reason that group was named
// the way it was: the history here is RecentFileList's, of files and workspaces,
// and it stays on this page.
//
// STORAGE IS UNCHANGED. The rows call the same SetFileManager* and SetHistory*
// accessors of OpenEditor::GlobalSettings in the same order the classic
// OnApply() did, so an existing configuration reads and writes exactly as
// before.
//
// THREE CONVERSION DETAILS:
//
//   * The classic "Enable ascii file preview in tooltips" check box, its
//     IDC_OE_FLMGR_PREVIEWLINES edit, its spin and its "line(s)" static were four
//     controls ON ONE LINE of the template (the edit at y=27 beside the box at
//     y=28); here they are ONE row - the check box is the switch and the number
//     cell is its value, the idiom CPropTreePageHelpers documents and
//     COEFilesWorkspacePageTree already uses for "Memory mapped access for files
//     bigger than (Mb)". The unit moved into the label because a row has nowhere
//     to put a trailing static. SetItemLimit(1, 20) replaces the classic
//     DDV_MinMaxUInt and SetSpinRange() the classic UDM_SETRANGE32 over the same
//     bounds.
//
//   * The other five edit-plus-spin pairs of the classic template (the two
//     history sizes and the three menu sizes, each with a _SPIN buddy) become one
//     InsertNumberEdit() row each, which applies the classic DDV_MinMaxUInt range
//     both as the spin range (the classic UDM_SETRANGE32 over the same bounds)
//     and as the value limit, so typed text is clamped on commit.
//
//   * Labels are shortened where the classic text would not fit and the full
//     classic wording is kept in the row's info pane instead. CPropTreeItem
//     clips a non-root row's label at the splitter column even when the row has
//     no attribute cell, and the base class puts that column at two thirds of
//     the shared 400 DLU template - about 57 characters at 8pt, while the
//     classic "Keep editor cursor / scrolling position / bookmarks in the
//     history" ran to 65. Every label below is 46 characters or fewer.
//
// The enable chain of the classic page is preserved exactly - see
// UpdateEnabledState(). It is the only one on the page: the three shell context
// menu rows and all eight rows of the History group had no enable logic in
// DoDataExchange().
class COEFileManagerPageTree : public Common::CPropTreePage
{
public:
	COEFileManagerPageTree (SettingsManager& manager);
	virtual ~COEFileManagerPageTree ();

	// Dialog Data
	enum { IDD = IDD_OE_FILE_MANAGER_TREE };

protected:
	virtual void BuildTree ();
	virtual BOOL ApplyValues ();
	virtual void SaveTreeState ();
	virtual void OnItemChanged (CPropTreeItem* pItem);

private:
	// Grays out the rows that depend on another row (see the .cpp).
	void UpdateEnabledState ();

	// Clamps the preview line count into the range the classic DDV_MinMaxUInt
	// accepted, so a value that arrived out of bounds from a hand edited
	// configuration file cannot be written straight back out to the settings.
	static int ClampPreviewLines (int value);

	SettingsManager& m_manager;

	CPropTreeItem* m_pExplorerGroup;
	CPropTreeItem* m_pHistoryGroup;

	// The "Explorer" group: the rows of the classic "  Explorer  " group box.
	// The first two are the tooltip chain (the second holding both the preview
	// switch and its line count), the last three the shell context menu entries
	// CFileExplorerWnd, COpenFilesWnd, CMainFrame and COEView ask
	// CustomShellContextMenu for.
	CPropTreeItemStatic* m_pTooltips;
	// Check box AND integer cell in one row: the classic template had them on one
	// line too - the edit, its spin and the "line(s)" static sat beside the check
	// box. See the note in the header above the class.
	CPropTreeItemEdit*   m_pPreviewInTooltips;
	CPropTreeItemStatic* m_pTortoiseGit;
	CPropTreeItemStatic* m_pTortoiseSvn;
	CPropTreeItemStatic* m_pFileProperties;

	// The "History" group: the rows of the classic "  History  " group box, in
	// the order the template, DoDataExchange() and OnApply() all agree on.
	CPropTreeItemStatic* m_pRestoreEditorState;
	CPropTreeItemEdit*   m_pHistoryFiles;
	CPropTreeItemEdit*   m_pHistoryWorkspaces;
	CPropTreeItemEdit*   m_pRecentFilesInMenu;
	CPropTreeItemEdit*   m_pRecentWorkspacesInMenu;
	CPropTreeItemEdit*   m_pRecentQSWorkspacesInMenu;
	CPropTreeItemStatic* m_pOnlyFileNameInMenu;
	CPropTreeItemStatic* m_pOnlyWorkspaceNameInMenu;

	DECLARE_MESSAGE_MAP ()
};

//{{AFX_INSERT_LOCATION}}

#endif//__OEFileManagerPageTree_h__
