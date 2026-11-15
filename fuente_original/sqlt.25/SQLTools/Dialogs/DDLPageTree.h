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

#ifndef __DDLPageTree_h__
#define __DDLPageTree_h__
#pragma once

#include "Settings/SQLToolsSettings.h"
#include "Common/PropTreePage.h"
#include "PropTree/PropTreeItemEdit.h"
#include "PropTree/PropTreeItemStatic.h"
#include "PropTree/PropTreeItemCombo.h"

class CDDLPageTree : public Common::CPropTreePage
{
public:
	CDDLPageTree (SQLToolsSettings& settings);
	virtual ~CDDLPageTree ();

	BOOL m_bShowShowOnShiftOnly;
	BOOL m_bShowOnShiftOnly;

protected:
	virtual void BuildTree ();

	virtual BOOL ApplyValues ();

	virtual void SaveTreeState ();

private:
	SQLToolsSettings& m_settings;

	CPropTreeItem* m_pCommonGroup;
	CPropTreeItem* m_pTableGroup;

	CPropTreeItemStatic* m_pSQLPlusCompatibility;
	CPropTreeItemStatic* m_pSchemaName;
	CPropTreeItemStatic* m_pLowerNames;
	CPropTreeItemStatic* m_pGeneratePrompts;
	CPropTreeItemStatic* m_pAlwaysSlash;
	CPropTreeItemEdit*   m_pPreloadPercent;   // check = PreloadDictionary, value = percent
	CPropTreeItemStatic* m_pGrants;
	CPropTreeItemStatic* m_pComments;
	CPropTreeItemStatic* m_pSpecWithBody;
	CPropTreeItemStatic* m_pBodyWithSpec;
	CPropTreeItemStatic* m_pViewWithTriggers;
	CPropTreeItemStatic* m_pViewWithForce;
	CPropTreeItemStatic* m_pSeqWithStart;
	CPropTreeItemStatic* m_pShowOnShiftOnly;  // optional, see m_bShowShowOnShiftOnly

	CPropTreeItemStatic* m_pTable;
	CPropTreeItemStatic* m_pIndexes;
	CPropTreeItemStatic* m_pConstraints;
	CPropTreeItemStatic* m_pTriggers;
	CPropTreeItemStatic* m_pNoStorageForPkUk;
	CPropTreeItemStatic* m_pGroupByDDL;
	CPropTreeItemEdit*   m_pCommentsPos;      // check = CommentsAfterColumn, value = column
	CPropTreeItemStatic* m_pColumnLengthSemantics;
	CPropTreeItemStatic* m_pAlwaysPutTablespace;
	CPropTreeItemCombo*  m_pStorageClause;
};

//{{AFX_INSERT_LOCATION}}

#endif//__DDLPageTree_h__
