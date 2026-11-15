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

// 2026-09-22 replaces CObjectViewerPage, see the header

#include "stdafx.h"
#include "Dialogs/ObjectViewerPageTree.h"
#include "Common/PropTreePageHelpers.h"

	using namespace Common::PropTreeRows;

// Group expansion state, kept across dialog re-openings: written by
// SaveTreeState(), read by BuildTree().
static BOOL s_bObjectViewerExpanded = TRUE;
static BOOL s_bObjectListExpanded   = TRUE;

// The classic edit was validated with DDV_MinMaxUInt(..., 0, 1000) and its spin
// buddy got the same range through UDM_SETRANGE32. In the tree those two numbers
// are the item limit - typed text is clamped on commit - and the spin range. The
// classic spin had no UDACCEL table, so its step is the default 1.
static const int s_maxLineLengthMin  = 0;
static const int s_maxLineLengthMax  = 1000;
static const int s_maxLineLengthStep = 1;

CObjectViewerPageTree::CObjectViewerPageTree (SQLToolsSettings& settings)
	: Common::CPropTreePage(_T("SQLTools::Object Viewer / List"), 0 /*no help topic*/),
	m_settings(settings),
	m_pObjectViewerGroup(nullptr), m_pObjectListGroup(nullptr),
	m_pUseCache(nullptr), m_pClassicTreeLook(nullptr), m_pInfotips(nullptr),
	m_pShowComments(nullptr), m_pSortTabCol(nullptr), m_pSortPkgProc(nullptr),
	m_pCopyProcWithPkgName(nullptr), m_pMaxLineLength(nullptr),
	m_pOvQueryInActiveWindow(nullptr), m_pRetainFocusInEditor(nullptr),
	m_pCopyPasteOnDoubleClick(nullptr),
	m_pSchemasWithObjects(nullptr), m_pComboSelector(nullptr), m_pTabIconsOnly(nullptr),
	m_pSynonymWoObjectInvalid(nullptr), m_pOlQueryInActiveWindow(nullptr)
{
	// The 0 help id above clears PSP_HASHELP, which is what the classic page did
	// explicitly (m_psp.dwFlags &= ~PSP_HASHELP): Help\HTMLDefines.h has
	// HIDD_PROP_OBJECT_VIEWER, but Help\sqltools.hhp has no [ALIAS] entry for it.
}

CObjectViewerPageTree::~CObjectViewerPageTree ()
{
}

void CObjectViewerPageTree::BuildTree ()
{
	// -- Object Viewer (the "  Object Viewer  " group box of IDD_PROP_OBJECT_VIEWER)
	m_pObjectViewerGroup = InsertGroup(m_tree, _T("Object Viewer"),
		_T("The object viewer tree: how it is drawn, what it loads, and what a double click does."),
		s_bObjectViewerExpanded);

	// CTreeViewer::ShowObject() takes the object pool of the session cache only
	// while BOTH ObjectCache and ObjectViewerUseCache are on, which is why this
	// row is read only when the other one is off - see UpdateEnabledState().
	m_pUseCache = InsertCheckedStatic(m_tree, m_pObjectViewerGroup,
		_T("Use cache"),
		_T("Build the object tree from the definitions the session has already collected instead of reading them again. Available only while the object cache is enabled on the Session and Execution page."),
		m_settings.GetObjectViewerUseCache());

	// CTreeViewer::SetClassicTreeLook(): TVS_HASLINES instead of TVS_FULLROWSELECT,
	// and the custom draw handler of the tree returns at once.
	m_pClassicTreeLook = InsertCheckedStatic(m_tree, m_pObjectViewerGroup,
		_T("Classic tree look"),
		_T("Draw the tree with lines between its items and highlight a single item, like the classic Windows tree control. Turned off, the whole row is highlighted and the items are drawn with the custom colors."),
		m_settings.GetObjectViewerClassicTreeLook());

	// CObjectViewerWnd::m_infoTip: the tool tip window of the tree.
	m_pInfotips = InsertCheckedStatic(m_tree, m_pObjectViewerGroup,
		_T("Infotips"),
		_T("Show a tool tip with the details of an object while the mouse rests on its tree item."),
		m_settings.GetObjectViewerInfotips());

	// ObjectTree::Column::GetLabel() / TableBase::GetLabel() append the comment.
	m_pShowComments = InsertCheckedStatic(m_tree, m_pObjectViewerGroup,
		_T("Show table / column comments"),
		_T("Append the COMMENT ON TABLE / VIEW / COLUMN text to the labels of the tree."),
		m_settings.GetObjectViewerShowComments());

	// ObjectTree_Builder_2.cpp: the child nodes are sorted by name.
	m_pSortTabCol = InsertCheckedStatic(m_tree, m_pObjectViewerGroup,
		_T("Sort table and view columns alphabetically"),
		_T("Sort the columns of a table or a view by name. Turned off, they are listed in the order of the table definition."),
		m_settings.GetObjectViewerSortTabColAlphabetically());

	m_pSortPkgProc = InsertCheckedStatic(m_tree, m_pObjectViewerGroup,
		_T("Sort package procedures and functions alphabetically"),
		_T("Sort the subprograms of a package by name. Turned off, they are listed in the order of the package specification."),
		m_settings.GetObjectViewerSortPkgProcAlphabetically());

	// ObjectTree::PackageProcedure::GetTextToCopy() prefixes schema.package.
	m_pCopyProcWithPkgName = InsertCheckedStatic(m_tree, m_pObjectViewerGroup,
		_T("Copy procedure with package name"),
		_T("Prefix the name of a package procedure or function with its package name when it is copied or dragged into the editor."),
		m_settings.GetObjectViewerCopyProcedureWithPackageName());

	// ObjectTree::TextFormatter::m_max_line_length: Put() starts a new line once
	// the buffer would grow beyond it. The classic caption of this row was
	// "Number of charcters in Copy/Drag&&Drop line:" - the tree labels are drawn
	// with DT_NOPREFIX, so the mnemonic ampersands are gone and one of them is a
	// literal character now.
	m_pMaxLineLength = InsertNumberEdit(m_tree, m_pObjectViewerGroup,
		_T("Number of characters in Copy/Drag&Drop line"),
		_T("Maximum length of a line of the text an item produces when it is copied or dragged into the editor; a longer list is wrapped over several lines."),
		m_settings.GetObjectViewerMaxLineLength(), s_maxLineLengthStep,
		s_maxLineLengthMin, s_maxLineLengthMax);

	// CPLSWorksheetDoc::DoSqlQuery(..., bInActiveWindow) of the quick queries.
	m_pOvQueryInActiveWindow = InsertCheckedStatic(m_tree, m_pObjectViewerGroup,
		_T("Run query in the active window"),
		_T("Run the quick queries of the object viewer in the worksheet that is currently active instead of opening a new one."),
		m_settings.GetObjectViewerQueryInActiveWindow());

	// CSQLWorksheetDoc: viewer.SetFocus() is skipped while this is on.
	m_pRetainFocusInEditor = InsertCheckedStatic(m_tree, m_pObjectViewerGroup,
		_T("Retain the focus in the editor"),
		_T("Keep the keyboard focus in the editor after the object viewer has been opened from it."),
		m_settings.GetObjectViewerRetainFocusInEditor());

	// CTreeViewer::OnLButtonDblClk() inserts the item at the cursor of the editor.
	m_pCopyPasteOnDoubleClick = InsertCheckedStatic(m_tree, m_pObjectViewerGroup,
		_T("Copy and paste on left double click"),
		_T("Insert the selected item into the editor at the cursor on a left double click. Turned off, a double click only expands or collapses an item and the Space key inserts it."),
		m_settings.GetObjectViewerCopyPasteOnDoubleClick());

	// -- Object List (the "  Object List  " group box of IDD_PROP_OBJECT_VIEWER)
	m_pObjectListGroup = InsertGroup(m_tree, _T("Object List"),
		_T("The object list window of the database browser: its schema list and its object type tabs."),
		s_bObjectListExpanded);

	// CDbSourceWnd::m_bAllSchemas = !ObjectsListOnlySchemasWithObjects.
	m_pSchemasWithObjects = InsertCheckedStatic(m_tree, m_pObjectListGroup,
		_T("Show only schemas with objects (slower)"),
		_T("List only the schemas that hold at least one object of the selected type. Turned off, every schema is listed without being queried first."),
		m_settings.GetObjectsListOnlySchemasWithObjects());

	// CDbSourceWnd::m_bUseCombo: a drop down list in place of the tab row.
	m_pComboSelector = InsertCheckedStatic(m_tree, m_pObjectListGroup,
		_T("Show object types in a dropdown instead of tabs"),
		_T("Select the object type of the list in a drop down list instead of a row of tabs."),
		m_settings.GetObjectsListUseComboSelector());

	// Dependent row: CDbSourceWnd::m_bShowTabTitles = !ObjectsListTabIconsOnly, and
	// there are no tabs at all in the drop down variant - the classic
	// OnBnClicked_ComboSelector() disabled the check box for it. SetIndent() marks
	// the dependency, UpdateEnabledState() grays the row out; the stored value is
	// written back unchanged, exactly like the classic disabled control did.
	m_pTabIconsOnly = InsertCheckedStatic(m_tree, m_pObjectListGroup,
		_T("Show only icons in object-type tabs"),
		_T("Show the object type tabs without their captions. Not available while the object types are selected in a drop down list."),
		m_settings.GetObjectsListTabIconsOnly());
	m_pTabIconsOnly->SetIndent(CPropTreeItem::GetLevelIndent()); // dependent row

	// DBBrowserSynonymList.cpp marks a synonym invalid while this is on.
	m_pSynonymWoObjectInvalid = InsertCheckedStatic(m_tree, m_pObjectListGroup,
		_T("Show synonym w/o associated object as invalid"),
		_T("Mark a synonym whose object does not exist as invalid in the synonym list. Checking every synonym needs an extra query and can be slow on some databases."),
		m_settings.GetObjectsListSynonymWithoutObjectInvalid());

	// DBBrowserTableList.cpp / DBBrowserViewList.cpp: the DoSqlQuery() flag of the
	// query a table or view list raises. Same caption as the object viewer row
	// above, but a setting of its own - the classic template had the two check
	// boxes in its two group boxes.
	m_pOlQueryInActiveWindow = InsertCheckedStatic(m_tree, m_pObjectListGroup,
		_T("Run query in the active window"),
		_T("Run the query raised from the object list - the table or view definition - in the worksheet that is currently active instead of opening a new one."),
		m_settings.GetObjectsListQueryInActiveWindow());

	UpdateEnabledState();
}

// Grays out (and makes unclickable) the rows that depend on another row or on
// another setting. A read only row cannot be toggled: CPropTreeList skips the
// click and CPropTreeItem draws it grayed out.
void CObjectViewerPageTree::UpdateEnabledState ()
{
	// The classic OnBnClicked_ComboSelector() enabled IDC_OVP_TAB_ICONS_ONLY only
	// while IDC_OVP_COMBO_SELECTOR was not checked.
	if (m_pTabIconsOnly && m_pComboSelector)
		m_pTabIconsOnly->ReadOnly(m_pComboSelector->IsChecked() ? TRUE : FALSE);

	// The tail of the classic DoDataExchange() enabled IDC_OVP_USE_CACHE with
	// m_settings.GetObjectCache(): the object viewer can reuse the session cache
	// only while the cache itself is on. ObjectCache is a row of the "Session and
	// Execution" page and cannot change while this page is open, so this test
	// always gives the same answer here; it is recomputed anyway to keep both
	// dependencies in one place.
	if (m_pUseCache)
		m_pUseCache->ReadOnly(!m_settings.GetObjectCache());

	// The rows are painted by the child CPropTreeList window, so its children have
	// to be invalidated too - see Common::CPropTreePage::RedrawTree().
	RedrawTree();
}

void CObjectViewerPageTree::OnItemChanged (CPropTreeItem* pItem)
{
	// Only the object type selector has a dependent row; the test keeps this
	// handler correct when new dependents are added.
	if (pItem == m_pComboSelector)
		UpdateEnabledState();
}

// Called from Common::CPropTreePage::OnApply(), which has already committed the
// cell being edited and which reports exceptions. There is nothing to validate
// here: the only numeric row clamps its value to the item limit BuildTree() set.
BOOL CObjectViewerPageTree::ApplyValues ()
{
	// All three sheets hand the page a COPY of the settings: SettingsDialogCallback
	// owns one and pushes it into the global settings after OK, and
	// CDbSourceWnd::OnSettings() / CTreeViewer::OnSettings() do the same with the
	// copy they took at their beginning. That is also where the single
	// NotifySettingsChanged() is raised, hence notify = false everywhere. The
	// public Set accessors are used because the m_ObjectViewer* / m_ObjectsList*
	// members are private and only the classic page is a friend of SQLToolsSettings.
	//
	// Every row is written back, the read only ones included: the classic page
	// exchanged the values of its disabled check boxes too, so a setting that is
	// not available right now keeps the value that was chosen earlier.
	m_settings.SetObjectViewerUseCache                     (m_pUseCache->IsChecked()             ? true : false, false /*notify*/);
	m_settings.SetObjectViewerClassicTreeLook              (m_pClassicTreeLook->IsChecked()      ? true : false, false /*notify*/);
	m_settings.SetObjectViewerInfotips                     (m_pInfotips->IsChecked()             ? true : false, false /*notify*/);
	m_settings.SetObjectViewerShowComments                 (m_pShowComments->IsChecked()         ? true : false, false /*notify*/);
	m_settings.SetObjectViewerSortTabColAlphabetically     (m_pSortTabCol->IsChecked()           ? true : false, false /*notify*/);
	m_settings.SetObjectViewerSortPkgProcAlphabetically    (m_pSortPkgProc->IsChecked()          ? true : false, false /*notify*/);
	m_settings.SetObjectViewerCopyProcedureWithPackageName (m_pCopyProcWithPkgName->IsChecked()  ? true : false, false /*notify*/);

	// GetItemValue() returns the value clamped to the item limit set in
	// BuildTree(), which is what DDV_MinMaxUInt(..., 0, 1000) guaranteed.
	int nValue = 0;
	m_pMaxLineLength->GetItemValue(nValue);
	m_settings.SetObjectViewerMaxLineLength(nValue, false /*notify*/);

	m_settings.SetObjectViewerQueryInActiveWindow          (m_pOvQueryInActiveWindow->IsChecked()? true : false, false /*notify*/);
	m_settings.SetObjectViewerRetainFocusInEditor          (m_pRetainFocusInEditor->IsChecked()  ? true : false, false /*notify*/);
	m_settings.SetObjectViewerCopyPasteOnDoubleClick       (m_pCopyPasteOnDoubleClick->IsChecked()? true : false, false /*notify*/);

	m_settings.SetObjectsListOnlySchemasWithObjects        (m_pSchemasWithObjects->IsChecked()   ? true : false, false /*notify*/);
	m_settings.SetObjectsListUseComboSelector              (m_pComboSelector->IsChecked()        ? true : false, false /*notify*/);
	m_settings.SetObjectsListTabIconsOnly                  (m_pTabIconsOnly->IsChecked()         ? true : false, false /*notify*/);
	m_settings.SetObjectsListSynonymWithoutObjectInvalid   (m_pSynonymWoObjectInvalid->IsChecked()? true : false, false /*notify*/);
	m_settings.SetObjectsListQueryInActiveWindow           (m_pOlQueryInActiveWindow->IsChecked()? true : false, false /*notify*/);

	return TRUE;
}

// Called from OnApply() and from OnCancel(), so the expansion state survives
// both. The page may never have been built, hence the null checks.
void CObjectViewerPageTree::SaveTreeState ()
{
	if (m_pObjectViewerGroup)
		s_bObjectViewerExpanded = m_pObjectViewerGroup->IsExpanded();

	if (m_pObjectListGroup)
		s_bObjectListExpanded = m_pObjectListGroup->IsExpanded();
}



