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
#ifndef __SqlValue_h__
#define __SqlValue_h__

#include "SqlArena.h"
#include "SqlNumber.h"
#include <string>

namespace Common
{

// The kind lives in the payload header, not in the column, so a column can still mix kinds.
enum class SqlValueKind : unsigned char
{
    Null = 0,
    Text, Number, BinaryFloat, BinaryDouble,
    Date, Timestamp, TimestampTz, TimestampLtz,
    IntervalYm, IntervalDs, Raw, Unsupported,
    KindCount
};

// Cross-kind Compare order, written down because Stage 6 sorts with it: Null < Number/Binary*
// < Date/Timestamp* < Interval* < Text < Raw < Unsupported, ties within a band by ordinal.
enum ESqlValueBand
{
    evbNull = 0, evbNumeric, evbTemporal, evbInterval, evbText, evbRaw, evbUnsupported
};

int SqlValueBand (SqlValueKind kind);

enum ESqlValueFlag
{
    evfNone     = 0,
    evfInterned = 0x01,   // shared by several cells: read-only, except for textOffset

    // The stored bytes are a prefix, cut by a fetch limit rather than by the data itself. Set by the
    // two fetch paths that can know a cut happened: OCI8::LobVar, whose getLobLength() clamps a LOB
    // read to AutoCursor::m_StringLimit and knows the true length at the moment it clamps, and
    // OCI8::StringVar, for a VARCHAR2 cut at its define buffer.
    //
    // The StringVar half was deferred until ORA-01406 could be verified against a live server,
    // because the headers settle nothing: oci.h defines no truncation indicator, oro.h:308-311 lists
    // the whole indicator vocabulary and -2 there is OCI_IND_BADNULL, and Variable::Define used to
    // pass 0 for rcodep, discarding the one signal OCI gives per column. That measurement has now
    // been taken and is recorded, with its table, at StringVar::IsTruncated. Its useful findings were
    // that the returned length cannot separate a value that exactly fills its buffer from one that
    // was cut to fill it, and that the indicator and the return code can - and agree.
    //
    // The fetch layer sets this before the payload is published, and that timing is what makes it
    // part of ValueDict's key - two cells holding the same bytes must not come to share one payload
    // when only one of them was cut. It is a property of the value, unlike evfInterned, which is a
    // property of how many cells happen to point at it.
    evfTruncated = 0x02
};

// Options for ToSqlLiteral(). evlNone is the historical behaviour and stays the default, so
// adding the parameter changes no existing call site and no harness expectation.
enum ESqlLiteralOption
{
    evlNone         = 0,
    evlAnsiTemporal = 0x01    // DATE '...' / TIMESTAMP '...' where Oracle accepts an ANSI literal
};

struct SqlDate
{
    short year;
    unsigned char month, day, hour, minute, second;
};

enum ESqlTzFlag
{
    etfNone   = 0,
    etfDst    = 0x01,     // daylight-saving offset applied
    etfRegion = 0x02      // a named region is present, not just a fixed offset
};

struct SqlTimestamp : SqlDate
{
    int fsec;                     // 0..999999999 ns
    signed char tzHour, tzMinute;
    unsigned char tzFlags;        // ESqlTzFlag
    char tzRegion[32];
    unsigned char tzRegionLen;    // 0 == fixed offset, no region id
};

struct SqlIntervalYm { int years, months; };
struct SqlIntervalDs { int days, hours, minutes, seconds; int fsec; };

// The 8-byte payload header. textOffset is what turns Stage 5's lazy formatting into
// once-per-distinct-value instead of once-per-cell.
struct SqlValueHeader
{
    enum { MaxLen = 0xFFFF };

    unsigned char  kind;
    unsigned char  flags;
    unsigned short len;             // body bytes following this header
    SqlArena::CellOffset textOffset;  // rendered display text, 0 == not rendered yet
};

static_assert(8 == sizeof(SqlValueHeader), "the payload header is promised to be 8 bytes");

// A non-owning handle onto an arena payload: two words, no std::string member,
// nothing to copy and nothing to free. A NULL cell is m_data == nullptr.
class SqlValue
{
public:
    SqlValue ();
    explicit SqlValue (const void* payload);

    SqlValueKind Kind () const { return m_kind; }

    // A NULL is either no payload at all (offset 0) or a payload whose header kind is Null. The
    // second form is what lets a NULL carry shared display text: ValueTable points every NULL cell
    // at one interned Null payload rather than storing a marker per cell, which is how the width
    // and line-count metrics come to measure NULLs exactly as StringTable did while a NULL stays
    // exactly distinguishable from an empty VARCHAR2 (B8). Kind is cached from the header, so
    // this costs no dereference either.
    bool IsNull () const { return 0 == m_data || SqlValueKind::Null == m_kind; }

    // True when what is stored is a prefix of the real value, cut by a fetch limit. This has to be
    // a flag test and cannot be inferred from the length: a payload does not record the size it was
    // cut from, so a truncated 4000-byte LOB and a genuine 4000-byte one are otherwise identical.
    bool IsTruncated () const;

    const void* Payload () const { return m_data; }
    size_t PayloadBytes () const;                 // header + body; the arena bills this total

    const SqlValueHeader* Header () const;
    const void* Body () const;                    // bytes after the header

    const char*      Text (size_t& len) const;    // Text/Raw/Unsupported bytes, not NUL-terminated
    const SqlNumber* Number () const;             // nullptr when the kind differs
    const SqlDate*   Date () const;
    const SqlTimestamp* Timestamp () const;       // also valid for the TZ and LTZ kinds
    const SqlIntervalYm* IntervalYm () const;
    const SqlIntervalDs* IntervalDs () const;
    bool BinaryFloat (float& out) const;
    bool BinaryDouble (double& out) const;

    int Compare (const SqlValue& other) const;    // type-aware, total order
    std::string ToSqlLiteral (unsigned options = evlNone) const;

    // Writers. Stage 2's fetch path and ValueDict both come through here, so the payload
    // layout lives in exactly one place and interning cannot drift from it.
    static SqlArena::CellOffset Store (SqlArena& arena, SqlValueKind kind,
                                       const void* body, size_t len, unsigned flags = evfNone);
    static SqlArena::CellOffset StoreText (SqlArena& arena, const char* text, size_t len,
                                           unsigned flags = evfNone);
    static SqlArena::CellOffset StoreNumber (SqlArena& arena, const SqlNumber& num);

    // A header-only Null payload. Store() refuses the Null kind on purpose - a NULL has no body -
    // so this is the one way to make one, and it is flagged interned because it is meant to be
    // shared by many cells: read-only except for textOffset, exactly as evfInterned promises.
    // The caller parks the display marker there once, and every NULL cell renders it for free.
    static SqlArena::CellOffset StoreNull (SqlArena& arena, unsigned flags = evfInterned);

    // T must be zero-initialised before it is passed in: padding is part of the payload
    // bytes, and uninitialised padding would make two equal values look different to Intern().
    template <typename T>
    static SqlArena::CellOffset StorePod (SqlArena& arena, SqlValueKind kind, const T& value,
                                          unsigned flags = evfNone)
    {
        return Store(arena, kind, &value, sizeof(T), flags);
    }

private:
    const void*  m_data;
    SqlValueKind m_kind;    // cached from the header so Kind() costs no dereference
};

static_assert(2 * sizeof(void*) >= sizeof(SqlValue), "SqlValue must stay two words");

}

#endif // __SqlValue_h__
