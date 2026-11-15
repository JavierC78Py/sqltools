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

// 17.01.2005 (ak) simplified settings class hierarchy
// 2016.06.28 improvement, added new setiing for Workspace saving and Backup space management
// 2026-09-25 replaces COEBackupPage, see the header

#include "stdafx.h"
#include <COMMON/ExceptionHelper.h>
#include <COMMON/MyUtf.h>
#include "OpenEditor/OEBackupPageTree.h"
#include "Common/PropTreePageHelpers.h"

	using namespace Common::PropTreeRows;

// The text the folder cell shows while the setting is empty, and the caption of
// the browser that replaces it. Both are the classic page's own strings:
// COEBackupPage's cszDefaultFolder, painted into the read-only IDC_OE_BACKUP_DIR
// edit for the same empty setting, and the title it passed to CDirSelectDlg.
//
// An empty folder IS the "default location" setting - OnApply() wrote
// SetFileBackupDirectoryV2("") for it - so this is display text only and never
// reaches the storage. CPropTreeItemFolder keeps it apart from the value for
// exactly that reason, which is why ApplyValues() reads GetFolder().
static LPCTSTR const cszDefaultFolder = _T("<Default location>");
static LPCTSTR const cszFolderCaption = _T("Choose the folder for saving backups:");

// Group expansion state, kept across dialog re-openings (the pattern used by
// COEFileManagerPageTree): written by SaveTreeState(), read by BuildTree().
static BOOL s_bBackupExpanded   = TRUE;
static BOOL s_bAutosaveExpanded = TRUE;
static BOOL s_bFolderExpanded   = TRUE;

// The bounds of the four edit-plus-spin pairs of the classic page, each of which
// had them twice - a DDV_MinMaxUInt for what is accepted and a UDM_SETRANGE32 on
// the _SPIN buddy for what the spin offers - over the same numbers. All four
// become an InsertNumberEdit() row, which applies the pair in one call, so the
// range is BOTH the spin range and the value limit (SetItemLimit): typed text is
// clamped on commit the way DDV_MinMaxUInt clamped it on exchange, and
// GetItemValue(int&) clamps once more on the way out to the settings.
//
// The autosave interval in seconds; 0 turns the snapshot off, which is what the
// classic "sec. (enter 0 to disable)" static beside the edit said.
static const int s_autosaveIntervalMin = 0;
static const int s_autosaveIntervalMax = 60 * 60;

// "Exclude files bigger than", in Mb: the size filter the snapshot writer applies.
static const int s_autosaveFileLimitMin = 1;
static const int s_autosaveFileLimitMax = 1024 * 1024;

// "Keep files for", in days: how long the space management retains a backup.
static const int s_keepDaysMin = 0;
static const int s_keepDaysMax = 365 * 10;

// "Total folder size limit", in Mb: the cap the space management prunes to.
static const int s_folderMaxSizeMin = 0;
static const int s_folderMaxSizeMax = 1024 * 1024;

// The spin steps. All four classic spins stepped by 1 across their whole range,
// which for the 3600 wide seconds range and for the two 1048576 wide Mb ranges
// means the arrows cannot carry the user any further than a few units from where
// they started - the same complaint the two history rows of
// COEFileManagerPageTree had. That page's rule of taking the lower bound as the
// step does not carry over here: three of these four ranges start at 0 or at 1,
// which is no step at all. So each gets one sized to its own unit instead - half
// a minute for a seconds timer, a month for a days counter, 64 for the two
// megabyte sizes - and the BOUNDS above are the classic ones untouched, so what
// is stored, and what a hand edited configuration file may hold, is unchanged.
// CPropTreeSpinButton::OnDeltaPos() clamps at both ends, so the first and the
// last value of every range stay reachable even where the step does not divide it.
static const int s_autosaveIntervalStep = 30;
static const int s_keepDaysStep         = 30;
static const int s_megabyteStep         = 64;

// The three EBackupMode choices, in the enum's own order: { ebmNone = 0,
// ebmCurrentDirectory = 1, ebmBackupDirectory = 2 }, which are exactly the
// indices the classic DDX_Radio over IDC_OE_BACKUP_TO_NOTHING produced and so
// exactly what InsertComboIndex() stores. The list must therefore never be
// sorted or reordered, and the entries are short - the attribute cell is one
// third of the shared template wide - with the classic radio captions kept in the
// row's info pane: "do &not backup", "in the &same folder with name:" and
// "in &Backup && Autosave folder".
static const LPCTSTR s_backupMethods[] =
{
	_T("Do not back up"),
	_T("In the same folder"),
	_T("In the backup folder"),
};

COEBackupPageTree::COEBackupPageTree (SettingsManager& manager)
	// The title is what CTreePropSheet splits on "::" to hang the page under the
	// "Editor" node of the sheet tree. It is the classic page's own caption,
	// unchanged: this is a one to one conversion and not a merge. The "&" needs no
	// escaping anywhere on the way - SplitPageTreePath() only looks for "::" and its
	// "\::" escape, and a CTreeCtrl item label does not process mnemonics - so the
	// node reads "Backup & Autosave" exactly as the classic caption did.
	: Common::CPropTreePage(_T("Editor::Backup & Autosave"), COEBackupPageTree::IDD)
	, m_manager(manager)
	, m_pBackupGroup(NULL)
	, m_pAutosaveGroup(NULL)
	, m_pFolderGroup(NULL)
	, m_pBackupMethod(NULL)
	, m_pBackupName(NULL)
	, m_pAutosaveInterval(NULL)
	, m_pAutosaveFileLimit(NULL)
	, m_pDeleteSnapshotOnExit(NULL)
	, m_pFolder(NULL)
	, m_pUseDefaultFolder(NULL)
	, m_pSpaceManagement(NULL)
	, m_pKeepDays(NULL)
	, m_pFolderMaxSize(NULL)
{
}

COEBackupPageTree::~COEBackupPageTree ()
{
}

BEGIN_MESSAGE_MAP (COEBackupPageTree, Common::CPropTreePage)
END_MESSAGE_MAP ()


// Called from Common::CPropTreePage::OnInitDialog() once the tree exists.
void COEBackupPageTree::BuildTree ()
{
	// The classic page cleared PSP_HASHELP in its constructor and Help/sqltools.hhp
	// has no [ALIAS] entry for it, so the sheet's Help button goes off while this
	// page is active. Without it, Help would report "topic not found" for the id
	// assigned in the constructor.
	SetPageHelp(FALSE);

	const OpenEditor::GlobalSettingsPtr settings = m_manager.GetGlobalSettings();

	// A root level item is drawn as a group HEADER - shaded, bold, full width, and
	// with the attribute cell skipped under "if (!IsRootLevel()) DrawAttribute(...)"
	// in CPropTreeItem::DrawItem - so no row here is a root level one: every row
	// lives in a group, or it would paint its label and no value cell at all.

	////////////////////////////////////////////////////////////////////////////
	// Create backup file when saving: the classic group box of the same name,
	// which held the three radio buttons and the name edit sitting on the second
	// radio's line.
	m_pBackupGroup = InsertGroup(m_tree, _T("Create backup file when saving"),
		_T("Whether saving a file also writes a backup copy of the version being overwritten, and where that copy goes."),
		s_bBackupExpanded);

	// The classic three BS_AUTORADIOBUTTON modes, exchanged by
	// DDX_Radio(IDC_OE_BACKUP_TO_NOTHING, m_BackupMethod) and written back by the
	// switch in OnApply(). OpenEditor::EBackupMode's own values are the radio
	// indices, so the combo index IS the setting and needs no translation.
	//
	// The row commits on a pick rather than on kill-focus - that is what
	// Common::CPropTreeItemComboOnSelect is for, and why this is an
	// InsertComboIndexOnSelect() row and not an InsertComboIndex() one - because
	// the classic page re-ran
	// DoDataExchange() from ON_BN_CLICKED on all three radios, and the name row
	// below has to gray in and out at that same moment rather than one click later.
	m_pBackupMethod = InsertComboIndexOnSelect(m_tree, m_pBackupGroup,
		_T("Backup file"),
		_T("Create a backup file when saving a file. \"Do not back up\" is the classic \"do not backup\". \"In the same folder\" is \"in the same folder with name:\": the backup is written beside the original under the name in the row below. \"In the backup folder\" is \"in Backup & Autosave folder\": it is written into the folder of the last group instead."),
		s_backupMethods, _countof(s_backupMethods),
		(int)settings->GetFileBackupV2());

	// The classic IDC_OE_BACKUP_NAME edit, which the template placed ON the second
	// radio's line - "in the &same folder with name:" at x=21 w=107 and the edit at
	// x=131 - and which DoDataExchange() enabled with
	// GetDlgItem(IDC_OE_BACKUP_NAME)->EnableWindow(m_BackupMethod == 1). A row has
	// one label and one value cell, so the pairing cannot be kept: the mode is the
	// combo above and the name becomes its own row, with SetIndent() marking it as
	// belonging to the row above and ReadOnly() doing the EnableWindow(FALSE) -
	// CPropTreeList tests !IsReadOnly() before it activates a cell, for the click
	// and for Enter alike, which is the tree's equivalent of a disabled edit box.
	// The classic had no DDV_MaxChars on this edit, so no text limit is set either.
	//
	// Common::wstr() and not the raw .c_str() the classic page assigned to its
	// CString: the setting is a narrow UTF-8 std::string, so a CString assignment
	// from it converts as CP_ACP while ApplyValues() writes it back through
	// Common::str() as UTF-8, and a name with anything non-ascii in it would not
	// survive that round trip. Reading and writing both go through Common now, the
	// way GridPageTree reads its own string settings.
	m_pBackupName = InsertEditText(m_tree, m_pBackupGroup,
		_T("Backup file name"),
		_T("The name of the backup file that the \"In the same folder\" mode writes beside the original. Unavailable while another mode is selected, because nothing writes this name then. The classic page kept the value of a disabled edit and exchanged it anyway, so the name survives a round trip through another mode; ApplyValues() writes it unconditionally for the same reason."),
		Common::wstr(settings->GetFileBackupName()).c_str());
	m_pBackupName->SetIndent(CPropTreeItem::GetLevelIndent()); // dependent row

	////////////////////////////////////////////////////////////////////////////
	// Autosave Snapshot: the classic "  Autosave Snapshoot " group box. The
	// caption's spelling is corrected here - it is a label and not part of the
	// storage, unlike the GetWorkspaceAutosave* accessor names behind it, which
	// are and so stay as they are. The rows keep the template's order, which
	// DoDataExchange() and OnApply() both follow: interval, size filter,
	// delete-on-exit.
	m_pAutosaveGroup = InsertGroup(m_tree, _T("Autosave Snapshot"),
		_T("The quick-save snapshots: how often one is written, how big a file it still takes, and whether a normal exit clears it."),
		s_bAutosaveExpanded);

	// The classic "&Interval:" edit, its _SPIN buddy and the trailing
	// "sec. (enter 0 to disable)" static - the unit and the note moved into the
	// label, because a row has nowhere to put a trailing static. 0 is inside the
	// range and is the off switch, exactly as the classic DDV_MinMaxUInt(0, 60*60)
	// and the matching UDM_SETRANGE32 allowed.
	m_pAutosaveInterval = InsertNumberEdit(m_tree, m_pAutosaveGroup,
		_T("Interval, sec. (0 disables)"),
		_T("How often the autosave snapshot is written, in seconds. 0 turns the snapshot off, which is what the classic \"enter 0 to disable\" note beside the edit said. The range is the classic spin's 0 to 3600; the arrows step by 30 rather than by 1, which over a range this wide could not carry the user anywhere."),
		settings->GetWorkspaceAutosaveInterval(),
		s_autosaveIntervalStep, s_autosaveIntervalMin, s_autosaveIntervalMax);

	// The classic "&Exclude files bigger than:" edit, its _SPIN buddy and the
	// trailing "Mb" static, over DDV_MinMaxUInt(1, 1024*1024).
	m_pAutosaveFileLimit = InsertNumberEdit(m_tree, m_pAutosaveGroup,
		_T("Exclude files bigger than, Mb"),
		_T("Leave a file out of the autosave snapshot when it is bigger than this, in megabytes. The range is the classic spin's 1 to 1048576; the arrows step by 64 rather than by 1, which over a range this wide could not carry the user anywhere."),
		settings->GetWorkspaceAutosaveFileLimit(),
		s_megabyteStep, s_autosaveFileLimitMin, s_autosaveFileLimitMax);

	// The classic "&Delete snapshot on normal exit" check box, whose control id the
	// template spelled IDC_OE_AUTOSAVE_DELETE_SMAPSHOT_ON_EXIT. DoDataExchange() had
	// no EnableWindow() for it, so nothing gates this row.
	m_pDeleteSnapshotOnExit = InsertCheckedStatic(m_tree, m_pAutosaveGroup,
		_T("Delete snapshot on normal exit"),
		_T("Delete the autosave snapshot when the editor closes normally, instead of keeping it. A snapshot left behind is what the recovery of the next start offers to restore, so keeping it costs disk space and clearing it gives up that one recovery."),
		settings->GetWorkspaceAutosaveDeleteOnExit());

	////////////////////////////////////////////////////////////////////////////
	// Backup & Autosave folder: the classic "  Backup && Autosave &folder  " group
	// box. One "&" and not two, because CPropTreeItem::DrawItem() draws a label with
	// DT_NOPREFIX and so paints an ampersand literally - the doubled one is a dialog
	// template escape this tree never applies. The rows keep the template's order:
	// the folder and its two buttons first, then the space management check box and
	// the two numbers it enables.
	m_pFolderGroup = InsertGroup(m_tree, _T("Backup & Autosave folder"),
		_T("The folder the backups of the \"In the backup folder\" mode and the autosave snapshots are written to, and the space management that prunes what collects in it."),
		s_bFolderExpanded);

	// The folder as stored, where an EMPTY string means the default location - the
	// classic page held the same value in m_BackupDirectory, painted its
	// cszDefaultFolder sentinel over it while it was empty, and mapped it back to
	// L"" in OnApply(). The sentinel stays display-only here too:
	// CPropTreeItemFolder keeps it in the inherited attribute text and the setting in
	// its own m_sFolder, which is what ApplyValues() reads through GetFolder().
	// Common::wstr() for the same reason the name row above uses it: the setting is
	// narrow UTF-8, and ApplyValues() writes it back through Common::str().
	const CString storedFolder = Common::wstr(settings->GetFileBackupDirectoryV2()).c_str();

	// The classic read-only EDITTEXT IDC_OE_BACKUP_DIR plus its "&..." button
	// IDC_OE_BACKUP_DIR_CHANGE, whose handler built a CDirSelectDlg with this same
	// caption and passed the current folder - or L"" for the sentinel, which is why
	// an unset folder starts the browser from its own remembered place rather than
	// from the desktop. One row does both: the cell shows the folder and opens the
	// browser when it is clicked or when Enter is pressed on it.
	//
	// It is deliberately NOT ReadOnly(), and not indented either. Both paths into
	// CPropTreeItemFolder::OnActivate() test !IsReadOnly() first, so graying the row
	// would make the browser unreachable rather than merely disabled - which is also
	// why the "use the default location" box below does not gate this row the way
	// the space management box gates the two numbers under it.
	m_pFolder = InsertFolder(m_tree, m_pFolderGroup,
		_T("Folder"),
		_T("The folder the backups and the autosave snapshots are written to. Click the path, or press Enter on it, to choose another; the cell shows \"<Default location>\" while none is set, which means the editor's own default. The classic page kept this path in a read-only edit beside a \"...\" button, so the browser was the only way to change it, and it still is."),
		cszFolderCaption, cszDefaultFolder, storedFolder);

	// The classic "&Default Location" PUSHBUTTON IDC_OE_BACKUP_DIR_CLEAR, whose
	// handler did nothing but set m_BackupDirectory = cszDefaultFolder and repaint -
	// that is, clear the setting. An empty folder IS the default location, so the
	// action is a state and not a press, and a check box holds it honestly: ticking
	// it clears the folder the way the button did, and OnItemChanged() unticks it
	// again when a folder is picked, because a chosen folder is by definition not the
	// default location. The box therefore always says what the setting is.
	m_pUseDefaultFolder = InsertCheckedStatic(m_tree, m_pFolderGroup,
		_T("Use the default location"),
		_T("Write the backups and the autosave snapshots to the editor's own default location instead of to a chosen folder. Tick it to clear the folder above, which is what the classic \"Default Location\" button did; choose a folder to untick it again. An empty folder is how this has always been stored, so nothing about the setting changed with the button becoming a check box."),
		storedFolder.IsEmpty());

	// The classic "Automatic space &managment" check box
	// (IDC_OE_AUTOSAVE_FOLDER_MANAGMENT), which DoDataExchange() exchanged and then
	// used to enable the two edits below it. The label's spelling is corrected here;
	// the GetFileBackupSpaceManagment() accessor behind it is part of the storage and
	// is left exactly as it is.
	m_pSpaceManagement = InsertCheckedStatic(m_tree, m_pFolderGroup,
		_T("Automatic space management"),
		_T("Prune the backup and autosave folder automatically, by the age of a file and by the total size of the folder. Untick it to keep everything written there, and the two rows below then have nothing to do."),
		settings->GetFileBackupSpaceManagment());

	// The classic "&Keep files for:" edit, its _SPIN buddy and the trailing "day(s)"
	// static, over DDV_MinMaxUInt(0, 365*10) and enabled by
	// GetDlgItem(IDC_OE_AUTOSAVE_KEEP_DAYS)->EnableWindow(m_FileBackupSpaceManagment).
	m_pKeepDays = InsertNumberEdit(m_tree, m_pFolderGroup,
		_T("Keep files for, day(s)"),
		_T("How long a file in the backup and autosave folder is kept before the space management deletes it, in days. Unavailable while the space management is off, because nothing deletes files then. The range is the classic spin's 0 to 3650; the arrows step by 30 rather than by 1, which over a range this wide could not carry the user anywhere."),
		settings->GetFileBackupKeepDays(),
		s_keepDaysStep, s_keepDaysMin, s_keepDaysMax);
	m_pKeepDays->SetIndent(CPropTreeItem::GetLevelIndent()); // dependent row

	// The classic "Total folder size &limit:" edit, its _SPIN buddy and the trailing
	// "Mb" static, over DDV_MinMaxUInt(0, 1024*1024) and enabled by
	// GetDlgItem(IDC_OE_AUTOSAVE_FOLDER_MAX_SIZE)->EnableWindow(m_FileBackupSpaceManagment).
	m_pFolderMaxSize = InsertNumberEdit(m_tree, m_pFolderGroup,
		_T("Total folder size limit, Mb"),
		_T("The size the backup and autosave folder is pruned back to, in megabytes. Unavailable while the space management is off, because nothing deletes files then. The range is the classic spin's 0 to 1048576; the arrows step by 64 rather than by 1, which over a range this wide could not carry the user anywhere."),
		settings->GetFileBackupFolderMaxSize(),
		s_megabyteStep, s_folderMaxSizeMin, s_folderMaxSizeMax);
	m_pFolderMaxSize->SetIndent(CPropTreeItem::GetLevelIndent()); // dependent row

	// The enable chains have to be applied HERE as well as on change. The classic
	// page drove them from DoDataExchange(), which runs on the initial UpdateData()
	// too - so a saved configuration whose mode was not "in the same folder" came up
	// with the name edit already grayed, and one with the space management off came
	// up with both of its numbers grayed. Wiring only OnItemChanged() would leave
	// them live until the user touched something.
	UpdateEnabledState();
}

// The enable chains of the classic COEBackupPage::DoDataExchange(), translated from
// EnableWindow() to the tree's ReadOnly() - which is what blocks the click
// (CPropTreeList tests !IsReadOnly() before it toggles a check box or activates a
// cell, for the mouse and for the keyboard alike) and what grays the row out:
//
//   IDC_OE_BACKUP_NAME              EnableWindow(m_BackupMethod == 1)
//   IDC_OE_AUTOSAVE_KEEP_DAYS       EnableWindow(m_FileBackupSpaceManagment)
//   IDC_OE_AUTOSAVE_FOLDER_MAX_SIZE EnableWindow(m_FileBackupSpaceManagment)
//
// m_BackupMethod == 1 is OpenEditor::ebmCurrentDirectory, the only mode that writes
// the backup beside the original under a name of the user's own.
//
// The folder row is NOT here, and that is not an omission: the classic template left
// IDC_OE_BACKUP_DIR and both of its buttons enabled at all times, and DoDataExchange()
// had no EnableWindow() for any of them. It could not be gated by the tree's
// ReadOnly() anyway, because that is the very test CPropTreeList applies before it
// reaches CPropTreeItemFolder::OnActivate() - a grayed folder row would be one whose
// browser can never open.
//
// ReadOnly() does not clear a check box and does not blank a text cell, which matches
// EnableWindow(): the classic page grayed the name edit while another mode was
// selected and still exchanged its value, so the name survives a round trip through
// another mode untouched. That is why ApplyValues() writes both dependent rows
// unconditionally rather than skipping them while they are read only.
void COEBackupPageTree::UpdateEnabledState ()
{
	if (!m_pBackupMethod || !m_pBackupName || !m_pSpaceManagement)
		return;

	// ebmCurrentDirectory == 1, the mode that uses the name.
	m_pBackupName->ReadOnly((int)m_pBackupMethod->GetItemValue() != OpenEditor::ebmCurrentDirectory);

	const BOOL bManage = m_pSpaceManagement->IsChecked();

	m_pKeepDays->ReadOnly(!bManage);
	m_pFolderMaxSize->ReadOnly(!bManage);

	// The rows are painted by the child CPropTreeList window, so its children have to
	// be invalidated too - see Common::CPropTreePage::RedrawTree(). Without this the
	// dependent rows keep their old colour until something else repaints them, which
	// is the missing redraw COEFilesWorkspacePageTree::UpdateEnabledState() had.
	RedrawTree();
}

// PTN_ITEMCHANGED for the combo row - posted from its own CBN_SELCHANGE, see
// CPropTreeItemComboOnSelect - for the folder row after a pick, and PTN_CHECKCLICK
// for the three check box rows. Common::CPropTreePage has already marked the page
// modified for both notifications. The four number rows feed nothing else on the
// page and are therefore deliberately absent: the classic DoDataExchange() had no
// EnableWindow() for any of them.
void COEBackupPageTree::OnItemChanged (CPropTreeItem* pItem)
{
	// The two heads of an enable chain: the mode grays the name row, the space
	// management grays the two numbers. UpdateEnabledState() repaints.
	if (pItem == m_pBackupMethod || pItem == m_pSpaceManagement)
	{
		UpdateEnabledState();
		return;
	}

	if (pItem == m_pUseDefaultFolder)
	{
		// Ticking the box is the classic "&Default Location" button: it clears the
		// folder, and the cell then shows the "<Default location>" placeholder again.
		// Unticking leaves the folder as it is, which for an already empty folder
		// means the cell keeps showing the placeholder while the box says otherwise -
		// a state the classic page could not be put into, because its button only ever
		// cleared. It is harmless: an empty folder IS the default location, so what is
		// stored and what happens at run time are the same either way, and the next
		// pick or the next tick puts the box back in step with the value.
		//
		// SetFolder() only sets text, so this raises no notification of its own and
		// cannot re-enter. The repaint is ours, because the folder cell is painted by
		// the list and not by anything this handler touched.
		if (m_pUseDefaultFolder->IsChecked())
			m_pFolder->SetFolder(_T(""));

		RedrawTree();
		return;
	}

	if (pItem == m_pFolder)
	{
		// Reached from CPropTreeItemFolder::OnActivate()'s CommitChanges() once the
		// browser has returned a folder. A folder that is there is not the default
		// location, so the box follows the value - the auto-untick that keeps the two
		// rows telling the same story. Check() sends nothing, only a user click
		// raises PTN_CHECKCLICK, so this cannot re-enter either.
		if (!m_pFolder->GetFolder().IsEmpty())
			m_pUseDefaultFolder->Check(FALSE);

		RedrawTree();
		return;
	}
}

// Called from Common::CPropTreePage::OnApply(), which has already committed the cell
// being edited and which wraps the call in its own EXCEPTION_FRAME, so neither the
// classic page's try block nor its _OE_DEFAULT_HANDLER_ is repeated here. Returning
// FALSE keeps the sheet open, which is what the classic OnApply() did when the user
// declined to reset a folder that no longer exists.
BOOL COEBackupPageTree::ApplyValues ()
{
	if (!m_pBackupGroup)
		return TRUE; // the page was never built

	OpenEditor::GlobalSettingsPtr settings = m_manager.GetGlobalSettings();

	// The same setters, in the same order, with the same "do not notify" flags the
	// classic OnApply() used. Nothing here is re-read by a later setter, and
	// SQLTools_4.cpp pushes the values out to the windows once, from
	// OnSettingsChanged() after the sheet closed.
	//
	// The name is written even while its row is read only, the way the classic
	// DDX_Text exchanged the value of a disabled edit box, so the setting survives a
	// round trip through another mode. GetItemValue() with no argument is the text
	// overload - the row was built by InsertEditText(), which selected
	// ValueFormatText - and Common::str is the conversion the classic page used.
	settings->SetFileBackupName(Common::str(m_pBackupName->GetItemValue()), false /*notify*/);

	// GetFolder() and NOT GetItemValue(): the inherited attribute text is the display
	// string, so reading it back would hand over "<Default location>" as though it
	// were a path. An empty folder is the stored form of the default location, which
	// is what the classic m_BackupDirectory == cszDefaultFolder ? L"" : m_BackupDirectory
	// arrived at from the other direction.
	CString backupFolder = m_pFolder->GetFolder();

	// The classic page's own check, kept as it was: a folder that has gone away is
	// reported, and the user is offered the reset its "&Default Location" button would
	// have done. Declining returns FALSE and the sheet stays open with the path still
	// in the cell, so the user can correct it rather than lose it.
	if (!backupFolder.IsEmpty() && !PathFileExists(backupFolder))
	{
		MessageBeep((UINT)-1);

		if (AfxMessageBox(_T("The backup folder \"") + backupFolder +
				_T("\" does not exist!\n\nWould you like to reset it to default?"),
				MB_ICONEXCLAMATION | MB_YESNO) == IDYES)
		{
			backupFolder.Empty();

			// Leave the page telling the same story as the value being written, so a
			// second Apply - or a look at the rows behind the message box - shows the
			// default location and not the path that failed. SetFolder() and Check()
			// raise nothing, so neither can re-enter OnItemChanged().
			m_pFolder->SetFolder(_T(""));
			m_pUseDefaultFolder->Check(TRUE);
			RedrawTree();
		}
		else
			return FALSE;
	}

	settings->SetFileBackupDirectoryV2(Common::str(backupFolder), false /*notify*/);

	// The combo index IS the EBackupMode value - see s_backupMethods - so the classic
	// three case switch, which only renumbered what DDX_Radio had numbered already,
	// collapses into a cast. Nothing out of range can arrive: InsertComboIndexOnSelect()
	// clamped what was loaded and a CBS_DROPDOWNLIST offers no entry to type.
	settings->SetFileBackupV2((OpenEditor::EBackupMode)(int)m_pBackupMethod->GetItemValue());

	// The four InsertNumberEdit() rows. GetItemValue(int&) already clamps to the item
	// limit they installed from the classic DDV_MinMaxUInt bounds, so - unlike a
	// check-box-plus-number row, whose InsertCheckedEdit() sets no range - there is
	// nothing left to clamp here. One local is reused across them, the way
	// COEFileManagerPageTree reads its number rows. Both are written while read only
	// too, for the reason given above.
	int nValue = 0;

	m_pAutosaveInterval->GetItemValue(nValue);
	settings->SetWorkspaceAutosaveInterval(nValue);

	m_pAutosaveFileLimit->GetItemValue(nValue);
	settings->SetWorkspaceAutosaveFileLimit(nValue);

	settings->SetWorkspaceAutosaveDeleteOnExit(m_pDeleteSnapshotOnExit->IsChecked() ? true : false);

	m_pKeepDays->GetItemValue(nValue);
	settings->SetFileBackupKeepDays(nValue);

	m_pFolderMaxSize->GetItemValue(nValue);
	settings->SetFileBackupFolderMaxSize(nValue);

	settings->SetFileBackupSpaceManagment(m_pSpaceManagement->IsChecked() ? true : false);

	return TRUE;
}

// Called from Common::CPropTreePage::OnApply() and from ::OnCancel(), so the groups
// stay expanded the way the user left them - also when the sheet is cancelled.
void COEBackupPageTree::SaveTreeState ()
{
	// The page may never have been shown, hence the null check.
	if (!m_pBackupGroup || !m_pAutosaveGroup || !m_pFolderGroup)
		return;

	s_bBackupExpanded   = m_pBackupGroup->IsExpanded();
	s_bAutosaveExpanded = m_pAutosaveGroup->IsExpanded();
	s_bFolderExpanded   = m_pFolderGroup->IsExpanded();
}

