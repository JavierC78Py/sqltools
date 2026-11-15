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
    16.03.2003 bug fix, keyword normalization fails in many cases
*/

#include "stdafx.h"
#include "OpenEditor/OEView.h"
#include "OpenEditor/OEHighlighter.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

    using namespace std;
    using namespace OpenEditor;

bool COEditorView::PreNormalizeOnChar (NormalizeOnCharCxt& cxt, wchar_t ch)
{
    cxt.matched = false;

    if (GetSettings().GetNormalizeKeywords())
    {
        Position pos = GetPosition();
        HighlighterBase* phighlighter = &*m_highlighter;
        
        if (phighlighter 
        && IsSelectionEmpty()
        && (phighlighter->GetDelimiters()[ch])
        && pos.line < GetLineCount())
        {
            OEStringW lineBuff;
            GetLineW(pos.line, lineBuff);
            const wchar_t* str = lineBuff.data();
            int len = lineBuff.length();
            pos.column = pos2inx(str, len, pos.column);
            int i = pos.column - 1;
            for (; i >= 0 && !phighlighter->GetDelimiters()[str[i]]; i--)
                ;
            if (i != pos.column - 1)
            {
                cxt.sqr.start.line = cxt.sqr.end.line = pos.line;
                cxt.sqr.start.column = i + 1;
                cxt.sqr.end.column   = pos.column;

                if (phighlighter->IsKeyword(str + cxt.sqr.start.column, cxt.sqr.end.column - cxt.sqr.start.column, cxt.keyword)
                && wcsncmp(cxt.keyword.c_str(), str + cxt.sqr.start.column, cxt.sqr.end.column - cxt.sqr.start.column))
                {
                    // the if below prevents annoying "normalization" for SQL file extention 
                    // incorrect and ugly way to handle that but lets make sacrifice
                    if (cxt.sqr.start.column > 0 
                    && str[cxt.sqr.start.column-1] == '.'
                    && cxt.keyword == L"SQL"
                    && GetSettings().GetLanguage() == "PL/SQL")
                        return false;

                    cxt.matched = true;
                    cxt.sqr.start.column = inx2pos(str, len, cxt.sqr.start.column);
                    cxt.sqr.end.column   = inx2pos(str, len, cxt.sqr.end.column);
                }
            }
        }
    }

    return cxt.matched;
}

// it is not cheap :(
void COEditorView::NormalizeOnChar (NormalizeOnCharCxt& cxt)
{
    if (cxt.matched
    && IsNormalizeablePos(cxt.sqr.start))
    {
        UndoGroup undo(*this);
        Position pos = GetPosition();
        PushInUndoStack(pos);

        SetSelection(cxt.sqr);
        DeleteBlock(false);

        MoveTo(cxt.sqr.start);
        InsertBlock(cxt.keyword.c_str(), true, false);

        MoveTo(pos);
        PushInUndoStack(pos);
    }
}

bool COEditorView::IsNormalizeablePos (Position pos) const
{
    HighlighterBase* phighlighter = m_bSyntaxHighlight ? m_highlighter.get() : 0;

    if (phighlighter)
    {
        bool parsing(false);
        int status(0), quoteId(0);
        wchar_t qClose(0); // q-literal delimiter, see COEditorView::Paint
        ScanMultilineQuotes(pos.line, status, quoteId, parsing, qClose);
        phighlighter->SetMultilineQuotesState(status, quoteId, parsing, qClose);

        OEStringW lineBuff;
        GetLineW(pos.line, lineBuff);
        int len = lineBuff.length();
        const wchar_t* str = lineBuff.data(); 

        phighlighter->NextLine(str, len);

        LineTokenizer tokenizer(false, GetTabSpacing(), phighlighter->GetDelimiters());
        tokenizer.EnableProcessSpaces(true);
        tokenizer.StartScan(str, len);

        while (!tokenizer.Eol())
        {
            int curPos, wlen;
            const wchar_t* word;

            tokenizer.GetCurentWord(word, curPos, wlen);
            phighlighter->NextWord(word, wlen, curPos);

            if (curPos >= pos.column && pos.column < curPos + wlen)
                return phighlighter->IsPlainText();

            tokenizer.Next();
        }
    }

    return false;
}

// The position is lexically inside a quoted string ('...', "..." or a char literal), so quote
// auto-closing/auto-surrounding has to be suppressed there. Cloned from IsNormalizeablePos with three
// deliberate differences:
//  - scanning stops at the cursor, so the returned state is the one that precedes the character about
//    to be typed. IsNormalizeablePos instead looks for the word at/after its argument, which is what
//    keyword normalization needs but not this;
//  - falling off the end of the line returns the state after the last word rather than a hardcoded
//    false. That is the common case while typing and it is what makes multiline strings work;
//  - whitespace words are not fed to the highlighter. They carry no lexical state and feeding them
//    would clear m_isStartLine before a start-line comment (SQL*Plus "remark"/"prompt") is ever seen.
// Unit note: LineTokenizer::GetCurentWord reports tab expanded display columns, i.e. the same space
// Position::column lives in - NormalizeText below feeds that very pos straight into PosToInx. So no
// pos2inx/inx2pos conversion belongs here; adding one would misplace the cursor on every line that
// contains a tab.
// It is not cheap :( - the whole line is rescanned and retokenized, so callers should ask only for
// quote characters and only when the relevant setting is enabled.
bool COEditorView::IsInStringAt (Position pos) const
{
    HighlighterBase* phighlighter = m_bSyntaxHighlight ? m_highlighter.get() : 0;

    if (phighlighter && pos.line < GetLineCount())
    {
        bool parsing(false);
        int status(0), quoteId(0);
        // Restoring qClose is what makes this work for a *multiline* q-literal: the
        // caret line is retokenized from its entering state, and without the delimiter
        // a caret inside q'[...  would look like ordinary code, re-enabling the quote
        // auto-close that section 2.3 requires to stay suppressed.
        wchar_t qClose(0);
        ScanMultilineQuotes(pos.line, status, quoteId, parsing, qClose);
        phighlighter->SetMultilineQuotesState(status, quoteId, parsing, qClose);

        OEStringW lineBuff;
        GetLineW(pos.line, lineBuff);
        int len = lineBuff.length();
        const wchar_t* str = lineBuff.data(); 

        phighlighter->NextLine(str, len);

        LineTokenizer tokenizer(false, GetTabSpacing(), phighlighter->GetDelimiters());
        tokenizer.EnableProcessSpaces(true);
        tokenizer.StartScan(str, len);

        while (!tokenizer.Eol())
        {
            int curPos, wlen;
            const wchar_t* word;

            tokenizer.GetCurentWord(word, curPos, wlen);

            if (curPos >= pos.column)   // the rest of the line is at/after the cursor
                break;

            if (!tokenizer.IsSpace())
                phighlighter->NextWord(word, wlen, curPos);

            tokenizer.Next();
        }

        return phighlighter->IsInString();
    }

    // no syntax highlighting => there is no lexical state, treat the position as plain text
    return false;
}

/////////////////////////////////////////////////////////////////////////////
// Auto-close/auto-surround tracking (spec "Editor Auto-closing and
// Auto-surround", §3.5).
//
// Records are keyed by LineId, not by line index, so line insertions/deletions elsewhere
// in the document cannot make them point at the wrong line. Columns are stored in index
// space (pos2inx) so a tab-width change cannot invalidate them; callers convert at query
// time. All of it is transient: never persisted, never part of an undo record.
/////////////////////////////////////////////////////////////////////////////

void COEditorView::ResetAutoPairs ()
{
    m_autoPairs.clear();
    m_autoPairsLine = -1;
}

// Drops the pairs recorded on the lines in [firstLine, lastLine]. Called from
// OnChangedLines for every edit this view did not make itself, which realizes the §3.5
// invalidation rules conservatively: paste, drag&drop, reformat, undo/redo and external
// edits clear the affected lines. Plain single-character typing keeps its records and
// shifts them instead - see ShiftAutoPairs and m_autoPairEditing.
void COEditorView::ResetAutoPairs (int firstLine, int lastLine)
{
    if (m_autoPairs.empty())
        return;

    AutoPairMap::iterator it = m_autoPairs.begin();
    for (; it != m_autoPairs.end(); )
    {
        int line(-1);

        // an id that no longer resolves belongs to a line that is gone
        if (!FindById(it->first, line) || (line >= firstLine && line <= lastLine))
            it = m_autoPairs.erase(it);
        else
            ++it;
    }

    if (m_autoPairs.empty())
        m_autoPairsLine = -1;
}

// Records a closer this view auto-inserted. Called after the insert has completed, so
// closerInx is final, and only in insert mode while the view is attached.
void COEditorView::TrackAutoPair (int line, int openerInx, int closerInx, wchar_t closer)
{
    if (!m_bAttached || line < 0 || line >= GetLineCount())
        return;

    OpenEditor::LineId id = GetLineId(line);

    if (!id.Valid())
        return;

    // "the caret moved off the line" invalidates everything recorded before (§3.5). There
    // is no caret notification in this codebase, so it is enforced lazily, here and in
    // FindAutoCloser.
    if (m_autoPairsLine != -1 && m_autoPairsLine != line)
        ResetAutoPairs();

    m_autoPairs[id].push_back(AutoPairRec(id, openerInx, closerInx, closer));
    m_autoPairsLine = line;
}

// Fine-grained bookkeeping for the plain single-character edits OnChar makes: columns at
// or after the edit shift by delta (+1 insert, -1 delete), and a pair whose own character
// was deleted is dropped. This is what lets skip-over survive typing between the opener
// and the closer - the coarse OnChangedLines hook cannot express it, because the
// notification carries no column.
void COEditorView::ShiftAutoPairs (int line, int fromInx, int delta)
{
    if (m_autoPairs.empty() || !delta || line < 0 || line >= GetLineCount())
        return;

    AutoPairMap::iterator it = m_autoPairs.find(GetLineId(line));

    if (it == m_autoPairs.end())
        return;

    // inserting at fromInx shifts the columns >= fromInx; deleting the character at
    // fromInx shifts the columns > fromInx (the deleted one is handled separately)
    const int shiftFrom = delta > 0 ? fromInx : fromInx + 1;

    std::vector<AutoPairRec>& recs = it->second;

    for (size_t i(0); i < recs.size(); )
    {
        AutoPairRec& rec = recs.at(i);

        if (delta < 0 && (rec.closerInx == fromInx || rec.openerInx == fromInx))
        {   // the deleted character is part of the tracked pair => the pair is gone (§3.5)
            recs.erase(recs.begin() + i);
            continue;
        }

        if (rec.openerInx >= shiftFrom) rec.openerInx += delta;
        if (rec.closerInx >= shiftFrom) rec.closerInx += delta;

        if (rec.openerInx < 0 || rec.closerInx < 0)  // shifted off the start of the line
            recs.erase(recs.begin() + i);
        else
            i++;
    }

    if (recs.empty())
        m_autoPairs.erase(it);
}

// The skip-over query (§3.5, §3.10 rule 5.4): true when the character at (line, inx) -
// index space - is a closing character this view auto-inserted and that has not been
// consumed yet. The record is consumed on success, so a closer is skipped over only once.
bool COEditorView::FindAutoCloser (int line, int inx, wchar_t& closer)
{
    if (m_autoPairs.empty() || line < 0 || line >= GetLineCount())
        return false;

    // lazy "the caret moved off the line" invalidation (§3.5)
    if (m_autoPairsLine != -1 && m_autoPairsLine != line)
    {
        ResetAutoPairs();
        return false;
    }

    AutoPairMap::iterator it = m_autoPairs.find(GetLineId(line));

    if (it == m_autoPairs.end())
        return false;

    OEStringW lineBuff;
    GetLineW(line, lineBuff);
    const wchar_t* str = lineBuff.data();
    const int len = static_cast<int>(lineBuff.length());

    std::vector<AutoPairRec>& recs = it->second;

    for (size_t i(0); i < recs.size(); i++)
    {
        if (recs.at(i).closerInx != inx)
            continue;

        // the storage may have changed behind our back: never skip over a character that
        // is not the one this view inserted
        if (inx < 0 || inx >= len || str[inx] != recs.at(i).closer)
            return false;

        closer = recs.at(i).closer;
        recs.erase(recs.begin() + i);

        if (recs.empty())
            m_autoPairs.erase(it);

        return true;
    }

    return false;
}

bool COEditorView::NormalizeText (const EditContext& cxt, std::wstring& _str)
{
    int line, start, end;
    cxt.GetScanPosition(line, start, end);
    
    const COEditorView& view = static_cast<const COEditorView&>(cxt);
    HighlighterBase* phighlighter = view.m_bSyntaxHighlight ? view.m_highlighter.get() : 0;

    if (phighlighter)
    {
        bool parsing(false);
        int status(0), quoteId(0);
        wchar_t qClose(0); // q-literal delimiter, see COEditorView::Paint
        cxt.ScanMultilineQuotes(line, status, quoteId, parsing, qClose);
        phighlighter->SetMultilineQuotesState(status, quoteId, parsing, qClose);

        OEStringW lineBuff;
        cxt.GetLineW(line, lineBuff);
        int llen = lineBuff.length();
        const wchar_t* str = lineBuff.data(); 

        phighlighter->NextLine(str, llen);

        LineTokenizer tokenizer(false, cxt.GetTabSpacing(), phighlighter->GetDelimiters());
        tokenizer.EnableProcessSpaces(true);
        tokenizer.StartScan(str, llen);

        wstring keyword;

        while (!tokenizer.Eol())
        {
            int pos, wlen;
            const wchar_t* word;

            tokenizer.GetCurentWord(word, pos, wlen);
            phighlighter->NextWord(word, wlen, pos);

            // 16.03.2003 bug fix, keyword normalization fails in many cases
            if (((start <= pos && pos <= end) || (start <= (pos + wlen - 1) && (pos + wlen - 1) <= end))
            && phighlighter->IsPlainText() && phighlighter->IsKeyword(word, wlen, keyword))
            {
                _ASSERTE(wlen == static_cast<int>(keyword.length()));

                int inx = cxt.PosToInx(line, pos, llen) - start;

                for (int i(0); i < wlen; i++)
                    if (inx + i >= 0 && inx + i < end - start)
                        _str.at(inx + i) = keyword.at(i);
            }

            tokenizer.Next();
        }
    }

    return true;
}
