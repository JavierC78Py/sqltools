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

#ifndef __PropServerScriptPageTree_h__
#define __PropServerScriptPageTree_h__
#pragma once

#include "Settings/SQLToolsSettings.h"
#include "Common/PropTreePage.h"
#include "PropTree/PropTreeItemEdit.h"
#include "PropTree/PropTreeItemStatic.h"
#include "PropTree/PropTreeItemCombo.h"

class CPropServerScriptPageTree : public Common::CPropTreePage
{
public:
	CPropServerScriptPageTree (SQLToolsSettings& settings);
	virtual ~CPropServerScriptPageTree ();

	BOOL m_bShowSessionScriptGroups;
	BOOL m_bShowHistoryGroup;

protected:
	virtual void BuildTree ();                          // required: the page content
	virtual BOOL ApplyValues ();                        // tree -> settings
	virtual void SaveTreeState ();                      // group expansion statics

	virtual void OnItemChanged (CPropTreeItem* pItem);

private:
	void BuildSessionAndScriptGroups ();
	void BuildHistoryGroup ();

	void UpdateEnabledState ();

	SQLToolsSettings& m_settings;

	CPropTreeItem* m_pSessionGroup;
	CPropTreeItem* m_pScriptGroup;
	CPropTreeItem* m_pHistoryGroup;

	CPropTreeItemCombo*  m_pDateFormat;
	CPropTreeItemEdit*   m_pPlanTable;
	CPropTreeItemStatic* m_pObjectCache;
	CPropTreeItemStatic* m_pSessionStatistics;
	CPropTreeItemStatic* m_pAutocommit;
	CPropTreeItemCombo*  m_pCommitOnDisconnect;
	CPropTreeItemStatic* m_pOutputEnable;
	CPropTreeItemEdit*   m_pOutputSize;
	CPropTreeItemStatic* m_pCancelConfirmation;
	CPropTreeItemEdit*   m_pKeepAlivePeriod;

	CPropTreeItemCombo*  m_pExecutionStyle;
	CPropTreeItemCombo*  m_pExecutionStyleAlt;
	CPropTreeItemStatic* m_pHaltOnError;
	CPropTreeItemStatic* m_pHighlightExecuted;
	CPropTreeItemStatic* m_pAutoscrollToExecuted;
	CPropTreeItemStatic* m_pAutoscrollExecuted;
	CPropTreeItemStatic* m_pBufferedOutput;
	CPropTreeItemStatic* m_pIgnoreCommentsAfterSemicolon;

	CPropTreeItemStatic* m_pHistoryEnabled;
	CPropTreeItemCombo*  m_pHistoryAction;
	CPropTreeItemStatic* m_pHistoryKeepSelection;
	CPropTreeItemEdit*   m_pHistoryMaxCount;      // dependent on m_pHistoryEnabled
	CPropTreeItemEdit*   m_pHistoryMaxSize;       // dependent on m_pHistoryEnabled
	CPropTreeItemStatic* m_pHistoryValidOnly;
	CPropTreeItemStatic* m_pHistoryDistinct;      // dependent on m_pHistoryEnabled
	CPropTreeItemStatic* m_pHistoryGlobal;
	CPropTreeItemStatic* m_pHistoryPersistent;
};

//{{AFX_INSERT_LOCATION}}

#endif//__PropServerScriptPageTree_h__
