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
/*  Purpose: what one typed grid value becomes in one XLSX worksheet cell   */
/***************************************************************************/

// No precompiled header here, deliberately: this file is compiled both with and without one, so
// it has to stay self-contained.

#include "XlsxCellMap.h"

#include <cmath>        // std::isfinite, std::isnan, std::isinf

namespace OG2
{

const char* const kXlsxDateTimeFormat       = "yyyy-mm-dd hh:mm:ss";
const char* const kXlsxDateTimeMillisFormat = "yyyy-mm-dd hh:mm:ss.000";

namespace
{
    // A double carries 15 decimal digits without a promise being broken. Above it the value goes
    // out as its canonical text: showing 123456789012345680000 is worse than showing the digits.
    const int kMaxExactDigits = 15;

    // lxw_datetime documents 1900 - 9999 and Excel's serial starts at 1900-01-01, while an Oracle
    // DATE reaches back to -4712: outside the range the value becomes text and keeps its digits.
    const int kMinExcelYear = 1900;
    const int kMaxExcelYear = 9999;

    // Nanoseconds per millisecond: the granularity of SqlTimestamp::fsec against the granularity
    // of an Excel serial, which is a double and so cannot hold the ninth digit Oracle stores.
    const int kNanosPerMilli = 1000000;

    bool YearFitsExcel (int year)
    {
        return year >= kMinExcelYear && year <= kMaxExcelYear;
    }

    XlsxCell TextCell (const std::string& text)
    {
        XlsxCell cell;
        cell.Type = xlsxString;
        cell.Text = Utf8Cut(text, kXlsxMaxCellChars, cell.Cut);
        return cell;
    }
}

std::string Utf8Cut (const std::string& s, size_t maxChars, bool& cut)
{
    cut = false;

    // Walk whole characters: cutting a multi-byte one makes the string invalid UTF-8, and a
    // worksheet is XML, which a parser rejects - Excel would refuse the whole file over two bytes.

    // No "it already fits" shortcut: Utf8CharCount counts the incomplete tail of a value cut at a
    // fetch limit as one character, and only the walk notices that it must not be written.
    size_t chars = 0;
    size_t pos   = 0;

    while (pos < s.size() && chars < maxChars)
    {
        const unsigned char b = static_cast<unsigned char>(s[pos]);
        size_t width = 1;
        if      (b >= 0xF0) width = 4;
        else if (b >= 0xE0) width = 3;
        else if (b >= 0xC0) width = 2;

        if (pos + width > s.size())
            break;          // a truncated tail: stop in front of it rather than emit half of it

        pos   += width;
        chars += 1;
    }

    cut = (pos < s.size());
    return cut ? s.substr(0, pos) : s;
}

int SignificantDigits (const std::string& canonicalNumber)
{
    // Precision is the span from the first non-zero digit to the last: the zeros in front of
    // 0.000123 are place, and the zeros behind 12300 are not precision either.
    int first = -1;
    int last  = -1;
    int index = 0;

    for (size_t i = 0; i < canonicalNumber.size(); i++)
    {
        const char c = canonicalNumber[i];

        if (c < '0' || c > '9')
            continue;           // a sign or the point; ToCanonical writes no exponent

        if ('0' != c)
        {
            if (first < 0)
                first = index;
            last = index;
        }
        index++;
    }

    return (first < 0) ? 0 : (last - first + 1);
}

XlsxCell MapSqlValueToXlsx (const Common::SqlValue& v, const std::string& exportText)
{
    XlsxCell cell;

    // A NULL becomes a blank cell, never an empty string: the payload knows the difference
    // and so does Excel, and losing it here would undo the reason the typed path exists.
    if (v.IsNull())
        return cell;                                    // xlsxBlank

    switch (v.Kind())
    {
    case Common::SqlValueKind::Number:
        if (const Common::SqlNumber* n = v.Number())
        {
            const std::string canonical = n->ToCanonical();
            double d = 0;

            if (SignificantDigits(canonical) <= kMaxExactDigits && n->ToDouble(d) && std::isfinite(d))
            {
                cell.Type   = xlsxNumber;
                cell.Number = d;
                return cell;
            }
            return TextCell(canonical);                 // full precision, as text
        }
        break;

    case Common::SqlValueKind::BinaryFloat:
    case Common::SqlValueKind::BinaryDouble:
    {
        const bool isFloat = (Common::SqlValueKind::BinaryFloat == v.Kind());
        float  f = 0;
        double d = 0;

        if (isFloat ? v.BinaryFloat(f) : v.BinaryDouble(d))
        {
            const double val = isFloat ? (double)f : d;

            // Excel has no NaN and no infinity and the format has no spelling for either, so they
            // go out as the same three words the JSON export uses and read the same in both.
            if (std::isnan(val))
                return TextCell("NaN");
            if (std::isinf(val))
                return TextCell(val < 0 ? "-Infinity" : "Infinity");

            cell.Type   = xlsxNumber;
            cell.Number = val;
            return cell;
        }
        break;
    }
    case Common::SqlValueKind::Date:
        if (const Common::SqlDate* dt = v.Date())
        {
            if (YearFitsExcel(dt->year))
            {
                cell.Type        = xlsxDateTime;
                cell.When.year   = dt->year;
                cell.When.month  = dt->month;
                cell.When.day    = dt->day;
                cell.When.hour   = dt->hour;
                cell.When.minute = dt->minute;
                cell.When.sec    = dt->second;
                cell.Millis      = false;               // a DATE carries no fraction
                return cell;
            }
        }
        break;

    case Common::SqlValueKind::Timestamp:
        if (const Common::SqlTimestamp* ts = v.Timestamp())
        {
            // Whole milliseconds only: an Excel serial is a double, so a microsecond would be
            // rounded away with nothing in the file to say so. A negative fsec is refused likewise.
            if (YearFitsExcel(ts->year) && ts->fsec >= 0 && 0 == (ts->fsec % kNanosPerMilli))
            {
                cell.Type        = xlsxDateTime;
                cell.When.year   = ts->year;
                cell.When.month  = ts->month;
                cell.When.day    = ts->day;
                cell.When.hour   = ts->hour;
                cell.When.minute = ts->minute;
                cell.When.sec    = ts->second + (double)(ts->fsec / kNanosPerMilli) / 1000.0;
                cell.Millis      = (0 != ts->fsec);
                return cell;
            }
        }
        break;

    case Common::SqlValueKind::TimestampTz:
    case Common::SqlValueKind::TimestampLtz:
        // An Excel serial carries no zone, so writing the wall clock would drop the offset and
        // quietly change the instant. Text keeps it, spelled as the grid showed it.
        break;

    case Common::SqlValueKind::IntervalYm:
    case Common::SqlValueKind::IntervalDs:
        // A worksheet has no interval type. Excel's "duration" number formats are a way of
        // showing a serial, not a value, and an INTERVAL YEAR TO MONTH has no serial at all.
        break;

    case Common::SqlValueKind::Raw:
        // Hex, as every other text exporter writes it, rather than the base64 JSON chose:
        // a sheet is read by a person, and hex is what the grid showed.
    case Common::SqlValueKind::Text:
    case Common::SqlValueKind::Null:
    case Common::SqlValueKind::Unsupported:
    default:
        break;
    }

    // Text and Unsupported carry their bytes directly, and so does any kind whose accessor
    // declined - the same last resort JsonStreamer::writeValue ends on.
    return TextCell(exportText);
}

std::wstring FileBaseName (const std::wstring& path)
{
    // Separators first, so a dot in a directory name is not taken for the one starting the extension.
    const size_t slash = path.find_last_of(L"\\/");
    std::wstring name  = path.substr(slash == std::wstring::npos ? 0 : slash + 1);

    const size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos)
        name.erase(dot);

    return name;
}

namespace
{
    // A switch and not strchr(): a UTF-8 continuation byte is negative in a plain char.
    bool IllegalInSheetName (char c)
    {
        switch (c)
        {
        case '[': case ']': case ':': case '*': case '?': case '/': case '\\':
            return true;
        default:
            return false;
        }
    }
}

std::string SanitizeXlsxSheetName (const std::string& utf8Candidate)
{
    std::string name = utf8Candidate;

    // Replaced rather than dropped, so "q1/q2" reads as "q1_q2" and not as "q1q2".
    for (size_t i = 0; i < name.size(); i++)
        if (IllegalInSheetName(name[i]))
            name[i] = '_';

    bool cut = false;
    name = Utf8Cut(name, kXlsxMaxSheetNameChars, cut);

    // After the cut, which can expose an apostrophe at the new end; stripping only shortens.
    size_t first = 0;
    size_t last  = name.size();

    while (first < last && '\'' == name[first])     first++;
    while (last > first && '\'' == name[last - 1])  last--;

    return name.substr(first, last - first);
}

}

