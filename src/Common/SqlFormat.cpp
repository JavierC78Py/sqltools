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

#include "SqlFormat.h"
#include <cstdio>
#include <cstring>
#include <cmath>

namespace Common
{
namespace {

// ---- case-insensitive token matching, so "dd", "DD" and "Dd" all resolve the same element ----
static char Up (char c) { return ('a' <= c && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }

static bool MatchAt (const std::string& f, size_t i, const char* tok)
{
    size_t n = strlen(tok);
    if (f.size() - i < n)
        return false;
    for (size_t k = 0; k < n; ++k)
        if (Up(f[i + k]) != tok[k])
            return false;
    return true;
}

static char HexDigit (unsigned v) { return static_cast<char>(v < 10 ? '0' + v : 'A' + v - 10); }

// ---- Gregorian calendar helpers ----
static bool IsLeap (int y) { return (0 == y % 4 && 0 != y % 100) || 0 == y % 400; }

static int DaysInMonth (int y, int m)
{
    static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (2 == m && IsLeap(y))
        return 29;
    return (1 <= m && m <= 12) ? d[m - 1] : 0;
}

// Day of year, 1..366*: the number of whole days that precede the given date's first second.
static int DayOfYear (int y, int m, int d)
{
    int n = d;
    for (int i = 1; i < m; ++i)
        n += DaysInMonth(y, i);
    return n;
}

// Zeller's congruence, mapped to 0 == Sunday .. 6 == Saturday.
static int DayOfWeek (int y, int m, int d)
{
    if (m < 3) { m += 12; --y; }
    const int K = y % 100, J = y / 100;
    const int h = (d + 13 * (m + 1) / 5 + K + K / 4 + J / 4 + 5 * J) % 7; // 0 == Saturday
    return (h + 6) % 7;   // now 0 == Sunday
}

static const char* const MONTH_NAME[12] =
{ "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE",
  "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER" };
static const char* const DAY_NAME[7] =
{ "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY" };

static void AppendFsec (std::string& out, int fsec, int digits)
{
    if (digits < 1) digits = 9;
    if (digits > 9) digits = 9;
    char buf[16];
    snprintf(buf, sizeof(buf), "%09d", fsec);
    out.append(buf, static_cast<size_t>(digits));
}

static void AppInt (std::string& out, int v, int width)
{
    char buf[40];
    if (0 < width) snprintf(buf, sizeof(buf), "%0*d", width, v);
    else           snprintf(buf, sizeof(buf), "%d", v);
    out += buf;
}

// Sign of the leading field: a literal +/- already in the picture wins, else '-' when negative.
static bool PicHasOwnSign (const std::string& fmt, size_t i)
{
    return 0 < i && ('+' == fmt[i - 1] || '-' == fmt[i - 1]);
}

// The one datetime tokenizer both DATE and TIMESTAMP* go through. haveFsec distinguishes a DATE
// (no fraction, no zone) from a TIMESTAMP*; tz, when non-null, supplies the zone for TZH/TZM/TZR.
static std::string RenderTemporal (const std::string& fmt,
                                   int year, int month, int day, int hour, int minute, int second,
                                   int fsec, bool haveFsec, const SqlTimestamp* tz)
{
    std::string out;
    char buf[40];

    const int ad = (year > 0) ? 1 : 0;   // era for BC/AD

    for (size_t i = 0; i < fmt.size(); )
    {
        const char c = fmt[i];

        if ('"' == c)   // a quoted literal is copied verbatim, quotes stripped
        {
            ++i;
            while (i < fmt.size() && '"' != fmt[i])
                out += fmt[i++];
            if (i < fmt.size()) ++i;
            continue;
        }

        // Longest matches first, so MONTH/MON, DAY/DY, SSSSS/SS never misfire.
        if (MatchAt(fmt, i, "MONTH")) { int m = (1 <= month && month <= 12) ? month - 1 : 0; out += MONTH_NAME[m]; i += 5; continue; }
        if (MatchAt(fmt, i, "SSSSS")) { snprintf(buf, sizeof(buf), "%d", hour * 3600 + minute * 60 + second); out += buf; i += 5; continue; }
        if (MatchAt(fmt, i, "HH24"))  { snprintf(buf, sizeof(buf), "%02d", hour); out += buf; i += 4; continue; }
        if (MatchAt(fmt, i, "HH12"))  { int h = hour % 12; if (0 == h) h = 12; snprintf(buf, sizeof(buf), "%02d", h); out += buf; i += 4; continue; }
        if (MatchAt(fmt, i, "SYYYY") || MatchAt(fmt, i, "IYYY") || MatchAt(fmt, i, "RRRR") || MatchAt(fmt, i, "YYYY"))
        { snprintf(buf, sizeof(buf), "%04d", year < 0 ? -year : year); out += buf; i += 4; continue; }
        if (MatchAt(fmt, i, "YYY"))  { snprintf(buf, sizeof(buf), "%03d", (year < 0 ? -year : year) % 1000); out += buf; i += 3; continue; }
        if (MatchAt(fmt, i, "YY") || MatchAt(fmt, i, "RR"))  { snprintf(buf, sizeof(buf), "%02d", (year < 0 ? -year : year) % 100); out += buf; i += 2; continue; }
        if (MatchAt(fmt, i, "MON"))  { int m = (1 <= month && month <= 12) ? month - 1 : 0; out.append(MONTH_NAME[m], 3); i += 3; continue; }
        if (MatchAt(fmt, i, "MM"))   { snprintf(buf, sizeof(buf), "%02d", month); out += buf; i += 2; continue; }
        if (MatchAt(fmt, i, "MI"))   { snprintf(buf, sizeof(buf), "%02d", minute); out += buf; i += 2; continue; }
        if (MatchAt(fmt, i, "DAY"))  { out += DAY_NAME[DayOfWeek(year, month, day)]; i += 3; continue; }
        if (MatchAt(fmt, i, "DY"))   { out.append(DAY_NAME[DayOfWeek(year, month, day)], 3); i += 2; continue; }
        if (MatchAt(fmt, i, "DDD"))  { snprintf(buf, sizeof(buf), "%03d", DayOfYear(year, month, day)); out += buf; i += 3; continue; }
        if (MatchAt(fmt, i, "DD"))   { snprintf(buf, sizeof(buf), "%02d", day); out += buf; i += 2; continue; }
        if (MatchAt(fmt, i, "FF"))   { int d = 9; if (i + 2 < fmt.size() && '1' <= fmt[i + 2] && fmt[i + 2] <= '9') { d = fmt[i + 2] - '0'; i += 3; } else i += 2; if (haveFsec) AppendFsec(out, fsec, d); continue; }
        if (MatchAt(fmt, i, "A.M.") || MatchAt(fmt, i, "P.M.")) { out += (hour < 12 ? "A.M." : "P.M."); i += 4; continue; }
        if (MatchAt(fmt, i, "B.C.") || MatchAt(fmt, i, "A.D.")) { out += (ad ? "A.D." : "B.C."); i += 4; continue; }
        if (MatchAt(fmt, i, "TZH")) { int h = (0 != tz) ? tz->tzHour : 0; snprintf(buf, sizeof(buf), "%c%02d", (0 > h ? '-' : '+'), (0 > h ? -h : h)); out += buf; i += 3; continue; }
        if (MatchAt(fmt, i, "TZM")) { int m = (0 != tz) ? tz->tzMinute : 0; snprintf(buf, sizeof(buf), "%02d", (0 > m ? -m : m)); out += buf; i += 3; continue; }
        if (MatchAt(fmt, i, "TZR")) {
            if (0 != tz)
            {
                const size_t max = sizeof(tz->tzRegion);
                const size_t len = (max < tz->tzRegionLen) ? max : tz->tzRegionLen;
                if (0 != (tz->tzFlags & 0x02) && 0 < len)   // etfRegion
                    out.append(tz->tzRegion, len);
                else { int h = tz->tzHour, m = tz->tzMinute; snprintf(buf, sizeof(buf), "%c%02d:%02d", (0 > h ? '-' : '+'), (0 > h ? -h : h), (0 > m ? -m : m)); out += buf; }
            }
            i += 3; continue;
        }
        if (MatchAt(fmt, i, "AM") || MatchAt(fmt, i, "PM")) { out += (hour < 12 ? "AM" : "PM"); i += 2; continue; }
        if (MatchAt(fmt, i, "AD") || MatchAt(fmt, i, "BC")) { out += (ad ? "AD" : "BC"); i += 2; continue; }
        if (MatchAt(fmt, i, "SS"))   { snprintf(buf, sizeof(buf), "%02d", second); out += buf; i += 2; continue; }
        if (MatchAt(fmt, i, "HH"))   { int h = hour % 12; if (0 == h) h = 12; snprintf(buf, sizeof(buf), "%02d", h); out += buf; i += 2; continue; }
        if ('X' == Up(c))            { out += '.'; ++i; continue; }   // local radix before FF
        if ('Y' == Up(c) || 'D' == Up(c)) { out += c; ++i; continue; }   // bare Y/D: no numeric expansion, emitted literally

        out += c;   // literal punctuation / spacing / anything else
        ++i;
    }

    return out;
}

// ---- numeric picture parsing and rounding ----
struct NumPic
{
    bool sci;          // EEEE present
    bool hasSign;      // S present
    bool group;        // G / ',' present
    bool seenSep;      // a decimal point or V present
    bool isV;          // V present (multiply, no separator)
    int  intPos;       // digit positions before the separator
    int  fracPos;      // digit positions after it
    std::string prefix;// literal emitted before the number (currency)
};

static bool IsMinimalText (const std::string& fmt)
{
    std::string t;
    for (size_t i = 0; i < fmt.size(); ++i)
        if (' ' != fmt[i] && '\t' != fmt[i])
            t += Up(fmt[i]);
    return "TM" == t || "TM9" == t;
}

static NumPic ParseNumPic (const std::string& fmt)
{
    NumPic p = { false, false, false, false, false, 0, 0, std::string() };
    for (size_t i = 0; i < fmt.size(); ++i)
    {
        const char c = fmt[i];
        if (' ' == c || '\t' == c)
            continue;
        if ('S' == Up(c))                        { p.hasSign = true; continue; }
        if ('$' == c || 'L' == Up(c))            { p.prefix += '$'; continue; }
        if (',' == c || 'G' == Up(c))            { p.group = true; continue; }
        if ('.' == c || 'D' == Up(c))            { p.seenSep = true; p.isV = false; continue; }
        if ('V' == Up(c))                        { p.seenSep = true; p.isV = true; continue; }
        if ('9' == c || '0' == c)                { if (p.seenSep) ++p.fracPos; else ++p.intPos; continue; }
        if ('E' == Up(c))
        {
            p.sci = true;
            while (i + 1 < fmt.size() && 'E' == Up(fmt[i + 1])) ++i;   // absorb a run of E's
            continue;
        }
        // any other character is ignored: Oracle requires quotes for a literal in a picture.
    }
    return p;
}

static std::string AddOne (std::string s)
{
    int i = static_cast<int>(s.size()) - 1;
    while (0 <= i && '9' == s[i]) { s[i] = '0'; --i; }
    if (0 > i) s.insert(s.begin(), '1');
    else s[i] = static_cast<char>(s[i] + 1);
    return s;
}

// Pass-through when ToCanonical already matched: minimal text, only the decimal char is swapped and
// no grouping is added (TM has none).
static std::string MinimalNumber (const SqlNumber& n, char decimal)
{
    std::string s = n.ToCanonical();
    for (size_t i = 0; i < s.size(); ++i)
        if ('.' == s[i])
            s[i] = decimal;
    return s;
}

// Mangles a picture onto the canonical decimal of a SqlNumber (neg sign stripped first). The result
// is a digit string whose implied scale is the integer-digit count - the caller inserts the group
// char, the decimal char and the leading sign/currency around it. Only the magnitude is built here;
// the '-' is folded back by FormatNumber, which is the only place that knows the picture's S flag.
static void PictureDigits (const SqlNumber& n, const NumPic& p, std::string& digits, size_t& intLen)
{
    const std::string canon = n.ToCanonical();
    size_t start = 0;
    if (0 < canon.size() && '-' == canon[0]) start = 1;

    std::string intStr = "0", fracStr = "";
    const size_t dot = canon.find('.', start);
    if (std::string::npos == dot)
    {
        if (start < canon.size()) intStr = canon.substr(start);
    }
    else
    {
        intStr = (dot == start) ? "0" : canon.substr(start, dot - start);
        fracStr = canon.substr(dot + 1);
    }

    // Fold to a single digit string: integer digits then fractional digits.
    const size_t iLen = intStr.size();
    std::string all = intStr + fracStr;
    const size_t keep = iLen + static_cast<size_t>(p.fracPos);
    while (all.size() < keep) all += '0';
    const bool roundUp = (all.size() > keep) && all[keep] >= '5';
    all = all.substr(0, keep);
    if (roundUp) all = AddOne(all);

    intLen = (all.size() > static_cast<size_t>(p.fracPos)) ? all.size() - static_cast<size_t>(p.fracPos) : 0;
    digits = all;
}

static std::string GroupInt (std::string intPart, char group)
{
    // Insert the group char every three integer digits, counting from the decimal point (right).
    std::string out;
    const size_t n = intPart.size();
    for (size_t i = 0; i < n; ++i)
    {
        if (0 < i && 0 == (n - i) % 3)
            out += group;
        out += intPart[i];
    }
    return out;
}

// Scientific (EEEE): re-anchor the digits so exactly one integer digit precedes the decimal point,
// then append an uppercase E and a sign-plus-three-digit exponent.
static std::string ScientificNumber (const SqlNumber& n, const NumPic& p, char decimal)
{
    const std::string canon = n.ToCanonical();
    bool neg = false;
    size_t start = 0;
    if (0 < canon.size() && '-' == canon[0]) { neg = true; start = 1; }

    std::string digits;   // significant digit string of |n|, no dot, no leading zeros
    const size_t dot = canon.find('.', start);
    if (std::string::npos == dot)
        digits = canon.substr(start);
    else
        digits = canon.substr(start, dot - start) + canon.substr(dot + 1);

    int exp = 0;
    if (digits.size() && '0' != digits[0])
    {
        const size_t iLen = (std::string::npos == dot) ? canon.size() - start : dot - start;
        exp = static_cast<int>(iLen) - 1;                          // e.g. 1234.5 -> 3
    }
    else
    {
        // value in (0,1): the first non-zero fractional digit's place sets the exponent.
        size_t lead = 0;
        while (lead < digits.size() && '0' == digits[lead]) ++lead;
        if (lead >= digits.size()) { exp = 0; digits = "0"; }       // zero
        else { exp = -static_cast<int>(lead) - 1; }
    }

    // Trim leading zeros left by the (0,1) branch, keeping at least one digit.
    while (1 < digits.size() && '0' == digits[0]) digits.erase(digits.begin());

    // Round to 1 + fracPos significant digits.
    const size_t keep = 1 + static_cast<size_t>(p.fracPos);
    while (digits.size() < keep) digits += '0';
    bool roundUp = (digits.size() > keep) && digits[keep] >= '5';
    digits = digits.substr(0, keep);
    if (roundUp)
    {
        digits = AddOne(digits);
        if (digits.size() > keep) { digits = digits.substr(0, keep); ++exp; }   // 9.99 -> 1.00E+..
    }

    std::string mant = digits.substr(0, 1);
    if (0 < p.fracPos) { mant += decimal; mant += digits.substr(1); }

    char ebuf[8];
    snprintf(ebuf, sizeof(ebuf), "E%c%03d", (0 <= exp ? '+' : '-'), (0 <= exp ? exp : -exp));
    return (neg ? "-" : "") + mant + ebuf;
}

} // namespace

///////////////////////////////////////////////////////////////////////////////
// SqlFormat
///////////////////////////////////////////////////////////////////////////////

SqlFormat::SqlFormat ()
    : m_numberFormat("TM9")
    , m_dateFormat("YYYY-MM-DD HH24:MI:SS")
    , m_timestampFormat("YYYY-MM-DD HH24:MI:SS.FF")
    , m_timestampTzFormat("YYYY-MM-DD HH24:MI:SS.FF TZR")
    , m_intervalYmFormat()
    , m_intervalDsFormat()
    , m_nullText()
    , m_blobHexRowLength(0)
    , m_decimal('.')
    , m_group(',')
{
}

std::string SqlFormat::Render (const SqlValue& v) const
{
    switch (v.Kind())
    {
    case SqlValueKind::Null:
        return m_nullText;

    case SqlValueKind::Text:
    case SqlValueKind::Unsupported:   // no picture applies; bytes pass through unchanged
    {
        size_t len = 0;
        const char* s = v.Text(len);
        return 0 == s ? std::string() : std::string(s, len);
    }

    case SqlValueKind::Raw:
    {
        size_t len = 0;
        const char* s = v.Text(len);
        return FormatRaw(0 == s ? "" : s, len);
    }

    case SqlValueKind::Number:
    {
        const SqlNumber* n = v.Number();
        return 0 == n ? std::string() : FormatNumber(*n);
    }

    case SqlValueKind::BinaryFloat:
    {
        float f = 0;
        return v.BinaryFloat(f) ? FormatReal(static_cast<double>(f), 9) : std::string();
    }

    case SqlValueKind::BinaryDouble:
    {
        double d = 0;
        return v.BinaryDouble(d) ? FormatReal(d, 17) : std::string();
    }

    case SqlValueKind::Date:
    {
        const SqlDate* d = v.Date();
        return 0 == d ? std::string() : FormatDate(*d);
    }

    case SqlValueKind::Timestamp:
    case SqlValueKind::TimestampTz:
    case SqlValueKind::TimestampLtz:
    {
        const SqlTimestamp* t = v.Timestamp();
        return 0 == t ? std::string() : FormatTimestamp(*t, SqlValueKind::Timestamp != v.Kind());
    }

    case SqlValueKind::IntervalYm:
    {
        const SqlIntervalYm* i = v.IntervalYm();
        return 0 == i ? std::string() : FormatIntervalYm(*i);
    }

    case SqlValueKind::IntervalDs:
    {
        const SqlIntervalDs* i = v.IntervalDs();
        return 0 == i ? std::string() : FormatIntervalDs(*i);
    }

    default:
        return std::string();
    }
}

bool SqlFormat::IsMinimalNumberFormat () const
{
    return IsMinimalText(m_numberFormat);
}

std::string SqlFormat::FormatNumber (const SqlNumber& n) const
{
    if (IsMinimalText(m_numberFormat))
        return MinimalNumber(n, m_decimal);

    const NumPic p = ParseNumPic(m_numberFormat);
    if (p.sci)
        return ScientificNumber(n, p, m_decimal);

    bool neg = false;
    {
        const std::string canon = n.ToCanonical();
        if (0 < canon.size() && '-' == canon[0]) neg = true;
    }

    std::string digits;
    size_t intLen = 0;
    PictureDigits(n, p, digits, intLen);

    std::string intPart  = digits.substr(0, intLen);
    std::string fracPart = digits.substr(intLen);

    if (p.group)
        intPart = GroupInt(intPart, m_group);

    std::string out = p.prefix;
    if (p.hasSign)
        out += (neg ? '-' : '+');
    else if (neg)
        out += '-';

    if (!p.seenSep)
        out += intPart;                         // integer-only picture
    else if (p.isV)
        out += intPart + fracPart;              // V: fraction shifted into the integer field, no separator
    else
    {
        out += intPart;
        if (0 < p.fracPos)
        {
            out += m_decimal;
            out += fracPart;
        }
    }
    return out;
}

std::string SqlFormat::FormatReal (double v, int sigDigits) const
{
    if (std::isnan(v))
        return "NaN";
    if (std::isinf(v))
        return std::string(0 < v ? "" : "-") + "Inf";
    char buf[64];
    snprintf(buf, sizeof(buf), "%.*g", sigDigits, v);
    std::string s = buf;
    for (size_t i = 0; i < s.size(); ++i)
        if ('.' == s[i])
            s[i] = m_decimal;
    return s;
}

std::string SqlFormat::FormatDate (const SqlDate& d) const
{
    return RenderTemporal(m_dateFormat, d.year, d.month, d.day, d.hour, d.minute, d.second, 0, false, 0);
}

std::string SqlFormat::FormatTimestamp (const SqlTimestamp& t, bool withZone) const
{
    return RenderTemporal(withZone ? m_timestampTzFormat : m_timestampFormat,
                          t.year, t.month, t.day, t.hour, t.minute, t.second, t.fsec, true,
                          withZone ? &t : 0);
}

std::string SqlFormat::FormatIntervalYm (const SqlIntervalYm& v) const
{
    const int y = (0 > v.years ? -v.years : v.years);
    const int m = (0 > v.months ? -v.months : v.months);
    const bool neg = (0 > v.years) || (0 > v.months);

    if (m_intervalYmFormat.empty())
    {
        char buf[48];
        snprintf(buf, sizeof(buf), "%s%d-%02d", neg ? "-" : "+", y, m);
        return buf;
    }

    const std::string& f = m_intervalYmFormat;
    std::string out;
    bool signDone = false;
    for (size_t i = 0; i < f.size(); )
    {
        if ('"' == f[i])
        {
            ++i;
            while (i < f.size() && '"' != f[i]) out += f[i++];
            if (i < f.size()) ++i;
            continue;
        }
        int width = 0, value = 0, adv = 0;
        if      (MatchAt(f, i, "YYYY")) { width = 4; value = y; adv = 4; }
        else if (MatchAt(f, i, "YYY"))  { width = 3; value = y; adv = 3; }
        else if (MatchAt(f, i, "YY"))   { width = 2; value = y; adv = 2; }
        else if (MatchAt(f, i, "Y"))    {            value = y; adv = 1; }
        else if (MatchAt(f, i, "MM"))   { width = 2; value = m; adv = 2; }
        else if (MatchAt(f, i, "M"))    {            value = m; adv = 1; }
        else { out += f[i++]; continue; }
        if (!signDone)
        {
            if (!PicHasOwnSign(f, i) && neg) out += '-';
            signDone = true;
        }
        AppInt(out, value, width);
        i += adv;
    }
    return out;
}

std::string SqlFormat::FormatIntervalDs (const SqlIntervalDs& v) const
{
    const int d  = (0 > v.days ? -v.days : v.days);
    const int h  = (0 > v.hours ? -v.hours : v.hours);
    const int mi = (0 > v.minutes ? -v.minutes : v.minutes);
    const int s  = (0 > v.seconds ? -v.seconds : v.seconds);
    const int fs = (0 > v.fsec ? -v.fsec : v.fsec);
    const bool neg = (0 > v.days) || (0 > v.hours) || (0 > v.minutes) || (0 > v.seconds) || (0 > v.fsec);

    if (m_intervalDsFormat.empty())
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s%d %02d:%02d:%02d.%09d", neg ? "-" : "+", d, h, mi, s, fs);
        return buf;
    }

    const std::string& f = m_intervalDsFormat;
    std::string out;
    bool signDone = false;
    for (size_t i = 0; i < f.size(); )
    {
        if ('"' == f[i])
        {
            ++i;
            while (i < f.size() && '"' != f[i]) out += f[i++];
            if (i < f.size()) ++i;
            continue;
        }
        if (MatchAt(f, i, "FF"))
        {
            int digits = 9, adv = 2;
            if (i + 2 < f.size() && '1' <= f[i + 2] && f[i + 2] <= '9') { digits = f[i + 2] - '0'; adv = 3; }
            if (!signDone)
            {
                if (!PicHasOwnSign(f, i) && neg) out += '-';
                signDone = true;
            }
            AppendFsec(out, fs, digits);
            i += adv;
            continue;
        }
        int width = 0, value = 0, adv = 0;
        if      (MatchAt(f, i, "DD")) {            value = d;  adv = 2; }
        else if (MatchAt(f, i, "HH")) { width = 2; value = h;  adv = 2; }
        else if (MatchAt(f, i, "MI")) { width = 2; value = mi; adv = 2; }
        else if (MatchAt(f, i, "SS")) { width = 2; value = s;  adv = 2; }
        else { out += f[i++]; continue; }
        if (!signDone)
        {
            if (!PicHasOwnSign(f, i) && neg) out += '-';
            signDone = true;
        }
        AppInt(out, value, width);
        i += adv;
    }
    return out;
}

std::string SqlFormat::FormatRaw (const char* bytes, size_t len) const
{
    if (0 < m_blobHexRowLength && len > static_cast<size_t>(m_blobHexRowLength))
        len = static_cast<size_t>(m_blobHexRowLength);
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i)
    {
        const unsigned char b = static_cast<unsigned char>(bytes[i]);
        out += HexDigit(b >> 4);
        out += HexDigit(b & 0x0F);
    }
    return out;
}

} // namespace Common