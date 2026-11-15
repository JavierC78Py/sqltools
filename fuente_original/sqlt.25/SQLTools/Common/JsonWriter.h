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
#ifndef __JsonWriter_h__
#define __JsonWriter_h__

#include <ostream>
#include <string>
#include <vector>

namespace Common
{

// A streaming JSON writer: no DOM, one reusable escape buffer, and all comma and indent state held
// here rather than in the caller. Free of MFC and oci.h so Tests/grid/harness gates it offline.
class JsonWriter
{
public:
    // indent < 0 is compact: no indentation, and one line per array element so a wide export stays
    // readable. indent >= 0 pretty-prints that many spaces per level. One flag, not two paths (JD7).
    explicit JsonWriter (std::ostream& out, int indent = -1);

    // Flushes the tail. Holds a reference to the stream, so it must not outlive it.
    ~JsonWriter ();

    // Containers.
    void BeginObject ();
    void EndObject   ();
    void BeginArray  ();
    void EndArray    ();

    // Members and values. Key() writes the member name and the colon; the value that follows is
    // placed on the same line even when pretty-printing.
    void Key       (const std::string& utf8);
    void String    (const std::string& utf8);
    void RawNumber (const std::string& canonical);
    void Null      ();

    // Pushes the buffer to the stream. Called automatically at ~8 KB and by the destructor.
    void Flush ();

    // False once the underlying stream has failed. No precedent in the Streamer hierarchy: nothing
    // on today's export path detects a stream failure, so a full disk yields a silent truncation.
    bool good () const { return m_out.good(); }

    // Appends the JSON string body (no quotes) for utf8 to out; exposed for testing, reused by
    // String()/Key(). Escapes controls, U+2028/9, bad UTF-8 as U+FFFD, non-BMP as a surrogate pair.
    static void Escape (const std::string& utf8, std::string& out);

    // True if s is a legal RFC 8259 number and can be emitted verbatim. A grammar check, not the
    // charset [-+0-9.eE] the plan sketched - that would admit "+1", "01", "1." and "1e".
    static bool IsJsonNumber (const std::string& s);

private:
    JsonWriter (const JsonWriter&) = delete;
    JsonWriter& operator = (const JsonWriter&) = delete;

    // One entry per open container. Nesting depth is m_stack.size() rather than a separate counter,
    // so the two cannot disagree.
    struct Frame
    {
        bool m_isObject;
        bool m_needComma;   // at least one member/element written: separate the next one
        bool m_afterKey;    // a Key() is waiting for its value: no comma, no line break
        explicit Frame (bool isObject)
            : m_isObject(isObject), m_needComma(false), m_afterKey(false) {}
    };

    void Separate     ();               // comma + line break before the next item
    void EndContainer (char close);
    void WriteQuoted  (const std::string& utf8);   // quotes + body, no separator
    void WriteIndent  ();
    void Put          (const char* s, size_t n);
    void Put          (char c);

    std::ostream&      m_out;
    std::string        m_buf;
    std::string        m_scratch;    // escape target, reused so a cell costs no allocation
    std::vector<Frame> m_stack;
    int                m_indent;
};

}

#endif // __JsonWriter_h__
