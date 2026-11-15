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

#include "JsonWriter.h"

namespace Common
{

namespace
{
    // Buffer this long before touching the stream: the nine existing streamers do one `m_stream <<`
    // per cell, which at the plan's ~14 M-cell ceiling is ~14 M virtual calls on the hot path.
    const size_t kFlushAt = 8 * 1024;

    const unsigned kReplacement = 0xFFFDu;

    bool IsContinuation (unsigned char c) { return 0x80u == (c & 0xC0u); }

    void AppendHex4 (std::string& out, unsigned v)
    {
        static const char digits[] = "0123456789abcdef";
        out += '\\';
        out += 'u';
        out += digits[(v >> 12) & 0xFu];
        out += digits[(v >>  8) & 0xFu];
        out += digits[(v >>  4) & 0xFu];
        out += digits[ v        & 0xFu];
    }

    // Decodes the sequence at p (n bytes available) into a code point, setting len to the bytes
    // consumed; len is always 1..n so the caller always advances.
    unsigned DecodeUtf8 (const unsigned char* p, size_t n, size_t& len)
    {
        const unsigned b0 = p[0];

        if (b0 < 0x80u)
        {
            len = 1;
            return b0;
        }

        size_t   want;
        unsigned cp;
        if      (b0 >= 0xC2u && b0 <= 0xDFu) { want = 2; cp = (b0 & 0x1Fu) << 6;  }
        else if (b0 >= 0xE0u && b0 <= 0xEFu) { want = 3; cp = (b0 & 0x0Fu) << 12; }
        else if (b0 >= 0xF0u && b0 <= 0xF4u) { want = 4; cp = (b0 & 0x07u) << 18; }
        else
        {
            // 0x80-0xBF is a stray continuation, 0xC0/0xC1 only starts an overlong form, 0xF5-0xFF
            // never appears. Consume one byte so a following valid sequence still decodes.
            len = 1;
            return kReplacement;
        }

        size_t k = 1;
        while (k < want && k < n && IsContinuation(p[k]))
        {
            cp |= (p[k] & 0x3Fu) << (6 * (int)(want - 1 - k));
            ++k;
        }
        if (k < want)
        {
            // Unicode "maximal subpart" recovery: one truncated sequence yields one U+FFFD, never
            // one per surviving byte. A cut is the normal case - LOBs truncate at StringLimit.
            len = k;                 // ran out of input, or p[k] is not a continuation
            return kReplacement;
        }

        // Full length present. The second byte carries the range test for the four boundary leads;
        // without it the decoder would accept overlongs, encoded surrogates and >U+10FFFF as valid.
        const unsigned b1 = p[1];
        if ((0xE0u == b0 && b1 < 0xA0u) || (0xEDu == b0 && b1 > 0x9Fu)
         || (0xF0u == b0 && b1 < 0x90u) || (0xF4u == b0 && b1 > 0x8Fu))
        {
            len = want;
            return kReplacement;
        }

        len = want;
        return cp;
    }
}

JsonWriter::JsonWriter (std::ostream& out, int indent)
    : m_out(out), m_indent(indent)
{
    m_buf.reserve(kFlushAt + 1024);
}

JsonWriter::~JsonWriter ()
{
    Flush();
}

void JsonWriter::Put (const char* s, size_t n)
{
    m_buf.append(s, n);
    if (m_buf.size() >= kFlushAt)
        Flush();
}

void JsonWriter::Put (char c)
{
    m_buf += c;
    if (m_buf.size() >= kFlushAt)
        Flush();
}

void JsonWriter::Flush ()
{
    if (m_buf.empty())
        return;

    if (!m_out.rdbuf())
    {
        m_out.setstate(std::ios::badbit);
        m_buf.clear();
        return;
    }

    // sputn on the streambuf, not `m_out <<`: one virtual call for the whole buffer, no sentry. It
    // does not set stream state, so a short write must become badbit or good() means nothing.
    const std::streamsize want = (std::streamsize)m_buf.size();
    const std::streamsize got  = m_out.rdbuf()->sputn(m_buf.data(), want);
    if (got != want)
        m_out.setstate(std::ios::badbit);

    m_buf.clear();
}

void JsonWriter::WriteIndent ()
{
    if (m_indent <= 0)
        return;

    // Depth is m_stack.size(), not a separate counter, so it cannot drift out of step with the
    // frames it is supposed to describe.
    m_buf.append((size_t)m_indent * m_stack.size(), ' ');
    if (m_buf.size() >= kFlushAt)
        Flush();
}

void JsonWriter::Separate ()
{
    if (m_stack.empty())
        return;                      // a value at the document root needs no separator

    Frame& f = m_stack.back();
    if (f.m_afterKey)
    {
        f.m_afterKey = false;        // the value follows its key on the same line, even when pretty
    }
    else
    {
        if (f.m_needComma)
            Put(',');
        if (m_indent >= 0)
        {
            Put('\n');
            WriteIndent();
        }
        else if (!f.m_isObject)
            Put('\n');               // compact: one array element per line, objects stay inline
    }
    f.m_needComma = true;
}

void JsonWriter::BeginObject ()
{
    Separate();                      // separates this object within its parent, before the push
    Put('{');
    m_stack.push_back(Frame(true));
}

void JsonWriter::BeginArray ()
{
    Separate();
    Put('[');
    m_stack.push_back(Frame(false));
}

void JsonWriter::EndContainer (char close)
{
    if (m_stack.empty())
        return;                      // unbalanced call: drop it rather than corrupt the stream

    const Frame f = m_stack.back();
    m_stack.pop_back();

    // A non-empty container closes on its own line: indented to the parent depth now that this frame
    // is popped, bare when compact and an array. An empty one stays on one line as {} or [].
    if (f.m_needComma && (m_indent >= 0 || !f.m_isObject))
    {
        Put('\n');
        WriteIndent();
    }
    Put(close);
}

void JsonWriter::EndObject () { EndContainer('}'); }
void JsonWriter::EndArray  () { EndContainer(']'); }

void JsonWriter::WriteQuoted (const std::string& utf8)
{
    Put('\"');
    m_scratch.clear();               // reused across cells: no allocation per value
    Escape(utf8, m_scratch);
    Put(m_scratch.data(), m_scratch.size());
    Put('\"');
}

void JsonWriter::Key (const std::string& utf8)
{
    Separate();
    WriteQuoted(utf8);
    Put(':');
    if (m_indent >= 0)
        Put(' ');

    // Only an object can have members. A Key() inside an array is caller misuse; leaving afterKey
    // clear there means the following value is still comma-separated instead of being glued on.
    if (!m_stack.empty() && m_stack.back().m_isObject)
        m_stack.back().m_afterKey = true;
}

void JsonWriter::String (const std::string& utf8)
{
    Separate();
    WriteQuoted(utf8);
}

void JsonWriter::Null ()
{
    Separate();
    Put("null", 4);
}

void JsonWriter::RawNumber (const std::string& canonical)
{
    Separate();

    // Verbatim when legal, quoted otherwise. A safety net, not the route for a non-finite real:
    // JD11 has the streamer emit "NaN" / "Infinity" / "-Infinity" itself.
    if (IsJsonNumber(canonical))
        Put(canonical.data(), canonical.size());
    else
        WriteQuoted(canonical);
}

void JsonWriter::Escape (const std::string& utf8, std::string& out)
{
    const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8.data());
    const size_t n = utf8.size();

    for (size_t i = 0; i < n; )
    {
        size_t len = 1;
        const unsigned cp = DecodeUtf8(p + i, n - i, len);
        i += len;

        if (cp < 0x80u)
        {
            switch (cp)
            {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                // A raw control character is invalid JSON, so everything below U+0020 that has no
                // short form goes out as \u00XX. U+007F is legal raw and is left alone.
                if (cp < 0x20u)
                    AppendHex4(out, cp);
                else
                    out += (char)cp;
            }
            continue;
        }

        if (kReplacement == cp)
        {
            // A genuine U+FFFD and a substituted one are indistinguishable; both emit the canonical
            // three bytes, since copying the input would put invalid UTF-8 into the output.
            out += "\xEF\xBF\xBD";
            continue;
        }

        if (0x2028u == cp || 0x2029u == cp)
        {
            // Legal JSON, but both are line terminators to JavaScript and break a consumer that
            // inlines the string into source.
            AppendHex4(out, cp);
            continue;
        }

        if (cp <= 0xFFFFu)
        {
            out.append(reinterpret_cast<const char*>(p + i - len), len);
            continue;
        }

        // Above the BMP: a matched surrogate pair. Raw four-byte UTF-8 is equally legal and smaller,
        // but section 2.2 locks the pair form, and an unpaired surrogate would be invalid JSON.
        const unsigned v = cp - 0x10000u;
        AppendHex4(out, 0xD800u + (v >> 10));
        AppendHex4(out, 0xDC00u + (v & 0x3FFu));
    }
}

bool JsonWriter::IsJsonNumber (const std::string& s)
{
    // RFC 8259: number = [minus] int [frac] [exp], int = zero / (digit1-9 *DIGIT),
    // frac = decimal-point 1*DIGIT, exp = e [minus/plus] 1*DIGIT.
    const size_t n = s.size();
    size_t i = 0;

    if (i < n && '-' == s[i])
        ++i;

    if (i >= n)
        return false;
    if ('0' == s[i])
        ++i;                                    // "0" alone; "01" is not a JSON number
    else if (s[i] >= '1' && s[i] <= '9')
    {
        while (i < n && s[i] >= '0' && s[i] <= '9')
            ++i;
    }
    else
        return false;                           // no leading '+', no bare '.', no empty string

    if (i < n && '.' == s[i])
    {
        ++i;
        const size_t start = i;
        while (i < n && s[i] >= '0' && s[i] <= '9')
            ++i;
        if (i == start)
            return false;                       // digits are required after the point
    }

    if (i < n && ('e' == s[i] || 'E' == s[i]))
    {
        ++i;
        if (i < n && ('+' == s[i] || '-' == s[i]))
            ++i;
        const size_t start = i;
        while (i < n && s[i] >= '0' && s[i] <= '9')
            ++i;
        if (i == start)
            return false;                       // digits are required in the exponent
    }

    return i == n;                              // no trailing junk
}

}
