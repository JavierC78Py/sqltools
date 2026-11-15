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

/***************************************************************************/
/*      Purpose: XLSX export - the binary member of the streamer family     */
/***************************************************************************/

#include "stdafx.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <COMMON/AppGlobal.h>
#include <COMMON/MyUtf.h>

#include "xlsxwriter.h"

#include "GridSourceBase.h"
#include "GridSourceExportXlsx.h"
#include "XlsxCellMap.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace OG2
{

namespace
{
    // Excel's sheet limits, checked up front: libxlsxwriter refuses the cell rather than
    // truncating, and a sheet that silently stops is worse than an error the user can act on.
    const long long kMaxSheetRows = 1048576;
    const int       kMaxSheetCols = 16384;

    // worksheet_set_column takes a width in characters and rejects anything outside Excel's range.
    const double kMinColWidth = 1.0;
    const double kMaxColWidth = 255.0;

    // Slack on the measured count: the grid measures in its fixed-pitch font while the sheet
    // renders in Calibri, where wide glyphs need more room than their character count suggests.
    const double kColWidthPadding = 1.0;

    // Cap on a measured value column, so a VARCHAR2(4000) cannot push the rest of the sheet off
    // screen. Headers are not capped: an unreadable column name is worse than a wide column.
    const double kMaxAutoFitWidth = 60.0;

    // LXW_DEF_COL_WIDTH to the digit, which the library recognises by value and then writes without
    // customWidth: for a column nothing was measured for, the file says "no opinion", not "sized".
    const double kDefaultColWidth = 8.43;

    // Bytes moved at a time out of the finished workbook - the reason it goes to a temporary file
    // rather than to lxw_workbook_options::output_buffer, so a 1 GB export needs 64 KB of RAM.
    const size_t kCopyChunk = 64 * 1024;

    // The header row's fill, Excel's "Blue, Accent 1, Lighter 80%" - what a table style would
    // have used, had constant_memory mode allowed worksheet_add_table() at all.
    const lxw_color_t kHeaderBgColor = 0xD9E1F2;

    // True when every character is ASCII, the only range in which the UTF-8 bytes Common::str()
    // produces are the bytes the ANSI file APIs inside the vendored library expect.
    bool IsAsciiOnly (const std::wstring& s)
    {
        for (size_t i = 0; i < s.size(); i++)
            if (s[i] > 0x7F)
                return false;

        return true;
    }

    // A temporary workbook file, plus its directory in the narrow form the library needs: it opens
    // both with ANSI CRT calls, and tmpfileplus() left to itself falls through to the install dir.
    bool MakeTempWorkbookPath (std::wstring& path, std::string& narrowDir, std::wstring& reason)
    {
        wchar_t longDir[_MAX_PATH + 1] = L"";

        if (0 == ::GetTempPathW(_MAX_PATH, longDir))
        {
            reason = L"cannot read the temporary directory";
            return false;
        }

        std::wstring dir = longDir;

        if (!IsAsciiOnly(dir))
        {
            wchar_t shortDir[_MAX_PATH + 1] = L"";
            const DWORD len = ::GetShortPathNameW(longDir, shortDir, _MAX_PATH);

            // A return at or past the buffer size means the short name did not fit in it.
            if (0 == len || len >= _MAX_PATH || !IsAsciiOnly(shortDir))
            {
                reason  = L"the temporary directory ";
                reason += longDir;
                reason += L" cannot be handed to the export library - point TMP at a path the"
                          L" system ANSI code page can spell";
                return false;
            }

            dir = shortDir;
        }

        wchar_t name[_MAX_PATH + 1] = L"";

        // GetTempFileNameW creates the file, and that is what makes the name unique; the zip writer
        // truncates it again when it opens it. "sqx" makes a crashed export's leftover findable.
        if (0 == ::GetTempFileNameW(dir.c_str(), L"sqx", 0, name))
        {
            reason  = L"cannot create a temporary file in ";
            reason += dir;
            return false;
        }

        // tmpfileplus() appends a separator of its own; GetTempFileNameW wants the opposite.
        std::wstring bare = dir;
        while (bare.size() > 1 && L'\\' == bare[bare.size() - 1])
            bare.erase(bare.size() - 1);

        path      = name;
        narrowDir = Common::str(bare);      // ASCII, so UTF-8 and ANSI are the same bytes here
        return true;
    }

    bool CopyFileToStream (const std::wstring& path, std::ostream& of)
    {
        std::ifstream in(path.c_str(), std::ios::binary);
        if (!in)
            return false;

        std::vector<char> buffer(kCopyChunk);

        while (in)
        {
            in.read(&buffer[0], (std::streamsize)buffer.size());

            const std::streamsize got = in.gcount();
            if (got > 0)
                of.write(&buffer[0], got);
        }

        of.flush();
        return !of.fail();
    }

    double ClampColumnWidth (double w)
    {
        // !(w >= min) rather than w < min so that a NaN width, should one ever arrive, lands on
        // the minimum instead of sailing through both tests.
        if (!(w >= kMinColWidth)) return kMinColWidth;
        if (w > kMaxColWidth)     return kMaxColWidth;
        return w;
    }

    // One column's width: the measured content, capped, then the header as a floor - a header the
    // values do not fill out still has to be readable. A zero length means nothing was measured.
    double ColumnWidthFrom (size_t contentLen, size_t headerLen)
    {
        double w = kDefaultColWidth;

        if (contentLen > 0)
        {
            w = (double)contentLen + kColWidthPadding;
            if (w > kMaxAutoFitWidth)
                w = kMaxAutoFitWidth;
        }

        if (headerLen > 0)
            w = std::max(w, (double)headerLen + kColWidthPadding);

        return ClampColumnWidth(w);
    }

    // Widths measured from the cells being exported, as PlainTextStreamer measures them, because
    // ColumnInfo::m_CharWidth is never assigned on the query path and stays at its default 10.
    void MeasureColumnWidths (const IGridTextSource& source,
                              int firstRow, int nRows, int firstCol, int nCols,
                              bool withHeader, int labelCols,
                              std::vector<double>& widths)
    {
        const bool table = source.IsTableOrientation();

        Common::nvector<size_t> measured("ExportXlsx::measured");

        // The exported rows. An empty selection makes this (0, -1), which both storages read as "no
        // rows" and answer with a zeroed vector, so every width below falls back to the default.
        source.GetMaxColumnWidthByFirstLine(measured, firstRow, firstRow + nRows - 1);

        widths.assign((size_t)nCols + (size_t)labelCols, kDefaultColWidth);

        if (labelCols)
        {
            size_t widest = 0;

            for (int row = firstRow; row < firstRow + nRows; row++)
                widest = std::max(widest, source.GetColHeader(row).size());

            widths[0] = ColumnWidthFrom(widest, 0);
        }

        for (int i = 0; i < nCols; i++)
        {
            // `measured` is indexed by whatever the sheet gives a column of its own: a storage
            // column in table orientation, a record in form orientation, where a header is a row.
            const int    src = firstCol + i;
            const size_t len = (src < (int)measured.size()) ? measured[src] : 0;

            // A column of NULLs measures the grid's marker, which the sheet writes empty.
            const size_t headerLen = (table && withHeader) ? source.GetColHeader(src).size() : 0;

            widths[labelCols + i] = ColumnWidthFrom(len, headerLen);
        }
    }
}

bool ExportXlsx (const IGridTextSource& source, std::ostream& of,
                 int startRow, int rowCount, int startCol, int colCount)
{
    // The -1 "all" sentinels, resolved as Streamer's constructor resolves them. This writer needs
    // the counts up front - a sheet is sized, filtered and pane-frozen before its first row.
    const bool table = source.IsTableOrientation();

    int firstCol = 0, nCols = source.GetCount(edHorz);
    int firstRow = 0, nRows = source.GetCount(edVert);

    if (startCol != -1) { firstCol = startCol; nCols = colCount; }
    if (startRow != -1) { firstRow = startRow; nRows = rowCount; }

    if (nCols < 0) nCols = 0;
    if (nRows < 0) nRows = 0;

    // The header the text streamers write, and the same single-value exception they make, so
    // copying one cell does not grow a caption.
    const bool withHeader = source.GetOutputWithHeader() && !(rowCount == 1 && colCount == 1);

    // In table orientation the caption is a row of its own; transposed, the field names run down a
    // leading column as PlainTextStreamer::writeRow writes them and the record columns stay unnamed.
    const int headerRows = (table && withHeader) ? 1 : 0;
    const int labelCols  = (!table && withHeader) ? 1 : 0;

    const long long totalRows = (long long)nRows + headerRows;
    const long long totalCols = (long long)nCols + labelCols;

    if (totalRows > kMaxSheetRows || totalCols > kMaxSheetCols)
    {
        std::ostringstream msg;
        msg << "XLSX export refused: " << totalRows << " rows by " << totalCols
            << " columns does not fit an Excel worksheet (" << kMaxSheetRows
            << " by " << kMaxSheetCols << ")";
        Global::SetStatusText(msg.str().c_str());
        of.setstate(std::ios::failbit);
        return false;
    }

    std::wstring tempPath;
    std::string  tempDir;
    std::wstring reason;

    if (!MakeTempWorkbookPath(tempPath, tempDir, reason))
    {
        // Wide, because the directory being reported is what a narrow line could not have spelled.
        Global::SetStatusText(std::wstring(L"XLSX export failed: ") + reason);
        of.setstate(std::ios::failbit);
        return false;
    }

    bool ok          = false;
    int  cutCells    = 0;
    int  failedCells = 0;

    lxw_workbook* workbook = 0;

    // One attempt, written as a loop so the failure paths can leave the cleanup in one place.
    do
    {
        lxw_workbook_options options;
        memset(&options, 0, sizeof(options));

        // Constant memory: each row is flushed as the next is written, so a million-row export
        // costs the RAM of one row - and everything sheet-shaped must be decided before row one.
        options.constant_memory = LXW_TRUE;

        // A single sheet's XML passes 4 GB uncompressed well before a million wide rows do, and
        // without the extension that zip entry is not readable.
        options.use_zip64 = LXW_TRUE;

        // The directory the workbook file is in: writable, and narrow-spellable. The library
        // duplicates the string; NULL would send its spool files to the current directory.
        options.tmpdir = tempDir.c_str();

        workbook = workbook_new_opt(Common::str(tempPath.c_str()).c_str(), &options);

        if (!workbook)
        {
            Global::SetStatusText("XLSX export failed: cannot create the workbook");
            break;
        }

        // The name the view set from the file the user chose, sanitized because the library refuses a
        // name it dislikes instead of falling back. Empty - quick-open has none - gives Sheet1.
        const std::string sheetName = SanitizeXlsxSheetName(Common::str(source.GetXlsxSheetName()));
        lxw_worksheet* sheet = workbook_add_worksheet(workbook,
                                                      sheetName.empty() ? NULL : sheetName.c_str());

        lxw_format* headerFormat = sheet ? workbook_add_format(workbook) : 0;
        lxw_format* dateTimeFormat = headerFormat ? workbook_add_format(workbook) : 0;
        lxw_format* dateTimeMillisFormat = headerFormat ? workbook_add_format(workbook) : 0;

        if (!sheet || !headerFormat || !dateTimeFormat || !dateTimeMillisFormat)
        {
            Global::SetStatusText("XLSX export failed: out of memory building the workbook");
            break;
        }

        format_set_bold(headerFormat);
        format_set_bg_color(headerFormat, kHeaderBgColor);
        format_set_border(headerFormat, LXW_BORDER_THIN);

        format_set_num_format(dateTimeFormat,       kXlsxDateTimeFormat);
        format_set_num_format(dateTimeMillisFormat, kXlsxDateTimeMillisFormat);

        // Widths, panes and the filter go in before the first row: constant_memory has flushed the
        // sheet head by the time a cell is written, so anything set later has nowhere to land.
        std::vector<double> widths;
        MeasureColumnWidths(source, firstRow, nRows, firstCol, nCols, withHeader, labelCols, widths);

        // One call per column rather than one ranged call for the lot, because the widths differ.
        for (size_t i = 0; i < widths.size(); i++)
            worksheet_set_column(sheet, (lxw_col_t)i, (lxw_col_t)i, widths[i], NULL);

        if (headerRows)
        {
            // Both are worth having and both survive constant_memory mode, which the library's
            // documentation only promises for worksheet_add_table() to not do.
            worksheet_freeze_panes(sheet, 1, 0);
            worksheet_autofilter(sheet, 0, 0, (lxw_row_t)(totalRows - 1), (lxw_col_t)(totalCols - 1));
        }

        std::string       text;
        Common::SqlValue  value;

        if (headerRows)
        {
            for (int col = firstCol; col < firstCol + nCols; col++)
            {
                text = source.GetColHeader(col);
                if (LXW_NO_ERROR != worksheet_write_string(sheet, 0, (lxw_col_t)(col - firstCol),
                                                           text.c_str(), headerFormat))
                    failedCells++;
            }
        }

        // Rows strictly in order, which is what constant_memory demands: each row is flushed when
        // the next one starts, so a cell can never be revisited.
        for (int row = firstRow; row < firstRow + nRows; row++)
        {
            const lxw_row_t sheetRow = (lxw_row_t)(headerRows + (row - firstRow));

            if (labelCols)
            {
                text = source.GetColHeader(row);
                if (LXW_NO_ERROR != worksheet_write_string(sheet, sheetRow, 0, text.c_str(),
                                                           headerFormat))
                    failedCells++;
            }

            for (int col = firstCol; col < firstCol + nCols; col++)
            {
                const lxw_col_t sheetCol = (lxw_col_t)(labelCols + (col - firstCol));

                text.clear();
                source.GetCellStrForExport(text, row, col, 0);

                if (!source.GetCellSqlValue(value, row, col))
                {
                    // No payloads on this source - GridStringSource among them. Text is all there
                    // is, which is where every other exporter lands too.
                    if (LXW_NO_ERROR != worksheet_write_string(sheet, sheetRow, sheetCol,
                                                               text.c_str(), NULL))
                        failedCells++;
                    continue;
                }

                const XlsxCell cell = MapSqlValueToXlsx(value, text);
                lxw_error err = LXW_NO_ERROR;

                switch (cell.Type)
                {
                case xlsxNumber:
                    err = worksheet_write_number(sheet, sheetRow, sheetCol, cell.Number, NULL);
                    break;

                case xlsxDateTime:
                {
                    lxw_datetime when;
                    when.year   = cell.When.year;
                    when.month  = cell.When.month;
                    when.day    = cell.When.day;
                    when.hour   = cell.When.hour;
                    when.min    = cell.When.minute;
                    when.sec    = cell.When.sec;

                    // The format rides with the cell, not the column: a column can mix kinds, and
                    // a datetime written without one shows as a serial number.
                    err = worksheet_write_datetime(sheet, sheetRow, sheetCol, &when,
                                                   cell.Millis ? dateTimeMillisFormat
                                                               : dateTimeFormat);
                    break;
                }

                case xlsxString:
                    if (cell.Cut)
                        cutCells++;
                    err = worksheet_write_string(sheet, sheetRow, sheetCol, cell.Text.c_str(), NULL);
                    break;

                case xlsxBlank:
                default:
                    // A NULL. With no format the library writes no cell at all, which is what an
                    // empty cell is; the column format would only matter if one were set.
                    err = worksheet_write_blank(sheet, sheetRow, sheetCol, NULL);
                    break;
                }

                if (LXW_NO_ERROR != err)
                    failedCells++;
            }
        }

        const lxw_error closed = workbook_close(workbook);
        workbook = 0;                       // closed frees it, whichever way it returned

        if (LXW_NO_ERROR != closed)
        {
            std::ostringstream msg;
            msg << "XLSX export failed: " << lxw_strerror(closed);
            Global::SetStatusText(msg.str().c_str());
            break;
        }

        ok = CopyFileToStream(tempPath, of);

        if (!ok)
            Global::SetStatusText("XLSX export failed: cannot write the file that was chosen");
    }
    while (false);

    if (workbook)
        workbook_close(workbook);           // a break above left it open

    ::DeleteFileW(tempPath.c_str());

    if (!ok)
    {
        of.setstate(std::ios::failbit);     // the caller's guard reads this and keeps the file closed
        return false;
    }

    // Ephemeral: a status-bar line and no modal box, because a data path does not stop the user
    // with a dialog. One message for both counts.
    if (cutCells || failedCells)
    {
        std::ostringstream msg;

        if (cutCells)
            msg << cutCells << " value(s) exceeded Excel's " << (int)kXlsxMaxCellChars
                << " character cell limit and were cut";

        if (failedCells)
            msg << (cutCells ? "; " : "") << failedCells << " cell(s) could not be written";

        Global::SetStatusText(msg.str().c_str());
    }

    return true;
}

}
