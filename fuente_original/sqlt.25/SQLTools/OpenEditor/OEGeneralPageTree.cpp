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

// 2026-09-24 replaces COEGeneralPage, see the header

#include "stdafx.h"
#include "OpenEditor/OEGeneralPageTree.h"
#include "Common/PropTreePageHelpers.h"
#include "Common/MyUtf.h"

#include <algorithm>
#include <sstream>

	using namespace Common::PropTreeRows;

// Group expansion state, kept across dialog re-openings (the pattern used by
// COEEditingPageTree): written by SaveTreeState(), read by BuildTree().
static BOOL s_bGeneralExpanded = TRUE;
static BOOL s_bFindReplaceExpanded = TRUE;
static BOOL s_bMdiTabsExpanded = TRUE;

// The locale presets of the classic IDC_OE_GEN_LOCALE, decoded from the DLGINIT
// block of IDD_OE_GENERAL in OpenEditorInc.rc - a code built page cannot use
// DLGINIT, so the strings are declared here. They are in the alphabetical order
// that CBS_SORT gave the classic drop down; the order is NOT part of the setting,
// because the row stores the selected text.
static const LPCTSTR s_localeList[] =
{
	_T("Belgian"),
	_T("Czech"),
	_T("Danish"),
	_T("Dutch"),
	_T("English"),
	_T("English-Aus"),
	_T("English-Can"),
	_T("English-Nz"),
	_T("English-Uk"),
	_T("English-Us"),
	_T("Finnish"),
	_T("French"),
	_T("French-Belgian"),
	_T("French-Canadian"),
	_T("French-Swiss"),
	_T("German"),
	_T("German-Austrian"),
	_T("German-Swiss"),
	_T("Greek"),
	_T("Hungarian"),
	_T("Icelandic"),
	_T("Italian"),
	_T("Italian-Swiss"),
	_T("Norwegian"),
	_T("Norwegian-Bokmal"),
	_T("Norwegian-Nynorsk"),
	_T("Polish"),
	_T("Portuguese"),
	_T("Portuguese-Brazil"),
	_T("Russian"),
	_T("Slovak"),
	_T("Spanish"),
	_T("Spanish-Mexican"),
	_T("Spanish-Modern"),
	_T("Swedish"),
	_T("Turkish"),
};

// The "Default encoding" list the classic COEGeneralPage::OnInitDialog() filled
// IDC_OE_GEN_ENCODING with, in that order - and the order IS the stored value
// here, an index into OpenEditor::EFileEncoding.
static const LPCTSTR s_encodingList[] =
{
	_T("ANSI (Current Windows codepage)"),
	_T("UTF-8"),
	_T("UTF-8 with BOM"),
	_T("UTF-16 LE with BOM"),
};

// Any value in the WM_APP range. The window is created and owned by this page and
// has no messages of its own in that range.
//
// The commit-on-select id this page used to declare beside it moved into
// Common\PropTreeItemComboOnSelect.cpp together with the class, once
// COEBackupPageTree needed the same row: two file local copies of one
// external-linkage MFC class collide at link time, so the second page has to
// share the first one's definition instead of repeating it.
static const UINT WM_OEG_KEYMAP_WARNING    = WM_APP + 42;

// Leaves a drop down LIST row with no selection, which is what the classic
// IDC_OE_GEN_LOCALE showed for a locale that is in none of its 36 presets.
// InsertComboIndex() clamps an out of range index to 0, so the row is seeded
// first and put back here. SetItemValue(-1) alone is not enough:
// CPropTreeItemCombo::OnRefresh() only ever SETS a selection - FindCBData() fails
// on -1 and it returns without clearing the one the clamped seed left behind -
// and the cell draws GetCurSel(), not the stored value.
static void SetNoSelection (CPropTreeItemCombo* p)
{
	p->SetItemValue(-1);
	p->SetCurSel(-1);
}

// The text a "stores text" pick list row holds: the text of the entry its combo has
// selected, or an empty string while nothing is selected. Read from GetCurSel() - the
// very thing CPropTreeItemCombo::OnCommit() and ::DrawAttribute() use - rather than
// from GetItemValue(), because OnCommit() maps a cell with NO selection to item data
// 0: reading the stored value would return the first preset for an unmatched locale
// while the cell still draws blank. Empty is also what the classic combo's
// GetWindowText() returned with no selection, so an unmatched locale survives a trip
// through the page unchanged instead of turning into an entry of the list. The list
// passed here is the very one the row was built from, which is what keeps the index
// meaningful.
static CString GetListText (CPropTreeItemCombo* pRow, const LPCTSTR* items, int itemCount)
{
	const LONG nSel = pRow->GetCurSel();

	if (nSel >= 0 && nSel < itemCount)
		return CString(items[nSel]);

	return CString();
}

static CString GetListText (CPropTreeItemCombo* pRow, const std::vector<CString>& items)
{
	const LONG nSel = pRow->GetCurSel();

	if (nSel >= 0 && nSel < (LONG)items.size())
		return items[(size_t)nSel];

	return CString();
}

// The case insensitive order CBS_SORT gave the classic "Keymap layout" drop down.
static bool CompareLayoutsNoCase (const CString& a, const CString& b)
{
	return _tcsicmp(a, b) < 0;
}

////////////////////////////////////////////////////////////////////////////////
// The page.

BEGIN_MESSAGE_MAP(COEGeneralPageTree, Common::CPropTreePage)
	// PTN_ITEMCHANGED and PTN_CHECKCLICK are entries of the base class map. This one
	// is the page's own: the restart warning of the "Keymap layout" row, posted
	// rather than shown from OnItemChanged() - see the header.
	ON_MESSAGE(WM_OEG_KEYMAP_WARNING, OnKeymapLayoutWarning)
END_MESSAGE_MAP()

COEGeneralPageTree::COEGeneralPageTree (SettingsManager& manager)
	: Common::CPropTreePage(_T("Editor::General"), COEGeneralPageTree::IDD)
	, m_manager(manager)
	, m_pGeneralGroup(nullptr)
	, m_pFindReplaceGroup(nullptr)
	, m_pMdiTabsGroup(nullptr)
	, m_pAllowMultipleInstances(nullptr)
	, m_pNewDocOnStartup(nullptr)
	, m_pWorkDirFollowsDoc(nullptr)
	, m_pSaveMainWindowPosition(nullptr)
	, m_pKeymapLayout(nullptr)
	, m_pLocale(nullptr)
	, m_pEncoding(nullptr)
	, m_pAsciiAsUtf8(nullptr)
	, m_pUnsupportedAsianChars(nullptr)
	, m_pFindCloseOnDeactivate(nullptr)
	, m_pFindDisableLivePreview(nullptr)
	, m_pTabsOnTop(nullptr)
	, m_pTabsCloseButton(nullptr)
	, m_pTabsAutoColor(nullptr)
	, m_pTabs3DLook(nullptr)
	, m_pTabsListMenuButton(nullptr)
	, m_pTabsCtrlTabSwitchesToPrevActive(nullptr)
	, m_lKeymapLayout(-1)
{
	// The classic page cleared PSP_HASHELP itself, and Help\sqltools.hhp still has no
	// [ALIAS] entry for it - Help\HTMLDefines.h defines HIDD_OE_GENERAL, but nothing
	// maps that to a topic - so keep the sheet's Help button disabled while this page
	// is active. Otherwise Help reports "topic not found".
	SetPageHelp(FALSE);
}

COEGeneralPageTree::~COEGeneralPageTree ()
{
}

// Tree creation and layout, the PTN_* notifications, the resize handling, the
// commit-before-read on Apply and the expansion state on Cancel all come from
// Common::CPropTreePage - this file only holds the page content.

void COEGeneralPageTree::BuildTree ()
{
	const OpenEditor::GlobalSettingsPtr s = m_manager.GetGlobalSettings();

	// -- The controls that stood OUTSIDE the one group box of IDD_OE_GENERAL ------
	//
	// They cannot be root level rows. CPropTreeItem::DrawItem() treats a root level
	// item as a group HEADER: it shades the row (COLOR_BTNFACE), draws the label in
	// the bold font, spans the label area to the full width - drc.right = rc.right
	// instead of the attribute column - draws no column separator, and finally
	// guards the value cell with "if (!IsRootLevel()) DrawAttribute(...)".
	//
	// So a root level combo shows its label and NO value: CPropTreeItemCombo creates
	// its CComboBox hidden (CreateComboBox() masks off WS_VISIBLE) and the cell is
	// painted by DrawAttribute(), which for a root level row never runs. A root level
	// check box still works, because the box glyph is drawn in the label area, but it
	// comes out bold on the gray band and reads like a header.
	//
	// Every value row of a tree page therefore has to sit under a group - which is
	// why the six other InsertComboIndex() calls in the codebase (DDLPageTree,
	// GridPageTree, PropServerScriptPageTree) all pass a parent group.
	//
	// The page is already called General, so the group repeats the node caption: the
	// convention COEEditingPageTree follows with its own "Editing" node.
	m_pGeneralGroup = InsertGroup(m_tree, _T("General"),
		_T("Startup, window, key binding, locale and encoding settings of the editor."),
		s_bGeneralExpanded);

	m_pAllowMultipleInstances = InsertCheckedStatic(m_tree, m_pGeneralGroup,
		_T("Allow multiple instances to run"),
		_T("Let another copy of SQLTools start. While this is off, starting SQLTools again activates the copy that is already running."),
		s->GetAllowMultipleInstances());

	m_pNewDocOnStartup = InsertCheckedStatic(m_tree, m_pGeneralGroup,
		_T("Open new document on startup"),
		_T("Open an empty editor document when SQLTools starts with no file to open, so there is somewhere to type."),
		s->GetNewDocOnStartup());

	m_pWorkDirFollowsDoc = InsertCheckedStatic(m_tree, m_pGeneralGroup,
		_T("Working directory follows active document"),
		_T("Make the current directory the folder of the active document, so the file dialogs and the relative paths start there."),
		s->GetWorkDirFollowsDocument());

	m_pSaveMainWindowPosition = InsertCheckedStatic(m_tree, m_pGeneralGroup,
		_T("Save main window position on exit"),
		_T("Remember where the main window was and how big it was, and restore that on the next start."),
		s->GetSaveMainWinPosition());

	// The layout list is OpenEditor::KeymapLayoutList, which
	// CSQLToolsApp::InitInstance() has replaced with the layouts SQLTools ships by the
	// time this page is built. Parsed as the classic ParseKeymapLayoutList() parsed
	// it - semicolon delimited, empty tokens skipped - and then sorted the way
	// CBS_SORT sorted the classic drop down.
	{
		std::istringstream in(OpenEditor::KeymapLayoutList);
		std::string tok;

		while (std::getline(in, tok, ';'))
			if (tok.length())
				m_keymapLayouts.push_back(CString(Common::wstr(tok).c_str()));

		std::sort(m_keymapLayouts.begin(), m_keymapLayouts.end(), CompareLayoutsNoCase);
	}

	// The row builder wants the entries as an array of pointers. m_keymapLayouts is
	// not touched again once it is filled, so the pointers stay good for the life of
	// the page - ApplyValues() maps the selected index back through the same vector.
	std::vector<LPCTSTR> layouts;
	layouts.reserve(m_keymapLayouts.size());

	for (size_t i = 0; i < m_keymapLayouts.size(); i++)
		layouts.push_back((LPCTSTR)m_keymapLayouts[i]);

	// The classic page selected the stored layout with CB_FINDSTRINGEXACT and fell back
	// to the FIRST entry when it was in none of them; that entry is what it wrote back
	// on Apply, and what this row writes back too.
	const CString strKeymap(Common::wstr(s->GetKeymapLayout()).c_str());
	int nKeymap = 0;

	for (size_t i = 0; i < m_keymapLayouts.size(); i++)
		if (_tcsicmp(m_keymapLayouts[i], strKeymap) == 0)
		{
			nKeymap = (int)i;
			break;
		}

	m_pKeymapLayout = InsertComboIndexOnSelect(m_tree, m_pGeneralGroup,
		_T("Keymap layout"),
		_T("The set of editor key bindings. Takes effect only after you exit and restart SQLTools."),
		layouts.empty() ? NULL : &layouts[0], (int)layouts.size(), nKeymap);

	// The value the row is seeded with, so OnItemChanged() can tell a change from a
	// commit that changed nothing.
	m_lKeymapLayout = m_pKeymapLayout->GetItemValue();

	// The classic page selected the stored locale with CB_FINDSTRINGEXACT and, when it
	// was in none of the presets, left the combo with NO selection - which wrote an
	// empty locale back on Apply. SetNoSelection() below reproduces that, so an unknown
	// locale survives a trip through this page instead of turning into the first entry
	// of the list.
	const CString strLocale(Common::wstr(s->GetLocale()).c_str());
	int nLocale = -1;

	for (int i = 0; i < (int)_countof(s_localeList); i++)
		if (_tcsicmp(s_localeList[i], strLocale) == 0)
		{
			nLocale = i;
			break;
		}

	// The "Locale affects on upper/lower/sort/... operations." static of the classic
	// template is this row's info text - a tree page has no room for a caption next to
	// a control, and the info pane is where a page explains itself.
	m_pLocale = InsertComboIndex(m_tree, m_pGeneralGroup,
		_T("Locale"),
		_T("The locale the editor's upper, lower, sort and similar text operations use."),
		s_localeList, (int)_countof(s_localeList), nLocale);

	if (nLocale < 0)
		SetNoSelection(m_pLocale);

	m_pEncoding = InsertComboIndexOnSelect(m_tree, m_pGeneralGroup,
		_T("Default encoding"),
		_T("The encoding an editor document uses by default. This row stores the index of the list, so the order of the four entries is part of the setting."),
		s_encodingList, (int)_countof(s_encodingList), (int)s->GetEncoding());

	// The classic label read "Open ASCII (7-bit) files as UFT-8"; the typo is not
	// reproduced. The one dependent row of the page, so it is indented one level under
	// "Default encoding" and grayed out while the encoding is anything else - see
	// UpdateEnabledState().
	m_pAsciiAsUtf8 = InsertCheckedStatic(m_tree, m_pGeneralGroup,
		_T("Open ASCII (7-bit) files as UTF-8"),
		_T("Read a file that holds nothing but 7-bit ASCII characters as UTF-8. Consulted only while the default encoding is UTF-8."),
		s->GetAsciiAsUtf8());
	m_pAsciiAsUtf8->SetIndent(CPropTreeItem::GetLevelIndent());

	// Stood BELOW the "  MDI Tabs  " group box in the classic template, outside every
	// group box, and it is not about tabs: COEditorView::TextOutToScreen() reads it.
	// It is the last row of the "General" group rather than a root level row after
	// "MDI Tabs", because a root level row draws as a bold, shaded group header with
	// no value cell - see the note where the group is built. The one visible cost is
	// that it now reads before "MDI Tabs" instead of after it.
	m_pUnsupportedAsianChars = InsertCheckedStatic(m_tree, m_pGeneralGroup,
		_T("Show unsupported Asian characters"),
		_T("Draw double width Asian and right to left characters as ordinary text. While this is off, a character wider than the editor's fixed width cell is detected and marked instead of drawn out of place."),
		s->GetUnsupportedAsianChars());

	// -- The "Find/Replace" group ----------------------------------------------
	// Find/Replace dialog behaviour. Not from the classic template - added with the
	// modeless conversion (md/editor_search_modeless_conversion_plan.md section 8),
	// both default to unchecked = the modern modeless behaviour. Read where they act:
	// CFindReplaceDlg (OEFindReplaceDlg.cpp). They get their own group rather than
	// extending the General one; it sits between "General" and "MDI Tabs" -
	// InsertItem appends to the parent list, so the creation order below IS the
	// on-screen order.
	m_pFindReplaceGroup = InsertGroup(m_tree, _T("Find/Replace"),
		_T("How the modeless Find/Replace dialog behaves."),
		s_bFindReplaceExpanded);

	m_pFindCloseOnDeactivate = InsertCheckedStatic(m_tree, m_pFindReplaceGroup,
		_T("Close Find/Replace dialog when it loses focus"),
		_T("Close the modeless Find/Replace dialog when the focus moves away from both the dialog and the editor being searched - the classic dialog behaviour. While this is off, the dialog stays open until Esc, the caption X or the Close button dismisses it."),
		s->GetFindCloseOnDeactivate());

	m_pFindDisableLivePreview = InsertCheckedStatic(m_tree, m_pFindReplaceGroup,
		_T("Disable search-as-you-type live preview"),
		_T("Search only on Enter, F3, Shift+F3 and the dialog buttons. While this is off, typing in the Find/Replace dialog searches after a short pause and shows the match count and the first hit live."),
		s->GetFindDisableLivePreview());

	// -- The "  MDI Tabs  " group box of IDD_OE_GENERAL --------------------------
	m_pMdiTabsGroup = InsertGroup(m_tree, _T("MDI Tabs"),
		_T("How the document tab strip of the main window looks and behaves."),
		s_bMdiTabsExpanded);

	// Row-major reading order of the template's two columns: "Tabs on Top" and "Active
	// Tab has Close button" were the top of the left and of the right column, "Tabs
	// Auto-Color" and "Rectangular Tabs" the second row of each. A tree has one column,
	// so the pairs of the template become consecutive rows.
	m_pTabsOnTop = InsertCheckedStatic(m_tree, m_pMdiTabsGroup,
		_T("Tabs on Top"),
		_T("Show the document tabs above the editor area instead of below it."),
		s->GetMDITabsOnTop());

	m_pTabsCloseButton = InsertCheckedStatic(m_tree, m_pMdiTabsGroup,
		_T("Active Tab has \"Close\" button"),
		_T("Put a close button on the tab of the active document."),
		s->GetMDITabsActiveTabCloseButton());

	// CMFCTabCtrl::m_bAutoColor, read by CMainFrame from the tab parameters.
	m_pTabsAutoColor = InsertCheckedStatic(m_tree, m_pMdiTabsGroup,
		_T("Tabs Auto-Color"),
		_T("Let the tab control colour the document tabs automatically, so that neighbouring tabs are easier to tell apart."),
		s->GetMDITabsAutoColor());

	// CMFCTabCtrl::STYLE_3D instead of STYLE_3D_ONENOTE, read by CMainFrame.
	m_pTabs3DLook = InsertCheckedStatic(m_tree, m_pMdiTabsGroup,
		_T("Rectangular Tabs"),
		_T("Draw the document tabs as classic 3D rectangles instead of in the rounded one-note style."),
		s->GetMDITabsRectangularLook());

	// CMFCTabCtrl::m_bDocumentMenu, read by CMainFrame.
	m_pTabsListMenuButton = InsertCheckedStatic(m_tree, m_pMdiTabsGroup,
		_T("Document List button instead of Scroll buttons"),
		_T("Replace the tab scroll buttons with a button that drops down the list of the open documents."),
		s->GetMDITabsDocumentMenuButton());

	m_pTabsCtrlTabSwitchesToPrevActive = InsertCheckedStatic(m_tree, m_pMdiTabsGroup,
		_T("The first Ctrl+Tab switches to the previously active window"),
		_T("Make the first Ctrl+Tab go to the document that was active before this one, instead of to the next tab."),
		s->GetMDITabsCtrlTabSwitchesToPrevActive());

	UpdateEnabledState();
}

// The classic page enabled IDC_OE_GEN_ASCII_AS_UTF8 only while the encoding combo
// stood on UTF-8, and COEDocument consults the flag in
// "GetEncoding() == feUTF8 && GetAsciiAsUtf8()" alone, so it means nothing for any
// other encoding. ReadOnly() is the tree's EnableWindow(); the row is still written
// back while read only, because the classic DDX_Check exchanged the value of its
// disabled control too.
void COEGeneralPageTree::UpdateEnabledState ()
{
	if (!m_pEncoding || !m_pAsciiAsUtf8)
		return;

	m_pAsciiAsUtf8->ReadOnly(m_pEncoding->GetItemValue() != OpenEditor::feUTF8);

	// The rows are painted by the child CPropTreeList window, so its children have to
	// be invalidated too - see Common::CPropTreePage::RedrawTree().
	RedrawTree();
}

void COEGeneralPageTree::OnItemChanged (CPropTreeItem* pItem)
{
	if (pItem == m_pLocale)
	{
		// CPropTreeItemCombo::OnCommit() maps a cell with NO selection to item data 0.
		// Left alone, an unmatched locale would show the first preset selected the next
		// time the cell is activated, because OnActivate() does
		// FindCBData(m_lComboData). Put back the -1 that SetNoSelection() established;
		// this runs inside CommitChanges(), ahead of its RefreshItems().
		if (m_pLocale->GetCurSel() == CB_ERR)
			m_pLocale->SetItemValue(-1);
	}
	else if (pItem == m_pEncoding)
	{
		UpdateEnabledState();
	}
	else if (pItem == m_pKeymapLayout)
	{
		// The cell commits on a pick - that is what CPropTreeItemComboOnSelect is for -
		// but CPropTreeItemCombo also commits it on kill-focus and on Enter, and either
		// can commit it unchanged. The classic page warned on CBN_SELCHANGE only, so the
		// value is compared against the one last seen here; that also keeps a second
		// warning from following a first.
		const LPARAM lData = m_pKeymapLayout->GetItemValue();

		if (lData != m_lKeymapLayout)
		{
			m_lKeymapLayout = lData;
			PostMessage(WM_OEG_KEYMAP_WARNING, 0, 0);
		}
	}
}

LRESULT COEGeneralPageTree::OnKeymapLayoutWarning (WPARAM, LPARAM)
{
	// The text of the classic COEGeneralPage::ShowWarningAfterExitAndRestart(). Posted
	// from OnItemChanged() instead of being shown there, because that handler runs
	// inside CPropTreeItem::CommitChanges() - before CPropTree::RefreshItems() has
	// repainted the row - and a modal box raised there would sit over a cell still
	// showing the previous value.
	AfxMessageBox(_T("This setting takes effect only after you exit and restart!"));
	return 0;
}

// Called from Common::CPropTreePage::OnApply(), which has already committed the cell
// being edited and which reports exceptions.
BOOL COEGeneralPageTree::ApplyValues ()
{
	if (!m_pAllowMultipleInstances)
		return TRUE; // the page was never built

	OpenEditor::GlobalSettingsPtr settings = m_manager.GetGlobalSettings();

	settings->SetAllowMultipleInstances(m_pAllowMultipleInstances->IsChecked() ? true : false, false /*notify*/);
	settings->SetNewDocOnStartup       (m_pNewDocOnStartup->IsChecked()        ? true : false, false /*notify*/);
	settings->SetWorkDirFollowsDocument(m_pWorkDirFollowsDoc->IsChecked()      ? true : false, false /*notify*/);
	settings->SetSaveMainWinPosition   (m_pSaveMainWindowPosition->IsChecked() ? true : false, false /*notify*/);

	// Both pick lists store the TEXT of the selected entry, as the classic combos did,
	// and an unselected "Locale" stores an empty string - see GetListText().
	// Common::str() is the UTF-8 side of the Common::wstr() that read them in
	// BuildTree(); the classic page read both with a plain .c_str(), which went through
	// the ANSI conversion instead.
	settings->SetKeymapLayout(Common::str(GetListText(m_pKeymapLayout, m_keymapLayouts)), false /*notify*/);
	settings->SetLocale(Common::str(GetListText(m_pLocale, s_localeList,
		(int)_countof(s_localeList))), false /*notify*/);

	// The encoding row stores the list index, which is the OpenEditor::EFileEncoding
	// value the classic page stored in its m_Encoding.
	settings->SetEncoding((UINT)m_pEncoding->GetItemValue(), false /*notify*/);
	settings->SetAsciiAsUtf8(m_pAsciiAsUtf8->IsChecked() ? true : false, false /*notify*/);

	settings->SetMDITabsOnTop(               m_pTabsOnTop->IsChecked()          ? true : false, false /*notify*/);
	settings->SetMDITabsAutoColor(           m_pTabsAutoColor->IsChecked()      ? true : false, false /*notify*/);
	settings->SetMDITabsDocumentMenuButton(  m_pTabsListMenuButton->IsChecked() ? true : false, false /*notify*/);
	settings->SetMDITabsActiveTabCloseButton(m_pTabsCloseButton->IsChecked()    ? true : false, false /*notify*/);
	settings->SetMDITabsRectangularLook(     m_pTabs3DLook->IsChecked()         ? true : false, false /*notify*/);
	settings->SetMDITabsCtrlTabSwitchesToPrevActive(
		m_pTabsCtrlTabSwitchesToPrevActive->IsChecked() ? true : false, false /*notify*/);

	settings->SetUnsupportedAsianChars(m_pUnsupportedAsianChars->IsChecked() ? true : false, false /*notify*/);

	settings->SetFindCloseOnDeactivate (m_pFindCloseOnDeactivate->IsChecked()  ? true : false, false /*notify*/);
	settings->SetFindDisableLivePreview(m_pFindDisableLivePreview->IsChecked() ? true : false, false /*notify*/);

	return TRUE;
}

// Called from Common::CPropTreePage::OnApply() and from ::OnCancel(), so the groups
// stay expanded the way the user left them - also when the sheet is cancelled.
void COEGeneralPageTree::SaveTreeState ()
{
	// The page may never have been shown, hence the null check.
	if (!m_pGeneralGroup || !m_pFindReplaceGroup || !m_pMdiTabsGroup)
		return;

	s_bGeneralExpanded = m_pGeneralGroup->IsExpanded();
	s_bFindReplaceExpanded = m_pFindReplaceGroup->IsExpanded();
	s_bMdiTabsExpanded = m_pMdiTabsGroup->IsExpanded();
}
