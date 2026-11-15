/* 
    Copyright (C) 2002 Aleksey Kochetov

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

/*
  10.03.2003 bug fix, start-line comment (promt & remark) should be separeted by delimiter from text
*/

#include "stdafx.h"
#include "OpenEditor/OEHelpers.h"
#include "OpenEditor/OEStorage.h"
#include "COMMON/StrHelpers.h" // Common::q_closer_for


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


namespace OpenEditor
{
    using namespace std;

MultilineQuotesScanner::MultilineQuotesScanner (Storage& storage)
: m_Storage(storage),
m_OpeningFastMap(0)
{
    m_nQuotesValidLimit = 0;
//    m_pDelimiters = 0;
    m_bCaseSensitive = false;
    m_OpeningFastMapDirty = true;
    m_bParsingAlways = true;
    m_qQuoteChar = 0;
}

bool MultilineQuotesScanner::IsCaseSensitive () const
{
  return m_bCaseSensitive;
}

void MultilineQuotesScanner::SetCaseSensitive (bool flag)
{
    m_bCaseSensitive = flag;
    m_OpeningFastMapDirty = true;
}

void MultilineQuotesScanner::ClearSettings ()
{
    // reset scanned quotes too, because it depends on settings
    m_mapQuotes.clear();
    m_nQuotesValidLimit = 0;

    m_StartLineQuotes.clear();
    m_SingleLineQuotes.clear();
    m_MultilineQuotes.clear();
    m_escapeChar.erase();
    m_qQuoteChar = 0;
    m_OpeningFastMapDirty = true;
    m_bParsingAlways = true;
    m_ParsingQuotes.first.erase();
    m_ParsingQuotes.second.erase();
//    m_pDelimiters = 0;
}

    inline
    pair<std::wstring, std::wstring> wpair (pair<std::string, std::string> p)
    {
        return std::pair<std::wstring, std::wstring>(Common::wstr(p.first), Common::wstr(p.second));
    }

void MultilineQuotesScanner::SetParsingQuotes (const pair<string, string>& _pair)
{
    m_bParsingAlways = false;
    m_ParsingQuotes = wpair(_pair);
    m_OpeningFastMapDirty = true;
}

int MultilineQuotesScanner::AddStartLineQuotes (const string& str)
{
    _ASSERTE(str.size());
    m_StartLineQuotes.push_back(Common::wstr(str));
    m_OpeningFastMapDirty = true;
    return m_StartLineQuotes.size() - 1;
}

int MultilineQuotesScanner::AddSingleLineQuotes (const string& str)
{
    _ASSERTE(str.size());
    m_SingleLineQuotes.push_back(Common::wstr(str));
    m_OpeningFastMapDirty = true;
    return m_SingleLineQuotes.size() - 1;
}

int MultilineQuotesScanner::AddMultilineQuotes (const pair<string, string>& _pair)
{
    _ASSERTE(_pair.first.size() && _pair.second.size());
    m_MultilineQuotes.push_back(wpair(_pair));
    m_OpeningFastMapDirty = true;
    return m_MultilineQuotes.size() - 1;
}

void MultilineQuotesScanner::AddEscapeChar (const string& esc)
{
    m_escapeChar = Common::wstr(esc);
}

void MultilineQuotesScanner::SetDelimitersMap (const DelimitersMap& delimiters)
{
    m_delimiters = delimiters;
}

void MultilineQuotesScanner::SetAlternativeQuotes (wchar_t quote)
{
    m_qQuoteChar = quote;
    m_OpeningFastMapDirty = true;
}

void MultilineQuotesScanner::Scan (int to_line, int& state, int& quoteId, bool& parsing, wchar_t& qClose)
{
    if (to_line > m_nQuotesValidLimit)
        buildMap(to_line + 100);

    map<int,Entry>::const_iterator
        it  = m_mapQuotes.begin(),
        end = m_mapQuotes.end();

    for (; it != end && it->first < to_line; ++it)
    {
        state   = it->second.state;
        quoteId = it->second.quoteId;
        parsing = it->second.parsing;
        qClose  = it->second.qClose;
    }
}

bool MultilineQuotesScanner::ScanLine (const wchar_t* str, int len, int& state, int& quoteId, bool& parsing, wchar_t& qClose, int* parsingLength, const ScanProbe* probeReq) const
{
    int  quotesBalance = 0;
    int  parsingBalance = 0;
    bool isStartSpace = true;
    bool ignoreRemainder = false;

    // The character the in-quote branch fast-compares against. For a q-literal it is
    // the runtime delimiter carried in qClose, and the configured table must not be
    // consulted: quoteId is not an index for that state, so .at() would throw
    // std::out_of_range on the first line that *starts* inside a q-literal
    // (section 4.5 item 1).
    wchar_t closingFastChr = (state == qsMultiline)   ? m_MultilineQuotes.at(quoteId).second.at(0)
                           : (state == qsAlternative) ? qClose
                                                      : wchar_t(0);

    if (m_OpeningFastMapDirty) buildOpeningFastMap();

    int i(0);
    size_t probeInx(0);
    for (; i < len; i++)
    {
        // Probe hook, and it has to be the FIRST statement in the body: `state` at
        // this point still describes exactly [0, i), which is what "is index i code
        // or inside a quote/comment?" means. One statement later and it would report
        // the state after i has been consumed.
        //
        // <= rather than == so that a duplicate or an out-of-order position is still
        // consumed instead of stalling the cursor for the rest of the walk; only the
        // exact index earns a 1, everything else keeps the caller's zero-fill, which
        // is the "not code" verdict the old per-position parsingLength test gave.
        if (probeReq)
        {
            while (probeInx < probeReq->count && probeReq->positions[probeInx] <= i)
            {
                if (!state && probeReq->positions[probeInx] == i)
                    probeReq->isCode[probeInx] = 1;
                ++probeInx;
            }

            if (probeInx >= probeReq->count)
                probeReq = 0; // every position answered, skip the test from here on
        }

        wchar_t ch = str[i];
        wchar_t probe = m_OpeningFastMap[ch];

        if (!m_bParsingAlways && !parsing)
        {
            if ((probe & eqtOpeningParse) && is_equal(str, len, i, m_ParsingQuotes.first))
            {
                parsing = true;
                parsingBalance++;
            }
            else
                continue;
        }

        if (!state)
        {
            if (isStartSpace && probe & eqtStartLine)
            {
                auto it(m_StartLineQuotes.begin()), end(m_StartLineQuotes.end());

                for (; it != end; ++it)
                    if (is_equal(str, len, i, *it))
                    {
                        ignoreRemainder = true;
                        break;
                    }

                if (ignoreRemainder) break;
            }

            if (probe & eqtSingleLine)
            {
                auto it(m_SingleLineQuotes.begin()), end(m_SingleLineQuotes.end());

                for (; it != end; ++it)
                    if (is_equal(str, len, i, *it))
                    {
                        ignoreRemainder = true;
                        break;
                    }

                if (ignoreRemainder) break;
            }

            // Oracle alternative quoting: q'X ... X' and nq'X ... X'. Tested before
            // the configured multiline quotes because the closer is chosen at runtime
            // and therefore has no entry in m_MultilineQuotes for the loop below to
            // find (section 4.2). On success `state` is no longer plain, which is why
            // that loop is guarded by !state.
            if (probe & eqtQQuote)
            {
                wchar_t closer(0);
                const int prefix = qQuotePrefix(str, len, i, closer);

                if (prefix)
                {
                    state          = qsAlternative;
                    quoteId        = 0; // meaningless for this state, qClose carries the delimiter
                    qClose         = closer;
                    closingFastChr = closer;
                    // Bumping the balance is what makes buildMap record an Entry for a
                    // line that ends inside the literal; without it the literal would
                    // not be cached and every line below would be misclassified
                    // (section 4.5 item 3).
                    quotesBalance++;
                    i += prefix - 1; // the loop's own ++i steps past the delimiter
                }
            }

            if (!state && probe & eqtOpeningMultiline)
            {
                auto it(m_MultilineQuotes.begin()), end(m_MultilineQuotes.end());

                for (int j(0); it != end; ++it, j++)
                    if (is_equal(str, len, i, it->first))
                    {
                        state   = qsMultiline;
                        quoteId = j;
                        closingFastChr = m_MultilineQuotes.at(quoteId).second.at(0);
                        quotesBalance++;
                        i += it->first.length() - 1;
                        break;
                    }
            }
        }
        else if (state == qsAlternative)
        {
            // A q-literal ends only at "<delimiter><quote>". Every other character -
            // including a bare quote, a semicolon, "--" or "/*" - is body text; that
            // is the whole point of q'[it's mine]'. The configured closer and the
            // escape chain below are deliberately not consulted: Oracle defines no
            // escape inside a q-literal (P5), and m_MultilineQuotes[quoteId] would be
            // an unchecked out-of-bounds subscript on a state whose quoteId is not an
            // index (section 4.5 item 2).
            if (ch == closingFastChr && i + 1 < len && str[i + 1] == m_qQuoteChar)
            {
                state          = qsPlain;
                qClose         = 0;
                closingFastChr = 0;
                quotesBalance--;
                i += 1; // the closing quote; the loop steps past it
            }
        }
        else if (ch == closingFastChr)
        {
            if (is_equal(str, len, i, m_MultilineQuotes[quoteId].second)
                && (!i
                    // escape is not defined or the previos fragment is not escape or it is not double escape
                    || m_escapeChar.empty()
                    // the current position < escape size
                    || i < static_cast<int>(m_escapeChar.size())
                    // the previous fragment is not escape
                    || wcsncmp(m_escapeChar.c_str(), str + i - m_escapeChar.size(), m_escapeChar.size())
                    // it is double escape
                    || (
                        // the current position >= 2 * escape size
                        i >= 2 * static_cast<int>(m_escapeChar.size())
                        // the previous previous fragment is escape
                        && !wcsncmp(m_escapeChar.c_str(), str + i - 2 * m_escapeChar.size(), m_escapeChar.size())
                    )
                )
            )
            {
                state = qsPlain;
                quotesBalance--;
                i += m_MultilineQuotes[quoteId].second.length() - 1;
                // 2018-01-02 bug fix, single line xml comment breaks highlighting when it is scrolled out of view
                // MultilineQuotes and ParsingQuotes can overlap, for example in XML, <> and <!-- -->
                // it is not a clean fix but not sure how to do that in proper way
                //if (parsingBalance)
                //{
                //    const string& pq = m_ParsingQuotes.second;
                //    const string& mq = m_MultilineQuotes[quoteId].second;
                //    if (mq.length() >= pq.length()
                //    && pq == mq.c_str() + (mq.length() - pq.length()))
                //    {
                //        parsing = false;
                //        parsingBalance--;
                //        state = quoteId = 0;
                //    }
                //}
            }
        }

        if (isStartSpace && !iswspace(ch))
            isStartSpace = false;

        if (!m_bParsingAlways && !state 
        && (probe & eqtClosingParse) && is_equal(str, len, i, m_ParsingQuotes.second))
        {
            parsing = false;
            parsingBalance--;
            state = quoteId = 0;
            qClose = 0;
        }
    }

    if (parsingLength)
        *parsingLength = ignoreRemainder ? i : len;

    return quotesBalance || parsingBalance ? true : false;
}

void MultilineQuotesScanner::buildMap (int to_line)
{
    m_mapQuotes.erase(m_mapQuotes.lower_bound(m_nQuotesValidLimit), m_mapQuotes.end());

    if (m_OpeningFastMapDirty) buildOpeningFastMap();

    int state(0), quoteId(0);
    bool parsing(m_bParsingAlways);
    wchar_t qClose(0);
    Scan(m_nQuotesValidLimit, state, quoteId, parsing, qClose);

    to_line = min(to_line, m_Storage.GetLineCount());
    for (int line = m_nQuotesValidLimit; line < to_line; line++)
    {
        OEStringW str;
        m_Storage.GetLineW(line, str);

        if (ScanLine(str.data(), str.length(), state, quoteId, parsing, qClose))
            m_mapQuotes.insert(map<int,Entry>::value_type(line, Entry(state, quoteId, parsing, qClose)));
    }

    m_nQuotesValidLimit = to_line;
}

void MultilineQuotesScanner::buildOpeningFastMap () const
{
    m_OpeningFastMap.erase();

    if (m_ParsingQuotes.first.size())
        m_OpeningFastMap.bitwiseOR(m_ParsingQuotes.first.at(0), eqtOpeningParse);
    if (m_ParsingQuotes.second.size())
        m_OpeningFastMap.bitwiseOR(m_ParsingQuotes.second.at(0), eqtClosingParse);

    {
        auto it(m_StartLineQuotes.begin()), end(m_StartLineQuotes.end());
        for (; it != end; ++it)
        {
            if (m_bCaseSensitive)
                m_OpeningFastMap.bitwiseOR(it->at(0), eqtStartLine);
            else
            {
                m_OpeningFastMap.bitwiseOR(towupper(it->at(0)), eqtStartLine);
                m_OpeningFastMap.bitwiseOR(towlower(it->at(0)), eqtStartLine);
            }
        }
    }

    {
        auto it(m_SingleLineQuotes.begin()), end(m_SingleLineQuotes.end());
        for (; it != end; ++it)
            if (m_bCaseSensitive)
                m_OpeningFastMap.bitwiseOR(it->at(0), eqtSingleLine);
            else
            {
                m_OpeningFastMap.bitwiseOR(towupper(it->at(0)), eqtSingleLine);
                m_OpeningFastMap.bitwiseOR(towlower(it->at(0)), eqtSingleLine);
            }
    }

    {
        auto it(m_MultilineQuotes.begin()), end(m_MultilineQuotes.end());
        for (; it != end; ++it)
            if (m_bCaseSensitive)
                m_OpeningFastMap.bitwiseOR(it->first.at(0), eqtOpeningMultiline);
            else
            {
                m_OpeningFastMap.bitwiseOR(towupper(it->first.at(0)), eqtOpeningMultiline);
                m_OpeningFastMap.bitwiseOR(towlower(it->first.at(0)), eqtOpeningMultiline);
            }
    }

    // Oracle alternative quoting. Only the two letters that can begin the prefix are
    // marked, and only when the language opted in, so an ordinary q/Q letter anywhere
    // else in the file costs a single array lookup and nothing more (section 4.2).
    if (m_qQuoteChar != 0)
    {
        m_OpeningFastMap.bitwiseOR(L'q', eqtQQuote);
        m_OpeningFastMap.bitwiseOR(L'Q', eqtQQuote);
        m_OpeningFastMap.bitwiseOR(L'n', eqtQQuote);
        m_OpeningFastMap.bitwiseOR(L'N', eqtQQuote);
    }

    m_OpeningFastMapDirty = false;
}

bool MultilineQuotesScanner::is_equal (const wchar_t* str, int len, int offset, const std::wstring& shape) const
{
    bool retVal = false;

    if ((len - offset) >= static_cast<int>(shape.length()))
    {
        if (m_bCaseSensitive)
            retVal = !wcsncmp(str + offset, shape.c_str(), shape.size()) ? true : false;
        else
            retVal = !wcsnicmp(str + offset, shape.c_str(), shape.size()) ? true : false;

        // check begin of fragment
        if (retVal && !m_delimiters[*shape.begin()]
        && offset > 0 && !m_delimiters[str[offset - 1]])
            retVal = false;

        // check end of fragment
        // 10.03.2003 bug fix, start-line comment (promt & remark) should be separeted by delimiter from text
        if (retVal && !m_delimiters[*shape.rbegin()]                    // last char is not delim
        && offset + static_cast<int>(shape.size()) 
            < len && str[offset + shape.size()]                         // there is a char after keyword 
        && !m_delimiters[str[offset + shape.size()]])                   // this char is not delim
            retVal = false;
    }

    return retVal;
}

// Oracle alternative quoting: q'X ... X' and nq'X ... X'.
// Unlike CommonHighlighter::openingOfQSeq, which is handed one whole word at a time
// and can therefore test the prefix by length, this runs over a raw line and has to
// establish the word boundary itself - from the language's delimiter map, exactly the
// way is_equal does. That is what keeps "myq'[x]'" an identifier followed by an
// ordinary string, and stops the q of UNIQUE or SEQUENCE from reading as a prefix.
// Returns the number of characters the prefix consumes - 3 for "q'X", 4 for "nq'X" -
// and 0 when there is no prefix at `offset`.
int MultilineQuotesScanner::qQuotePrefix (const wchar_t* str, int len, int offset, wchar_t& closer) const
{
    closer = 0;

    if (m_qQuoteChar == 0)
        return 0;

    // "q", or "nq" for the national-character form. Case is ignored even in a
    // case-sensitive language, because Oracle accepts both spellings - the same choice
    // openingOfQSeq makes.
    int prefix(0);
    if (offset < len && towlower(str[offset]) == L'q')
        prefix = 1;
    else if (offset + 1 < len && towlower(str[offset]) == L'n' && towlower(str[offset + 1]) == L'q')
        prefix = 2;

    if (prefix == 0)
        return 0;

    // the letter has to start a word
    if (offset > 0 && !m_delimiters[str[offset - 1]])
        return 0;

    // str[offset + prefix] is the opening quote and str[offset + prefix + 1] the
    // delimiter. Both have to sit on this line: the prefix and its delimiter cannot be
    // split across a line break, so a line ending in "q" simply has no q-literal.
    if (offset + prefix + 1 >= len)
        return 0;

    if (str[offset + prefix] != m_qQuoteChar)
        return 0;

    // q_closer_for returns 0 for whitespace and for the quote character itself, so
    // those reject the construct outright instead of entering a state that could never
    // be closed.
    closer = Common::q_closer_for(str[offset + prefix + 1]);
    if (closer == 0)
        return 0;

    return prefix + 2;
}

};
