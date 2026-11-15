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

#ifndef __OEBackupPageTree_h__
#define __OEBackupPageTree_h__

#include "resource.h"
#include "Common/PropTreePage.h"
#include "Common/PropTreeItemFolder.h"
#include "OpenEditor/OESettings.h"
#include "PropTree/PropTreeItemStatic.h"
#include "PropTree/PropTreeItemEdit.h"
#include "PropTree/PropTreeItemCombo.h"

	using OpenEditor::SettingsManager;

// The "Editor::Backup & Autosave" settings page, which replaces COEBackupPage
// (IDD_OE_BACKUP). All the page plumbing - the shared empty dialog template, tree
// creation and layout, commit-before-read on Apply, the modified state - lives in
// Common::CPropTreePage. IDD below is the *help* id only; the template used is the
// shared IDD_PROPTREE_PAGE.
//
// A ONE TO ONE CONVERSION, not a merge: the page keeps its classic title and its
// three classic group boxes become its three tree groups, in the template's order -
//
//   Create backup file when saving - the three way EBackupMode choice and the
//                                    file name the "same folder" mode uses
//   Autosave Snapshot              - the quick-save snapshot timer, its size
//                                    filter and whether a normal exit clears it
//   Backup & Autosave folder       - where those files go, and the space
//                                    management that prunes them
//
// STORAGE IS UNCHANGED. The rows call the same SetFileBackup* and
// SetWorkspaceAutosave* accessors of OpenEditor::GlobalSettings in the same order
// the classic OnApply() did, with the same "do not notify" flags, so an existing
// configuration reads and writes exactly as before. The misspelled setting NAMES -
// FileBackupSpaceManagment among them - are part of that on-disk format and are not
//
// FOUR CONVERSION DETAILS:
//
//   * The three BS_AUTORADIOBUTTON modes (DDX_Radio over IDC_OE_BACKUP_TO_NOTHING)
//     become one InsertComboIndex() row. OpenEditor::EBackupMode is declared
//     { ebmNone = 0, ebmCurrentDirectory = 1, ebmBackupDirectory = 2 }, which are
//     exactly the indices DDX_Radio produced, so the stored value is the combo
//     index and no mapping table is needed. The classic radio captions ran to
//     "in Backup && Autosave folder", too long for an attribute cell one third of
//     the template wide, so the entries are short and the full wording is in the
//     row's info pane - the documented convention for this conversion.
//
//   * The IDC_OE_BACKUP_NAME edit sat ON THE SAME LINE as the second radio ("in the
//     same folder with name:" at x=21, the edit at x=131), which a combo row cannot
//     reproduce: a row has one label and one value cell. It becomes its own row
//     below, marked as belonging to the one above with SetIndent() and grayed with
//     ReadOnly() unless the "same folder" mode is selected - the classic
//     GetDlgItem(IDC_OE_BACKUP_NAME)->EnableWindow(m_BackupMethod == 1).
//
//   * The folder was a read-only EDITTEXT (IDC_OE_BACKUP_DIR, ES_READONLY) with a
//     "&..." browse button beside it and a "&Default Location" button below. The
//     vendored CPropTree has no button, so the two become: one
//     Common::CPropTreeItemFolder row, whose value cell opens Common::CDirSelectDlg
//     on a click or on Enter (see that class for why it rides OnActivate() and not
//     PTN_PROPCLICK, which no key path sends), and one check box row for the
//     default location. That box is not a stand-in for the button: an EMPTY folder
//     IS the "default location" setting, which the classic page only expressed
//     through the "<Default location>" sentinel it painted into the read-only edit,
//     so the state maps onto a box directly. Ticking it clears the folder, which is
//     what the classic button did; picking a folder unticks it, because a chosen
//     folder is by definition not the default location.
//
//   * The four edit-plus-spin pairs each become one InsertNumberEdit() row, which
//     applies the classic DDV_MinMaxUInt range both as the spin range (the classic
//     UDM_SETRANGE32 over the same numbers) and as the value limit, so typed text
//     is clamped on commit and GetItemValue(int&) clamps again on the way out. The
//     trailing unit statics ("sec. (enter 0 to disable)", "Mb", "day(s)") moved
//     into the labels, because a row has nowhere to put a trailing static.
//
// The enable chains of the classic page are preserved exactly - see
// UpdateEnabledState(). The folder row is deliberately NOT part of them: both
// input paths that reach CPropTreeItemFolder::OnActivate() test !IsReadOnly()
// first, so graying it would make the browser unreachable rather than merely
// disabled. Nothing else on the page had enable logic in DoDataExchange().
class COEBackupPageTree : public Common::CPropTreePage
{
public:
	COEBackupPageTree (SettingsManager& manager);
	virtual ~COEBackupPageTree ();

	// Dialog Data
	enum { IDD = IDD_OE_BACKUP_TREE };

protected:
	virtual void BuildTree ();
	virtual BOOL ApplyValues ();
	virtual void SaveTreeState ();
	virtual void OnItemChanged (CPropTreeItem* pItem);

private:
	// Grays out the rows that depend on another row (see the .cpp).
	void UpdateEnabledState ();

	SettingsManager& m_manager;

	CPropTreeItem* m_pBackupGroup;
	CPropTreeItem* m_pAutosaveGroup;
	CPropTreeItem* m_pFolderGroup;

	// "Create backup file when saving": the EBackupMode choice and the file name
	// the ebmCurrentDirectory mode writes beside the original.
	CPropTreeItemCombo*  m_pBackupMethod;
	CPropTreeItemEdit*   m_pBackupName;

	// "Autosave Snapshot": the quick-save snapshot timer in seconds (0 turns it
	// off), the size filter that keeps big files out of a snapshot, and whether a
	// normal exit deletes the snapshot it left behind.
	CPropTreeItemEdit*   m_pAutosaveInterval;
	CPropTreeItemEdit*   m_pAutosaveFileLimit;
	CPropTreeItemStatic* m_pDeleteSnapshotOnExit;

	// "Backup & Autosave folder": the folder both the ebmBackupDirectory backups
	// and the snapshots go to, the box that clears it back to the default
	// location, and the space management that prunes what is in it. m_pFolder
	// holds the SETTING; its inherited attribute text is only the display string,
	// so ApplyValues() reads GetFolder() and never GetItemValue(), which would
	// hand back the "<Default location>" placeholder as though it were a path.
	Common::CPropTreeItemFolder* m_pFolder;
	CPropTreeItemStatic* m_pUseDefaultFolder;
	CPropTreeItemStatic* m_pSpaceManagement;
	CPropTreeItemEdit*   m_pKeepDays;
	CPropTreeItemEdit*   m_pFolderMaxSize;

	DECLARE_MESSAGE_MAP ()
};

//{{AFX_INSERT_LOCATION}}

#endif//__OEBackupPageTree_h__

// touched; only the user-visible labels were corrected.
