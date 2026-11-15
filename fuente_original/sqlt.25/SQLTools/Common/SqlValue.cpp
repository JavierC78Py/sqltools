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

#include "SqlValue.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Common
{

int SqlValueBand (SqlValueKind kind)
{
    switch (kind)
    {
    case SqlValueKind::Null:
        return evbNull;
    case SqlValueKind::Number:
    case SqlValueKind::BinaryFloat:
    case SqlValueKind::BinaryDouble:
        return evbNumeric;
    case SqlValueKind::Date:
    case SqlValueKind::Timestamp:
    case SqlValueKind::TimestampTz:
    case SqlValueKind::TimestampLtz:
        return evbTemporal;
    case SqlValueKind::IntervalYm:
    case SqlValueKind::IntervalDs:
        return evbInterval;
    case SqlValueKind::Text:
        return evbText;
    case SqlValueKind::Raw:
        return evbRaw;
    default:
        return evbUnsupported;
    }
}

SqlValue::SqlValue ()
    : m_data(0)
    , m_kind(SqlValueKind::Null)
{
}

SqlValue::SqlValue (const void* payload)
    : m_data(payload)
    , m_kind(0 == payload ? SqlValueKind::Null
                          : static_cast<SqlValueKind>(static_cast<const SqlValueHeader*>(payload)->kind))
{
}

const SqlValueHeader* SqlValue::Header () const
{
    return static_cast<const SqlValueHeader*>(m_data);
}

const void* SqlValue::Body () const
{
    return 0 == m_data ? 0 : static_cast<const char*>(m_data) + sizeof(SqlValueHeader);
}

size_t SqlValue::PayloadBytes () const
{
    const SqlValueHeader* h = Header();
    return 0 == h ? 0 : sizeof(SqlValueHeader) + h->len;
}

bool SqlValue::IsTruncated () const
{
    const SqlValueHeader* h = Header();
    return 0 != h && 0 != (h->flags & evfTruncated);
}

const char* SqlValue::Text (size_t& len) const
{
    const SqlValueHeader* h = Header();
    const bool textual = SqlValueKind::Text == m_kind || SqlValueKind::Raw == m_kind
                      || SqlValueKind::Unsupported == m_kind;
    if (0 == h || !textual)
    {
        len = 0;
        return 0;
    }

    len = h->len;
    return static_cast<const char*>(Body());
}

const SqlNumber* SqlValue::Number () const
{
    const SqlValueHeader* h = Header();
    if (0 == h || SqlValueKind::Number != m_kind || sizeof(SqlNumber) != h->len)
        return 0;
    return static_cast<const SqlNumber*>(Body());
}

const SqlDate* SqlValue::Date () const
{
    const SqlValueHeader* h = Header();
    if (0 == h || SqlValueKind::Date != m_kind || sizeof(SqlDate) != h->len)
        return 0;
    return static_cast<const SqlDate*>(Body());
}

const SqlTimestamp* SqlValue::Timestamp () const
{
    const SqlValueHeader* h = Header();
    const bool temporal = SqlValueKind::Timestamp == m_kind || SqlValueKind::TimestampTz == m_kind
                       || SqlValueKind::TimestampLtz == m_kind;
    if (0 == h || !temporal || sizeof(SqlTimestamp) != h->len)
        return 0;
    return static_cast<const SqlTimestamp*>(Body());
}

const SqlIntervalYm* SqlValue::IntervalYm () const
{
    const SqlValueHeader* h = Header();
    if (0 == h || SqlValueKind::IntervalYm != m_kind || sizeof(SqlIntervalYm) != h->len)
        return 0;
    return static_cast<const SqlIntervalYm*>(Body());
}

const SqlIntervalDs* SqlValue::IntervalDs () const
{
    const SqlValueHeader* h = Header();
    if (0 == h || SqlValueKind::IntervalDs != m_kind || sizeof(SqlIntervalDs) != h->len)
        return 0;
    return static_cast<const SqlIntervalDs*>(Body());
}

bool SqlValue::BinaryFloat (float& out) const
{
    const SqlValueHeader* h = Header();
    if (0 == h || SqlValueKind::BinaryFloat != m_kind || sizeof(float) != h->len)
        return false;
    memcpy(&out, Body(), sizeof(out));
    return true;
}

bool SqlValue::BinaryDouble (double& out) const
{
    const SqlValueHeader* h = Header();
    if (0 == h || SqlValueKind::BinaryDouble != m_kind || sizeof(double) != h->len)
        return false;
    memcpy(&out, Body(), sizeof(out));
    return true;
}

SqlArena::CellOffset SqlValue::Store (SqlArena& arena, SqlValueKind kind,
                                      const void* body, size_t len, unsigned flags)
{
    if (SqlValueKind::Null == kind)
        return SqlArena::NullOffset;                    // use StoreNull(); a NULL has no body
    if (SqlValueHeader::MaxLen < len || (0 != len && 0 == body))
        return SqlArena::NullOffset;

    const SqlArena::CellOffset off = arena.Allocate(sizeof(SqlValueHeader) + len);
    if (SqlArena::NullOffset == off)
        return SqlArena::NullOffset;

    SqlValueHeader* h = static_cast<SqlValueHeader*>(arena.At(off));
    h->kind = static_cast<unsigned char>(kind);
    h->flags = static_cast<unsigned char>(flags);
    h->len = static_cast<unsigned short>(len);
    h->textOffset = SqlArena::NullOffset;
    if (0 != len)
        memcpy(h + 1, body, len);

    return off;
}

SqlArena::CellOffset SqlValue::StoreText (SqlArena& arena, const char* text, size_t len,
                                          unsigned flags)
{
    return Store(arena, SqlValueKind::Text, text, len, flags);
}

SqlArena::CellOffset SqlValue::StoreNumber (SqlArena& arena, const SqlNumber& num)
{
    SqlNumber n;
    n.SetZero();                        // zeroes padding too, so equal values intern to equal bytes
    n.len = num.len;
    n.exp = num.exp;
    n.neg = num.neg;
    memcpy(n.digits, num.digits, SqlNumber::MaxDigits);
    return Store(arena, SqlValueKind::Number, &n, sizeof(n));
}

// The Null kind is deliberately unreachable through Store(): a NULL has no body, and letting one
// through here would produce a payload that claims a kind of Null while carrying bytes that no
// reader knows how to interpret. StoreNull is the only door, and it opens onto a header-only
// payload - 8 bytes, allocated once and shared by every NULL cell that points at it.
SqlArena::CellOffset SqlValue::StoreNull (SqlArena& arena, unsigned flags)
{
    const SqlArena::CellOffset off = arena.Allocate(sizeof(SqlValueHeader));
    if (SqlArena::NullOffset == off)
        return SqlArena::NullOffset;

    SqlValueHeader* h = static_cast<SqlValueHeader*>(arena.At(off));
    h->kind       = static_cast<unsigned char>(SqlValueKind::Null);
    h->flags      = static_cast<unsigned char>(flags);
    h->len        = 0;
    h->textOffset = SqlArena::NullOffset;    // no marker parked yet; the caller decides

    return off;
}

static int CompareBytes (const void* a, size_t alen, const void* b, size_t blen)
{
    const size_t n = (alen < blen) ? alen : blen;
    const int c = (0 == n) ? 0 : memcmp(a, b, n);
    if (0 != c)
        return (c < 0) ? -1 : 1;
    if (alen == blen)
        return 0;
    return (alen < blen) ? -1 : 1;
}

// NaN sorts after every real value and -0.0 equals 0.0 - the order a grid reader expects.
template <typename T>
static int CompareReal (T a, T b)
{
    const bool aNan = (0 != std::isnan(a)), bNan = (0 != std::isnan(b));
    if (aNan || bNan)
        return (aNan == bNan) ? 0 : (aNan ? 1 : -1);
    if (a < b) return -1;
    if (b < a) return 1;
    return 0;
}

static int CompareDate (const SqlDate& a, const SqlDate& b)
{
    if (a.year != b.year)     return (a.year < b.year) ? -1 : 1;
    if (a.month != b.month)   return (a.month < b.month) ? -1 : 1;
    if (a.day != b.day)       return (a.day < b.day) ? -1 : 1;
    if (a.hour != b.hour)     return (a.hour < b.hour) ? -1 : 1;
    if (a.minute != b.minute) return (a.minute < b.minute) ? -1 : 1;
    if (a.second != b.second) return (a.second < b.second) ? -1 : 1;
    return 0;
}

// tzRegion is a fixed 32-byte field with a separate length byte; never trust the length
// past the field, whatever wrote it.
static size_t RegionLen (const SqlTimestamp& t)
{
    return (sizeof(t.tzRegion) < t.tzRegionLen) ? sizeof(t.tzRegion) : t.tzRegionLen;
}

static int CompareTimestamp (const SqlTimestamp& a, const SqlTimestamp& b)
{
    const int c = CompareDate(a, b);      // wall-clock order first, offset only as a tiebreak
    if (0 != c) return c;
    if (a.fsec != b.fsec)           return (a.fsec < b.fsec) ? -1 : 1;
    if (a.tzHour != b.tzHour)       return (a.tzHour < b.tzHour) ? -1 : 1;
    if (a.tzMinute != b.tzMinute)   return (a.tzMinute < b.tzMinute) ? -1 : 1;
    if (a.tzFlags != b.tzFlags)     return (a.tzFlags < b.tzFlags) ? -1 : 1;
    const size_t alen = RegionLen(a), blen = RegionLen(b);
    if (alen != blen)               return (alen < blen) ? -1 : 1;
    return CompareBytes(a.tzRegion, alen, b.tzRegion, blen);
}

int SqlValue::Compare (const SqlValue& other) const
{
    if (m_kind != other.m_kind)
    {
        const int band = SqlValueBand(m_kind), otherBand = SqlValueBand(other.m_kind);
        if (band != otherBand)
            return (band < otherBand) ? -1 : 1;

        const int k = static_cast<int>(m_kind), otherK = static_cast<int>(other.m_kind);
        return (k < otherK) ? -1 : 1;     // same band, different kind: ordinal decides
    }

    switch (m_kind)
    {
    case SqlValueKind::Null:
        return 0;

    case SqlValueKind::Number:
    {
        const SqlNumber* a = Number();
        const SqlNumber* b = other.Number();
        if (a == b) return 0;
        if (0 == a) return -1;
        if (0 == b) return 1;
        return a->Compare(*b);
    }

    case SqlValueKind::BinaryFloat:
    {
        float a = 0, b = 0;
        const bool okA = BinaryFloat(a), okB = other.BinaryFloat(b);
        if (okA != okB) return okA ? 1 : -1;
        return okA ? CompareReal(a, b)
                   : CompareBytes(Body(), Header()->len, other.Body(), other.Header()->len);
    }

    case SqlValueKind::BinaryDouble:
    {
        double a = 0, b = 0;
        const bool okA = BinaryDouble(a), okB = other.BinaryDouble(b);
        if (okA != okB) return okA ? 1 : -1;
        return okA ? CompareReal(a, b)
                   : CompareBytes(Body(), Header()->len, other.Body(), other.Header()->len);
    }

    case SqlValueKind::Date:
    {
        const SqlDate* a = Date();
        const SqlDate* b = other.Date();
        if (a == b) return 0;
        if (0 == a) return -1;
        if (0 == b) return 1;
        return CompareDate(*a, *b);
    }

    case SqlValueKind::Timestamp:
    case SqlValueKind::TimestampTz:
    case SqlValueKind::TimestampLtz:
    {
        const SqlTimestamp* a = Timestamp();
        const SqlTimestamp* b = other.Timestamp();
        if (a == b) return 0;
        if (0 == a) return -1;
        if (0 == b) return 1;
        return CompareTimestamp(*a, *b);
    }

    case SqlValueKind::IntervalYm:
    {
        const SqlIntervalYm* a = IntervalYm();
        const SqlIntervalYm* b = other.IntervalYm();
        if (a == b) return 0;
        if (0 == a) return -1;
        if (0 == b) return 1;
        if (a->years != b->years)   return (a->years < b->years) ? -1 : 1;
        if (a->months != b->months) return (a->months < b->months) ? -1 : 1;
        return 0;
    }

    case SqlValueKind::IntervalDs:
    {
        const SqlIntervalDs* a = IntervalDs();
        const SqlIntervalDs* b = other.IntervalDs();
        if (a == b) return 0;
        if (0 == a) return -1;
        if (0 == b) return 1;
        if (a->days != b->days)       return (a->days < b->days) ? -1 : 1;
        if (a->hours != b->hours)     return (a->hours < b->hours) ? -1 : 1;
        if (a->minutes != b->minutes) return (a->minutes < b->minutes) ? -1 : 1;
        if (a->seconds != b->seconds) return (a->seconds < b->seconds) ? -1 : 1;
        if (a->fsec != b->fsec)       return (a->fsec < b->fsec) ? -1 : 1;
        return 0;
    }

    case SqlValueKind::Text:
    case SqlValueKind::Raw:
    case SqlValueKind::Unsupported:
    {
        // Sequenced on purpose: Text() writes the length that the call below then reads.
        size_t alen = 0, blen = 0;
        const char* a = Text(alen);
        const char* b = other.Text(blen);
        return CompareBytes(a, alen, b, blen);
    }

    default:
        break;
    }

    return 0;
}

static void AppendQuoted (std::string& out, const char* s, size_t len)
{
    out += '\'';
    for (size_t i = 0; i < len; ++i)
    {
        if ('\'' == s[i])
            out += '\'';              // Oracle escapes a quote by doubling it
        out += s[i];
    }
    out += '\'';
}

static std::string WallClock (const SqlTimestamp& t, int fsecDigits)
{
    char buf[80];
    if (0 < fsecDigits)
        snprintf(buf, sizeof(buf), "%04d-%02u-%02u %02u:%02u:%02u.%09d",
                 static_cast<int>(t.year), t.month, t.day, t.hour, t.minute, t.second, t.fsec);
    else
        snprintf(buf, sizeof(buf), "%04d-%02u-%02u %02u:%02u:%02u",
                 static_cast<int>(t.year), t.month, t.day, t.hour, t.minute, t.second);

    std::string out = buf;
    if (0 < fsecDigits && 9 > fsecDigits && out.size() > static_cast<size_t>(fsecDigits))
        out.erase(out.size() - (9 - fsecDigits));   // trim to the precision we actually have
    return out;
}

std::string SqlValue::ToSqlLiteral (unsigned options) const
{
    if (IsNull())
        return "NULL";

    char buf[128];
    switch (m_kind)
    {
    case SqlValueKind::Null:
        return "NULL";

    case SqlValueKind::Number:
    {
        const SqlNumber* n = Number();
        return 0 == n ? std::string("NULL") : n->ToCanonical();
    }

    case SqlValueKind::BinaryFloat:
    {
        float v = 0;
        if (!BinaryFloat(v))
            return "NULL";
        // Oracle spells these out; there is no %g form that would round-trip them.
        if (std::isnan(v))
            return "BINARY_FLOAT_NAN";
        if (std::isinf(v))
            return std::string(0 < v ? "" : "-") + "BINARY_FLOAT_INFINITY";
        snprintf(buf, sizeof(buf), "%.*gF", 9, static_cast<double>(v));
        return buf;
    }

    case SqlValueKind::BinaryDouble:
    {
        double v = 0;
        if (!BinaryDouble(v))
            return "NULL";
        if (std::isnan(v))
            return "BINARY_DOUBLE_NAN";
        if (std::isinf(v))
            return std::string(0 < v ? "" : "-") + "BINARY_DOUBLE_INFINITY";
        snprintf(buf, sizeof(buf), "%.*gD", 17, v);
        return buf;
    }

    case SqlValueKind::Date:
    {
        const SqlDate* d = Date();
        if (0 == d)
            return "NULL";
        // An ANSI DATE literal carries no time part - Oracle accepts only 'YYYY-MM-DD' between the
        // quotes - so it is available exactly for a midnight DATE, which is the common shape of a
        // column that really holds a day. Anything with a time keeps the explicit format model.
        if (0 != (options & evlAnsiTemporal) && 0 == d->hour && 0 == d->minute && 0 == d->second)
        {
            snprintf(buf, sizeof(buf), "DATE '%04d-%02u-%02u'",
                     static_cast<int>(d->year), d->month, d->day);
            return buf;
        }
        snprintf(buf, sizeof(buf), "TO_DATE('%04d-%02u-%02u %02u:%02u:%02u','YYYY-MM-DD HH24:MI:SS')",
                 static_cast<int>(d->year), d->month, d->day, d->hour, d->minute, d->second);
        return buf;
    }

    case SqlValueKind::Timestamp:
    case SqlValueKind::TimestampTz:
    case SqlValueKind::TimestampLtz:
    {
        const SqlTimestamp* t = Timestamp();
        if (0 == t)
            return "NULL";

        if (SqlValueKind::Timestamp == m_kind)
        {
            // ANSI TIMESTAMP takes the same wall-clock text and needs no format model, so it is
            // always available for a zone-less timestamp. There is no ANSI literal that carries a
            // zone, which is why the TZ and LTZ kinds below ignore the option entirely.
            const std::string wall = WallClock(*t, 9);
            if (0 != (options & evlAnsiTemporal))
                return "TIMESTAMP '" + wall + "'";
            return "TO_TIMESTAMP('" + wall + "','YYYY-MM-DD HH24:MI:SS.FF')";
        }

        std::string out = "TO_TIMESTAMP_TZ('" + WallClock(*t, 9);
        const size_t regionLen = RegionLen(*t);
        if (0 != (t->tzFlags & etfRegion) && 0 < regionLen)
        {
            out += ' ';
            out.append(t->tzRegion, regionLen);
            out += "','YYYY-MM-DD HH24:MI:SS.FF TZR')";
        }
        else
        {
            // tzHour/tzMinute are signed chars and OCI puts the sign on *both*, so a -05:30
            // zone arrives as tzHour=-5, tzMinute=-30. Printing tzMinute with %u promoted it
            // to 4294967266; the sign is already carried by the hour, so emit the magnitude.
            snprintf(buf, sizeof(buf), " %c%02d:%02d", (0 > t->tzHour ? '-' : '+'),
                     (0 > t->tzHour ? -static_cast<int>(t->tzHour) : static_cast<int>(t->tzHour)),
                     (0 > t->tzMinute ? -static_cast<int>(t->tzMinute)
                                      : static_cast<int>(t->tzMinute)));
            out += buf;
            out += "','YYYY-MM-DD HH24:MI:SS.FF TZH:TZM')";
        }
        return out;
    }

    case SqlValueKind::IntervalYm:
    {
        const SqlIntervalYm* v = IntervalYm();
        if (0 == v)
            return "NULL";
        const bool neg = (0 > v->years) || (0 > v->months);
        const int y = 0 > v->years ? -v->years : v->years;
        const int m = 0 > v->months ? -v->months : v->months;
        snprintf(buf, sizeof(buf), "INTERVAL '%s%d-%d' YEAR(9) TO MONTH", neg ? "-" : "", y, m);
        return buf;
    }

    case SqlValueKind::IntervalDs:
    {
        const SqlIntervalDs* v = IntervalDs();
        if (0 == v)
            return "NULL";
        const bool neg = (0 > v->days) || (0 > v->hours) || (0 > v->minutes)
                      || (0 > v->seconds) || (0 > v->fsec);
        snprintf(buf, sizeof(buf), "INTERVAL '%s%d %d:%d:%d.%09d' DAY(9) TO SECOND(9)",
                 neg ? "-" : "",
                 (0 > v->days ? -v->days : v->days),
                 (0 > v->hours ? -v->hours : v->hours),
                 (0 > v->minutes ? -v->minutes : v->minutes),
                 (0 > v->seconds ? -v->seconds : v->seconds),
                 (0 > v->fsec ? -v->fsec : v->fsec));
        return buf;
    }

    case SqlValueKind::Text:
    {
        size_t len = 0;
        const char* s = Text(len);
        std::string out;
        out.reserve(len + 2);
        AppendQuoted(out, 0 == s ? "" : s, len);
        return out;
    }

    case SqlValueKind::Raw:
    {
        size_t len = 0;
        const unsigned char* s = reinterpret_cast<const unsigned char*>(Text(len));
        if (0 == s)
            len = 0;
        std::string out = "HEXTORAW('";
        static const char hex[] = "0123456789ABCDEF";
        for (size_t i = 0; i < len; ++i)
        {
            out += hex[s[i] >> 4];
            out += hex[s[i] & 0x0F];
        }
        out += "')";
        return out;
    }

    default:
        break;
    }

    return "NULL";      // Unsupported: there is no literal that would round-trip it
}

}

