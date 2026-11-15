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

#ifndef __GridPageTree_h__
#define __GridPageTree_h__
#pragma once

#include "Settings/SQLToolsSettings.h"
#include "Common/PropTreePage.h"
#include "PropTree/PropTreeItemEdit.h"
#include "PropTree/PropTreeItemStatic.h"
#include "PropTree/PropTreeItemCombo.h"
#include "OpenGrid/GridExportSlots.h" // EExportSlot

class CGridPageTree : public Common::CPropTreePage
{
public:
	CGridPageTree (SQLToolsSettings& settings);
	virtual ~CGridPageTree ();

	BOOL m_bShowGridGroups;
	BOOL m_bShowExportGroups;

	BOOL m_bShowShowOnShiftOnly;
	BOOL m_bShowOnShiftOnly;
	BOOL m_bEnableFilenameTemplate;

	// TRUE in the per-command sheet, FALSE in the global one.
	BOOL             m_bSingleSlot;
	// Meaningful only when m_bSingleSlot is TRUE.
	OG2::EExportSlot m_slot;

protected:
	virtual void BuildTree ();

	virtual BOOL ApplyValues ();

	virtual void SaveTreeState ();

	virtual void OnItemChanged (CPropTreeItem* pItem);

	virtual int  GetTreeGutterLevels () const { return 2; }
	virtual BOOL GetTreeIndentsRootChildren () const { return TRUE; }

private:
	void UpdateEnabledState ();

	int GetExpFormat ();                    // per-command sheet: the one checked radio, or -1
	int GetSlotFormat (int nSlot);          // global sheet: that slot's combo, or -1

	SQLToolsSettings& m_settings;

	CPropTreeItem* m_pGridGroup;
	CPropTreeItem* m_pColumnFormatGroup;
	CPropTreeItem* m_pExportGroup;
	CPropTreeItem* m_pExportCsvXmlGroup;

	CPropTreeItemEdit*   m_pRowPrefetch;
	CPropTreeItemEdit*   m_pMaxLobLength;
	CPropTreeItemEdit*   m_pBlobHexRowLength;
	CPropTreeItemStatic* m_pSkipLobs;
	CPropTreeItemEdit*   m_pMaxColLength;
	CPropTreeItemEdit*   m_pMultilineCount;
	CPropTreeItemStatic* m_pAllowLessThanHeader;
	CPropTreeItemStatic* m_pAllowRememColWidth;
	CPropTreeItemStatic* m_pHeadersInLowerCase;
	CPropTreeItemStatic* m_pWraparound;

	CPropTreeItemCombo*  m_pDateFormat;
	CPropTreeItemCombo*  m_pTimestampFormat;
	CPropTreeItemCombo*  m_pTimestampTzFormat;
	CPropTreeItemCombo*  m_pIntervalYmFormat;
	CPropTreeItemCombo*  m_pIntervalDsFormat;
	CPropTreeItemCombo*  m_pNumberFormat;
	CPropTreeItemEdit*   m_pNullRepresentation;

	enum { EXP_FORMAT_COUNT = 11 };   // index == ETextFormat, so etfXlsx is the eleventh row
	enum { EXP_SLOT_COUNT   = 3  };   // index == EExportSlot

	CPropTreeItem*       m_pExpFormatGroup;                   // "Export format" sub group
	CPropTreeItemStatic* m_pExpFormatRadio[EXP_FORMAT_COUNT]; // index == ETextFormat

	// The global layout instead: one format combo and one pair of dependent rows per slot, all three
	// NULL on the per-command sheet, which shows a single slot as radios.
	CPropTreeItemCombo*  m_pExpSlotFormat    [EXP_SLOT_COUNT]; // index == EExportSlot
	CPropTreeItemStatic* m_pExpSlotWithHeader[EXP_SLOT_COUNT];
	CPropTreeItemStatic* m_pExpSlotJsonPretty[EXP_SLOT_COUNT];

	CPropTreeItemStatic* m_pExpWithHeaders;       // dependent on the selected format
	CPropTreeItemStatic* m_pExpJsonPretty;        // dependent on the selected format: JSON only
	CPropTreeItemEdit*   m_pExpFilenameTemplate;  // grayed when !m_bEnableFilenameTemplate
	CPropTreeItemStatic* m_pShowOnShiftOnly;      // optional, see m_bShowShowOnShiftOnly
	CPropTreeItemStatic* m_pExpAnsiLiterals;      // SQL format only, see GetCellStrForExport

	CPropTreeItemEdit*   m_pExpCommaChar;
	CPropTreeItemEdit*   m_pExpQuoteChar;
	CPropTreeItemEdit*   m_pExpQuoteEscapeChar;
	CPropTreeItemEdit*   m_pExpPrefixChar;
	CPropTreeItemEdit*   m_pExpXmlRootElement;
	CPropTreeItemEdit*   m_pExpXmlRecordElement;
	CPropTreeItemCombo*  m_pExpXmlFieldElementCase;
};

namespace OG2
{
    // The export sheet: one CGridPageTree, raised either per slot by the grid's copy/export commands or
    // globally by "Copy/Export Options...". Shared by GridView and GridWnd so the two hosts cannot drift.
    //
    // runExportSheetForSlot also owns the remembering: on OK it writes the sheet's values back into the
    // slot (storeExportSlot) and the global "show only on Shift" flag when it moved, so callers must not
    // store anything themselves. The sheet carries a "use these settings once" tick - seeded by holding
    // Ctrl+Shift when the command is invoked - and when it is ticked nothing is written at all: the
    // command still runs on the edited copy in `settings`, the profile is left exactly as it was.
    // Returns false only if the user cancelled, so callers can skip the operation.
    // `operation` names the command in the sheet title, e.g. L"Copy" -> "Copy settings".
    bool runExportSheetForSlot (SQLToolsSettings& settings, LPCTSTR operation, EExportSlot slot);
    bool runGlobalExportSheet  (SQLToolsSettings& settings);
}

//{{AFX_INSERT_LOCATION}}

#endif//__GridPageTree_h__
