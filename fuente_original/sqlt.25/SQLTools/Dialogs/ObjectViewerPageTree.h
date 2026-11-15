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

#ifndef __ObjectViewerPageTree_h__
#define __ObjectViewerPageTree_h__
#pragma once

#include "Settings/SQLToolsSettings.h"
#include "Common/PropTreePage.h"
#include "PropTree/PropTreeItemEdit.h"
#include "PropTree/PropTreeItemStatic.h"

class CObjectViewerPageTree : public Common::CPropTreePage
{
public:
	CObjectViewerPageTree (SQLToolsSettings& settings);
	virtual ~CObjectViewerPageTree ();

protected:
	virtual void BuildTree ();

	virtual BOOL ApplyValues ();

	virtual void SaveTreeState ();

	virtual void OnItemChanged (CPropTreeItem* pItem);

private:
	void UpdateEnabledState ();

	SQLToolsSettings& m_settings;

	CPropTreeItem* m_pObjectViewerGroup;
	CPropTreeItem* m_pObjectListGroup;

	CPropTreeItemStatic* m_pUseCache;              // read only while ObjectCache is off
	CPropTreeItemStatic* m_pClassicTreeLook;
	CPropTreeItemStatic* m_pInfotips;
	CPropTreeItemStatic* m_pShowComments;
	CPropTreeItemStatic* m_pSortTabCol;
	CPropTreeItemStatic* m_pSortPkgProc;
	CPropTreeItemStatic* m_pCopyProcWithPkgName;
	CPropTreeItemEdit*   m_pMaxLineLength;
	CPropTreeItemStatic* m_pOvQueryInActiveWindow;
	CPropTreeItemStatic* m_pRetainFocusInEditor;
	CPropTreeItemStatic* m_pCopyPasteOnDoubleClick;

	CPropTreeItemStatic* m_pSchemasWithObjects;
	CPropTreeItemStatic* m_pComboSelector;
	CPropTreeItemStatic* m_pTabIconsOnly;          // dependent on m_pComboSelector
	CPropTreeItemStatic* m_pSynonymWoObjectInvalid;
	CPropTreeItemStatic* m_pOlQueryInActiveWindow;
};

//{{AFX_INSERT_LOCATION}}

#endif//__ObjectViewerPageTree_h__
