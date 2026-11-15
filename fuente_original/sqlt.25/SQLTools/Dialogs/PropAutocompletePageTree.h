/* 
    SQLTools is a tool for Oracle database developers and DBAs.
    Copyright (C) 1997-2025 Aleksey Kochetov

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

#ifndef __PropAutocompletePageTree_h__
#define __PropAutocompletePageTree_h__
#pragma once

#include "Settings/SQLToolsSettings.h"
#include "Common/PropTreePage.h"
#include "PropTree/PropTreeItemStatic.h"

class CPropAutocompletePageTree : public Common::CPropTreePage
{
public:
	CPropAutocompletePageTree (SQLToolsSettings& settings);
	virtual ~CPropAutocompletePageTree ();

protected:
	virtual void BuildTree ();

	virtual BOOL ApplyValues ();

	virtual void SaveTreeState ();

private:
	SQLToolsSettings& m_settings;

	CPropTreeItem* m_pObjectTypesGroup;
	CPropTreeItem* m_pMultiColumnSelGroup;

	CPropTreeItemStatic* m_pTable;
	CPropTreeItemStatic* m_pView;
	CPropTreeItemStatic* m_pPackage;
	CPropTreeItemStatic* m_pProcedure;
	CPropTreeItemStatic* m_pFunction;
	CPropTreeItemStatic* m_pSequence;
	CPropTreeItemStatic* m_pSynonym;
	CPropTreeItemStatic* m_pType;

	CPropTreeItemStatic* m_pSortSubobjects;   // the orphan row, see the header
	CPropTreeItemStatic* m_pMultiColumnSel;
	CPropTreeItemStatic* m_pSeparateLines;
	CPropTreeItemStatic* m_pLeadingComma;
};

//{{AFX_INSERT_LOCATION}}

#endif//__PropAutocompletePageTree_h__
