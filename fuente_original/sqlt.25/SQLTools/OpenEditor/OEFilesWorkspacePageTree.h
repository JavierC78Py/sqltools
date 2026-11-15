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

#ifndef __OEFilesWorkspacePageTree_h__
#define __OEFilesWorkspacePageTree_h__

#include "resource.h"
#include "Common/PropTreePage.h"
#include "OpenEditor/OESettings.h"
#include "PropTree/PropTreeItemStatic.h"
#include "PropTree/PropTreeItemEdit.h"

	using OpenEditor::SettingsManager;

// The "Editor::Files and Workspace" settings page, which replaces TWO classic
// pages at once: COEFilePage (IDD_OE_FILES, "Editor::Files") and OEWorkspacePage
// (IDD_OE_WORKSPACE, "Editor::Workspace"). All the page plumbing - the shared
// empty dialog template, tree creation and layout, commit-before-read on Apply,
// the modified state - lives in Common::CPropTreePage. IDD below is the *help*
// id only; the template used is the shared IDD_PROPTREE_PAGE.
//
// WHY ONE PAGE. The two classic pages were not a clean split. Change detection
// was cut in half across them: "Detect when file is changed outside the editor"
// and its two dependents sat on Files, while "Detect if file changed after
// opening modified version from workspace" - the same idea, for the moment a
// file is restored from a workspace - sat on Workspace. Someone turning change
// detection off had no way to know there was a second, related switch on another
// page. The Workspace page was also only five check boxes, a whole entry in the
// sheet tree for five ticks. So the merge regroups by SUBJECT rather than
// stacking one page's rows under the other:
//
//   Files             - how a file is held and written while it is open
//   Change detection  - every "did this file change under me?" switch, from both
//                       classic pages, now together
//   Workspace         - what a workspace saves, stores and shows
//
// STORAGE IS UNCHANGED. The rows still call the same SetFile* and SetWorkspace*
// accessors of OpenEditor::GlobalSettings, so an existing configuration reads
// and writes exactly as before - merging the pages does not merge the settings.
//
// TWO CONVERSION DETAILS:
//
//   * The classic "Using memory mapped file access for files bigger than:" check
//     box, its IDC_OE_FILE_MEMMAP_THRESHOLD edit, its spin and its "Mb" static
//     were four controls; here they are ONE row - the check box is the switch and
//     the number cell is its value, the idiom CPropTreePageHelpers documents and
//     DDLPageTree already uses for "Preload DB dictionary with (%)". The unit
//     moved into the label because a row has nowhere to put a trailing static.
//     SetItemLimit(1, 999999999) replaces the classic DDV_MinMaxUInt, and
//     SetSpinRange() the classic UDM_SETRANGE32 over the same bounds.
//
//   * Labels are shortened where the classic text would not fit, and the full
//     classic wording is kept in the row's info pane instead. CPropTreeItem clips
//     a non-root row's label at the splitter column even when the row has no
//     attribute cell (the "if (!IsRootLevel())" CreateRectRgn in
//     CPropTreeItem::DrawItem), and the base class puts that column at two thirds
//     of the shared 400 DLU template - about 355 px of text after the expand
//     column, spacing and check box. That is roughly 57 characters at 8pt, while
//     the classic labels ran to 68. Every label below is 49 characters or fewer.
//     Widening GetTreeColumn() instead would hard-code pixels against the base
//     class's deliberately proportional split, and would steal width from the one
//     row here that does have an attribute cell.
//
// The enable chain of the classic Files page is preserved exactly - see
// UpdateEnabledState(). Note it spans two groups now: "Keep files locked" is in
// Files and grays three rows of Change detection out.
class COEFilesWorkspacePageTree : public Common::CPropTreePage
{
public:
	COEFilesWorkspacePageTree (SettingsManager& manager);
	virtual ~COEFilesWorkspacePageTree ();

	// Dialog Data
	enum { IDD = IDD_OE_FILES_WORKSPACE_TREE };

protected:
	virtual void BuildTree ();
	virtual BOOL ApplyValues ();
	virtual void SaveTreeState ();
	virtual void OnItemChanged (CPropTreeItem* pItem);

private:
	// Grays out the rows that depend on another row (see the .cpp).
	void UpdateEnabledState ();

	// Clamps the memory mapped threshold into the range the classic
	// DDV_MinMaxUInt accepted, so a hand edited configuration cannot reach the
	// settings out of bounds.
	static int ClampMemMapThreshold (int value);

	SettingsManager& m_manager;

	CPropTreeItem* m_pFilesGroup;
	CPropTreeItem* m_pChangeDetectionGroup;
	CPropTreeItem* m_pWorkspaceGroup;

	// The "Files" group: the rows of the classic IDD_OE_FILES that are about
	// holding and writing an open file.
	CPropTreeItemStatic* m_pLocking;
	CPropTreeItemStatic* m_pSaveOnSwitch;
	CPropTreeItemStatic* m_pOverwriteReadonly;
	// Check box AND integer cell in one row - see the note in the header.
	CPropTreeItemEdit*   m_pMemMapForBig;

	// The "Change detection" group: the three dependent rows of the classic
	// IDD_OE_FILES plus the one row the classic IDD_OE_WORKSPACE had, which is
	// the reason these two pages are now one.
	CPropTreeItemStatic* m_pDetectFileChanges;
	CPropTreeItemStatic* m_pReloadChangedFiles;
	CPropTreeItemStatic* m_pAutoscrollAfterReload;
	CPropTreeItemStatic* m_pWorkspaceDetectChangesOnOpen;

	// The "Workspace" group: the remaining rows of the classic IDD_OE_WORKSPACE.
	CPropTreeItemStatic* m_pWorkspaceSaveOnExit;
	CPropTreeItemStatic* m_pWorkspaceRelativePath;
	CPropTreeItemStatic* m_pWorkspaceNameInTitleBar;
	CPropTreeItemStatic* m_pWorkspaceNameInStatusBar;

	DECLARE_MESSAGE_MAP ()
};

//{{AFX_INSERT_LOCATION}}

#endif//__OEFilesWorkspacePageTree_h__
