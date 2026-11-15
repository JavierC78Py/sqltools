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

// 2026-09-25 the folder browse cell of the CPropTree based settings pages, see
//            the header. First used by COEBackupPageTree for the Backup &&
//            Autosave folder, replacing the read-only IDC_OE_BACKUP_DIR edit and
//            its "&..." button of COEBackupPage.

#include "stdafx.h"
#include <Common/DirSelectDlg.h>
#include "Common/PropTreeItemFolder.h"
// For the complete CPropTree, which PropTreeItem.h only forward declares: the
// owner is the dialog the folder browser is modal to.
#include "PropTree/PropTree.h"

namespace Common {

CPropTreeItemFolder::CPropTreeItemFolder (LPCTSTR caption, LPCTSTR emptyText)
	: m_sCaption(caption ? caption : _T(""))
	, m_sEmptyText(emptyText ? emptyText : _T(""))
{
}

CPropTreeItemFolder::~CPropTreeItemFolder ()
{
}

void CPropTreeItemFolder::SetFolder (LPCTSTR path)
{
	m_sFolder = path ? path : _T("");
	UpdateText();
}

void CPropTreeItemFolder::UpdateText ()
{
	// Through the inherited setter rather than straight at m_sAttribute, so that
	// a CPropTreeItemStatic change to how the display text is kept is picked up
	// here too. It takes the string as an LPARAM and rejects 0, which a
	// (LPCTSTR)0 placeholder never is because one of the two branches always
	// supplies a real buffer.
	CPropTreeItemStatic::SetItemValue((LPARAM)(LPCTSTR)
		(m_sFolder.IsEmpty() ? (LPCTSTR)m_sEmptyText : (LPCTSTR)m_sFolder));
}

void CPropTreeItemFolder::AppendSearchValue (CString& sText)
{
	// m_sFolder is the setting. The inherited m_sAttribute that
	// CPropTreeItemStatic::AppendSearchValue() would hand over is the DISPLAY text,
	// which for an unset folder is the placeholder - matching that would find the
	// row by the stand-in shown precisely when there is no value at all.
	if (!m_sFolder.IsEmpty())
	{
		sText += m_sFolder;
		sText += _T(' ');
	}
}



void CPropTreeItemFolder::OnActivate ()
{
	// Entered from CPropTreeItem::Activate(), which has already set
	// TreeItemActivated and cleared m_bCommitOnce, so exactly one of the two
	// exits below is what the state machine expects: CommitChanges() notifies
	// once and clears the flag again, Cancel() clears the state and stays quiet.
	//
	// The dialog is owned by the tree rather than by the page because the item
	// only ever sees its CPropTree owner; the sheet is that window's ancestor, so
	// the browser still centres on the sheet and stays modal to it.
	//
	// The initial folder is passed only when there is one. CDirSelectDlg leaves the folder
	// picker's own memory of the last location alone when there is none to pass, so an empty
	// setting falls back to wherever the user last looked instead of to the desktop - the classic
	// COEBackupPage::OnBnClicked_FileBackupDirChange() passed L"" for the same "<Default
	// location>" case and got the same behaviour.
	CDirSelectDlg dlg(m_sCaption, m_pProp, m_sFolder.IsEmpty() ? NULL : (LPCTSTR)m_sFolder);

	if (dlg.DoModal() != IDOK)
	{
		Cancel();
		return;
	}

	CString picked;
	dlg.GetPath(picked);

	// SetFolder() before CommitChanges(): the notification the page receives has
	// to find the new value already in place, both in m_sFolder, which
	// ApplyValues() reads through GetFolder(), and in the display text, which
	// CommitChanges()'s RefreshItems() repaints.
	SetFolder(picked);
	CommitChanges();
}

}; // namespace Common
