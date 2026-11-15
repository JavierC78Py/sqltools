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

#ifndef __OEGeneralPageTree_h__
#define __OEGeneralPageTree_h__

#include <vector>

#include "resource.h"
#include "Common/PropTreePage.h"
#include "OpenEditor/OESettings.h"
#include "PropTree/PropTreeItemStatic.h"
#include "PropTree/PropTreeItemCombo.h"

	using OpenEditor::SettingsManager;

// The "Editor::General" settings page, which replaces the classic COEGeneralPage
// (IDD_OE_GENERAL). The content is a CPropTree built in BuildTree(); all the page
// plumbing - the shared empty dialog template, tree creation and layout,
// commit-before-read on Apply, the modified state - lives in
// Common::CPropTreePage. IDD below is the *help* id only: the template used is
// the shared IDD_PROPTREE_PAGE.
//
// Shape of the tree, which follows the classic template:
//
//   * the controls that stood OUTSIDE the one group box - the four startup and
//     window check boxes, "Keymap layout", "Locale", "Default encoding" with its
//     dependent "Open ASCII (7-bit) files as UTF-8", and "Show unsupported Asian
//     characters" - are the rows of a "General" group;
//   * the six check boxes of the "  MDI Tabs  " group box are the second group, in
//     the row-major reading order of the template's two columns ("Tabs on Top"
//     and "Active Tab has Close button" were the top of the left and the right
//     column).
//
// Two groups rather than root level rows for the first set, because CPropTreeItem
// draws a root level item as a group HEADER - shaded, bold, full width, and with
// the attribute cell skipped under "if (!IsRootLevel()) DrawAttribute(...)". A root
// level combo therefore paints its label and no value. BuildTree() carries the full
// note. "Show unsupported Asian characters" moved up into "General" for the same
// reason, which is the one place the tree reads in a different order than the
// template did.
//
// Three details worth knowing:
//
//   * "Keymap layout" and "Locale" store TEXT, not a list index: the classic
//     combos were CBS_DROPDOWNLIST whose selection was matched back with
//     CB_FINDSTRINGEXACT and written out with GetWindowText(). Both rows are
//     therefore index mode combos - a pick list, as before, because
//     CPropTreeItemCombo's text mode needs an EDITABLE combo: it drives the cell
//     with SetWindowText()/GetWindowText() and SetEditSel() - and the page keeps
//     the list each row was built from, mapping the selected index back to its
//     text in ApplyValues(). A row with no selection writes an empty string,
//     which is what the classic GetWindowText() returned then: "Locale" can
//     legitimately have none, because a stored locale that is in none of the 36
//     presets left the classic combo unselected.
//   * "Keymap layout" warns that the choice needs a restart, as the classic page
//     did on CBN_SELCHANGE - and only on an actual change, not on every commit of
//     the cell. The box is POSTED rather than shown from OnItemChanged(), because
//     that handler runs inside CPropTreeItem::CommitChanges(), before
//     CPropTree::RefreshItems() has repainted the row: a modal box raised there
//     would sit over a cell still showing the old value.
//   * the layout list itself is OpenEditor::KeymapLayoutList (OESettings.h),
//     which CSQLToolsApp::InitInstance() replaces with the layouts SQLTools
//     ships. It used to be a static member of the classic page.
class COEGeneralPageTree : public Common::CPropTreePage
{
public:
	COEGeneralPageTree (SettingsManager& manager);
	virtual ~COEGeneralPageTree ();

	// Dialog Data
	enum { IDD = IDD_OE_GENERAL_TREE };

protected:
	virtual void BuildTree ();
	virtual BOOL ApplyValues ();
	virtual void SaveTreeState ();
	virtual void OnItemChanged (CPropTreeItem* pItem);

private:
	// Grays out the rows that depend on another row (see the .cpp).
	void UpdateEnabledState ();

	SettingsManager& m_manager;

	CPropTreeItem* m_pGeneralGroup;
	CPropTreeItem* m_pFindReplaceGroup;
	CPropTreeItem* m_pMdiTabsGroup;

	// The controls that stood outside the one group box of the classic template,
	// now the rows of the "General" group. They cannot be root level rows: a root
	// level item draws as a group header, with no value cell at all - see the note
	// in BuildTree().
	CPropTreeItemStatic* m_pAllowMultipleInstances;
	CPropTreeItemStatic* m_pNewDocOnStartup;
	CPropTreeItemStatic* m_pWorkDirFollowsDoc;
	CPropTreeItemStatic* m_pSaveMainWindowPosition;
	CPropTreeItemCombo*  m_pKeymapLayout;
	CPropTreeItemCombo*  m_pLocale;
	CPropTreeItemCombo*  m_pEncoding;
	CPropTreeItemStatic* m_pAsciiAsUtf8;
	CPropTreeItemStatic* m_pUnsupportedAsianChars;

	// Find/Replace dialog behaviour - not part of the classic template; added with
	// the modeless conversion (md/editor_search_modeless_conversion_plan.md section 8).
	// Rows of their own "Find/Replace" group, after the General group.
	CPropTreeItemStatic* m_pFindCloseOnDeactivate;
	CPropTreeItemStatic* m_pFindDisableLivePreview;

	// The "  MDI Tabs  " group box.
	CPropTreeItemStatic* m_pTabsOnTop;
	CPropTreeItemStatic* m_pTabsCloseButton;
	CPropTreeItemStatic* m_pTabsAutoColor;
	CPropTreeItemStatic* m_pTabs3DLook;
	CPropTreeItemStatic* m_pTabsListMenuButton;
	CPropTreeItemStatic* m_pTabsCtrlTabSwitchesToPrevActive;

	// The keymap layouts, parsed from OpenEditor::KeymapLayoutList in BuildTree()
	// and not touched afterwards, so the indices the "Keymap layout" row holds
	// stay valid until ApplyValues() maps them back to text.
	std::vector<CString> m_keymapLayouts;

	// The value the "Keymap layout" row was last seen with, so the restart warning
	// is raised on a change only.
	LPARAM m_lKeymapLayout;

	afx_msg LRESULT OnKeymapLayoutWarning (WPARAM, LPARAM);

	DECLARE_MESSAGE_MAP ()
};

//{{AFX_INSERT_LOCATION}}

#endif//__OEGeneralPageTree_h__
