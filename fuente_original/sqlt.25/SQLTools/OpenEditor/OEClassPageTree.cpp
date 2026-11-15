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
// 2011.09.13 bug fix, the application hangs if user sets either Indent or Tab size to 0
//            (carried over as the 1..32 limit of the two number rows - see the header)

#include "stdafx.h"
#include <sstream>
#include "OpenEditor/OEClassPageTree.h"
#include "OpenEditor/OEHelpers.h"          // OpenEditor::EIndent, OpenEditor::eiSmart
#include "OpenEditor/OELanguageManager.h"  // LanguageManager::GetSmartIndent
#include "Common/PropTreePageHelpers.h"
#include "COMMON/StrHelpers.h"             // Common::to_printable_str / to_unprintable_str
#include "COMMON/MyUtf.h"                  // Common::wstr / Common::str

	using OpenEditor::ClassSettings;
	using OpenEditor::ClassSettingsPtr;
	using OpenEditor::LanguageManager;
	using OpenEditor::eiSmart;
	using namespace Common::PropTreeRows;

// Group expansion state, kept across dialog re-openings: written by SaveTreeState(),
// read by BuildTree(). One BOOL per category, by position, and NOT keyed by title the
// way CVisualAttributesPageTree does it - that page is built by four hosts under two
// different titles, this one by a single host under one.
static std::vector<BOOL> s_groupExpanded;

// "Create a new file as" - an OpenEditor::EFileFormat. The list is in the order the
// classic combo box was filled in OnInitDialog() - "Dos (LF/CR)", "Unix (LF)",
// "Mac (CR/LF)" - with the "Default" entry of the sibling COEPropPage put in front of
// it, so that index == value + 1 and effDefault (-1) has an entry of its own instead
// of the blank combo box the classic page showed for it. See the header.
//
// The lists must never be sorted (InsertComboIndex() passes no CBS_SORT): the index IS
// the stored value.
static const LPCTSTR s_fileCreateAs[] =
{
	_T("Default"),
	_T("Dos (LF/CR)"),
	_T("Unix (LF)"),
	_T("Mac (CR/LF)"),
};

// The offset between the index of s_fileCreateAs and the EFileFormat it stands for.
static const int s_fileCreateAsOffset = 1;

// "Tab expand" - the two BS_AUTORADIOBUTTON of the classic nested group box, in the
// template's order. Index 0 is TabExpand == true: the classic CheckRadioButton() added
// `GetTabExpand() ? 0 : 1` to IDC_OEC_INSERT_SPACES, and its OnChangeData() wrote
// `IsDlgButtonChecked(IDC_OEC_INSERT_SPACES)` straight back as the bool.
static const LPCTSTR s_tabExpand[] =
{
	_T("Insert spaces"),
	_T("Keep tabs"),
};

// "Auto indent" - an OpenEditor::EIndent, index == value in BOTH lists, so the row value
// is the setting itself and needs no mapping. "Smart" is offered only by the second list:
// it does something only for a language LanguageManager::GetSmartIndent() has rules for,
// and anywhere else it is indistinguishable from "Default", so offering it there would be
// a choice the user can not tell apart from the one above it.
//
// The second list is named for PL/SQL because that is the only language with rules today;
// the test that picks between them asks the factory rather than comparing a name, so a
// second strategy needs this array renamed and nothing else.
static const LPCTSTR s_indentType[] =
{
	_T("None"),
	_T("Default"),
};

static const LPCTSTR s_PlSqlIndentType[] =
{
	_T("None"),
	_T("Default"),
	_T("Smart"),
};

// Which of the two lists a category gets.
//
// The second condition is a round-trip guard, not a courtesy. InsertComboIndex() clamps an
// out of range value to entry 0 - eiNone for this row - and ApplyValues() writes the
// clamped index straight back, so a category that stores eiSmart but whose language has no
// rules would be SILENTLY REWRITTEN to eiNone on the next Apply. That is reachable: set
// Smart on a PL/SQL category, then change that category's language to Text. Offering the
// third entry keeps the stored 2 visible and intact, and it then behaves as eiAuto -
// exactly what EditContext::applySmartIndent() does for a language with no strategy.
static bool categoryOffersSmartIndent (const ClassSettingsPtr& s)
{
	return LanguageManager::GetSmartIndent(s->GetLanguage()) != NULL
	    || s->GetIndentType() == eiSmart;
}

// The info text names only the choices the row actually offers.
static LPCTSTR indentTypeInfoText (bool offersSmart)
{
	return offersSmart
		? _T("Whether pressing Enter inside an indented block lines the new line up under the one above it. \"Default\" does, \"None\" starts the new line at column 1, \"Smart\" also follows the block structure of the language.")
		: _T("Whether pressing Enter inside an indented block lines the new line up under the one above it. \"Default\" does, \"None\" starts the new line at column 1.");
}

// The Tab size and Indent size limit the classic page set on its two spin controls
// (UDM_SETRANGE32, 1, 32). 0 hangs the editor - the 2011.09.13 bug fix above.
static const int s_spacingMin = 1;
static const int s_spacingMax = 32;

COEClassPageTree::COEClassPageTree (SettingsManager& manager, LPCTSTR pszOpenCategory)
	: Common::CPropTreePage(_T("Editor::Categories"), COEClassPageTree::IDD)
	, m_manager(manager)
	, m_openCategory(pszOpenCategory ? pszOpenCategory : _T(""))
{
	// No help topic is registered for this page (there is no [ALIAS] entry for
	// hidd_oe_classes_tree in Help/sqltools.hhp), so keep the sheet's Help button
	// disabled while the page is active. The classic COEClassPage cleared PSP_HASHELP
	// itself for the same reason.
	SetPageHelp(FALSE);
}

COEClassPageTree::~COEClassPageTree ()
{
}

// Tree creation and layout, the PTN_* notifications - the accordion of
// GetOneGroupOpenAtATime() among them - the resize handling, the commit-before-read on
// Apply and the expansion state on Cancel all come from Common::CPropTreePage; this
// file only holds the page content.


// The info pane text of a category group. The extensions are in it because they are the
// one property that says which files a category is for - the group label is only its name.
static CString categoryGroupInfo (const ClassSettingsPtr& s)
{
	CString info;

	info.Format(_T("The editor settings of the files this category claims - \"%s\". ")
		_T("A file belongs to the category whose \"File extensions\" list holds its ")
		_T("extension, and that category then supplies the file's word delimiters, column ")
		_T("markers, tab and indent behaviour, keyword case and the format a new file is ")
		_T("created in."),
		Common::wstr(s->GetExtensions()).c_str());

	return info;
}

void COEClassPageTree::BuildTree ()
{
	m_rows.clear();

	const int count = m_manager.GetClassCount();

	// The category the host asked the page to open, matched by name the way the classic
	// page matched its m_className against the "&Category:" combo box - but only while one
	// of the categories really carries that name. The classic page fell back to its first
	// category when none did, and closing every group instead would leave the page looking
	// empty, so a name that matches nothing is dropped here and the remembered state stays
	// in charge.
	CString openCategory;

	if (!m_openCategory.IsEmpty())
	{
		for (int i = 0; i < count && openCategory.IsEmpty(); i++)
		{
			ClassSettingsPtr s = m_manager.GetClassByPos(i);

			if (s && m_openCategory == Common::wstr(s->GetName()).c_str())
				openCategory = m_openCategory;
		}
	}

	// With the accordion of GetOneGroupOpenAtATime() at most one group is open, so at most
	// one is restored: a state saved before the accordion existed could hold several.
	bool groupOpened = false;

	for (int i = 0; i < count; i++)
	{
		CategoryRows rows;

		rows.m_settings = m_manager.GetClassByPos(i);

		if (!rows.m_settings)
			continue;

		// The expansion state is indexed by the GROUP and not by the category, so that a
		// category skipped above cannot shift the indices SaveTreeState() writes.
		const size_t groupIndex = m_rows.size();

		// While the host named a category to open, that name decides and every other group
		// stays closed: the sheet then comes up on the category of the document it was
		// opened from, which is what selecting that category did in the classic page.
		//
		// Otherwise the remembered state decides, and a category the state has no entry for
		// - a settings file with more categories than the one that saved it - starts
		// expanded. The accordion then leaves the first of them open, which is the category
		// the classic combo box came up with.
		BOOL expanded;

		if (!openCategory.IsEmpty())
			expanded = (openCategory == Common::wstr(rows.m_settings->GetName()).c_str()) ? TRUE : FALSE;
		else
			expanded = (groupIndex < s_groupExpanded.size()) ? s_groupExpanded[groupIndex] : TRUE;

		if (expanded && groupOpened)
			expanded = FALSE;

		if (expanded)
			groupOpened = true;

		rows.m_pGroup = InsertGroup(m_tree,
			Common::wstr(rows.m_settings->GetName()).c_str(),
			categoryGroupInfo(rows.m_settings), expanded);

		// The rows are built on the element that is stored, not on the local: a vector
		// reallocation on the next push_back would leave them pointing into m_rows.
		m_rows.push_back(rows);

		BuildCategoryRows(m_rows.back());
	}
}

// The nine rows of one category, in the reading order of the classic template from top to
// bottom. The "&Category:" combo box is the group this builds under, so the first row is
// the template's "File e&xtensions:". The labels are the classic ones with their
// mnemonics dropped - a tree row draws no accelerator - and the two group boxes the
// template nested below them ("  Tab expand  " and "  Auto indent  ") are rows, because
// a row has one label and one value cell: the box caption became the label and the radios
// inside it the entries of the list, which keeps the classic reading order ("Tab expand:
// Insert spaces", "Auto indent: Default") instead of flattening the radios into rows of
// their own.
void COEClassPageTree::BuildCategoryRows (CategoryRows& rows)
{
	const ClassSettingsPtr& s = rows.m_settings;
	CPropTreeItem* pGroup     = rows.m_pGroup;

	rows.m_pExtensions = InsertEditText(m_tree, pGroup,
		_T("File extensions"),
		_T("The space separated extensions, without the dot, that make a file belong to this category. A file whose extension no category lists gets the first category of this list."),
		Common::wstr(s->GetExtensions()).c_str());

	// EFileFormat, index == value + s_fileCreateAsOffset - the "Default" entry in front of
	// the classic three is what gives effDefault (-1) a row of its own. See the header.
	rows.m_pFileCreateAs = InsertComboIndex(m_tree, pGroup,
		_T("Create a new file as"),
		_T("The line ending a NEW file of this category is created with. \"Default\" keeps whatever the editor already uses, and an existing file always keeps the ending it was read with."),
		s_fileCreateAs, (int)_countof(s_fileCreateAs),
		s->GetFileCreateAs() + s_fileCreateAsOffset);

	// Stored the way the classic page stored it - Common::to_unprintable_str() of what the
	// row shows, so a tab is the two characters \t in the settings file - and shown the way
	// OnSelChangeLanguage() showed it, through to_printable_str().
	std::string delimiters;
	Common::to_printable_str(s->GetDelimiters().c_str(), delimiters);

	rows.m_pDelimiters = InsertEditText(m_tree, pGroup,
		_T("Word delimiters"),
		_T("The characters that end a word, and so decide what a double click selects and where word-wise navigation stops. Write a control character as an escape - \\t for a tab, \\n for a line feed."),
		Common::wstr(delimiters).c_str());

	// Stored 0 based, shown 1 based - the classic page wrote `*it + 1` and read `val - 1`,
	// so the row a user reads is the column number the ruler shows and not an offset.
	std::ostringstream markers;
	const std::vector<int>& markerSet = s->GetColumnMarkersSet();

	for (std::vector<int>::const_iterator it = markerSet.begin(); it != markerSet.end(); ++it)
	{
		if (it != markerSet.begin())
			markers << ',';

		markers << (*it + 1);
	}

	rows.m_pColumnMarkers = InsertEditText(m_tree, pGroup,
		_T("Column markers"),
		_T("The columns the editor draws a vertical marker at, as column numbers from 1, separated by commas or spaces. Leave the row empty for no marker."),
		Common::wstr(markers.str()).c_str());

	rows.m_pNormalizeKeywords = InsertCheckedStatic(m_tree, pGroup,
		_T("Normalize keywords"),
		_T("Compare the words of a file of this category case insensitively, so that the keyword list of its language highlights SELECT and select alike."),
		s->GetNormalizeKeywords());

	// The classic nested group box of the two radios over the one TabExpand bool: index 0
	// is "Insert spaces", which is TabExpand == true - see s_tabExpand.
	rows.m_pTabExpand = InsertComboIndex(m_tree, pGroup,
		_T("Tab expand"),
		_T("What the Tab key inserts. \"Insert spaces\" writes spaces, \"Keep tabs\" writes a tab character."),
		s_tabExpand, (int)_countof(s_tabExpand),
		s->GetTabExpand() ? 0 : 1);

	// InsertNumberEdit() takes (value, spinStep, lower, upper): the 1..32 limit is applied
	// both as the spin range and as the value limit, so a typed 0 is clamped on commit too.
	rows.m_pTabSize = InsertNumberEdit(m_tree, pGroup,
		_T("Tab size"),
		_T("How many columns a tab character is worth. 0 would hang the editor, so the row does not go below 1."),
		s->GetTabSpacing(), 1 /*spinStep*/, s_spacingMin, s_spacingMax);

	rows.m_pIndentSize = InsertNumberEdit(m_tree, pGroup,
		_T("Indent size"),
		_T("How many columns one indent step moves, and how many spaces \"Insert spaces\" writes for it. 0 would hang the editor, so the row does not go below 1."),
		s->GetIndentSpacing(), 1 /*spinStep*/, s_spacingMin, s_spacingMax);

	// Which list - and so whether "Smart" is a choice at all - follows the category's
	// language; see categoryOffersSmartIndent(). Index == value in both lists, so the
	// stored EIndent goes in unmapped either way.
	const bool offersSmart = categoryOffersSmartIndent(s);

	rows.m_pIndentType = InsertComboIndex(m_tree, pGroup,
		_T("Auto indent"),
		indentTypeInfoText(offersSmart),
		offersSmart ? s_PlSqlIndentType : s_indentType,
		offersSmart ? (int)_countof(s_PlSqlIndentType) : (int)_countof(s_indentType),
		s->GetIndentType());
}

// The column marker list of one row, parsed the way the classic OnChangeData() parsed it:
// every comma becomes a space and the stream then reads integers, each of them a 1 based
// column number that is stored 0 based. Returns false - with markers cleared - on a token
// that is not a number, the only way the classic input could fail.
static bool parseColumnMarkers (const CString& text, std::vector<int>& markers)
{
	std::string str = Common::str(text);

	for (std::string::iterator it = str.begin(); it != str.end(); ++it)
		if (*it == ',')
			*it = ' ';

	std::istringstream in(str);

	markers.clear();

	while (true)
	{
		int val = 0;

		in >> val;

		if (!in)
		{
			// End of input is the normal way out, and also what an empty row gives - which
			// stores no marker at all, as clearing the classic edit did.
			if (in.eof())
				break;

			return false;      // a token that is not a number
		}

		markers.push_back(val - 1);
	}

	return true;
}

// Called from Common::CPropTreePage::OnApply(), which has already committed the cell being
// edited and which reports exceptions.
//
// Every category is validated BEFORE anything is written, so a bad column marker list
// leaves the settings as they are instead of half updated - the classic page wrote each
// control as it changed and so could not do that. FALSE keeps the sheet open.
BOOL COEClassPageTree::ApplyValues ()
{
	if (m_rows.empty())
		return TRUE; // the page was never built

	// Pass 1: parse what can fail, one vector per category, kept until pass 2 writes it.
	std::vector<std::vector<int> > markers(m_rows.size());

	for (size_t i = 0; i < m_rows.size(); i++)
	{
		if (!parseColumnMarkers(m_rows[i].m_pColumnMarkers->GetItemValue(), markers[i]))
		{
			// The classic message, with the category named: this page holds the marker list
			// of every category at once, so "which one" is part of the answer. Reported and
			// then FALSE rather than the classic AfxThrowUserException(), because the base
			// OnApply() would catch that exception and report it a second time.
			CString text;

			text.Format(_T("Cannot parse the column markers of the \"%s\" category. Please use ',' or ' ' as delimiters."),
				m_rows[i].m_pGroup->GetLabelText());

			AfxMessageBox(text, MB_OK | MB_ICONSTOP);
			return FALSE;
		}
	}

	// Pass 2: write. The same accessors, in the same order and with the same "do not
	// notify" flags as the classic COEClassPage::OnChangeData() - including
	// SetColumnMarkersSet(), the one setter the classic page called without the flag and
	// which therefore does notify.
	for (size_t i = 0; i < m_rows.size(); i++)
	{
		const CategoryRows&     rows = m_rows[i];
		const ClassSettingsPtr& s    = rows.m_settings;

		// to_unprintable_str() fills an out parameter, it does not return the string.
		std::string delimiters;
		Common::to_unprintable_str(Common::str(rows.m_pDelimiters->GetItemValue()).c_str(), delimiters);

		s->SetDelimiters(delimiters, false /*notify*/);
		s->SetExtensions(Common::str(rows.m_pExtensions->GetItemValue()), false /*notify*/);
		s->SetIndentSpacing(_wtoi(rows.m_pIndentSize->GetItemValue()), false /*notify*/);
		s->SetTabSpacing(_wtoi(rows.m_pTabSize->GetItemValue()), false /*notify*/);

		// Index == value for all four lists - see s_indentType / s_PlSqlIndentType and
		// s_tabExpand. Nothing out of range can arrive: InsertComboIndex() clamped what was
		// loaded, and a CBS_DROPDOWNLIST offers no entry to type. The two mappings are the classic
		// `if (IsDlgButtonChecked(...SMART)) SetIndentType(2); else if (...DEFAULT)
		// SetIndentType(1); else SetIndentType(0);` and
		// `SetIndentType(0)` / `SetTabExpand(IsDlgButtonChecked(IDC_OEC_INSERT_SPACES))`,
		// written the way round the lists read.
		s->SetIndentType((int)rows.m_pIndentType->GetItemValue(), false /*notify*/);
		s->SetTabExpand((int)rows.m_pTabExpand->GetItemValue() == 0, false /*notify*/);

		// The "Default" entry in front of the classic three, so index - 1 is the
		// EFileFormat - effDefault (-1) included. See s_fileCreateAs.
		s->SetFileCreateAs((int)rows.m_pFileCreateAs->GetItemValue() - s_fileCreateAsOffset, false /*notify*/);
		s->SetNormalizeKeywords(rows.m_pNormalizeKeywords->IsChecked() ? true : false, false /*notify*/);

		s->SetColumnMarkersSet(markers[i]);
	}

	return TRUE;
}

// Called from Common::CPropTreePage::OnApply() and from ::OnCancel(), so the groups stay
// expanded the way the user left them - also when the sheet is cancelled.
void COEClassPageTree::SaveTreeState ()
{
	// The page may never have been shown, hence the empty test; it also keeps the state a
	// previous opening stored instead of clearing it.
	if (m_rows.empty())
		return;

	s_groupExpanded.assign(m_rows.size(), TRUE);

	for (size_t i = 0; i < m_rows.size(); i++)
		if (m_rows[i].m_pGroup)
			s_groupExpanded[i] = m_rows[i].m_pGroup->IsExpanded();
}

