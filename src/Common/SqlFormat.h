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
#ifndef __SqlFormat_h__
#define __SqlFormat_h__

#include "SqlValue.h"
#include <string>

namespace Common
{

// Oracle format-model subset plus the grid's display state, the piece Stage 5 (plan $4.3) adds.
// Renders a typed SqlValue to UTF-8 the way the grid draws it: the six GridNls*Format strings
// (Number, Date, Timestamp, TimestampTz, IntervalYm, IntervalDs), the null marker GridNullRepresentation, the
// BLOB hex row limit GridBlobHexRowLength, and the two NLS numeric characters.
//
// Deliberately free of MFC and oci.h so the console harness compiles it next to SqlValue, and so
// the whole reader is one place rather than the per-kind literals SqlValue already knows how to
// spell on the wire. The subset is deliberately written down rather than inferred from a live
// server: the tokens it resolves are listed in FormatDate/FormatTimestamp/FormatNumber, and any
// token outside them is emitted literally, never silently dropped.
//
// This is a subset, not an OCI clone: English month/day names only, a fixed interpretation of the
// numeric picture ('9' and '0' both zero-fill so no leading-blank padding is produced), '$' and 'L'
// both render as '$', and TZR falls back to the numeric offset when no region is present. A live
// client compares the results against OCI (Tests/grid/live) and can fall back to the eager GetString
// path behind ValueTable::SetLazyRender, so a deviation is a measured choice, never a silent one.
class SqlFormat
{
public:
    SqlFormat();

    // Format strings, one per kind the grid draws. Time is not a kind: SqlValueKind::Date carries
    // both date and time-of-day, so Date's picture is the only one a DATE consults. Intervals get
    // two pictures because they are two unrelated types with two disjoint token sets - see
    // FormatIntervalYm/FormatIntervalDs. A token outside the set its formatter resolves is emitted
    // literally, so sharing one picture between them rendered values as the picture's own text.
    void SetNumberFormat    (const std::string& f) { m_numberFormat = f; }
    void SetDateFormat      (const std::string& f) { m_dateFormat = f; }
    void SetTimestampFormat (const std::string& f) { m_timestampFormat = f; }
    void SetTimestampTzFormat(const std::string& f) { m_timestampTzFormat = f; }
    void SetIntervalYmFormat(const std::string& f) { m_intervalYmFormat = f; }
    void SetIntervalDsFormat(const std::string& f) { m_intervalDsFormat = f; }

    const std::string& NumberFormat     () const { return m_numberFormat; }
    const std::string& DateFormat       () const { return m_dateFormat; }
    const std::string& TimestampFormat  () const { return m_timestampFormat; }
    const std::string& TimestampTzFormat() const { return m_timestampTzFormat; }
    const std::string& IntervalYmFormat () const { return m_intervalYmFormat; }
    const std::string& IntervalDsFormat () const { return m_intervalDsFormat; }

    // True when the number picture asks for the shortest canonical text (TM/TM9), which is the form
    // the server's own implicit conversion reads back unchanged. A write-back bind needs no TO_NUMBER
    // wrapper then, and naming TM as an input format model would only add a way to fail.
    bool IsMinimalNumberFormat () const;

    // GridNullRepresentation. An empty marker is legal and blanks NULLs.
    void SetNullText (const std::string& t)                 { m_nullText = t; }
    const std::string& NullText () const                    { return m_nullText; }

    // GridBlobHexRowLength. 0 means "no limit"; a positive value caps a RAW's hex dump at that many
    // bytes (so the grid's long-BLOB preview has a single source of truth here too).
    void SetBlobHexRowLength (int rows)                     { m_blobHexRowLength = rows; }
    int  BlobHexRowLength () const                          { return m_blobHexRowLength; }

    void SetDecimalChar (char c)                            { m_decimal = c; }
    void SetGroupChar   (char c)                            { m_group = c; }
    char DecimalChar () const                               { return m_decimal; }
    char GroupChar   () const                               { return m_group; }

    // The display text for one arena value. Only the kinds a grid actually draws are specialised;
    // Text passes its bytes through (ValueTable short-circuits this to avoid a duplicate copy) and
    // Unsupported returns its raw bytes, so both stay readable even though neither has a picture.
    std::string Render (const SqlValue& v) const;

private:
    std::string FormatNumber    (const SqlNumber& n) const;
    std::string FormatReal      (double v, int sigDigits) const;
    std::string FormatDate      (const SqlDate& d) const;
    std::string FormatTimestamp (const SqlTimestamp& t, bool withZone) const;
    std::string FormatIntervalYm(const SqlIntervalYm& v) const;
    std::string FormatIntervalDs(const SqlIntervalDs& v) const;
    std::string FormatRaw       (const char* bytes, size_t len) const;

    std::string m_numberFormat;
    std::string m_dateFormat;
    std::string m_timestampFormat;
    std::string m_timestampTzFormat;
    std::string m_intervalYmFormat;
    std::string m_intervalDsFormat;
    std::string m_nullText;
    int         m_blobHexRowLength;
    char        m_decimal;
    char        m_group;
};

}

#endif // __SqlFormat_h__