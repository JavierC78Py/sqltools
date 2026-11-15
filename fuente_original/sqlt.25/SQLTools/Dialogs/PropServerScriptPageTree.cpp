/* 
	SQLTools is a tool for Oracle database developers and DBAs.
    Copyright (C) 1997-2004 Aleksey Kochetov

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

#include "stdafx.h"
#include "Dialogs/PropServerScriptPageTree.h"
#include "Common/PropTreePageHelpers.h"
#include "Common/MyUtf.h"

	using namespace Common::PropTreeRows;

// Group expansion state, kept across dialog re-openings: written by
// SaveTreeState(), read by BuildTree().
static BOOL s_bSessionExpanded = TRUE;
static BOOL s_bScriptExpanded  = TRUE;
static BOOL s_bHistoryExpanded = TRUE;

// The classic page validated the plan table name with DDV_MaxChars(..., 65).
static const int s_planTableMaxChars = 65;

// The classic history page exchanged both limits with DDV_MinMaxUInt(0, INT_MAX)
// while its spin buddies ran from 1 (UDM_SETRANGE32, 1, INT_MAX). The tree takes
// the spin range and the value limit from one pair, so the range the classic spin
// could actually reach wins: a 0 would silently keep no statement at all, since
// HistorySource::AddStatement() drops every row while a limit is 0.
static const int s_historyCountMin = 1;
static const int s_historyCountMax = INT_MAX;
static const int s_historySizeMin  = 1;

// HistorySource::AddStatement() computes "1024 * GetHistoryMaxSize()" in an int,
// so 2097151 Kb (just under 2 Gb) is the largest size that does not overflow it
// into a negative limit - which would empty the history instead of filling it.
static const int s_historySizeMax  = 2097151;

CPropServerScriptPageTree::CPropServerScriptPageTree (SQLToolsSettings& settings)
	: Common::CPropTreePage(_T("SQLTools::Session and Execution"), 0 /*no help topic*/),
	m_bShowSessionScriptGroups(TRUE),
	m_bShowHistoryGroup(TRUE),
	m_settings(settings),
	m_pSessionGroup(nullptr), m_pScriptGroup(nullptr), m_pHistoryGroup(nullptr),
	m_pDateFormat(nullptr), m_pPlanTable(nullptr), m_pObjectCache(nullptr),
	m_pSessionStatistics(nullptr), m_pAutocommit(nullptr), m_pCommitOnDisconnect(nullptr),
	m_pOutputEnable(nullptr), m_pOutputSize(nullptr), m_pCancelConfirmation(nullptr),
	m_pKeepAlivePeriod(nullptr),
	m_pExecutionStyle(nullptr), m_pExecutionStyleAlt(nullptr), m_pHaltOnError(nullptr),
	m_pHighlightExecuted(nullptr), m_pAutoscrollToExecuted(nullptr),
	m_pAutoscrollExecuted(nullptr), m_pBufferedOutput(nullptr),
	m_pIgnoreCommentsAfterSemicolon(nullptr),
	m_pHistoryEnabled(nullptr), m_pHistoryAction(nullptr),
	m_pHistoryKeepSelection(nullptr), m_pHistoryMaxCount(nullptr),
	m_pHistoryMaxSize(nullptr), m_pHistoryValidOnly(nullptr),
	m_pHistoryDistinct(nullptr), m_pHistoryGlobal(nullptr),
	m_pHistoryPersistent(nullptr)
{
	// The 0 help id above clears PSP_HASHELP, which is what all three classic
	// pages did explicitly: Help\HTMLDefines.h has HIDD_PROP_SERVER,
	// HIDD_PROP_SCRIPT and HIDD_PROP_HISTORY, but Help\sqltools.hhp has no
	// [ALIAS] entry for any of them.
}

CPropServerScriptPageTree::~CPropServerScriptPageTree ()
{
}

// The groups are built by helpers of their own because every one of them is
// optional, see m_bShowSessionScriptGroups / m_bShowHistoryGroup in the header:
// a helper runs only when the sheet that shows the page wants its group.
void CPropServerScriptPageTree::BuildTree ()
{
	if (m_bShowSessionScriptGroups)
		BuildSessionAndScriptGroups();

	if (m_bShowHistoryGroup)
		BuildHistoryGroup();

	// Grays the dependent rows out, exactly like the classic
	// CPropHistoryPage::OnInitDialog() called OnBnClicked_Enable() once the
	// exchange had filled the check boxes.
	UpdateEnabledState();
}

void CPropServerScriptPageTree::BuildSessionAndScriptGroups ()
{
	// -- Session (was CPropServerPage, "SQLTools::Session") -----------------
	m_pSessionGroup = InsertGroup(m_tree, _T("Session"),
		_T("Formats, transaction handling and Dbms_Output of a database session."),
		s_bSessionExpanded);

	// Editable combo box (CBS_DROPDOWN in the classic dialog): the presets are
	// only suggestions and the value is stored as text, so a hand written format
	// survives. The twelve strings are the ones the classic dialog got from its
	// IDD_PROP_SERVER DLGINIT resource, in the same (unsorted) order.
	static const LPCTSTR dateFormats[] =
	{
		_T("dd.mm.yy hh24:mi:ss"),
		_T("dd.mm.yy hh24:mi"),
		_T("dd.mm.yy"),
		_T("dd.mm.yyyy hh24:mi:ss"),
		_T("dd.mm.yyyy hh24:mi"),
		_T("dd.mm.yyyy"),
		_T("mm/dd/yy hh12:mi:ss am"),
		_T("mm/dd/yy hh12:mi am"),
		_T("mm/dd/yy"),
		_T("mm/dd/yyyy hh12:mi:ss am"),
		_T("mm/dd/yyyy hh12:mi am"),
		_T("mm/dd/yyyy"),
	};

	m_pDateFormat = InsertComboText(m_tree, m_pSessionGroup,
		_T("Date format (empty for default)"),
		_T("NLS date format applied to a session. Leave it empty to use the database default."),
		dateFormats, _countof(dateFormats),
		Common::wstr(m_settings.GetDateFormat()).c_str());

	m_pPlanTable = InsertEditText(m_tree, m_pSessionGroup,
		_T("Plan table name"),
		_T("Name of the plan table an explain plan is written to."),
		Common::wstr(m_settings.GetPlanTable()).c_str(), s_planTableMaxChars);

	m_pObjectCache = InsertCheckedStatic(m_tree, m_pSessionGroup,
		_T("Object cache"),
		_T("Reuse the database object definitions (metadata) a session has already collected."),
		m_settings.GetObjectCache());

	m_pSessionStatistics = InsertCheckedStatic(m_tree, m_pSessionGroup,
		_T("Session statistics"),
		_T("Collect the session statistics of a running statement and show them."),
		m_settings.GetSessionStatistics());

	m_pAutocommit = InsertCheckedStatic(m_tree, m_pSessionGroup,
		_T("Autocommit"),
		_T("Commit every statement automatically, without an explicit COMMIT."),
		m_settings.GetAutocommit());

	// Drop down list storing the selected index, like the DDX_CBIndex combo of
	// the classic dialog: 0 - always rollback, 1 - always commit, 2 - ask (the
	// default). Consumed by before_closing_connect() in App\ConnectionTasks.cpp.
	static const LPCTSTR commitOnDisconnect[] =
	{
		_T("Always rollback"),
		_T("Always commit"),
		_T("User choice"),
	};

	m_pCommitOnDisconnect = InsertComboIndex(m_tree, m_pSessionGroup,
		_T("Commit/rollback on disconnect"),
		_T("What to do with an open transaction when a session is closed."),
		commitOnDisconnect, _countof(commitOnDisconnect),
		m_settings.GetCommitOnDisconnect());

	m_pOutputEnable = InsertCheckedStatic(m_tree, m_pSessionGroup,
		_T("Enable Dbms_Output"),
		_T("Capture the DBMS_OUTPUT a session produces."),
		m_settings.GetOutputEnable());

	// Dependent of "Enable Dbms_Output": indented one level like the dependent
	// rows of the editor pages, but left editable - the classic page did not gray
	// it out, and the size is worth keeping while the output is off. The value is
	// stored raw (see ApplyValues), so nothing is lost either way.
	// The bounds are the DDV_MinMaxUInt(0, 1000000000) of the classic page; the
	// spin step of 10000 is what its UDM_SETACCEL hack multiplied the default
	// increment by (CPropServerPage::OnInitDialog).
	m_pOutputSize = InsertNumberEdit(m_tree, m_pSessionGroup,
		_T("Dbms_Output size"),
		_T("Size of the DBMS_OUTPUT buffer in bytes (0 - unlimited)."),
		m_settings.GetOutputSize(), 10000 /*spin step*/, 0, 1000000000);
	m_pOutputSize->SetIndent(CPropTreeItem::GetLevelIndent());

	m_pCancelConfirmation = InsertCheckedStatic(m_tree, m_pSessionGroup,
		_T("Cancel confirmation"),
		_T("Ask for a confirmation before a running operation is cancelled."),
		m_settings.GetCancelConfirmation());

	m_pKeepAlivePeriod = InsertNumberEdit(m_tree, m_pSessionGroup,
		_T("Keep alive period, minutes"),
		_T("Idle time in minutes after which a keep-alive statement (begin null; end;) is executed on a connection. 0 disables it."),
		m_settings.GetKeepAlivePeriod(), 1 /*spin step*/, 0, 60);

	// -- Script (was CPropScriptPage, "SQLTools::Script") -------------------
	m_pScriptGroup = InsertGroup(m_tree, _T("Script execution"),
		_T("How the statements of a worksheet are located, executed and shown."),
		s_bScriptExpanded);

	// The two radio button groups of the classic dialog (DDX_Radio) become drop
	// down lists - CPropTree has no radio item. The order of the items IS the
	// stored value: CPLSWorksheetDoc::ExecutionStyle (0 - OLD, 1 - NEW, 2 - TOAD),
	// so the array must never be reordered. Two captions are the classic radio
	// button texts; the middle one is not - "New (based on sql syntax)" said when
	// the mode was added, not what it does. It is the only style that runs the SQL
	// CommandParser over the enclosing range and picks the statement that contains
	// the cursor (OLD takes everything from the current line, TOAD the blank line
	// separated block).
	static const LPCTSTR executionStyles[] =
	{
		_T("Classic (from the current line)"),
		_T("SQL syntax (statement at the cursor)"),
		_T("TOAD (blank line separated)"),
	};

	m_pExecutionStyle = InsertComboIndex(m_tree, m_pScriptGroup,
		_T("Execute \"Current\" style"),
		_T("How the statement to run is located by the \"Execute current\" command."),
		executionStyles, _countof(executionStyles),
		m_settings.GetExecutionStyle());

	m_pExecutionStyleAlt = InsertComboIndex(m_tree, m_pScriptGroup,
		_T("Execute \"Current (Alt)\" style"),
		_T("How the statement to run is located by the \"Execute current (Alt)\" command."),
		executionStyles, _countof(executionStyles),
		m_settings.GetExecutionStyleAlt());

	m_pHaltOnError = InsertCheckedStatic(m_tree, m_pScriptGroup,
		_T("Halt execution on error"),
		_T("Stop a script when a statement fails."),
		m_settings.GetExecutionHaltOnError());

	m_pHighlightExecuted = InsertCheckedStatic(m_tree, m_pScriptGroup,
		_T("Highlight executed statement"),
		_T("Highlight the statement that has been executed in the editor."),
		m_settings.GetHighlightExecutedStatement());

	m_pAutoscrollToExecuted = InsertCheckedStatic(m_tree, m_pScriptGroup,
		_T("Autoscroll to the executed statement"),
		_T("Scroll the editor to the statement that is being executed."),
		m_settings.GetAutoscrollToExecutedStatement());

	m_pAutoscrollExecuted = InsertCheckedStatic(m_tree, m_pScriptGroup,
		_T("Autoscroll the whole statement before execution"),
		_T("Scroll the whole statement into view before it is executed."),
		m_settings.GetAutoscrollExecutedStatement());

	m_pBufferedOutput = InsertCheckedStatic(m_tree, m_pScriptGroup,
		_T("Buffered output"),
		_T("Collect the output of a script and show it once the execution has finished."),
		m_settings.GetBufferedExecutionOutput());

	// The classic label read "Ignore end-line commments ..." - the typo is fixed
	// here, the setting itself (IgnoreCommmentsAfterSemicolon) keeps its name.
	m_pIgnoreCommentsAfterSemicolon = InsertCheckedStatic(m_tree, m_pScriptGroup,
		_T("Ignore end-line comments after the final semicolon"),
		_T("Do not treat the end-line comments that follow the final semicolon as a part of the statement."),
		m_settings.GetIgnoreCommmentsAfterSemicolon());

}

void CPropServerScriptPageTree::BuildHistoryGroup ()
{
	// -- Execution history (was CPropHistoryPage, "SQLTools::History") -------
	m_pHistoryGroup = InsertGroup(m_tree, _T("Execution history"),
		_T("The statements a worksheet has executed: what is collected, how much of it is kept, and what picking an entry does."),
		s_bHistoryExpanded);

	// The master switch of the group. HistorySource::AddStatement() records a
	// statement only while it is on, HistorySource::OnSettingsChanged() empties the
	// history as soon as it is turned off, and the history commands and the history
	// view of a worksheet are disabled without it (SQLWorksheetDoc::OnUpdate*,
	// CHistoryView).
	m_pHistoryEnabled = InsertCheckedStatic(m_tree, m_pHistoryGroup,
		_T("Enable"),
		_T("Collect the statements a worksheet executes and offer them in its history list. Turning this off empties the history of the open worksheets."),
		m_settings.GetHistoryEnabled());

	// The three radio buttons of the classic "Action" group box (DDX_Radio on
	// IDC_PROP_HIST_ACTION_COPY) become a drop down list - CPropTree has no radio
	// item, and the two "Execution style" groups above are converted the same way.
	// The order of the items IS the stored value (SQLToolsSettings::HistoryAction:
	// Copy = 0, Paste = 1, ReplaceAll = 2), so the array must never be reordered.
	// Read by CPLSWorksheetDoc::OnSqlHistoryGet().
	static const LPCTSTR historyActions[] =
	{
		_T("Copy to clipboard"),
		_T("Paste in the cursor position"),
		_T("Replace all editor content"),
	};

	m_pHistoryAction = InsertComboIndex(m_tree, m_pHistoryGroup,
		_T("Action"),
		_T("What picking an entry of the history list does: copy it to the clipboard, paste it at the cursor, or replace the whole content of the worksheet."),
		historyActions, _countof(historyActions),
		m_settings.GetHistoryAction());

	// CPLSWorksheetDoc::OnSqlHistoryGet() passes !KeepSelection to
	// CEditor::InsertBlock(), so "keep" means the pasted text stays selected.
	m_pHistoryKeepSelection = InsertCheckedStatic(m_tree, m_pHistoryGroup,
		_T("Keep selection after paste operation"),
		_T("Select the text a history entry pasted into the worksheet. Has an effect for the paste and the replace action only."),
		m_settings.GetHistoryKeepSelection());

	// Dependent of "Enable": the classic OnBnClicked_Enable() disabled the edit box
	// and its spin buddy. HistorySource::AddStatement() drops the oldest entries
	// once the list reaches either limit. The classic template put the unit of the
	// second one into a "Kb" static behind the edit box; a tree cell has no room for
	// that, so the unit is part of the label - like "Keep alive period, minutes" of
	// the session group.
	m_pHistoryMaxCount = InsertNumberEdit(m_tree, m_pHistoryGroup,
		_T("Number of statements"),
		_T("How many statements the history list keeps. The oldest ones are dropped first."),
		m_settings.GetHistoryMaxCout(), 1 /*spin step*/, s_historyCountMin, s_historyCountMax);
	m_pHistoryMaxCount->SetIndent(CPropTreeItem::GetLevelIndent()); // dependent row

	m_pHistoryMaxSize = InsertNumberEdit(m_tree, m_pHistoryGroup,
		_T("Memory allocated, Kb"),
		_T("How much memory the statements of the history list may take. The oldest ones are dropped first."),
		m_settings.GetHistoryMaxSize(), 1 /*spin step*/, s_historySizeMin, s_historySizeMax);
	m_pHistoryMaxSize->SetIndent(CPropTreeItem::GetLevelIndent());  // dependent row

	// CommandPerformerSettings::historyValidOnly, read by CommandPerformerImpl.cpp:
	// a statement that raised an error is recorded too while this is off.
	m_pHistoryValidOnly = InsertCheckedStatic(m_tree, m_pHistoryGroup,
		_T("Valid statements only"),
		_T("Record only the statements that were executed without an error."),
		m_settings.GetHistoryValidOnly());

	// Dependent of "Enable", like the two limits above. Read both by
	// HistorySource::AddStatement(), which drops an older identical statement, and
	// by the merge HistoryFileManager::Load() performs on a persistent history.
	m_pHistoryDistinct = InsertCheckedStatic(m_tree, m_pHistoryGroup,
		_T("Remove duplicate entries"),
		_T("Drop an older identical statement instead of keeping it in the list twice."),
		m_settings.GetHistoryDistinct());
	m_pHistoryDistinct->SetIndent(CPropTreeItem::GetLevelIndent()); // dependent row

	// HistoryFileManager::CreateSource() hands every worksheet the one shared
	// HistorySource while this is on, and a source of its own otherwise. The classic
	// OnBnClicked_Global() did nothing but show a message box - "Changing GLOBAL
	// option will not affect already open windows!" - which the info text replaces.
	m_pHistoryGlobal = InsertCheckedStatic(m_tree, m_pHistoryGroup,
		_T("Global"),
		_T("Keep one history for all worksheets instead of one per worksheet. Changing this does not affect the already open windows."),
		m_settings.GetHistoryGlobal());

	// HistoryFileManager::Load() / Save(): the history is written into the history
	// folder and read back when a worksheet is opened.
	m_pHistoryPersistent = InsertCheckedStatic(m_tree, m_pHistoryGroup,
		_T("Persistent"),
		_T("Save the history in the history folder and load it again, so that it survives a restart of SQLTools."),
		m_settings.GetHistoryPersitent());
}

// Grays out (and makes unclickable) the rows that depend on another row. A read
// only row cannot be edited: CPropTreeList skips the click and CPropTreeItem draws
// it grayed out.
void CPropServerScriptPageTree::UpdateEnabledState ()
{
	// The classic CPropHistoryPage::OnBnClicked_Enable() enabled the two limits and
	// "Remove duplicate entries" only while IDC_PROP_HIST_ENABLE was checked. Its
	// two commented out lines - IDC_PROP_HIST_INHERIT, which has no control at all,
	// and IDC_PROP_HIST_PERSISTENT - are not reproduced, so "Persistent" stays
	// available while the history is off, as it always was.
	BOOL bHistoryOn = m_pHistoryEnabled && m_pHistoryEnabled->IsChecked();

	if (m_pHistoryMaxCount)
		m_pHistoryMaxCount->ReadOnly(!bHistoryOn);

	if (m_pHistoryMaxSize)
		m_pHistoryMaxSize->ReadOnly(!bHistoryOn);

	if (m_pHistoryDistinct)
		m_pHistoryDistinct->ReadOnly(!bHistoryOn);

	// The rows are painted by the child CPropTreeList window, so its children have
	// to be invalidated too - see Common::CPropTreePage::RedrawTree().
	RedrawTree();
}

void CPropServerScriptPageTree::OnItemChanged (CPropTreeItem* pItem)
{
	// Only "Enable" has dependent rows; the test keeps this handler correct when
	// new dependents are added.
	if (pItem == m_pHistoryEnabled)
		UpdateEnabledState();
}

// The cell being edited has already been committed by the base class, so the
// item values can be read as they are shown.
BOOL CPropServerScriptPageTree::ApplyValues ()
{
	// Every group is optional (see the class comment), so each block is guarded by
	// its own group and not by one test for the whole page: a history-only page
	// never writes a session setting and vice versa. A group that was not built has
	// a null pointer - the page may also never have been shown at all.
	//
	// All three sheets hand the page a COPY of the settings: SettingsDialogCallback
	// (App\SQLTools_4.cpp) owns one and pushes it into the global settings after OK,
	// OnGridSettings() gets one from its caller. That is also where the single
	// NotifySettingsChanged() is raised. Hence notify = false everywhere - the
	// classic DDX wrote the members directly and notified nothing either. The public
	// Set accessors are used because the members are private and only the three
	// classic pages are friends of SQLToolsSettings.
	int nValue = 0;

	if (m_pSessionGroup)
	{
		m_settings.SetDateFormat(Common::str(m_pDateFormat->GetItemValueText()), false /*notify*/);
		m_settings.SetPlanTable(Common::str(m_pPlanTable->GetItemValue()), false /*notify*/);
		m_settings.SetObjectCache(m_pObjectCache->IsChecked() ? true : false, false /*notify*/);
		m_settings.SetSessionStatistics(m_pSessionStatistics->IsChecked() ? true : false, false /*notify*/);
		m_settings.SetAutocommit(m_pAutocommit->IsChecked() ? true : false, false /*notify*/);
		m_settings.SetCommitOnDisconnect((int)m_pCommitOnDisconnect->GetItemValue(), false /*notify*/);
		m_settings.SetOutputEnable(m_pOutputEnable->IsChecked() ? true : false, false /*notify*/);
		m_settings.SetCancelConfirmation(m_pCancelConfirmation->IsChecked() ? true : false, false /*notify*/);

		// Dependent rows are stored RAW: the consumers gate on the master switch, and
		// gating here again would wipe the preference whenever Dbms_Output is off.
		m_pOutputSize->GetItemValue(nValue);    // already clamped to the item limit
		m_settings.SetOutputSize(nValue, false /*notify*/);

		m_pKeepAlivePeriod->GetItemValue(nValue);
		m_settings.SetKeepAlivePeriod(nValue, false /*notify*/);
	}

	if (m_pScriptGroup)
	{
		m_settings.SetExecutionStyle((int)m_pExecutionStyle->GetItemValue(), false /*notify*/);
		m_settings.SetExecutionStyleAlt((int)m_pExecutionStyleAlt->GetItemValue(), false /*notify*/);
		m_settings.SetExecutionHaltOnError(m_pHaltOnError->IsChecked() ? true : false, false /*notify*/);
		m_settings.SetHighlightExecutedStatement(m_pHighlightExecuted->IsChecked() ? true : false, false /*notify*/);
		m_settings.SetAutoscrollToExecutedStatement(m_pAutoscrollToExecuted->IsChecked() ? true : false, false /*notify*/);
		m_settings.SetAutoscrollExecutedStatement(m_pAutoscrollExecuted->IsChecked() ? true : false, false /*notify*/);
		m_settings.SetBufferedExecutionOutput(m_pBufferedOutput->IsChecked() ? true : false, false /*notify*/);
		m_settings.SetIgnoreCommmentsAfterSemicolon(m_pIgnoreCommentsAfterSemicolon->IsChecked() ? true : false, false /*notify*/);
	}

	if (m_pHistoryGroup)
	{
		m_settings.SetHistoryEnabled(m_pHistoryEnabled->IsChecked() ? true : false, false /*notify*/);
		m_settings.SetHistoryAction((int)m_pHistoryAction->GetItemValue(), false /*notify*/);
		m_settings.SetHistoryKeepSelection(m_pHistoryKeepSelection->IsChecked() ? true : false, false /*notify*/);
		m_settings.SetHistoryValidOnly(m_pHistoryValidOnly->IsChecked() ? true : false, false /*notify*/);
		m_settings.SetHistoryGlobal(m_pHistoryGlobal->IsChecked() ? true : false, false /*notify*/);
		m_settings.SetHistoryPersitent(m_pHistoryPersistent->IsChecked() ? true : false, false /*notify*/);

		// Dependent rows are stored RAW here too: the consumers gate on
		// HistoryEnabled (HistorySource::AddStatement, HistoryFileManager), so
		// writing them only while the history is on would wipe both limits and the
		// "no duplicates" preference - as the classic disabled controls did not.
		m_pHistoryMaxCount->GetItemValue(nValue);
		m_settings.SetHistoryMaxCout(nValue, false /*notify*/);

		m_pHistoryMaxSize->GetItemValue(nValue);
		m_settings.SetHistoryMaxSize(nValue, false /*notify*/);

		m_settings.SetHistoryDistinct(m_pHistoryDistinct->IsChecked() ? true : false, false /*notify*/);
	}

	return TRUE;
}

// Called from OnApply() and from OnCancel(), so the expansion state survives
// both. The page may never have been shown - and a group may not exist at all,
// see m_bShowSessionScriptGroups / m_bShowHistoryGroup - hence the null checks.
void CPropServerScriptPageTree::SaveTreeState ()
{
	if (m_pSessionGroup)
		s_bSessionExpanded = m_pSessionGroup->IsExpanded();

	if (m_pScriptGroup)
		s_bScriptExpanded = m_pScriptGroup->IsExpanded();

	if (m_pHistoryGroup)
		s_bHistoryExpanded = m_pHistoryGroup->IsExpanded();
}
