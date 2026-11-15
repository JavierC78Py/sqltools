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

// 2026-09-25 the "commit on pick" drop down list cell of the CPropTree based
//            settings pages, see the header. Promoted to Common out of
//            OEGeneralPageTree.cpp, where it was a file local class of the same
//            name, once COEBackupPageTree needed it too: two file local copies
//            of one external-linkage MFC class collide at link time (LNK2005),
//            so the second user has to share the first one's definition rather
//            than repeat it.

#include "stdafx.h"
#include "Common/PropTreeItemComboOnSelect.h"

namespace Common {

// Posted from the cell to itself, so the id only has to be unique inside this
// file's message map. WM_APP and above is the range an application may use for
// a window it owns, and the in-place combo box is a child of the property sheet.
static const UINT WM_PTOS_COMMIT_ON_SELECT = WM_APP + 41;

BEGIN_MESSAGE_MAP(CPropTreeItemComboOnSelect, CPropTreeItemCombo)
	ON_CONTROL_REFLECT(CBN_SELCHANGE, OnSelChange)
	ON_MESSAGE(WM_PTOS_COMMIT_ON_SELECT, OnCommitPosted)
END_MESSAGE_MAP()

void CPropTreeItemComboOnSelect::OnSelChange ()
{
	PostMessage(WM_PTOS_COMMIT_ON_SELECT, 0, 0);
}

LRESULT CPropTreeItemComboOnSelect::OnCommitPosted (WPARAM, LPARAM)
{
	// Raises PTN_ITEMCHANGED, which reaches the page's OnItemChanged().
	CommitChanges();
	return 0;
}

}; // namespace Common
