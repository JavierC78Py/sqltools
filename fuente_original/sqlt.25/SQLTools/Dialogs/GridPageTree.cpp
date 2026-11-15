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

// 2026-09-22 replaces CPropGridPage1/2 and CPropGridOutputPage1/2, see the header

#include "stdafx.h"
#include "Dialogs/GridPageTree.h"
#include "Common/PropTreePageHelpers.h"
#include "Common/MyUtf.h"
#include <climits>  // INT_MAX, the upper limit of the classic "Row prefetch" DDV
#include "App/SQLTools.h" // GetSQLToolsSettingsForUpdate

	using namespace Common::PropTreeRows;

static BOOL s_bGridExpanded         = TRUE;
static BOOL s_bColumnFormatExpanded = TRUE;
static BOOL s_bExportExpanded       = TRUE;
static BOOL s_bExportFormatExpanded = TRUE;   // the "Export format" radio sub group
static BOOL s_bExportCsvXmlExpanded = TRUE;

static const int s_rowPrefetchMin  = 0,          s_rowPrefetchMax  = INT_MAX;
static const int s_maxLobLenMin    = 255,        s_maxLobLenMax    = 1024 * 1024;
static const int s_blobHexLenMin   = 1,          s_blobHexLenMax   = 512;
static const int s_maxColLenMin    = 1,          s_maxColLenMax    = 255;
static const int s_multilineMin    = 1,          s_multilineMax    = 16;

static const int s_rowPrefetchStep = 64;
static const int s_maxLobLenStep   = 1024;
static const int s_smallStep       = 1;
static const int s_blobHexLenStep  = 4;   // see the "Blob hex format length" info text

static const LPCTSTR s_exportFormats[] =
{
	_T("Plain text"),                  // 0 etfPlainText
	_T("Comma delimited (CSV)"),       // 1 etfQuoteDelimited
	_T("Tab delimited"),               // 2 etfTabDelimitedText
	_T("XML"),                         // 3 etfXmlElem
	_T("JSON"),                        // 4 etfJson
	_T("HTML"),                        // 5 etfHtml
	_T("SQL values"),                  // 6 etfPLSQL1
	_T("SQL expression"),              // 7 etfPLSQL2
	_T("SQL IN list"),                 // 8 etfPLSQL3
	_T("SQL Inserts"),                 // 9 etfPLSQL4
	_T("Excel workbook (XLSX)"),       // 10 etfXlsx
};

static const LPCTSTR s_exportFormatInfos[] =
{
	_T("Fixed width text: every value padded with spaces to the width of its column and aligned the way the grid aligns that column, so the columns line up in a fixed pitch font."),
	_T("Values separated by the comma char and, when they need it, enclosed in the quote char - both set in the 'Export CSV / XML' group below, with the escape and the prefix char. A quote inside a value is escaped. What a spreadsheet reads."),
	_T("Values separated by a tab and written as they are, unquoted. The format a spreadsheet reads when it opens a plain text file."),
	_T("Every row becomes one record element and every value an element of its own, named after its column. The root, record and field element names come from the 'Export CSV / XML' group."),
	_T("One JSON document: a 'meta' object with the column names and types, then the rows as an array of objects keyed by column name. Numbers stay numbers, a NULL becomes null, a RAW or a BLOB becomes base64. Pretty-printed by default; the 'Pretty-print JSON' row below switches to compact output, one row per line."),
	_T("An HTML table, so the rows can be pasted into a web page or opened in a browser."),
	_T("The values of every row as SQL literals, one row per line, separated by commas."),
	_T("Every row as a condition - COLUMN = value, joined by AND - ready to paste into a WHERE clause. A NULL value becomes COLUMN IS NULL."),
	_T("One IN list for the selection: the column names first, then one (value, value) tuple per row."),
	_T("One INSERT INTO &table (columns) VALUES (values); statement per row, preceded by a 'define table =' line and ended by COMMIT;. Needs the normal row-wise grid orientation - a transposed grid is refused."),
	_T("A real Excel workbook of one sheet: numbers stay numbers, a DATE or a TIMESTAMP becomes a date Excel sorts and filters, a NULL becomes an empty cell, and the column widths come from the grid. What Excel has no cell for - an interval, a timestamp with a time zone, a NUMBER longer than 15 digits, a RAW - is written as the text the grid showed. File export only: the clipboard and 'Open in Editor' carry text, and fall back to CSV."),
};

static const int s_etfXmlElem = 3;
static const int s_etfJson    = 4;
static const int s_etfPLSQL1  = 6;
static const int s_etfPLSQL4  = 9;

static bool hasColumnHeaders (int nFormat)
{
	// JSON joins XML-elem and the SQL formats here: its column names are the record keys and
	// meta.columns lists them, so the toggle would be a live control that silently does nothing.
	return !(nFormat == s_etfXmlElem || nFormat == s_etfJson
			|| (nFormat >= s_etfPLSQL1 && nFormat <= s_etfPLSQL4));
}

// Indentation is a JSON-only notion, so the row is grayed for the other nine formats rather than
// left live - the reason hasColumnHeaders excludes JSON (plan 4.4a).
static bool isJsonFormat (int nFormat)
{
	return nFormat == s_etfJson;
}

// The global layout's three rows, in EExportSlot order.
static const LPCTSTR s_slotFormatLabels[] =
{
	_T("Copy to clipboard"),
	_T("Open in editor"),
	_T("Save to file"),
};

static const LPCTSTR s_slotFormatInfos[] =
{
	_T("Format of the text Copy, Copy All, Copy Row and Copy Column place on the clipboard. The Excel workbook is not offered: the clipboard carries text."),
	_T("Format of the text Open in Editor places in a new editor buffer. The Excel workbook is not offered: an editor buffer carries text."),
	_T("Format of the file Export and Export and Open write. Every format is offered, the Excel workbook included."),
};

// Suffix of the two dependent rows, so each names the destination it belongs to.
static const LPCTSTR s_slotNames[] = { _T("clipboard"), _T("editor"), _T("file") };

// A combo offers a contiguous run of s_exportFormats[], so the count it is built with is the length of
// the offered prefix.
static constexpr int countOfferedPrefix (OG2::EExportSlot slot)
{
	int n = 0;
	while (n < int(_countof(s_exportFormats)) && OG2::isFormatOffered(slot, n))
		n++;
	return n;
}

static constexpr int countOfferedTotal (OG2::EExportSlot slot)
{
	int n = 0;
	for (int i = 0; i < int(_countof(s_exportFormats)); i++)
		if (OG2::isFormatOffered(slot, i))
			n++;
	return n;
}

static_assert(countOfferedPrefix(OG2::eslotClipboard) == countOfferedTotal(OG2::eslotClipboard)
	&& countOfferedPrefix(OG2::eslotEditor) == countOfferedTotal(OG2::eslotEditor)
	&& countOfferedPrefix(OG2::eslotFile)   == countOfferedTotal(OG2::eslotFile),
	"the formats a slot does not offer must sit at the tail of s_exportFormats[], or a combo would "
	"silently drop the offered ones after them");

static const LPCTSTR s_xmlFieldElementCases[] =
{
	_T("As in grid header"),
	_T("Lowercase"),
	_T("Uppercase"),
};

static const LPCTSTR s_dateFormats[] =
{
	_T("dd.mm.yy hh24:mi:ss"),
	_T("dd.mm.yy hh24:mi"),
	_T("dd.mm.yy"),
	_T("dd.mm.yyyy hh24:mi:ss"),
	_T("dd.mm.yyyy hh24:mi"),
	_T("dd.mm.yyyy"),
	_T("mm/dd/yy hh12:mi:ss am"),
	_T("mm/dd/yy hh12:mi am"),
	_T("mm/dd/yy"),
	_T("mm/dd/yyyy hh12:mi:ss am"),
	_T("mm/dd/yyyy hh12:mi am"),
	_T("mm/dd/yyyy"),
};

// Interval pictures. These are SQLTools format pictures, not NLS parameters - Oracle defines no
// NLS_*_FORMAT for intervals. The two types resolve disjoint tokens (YEAR TO MONTH: Y..YYYY, M, MM;
// DAY TO SECOND: DD, HH, MI, SS, FF[n]) and SqlFormat emits a token it cannot resolve literally, so
// the presets below never mix the two vocabularies. In particular DAY TO SECOND takes HH, not the
// hh24 the retired TIME list used - there is no 24-hour token here, so "hh24" would render the hours
// followed by a literal "24". A leading sign is added automatically for negative values unless the
// picture carries its own sign token; an empty picture selects the Oracle default rendering.
static const LPCTSTR s_intervalYmFormats[] =
{
	_T("y-m"),
	_T("yyyy-mm"),
	_T("yy-mm"),
	_T("y\" yrs \"m\" mo\""),
	_T("yyyy\" years \"mm\" months\""),
};

static const LPCTSTR s_intervalDsFormats[] =
{
	_T("dd hh:mi:ss.ff9"),
	_T("dd hh:mi:ss.ff6"),
	_T("dd hh:mi:ss.ff3"),
	_T("dd hh:mi:ss"),
	_T("dd\"d \"hh:mi:ss.ff6"),
};
// Deliberately no day-less preset such as "hh:mi:ss.ff6": a picture renders only the tokens it
// contains, so dropping DD silently discards the day count - the same loss of a leading component
// that the shared-picture bug caused. Anyone who wants one can type it into the editable combo.

static const LPCTSTR s_timestampFormats[] =
{
	_T("dd.mm.yy hh24:mi:ss.ff3"),
	_T("dd.mm.yy hh24:mi:ss"),
	_T("dd.mm.yy hh24:mi"),
	_T("dd.mm.yy"),
	_T("dd.mm.yyyy hh24:mi:ss.ff3"),
	_T("dd.mm.yyyy hh24:mi:ss"),
	_T("dd.mm.yyyy hh24:mi"),
	_T("dd.mm.yyyy"),
	_T("mm/dd/yy hh12:mi:ss.ff3 am"),
	_T("mm/dd/yy hh12:mi:ss am"),
	_T("mm/dd/yy hh12:mi am"),
	_T("mm/dd/yy"),
	_T("mm/dd/yyyy hh12:mi:ss.ff3 am"),
	_T("mm/dd/yyyy hh12:mi:ss am"),
	_T("mm/dd/yyyy hh12:mi am"),
	_T("mm/dd/yyyy"),
};

static const LPCTSTR s_timestampTzFormats[] =
{
	_T("dd.mm.yy hh24:mi:ss.ff3 tzr"),
	_T("dd.mm.yy hh24:mi:ss tzr"),
	_T("dd.mm.yy hh24:mi tzr"),
	_T("dd.mm.yy"),
	_T("dd.mm.yyyy hh24:mi:ss.ff3 tzr"),
	_T("dd.mm.yyyy hh24:mi:ss tzr"),
	_T("dd.mm.yyyy hh24:mi tzr"),
	_T("dd.mm.yyyy"),
	_T("mm/dd/yy hh12:mi:ss.ff3 am tzr"),
	_T("mm/dd/yy hh12:mi:ss am tzr"),
	_T("mm/dd/yy hh12:mi am tzr"),
	_T("mm/dd/yy"),
	_T("mm/dd/yyyy hh12:mi:ss.ff3 am tzr"),
	_T("mm/dd/yyyy hh12:mi:ss am tzr"),
	_T("mm/dd/yyyy hh12:mi am tzr"),
	_T("mm/dd/yyyy"),
};

static const LPCTSTR s_numberFormats[] =
{
	_T("TM9"),
	_T("FM999g999g999g999d00"),
	_T("FM9d999eeee"),
};

static BOOL checkXmlElementName (const CString& value, LPCTSTR fieldName)
{
	if (value.IsEmpty())
	{
		AfxMessageBox(CString(fieldName) + _T(" is required!"), MB_OK | MB_ICONSTOP);
		return FALSE;
	}

	if (!(_istalpha(value[0]) || value[0] == _T('_')))
	{
		AfxMessageBox(CString(fieldName) + _T(" cannot start with \"") + value[0] + _T("\"!"),
			MB_OK | MB_ICONSTOP);
		return FALSE;
	}

	for (int i = 0; i < value.GetLength(); i++)
	{
		TCHAR ch = value[i];

		if (!(_istalnum(ch) || ch == _T('.') || ch == _T('-') || ch == _T('_')))
		{
			AfxMessageBox(CString(fieldName) + _T(" cannot contain \"") + ch + _T("\"!"),
				MB_OK | MB_ICONSTOP);
			return FALSE;
		}
	}

	return TRUE;
}

CGridPageTree::CGridPageTree (SQLToolsSettings& settings)
	: Common::CPropTreePage(_T("SQLTools::Data Grid"), 0 /*no help topic*/),
	m_bShowGridGroups(TRUE),
	m_bShowExportGroups(TRUE),
	m_bShowShowOnShiftOnly(TRUE),
	m_bShowOnShiftOnly(settings.GetGridExpShowOnShiftOnly() ? TRUE : FALSE),
	m_bEnableFilenameTemplate(TRUE),
	m_bSingleSlot(FALSE),
	m_slot(OG2::eslotFile),
	m_settings(settings),
	m_pGridGroup(nullptr), m_pColumnFormatGroup(nullptr),
	m_pExportGroup(nullptr), m_pExportCsvXmlGroup(nullptr),
	m_pRowPrefetch(nullptr), m_pMaxLobLength(nullptr), m_pBlobHexRowLength(nullptr),
	m_pSkipLobs(nullptr), m_pMaxColLength(nullptr), m_pMultilineCount(nullptr),
	m_pAllowLessThanHeader(nullptr), m_pAllowRememColWidth(nullptr),
	m_pHeadersInLowerCase(nullptr), m_pWraparound(nullptr),
	m_pDateFormat(nullptr), m_pTimestampFormat(nullptr),
	m_pTimestampTzFormat(nullptr), m_pIntervalYmFormat(nullptr), m_pIntervalDsFormat(nullptr),
	m_pNumberFormat(nullptr), m_pNullRepresentation(nullptr),
	m_pExpFormatGroup(nullptr), m_pExpFormatRadio(),
	m_pExpSlotFormat(), m_pExpSlotWithHeader(), m_pExpSlotJsonPretty(),
	m_pExpWithHeaders(nullptr),
	m_pExpJsonPretty(nullptr), m_pExpFilenameTemplate(nullptr), m_pShowOnShiftOnly(nullptr),
	m_pExpAnsiLiterals(nullptr),
	m_pExpCommaChar(nullptr), m_pExpQuoteChar(nullptr),
	m_pExpQuoteEscapeChar(nullptr), m_pExpPrefixChar(nullptr),
	m_pExpXmlRootElement(nullptr), m_pExpXmlRecordElement(nullptr),
	m_pExpXmlFieldElementCase(nullptr)
{
}

CGridPageTree::~CGridPageTree ()
{
}

void CGridPageTree::BuildTree ()
{
	if (m_bShowGridGroups)
	{
		m_pGridGroup = InsertGroup(m_tree, _T("Data Grid"),
			_T("How a data grid fetches and displays its rows: row prefetch, the size limits of the values it reads, and the column auto fit behaviour."),
			s_bGridExpanded);

		m_pRowPrefetch = InsertNumberEdit(m_tree, m_pGridGroup,
			_T("Row prefetch"),
			_T("Number of rows the Oracle client reads in one round trip. A larger number scrolls through a big result set faster but needs more memory and makes the first fetch slower; 0 leaves the choice to the client."),
			m_settings.GetGridPrefetchLimit(), s_rowPrefetchStep, s_rowPrefetchMin, s_rowPrefetchMax);

		m_pMaxLobLength = InsertNumberEdit(m_tree, m_pGridGroup,
			_T("Max column fetch size"),
			_T("Maximum number of characters read from a LONG, CLOB, BLOB or BFILE column for display. The rest of the value is not fetched."),
			m_settings.GetGridMaxLobLength(), s_maxLobLenStep, s_maxLobLenMin, s_maxLobLenMax);

		m_pBlobHexRowLength = InsertNumberEdit(m_tree, m_pGridGroup,
			_T("Blob hex format length"),
			_T("Width of a line when a BLOB or BFILE value is shown as a hex dump, counted in hex characters (two per byte), so use a multiple of 4. The default 32 prints 16 bytes per line."),
			m_settings.GetGridBlobHexRowLength(), s_blobHexLenStep, s_blobHexLenMin, s_blobHexLenMax);

		m_pSkipLobs = InsertCheckedStatic(m_tree, m_pGridGroup,
			_T("Disable LOB fetching"),
			_T("Do not read LOB column values at all. Fetching a result set with big LOBs is much faster, the grid shows their length only."),
			m_settings.GetGridSkipLobs());

		m_pMaxColLength = InsertNumberEdit(m_tree, m_pGridGroup,
			_T("Max column length"),
			_T("Upper limit of the width the column auto fit may give to a column, in characters."),
			m_settings.GetGridMaxColLength(), s_smallStep, s_maxColLenMin, s_maxColLenMax);

		m_pMultilineCount = InsertNumberEdit(m_tree, m_pGridGroup,
			_T("Multi line limit"),
			_T("Maximum number of lines shown in a multi line cell."),
			m_settings.GetGridMultilineCount(), s_smallStep, s_multilineMin, s_multilineMax);

		m_pAllowLessThanHeader = InsertCheckedStatic(m_tree, m_pGridGroup,
			_T("Auto fit may shrink below header width"),
			_T("Let the column auto fit make a column narrower than its own header. Applies only while columns are fitted by data."),
			m_settings.GetGridAllowLessThanHeader());

		m_pAllowRememColWidth = InsertCheckedStatic(m_tree, m_pGridGroup,
			_T("Remember manually changed column width"),
			_T("Keep the width of a column you resized by hand and use it again the next time the same column is fetched."),
			m_settings.GetGridAllowRememColWidth());

		m_pHeadersInLowerCase = InsertCheckedStatic(m_tree, m_pGridGroup,
			_T("Headers in lowercase"),
			_T("Show the column headers in lower case."),
			m_settings.GetGridHeadersInLowerCase());

		m_pWraparound = InsertCheckedStatic(m_tree, m_pGridGroup,
			_T("Wrap around cursor"),
			_T("Move the cursor to the other end of the grid when it goes past the first or the last row or column."),
			m_settings.GetGridWraparound());

		m_pColumnFormatGroup = InsertGroup(m_tree, _T("Column Formats"),
			_T("The format strings used to draw the grid columns, and the text shown for NULL. The date, timestamp and number pictures follow the Oracle NLS format models. The two interval pictures are SQLTools format models rather than NLS ones, because Oracle defines no NLS parameter for an INTERVAL. ATTENTION: format validation is not implemented yet, so you may get a runtime error when you execute a query!"),
			s_bColumnFormatExpanded);

		m_pDateFormat = InsertComboText(m_tree, m_pColumnFormatGroup,
			_T("Date format"),
			_T("NLS_DATE_FORMAT used for the DATE columns of the grid."),
			s_dateFormats, _countof(s_dateFormats),
			Common::wstr(m_settings.GetGridNlsDateFormat()).c_str());

		m_pTimestampFormat = InsertComboText(m_tree, m_pColumnFormatGroup,
			_T("Timestamp format"),
			_T("NLS_TIMESTAMP_FORMAT used for the TIMESTAMP columns of the grid."),
			s_timestampFormats, _countof(s_timestampFormats),
			Common::wstr(m_settings.GetGridNlsTimestampFormat()).c_str());

		m_pTimestampTzFormat = InsertComboText(m_tree, m_pColumnFormatGroup,
			_T("Timestamp format with TZ"),
			_T("NLS_TIMESTAMP_TZ_FORMAT used for the TIMESTAMP WITH TIME ZONE columns of the grid."),
			s_timestampTzFormats, _countof(s_timestampTzFormats),
			Common::wstr(m_settings.GetGridNlsTimestampTzFormat()).c_str());

		// One row per Oracle interval type. They cannot share a picture: SqlFormat emits a token it
		// cannot resolve literally, so a DAY TO SECOND picture renders every YEAR TO MONTH value as
		// the picture's own text rather than as a duration.
		m_pIntervalYmFormat = InsertComboText(m_tree, m_pColumnFormatGroup,
			_T("Interval YEAR TO MONTH format"),
			_T("Format picture for the INTERVAL YEAR TO MONTH columns of the grid. Tokens: Y, YY, YYY, YYYY for the years and M, MM for the months; anything else is written out literally, and text in double quotes is always literal. YYYY and MM zero-pad to their width, Y and M do not. Only the tokens you include are rendered, so leaving out Y drops the year count. Leave empty for the default Oracle rendering, +Y-MM. Negative values get a leading minus sign unless the picture has its own sign token."),
			s_intervalYmFormats, _countof(s_intervalYmFormats),
			Common::wstr(m_settings.GetGridNlsIntervalYmFormat()).c_str());

		m_pIntervalDsFormat = InsertComboText(m_tree, m_pColumnFormatGroup,
			_T("Interval DAY TO SECOND format"),
			_T("Format picture for the INTERVAL DAY TO SECOND columns of the grid. Tokens: DD, HH, MI, SS and FF1..FF9 for the fractional seconds; anything else is written out literally, and text in double quotes is always literal. Note HH, not HH24 - there is no 24-hour token here. Only the tokens you include are rendered, so leaving out DD drops the day count. Leave empty for the default Oracle rendering, +D HH:MI:SS.FF9. Negative values get a leading minus sign unless the picture has its own sign token."),
			s_intervalDsFormats, _countof(s_intervalDsFormats),
			Common::wstr(m_settings.GetGridNlsIntervalDsFormat()).c_str());

		m_pNumberFormat = InsertComboText(m_tree, m_pColumnFormatGroup,
			_T("Number format"),
			_T("NLS_NUMBER_FORMAT used for the numeric columns of the grid."),
			s_numberFormats, _countof(s_numberFormats),
			Common::wstr(m_settings.GetGridNlsNumberFormat()).c_str());

		m_pNullRepresentation = InsertEditText(m_tree, m_pColumnFormatGroup,
			_T("NULL representation"),
			_T("Text the grid shows in place of a NULL value."),
			Common::wstr(m_settings.GetGridNullRepresentation()).c_str(), 255);
	}

	if (m_bShowExportGroups)
	{
		const BOOL bGlobal = m_bSingleSlot ? FALSE : TRUE;

		m_pExportGroup = InsertGroup(m_tree, _T("Export"),
			bGlobal
				? _T("The format, headers and JSON layout remembered for each destination. Copy, Open in Editor and Export use their own; the CSV characters, XML element names and file name below are shared by all three.")
				: _T("The format used when a data grid is copied to the clipboard or written to a file, and the name of that file."),
			s_bExportExpanded);

		static_assert(_countof(s_exportFormats) == EXP_FORMAT_COUNT
			&& _countof(s_exportFormatInfos) == EXP_FORMAT_COUNT,
			"s_exportFormats[] and s_exportFormatInfos[] must hold one entry per "
			"GridView::ETextFormat, in enum order, for the row index to BE the format");
		static_assert(_countof(s_slotFormatLabels) == EXP_SLOT_COUNT
			&& _countof(s_slotFormatInfos) == EXP_SLOT_COUNT
			&& _countof(s_slotNames) == EXP_SLOT_COUNT,
			"the slot tables must hold one entry per OG2::EExportSlot, in enum order");

		m_pExpFormatGroup = InsertGroup(m_tree, _T("Export format"),
			bGlobal
				? _T("Format of the text a grid is copied or exported in. Exactly one format is selected for each destination.")
				: _T("Format of the text a grid is copied or exported in. Exactly one of the formats is selected, and the pane below shows what the selected one writes."),
			s_bExportFormatExpanded, m_pExportGroup);

		if (bGlobal)
		{
			// Three combos, and the dependent rows name their slot in the label rather than nesting
			// under it, which would need a third gutter level.
			for (int s = 0; s < EXP_SLOT_COUNT; s++)
			{
				const OG2::EExportSlot slot = (OG2::EExportSlot)s;

				m_pExpSlotFormat[s] = InsertComboIndexOnSelect(m_tree, m_pExpFormatGroup,
					s_slotFormatLabels[s], s_slotFormatInfos[s],
					s_exportFormats, countOfferedPrefix(slot),
					OG2::clampFormatForSlot(slot, OG2::slotFormat(m_settings, slot)));
			}

			for (int s = 0; s < EXP_SLOT_COUNT; s++)
			{
				const OG2::EExportSlot slot = (OG2::EExportSlot)s;
				const CString strSlot = s_slotNames[s];

				m_pExpSlotWithHeader[s] = InsertCheckedStatic(m_tree, m_pExpFormatGroup,
					_T("With column headers (") + strSlot + _T(")"),
					_T("Put the column names in the first line of the text this destination writes. Not available for the XML, the JSON and the SQL formats."),
					OG2::slotWithHeader(m_settings, slot));

				// Grayed rather than hidden for the other nine formats: a row that came and went with
				// the combo would move every row below it under the mouse.
				m_pExpSlotJsonPretty[s] = InsertCheckedStatic(m_tree, m_pExpFormatGroup,
					_T("Pretty-print JSON (") + strSlot + _T(")"),
					_T("Write this destination's JSON export indented, one member per line, so it can be read and compared by hand. Unchecked writes it compact - no indentation, one row per line - which is what a machine consumer wants. Applies to the JSON format only."),
					OG2::slotJsonPretty(m_settings, slot));
			}
		}
		else
		{
			// A format this destination cannot write must still select the row it will fall back to;
			// unchecked leaves the group with no selection and GetExpFormat() returning -1.
			int nExpFormat = OG2::clampFormatForSlot(m_slot, m_settings.GetGridExpFormat());

			if (nExpFormat < 0 || nExpFormat >= EXP_FORMAT_COUNT)
				nExpFormat = 0;

			for (int i = 0; i < EXP_FORMAT_COUNT; i++)
			{
				// Omitted, not disabled: this filter cannot change while the sheet is open. Entries
				// stay NULL, never compacted - the index IS the format.
				if (!OG2::isFormatOffered(m_slot, i))
				{
					m_pExpFormatRadio[i] = nullptr;
					continue;
				}

				m_pExpFormatRadio[i] = InsertRadio(m_tree, m_pExpFormatGroup,
					s_exportFormats[i], s_exportFormatInfos[i], i == nExpFormat);
			}

			m_pExpWithHeaders = InsertCheckedStatic(m_tree, m_pExportGroup,
				_T("With column headers"),
				_T("Put the column names in the first line of the exported text. Not available for the XML, the JSON and the SQL formats."),
				m_settings.GetGridExpWithHeader());
			m_pExpWithHeaders->SetIndent(CPropTreeItem::GetLevelIndent() * 2); // dependent row

			// Grayed rather than hidden for the other nine formats: a row that came and went with the
			// radio would move every row below it under the mouse.
			m_pExpJsonPretty = InsertCheckedStatic(m_tree, m_pExportGroup,
				_T("Pretty-print JSON"),
				_T("Write the JSON export indented, one member per line, so it can be read and compared by hand. Unchecked writes it compact - no indentation, one row per line - which is what a machine consumer wants and what makes the file smaller. Applies to the JSON format only."),
				m_settings.GetGridExpJsonPretty());
			m_pExpJsonPretty->SetIndent(CPropTreeItem::GetLevelIndent() * 2); // dependent row
		}

		m_pExpFilenameTemplate = InsertEditText(m_tree, m_pExportGroup,
			_T("Filename template"),
			_T("Name of the file a grid is exported to. Substitutions: &n - document counter; &dd, &mm, &month, &mon, &yyyy, &yy, &hh24, &hh12, &am, &mi, &ss - date and time; &script - name of the script."),
			Common::wstr(m_settings.GetGridExpFilenameTemplate()).c_str());

		m_pExpAnsiLiterals = InsertCheckedStatic(m_tree, m_pExportGroup,
			_T("ANSI DATE and TIMESTAMP literals"),
			_T("Write DATE '2026-10-01' and TIMESTAMP '2026-10-01 12:30:45.123456789' instead of TO_DATE(...) and TO_TIMESTAMP(...). An ANSI DATE literal cannot carry a time, so a DATE with a non-midnight time keeps TO_DATE, and a TIMESTAMP WITH TIME ZONE always keeps TO_TIMESTAMP_TZ because no ANSI literal carries a zone. Applies to the SQL export formats only."),
			m_settings.GetGridExpAnsiLiterals());

		if (m_bShowShowOnShiftOnly)
		{
			// Two wordings for one property: on the global page "this dialog" has no referent, so the
			// row names the dialog and the commands it governs instead.
			m_pShowOnShiftOnly = InsertCheckedStatic(m_tree, m_pExportGroup,
				bGlobal
					? _T("Only show the format dialog when the Shift key is down")
					: _T("Only show this dialog when the Shift key is down"),
				bGlobal
					? _T("Run Copy, Copy All, Open in Editor and Export with the format remembered for that command, and raise the format dialog only while the Shift key is held down. Unticked, the dialog is raised before every one of those commands. The \"Shift for opt\" hints in the grid menus describe the ticked state.")
					: _T("Skip this dialog and use the format remembered for this command, unless the Shift key is held down when the command is invoked. When checked, the \"Shift for opt\" hints in the grid menus are accurate."),
				m_bShowOnShiftOnly ? true : false);
		}

		m_pExportCsvXmlGroup = InsertGroup(m_tree, _T("Export CSV / XML"),
			_T("The delimiters of the CSV export and the element names of the XML export."),
			s_bExportCsvXmlExpanded);

		m_pExpCommaChar = InsertEditText(m_tree, m_pExportCsvXmlGroup,
			_T("Comma char"),
			_T("Character that separates the columns of the CSV export. Use a semicolon for the locales whose decimal separator is a comma."),
			Common::wstr(m_settings.GetGridExpCommaChar()).c_str(), 1);

		m_pExpQuoteChar = InsertEditText(m_tree, m_pExportCsvXmlGroup,
			_T("Quote char"),
			_T("Character that encloses a CSV value which needs quoting, i.e. a text value or one containing the separator."),
			Common::wstr(m_settings.GetGridExpQuoteChar()).c_str(), 1);

		m_pExpQuoteEscapeChar = InsertEditText(m_tree, m_pExportCsvXmlGroup,
			_T("Quote escape char"),
			_T("Character written in front of a quote character inside a quoted CSV value - the quote itself in the standard CSV dialect."),
			Common::wstr(m_settings.GetGridExpQuoteEscapeChar()).c_str(), 1);

		m_pExpPrefixChar = InsertEditText(m_tree, m_pExportCsvXmlGroup,
			_T("Text prefix char"),
			_T("Character written in front of a quoted CSV value; the default = makes a spreadsheet read the value as text and keep its leading zeros."),
			Common::wstr(m_settings.GetGridExpPrefixChar()).c_str(), 1);

		m_pExpXmlRootElement = InsertEditText(m_tree, m_pExportCsvXmlGroup,
			_T("Root element"),
			_T("Name of the element that encloses all records of the XML export. Letters, digits, '.', '-' and '_', the first character a letter or '_'."),
			Common::wstr(m_settings.GetGridExpXmlRootElement()).c_str());

		m_pExpXmlRecordElement = InsertEditText(m_tree, m_pExportCsvXmlGroup,
			_T("Record element"),
			_T("Name of the element written for every row of the XML export. Letters, digits, '.', '-' and '_', the first character a letter or '_'."),
			Common::wstr(m_settings.GetGridExpXmlRecordElement()).c_str());

		m_pExpXmlFieldElementCase = InsertComboIndex(m_tree, m_pExportCsvXmlGroup,
			_T("Field element case"),
			_T("Case of the element name written for a column of the XML export."),
			s_xmlFieldElementCases, _countof(s_xmlFieldElementCases),
			m_settings.GetGridExpXmlFieldElementCase());

	}

	UpdateEnabledState();
}

int CGridPageTree::GetExpFormat ()
{
	for (int i = 0; i < EXP_FORMAT_COUNT; i++)
	{
		if (m_pExpFormatRadio[i] && m_pExpFormatRadio[i]->IsChecked())
			return i;
	}

	return -1;
}

int CGridPageTree::GetSlotFormat (int nSlot)
{
	if (nSlot < 0 || nSlot >= EXP_SLOT_COUNT || !m_pExpSlotFormat[nSlot])
		return -1;

	return (int)m_pExpSlotFormat[nSlot]->GetItemValue();
}

void CGridPageTree::UpdateEnabledState ()
{
	if (m_pExpWithHeaders && m_pExpFormatGroup)
		m_pExpWithHeaders->ReadOnly(!hasColumnHeaders(GetExpFormat()));

	if (m_pExpJsonPretty)
		m_pExpJsonPretty->ReadOnly(!isJsonFormat(GetExpFormat()));

	// Each slot's two dependent rows track that slot's own format, never another slot's.
	for (int s = 0; s < EXP_SLOT_COUNT; s++)
	{
		const int nFormat = GetSlotFormat(s);

		if (m_pExpSlotWithHeader[s])
			m_pExpSlotWithHeader[s]->ReadOnly(!hasColumnHeaders(nFormat));

		if (m_pExpSlotJsonPretty[s])
			m_pExpSlotJsonPretty[s]->ReadOnly(!isJsonFormat(nFormat));
	}

	if (m_pExpFilenameTemplate)
		m_pExpFilenameTemplate->ReadOnly(!m_bEnableFilenameTemplate);

	RedrawTree();
}

void CGridPageTree::OnItemChanged (CPropTreeItem* pItem)
{
	if (pItem && m_pExpFormatGroup && pItem->GetParent() == m_pExpFormatGroup)
		UpdateEnabledState();
}

BOOL CGridPageTree::ApplyValues ()
{
	if (m_pExportCsvXmlGroup)
	{
		const CString rootElement   = m_pExpXmlRootElement->GetItemValue();
		const CString recordElement = m_pExpXmlRecordElement->GetItemValue();

		if (!checkXmlElementName(rootElement,   _T("\"Root element\""))
		||  !checkXmlElementName(recordElement, _T("\"Record element\"")))
			return FALSE;
	}

	int nValue = 0;

	if (m_pGridGroup)
	{
		m_pRowPrefetch->GetItemValue(nValue);
		m_settings.SetGridPrefetchLimit(nValue, false /*notify*/);

		m_pMaxLobLength->GetItemValue(nValue);
		m_settings.SetGridMaxLobLength(nValue, false /*notify*/);

		m_pBlobHexRowLength->GetItemValue(nValue);
		m_settings.SetGridBlobHexRowLength(nValue, false /*notify*/);

		m_settings.SetGridSkipLobs(m_pSkipLobs->IsChecked() ? true : false, false /*notify*/);

		m_pMaxColLength->GetItemValue(nValue);
		m_settings.SetGridMaxColLength(nValue, false /*notify*/);

		m_pMultilineCount->GetItemValue(nValue);
		m_settings.SetGridMultilineCount(nValue, false /*notify*/);

		m_settings.SetGridAllowLessThanHeader(m_pAllowLessThanHeader->IsChecked() ? true : false, false /*notify*/);
		m_settings.SetGridAllowRememColWidth (m_pAllowRememColWidth->IsChecked()  ? true : false, false /*notify*/);
		m_settings.SetGridHeadersInLowerCase (m_pHeadersInLowerCase->IsChecked()  ? true : false, false /*notify*/);
		m_settings.SetGridWraparound         (m_pWraparound->IsChecked()          ? true : false, false /*notify*/);
	}

	if (m_pColumnFormatGroup)
	{
		m_settings.SetGridNlsDateFormat      (Common::str(m_pDateFormat->GetItemValueText()),         false /*notify*/);
		m_settings.SetGridNlsTimestampFormat (Common::str(m_pTimestampFormat->GetItemValueText()),   false /*notify*/);
		m_settings.SetGridNlsTimestampTzFormat(Common::str(m_pTimestampTzFormat->GetItemValueText()), false /*notify*/);
		m_settings.SetGridNlsIntervalYmFormat(Common::str(m_pIntervalYmFormat->GetItemValueText()),  false /*notify*/);
		m_settings.SetGridNlsIntervalDsFormat(Common::str(m_pIntervalDsFormat->GetItemValueText()),  false /*notify*/);
		m_settings.SetGridNlsNumberFormat    (Common::str(m_pNumberFormat->GetItemValueText()),      false /*notify*/);

		m_settings.SetGridNullRepresentation (Common::str(m_pNullRepresentation->GetItemValue()),    false /*notify*/);
	}

	if (m_pExportGroup)
	{
		if (m_bSingleSlot)
		{
			// The per-command sheet edits the shared three on a copy of the settings;
			// runExportSheetForSlot moves them into the slot with storeExportSlot, unless the
			// user ticked "use these settings once".
			const int nExpFormat = GetExpFormat();

			if (nExpFormat >= 0)
				m_settings.SetGridExpFormat(nExpFormat, false /*notify*/);

			m_settings.SetGridExpWithHeader(m_pExpWithHeaders->IsChecked() ? true : false, false /*notify*/);

			m_settings.SetGridExpJsonPretty(m_pExpJsonPretty->IsChecked() ? true : false, false /*notify*/);
		}
		else
		{
			// The global sheet edits the nine slot properties: no consumer reads the shared three any
			// more, they are only the migration seed.
			for (int s = 0; s < EXP_SLOT_COUNT; s++)
			{
				const OG2::EExportSlot slot = (OG2::EExportSlot)s;
				const int nFormat = GetSlotFormat(s);

				if (nFormat >= 0)
					OG2::setSlotFormat(m_settings, slot, nFormat, false /*notify*/);

				OG2::setSlotWithHeader(m_settings, slot, m_pExpSlotWithHeader[s]->IsChecked() ? true : false, false /*notify*/);

				OG2::setSlotJsonPretty(m_settings, slot, m_pExpSlotJsonPretty[s]->IsChecked() ? true : false, false /*notify*/);
			}
		}

		m_settings.SetGridExpFilenameTemplate(Common::str(m_pExpFilenameTemplate->GetItemValue()), false /*notify*/);

		m_settings.SetGridExpAnsiLiterals(m_pExpAnsiLiterals->IsChecked() ? true : false, false /*notify*/);

		if (m_pShowOnShiftOnly)
		{
			m_bShowOnShiftOnly = m_pShowOnShiftOnly->IsChecked() ? TRUE : FALSE;

			// Only the global sheet owns the property here; the per-command sheet reports it out
			// through m_bShowOnShiftOnly and its caller writes it.
			if (!m_bSingleSlot)
				m_settings.SetGridExpShowOnShiftOnly(m_bShowOnShiftOnly ? true : false, false /*notify*/);
		}
	}

	if (m_pExportCsvXmlGroup)
	{
		m_settings.SetGridExpCommaChar      (Common::str(m_pExpCommaChar->GetItemValue()),      false /*notify*/);
		m_settings.SetGridExpQuoteChar      (Common::str(m_pExpQuoteChar->GetItemValue()),      false /*notify*/);
		m_settings.SetGridExpQuoteEscapeChar(Common::str(m_pExpQuoteEscapeChar->GetItemValue()), false /*notify*/);
		m_settings.SetGridExpPrefixChar     (Common::str(m_pExpPrefixChar->GetItemValue()),     false /*notify*/);

		m_settings.SetGridExpXmlRootElement  (Common::str(m_pExpXmlRootElement->GetItemValue()),   false /*notify*/);
		m_settings.SetGridExpXmlRecordElement(Common::str(m_pExpXmlRecordElement->GetItemValue()), false /*notify*/);

		m_settings.SetGridExpXmlFieldElementCase((int)m_pExpXmlFieldElementCase->GetItemValue(), false /*notify*/);
	}

	return TRUE;
}

void CGridPageTree::SaveTreeState ()
{
	if (m_pGridGroup)
		s_bGridExpanded = m_pGridGroup->IsExpanded();

	if (m_pColumnFormatGroup)
		s_bColumnFormatExpanded = m_pColumnFormatGroup->IsExpanded();

	if (m_pExportGroup)
		s_bExportExpanded = m_pExportGroup->IsExpanded();

	if (m_pExpFormatGroup)
		s_bExportFormatExpanded = m_pExpFormatGroup->IsExpanded();

	if (m_pExportCsvXmlGroup)
		s_bExportCsvXmlExpanded = m_pExportCsvXmlGroup->IsExpanded();
}

namespace OG2
{
    // ---------------------------------------------------------------------------
    // CExportSheet: a CPropertySheet plus one tick in the strip comctl32 leaves
    // bare to the left of OK/Cancel.
    //
    // It lives on the SHEET and not on CGridPageTree because every row that page
    // builds is a setting, and this is a choice about *saving* them: under eleven
    // format radios it would be as easy to miss as the Ctrl+Shift that seeds it,
    // which is the problem it exists to solve.
    //
    // Nothing is resized and nothing the sheet already positioned is moved, so
    // there is no re-centring to re-run. Every coordinate comes from IDOK and the
    // tab control, which is what keeps the tick right at any DPI and with any
    // sheet font - there is no pixel constant in here to scale.
    // ---------------------------------------------------------------------------
    class CExportSheet : public CPropertySheet
    {
    public:
        // pszBoxText empty -> no tick and no layout work at all. That is the global
        // "Grid settings" sheet, where "don't save" would contradict the page it hosts.
        CExportSheet (LPCTSTR pszCaption, LPCTSTR pszBoxText, BOOL bChecked)
            : CPropertySheet(pszCaption)
            // nullptr reads better than _T("") at the call site for "no tick", and CString
            // is not documented to accept one, so it is normalised here.
            , m_strBox(pszBoxText ? pszBoxText : _T(""))
            , m_bChecked(bChecked) {}

        // Readable after DoModal() has returned, which is the only time anybody asks.
        // The tick is mirrored into m_bChecked in OnDestroy() first: GetCheck() on a
        // CButton whose HWND has already gone answers BST_UNCHECKED, not the last state.
        BOOL IsOneShot () const { return m_bChecked; }

        virtual BOOL OnInitDialog ();

    protected:
        // Private to the sheet, so no resource.h entry - the convention
        // CPropTreeSearchBar::IDC_BAR_EDIT set (PropTreeSearchBar.h:229).
        enum { IDC_ONE_SHOT = 4201 };

        DECLARE_MESSAGE_MAP()
        afx_msg void OnDestroy ();

    private:
        CButton m_box;
        CString m_strBox;
        BOOL    m_bChecked;
    };

    BEGIN_MESSAGE_MAP(CExportSheet, CPropertySheet)
        ON_WM_DESTROY()
    END_MESSAGE_MAP()

    BOOL CExportSheet::OnInitDialog ()
    {
        const BOOL bResult = CPropertySheet::OnInitDialog();

        if (m_strBox.IsEmpty())
            return bResult;

        CWnd* const pOk  = GetDlgItem(IDOK);
        CWnd* const pTab = GetTabControl();

        if (NULL == pOk || NULL == pTab)
            return bResult;

        CRect rcOk, rcTab;
        pOk ->GetWindowRect(rcOk);   ScreenToClient(rcOk);
        pTab->GetWindowRect(rcTab);  ScreenToClient(rcTab);

        // Left edge on the page's, right edge short of OK by half a button, top and
        // bottom on OK's so a button control - which centres its own text vertically -
        // puts the caption on the buttons' line.
        const CRect rcBox(rcTab.left, rcOk.top, rcOk.left - rcOk.Height() / 2, rcOk.bottom);

        if (!m_box.Create(m_strBox, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                          rcBox, this, IDC_ONE_SHOT))
            return bResult;

        m_box.SetFont(pOk->GetFont());   // a child created at runtime inherits no font
        m_box.SetCheck(m_bChecked);

        // Z-order is tab order, so this puts the tick after the page and before OK:
        // one Tab from the last tree row reaches it, the next reaches OK.
        m_box.SetWindowPos(pOk, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

#ifdef _DEBUG
        // Guard on the one thing this placement depends on: the caption has to fit the
        // strip. ~350px of text against ~500px of background at 96dpi with the 450-DLU
        // page, and both sides scale together, so it should never fire. GetTextExtent
        // counts the mnemonic '&' that is never drawn, so it errs wide.
        CClientDC dc(&m_box);
        CFont* const pOldFont = dc.SelectObject(m_box.GetFont());
        const CSize szText = dc.GetTextExtent(m_strBox);
        dc.SelectObject(pOldFont);
        ASSERT(szText.cx + rcOk.Height() <= rcBox.Width());
#endif

        return bResult;
    }

    void CExportSheet::OnDestroy ()
    {
        // Cache, then tear down - the order CPropertySheetMem::OnDestroy() reads its
        // search bar's ticks back in (PropertySheetMem.cpp:160-190), for the same reason.
        if (::IsWindow(m_box.GetSafeHwnd()))
        {
            m_bChecked = (BST_CHECKED == m_box.GetCheck());
            m_box.DestroyWindow();
        }

        CPropertySheet::OnDestroy();
    }

    // Kept short on purpose: the strip is only as wide as the background comctl32
    // leaves to the left of OK, and the long explanation is the Shift row's info text.
    static const LPCTSTR s_oneShotText = _T("&Use these settings once - don't save as the default");

    // A null slot is the global sheet; a real one limits the page to what that destination writes.
    // pszOneShotText non-null adds the "use once" tick (per-command sheets only); oneShot
    // arrives as the tick's default and leaves as the user's answer, untouched on Cancel.
    static bool runExportSheet (SQLToolsSettings& settings, const CString& title, bool showShiftRow,
                                bool& showOnShiftOnly, const EExportSlot* slot,
                                LPCTSTR pszOneShotText, bool& oneShot)
    {
        CExportSheet sheet(title, pszOneShotText, oneShot ? TRUE : FALSE);
        sheet.m_psh.dwFlags |= PSH_NOAPPLYNOW;
        sheet.m_psh.dwFlags &= ~PSH_HASHELP;

        // Was CPropGridOutputPage1 ("Format") + CPropGridOutputPage2
        // ("CSV/XML preferences"): one CPropTree page with the two export groups.
        CGridPageTree gridPage(settings);
        gridPage.m_psp.pszTitle = L"Export";  // PSP_USETITLE is set by CPropTreePage

        gridPage.m_bShowGridGroups         = FALSE;   // no grid settings in this sheet
        gridPage.m_bShowShowOnShiftOnly    = showShiftRow ? TRUE : FALSE;
        gridPage.m_bShowOnShiftOnly        = showOnShiftOnly ? TRUE : FALSE;

        // Set here for whichever slot goes on to use it, so the global sheet keeps it editable too.
        gridPage.m_bEnableFilenameTemplate = (nullptr == slot || eslotFile == *slot) ? TRUE : FALSE;

        if (nullptr != slot)
        {
            gridPage.m_bSingleSlot = TRUE;
            gridPage.m_slot        = *slot;
        }

        sheet.AddPage(&gridPage);

        if (sheet.DoModal() != IDOK)
            return false;

        showOnShiftOnly = (FALSE != gridPage.m_bShowOnShiftOnly);
        oneShot         = (FALSE != sheet.IsOneShot());
        return true;
    }

    // Runs the sheet for one command and, unless the user ticked "use once", remembers what they
    // chose. Owning the store here rather than in the fourteen handlers is what makes "once" a
    // single rule no caller can forget to honour.
    bool runExportSheetForSlot (SQLToolsSettings& settings, LPCTSTR operation, EExportSlot slot)
    {
        const bool wasShowOnShiftOnly = settings.GetGridExpShowOnShiftOnly();
        bool showOnShiftOnly = wasShowOnShiftOnly;

        // D6: Shift raises the sheet, a plain click runs with the slot as stored - which is what the
        // "&Copy\tShift for opt" menu hints have always claimed but never delivered.
        if (showOnShiftOnly && !HIBYTE(GetKeyState(VK_SHIFT)))
            return true;

        // Ctrl+Shift seeds the sheet's "use once" tick. BOTH modifiers, never Ctrl on its own:
        // Ctrl+C is Edit.Copy in the grid (Settings/Data/default.keymap:29) and must keep saving.
        // Ctrl+Shift+C is Edit.CopyAppend, which no grid handles, so in practice this default is
        // reached from a menu or toolbar click made with the two keys held down.
        bool oneShot = HIBYTE(GetKeyState(VK_CONTROL)) && HIBYTE(GetKeyState(VK_SHIFT));

        // The settings are remembered per destination, so the "current operation ONLY!" shout this
        // title used to carry is no longer true and is dropped (plan 3.3).
        const CString title = CString(operation) + _T(" settings");

        if (!runExportSheet(settings, title, true, showOnShiftOnly, &slot, s_oneShotText, oneShot))
            return false;

        // Defensive: an unclamped value reaching the format switch asserts in debug, and in release
        // skips SetOutputFormat so the source keeps whatever format its previous export left.
        settings.SetGridExpFormat(clampFormatForSlot(slot, settings.GetGridExpFormat()), false);

        // The sheet edited a copy of the settings and the command runs on that copy, so "once"
        // needs no undo - not writing is the whole of it. That covers the Shift row too: one
        // rule, no exception to explain.
        if (!oneShot)
        {
            storeExportSlot(slot, settings);

            if (wasShowOnShiftOnly != showOnShiftOnly)
                GetSQLToolsSettingsForUpdate().SetGridExpShowOnShiftOnly(showOnShiftOnly);
        }

        return true;
    }

    bool runGlobalExportSheet (SQLToolsSettings& settings)
    {
        // The Shift-only row is offered here too, with the wording that names the commands it governs;
        // ApplyValues writes the property and the caller's assignment persists it.
        bool showOnShiftOnly = settings.GetGridExpShowOnShiftOnly();

        // No "use once" tick here: this sheet's only job is to save.
        bool oneShot = false;

        return runExportSheet(settings, CString("Settings"), true, showOnShiftOnly, nullptr,
                              nullptr, oneShot);
    }
}//namespace OG2
