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

#ifndef __PropTreeItemComboOnSelect_h__
#define __PropTreeItemComboOnSelect_h__

#include "PropTree/PropTreeItemCombo.h"

namespace Common {

////////////////////////////////////////////////////////////////////////////////
// A drop down list row that commits its cell as soon as an entry is picked,
// instead of on kill-focus or on Enter as CPropTreeItemCombo does.
//
// WHY
//
// CPropTreeItemCombo handles CBN_KILLFOCUS and VK_RETURN only, so a pick from
// the drop down list stays inside the cell until the user clicks away. A page
// whose classic dialog reacted to CBN_SELCHANGE - graying a dependent row in and
// out, or warning that the choice needs a restart - would lag by one click
// without this class. Build such a row with
// Common::PropTreeRows::InsertComboIndexOnSelect() rather than by hand.
//
// WHY THE COMMIT IS POSTED AND NOT DONE IN THE HANDLER
//
// CommitChanges() deactivates the cell, that is, hides the very combo box that
// is sending CBN_SELCHANGE while its drop down list is still closing. Doing the
// commit inline therefore destroys the control from inside its own
// notification, so OnSelChange() posts to the cell window - CPropTreeItemCombo
// derives from CComboBox as well as from CPropTreeItem, which is what makes
// PostMessage() and the item's own message map the same window - and the posted
// handler runs once the list has closed. CommitChanges() is idempotent per
// activation (m_bCommitOnce), so the later kill-focus commit is a no-op.
//
// The commit raises PTN_ITEMCHANGED, which CPropTreePage routes to the page's
// OnItemChanged() - no notification plumbing of its own is needed.
//
// Dialogs\VisualAttributesPageTree.cpp has its own CPropTreeItemFontName for
// the same reason; it stays local to that page because it also carries the
// font-preview behaviour of its row and not only this commit.
////////////////////////////////////////////////////////////////////////////////
class CPropTreeItemComboOnSelect : public CPropTreeItemCombo
{
protected:
	afx_msg void    OnSelChange ();
	afx_msg LRESULT OnCommitPosted (WPARAM, LPARAM);

	DECLARE_MESSAGE_MAP ()
};

}; // namespace Common

//{{AFX_INSERT_LOCATION}}

#endif//__PropTreeItemComboOnSelect_h__
