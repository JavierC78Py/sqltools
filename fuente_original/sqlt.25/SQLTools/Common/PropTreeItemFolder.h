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

#ifndef __PropTreeItemFolder_h__
#define __PropTreeItemFolder_h__

#include "PropTree/PropTreeItemStatic.h"

namespace Common {

////////////////////////////////////////////////////////////////////////////////
// A value cell that shows a folder path and opens Common::CDirSelectDlg when the
// user acts on it - the tree equivalent of the classic read-only EDITTEXT plus
// "&..." browse button pair (IDD_OE_BACKUP's IDC_OE_BACKUP_DIR beside
// IDC_OE_BACKUP_DIR_CHANGE).
//
// WHY OnActivate() AND NOT PTN_PROPCLICK
//
// The vendored control has no button: no button item class, no button drawing
// and no attribute-area button hit test - CPropTreeItem offers HitExpand() and
// HitCheckBox() only. PTN_ITEMBUTTONCLICK is declared in PropTree.h and listed
// in CPropTree::SendNotify()'s switch, but nothing ever sends it, so it is not a
// channel to build on without adding one to ThirdParty\PropTree.
//
// PTN_PROPCLICK looks like the ready-made hook - CPropTreeList::OnLButtonDown()
// offers a value-cell click to the host before it activates the cell - but it is
// sent from that one place only. VK_RETURN calls pItem->Activate() directly
// without sending it and VK_SPACE only toggles check boxes, so a row driven from
// PTN_PROPCLICK would be mouse-only, and it is not in CPropTreePage's message
// map either. Overriding OnActivate() instead reaches BOTH input paths through
// the same virtual:
//
//   mouse    ... if (!SendNotify(PTN_PROPCLICK, pItem) && !pItem->IsReadOnly())
//                    pItem->Activate();
//   keyboard case VK_RETURN: ... pItem->Activate();
//
// and needs no notification plumbing at all, because the row reports through the
// PTN_ITEMCHANGED that CommitChanges() already sends and CPropTreePage already
// routes to OnItemChanged(). It is also how the control itself does colours:
// CPropTreeItemColor::OnActivate() builds and shows its palette popup. Clicking
// a value cell to open a picker is this control's own idiom, not an invention.
//
// THE ROW MUST NOT BE ReadOnly()
//
// Both paths above test !IsReadOnly() first, so a read-only row could never
// reach OnActivate(). That costs nothing here: CPropTreeItemStatic has no editor
// to suppress - it overrides neither OnActivate() nor OnCommit(), and the base
// CPropTreeItem::OnActivate() is "no attributes, do nothing". Leaving the row
// writable also keeps its text in COLOR_WINDOWTEXT rather than the
// COLOR_GRAYTEXT that CPropTreeItemStatic::DrawAttribute() uses for a read-only
// row, which is right for a cell the user is meant to click. A page that wants
// the row inert grays it with ReadOnly() as usual and the picker then cannot be
// reached, which is the point.
//
// RUNNING A MODAL DIALOG FROM OnActivate()
//
// Safe on both paths. The attribute-area branch of OnLButtonDown() takes no
// capture - only the HTCHECKBOX and HTCOLUMN branches do - so nothing is held
// across the nested message loop, and the OnLButtonUp() that follows the dialog
// finds m_bCheckPressed/m_bCheckCapture clear and GetCapture()!=this and does
// nothing. VK_RETURN holds no capture either. A double click is not a second
// opening: OnLButtonDblClk() only acts on an item that GetChild(), and a value
// row has no children, so one click yields one WM_LBUTTONDOWN and one dialog.
//
// WHERE THE VALUE LIVES
//
// The folder is m_sFolder and NOT the inherited m_sAttribute, which carries the
// DISPLAY text: an empty folder shows the placeholder instead, the way the
// classic read-only edit showed "<Default location>" for the same unset setting.
// Read the setting back with GetFolder(); GetItemValue() returns what is on
// screen, placeholder included, and is only there because CPropTreeItemStatic
// declares it.
////////////////////////////////////////////////////////////////////////////////
class CPropTreeItemFolder : public CPropTreeItemStatic
{
public:
	// caption is the title of the folder dialog, emptyText what the cell shows
	// while the folder is empty. Both are copied, so they may be temporaries.
	CPropTreeItemFolder (LPCTSTR caption, LPCTSTR emptyText);
	virtual ~CPropTreeItemFolder ();

	// The folder itself. Empty (or NULL) means "not set", and the cell then shows
	// the placeholder. Repaints nothing on its own: the caller is either building
	// the row or inside a change that ends in CommitChanges(), which refreshes.
	void SetFolder (LPCTSTR path);

	// The setting. Empty means the default location.
	const CString& GetFolder () const { return m_sFolder; }

	// CPropTreeItem: contributes the folder to a settings search. It is m_sFolder
	// and NOT the inherited m_sAttribute that CPropTreeItemStatic would hand over:
	// m_sAttribute carries the DISPLAY text, which for an unset folder is the
	// placeholder - searching must match the setting, not the stand-in shown when
	// there is none.
	virtual void AppendSearchValue (CString& sText);

	// CPropTreeItem: opens the folder dialog. A picked folder ends in
	// CommitChanges() - one PTN_ITEMCHANGED, which is how the page learns the
	// value moved - and a dismissed dialog in Cancel(), which clears the
	// activated state and sends nothing, so cancelling does not mark the page
	// modified for a value that did not change.
	virtual void OnActivate ();

private:
	// Pushes m_sFolder, or the placeholder when it is empty, into the inherited
	// m_sAttribute that CPropTreeItemStatic::DrawAttribute() paints.
	void UpdateText ();

	CString m_sCaption;
	CString m_sEmptyText;
	CString m_sFolder;
};

}; // namespace Common

//{{AFX_INSERT_LOCATION}}

#endif//__PropTreeItemFolder_h__
