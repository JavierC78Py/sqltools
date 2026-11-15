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

#ifndef __OEBlockPageTree_h__
#define __OEBlockPageTree_h__

#include "resource.h"
#include "Common/PropTreePage.h"
#include "OpenEditor/OESettings.h"
#include "PropTree/PropTreeItemStatic.h"
#include "PropTree/PropTreeItemCombo.h"

	using OpenEditor::SettingsManager;

// The "Editor::Selection" settings page, which replaces COEBlockPage
// (IDD_OE_BLOCKS). All the page plumbing - the shared empty dialog template, tree
// creation and layout, commit-before-read on Apply, the modified state - lives in
// Common::CPropTreePage. IDD below is the *help* id only; the template used is the
// shared IDD_PROPTREE_PAGE.
//
// A ONE TO ONE CONVERSION, not a merge: the page keeps its classic title - the
// template's CAPTION reads "Editor::Selection" even though its id is
// IDD_OE_BLOCKS - and its two classic group boxes become its two tree groups, in
// the template's order -
//
//   General          - the five switches that decide what an ordinary edit
//                      operation does to an existing selection
//   Column selection - the behaviour of an Alt-drag (column) block: what a move
//                      leaves behind, where the caret lands after a paste, and
//                      whether the rectangle stays selected while it is edited
//
// STORAGE IS UNCHANGED. The rows call the same eight SetBlock* / SetColBlock*
// accessors of OpenEditor::GlobalSettings, in the same order the classic OnApply()
// did and with the same "do not notify" flags, so an existing configuration reads
// and writes exactly as before. All eight are bool, so there is no range
// validation to carry over: the classic page had no DDV_* on any of them.
//
// THREE CONVERSION DETAILS:
//
//   * The classic "Column selection" group box holds two NESTED group boxes -
//     "  Move  " and "  Leave cursor at  " - each with two BS_AUTORADIOBUTTON.
//     A row has one label and one value cell, so a nested group box cannot stay a
//     group: each becomes one row whose label IS the nested group box's caption and
//     whose value cell is an InsertComboIndex() list of the two radio captions.
//     That keeps the reading order of the classic page ("Move: Delete space",
//     "Leave cursor at: End of a pasted text") instead of flattening four radios
//     into four unrelated rows.
//
//   * Both radio groups map onto a bool, so the combo index is that bool - see the
//     two s_* arrays in the .cpp for which end is which. "Move" is the direct one:
//     index 0 "Delete space" is ColBlockDeleteSpaceAfterMove == true, exactly what
//     OEContext_3.cpp does with it (`if (!deleteSpace) space.resize(...)` refills
//     the rectangle only when the setting is off).
//
//   * "Leave cursor at" is the INVERTED one, and deliberately so. The setting is
//     named ColBlockCursorToStartAfterPaste, but OEContext_3.cpp:822 reads
//
//         if (GetColBlockCursorToStartAfterPaste())
//             MoveTo(pos);           // pos is the END of the pasted text
//
//     so true really does put the caret at the END. The classic page agreed with
//     the code and not with the name: its ctor assigned the bool straight into the
//     int DDX_Radio member (true -> index 1 -> "&End of a pasted text") and its
//     OnApply() wrote the index straight back. This page reproduces that mapping,
//     so a configuration that says "End" keeps saying "End". Only the stored NAME
//     is misleading, and it is part of the on-disk format - the same reason the
//     misspelled FileBackupSpaceManagment survives on the Backup page.
//
// There are no dependent rows: the classic DoDataExchange() grayed nothing out, and
// no control acted on CBN_SELCHANGE, so this page needs neither
// UpdateEnabledState() nor OnItemChanged() - and the two combos are plain
// InsertComboIndex() rows, not the OnSelect variant.
class COEBlockPageTree : public Common::CPropTreePage
{
public:
	COEBlockPageTree (SettingsManager& manager);
	virtual ~COEBlockPageTree ();

	// Dialog Data
	enum { IDD = IDD_OE_BLOCKS_TREE };

protected:
	virtual void BuildTree ();
	virtual BOOL ApplyValues ();
	virtual void SaveTreeState ();

private:
	SettingsManager& m_manager;

	CPropTreeItem* m_pGeneralGroup;
	CPropTreeItem* m_pColumnGroup;

	// "General": what an ordinary edit operation does to an existing selection.
	CPropTreeItemStatic* m_pKeepMarking;
	CPropTreeItemStatic* m_pKeepMarkingAfterDrop;
	CPropTreeItemStatic* m_pDelAndBSDelete;
	CPropTreeItemStatic* m_pTypingOverwrite;
	CPropTreeItemStatic* m_pTabIndent;

	// "Column selection": the Alt-drag block. The two combos hold the index of the
	// bool behind them - see the note on the inverted one above.
	CPropTreeItemCombo*  m_pColMove;
	CPropTreeItemCombo*  m_pColCursorAt;
	CPropTreeItemStatic* m_pColEditMode;
};

//{{AFX_INSERT_LOCATION}}

#endif//__OEBlockPageTree_h__
