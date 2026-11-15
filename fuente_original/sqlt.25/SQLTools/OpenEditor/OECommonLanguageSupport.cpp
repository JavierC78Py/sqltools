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
    07.03.2003 improvement, match searcher mor C++, Jave and ... skips strings, charater and comments
    2011.10.02 improvement, FindMatch does not work for TXT files because no lenguage defined. Now C++ is used as default.
    2011.12.25 bug fix, Find Brace (Ctrl+[) causes an exception under some conditions
*/

#include "stdafx.h"
#include <stack>
#include <algorithm>
#include <COMMON/Fastmap.h>
#include "COMMON/StrHelpers.h"
#include <COMMON/ExceptionHelper.h>
#include "OpenEditor/OECommonLanguageSupport.h"
#include "OpenEditor/OELanguage.h"
#include "OpenEditor/OEStorage.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


namespace OpenEditor
{
    using namespace std;
    using namespace Common;

    static
    bool exist_any (set<int> s1, set<int> s2)
    {
        for (set<int>::const_iterator it1 = s1.begin(); it1 != s1.end(); ++it1)
        {
            set<int>::const_iterator it2 = s2.find(*it1);
            if (it2 != s2.end())
                return true;
        }

        return false;
    }

    struct MatchToken
    {
        wstring keyword;
        int orientation;
        set<int> lexemes;

        MatchToken () { orientation = 0; }
        MatchToken (const wstring& str, int ornt) : keyword(str), orientation(ornt) {}
    };

    struct MatchTokenPos : MatchToken
    {
        int position;

        MatchTokenPos () { position = 0; }
        MatchTokenPos (const MatchToken& tkn, int pos) : MatchToken(tkn), position(pos) {}
    };

    struct MatchEntry
    {
        int balance;
        set<int> lexemes;

        MatchEntry (int bal) : balance(bal) {}
        MatchEntry (const MatchToken& tkn) : lexemes(tkn.lexemes), balance(tkn.orientation) {}

        bool IsOrientationSame (const MatchToken tkn) const { return (balance > 0 && tkn.orientation > 0) || (balance < 0 && tkn.orientation < 0); }
    };

    struct CommonLanguageSupport::Context
    {
        bool backward;
        bool casesensitive;
        FastmapW<bool> fastmap;
        map<std::wstring, MatchToken> fullmap;

        Context () : fastmap(false) {}

        stack<MatchEntry> matchStack;
        vector<MatchTokenPos> tokens;

        void init (const LanguagePtr&);
    };

    void CommonLanguageSupport::Context::init (const LanguagePtr& lang)
    {
        backward = false;
        casesensitive = lang->IsCaseSensitive();

        const vector<vector<string> >& braces = lang->GetMatchBraces();

        for (unsigned i = 0; i < braces.size(); i++)
        {
            const vector<string>& braceSet = braces[i];

            for (unsigned j = 0; j < braceSet.size(); j++)
            {
                std::wstring brace;

                if (casesensitive)
                {
                    brace = wstr(braceSet[j]);
                    fastmap.assign(brace[0], true);
                }
                else
                {
                    to_upper_str(wstr(braceSet[j]).c_str(), brace);
                    fastmap.assign(towupper(brace[0]), true);
                    fastmap.assign(towlower(brace[0]), true);
                }

                auto it = fullmap.find(brace);
                if (it == fullmap.end())
                {
                    int orientation = (j == 0) ? 1 : ((j == braceSet.size()-1) ? -1 : 0);
                    fullmap[brace] = MatchToken(brace, orientation);
                    it = fullmap.find(brace);
                }
                it->second.lexemes.insert(i);
            }
        }
    }

CommonLanguageSupport::CommonLanguageSupport (Storage* pstorage)
    : LanguageSupport(pstorage),
    m_cxt(new Context)
{
    string language = m_pStorage->GetSettings().GetLanguage();

    if (language == "NONE")
        language = "C++"; // we need some language and why not C++

    const LanguagePtr langPtr = LanguagesCollection::Find(language);

    m_cxt->init(langPtr);
}

CommonLanguageSupport::~CommonLanguageSupport ()
{
    delete m_cxt;
}

bool CommonLanguageSupport::FindMatch (int _line, int _offset, Match& match, bool quickExactMatch /*= false*/)
{
    _CHECK_AND_THROW_(m_pStorage, "CommonLanguageSupport has not been initialized!");

    Context& cxt = *m_cxt;
    cxt.backward = false;
    cxt.tokens.clear();
    cxt.matchStack = stack<MatchEntry>();

    cxt.matchStack.push(MatchEntry(1)); // search for any closing brace

    match.found    = false;
    match.partial  = true;
    match.backward = false;
    match.broken   = false;
    match.line[0]   = _line;
    match.offset[0] = _offset;
    match.length[0] = 0;

    {
        readTokens(_line);

        vector<MatchTokenPos>::const_iterator it = cxt.tokens.begin();
        for (; it != cxt.tokens.end(); ++it)
        {
            if (it->position <= _offset
            && _offset < it->position + static_cast<int>(it->keyword.length()))
            {
                _offset = it->position;
                cxt.matchStack.pop(); // empty stack - search for the current lexeme
                cxt.backward = it->orientation == -1 ? true : false;

                match.backward  = cxt.backward;
                match.partial   = false;
                match.offset[0] = it->position;
                match.length[0] = it->keyword.length();
                break;
            }
        }
    }

    if (quickExactMatch && match.partial)
        return false;

    int offset = _offset;
    int nlines = m_pStorage->GetLineCount();
    for (int line = _line; !match.backward ? line < nlines : line >= 0; !match.backward ? line++ : line--)
    {
        readTokens(line);

        vector<MatchTokenPos>::const_iterator it = cxt.tokens.begin();
        for (; it != cxt.tokens.end(); ++it)
        {
            if (!match.backward ? (it->position >= offset) : (it->position <= offset))
            {
                if (cxt.matchStack.empty()
                || cxt.matchStack.top().IsOrientationSame(*it))
                {
                    cxt.matchStack.push(*it);

                    if (cxt.matchStack.size() == 1         // it's a top
                    && cxt.matchStack.top().balance == 0)  // if it's optional/middle brace/keyword
                        cxt.matchStack.top().balance = 1;  // then assume that it's a start point
                }
                else
                {
                    MatchEntry& top = cxt.matchStack.top();

                    if (top.lexemes.empty() // search for any
                    || exist_any(top.lexemes, it->lexemes))
                    {
                        if (!match.partial              // known search
                        && !match.backward              // do it if it's forward search
                        && it->orientation == 0         // it's an optional/middle brace/keyword
                        && cxt.matchStack.size() == 1)  // it's a top
                        {
                            top.balance = 0;
                        }
                        else
                            top.balance += it->orientation;

                        if (!top.balance)
                            cxt.matchStack.pop();
                    }
                    else
                        match.broken = true;

                    if (match.broken || cxt.matchStack.empty())
                    {
                        match.found = true;
                        match.line  [1] = line;
                        match.length[1] = it->keyword.length();
                        match.offset[1] = it->position;
                        break;
                    }
                }
            }
        }

        if (match.found)
            break;

        offset = !match.backward ? 0 : INT_MAX;
    }

    if (!match.partial && !match.found)
        match.broken = true;

    return match.found;
}

void CommonLanguageSupport::readTokens (int line)
{
    Context& cxt = *m_cxt;
    MultilineQuotesScanner& scanner = m_pStorage->GetMultilineQuotesScanner();
    LineTokenizer tokenizer(false, 1, scanner.GetDelimitersMap());

    OEStringW lineBuff;
    m_pStorage->GetLineW(line, lineBuff);
    const wchar_t* lineStr = lineBuff.data(); 
    int lineLength = lineBuff.length();

    tokenizer.StartScan(lineStr, lineLength);
    cxt.tokens.clear();

    // Brace tokens found in the line, as (map entry, offset). Pointers into
    // cxt.fullmap rather than copies, because MatchToken carries a wstring and a
    // set<int> and a long line holds tens of thousands of them; std::map references
    // stay valid as nothing below inserts into fullmap.
    //
    // Two passes instead of one, and the reason is the quote scan. Whether a brace is
    // code or sits inside a string/comment is a property of the whole prefix before
    // it, so this used to call ScanLine(str, pos) once per brace - restarting the scan
    // from index 0 every time. That is O(braces * lineLength): one 435 K-character
    // JSON line, roughly 20 K braces, summed to ~10^10 character steps and hung the
    // idle handler that HighlightBraces runs in. Collecting the offsets first lets a
    // single ScanLine answer all of them as it walks past, which is O(lineLength)
    // however many braces the line holds.
    vector<pair<const MatchToken*, int> > candidates;

    while (!tokenizer.Eol())
    {
        int pos, len;
        const wchar_t* str;
        tokenizer.GetCurentWord(str, pos, len);

        if (len > 0 && cxt.fastmap[*str])
        {
            wstring brace(str, len);

            if (!cxt.casesensitive)
                to_upper_str(brace.c_str(), brace);

            auto it = cxt.fullmap.find(brace);
            if (it != cxt.fullmap.end())
                candidates.push_back(make_pair(&it->second, pos));
        }
        tokenizer.Next();
    }

    if (!candidates.empty())
    {
        // Ascending and duplicate-free, which is what ScanProbe requires: the
        // tokenizer walks left to right and yields each offset exactly once.
        vector<int>  offsets(candidates.size());
        vector<char> isCode(candidates.size(), 0);

        for (size_t n = 0; n < candidates.size(); n++)
            offsets[n] = candidates[n].second;

        // probe the position status
        bool parsing = false; // ignore it
        int state = 0, quoteId = 0;
        wchar_t qClose = 0; // ignore it, only `state` matters here
        scanner.Scan(line, state, quoteId, parsing, qClose);

        MultilineQuotesScanner::ScanProbe probeReq;
        probeReq.positions = &offsets[0];
        probeReq.count     = offsets.size();
        probeReq.isCode    = &isCode[0];

        // len is the whole line now, not the offset of one brace - that is the point
        // of the probe, one walk instead of one per brace. parsingLength is therefore
        // not requested: "pos <= parsingLength" only ever meant "no comment swallowed
        // the rest of the line before pos", and a position the walk broke out of is
        // never probed, so its isCode keeps the zero-fill. Same verdict.
        scanner.ScanLine(lineStr, lineLength, state, quoteId, parsing, qClose, 0, &probeReq);

        // if it's code then change balance
        for (size_t n = 0; n < candidates.size(); n++)
            if (isCode[n])
                cxt.tokens.push_back(MatchTokenPos(*candidates[n].first, candidates[n].second));
    }

    if (cxt.backward)
        reverse(cxt.tokens.begin(), cxt.tokens.end());
}

};
