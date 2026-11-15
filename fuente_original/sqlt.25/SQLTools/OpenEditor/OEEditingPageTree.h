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

#ifndef __OEEditingPageTree_h__
#define __OEEditingPageTree_h__

#include "resource.h"
#include "Common/PropTreePage.h"
#include "OpenEditor/OESettings.h"
#include "PropTree/PropTreeItemEdit.h"
#include "PropTree/PropTreeItemStatic.h"
#include "PropTree/PropTreeItemCombo.h"

	using OpenEditor::SettingsManager;

// The "Editor::Editing" settings page. The content is a CPropTree built in
// BuildTree(); all the page plumbing - the shared empty dialog template, tree
// creation and layout, commit-before-read on Apply, the modified state - lives
// in Common::CPropTreePage. IDD below is the *help* id only: the template used
// is the shared IDD_PROPTREE_PAGE.
class COEEditingPageTree : public Common::CPropTreePage
{
public:
	COEEditingPageTree (SettingsManager& manager);
	virtual ~COEEditingPageTree ();

	enum { IDD = IDD_OE_EDITING_TREE };

protected:
	virtual void BuildTree ();
	virtual BOOL ApplyValues ();
	virtual void SaveTreeState ();
	virtual void OnItemChanged (CPropTreeItem* pItem);

private:
	// Grays out the rows that depend on another row (see the .cpp).
	void UpdateEnabledState ();

	SettingsManager& m_manager;

	CPropTreeItem* m_pUndoGroup, *m_pCursorGroup, *m_pDisplayGroup, *m_pAutoCloseGroup;

	// Undo
	CPropTreeItemEdit*   m_pMaxUndoCount;
	CPropTreeItemEdit*   m_pUndoMemLimit;
	CPropTreeItemStatic* m_pUndoAfterSaving;
	CPropTreeItemStatic* m_pTruncateSpaces;

	// Cursor
	CPropTreeItemStatic* m_pCursorBeyondEOL;
	CPropTreeItemStatic* m_pCursorBeyondEOF;
	CPropTreeItemStatic* m_pUseSmartHome;
	CPropTreeItemStatic* m_pUseSmartEnd;

	// Display (line numbers / visible spaces are View menu toggles - not exposed)
	CPropTreeItemStatic* m_pEOFMark;
	CPropTreeItemCombo*  m_pTimestampFormat;

	// Auto-close / auto-surround
	CPropTreeItemStatic* m_pAutoCloseBrackets;
	CPropTreeItemStatic* m_pAutoCloseBeforeWhitespaceOnly;
	CPropTreeItemStatic* m_pAutoSurroundQuotes;
	CPropTreeItemStatic* m_pAutoSurroundBrackets;
	CPropTreeItemStatic* m_pAutoSurroundPlsqlQuoting;
};

//{{AFX_INSERT_LOCATION}}

#endif//__OEEditingPageTree_h__
