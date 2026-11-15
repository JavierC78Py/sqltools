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
#include "Dialogs/DDLPageTree.h"
#include "Common/PropTreePageHelpers.h"

	using namespace Common::PropTreeRows;

// Group expansion state, kept across dialog re-openings: written by
// SaveTreeState(), read by BuildTree().
static BOOL s_bCommonExpanded = TRUE;
static BOOL s_bTableExpanded  = TRUE;

// The classic "&Preload DB Dictionary with" edit was ES_NUMBER validated with
// DDV_MinMaxInt(0, 100). In the tree the range is the item limit (typed text is
// clamped on commit, not rejected) and the range of the spin buddy.
static const int s_preloadPercentMin  = 0;
static const int s_preloadPercentMax  = 100;
static const int s_preloadPercentStep = 5;

// The classic "Comments after Column" position edit was ES_NUMBER with no DDV
// range at all. 0 .. 1000 keeps a mistyped value out without rejecting a real
// one: the number is only the column the end-line remark is padded to.
static const int s_commentsPosMin  = 0;
static const int s_commentsPosMax  = 1000;
static const int s_commentsPosStep = 5;

// The "&Storage Clause" radio group of the classic page (DDX_Radio on
// IDC_DBSP_STORAGE_DISABLE): Disable / Overrides / All, in template order. The
// INDEX is the stored value - WriteSettings::m_StorageClause, read back through
// IsStorageEnabled() (index != 0) and IsStorageAlways() (index == 2) - so the
// order of this array must never change, and the list must never be sorted.
static const LPCTSTR s_storageClauses[] =
{
	_T("Disabled"),
	_T("Overrides (non-default only)"),
	_T("All (always)"),
};

CDDLPageTree::CDDLPageTree (SQLToolsSettings& settings)
	: Common::CPropTreePage(_T("SQLTools::DDL Preferences"), 0 /*no help topic*/),
	m_bShowShowOnShiftOnly(FALSE),
	m_bShowOnShiftOnly(FALSE),
	m_settings(settings),
	m_pCommonGroup(nullptr), m_pTableGroup(nullptr),
	m_pSQLPlusCompatibility(nullptr), m_pSchemaName(nullptr), m_pLowerNames(nullptr),
	m_pGeneratePrompts(nullptr), m_pAlwaysSlash(nullptr), m_pPreloadPercent(nullptr),
	m_pGrants(nullptr), m_pComments(nullptr), m_pSpecWithBody(nullptr),
	m_pBodyWithSpec(nullptr), m_pViewWithTriggers(nullptr), m_pViewWithForce(nullptr),
	m_pSeqWithStart(nullptr), m_pShowOnShiftOnly(nullptr),
	m_pTable(nullptr), m_pIndexes(nullptr), m_pConstraints(nullptr),
	m_pTriggers(nullptr), m_pNoStorageForPkUk(nullptr), m_pGroupByDDL(nullptr),
	m_pCommentsPos(nullptr), m_pColumnLengthSemantics(nullptr),
	m_pAlwaysPutTablespace(nullptr), m_pStorageClause(nullptr)
{
	// The 0 help id above clears PSP_HASHELP, which is what both classic pages did
	// explicitly: Help\HTMLDefines.h has HIDD_DBS_PROP / HIDD_DBS_PROP_TABLE, but
	// Help\sqltools.hhp has no [ALIAS] entry for either of them.
}

CDDLPageTree::~CDDLPageTree ()
{
}

void CDDLPageTree::BuildTree ()
{
	// -- Common DDL (was CDBSCommonPage, "SQLTools::Common DDL") ---------------
	m_pCommonGroup = InsertGroup(m_tree, _T("Common DDL"),
		_T("How the generated DDL text looks for every object type - names, prompts, comments and grants - whenever objects are scripted, extracted or reverse engineered."),
		s_bCommonExpanded);

	m_pSQLPlusCompatibility = InsertCheckedStatic(m_tree, m_pCommonGroup,
		_T("SQL Plus compatibility"),
		_T("Generate SQL*Plus friendly text: no blank lines inside a statement and no trailing white space."),
		m_settings.m_SQLPlusCompatibility);

	m_pSchemaName = InsertCheckedStatic(m_tree, m_pCommonGroup,
		_T("Prefix with schema name"),
		_T("Write object names as SCHEMA.NAME instead of NAME alone."),
		m_settings.m_ShemaName);

	m_pLowerNames = InsertCheckedStatic(m_tree, m_pCommonGroup,
		_T("Lowercase object names"),
		_T("Write object names in lower case when that is safe, i.e. when the name needs no double quotes."),
		m_settings.m_LowerNames);

	m_pGeneratePrompts = InsertCheckedStatic(m_tree, m_pCommonGroup,
		_T("Generate PROMPT(s)"),
		_T("Put a SQL*Plus PROMPT line naming the object in front of every generated statement."),
		m_settings.m_GeneratePrompts);

	// Not a flag of its own: the classic page mapped this check box onto
	// WriteSettings::EndOfShortStatement, the text that ends a short statement
	// (a COMMENT or a GRANT) - "\n/" when checked, ";" when not.
	m_pAlwaysSlash = InsertCheckedStatic(m_tree, m_pCommonGroup,
		_T("Always use / as end of statement"),
		_T("End a short statement (COMMENT, GRANT) with a slash on its own line instead of a semicolon."),
		m_settings.m_EndOfShortStatement != ";");

	// One row for the classic check box plus its dependent edit ("&Preload DB
	// Dictionary with" enabled IDC_DBSP_PRELOAD_START_PERCENT): the check box is
	// the switch, the number cell is the percentage.
	m_pPreloadPercent = InsertCheckedEdit(m_tree, m_pCommonGroup,
		_T("Preload DB dictionary with (%)"),
		_T("Load the dictionary of the whole object type up front instead of object by object, once the selection covers at least this percentage of the list."),
		m_settings.GetPreloadDictionary(), m_settings.GetPreloadStartPercent());
	m_pPreloadPercent->SetSpinRange(s_preloadPercentMin, s_preloadPercentMax);
	m_pPreloadPercent->SetSpinStep(s_preloadPercentStep);
	m_pPreloadPercent->SetItemLimit(s_preloadPercentMin, s_preloadPercentMax);

	m_pGrants = InsertCheckedStatic(m_tree, m_pCommonGroup,
		_T("Include grants"),
		_T("Add the GRANT statements of an object to its DDL."),
		m_settings.m_Grants);

	m_pComments = InsertCheckedStatic(m_tree, m_pCommonGroup,
		_T("Include comments"),
		_T("Add the COMMENT ON TABLE / VIEW / COLUMN statements to the DDL."),
		m_settings.m_Comments);

	m_pSpecWithBody = InsertCheckedStatic(m_tree, m_pCommonGroup,
		_T("Package specification with body"),
		_T("Write the body of a package or type right after its specification."),
		m_settings.GetSpecWithBody());

	m_pBodyWithSpec = InsertCheckedStatic(m_tree, m_pCommonGroup,
		_T("Package body with specification"),
		_T("Write the specification of a package or type in front of its body. Has no effect while \"Package specification with body\" is on."),
		m_settings.GetBodyWithSpec());

	m_pViewWithTriggers = InsertCheckedStatic(m_tree, m_pCommonGroup,
		_T("View with INSTEAD OF triggers"),
		_T("Write the INSTEAD OF triggers of a view together with the view itself."),
		m_settings.m_ViewWithTriggers);

	m_pViewWithForce = InsertCheckedStatic(m_tree, m_pCommonGroup,
		_T("View with FORCE"),
		_T("Add the FORCE keyword to CREATE VIEW, so the view is created even when it does not compile."),
		m_settings.m_ViewWithForce);

	m_pSeqWithStart = InsertCheckedStatic(m_tree, m_pCommonGroup,
		_T("Sequences with START WITH"),
		_T("Add START WITH <last number> to CREATE SEQUENCE, so a recreated sequence continues where it stopped."),
		m_settings.m_SequnceWithStart);

	// Built only for the "Local DDL Preferences" sheet (see the class comment): the
	// classic control was created NOT WS_VISIBLE and shown by OnInitDialog() when
	// CDBSCommonPage::m_bShowShowOnShiftOnly was TRUE.
	if (m_bShowShowOnShiftOnly)
	{
		m_pShowOnShiftOnly = InsertCheckedStatic(m_tree, m_pCommonGroup,
			_T("Only show this dialog when the Shift key is down"),
			_T("Raise the DDL preferences before generating DDL only while the Shift key is held down. Kept in the profile, not in the SQLTools settings."),
			m_bShowOnShiftOnly ? true : false);
	}

	// -- Table specific DDL (was CDBSTablePage, "SQLTools::Table Specific DDL") -
	m_pTableGroup = InsertGroup(m_tree, _T("Table specific DDL"),
		_T("Which parts of a table are written, and how its storage clause and column comments look."),
		s_bTableExpanded);

	m_pTable = InsertCheckedStatic(m_tree, m_pTableGroup,
		_T("Include table"),
		_T("Write the CREATE TABLE statement itself: columns, defaults and storage."),
		m_settings.m_TableDefinition);

	m_pIndexes = InsertCheckedStatic(m_tree, m_pTableGroup,
		_T("Include indexes"),
		_T("Write CREATE INDEX for the indexes of the table."),
		m_settings.m_Indexes);

	m_pConstraints = InsertCheckedStatic(m_tree, m_pTableGroup,
		_T("Include constraints"),
		_T("Write the constraints of the table: check, primary key, unique key and foreign key."),
		m_settings.m_Constraints);

	m_pTriggers = InsertCheckedStatic(m_tree, m_pTableGroup,
		_T("Include triggers"),
		_T("Write the triggers of the table."),
		m_settings.m_Triggers);

	m_pNoStorageForPkUk = InsertCheckedStatic(m_tree, m_pTableGroup,
		_T("No storage for PK, UK"),
		_T("Do not write the USING INDEX storage of a primary or unique key; those indexes are written as separate CREATE INDEX statements instead."),
		m_settings.m_NoStorageForConstraint);

	m_pGroupByDDL = InsertCheckedStatic(m_tree, m_pTableGroup,
		_T("Group by DDL types"),
		_T("In a multi-object document write all objects of one type together - tables, then indexes, then constraints, ... - instead of one object after another."),
		m_settings.GetGroupByDDL());

	// One row for the classic "Comments after Column" check box plus its dependent
	// position edit (IDC_DBSP_COMMENTS_POS).
	m_pCommentsPos = InsertCheckedEdit(m_tree, m_pTableGroup,
		_T("Comments after column at position"),
		_T("Write a table or column comment as an end-line remark padded to this column instead of a COMMENT ON statement."),
		m_settings.m_CommentsAfterColumn, m_settings.m_CommentsPos);
	m_pCommentsPos->SetSpinRange(s_commentsPosMin, s_commentsPosMax);
	m_pCommentsPos->SetSpinStep(s_commentsPosStep);
	m_pCommentsPos->SetItemLimit(s_commentsPosMin, s_commentsPosMax);

	m_pColumnLengthSemantics = InsertCheckedStatic(m_tree, m_pTableGroup,
		_T("Always write column length semantics"),
		_T("Write the BYTE / CHAR length semantics of a character column even when it is the default of the database."),
		m_settings.m_AlwaysWriteColumnLengthSematics);

	m_pAlwaysPutTablespace = InsertCheckedStatic(m_tree, m_pTableGroup,
		_T("Always include tablespace name"),
		_T("Write the TABLESPACE clause even when the tablespace is the default one."),
		m_settings.m_AlwaysPutTablespace);

	// Drop down list replacing the "&Storage Clause" radio group (DDX_Radio). The
	// stored value is the list index, so the order of s_storageClauses[] is part of
	// the setting and must not be changed.
	m_pStorageClause = InsertComboIndex(m_tree, m_pTableGroup,
		_T("Storage clause"),
		_T("Which storage parameters to write: Disabled - none; Overrides - only the ones that differ from the default; All - the full clause, always."),
		s_storageClauses, _countof(s_storageClauses), m_settings.m_StorageClause);
}

// The cell being edited has already been committed by the base class, so the
// item values can be read as they are shown.
BOOL CDDLPageTree::ApplyValues ()
{
	if (!m_pCommonGroup || !m_pTableGroup)
		return TRUE;                      // page was never built

	// Both sheets hand the page a COPY of the settings: SettingsDialogCallback owns
	// one and pushes it into the global settings after OK, ShowDDLPreferences() gets
	// one from its caller. That is also where the single NotifySettingsChanged() is
	// raised, hence notify = false everywhere - and the public WriteSettings members
	// are written directly, exactly like the classic DDX_Check / DDX_Text did.
	m_settings.m_SQLPlusCompatibility = m_pSQLPlusCompatibility->IsChecked() ? true : false;
	m_settings.m_ShemaName            = m_pSchemaName->IsChecked() ? true : false;
	m_settings.m_LowerNames           = m_pLowerNames->IsChecked() ? true : false;
	m_settings.m_GeneratePrompts      = m_pGeneratePrompts->IsChecked() ? true : false;
	m_settings.m_EndOfShortStatement  = m_pAlwaysSlash->IsChecked() ? "\n/" : ";";

	// The two "check box + dependent edit" pairs of the classic pages are one row
	// each here. Both values are stored RAW: the consumers gate on the check box
	// (DbBrowserList::doLoad, Table::writeColumns), so gating here again would wipe
	// the preference whenever the switch is off.
	int nValue = 0;

	m_settings.SetPreloadDictionary(m_pPreloadPercent->IsChecked() ? true : false, false /*notify*/);
	m_pPreloadPercent->GetItemValue(nValue);   // already clamped to the item limit
	m_settings.SetPreloadStartPercent(nValue, false /*notify*/);

	m_settings.m_Grants           = m_pGrants->IsChecked() ? true : false;
	m_settings.m_Comments         = m_pComments->IsChecked() ? true : false;
	m_settings.SetSpecWithBody(m_pSpecWithBody->IsChecked() ? true : false, false /*notify*/);
	m_settings.SetBodyWithSpec(m_pBodyWithSpec->IsChecked() ? true : false, false /*notify*/);
	m_settings.m_ViewWithTriggers = m_pViewWithTriggers->IsChecked() ? true : false;
	m_settings.m_ViewWithForce    = m_pViewWithForce->IsChecked() ? true : false;
	m_settings.m_SequnceWithStart = m_pSeqWithStart->IsChecked() ? true : false;

	// -- Table specific DDL ----------------------------------------------------
	m_settings.m_TableDefinition        = m_pTable->IsChecked() ? true : false;
	m_settings.m_Indexes                = m_pIndexes->IsChecked() ? true : false;
	m_settings.m_Constraints            = m_pConstraints->IsChecked() ? true : false;
	m_settings.m_Triggers               = m_pTriggers->IsChecked() ? true : false;
	m_settings.m_NoStorageForConstraint = m_pNoStorageForPkUk->IsChecked() ? true : false;
	m_settings.SetGroupByDDL(m_pGroupByDDL->IsChecked() ? true : false, false /*notify*/);

	m_settings.m_CommentsAfterColumn = m_pCommentsPos->IsChecked() ? true : false;
	m_pCommentsPos->GetItemValue(nValue);
	m_settings.m_CommentsPos = nValue;

	m_settings.m_AlwaysWriteColumnLengthSematics = m_pColumnLengthSemantics->IsChecked() ? true : false;
	m_settings.m_AlwaysPutTablespace             = m_pAlwaysPutTablespace->IsChecked() ? true : false;
	m_settings.m_StorageClause                   = (int)m_pStorageClause->GetItemValue();

	// Not a setting: ShowDDLPreferences() keeps it in the profile, and the row only
	// exists in the local DDL preferences sheet.
	if (m_pShowOnShiftOnly)
		m_bShowOnShiftOnly = m_pShowOnShiftOnly->IsChecked() ? TRUE : FALSE;

	return TRUE;
}

// Called from OnApply() and from OnCancel(), so the expansion state survives
// both. The page may never have been shown, hence the null check.
void CDDLPageTree::SaveTreeState ()
{
	if (!m_pCommonGroup || !m_pTableGroup)
		return;

	s_bCommonExpanded = m_pCommonGroup->IsExpanded();
	s_bTableExpanded  = m_pTableGroup->IsExpanded();
}
