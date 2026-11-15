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

// 23.03.2003 improvement, added a new editor option - keep selection after drag and drop operation
// 17.01.2005 (ak) simplified settings class hierarchy 

#include "stdafx.h"
#include "OpenEditor/OEBlockPageTree.h"
#include "Common/PropTreePageHelpers.h"

	using OpenEditor::GlobalSettings;
	using namespace Common::PropTreeRows;

// Group expansion state, kept across dialog re-openings (the pattern used by
// COEEditingPageTree): written by SaveTreeState(), read by BuildTree().
static BOOL s_bGeneralExpanded = TRUE;
static BOOL s_bColumnExpanded  = TRUE;

// The two BS_AUTORADIOBUTTON pairs of the classic "  Move  " and
// "  Leave cursor at  " group boxes, in the order the template listed them - and
// that order IS the stored value, since each row keeps the index of a bool. The
// lists must therefore never be sorted (InsertComboIndex() passes no CBS_SORT).
//
// Index 0 of s_colMove is ColBlockDeleteSpaceAfterMove == true, which is what
// OEContext_3.cpp does with it: it refills the vacated rectangle with blanks only
// `if (!deleteSpace)`.
//
// Index 1 of s_colCursorAt is ColBlockCursorToStartAfterPaste == true - the name
// says "start" but OEContext_3.cpp:822 does `if (Get...()) MoveTo(pos)` with pos
// the END of the pasted text, and the classic page mapped true to its "&End of a
// pasted text" radio too. See the header for why that is reproduced rather than
// corrected.
static const LPCTSTR s_colMove[] =
{
	_T("Delete space"),
	_T("Leave space"),
};

static const LPCTSTR s_colCursorAt[] =
{
	_T("Beginning of a pasted text"),
	_T("End of a pasted text"),
};

COEBlockPageTree::COEBlockPageTree (SettingsManager& manager)
	: Common::CPropTreePage(_T("Editor::Selection"), COEBlockPageTree::IDD)
	, m_manager(manager)
	, m_pGeneralGroup(nullptr)
	, m_pColumnGroup(nullptr)
	, m_pKeepMarking(nullptr)
	, m_pKeepMarkingAfterDrop(nullptr)
	, m_pDelAndBSDelete(nullptr)
	, m_pTypingOverwrite(nullptr)
	, m_pTabIndent(nullptr)
	, m_pColMove(nullptr)
	, m_pColCursorAt(nullptr)
	, m_pColEditMode(nullptr)
{
	// No help topic is registered for this page (there is no [ALIAS] entry for
	// hidd_oe_blocks_tree in Help/sqltools.hhp), so keep the sheet's Help button
	// disabled while the page is active. The classic COEBlockPage cleared
	// PSP_HASHELP itself for the same reason.
	SetPageHelp(FALSE);
}

COEBlockPageTree::~COEBlockPageTree ()
{
}

// Tree creation and layout, the PTN_* notifications, the resize handling, the
// commit-before-read on Apply and the expansion state on Cancel all come from
// Common::CPropTreePage - this file only holds the page content.

void COEBlockPageTree::BuildTree ()
{
	const OpenEditor::GlobalSettingsPtr s = m_manager.GetGlobalSettings();

	// ── General ───────────────────────────────────────────────────────────
	// The classic "  General  " group box, its five BS_AUTOCHECKBOX in template
	// order. Labels are the classic captions with the & mnemonics stripped - a
	// tree row is drawn DT_NOPREFIX, so a mnemonic would show as a literal "&".
	m_pGeneralGroup = InsertGroup(m_tree, _T("General"),
		_T("What an ordinary edit operation does to the text you have selected."),
		s_bGeneralExpanded);

	// InsertBlock(str, !GetBlockKeepMarking()) - the flag is the hideSelection
	// argument, negated, at OEContext_3.cpp:668.
	m_pKeepMarking = InsertCheckedStatic(m_tree, m_pGeneralGroup,
		_T("Keep selection after edit operations"),
		_T("Leave the selection highlighted after text is pasted or inserted into it, instead of collapsing it to a caret."),
		s->GetBlockKeepMarking());

	// The same InsertBlock() call on the drop half of a drag and drop -
	// OEView_4.cpp:426.
	m_pKeepMarkingAfterDrop = InsertCheckedStatic(m_tree, m_pGeneralGroup,
		_T("Keep selection after drag and drop operation"),
		_T("Leave the selection highlighted after a block is moved or copied with the mouse."),
		s->GetBlockKeepMarkingAfterDragAndDrop());

	// OEView_1.cpp tests this on VK_DELETE, on VK_BACK and on the character key
	// path: on, the whole selection goes; off, the key deletes one character and
	// the selection is only cleared.
	m_pDelAndBSDelete = InsertCheckedStatic(m_tree, m_pGeneralGroup,
		_T("Del and BS keys delete selection"),
		_T("Delete the whole selection when Delete or Backspace is pressed. Off, the key deletes a single character and the selection is just cleared."),
		s->GetBlockDelAndBSDelete());

	// OEView_1.cpp:1436 and :3484 - `if (GetBlockTypingOverwrite()) DeleteBlock();`
	// ahead of inserting the character just typed.
	m_pTypingOverwrite = InsertCheckedStatic(m_tree, m_pGeneralGroup,
		_T("Typing overwrites selection"),
		_T("Replace the selection with the character you type. Off, the selection is cleared first and the character is inserted at the caret."),
		s->GetBlockTypingOverwrite());

	// Off, the selection is dropped before `case '\t'` routes, so Tab indents at the caret alone.
	// Shift+Tab never consults it - routing table: Editor_Features.md section 6.
	m_pTabIndent = InsertCheckedStatic(m_tree, m_pGeneralGroup,
		_T("Tab key indents selection"),
		_T("Indent every selected line when Tab is pressed. Off, Tab acts on the caret alone. Shift+Tab always outdents the selected lines."),
		s->GetBlockTabIndent());

	// ── Column selection ──────────────────────────────────────────────────
	m_pColumnGroup = InsertGroup(m_tree, _T("Column selection"),
		_T("How a column block - the rectangle selected by dragging with Alt held down - behaves."),
		s_bColumnExpanded);

	// The classic "  Move  " group box. Index 0 is the setting being true, so the
	// classic ctor's `Get...() ? 0 : 1` is reproduced literally.
	m_pColMove = InsertComboIndex(m_tree, m_pColumnGroup,
		_T("Move"),
		_T("What a moved or deleted column block leaves behind: \"Delete space\" closes the rectangle up, \"Leave space\" refills it with blanks so the text to its right keeps its column."),
		s_colMove, (int)_countof(s_colMove),
		s->GetColBlockDeleteSpaceAfterMove() ? 0 : 1);

	// The classic "  Leave cursor at  " group box, whose setting name is inverted
	// with respect to what the code does - see the header and s_colCursorAt.
	m_pColCursorAt = InsertComboIndex(m_tree, m_pColumnGroup,
		_T("Leave cursor at"),
		_T("Where the caret is left after a column block is pasted. The setting behind this row is stored under the misleading name ColBlockCursorToStartAfterPaste, in which true means the end."),
		s_colCursorAt, (int)_countof(s_colCursorAt),
		s->GetColBlockCursorToStartAfterPaste() ? 1 : 0);

	// Stood BELOW both nested group boxes in the classic template (y=142, they end
	// at y=137) but still inside the outer "  Column selection  " box, so it is the
	// last row of this group. OEView_1.cpp and OEContext_3.cpp consult it on every
	// column operation: on, the rectangle stays selected and Tab, Delete, Backspace
	// and typing all act on it (ColumnarIndent, ColumnarDelete, ...).
	m_pColEditMode = InsertCheckedStatic(m_tree, m_pColumnGroup,
		_T("Columnar edit mode"),
		_T("Keep a column selection alive while it is edited, so Tab, Delete, Backspace and typing act on the whole rectangle instead of on one line."),
		s->GetColBlockEditMode());
}

// Called from Common::CPropTreePage::OnApply(), which has already committed the
// cell being edited and which reports exceptions.
BOOL COEBlockPageTree::ApplyValues ()
{
	if (!m_pKeepMarking)
		return TRUE; // the page was never built

	OpenEditor::GlobalSettingsPtr settings = m_manager.GetGlobalSettings();

	// The same eight accessors, in the same order and with the same "do not
	// notify" flags as the classic COEBlockPage::OnApply().
	settings->SetBlockKeepMarking                (m_pKeepMarking->IsChecked()          ? true : false, false /*notify*/);
	settings->SetBlockDelAndBSDelete             (m_pDelAndBSDelete->IsChecked()       ? true : false, false /*notify*/);
	settings->SetBlockTypingOverwrite            (m_pTypingOverwrite->IsChecked()      ? true : false, false /*notify*/);
	settings->SetBlockTabIndent                  (m_pTabIndent->IsChecked()            ? true : false, false /*notify*/);
	settings->SetBlockKeepMarkingAfterDragAndDrop(m_pKeepMarkingAfterDrop->IsChecked() ? true : false, false /*notify*/);
	settings->SetColBlockEditMode                (m_pColEditMode->IsChecked()          ? true : false, false /*notify*/);

	// The combo index IS the bool behind each row - see s_colMove and s_colCursorAt
	// for which end is which. Nothing out of range can arrive: InsertComboIndex()
	// clamped what was loaded, and a CBS_DROPDOWNLIST offers no entry to type. The
	// two comparisons are the classic page's
	// `m_ColBlockDeleteSpaceAfterMove ? false : true` and
	// `m_ColBlockCursorToStartAfterPaste ? true : false`, written the way round the
	// lists read.
	settings->SetColBlockDeleteSpaceAfterMove   ((int)m_pColMove->GetItemValue()     == 0, false /*notify*/);
	settings->SetColBlockCursorToStartAfterPaste((int)m_pColCursorAt->GetItemValue() != 0, false /*notify*/);

	return TRUE;
}

// Called from Common::CPropTreePage::OnApply() and from ::OnCancel(), so the
// groups stay expanded the way the user left them - also when the sheet is
// cancelled.
void COEBlockPageTree::SaveTreeState ()
{
	// The page may never have been shown, hence the null check.
	if (!m_pGeneralGroup)
		return;

	s_bGeneralExpanded = m_pGeneralGroup->IsExpanded();
	s_bColumnExpanded  = m_pColumnGroup->IsExpanded();
}
