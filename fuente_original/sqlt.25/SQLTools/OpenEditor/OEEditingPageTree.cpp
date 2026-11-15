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

#include "stdafx.h"
#include "OpenEditor/OEEditingPageTree.h"
#include "OpenEditor/OEPropTreePageHelpers.h"
#include "Common/MyUtf.h"

	using OpenEditor::GlobalSettings;
	using namespace OEPropTreePage;

// Group expansion state, kept across dialog re-openings (the pattern used by
// COESqlFormatPageTree): written by SaveTreeState(), read by BuildTree().
static BOOL s_bUndoExpanded      = TRUE;
static BOOL s_bCursorExpanded    = TRUE;
static BOOL s_bDisplayExpanded   = TRUE;
static BOOL s_bAutoCloseExpanded = TRUE;

COEEditingPageTree::COEEditingPageTree (SettingsManager& manager)
	: Common::CPropTreePage(_T("Editor::Editing"), COEEditingPageTree::IDD)
	, m_manager(manager)
	, m_pUndoGroup(nullptr), m_pCursorGroup(nullptr), m_pDisplayGroup(nullptr), m_pAutoCloseGroup(nullptr)
	, m_pMaxUndoCount(nullptr)
	, m_pUndoMemLimit(nullptr)
	, m_pUndoAfterSaving(nullptr)
	, m_pTruncateSpaces(nullptr)
	, m_pCursorBeyondEOL(nullptr)
	, m_pCursorBeyondEOF(nullptr)
	, m_pUseSmartHome(nullptr)
	, m_pUseSmartEnd(nullptr)
	, m_pEOFMark(nullptr)
	, m_pTimestampFormat(nullptr)
	, m_pAutoCloseBrackets(nullptr)
	, m_pAutoCloseBeforeWhitespaceOnly(nullptr)
	, m_pAutoSurroundQuotes(nullptr)
	, m_pAutoSurroundBrackets(nullptr)
	, m_pAutoSurroundPlsqlQuoting(nullptr)
{
	// No help topic is registered for this page yet (there is no [ALIAS] entry
	// for hidd_oe_editing_tree in Help/sqltools.hhp), so keep the sheet's Help
	// button disabled while the page is active.
	SetPageHelp(FALSE);
}

COEEditingPageTree::~COEEditingPageTree ()
{
}

// Tree creation and layout, the PTN_* notifications, the resize handling, the
// commit-before-read on Apply and the expansion state on Cancel all come from
// Common::CPropTreePage - this file only holds the page content.

void COEEditingPageTree::BuildTree ()
{
	const OpenEditor::GlobalSettingsPtr s = m_manager.GetGlobalSettings();

	// ── Undo ────────────────────────────────────────────────────────────
	m_pUndoGroup = InsertGroup(m_tree, _T("Undo"),
		_T("Undo buffer limits and behavior."), s_bUndoExpanded);

	// Both undo limits keep the range the classic page validated with
	// DDV_MinMaxUInt(..., 0, 1000000). InsertNumberEdit applies it as the spin
	// range *and* as the value limit, so typed text is clamped on commit (see
	// CPropTreeItemEdit::SetItemLimit) - which also keeps the "KB * 1024"
	// conversion in OnApply() inside int range.
	m_pMaxUndoCount = InsertNumberEdit(m_tree, m_pUndoGroup,
		_T("Max undo count"),
		_T("Maximum number of undo records kept (0 = unlimited)."),
		(int)s->GetUndoLimit(), 1000 /*spin step*/, 0, 1000000);

	m_pUndoMemLimit = InsertNumberEdit(m_tree, m_pUndoGroup,
		_T("Undo memory limit, KB"),
		_T("Maximum amount of memory, in kilobytes, used for undo records."),
		(int)(s->GetUndoMemLimit() / 1024), 1024 /*spin step*/, 0, 1000000);

	m_pUndoAfterSaving = InsertCheckedStatic(m_tree, m_pUndoGroup,
		_T("Keep undo history after saving"),
		_T("If checked, undo history is preserved after the file is saved."),
		s->GetUndoAfterSaving());

	m_pTruncateSpaces = InsertCheckedStatic(m_tree, m_pUndoGroup,
		_T("Truncate trailing spaces on save"),
		_T("Remove trailing whitespace from every line when the file is saved."),
		s->GetTruncateSpaces());

	// ── Cursor ──────────────────────────────────────────────────────────
	m_pCursorGroup = InsertGroup(m_tree, _T("Cursor"),
		_T("Cursor movement and positioning options."), s_bCursorExpanded);

	m_pCursorBeyondEOL = InsertCheckedStatic(m_tree, m_pCursorGroup,
		_T("Cursor beyond EOL"),
		_T("Allow placing the cursor past the end of a line."),
		s->GetCursorBeyondEOL());

	// Dependent row: meaningless while the cursor may not go beyond EOL. SetIndent()
	// marks the dependency, UpdateEnabledState() grays the row out. The stored value
	// is left alone (see OnApply).
	m_pCursorBeyondEOF = InsertCheckedStatic(m_tree, m_pCursorGroup,
		_T("Cursor beyond EOF"),
		_T("Allow placing the cursor past the end of the file. Requires \"Cursor beyond EOL\"."),
		s->GetCursorBeyondEOF());
	m_pCursorBeyondEOF->SetIndent(CPropTreeItem::GetLevelIndent()); // dependent row

	m_pUseSmartHome = InsertCheckedStatic(m_tree, m_pCursorGroup,
		_T("Smart HOME"),
		_T("HOME key moves to first non-blank character before moving to column 0."),
		s->GetUseSmartHome());

	m_pUseSmartEnd = InsertCheckedStatic(m_tree, m_pCursorGroup,
		_T("Smart END"),
		_T("END key moves to last non-blank character before moving to the true end of line."),
		s->GetUseSmartEnd());

	// ── Display ─────────────────────────────────────────────────────────
	m_pDisplayGroup = InsertGroup(m_tree, _T("Display"),
		_T("Editor display options."), s_bDisplayExpanded);

	// "Line numbers" and "Visible spaces" are deliberately NOT offered here: the
	// classic dialog declared both check boxes NOT WS_VISIBLE | WS_DISABLED,
	// because they are View menu toggles (COEDocument::OnEditViewLineNumbers /
	// OnEditViewWhiteSpace, which save the setting immediately). OnApply() leaves
	// the stored values alone.

	m_pEOFMark = InsertCheckedStatic(m_tree, m_pDisplayGroup,
		_T("EOF mark"),
		_T("Show a marker at the end of the file."),
		s->GetEOFMark());

	// Editable combo box: the presets are only suggestions, the value is stored
	// as text (CPropTreeItemCombo text mode), so a hand written format survives -
	// exactly like the CBS_DROPDOWN edit of the classic page.
	static const LPCTSTR timestampFormats[] =
	{
		_T("yyyy-mm-dd hh24:mi:ss"),
		_T("yyyy-mm-dd hh24:mi"),
		_T("yyyy-mm-dd"),
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

	m_pTimestampFormat = InsertComboText(m_tree, m_pDisplayGroup,
		_T("Timestamp format"),
		_T("Format string used to insert the current timestamp."),
		timestampFormats, _countof(timestampFormats),
		Common::wstr(s->GetTimestampFormat()).c_str());

	// ── Auto-closing and auto-surround ────────────────────────────────────
	m_pAutoCloseGroup = InsertGroup(m_tree, _T("Auto-closing and auto-surround"),
		_T("Automatic closing of brackets/quotes and surrounding a selection when typing."),
		s_bAutoCloseExpanded);

	m_pAutoCloseBrackets = InsertCheckedStatic(m_tree, m_pAutoCloseGroup,
		_T("Auto-close brackets"),
		_T("Automatically insert a closing bracket/quote when the opening one is typed."),
		s->GetAutoCloseBrackets());

	// Dependent row: COEditorView::GetAutoCloser() (OEView_1.cpp) returns 0 while
	// the master switch is off, so this rule is only ever consulted with
	// auto-close enabled. UpdateEnabledState() grays the row out, the stored
	// value is written unchanged (see OnApply) so the preference is not lost.
	m_pAutoCloseBeforeWhitespaceOnly = InsertCheckedStatic(m_tree, m_pAutoCloseGroup,
		_T("Only before whitespace"),
		_T("Only auto-close when the character to the right is whitespace or end of line."),
		s->GetAutoCloseBeforeWhitespaceOnly());
	m_pAutoCloseBeforeWhitespaceOnly->SetIndent(CPropTreeItem::GetLevelIndent()); // dependent row

	m_pAutoSurroundQuotes = InsertCheckedStatic(m_tree, m_pAutoCloseGroup,
		_T("Auto-surround quotes"),
		_T("Surround the current selection with quotes when a quote character is typed."),
		s->GetAutoSurroundQuotes());

	m_pAutoSurroundBrackets = InsertCheckedStatic(m_tree, m_pAutoCloseGroup,
		_T("Auto-surround brackets"),
		_T("Surround the current selection with brackets when an opening bracket is typed."),
		s->GetAutoSurroundBrackets());

	// Independent row (spec §2.4 / D7), NOT a dependent of the two above: when it is
	// on, it *replaces* their gate for the ' / { / [ keys in languages that declare
	// AlternativeQuotes (PL/SQL), so it must stay clickable while either is off.
	m_pAutoSurroundPlsqlQuoting = InsertCheckedStatic(m_tree, m_pAutoCloseGroup,
		_T("PL/SQL-aware auto-surround"),
		_T("In PL/SQL files: typing ' around a selection doubles embedded quotes; typing { or [ produces a q'{...}' / q'[...]' literal."),
		s->GetAutoSurroundPlsqlQuoting());

	UpdateEnabledState();
}

// Grays out (and makes unclickable) the rows that depend on another row. A read
// only row cannot be toggled: CPropTreeList skips HTCHECKBOX clicks for it and
// CPropTreeItem draws its label and check box grayed out.
void COEEditingPageTree::UpdateEnabledState ()
{
	if (!m_pCursorBeyondEOL || !m_pCursorBeyondEOF
	||  !m_pAutoCloseBrackets || !m_pAutoCloseBeforeWhitespaceOnly)
		return;

	m_pCursorBeyondEOF->ReadOnly(!m_pCursorBeyondEOL->IsChecked());
	m_pAutoCloseBeforeWhitespaceOnly->ReadOnly(!m_pAutoCloseBrackets->IsChecked());

	// The rows are painted by the child CPropTreeList window, so its children
	// have to be invalidated too - see Common::CPropTreePage::RedrawTree().
	RedrawTree();
}

void COEEditingPageTree::OnItemChanged (CPropTreeItem* pItem)
{
	// Only the two master switches have dependent rows; recomputing them all is
	// cheap and keeps this handler correct when new dependents are added.
	if (pItem == m_pCursorBeyondEOL || pItem == m_pAutoCloseBrackets)
		UpdateEnabledState();
}

// Called from Common::CPropTreePage::OnApply(), which has already committed the
// cell being edited and which reports exceptions.
BOOL COEEditingPageTree::ApplyValues ()
{
	if (!m_pMaxUndoCount)
		return TRUE; // the page was never built

	OpenEditor::GlobalSettingsPtr settings = m_manager.GetGlobalSettings();

	int v = 0;

	// The page shows kilobytes, the setting stores bytes. GetItemValue()
	// clamps to the 0..1000000 KB limit configured in BuildTree(), so the
	// "* 1024" below cannot overflow an int.
	m_pUndoMemLimit->GetItemValue(v);
	settings->SetUndoMemLimit(v * 1024, false /*notify*/);

	m_pMaxUndoCount->GetItemValue(v);
	settings->SetUndoLimit(v, false /*notify*/);

	settings->SetUndoAfterSaving(m_pUndoAfterSaving->IsChecked() ? true : false, false /*notify*/);
	settings->SetTruncateSpaces (m_pTruncateSpaces->IsChecked()  ? true : false, false /*notify*/);

	const bool bCursorBeyondEOL = m_pCursorBeyondEOL->IsChecked() ? true : false;
	settings->SetCursorBeyondEOL(bCursorBeyondEOL, false /*notify*/);
	settings->SetCursorBeyondEOF(bCursorBeyondEOL && m_pCursorBeyondEOF->IsChecked()
									? true : false,                false /*notify*/);

	// Line numbers / visible spaces are View menu toggles - not written here.
	settings->SetEOFMark        (m_pEOFMark->IsChecked()       ? true : false, false /*notify*/);
	settings->SetUseSmartHome   (m_pUseSmartHome->IsChecked()  ? true : false, false /*notify*/);
	settings->SetUseSmartEnd    (m_pUseSmartEnd->IsChecked()   ? true : false, false /*notify*/);
	settings->SetTimestampFormat(Common::str(m_pTimestampFormat->GetItemValueText()), false /*notify*/);

	// Stored as is, *not* gated by bAutoCloseBrackets: COEditorView::
	// GetAutoCloser() already returns 0 while auto-close is off, so gating
	// here would silently wipe the user's preference every time the master
	// switch is turned off (the classic page stores the raw check box too).
	const bool bAutoCloseBrackets = m_pAutoCloseBrackets->IsChecked() ? true : false;
	settings->SetAutoCloseBrackets             (bAutoCloseBrackets,                                            false /*notify*/);
	settings->SetAutoCloseBeforeWhitespaceOnly (m_pAutoCloseBeforeWhitespaceOnly->IsChecked() ? true : false, false /*notify*/);
	settings->SetAutoSurroundQuotes            (m_pAutoSurroundQuotes->IsChecked()   ? true : false,           false /*notify*/);
	settings->SetAutoSurroundBrackets          (m_pAutoSurroundBrackets->IsChecked() ? true : false,           false /*notify*/);
	settings->SetAutoSurroundPlsqlQuoting      (m_pAutoSurroundPlsqlQuoting->IsChecked() ? true : false,       false /*notify*/);

	return TRUE;
}

// Called from Common::CPropTreePage::OnApply() and from ::OnCancel(), so the
// groups stay expanded the way the user left them - also when the sheet is
// cancelled.
void COEEditingPageTree::SaveTreeState ()
{
	// The page may never have been shown, hence the null check.
	if (!m_pUndoGroup)
		return;

	s_bUndoExpanded      = m_pUndoGroup->IsExpanded();
	s_bCursorExpanded    = m_pCursorGroup->IsExpanded();
	s_bDisplayExpanded   = m_pDisplayGroup->IsExpanded();
	s_bAutoCloseExpanded = m_pAutoCloseGroup->IsExpanded();
}
