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

#pragma once
#ifndef __XlsxCellMap_h__
#define __XlsxCellMap_h__

#include <Common/SqlValue.h>
#include <string>

namespace OG2 /* OG = OpenGrid */
{

// The library-free half of the XLSX export: cell mapping and sheet naming. Free of MFC, of oci.h
// and of xlsxwriter.h, so it can be compiled and exercised standalone.

// Excel's per-cell string limit, in characters and not in bytes: libxlsxwriter measures it with
// lxw_utf8_strlen and refuses a longer string outright, so the cut has to happen here.
const size_t kXlsxMaxCellChars = 32767;

// Excel's worksheet-name limit, LXW_SHEETNAME_MAX in the vendored library, in characters for the
// same reason the cell limit is.
const size_t kXlsxMaxSheetNameChars = 31;

enum EXlsxCellType
{
    xlsxBlank = 0,      // worksheet_write_blank: a NULL, which Excel keeps distinct from ""
    xlsxNumber,         // worksheet_write_number
    xlsxString,         // worksheet_write_string, UTF-8
    xlsxDateTime        // worksheet_write_datetime, plus the number format that shows it as a date
};

// The shape lxw_datetime wants: a year inside Excel's serial range, and seconds carrying at most
// milliseconds because that is all a double-based serial can hold.
struct XlsxDateTime
{
    int    year, month, day, hour, minute;
    double sec;                 // 0 - 59.999

    XlsxDateTime () : year(1900), month(1), day(1), hour(0), minute(0), sec(0) {}
};

struct XlsxCell
{
    EXlsxCellType Type;
    double        Number;       // xlsxNumber
    std::string   Text;         // xlsxString: UTF-8, at most kXlsxMaxCellChars characters
    XlsxDateTime  When;         // xlsxDateTime
    bool          Millis;       // the datetime needs the fractional-seconds format
    bool          Cut;          // Text was cut at kXlsxMaxCellChars, and the caller says so

    XlsxCell () : Type(xlsxBlank), Number(0), Millis(false), Cut(false) {}
};

// Number formats, as Excel spells them. Fixed rather than derived from the NLS settings: turning
// an Oracle format model into an Excel one is a job of its own, and getting it wrong is silent.
extern const char* const kXlsxDateTimeFormat;        // whole seconds
extern const char* const kXlsxDateTimeMillisFormat;  // milliseconds

// Maps one typed payload onto the cell a worksheet can hold. `exportText` is what the exporter
// already spells this value as, and is what every unrepresentable kind becomes.
XlsxCell MapSqlValueToXlsx (const Common::SqlValue& v, const std::string& exportText);

// Split out because each carries a rule that is easy to get subtly wrong. Utf8Cut has to count
// characters exactly as lxw_utf8_strlen does.
std::string Utf8Cut           (const std::string& s, size_t maxChars, bool& cut);
int         SignificantDigits (const std::string& canonicalNumber);

// Naming the sheet, in the two halves the export splits it into. Sanitizing is mandatory: the library
// refuses a name it dislikes and returns NULL rather than falling back, and empty means Sheet1.
std::wstring FileBaseName          (const std::wstring& path);
std::string  SanitizeXlsxSheetName (const std::string& utf8Candidate);

}

#endif // __XlsxCellMap_h__
