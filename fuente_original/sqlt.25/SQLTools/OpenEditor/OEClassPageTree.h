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

#ifndef __OEClassPageTree_h__
#define __OEClassPageTree_h__

#include <vector>
#include "resource.h"
#include "Common/PropTreePage.h"
#include "OpenEditor/OESettings.h"
#include "PropTree/PropTreeItemStatic.h"
#include "PropTree/PropTreeItemCombo.h"
#include "PropTree/PropTreeItemEdit.h"

	using OpenEditor::SettingsManager;

////////////////////////////////////////////////////////////////////////////////
// The "Editor::Categories" settings page, which replaces COEClassPage
// (IDD_OE_CLASSES). All the page plumbing - the shared empty dialog template, tree
// creation and layout, commit-before-read on Apply, the modified state - lives in
// Common::CPropTreePage. IDD below is the *help* id only; the template used is the
// shared IDD_PROPTREE_PAGE.
//
// WHY "CATEGORY" AND NOT "LANGUAGE". The page keeps the classic name, and that is
// not just the caption it inherited. A category and a language are two different
// properties of the same OpenEditor::ClassSettings, and the settings file spells
// them out as two attributes of one element:
//
//     <Categories>
//       <Category Name="Text" Extensions="txt lst log" Language="NONE" ... />
//
// The category is the Name - the bundle of editor settings a file gets - while the
// Language picks the syntax definition languages.xml holds, and the two are not
// synonyms: the shipped "Text" category is Name="Text" with Language="NONE". This
// page has never edited the Language property (no classic control touched it), and
// renaming the categories to "languages" would collide with the real one that
// LanguageManager::CreateLanguageSupport(), LanguagesCollection::Find() and
// HighlighterFactory::CreateHighlighter() read. The code does call the list a
// language in places - COEClassPage::OnSelChangeLanguage(), IDC_OEC_LANGUAGE,
// SettingsManager::SetClassSettingsByLang() (which really does FindByName()) and the
// live COEPropPage, which labels the very same list "Language" - but those are
// historical names for the category list.
//
// THE SHAPE OF THE PAGE: DATA DRIVEN, ONE GROUP PER CATEGORY. The classic page held
// ONE "&Category:" combo box and re-pointed every other control at the category it
// selected (OnSelChangeLanguage() re-read all of them, OnChangeData() wrote all of
// them back). A tree can hold the categories side by side instead, so this page
// builds one group per ClassSettings, labelled with GetName(), holding that
// category's own rows - the way CVisualAttributesPageTree builds one group per
// visual attribute set. The "&Category:" combo box therefore has no row of its own:
// picking a category is opening its group.
//
// Because eight categories of nine rows each are far taller than the tree pane of
// the sheet, the page asks the base class for its accordion -
// GetOneGroupOpenAtATime() returns TRUE - so opening one category closes the others
// and the category being edited is the only one on screen. The category of the
//
// STORAGE IS UNCHANGED. The rows call the same ClassSettings accessors the classic
// OnChangeData() called, with the same "do not notify" flags - including
// SetColumnMarkersSet(), which the classic page called WITHOUT the flag and which
// therefore does notify, the one setter of this page that does.
//
// THREE CONVERSION DETAILS, each a value the classic page could hold that a naive
// row would have silently rewritten:
//
//   * "Create a new file as" is an OpenEditor::EFileFormat, in which effDefault is
//     -1 and NOT the first entry of a list. The classic combo held only Dos, Unix and
//     Mac, so a stored -1 showed as a BLANK combo box and CB_GETCURSEL handed -1
//     back - correct, but invisible. InsertComboIndex() clamps an out of range value
//     to entry 0, which would have turned effDefault into effDos. This page uses the
//     four entry list the sibling COEPropPage already shows - "Default" first, index
//     == value + 1 - so -1 round-trips and is a choice the user can see and pick.
//
//   * "Auto indent" is an OpenEditor::EIndent { eiNone = 0, eiAuto = 1, eiSmart = 2 },
//     index == value in both of its lists, so a stored value always round-trips. The
//     "Smart" entry is offered only where it does something - a language with
//     smart-indent rules - or where it is already stored, so it can never be clamped
//     away. See categoryOffersSmartIndent().
//
//   * Tab size and Indent size are InsertNumberEdit() rows limited to 1..32, the
//     range the classic page set on its two spin controls with UDM_SETRANGE32 - the
//     "2011.09.13 bug fix, the application hangs if user sets either Indent or Tab
//     size to 0" of the old file header. A spinner range never constrained what the
//     EDIT beside it accepted, and the classic OnChangeData() wrote _wtoi() of that
//     text straight through, so 0 was still reachable by typing it; the row limit
//     closes that last path.
//
// WHAT CHANGED IN BEHAVIOUR, deliberately: the classic page wrote the settings on
// EVERY control change (OnChangeData()), this page writes them in ApplyValues(). That
// is the CPropTreePage contract and it is safe here -
// COEDocument::ShowSettingsDialog() edits a COPY of the settings manager and assigns
// it back only on IDOK, so nothing a cancelled sheet did was ever kept either way.
// Column markers are now validated once for every category before anything is
// written, and a parse failure reports the classic message and returns FALSE (the
// sheet stays open) instead of throwing a CUserException the base OnApply() would
// report a second time.
class COEClassPageTree : public Common::CPropTreePage
{
public:
	// pszOpenCategory is the category whose group opens first - the name of the class
	// of the active document, which is what the classic page selected in its
	// "&Category:" combo box. Null or a name no category carries leaves the remembered
	// expansion state in charge. It is matched by name because a host cannot know the
	// index a category has in a copy of the settings manager; see BuildTree().
	COEClassPageTree (SettingsManager& manager, LPCTSTR pszOpenCategory);
	virtual ~COEClassPageTree ();

	// Dialog Data
	enum { IDD = IDD_OE_CLASSES_TREE };

protected:
	virtual void BuildTree ();
	virtual BOOL ApplyValues ();
	virtual void SaveTreeState ();

	// One open category at a time - see the base class and the note above.
	virtual BOOL GetOneGroupOpenAtATime () const { return TRUE; }

private:
	// The rows of ONE category, plus the ClassSettings they read and write. The
	// pointers are null until BuildTree() ran, and Common::CPropTreePage::OnApply()
	// calls ApplyValues() only after it - the empty test in ApplyValues() and in
	// SaveTreeState() is for a page of a sheet that was cancelled before it was shown.
	struct CategoryRows
	{
		OpenEditor::ClassSettingsPtr m_settings;   // the category these rows belong to

		CPropTreeItem*       m_pGroup;
		CPropTreeItemEdit*   m_pExtensions;
		CPropTreeItemCombo*  m_pFileCreateAs;
		CPropTreeItemEdit*   m_pDelimiters;
		CPropTreeItemEdit*   m_pColumnMarkers;
		CPropTreeItemStatic* m_pNormalizeKeywords;
		CPropTreeItemCombo*  m_pTabExpand;
		CPropTreeItemEdit*   m_pTabSize;
		CPropTreeItemEdit*   m_pIndentSize;
		CPropTreeItemCombo*  m_pIndentType;

		CategoryRows ()
			: m_pGroup(nullptr), m_pExtensions(nullptr), m_pFileCreateAs(nullptr),
			m_pDelimiters(nullptr), m_pColumnMarkers(nullptr),
			m_pNormalizeKeywords(nullptr), m_pTabExpand(nullptr),
			m_pTabSize(nullptr), m_pIndentSize(nullptr), m_pIndentType(nullptr)
		{
		}
	};

	// Builds the nine rows of one category under its group, in the reading order of the
	// classic template. rows.m_settings and rows.m_pGroup are already set.
	void BuildCategoryRows (CategoryRows& rows);

	SettingsManager& m_manager;

	std::vector<CategoryRows> m_rows;

	// The category whose group opens first; empty takes the remembered expansion state.
	// A copy of the string the constructor was handed, so the page does not depend on
	// the lifetime of that one - the host passes category.c_str() of a local.
	CString m_openCategory;
};

//{{AFX_INSERT_LOCATION}}

#endif//__OEClassPageTree_h__
