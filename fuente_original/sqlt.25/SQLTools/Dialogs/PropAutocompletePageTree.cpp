/* 
    SQLTools is a tool for Oracle database developers and DBAs.
    Copyright (C) 1997-2025 Aleksey Kochetov

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

// 2026-09-22 replaces CPropAutocompletePage, see the header

#include "stdafx.h"
#include "Dialogs/PropAutocompletePageTree.h"
#include "Common/PropTreePageHelpers.h"

	using namespace Common::PropTreeRows;

// Group expansion state, kept across dialog re-openings: written by
// SaveTreeState(), read by BuildTree().
static BOOL s_bObjectTypesExpanded    = TRUE;
static BOOL s_bMultiColumnSelExpanded = TRUE;

CPropAutocompletePageTree::CPropAutocompletePageTree (SQLToolsSettings& settings)
	: Common::CPropTreePage(_T("SQLTools::Autocomplete"), 0 /*no help topic*/),
	m_settings(settings),
	m_pObjectTypesGroup(nullptr), m_pMultiColumnSelGroup(nullptr),
	m_pTable(nullptr), m_pView(nullptr), m_pPackage(nullptr),
	m_pProcedure(nullptr), m_pFunction(nullptr), m_pSequence(nullptr),
	m_pSynonym(nullptr), m_pType(nullptr),
	m_pSortSubobjects(nullptr), m_pMultiColumnSel(nullptr),
	m_pSeparateLines(nullptr), m_pLeadingComma(nullptr)
{
	// The 0 help id above clears PSP_HASHELP, which is what the classic page did
	// explicitly (m_psp.dwFlags &= ~PSP_HASHELP): Help\HTMLDefines.h has
	// HIDD_PROP_AUTOCOMPLETE, but Help\sqltools.hhp has no [ALIAS] entry for it.
}

CPropAutocompletePageTree::~CPropAutocompletePageTree ()
{
}

void CPropAutocompletePageTree::BuildTree ()
{
	// -- Object types shown in autocomplete list (the group box of the same name
	//    in IDD_PROP_AUTOCOMPLETE). The classic template shows the eight check
	//    boxes in TWO columns, Table..Function left and Sequence..Type right; a
	//    tree has one column, so the rows keep the order of the template, of
	//    DoDataExchange() and of the m_Autocomplete* declarations, which all
	//    three agree on (and which AutocompleteTypeFilter repeats).
	m_pObjectTypesGroup = InsertGroup(m_tree, _T("Object types shown in autocomplete list"),
		_T("The object types the editor's autocomplete list offers for the current schema: Ctrl+Shift+Space, or a schema name and a dot. They are the object_type filter of the query that fills the session's object cache, so a change makes that cache load again."),
		s_bObjectTypesExpanded);

	m_pTable = InsertCheckedStatic(m_tree, m_pObjectTypesGroup,
		_T("Table"),
		_T("Offer tables. Their columns are what the list shows after the table name, or after an alias of it, and a dot."),
		m_settings.GetAutocompleteTable());

	m_pView = InsertCheckedStatic(m_tree, m_pObjectTypesGroup,
		_T("View"),
		_T("Offer views. Their columns are what the list shows after the view name, or after an alias of it, and a dot."),
		m_settings.GetAutocompleteView());

	m_pPackage = InsertCheckedStatic(m_tree, m_pObjectTypesGroup,
		_T("Package"),
		_T("Offer packages. After a package name and a dot the list shows their procedures and functions, with the parameter lists."),
		m_settings.GetAutocompletePackage());

	m_pProcedure = InsertCheckedStatic(m_tree, m_pObjectTypesGroup,
		_T("Procedure"),
		_T("Offer the standalone procedures of the current schema."),
		m_settings.GetAutocompleteProcedure());

	m_pFunction = InsertCheckedStatic(m_tree, m_pObjectTypesGroup,
		_T("Function"),
		_T("Offer the standalone functions of the current schema."),
		m_settings.GetAutocompleteFunction());

	m_pSequence = InsertCheckedStatic(m_tree, m_pObjectTypesGroup,
		_T("Sequence"),
		_T("Offer the sequences of the current schema."),
		m_settings.GetAutocompleteSequence());

	m_pSynonym = InsertCheckedStatic(m_tree, m_pObjectTypesGroup,
		_T("Synonym"),
		_T("Offer the synonyms of the current schema."),
		m_settings.GetAutocompleteSynonym());

	m_pType = InsertCheckedStatic(m_tree, m_pObjectTypesGroup,
		_T("Type"),
		_T("Offer the object types of the current schema."),
		m_settings.GetAutocompleteType());

	// -- Multi-column selection (the second group box of IDD_PROP_AUTOCOMPLETE),
	//    with IDC_PROP_AC_SORT_SUBOBJ in front of it: that check box stands in the
	//    gap between the two group boxes, and all four rows describe the same
	//    subobject list - see the header.
	m_pMultiColumnSelGroup = InsertGroup(m_tree, _T("Multi-column selection"),
		_T("The list of columns and package procedures the editor offers after an object name or an alias and a dot, and the text a multi-column selection inserts. The two format rows are also used when several columns are copied or dragged out of the object viewer tree."),
		s_bMultiColumnSelExpanded);

	// AutocompleteColBaseTemplate passes it to Template::SetSortedList(), which
	// COEAutocompleteCtrl acts upon when it creates the list.
	m_pSortSubobjects = InsertCheckedStatic(m_tree, m_pMultiColumnSelGroup,
		_T("Sort columns and package procedures"),
		_T("Sort the entries of that list by name instead of keeping the order the database returned them in."),
		m_settings.GetAutocompleteSortSubobjects());

	// Template::SetMultiSelectionSupported(); COEAutocompleteCtrl collects the
	// picked rows in the order they were picked. AutocompleteSubobjectTemplate
	// turns it back off when the children are not columns.
	m_pMultiColumnSel = InsertCheckedStatic(m_tree, m_pMultiColumnSelGroup,
		_T("Enable multi-column selection"),
		_T("Pick several columns of a table or a view - Space, Ins or Ctrl+left click, in the order they are to be inserted - and let Enter insert all of them. A list of package procedures always stays single selection."),
		m_settings.GetAutocompleteEnableMultiColumnSelection());

	// AutocompleteColBaseTemplate::ExpandKeyword() branches on it, and so does
	// the multi selected column copy of CTreeViewer.
	m_pSeparateLines = InsertCheckedStatic(m_tree, m_pMultiColumnSelGroup,
		_T("Insert selected columns on separate lines"),
		_T("Insert the selected columns one per line instead of a single comma separated line."),
		m_settings.GetAutocompleteInsertColumnsOnSeparateLines());

	// Read in the separate lines branch only, so it does nothing while the row
	// above is off. The classic dialog left the check box enabled, hence no
	// ReadOnly() here - the info text says it instead.
	m_pLeadingComma = InsertCheckedStatic(m_tree, m_pMultiColumnSelGroup,
		_T("Use leading comma for multi-line inserts"),
		_T("Write the comma in front of every next column instead of behind the previous one. Has an effect only while the columns are inserted on separate lines."),
		m_settings.GetAutocompleteUseLeadingComma());
}

// Called from Common::CPropTreePage::OnApply(), which has already committed the
// cell being edited and which reports exceptions. There is nothing to validate
// here: the page has check box rows only.
BOOL CPropAutocompletePageTree::ApplyValues ()
{
	// The settings sheet hands the page a COPY of the settings: the
	// SettingsDialogCallback of App\SQLTools_4.cpp owns it, pushes it into the
	// global settings after OK and raises the single NotifySettingsChanged() -
	// which is what makes SessionCache::Reload() notice a changed type filter -
	// hence notify = false everywhere. The public Set accessors are used because
	// the m_Autocomplete* members are private and only the classic page is a
	// friend of SQLToolsSettings.
	m_settings.SetAutocompleteTable                        (m_pTable->IsChecked()          ? true : false, false /*notify*/);
	m_settings.SetAutocompleteView                         (m_pView->IsChecked()           ? true : false, false /*notify*/);
	m_settings.SetAutocompletePackage                      (m_pPackage->IsChecked()        ? true : false, false /*notify*/);
	m_settings.SetAutocompleteProcedure                    (m_pProcedure->IsChecked()      ? true : false, false /*notify*/);
	m_settings.SetAutocompleteFunction                     (m_pFunction->IsChecked()       ? true : false, false /*notify*/);
	m_settings.SetAutocompleteSequence                     (m_pSequence->IsChecked()       ? true : false, false /*notify*/);
	m_settings.SetAutocompleteSynonym                      (m_pSynonym->IsChecked()        ? true : false, false /*notify*/);
	m_settings.SetAutocompleteType                         (m_pType->IsChecked()           ? true : false, false /*notify*/);

	m_settings.SetAutocompleteSortSubobjects               (m_pSortSubobjects->IsChecked() ? true : false, false /*notify*/);
	m_settings.SetAutocompleteEnableMultiColumnSelection   (m_pMultiColumnSel->IsChecked() ? true : false, false /*notify*/);
	m_settings.SetAutocompleteInsertColumnsOnSeparateLines (m_pSeparateLines->IsChecked()  ? true : false, false /*notify*/);
	m_settings.SetAutocompleteUseLeadingComma              (m_pLeadingComma->IsChecked()   ? true : false, false /*notify*/);

	return TRUE;
}

// Called from OnApply() and from OnCancel(), so the expansion state survives
// both. The page may never have been built, hence the null checks.
void CPropAutocompletePageTree::SaveTreeState ()
{
	if (m_pObjectTypesGroup)
		s_bObjectTypesExpanded = m_pObjectTypesGroup->IsExpanded();

	if (m_pMultiColumnSelGroup)
		s_bMultiColumnSelExpanded = m_pMultiColumnSelGroup->IsExpanded();
}
