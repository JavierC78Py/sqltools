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

// 22.07.2002 bug fix, stream block delete fails below EOF
// 03.08.2002 bug fix, block copy is too slow on 10M file
// 14.08.2002 bug fix, indent/undent block does not work if selection is not normalized
// 17.08.2002 bug fix, block copy fails below EOF
// 07.09.2002 bug fix, copy/paste operation loses the first blank line
// 17.09.2002 performance tuning, insert/delete block operation
// 13.01.2003 bug fix, useless selection on delete word undo
// 07.03.2003 bug fix, wrong columnar selection after undo
// 16.03.2003 bug fix, autoindent always uses a previos line as base even the line is empty
// 16.03.2003 bug fix, scroll position fails on delete boolmark lines operation
// 26.05.2003 bug fix, selection may still be out of visible text after undo
// 03.06.2003 bug fix, sql find match fails on select/insert/... if there is no ending ';'
// 10.06.2004 improvement, text has been sort implemented (currently only 1 sort key)
// 11.10.2004 improvement, columnar edit mode has been added
// 11.10.2004 improvement, columnar indent and undent mode has been added (if columnar mode is on)
// 24.10.2004 improvement, indent and undent align each line individually to the neatest indent level
// 30.03.2005 bug fix, sorting does not completely remove duplicate rows if a number of dups is even
// 2011.09.18 improvement, block indent keeps a selection anchored to a text
// 2011.09.18 bug fix, block undent does not work correctly if tab size > indent size
// 2011.11.28 bug fix, insert and delete of selection do not handle tabified lines properly in the column mode
// 2014.02.04 bug fix, selection stays on cursor move by Ctrl+[
// 2014.02.04 bug fix, paste on multi-line columnar selection uses the cursor position 
//                     instead of the beginning of selection
// 2014.05.09 bug fix, template changes are not available in already open files
// 2016.06.14 improvement, Alternative Columnar selection mode 
// 2026.07.27 bug fix, sort operation produced incorrect results when sorting text containing tab characters


#include "stdafx.h"
#include <algorithm>
#include <sstream>
#include <COMMON/AppGlobal.h>
#include <COMMON/ExceptionHelper.h>
#include <COMMON/StrHelpers.h>
#include "OpenEditor/OEContext.h"
#include "OpenEditor/OELanguageManager.h"
#include "OpenEditor/OESmartIndent.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define _CHECK_ALL_PTR_ _CHECK_AND_THROW_(m_pStorage, "Editor has not been initialized properly!")

namespace OpenEditor
{
    using namespace std;


    void space4insertion (std::wstring& buff, int from, int to, bool use_tab, int tab_spacing)
    {
        _ASSERTE(to - from >= 0);

        buff.erase();

        int cur_tabs = from / tab_spacing;
        int new_tabs = to / tab_spacing;

        if (use_tab)
        {
            int spaces   = to - new_tabs * tab_spacing;
            int tabs     = new_tabs - cur_tabs;
            buff.append(tabs, '\t');
            buff.append(spaces, ' ');
        }
        else
            buff.append(to - from, ' ');
    }


void EditContext::PushInUndoStack (Position pos)
{
    pos.column = pos2inx(pos.line, pos.column, true); // 07.03.2003 bug fix, wrong columnar selection after undo
    m_pStorage->PushInUndoStack(pos);
}

void EditContext::PushInUndoStack (EBlockMode mode, bool altColumnarMode, Square sel)
{
    sel.start.column = pos2inx(sel.start.line, sel.start.column, true); // 07.03.2003 bug fix, wrong columnar selection after undo
    sel.end.column = pos2inx(sel.end.line, sel.end.column, true);
    m_pStorage->PushInUndoStack(mode, altColumnarMode, sel);
}

void EditContext::expandVirtualSpace (int line, int column)
{
    m_pStorage->ExpandLinesTo(line);

    int length = GetLineLength(line);
    if (column > length)
    {
        std::wstring buff;
        space4insertion(buff, length, column, !GetTabExpand(), GetTabSpacing());
        InsertLinePart(line, length, buff.c_str(), buff.length());
    }
}

void EditContext::InsertLinePart (int line, int column, const wchar_t* str, int len)
{
    expandVirtualSpace(line, column);
    m_pStorage->InsertLinePart(line, !column ? 0 : pos2inx(line, column), str, len);
}

void EditContext::DeleteLinePart (int line, int from, int to)
{
    if (line < GetLineCount())
    {
        m_pStorage->DeleteLinePart(line, !from ? 0 : pos2inx(line, from), !to   ? 0 : pos2inx(line, to));
    }
}

void EditContext::SplitLine (int line, int pos)
{
    if (line < GetLineCount())
    {
        m_pStorage->SplitLine(line, !pos ? 0 : pos2inx(line, pos, true));
    }
}

void EditContext::DoIndent (bool moveOnly)
{
    UndoGroup undoGroup(*this);

    Position pos   = GetPosition();
    int indentSize = GetIndentSpacing();
    int newColumn  = (pos.column / indentSize + 1) * indentSize;

    if (!moveOnly && (!GetCursorBeyondEOL()
    || (pos.line < GetLineCount() && pos.column < GetLineLength(pos.line))))
    {
        std::wstring buff;
        space4insertion(buff, pos.column, newColumn, !GetTabExpand(), GetTabSpacing());
        InsertLinePart(pos.line, pos.column, buff.c_str(), buff.length());
    }
    pos.column = newColumn;

    MoveTo(pos);
}

void EditContext::DoUndent ()
{
    int indentSize = GetIndentSpacing();
    Position pos   = GetPosition();
    pos.column = max(0, (pos.column / indentSize
                         - ((pos.column % indentSize) ? 0 : 1)) * indentSize);
    MoveTo(pos);
}

void EditContext::DoCarriageReturn ()
{
    UndoGroup undoGroup(*this);

    int column;

    if (m_curPos.line < GetLineCount())
        //column = m_curPos.column;
        column = min(m_curPos.column, GetLineLength(m_curPos.line));
    else
        column = 0;

    expandVirtualSpace(m_curPos.line, column);
    m_pStorage->Insert('\r', m_curPos.line, !column ? 0 : pos2inx(m_curPos.line, column));

    if (GoToDown(true) && GoToStart(true))
    {
        switch (GetIndentType())
        {
        case eiAuto:
        case eiSmart:
            {
                const Position caret = GetPosition();
                const bool     smart = GetIndentType() == eiSmart;
                Position       pos   = caret;

                // Enter can land INSIDE a line's indentation, leaving a whitespace-only
                // fragment whose column is not the level the engine reads that line at.
                if (smart)
                    clearLineIfBlank(caret.line - 1);

                if (computeAutoIndentBase(pos))
                {
                    // The new line is a split's tail and can already carry the head's
                    // indentation; eiSmart owns it outright, so clear it or the two add up.
                    if (smart && applySmartIndent(caret, pos))
                        clearLeadingWhitespace(caret.line);

                    insertAutoIndent(pos);
                }
            }
            break;
        default:; // eiNone & others
        }
    }
    PushInUndoStack(m_curPos);
}

bool EditContext::computeAutoIndentBase (Position& pos) const
{
    if (pos.line <= 0 || pos.line >= GetLineCount())
        return false;

    OEStringW lineBuff;

    // 16.03.2003 bug fix, autoindent always uses a previos line as base even the line is empty
    for (int baseLine = pos.line - 1; baseLine >= 0 && lineBuff.length() == 0; baseLine--)
        GetLineW(baseLine, lineBuff);

    int len = lineBuff.length();
    const wchar_t* str = lineBuff.data();

    int i(0);
    for (; i < len && iswspace(str[i]); i++);

    pos.column = inx2pos(str, len, i);
    return true;
}

// True when the smart rules governed the new line, so its leading whitespace is indent
// the caller may rewrite; false leaves eiAuto standing (no rules, !confident, literal).
bool EditContext::applySmartIndent (Position caret, Position& base) const
{
    // No rules for this language: eiAuto wins.
    const SmartIndent* smart = LanguageManager::GetSmartIndent(GetSettings().GetLanguage());
    if (!smart)
        return false;

    // Enter split the caret's line, so what sits to the RIGHT of column 0 is the tail of
    // the same statement; its first token says dedent-under-opener or keep-continuing.
    const SmartIndentInfo info = smart->Compute(*m_pStorage, caret.line, SmartIndent::kWholeLine);

    if (!info.confident || info.inComment || info.inString)
        return false;

    // levels is negative for a tail starting with END / ELSE / ELSIF / EXCEPTION / WHEN
    // or ')', which sit above the level in force rather than below it.
    base.column = max(0, base.column + info.levels * GetIndentSpacing());
    return true;
}

void EditContext::insertAutoIndent (const Position& pos)
{
    if (!(GetCursorBeyondEOL() && GetLineLength(pos.line) == 0))
    {
        std::wstring buff;
        space4insertion(buff, 0, pos.column, !GetTabExpand(), GetTabSpacing());
        InsertLinePart(pos.line, 0, buff.c_str(), buff.length());
    }

    MoveTo(pos, true);
}

// Index just past a line's leading whitespace, or -1 when the line is not in the buffer.
// `length` receives its length, so a caller need not read the line twice.
int EditContext::leadingWhitespaceEnd (int line, int& length) const
{
    length = 0;

    if (line < 0 || line >= GetLineCount())
        return -1;

    OEStringW lineBuff;
    GetLineW(line, lineBuff);

    length = lineBuff.length();

    const wchar_t* str = lineBuff.data();

    int i(0);
    for (; i < length && iswspace(str[i]); i++);

    return i;
}

void EditContext::clearLeadingWhitespace (int line)
{
    int length(0);
    const int i = leadingWhitespaceEnd(line, length);

    if (i > 0)
        DeleteLinePart(line, 0, inx2pos(line, i));
}

void EditContext::clearLineIfBlank (int line)
{
    int length(0);
    const int i = leadingWhitespaceEnd(line, length);

    if (length > 0 && i == length)
        DeleteLinePart(line, 0, inx2pos(line, i));
}

    // Stage 3(a) trigger (a): keywords whose completion pulls a line back out of the block it was
    // indented into, matched case-insensitively. Only a GATE - DedentLevel decides (plan §6).
    static bool isDedentWord (const wchar_t* str, int len)
    {
        static const wchar_t* const words[] =
            { L"end", L"else", L"elsif", L"exception", L"when", L"begin" };

        for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
            if (static_cast<int>(wcslen(words[i])) == len
            && _wcsnicmp(str, words[i], len) == 0)
                return true;

        return false;
    }

    // Stage 3(a): the POSITION column the caret line belongs at once typing completes a dedent
    // token, or -1 to leave it alone. Enter only fixes the NEXT line; this fixes this one (plan §6).
    int EditContext::smartDedentColumn (Position caret) const
    {
        if (GetIndentType() != eiSmart)
            return -1;

        const SmartIndent* smart = LanguageManager::GetSmartIndent(GetSettings().GetLanguage());

        if (!smart)
            return -1;

        int length(0);
        const int wsEnd = leadingWhitespaceEnd(caret.line, length);

        if (wsEnd < 0)
            return -1;

        OEStringW lineBuff;
        GetLineW(caret.line, lineBuff);

        const wchar_t* str = lineBuff.data();

        // Bound the pre-filter at the caret: the `end` of `x := 1; end` is not a line-start dedent.
        // Clamped because CursorBeyondEOL lets the caret sit past the end of the line.
        const int caretInx = min(pos2inx(caret.line, caret.column), length);

        if (caretInx <= wsEnd)
            return -1;      // nothing but indent before the caret

        bool bCandidate(false);

        if (str[wsEnd] == L')' || str[wsEnd] == L']' || str[wsEnd] == L'}')
        {
            // Trigger (b): the closer is the line's first significant token. Quotes are not
            // included - a quote is its own opener, so it cannot start a dedent line here.
            bCandidate = true;
        }
        else
        {
            // Trigger (a): exactly one identifier-like word running right up to the caret. Stopping
            // at anything else is what rejects `end_x` and a second word on the line.
            int i = wsEnd;
            for (; i < caretInx && iswalnum(str[i]); i++);

            bCandidate = (i == caretInx) && isDedentWord(str + wsEnd, i - wsEnd);
        }

        if (!bCandidate)
            return -1;

        // The caret index, not kWholeLine: text to the right of the caret has not been typed
        // yet, and reading it would indent from words the user has not written.
        const int level = smart->DedentLevel(*m_pStorage, caret.line, caretInx);

        if (level < 0)
            return -1;

        return level * GetIndentSpacing();
    }

    // smartDedentColumn's textual test one character early and engine-free: the merge barrier must
    // precede the character. A yes is a PREDICTION - a lookalike costs one undo record, no rewrite.
    bool EditContext::completesDedentToken (wchar_t ch, Position caret) const
    {
        if (GetIndentType() != eiSmart)
            return false;

        // Without an engine for this language nothing can ever dedent, so no barrier is wanted.
        if (!LanguageManager::GetSmartIndent(GetSettings().GetLanguage()))
            return false;

        int length(0);
        const int wsEnd = leadingWhitespaceEnd(caret.line, length);

        if (wsEnd < 0)
            return false;

        const int caretInx = min(pos2inx(caret.line, caret.column), length);

        if (caretInx < wsEnd)
            return false;

        // Trigger (b): the closer would land exactly where the line's first significant
        // token sits. Quotes are not closers here - a quote is its own opener (§3.11).
        if (ch == L')' || ch == L']' || ch == L'}')
            return caretInx == wsEnd;

        if (!iswalpha(ch))
            return false;

        OEStringW lineBuff;
        GetLineW(caret.line, lineBuff);

        const wchar_t* str = lineBuff.data();

        // Trigger (a): everything between the indent and the caret must be ONE word, so that word
        // plus ch is the line's first token - smartDedentColumn's forward test, reversed.
        int i = caretInx;
        for (; i > wsEnd && iswalnum(str[i - 1]); i--);

        if (i != wsEnd)
            return false;

        const int n = caretInx - wsEnd;

        // 'exception' is the longest keyword at 9 characters; a longer prefix cannot match.
        if (n + 1 > 9)
            return false;

        wchar_t word[10];

        for (int k = 0; k < n; k++)
            word[k] = str[wsEnd + k];

        word[n]     = ch;
        word[n + 1] = L'\0';

        return isDedentWord(word, n + 1);
    }

    // Rewrites a line's leading whitespace to `wantColumn` POSITION columns, moving the caret by the
    // same delta. Returns the CHARACTER-COUNT change ShiftAutoPairs needs (§3.5, plan §10 lim. 4).
    int EditContext::setLineIndent (int line, int wantColumn, Position caret)
    {
        int length(0);
        const int wsEnd = leadingWhitespaceEnd(line, length);

        if (wsEnd < 0)
            return 0;

        const int haveColumn = inx2pos(line, wsEnd);

        if (haveColumn == wantColumn)
            return 0;

        if (wsEnd > 0)
            DeleteLinePart(line, 0, haveColumn);

        std::wstring buff;
        space4insertion(buff, 0, wantColumn, !GetTabExpand(), GetTabSpacing());

        if (!buff.empty())
            InsertLinePart(line, 0, buff.c_str(), static_cast<int>(buff.length()));

        Position pos = caret;
        pos.column   = max(0, caret.column - haveColumn + wantColumn);

        // force=true also resets m_orgHrzPos, so the sticky column follows the text; otherwise the
        // next up/down arrow lands where the caret used to be rather than where it is now.
        MoveTo(pos, true);

        return static_cast<int>(buff.length()) - wsEnd;
    }

    // Shift+Tab with no selection: the POSITION column `line` outdents to, or -1 to leave it alone.
    // Pure query, so no undo group opens for a no-op. The -1 cases and snap-down: Features.md §6.
    int EditContext::undentTargetColumn (int line) const
    {
        int length(0);
        const int wsEnd = leadingWhitespaceEnd(line, length);

        // Line not in the buffer, or nothing but whitespace on it: no first token for an indent
        // level to belong to.
        if (wsEnd < 0 || wsEnd >= length)
            return -1;

        const int indentSize = GetIndentSpacing();

        if (indentSize <= 0)
            return -1;

        const int haveColumn = inx2pos(line, wsEnd);

        // Already flush left. Returning 0 here would make the caller open an undo group, write
        // nothing, and leave a Ctrl+Z step that appears to do nothing.
        if (haveColumn <= 0)
            return -1;

        // Snap DOWN to the previous stop - the mirror of DoIndent's (col/size + 1)*size (:146),
        // which is what makes Tab,Shift+Tab a round trip rather than a one-way ratchet.
        return max(0, (haveColumn / indentSize
                       - ((haveColumn % indentSize) ? 0 : 1)) * indentSize);
    }

void EditContext::Insert (wchar_t chr)
{
    _CHECK_ALL_PTR_

    switch (chr)
    {
    case '\r':
        DoCarriageReturn();
        break;
    case '\t':
        DoIndent();
        break;
    default:
        {
            UndoGroup undoGroup(*this);

            expandVirtualSpace(m_curPos.line, m_curPos.column);
            m_pStorage->Insert(chr, m_curPos.line, pos2inx(m_curPos.line, m_curPos.column));
            GoToRight(true);
        }
    }
}

void EditContext::Backspace ()
{
    _CHECK_ALL_PTR_

    int nlines = GetLineCount();
    int length = (m_curPos.line < nlines) ? GetLineLength(m_curPos.line) : 0;

    if (m_curPos.line < nlines && m_curPos.column <= length)
    {
        UndoGroup undoGroup(*this);

        if (!m_curPos.column)
        {
            if (GoToLeft())
            {
                int inx = pos2inx(m_curPos.line, m_curPos.column, true);
                m_pStorage->Delete(m_curPos.line, inx);
            }
            return;
        }

        int inx = pos2inx(m_curPos.line, m_curPos.column);

        wchar_t chr(0);
        try
        {
            chr = m_pStorage->GetCharW(m_curPos.line, inx-1);
            m_pStorage->Delete(m_curPos.line, inx-1);
        }
        catch (std::out_of_range&) {}

        if (chr == '\t')
        {
            m_curPos.column =
            m_orgHrzPos     = inx2pos(m_curPos.line, inx-1);
        }
        else
            GoToLeft();
    }
    else
    {
        if (m_curPos.column > 0)
        {
            int indentSize = GetIndentSpacing();
            int lastIndent = (length / indentSize + 1) * indentSize;

            if (m_curPos.column <= lastIndent)
                GoToEnd();
            else
                DoUndent();
        }
        else
            GoToLeft();
    }
}

void EditContext::Overwrite (wchar_t chr)
{
    _CHECK_ALL_PTR_

    UndoGroup undoGroup(*this);

    switch (chr)
    {
    case '\r':
        if (GoToDown()) GoToStart();
        break;
    case '\t':
        DoIndent();
        break;
    default:
        expandVirtualSpace(m_curPos.line, m_curPos.column + 1);
        m_pStorage->Overwrite(chr, m_curPos.line, pos2inx(m_curPos.line, m_curPos.column));
        GoToRight(true);
        break;
    }
}

void EditContext::Delete ()
{
    _CHECK_ALL_PTR_

    if (m_curPos.line < GetLineCount())
    {
        UndoGroup undoGroup(*this);

        expandVirtualSpace(m_curPos.line, m_curPos.column);
        m_pStorage->Delete(m_curPos.line, pos2inx(m_curPos.line, m_curPos.column));
    }
}

void EditContext::DeleteLine ()
{
    if (m_curPos.line < GetLineCount())
    {
        UndoGroup undoGroup(*this);

        // save pos because it can be changed by notification handler
        Position pos = GetPosition();
        m_pStorage->DeleteLine(pos.line);
        MoveTo(pos);

        AdjustCaretPosition();
        ClearSelection(true);
    }
}

void EditContext::DeleteWordToLeft ()
{
    Square blkPos;
    blkPos.start = m_curPos;
    blkPos.end   = wordLeft(false);

    if (!blkPos.is_empty())
    {
        EBlockMode blkMode = GetBlockMode();
        bool isAltColumnarMode = IsAltColumnarMode();
        SetBlockMode(ebtStream);
        SetSelection(blkPos);
        DeleteBlock(false);
        if (!isAltColumnarMode)
            SetBlockMode(blkMode);
        AdjustCaretPosition();
    }
}

void EditContext::DeleteWordToRight ()
{
    Square blkPos;
    blkPos.start = wordRight(false);
    blkPos.end   = m_curPos;

    if (!blkPos.is_empty())
    {
        EBlockMode blkMode = GetBlockMode();
        bool isAltColumnarMode = IsAltColumnarMode();
        SetBlockMode(ebtStream);
        SetSelection(blkPos);
        DeleteBlock(false);
        if (!isAltColumnarMode)
            SetBlockMode(blkMode);
        AdjustCaretPosition();
    }
}

void EditContext::Undo ()
{
    _CHECK_ALL_PTR_

    UndoContext cxt(*m_pStorage);

    if (m_pStorage->Undo(cxt) && !cxt.m_onlyStorageAffected)
    {
        if (cxt.m_position.line < GetLineCount())
            cxt.m_position.column = inx2pos(cxt.m_position.line, cxt.m_position.column);
        // 26.06.2003 bug fix, selection may still be out of visible text after undo
        MoveToAndCenter(cxt.m_position);

        if (cxt.m_selMode != ebtUndefined)
        {
            if (cxt.m_selection.start.line < GetLineCount())
                cxt.m_selection.start.column = inx2pos(cxt.m_selection.start.line, cxt.m_selection.start.column);
            if (cxt.m_selection.end.line < GetLineCount())
                cxt.m_selection.end.column = inx2pos(cxt.m_selection.end.line, cxt.m_selection.end.column);

            SetBlockMode(cxt.m_selMode,  cxt.m_altColumnarMode);
            
            if (cxt.m_selection.is_empty())
                ClearSelection(true);
            else
                SetSelection(cxt.m_selection);
        }
        else
            ClearSelection(true);

        //if (cxt.m_position.line < GetLineCount())
        //    cxt.m_position.column = inx2pos(cxt.m_position.line, cxt.m_position.column);
        //MoveTo(cxt.m_position);
    }
}

void EditContext::Redo ()
{
    _CHECK_ALL_PTR_

    UndoContext cxt(*m_pStorage);

    if (m_pStorage->Redo(cxt) && !cxt.m_onlyStorageAffected)
    {
        if (cxt.m_selMode != ebtUndefined)
        {
            if (cxt.m_selection.start.line < GetLineCount())
                cxt.m_selection.start.column = inx2pos(cxt.m_selection.start.line, cxt.m_selection.start.column);
            if (cxt.m_selection.end.line < GetLineCount())
                cxt.m_selection.end.column = inx2pos(cxt.m_selection.end.line, cxt.m_selection.end.column);

            SetBlockMode(cxt.m_selMode, cxt.m_altColumnarMode);
            
            if (cxt.m_selection.is_empty())
                ClearSelection(true);
            else
                SetSelection(cxt.m_selection);
            
            SetSelection(cxt.m_selection);
        }
        else
            ClearSelection(true);

        if (cxt.m_position.line < GetLineCount())
            cxt.m_position.column = inx2pos(cxt.m_position.line, cxt.m_position.column);
        MoveTo(cxt.m_position);
    }
}

void EditContext::ClearSelection (bool clearAltColumnarMode /*= false*/)
{
    if (!m_blkPos.is_empty())
    {
        if (m_BlockMode == ebtColumn)
            InvalidateSquare(m_blkPos);
        else
            InvalidateLines(m_blkPos.start.line, m_blkPos.end.line);

        m_blkPos.start = m_blkPos.end;

        if (m_bDirtyPosition)
            adjustCursorPosition();
    }

    if (clearAltColumnarMode
    && m_AltColumnarMode && m_BlockMode == ebtColumn)
    {
        m_BlockMode = ebtStream;
        m_AltColumnarMode = false;
    }
}

void EditContext::ConvertSelectionToLines ()
{
    if (GetBlockMode() != ebtStream)
        SetBlockMode(ebtStream);

    Square pos;
    GetSelection(pos);

    if (!pos.is_empty())
    {
        pos.normalize();

        bool reselect = false;

        if (pos.start.column > 0)
        {
            reselect = true;
            pos.start.column = 0;
        }

        if (pos.end.column > 0 && GetLineLength(pos.end.line))
        {
            reselect = true;
            pos.end.line++;
            pos.end.column = 0;
        }

        if (reselect)
        {
            SetSelection(pos);
            MoveTo(pos.end, true);
        }
    }
}

void EditContext::SetSelection (const Square& blkPos, bool clearAltColumnarMode /*= false*/)
{
    if (m_blkPos != blkPos)
    {
        ClearSelection(clearAltColumnarMode);

        m_blkPos = blkPos;

        if (GetBlockMode() == ebtColumn)
            InvalidateSquare(m_blkPos);
        else
            InvalidateLines(m_blkPos.start.line, m_blkPos.end.line);
    }
}

void EditContext::SelectAll()
{
    if (GetLineCount())
    {
        Square selection;
        selection.start.line   = 0;
        selection.end.line     = GetLineCount() - 1;
        selection.start.column = 0;
        selection.end.column   = GetLineLength(selection.end.line);

        SetBlockMode(ebtStream);
        MoveTo(selection.end);
        SetSelection(selection);

        InvalidateLines(selection.start.line, selection.end.line);
    }
}

void EditContext::SelectLine (int line)
{
    ClearSelection(true);
    if (GetCursorBeyondEOF() || line < GetLineCount())
    {
        Square selection;
        selection.start.line   = line;
        selection.end.line     = line + 1;
        selection.start.column = 0;
        selection.end.column   = 0;

        SetBlockMode(ebtStream);
        MoveTo(selection.end);
        SetSelection(selection);

        InvalidateLines(selection.start.line, selection.end.line);
    }
}

void EditContext::SelectByCursor (Position pos)
{
    if (m_blkPos.is_empty())
        m_blkPos.start = m_blkPos.end = pos;
    else if (m_BlockMode == ebtColumn)
        InvalidateSquare(m_blkPos);

    int old_end_line = m_blkPos.end.line;

    // it's necessary to add checks here
    m_blkPos.end = GetPosition();

    if (m_BlockMode == ebtColumn)
        InvalidateSquare(m_blkPos);
    else
        InvalidateLines(old_end_line, m_blkPos.end.line);
}

void EditContext::GetBlock (std::wstring& str, const Square* sqr) const
{
    _CHECK_ALL_PTR_

    Square blk;

    if (sqr)
        blk = *sqr;
    else
        GetSelection(blk);

    if (!blk.is_empty())
    {
        blk.normalize();

        int nLines = GetLineCount();

        switch (GetBlockMode())
        {
        case ebtStream:
            {
                int reserve = 0;
                for (int i(blk.start.line); i <= blk.end.line; i++)
                    // 03/08/2002 bug fix, block copy is too slow on 10M file
                    // 17/08/2002 bug fix, block copy fails below EOF
                    if (i < nLines) 
                        reserve += m_pStorage->GetLineLengthW(i) + (sizeof("\r\n")-1);
                    else
                        break;

                str.reserve(reserve);

                if (blk.start.line == blk.end.line)
                {
                    line2buff(blk.start.line, blk.start.column, blk.end.column, str);
                }
                else
                {
                    line2buff(blk.start.line, blk.start.column, INT_MAX, str);
                    for (int i(blk.start.line + 1); i <= blk.end.line - 1; i++)
                    {
                        str += L"\r\n";
                        if (i < nLines)
                        {
                            OEStringW lineBuff;
                            GetLineW(i, lineBuff);
                            str.append(lineBuff.data(), lineBuff.length());
                        }
                    }
                    str += L"\r\n";
                    line2buff(blk.end.line, 0, blk.end.column, str);
                }
                TRACE("Copy Block reserved = %d, actual length = %d\n",  reserve, str.length());

                if ((blk.end.line < nLines
                  && blk.end.column > GetLineLength(blk.end.line))
                || (blk.end.line >= nLines
                  && blk.end.column > 0))
                        str += L"\r\n";
            }
            break;
        case ebtColumn:
            {
                blk.normalize_columns();

                str.reserve((blk.end.line - blk.start.line)
                           *(blk.end.column - blk.start.column));

                for (int line = blk.start.line; line <= blk.end.line; ++line)
                {
                    if (line > blk.start.line)
                        str += L"\r\n";

                    line2buff(line, blk.start.column, blk.end.column, str, true);
                }
            }
            break;
        }
    }
}

void EditContext::InsertBlock (const wchar_t* str)
{
    InsertBlock(str, !GetBlockKeepMarking());
}

void EditContext::InsertBlock (const wchar_t* str, bool hideSelection, bool putSelInUndo)
{
    _CHECK_ALL_PTR_

    Position pos = GetPosition();
    UndoGroup undoGroup(*this);
    PushInUndoStack(m_curPos);

    Square sel;
    GetSelection(sel);
    if (putSelInUndo)
        PushInUndoStack(GetBlockMode(), IsAltColumnarMode(), sel);

    switch (GetBlockMode())
    {
    case ebtStream:
        {
            std::wstring buff;
            int position = 0;
            bool cr;

            if (getLine(str, position, buff, cr))
            {
                if (cr) 
                    SplitLine(m_curPos.line, m_curPos.column); // multiline selection

                InsertLinePart(m_curPos.line, m_curPos.column, buff.c_str(), buff.length());

                if (cr) // multiline selection
                {
                    StringArrayW lines(1024, 16 * 1024);

                    m_curPos.column = 0;
                    while (getLine(str, position, buff, cr))
                    {
                        if (cr)
                            lines.append().assign(buff.c_str(), buff.length(), true);
                        else
                        {
                            InsertLinePart(m_curPos.line+1, 0, buff.c_str(), buff.length());
                            m_curPos.column = inx2pos(buff.c_str(), buff.size(), buff.size());
                        }
                    }

                    if (lines.size())
                        m_pStorage->InsertLines(m_curPos.line+1, lines);

                    m_curPos.line += lines.size() + 1;
                }
                else
                    m_curPos.column = inx2pos(m_curPos.line, pos2inx(m_curPos.line, m_curPos.column) + buff.size());

                MoveTo(m_curPos);
            }

            sel.start = pos;
            sel.end   = m_curPos;
            SetSelection(sel);
        }
        break;
    case ebtColumn:
        {
            // fill 0-with selection with a single line text
            if (str && wcslen(str) && !wcschr(str, '\n') 
            && sel.start.line != sel.end.line
            && sel.start.column == sel.end.column)
            {
                Square orgSel = sel;

                UntabifyForColumnarOperation(sel);

                sel.normalize();
                sel.normalize_columns();

                int length = wcslen(str);

                Storage::NotificationDisabler disabler(m_pStorage);

                for (int line = sel.start.line; line <= sel.end.line; ++line)
                    InsertLinePart(line, sel.start.column, str, length);
                
                disabler.Enable();

                // move to the right size of the inserted text
                pos.column = inx2pos(pos.line, pos2inx(pos.line, pos.column) + length);
                MoveTo(pos, true);
                hideSelection = false;
                // restore 0-width selection
                sel = orgSel;
                sel.start.column = pos.column;
                sel.end.column = pos.column;
                SetSelection(sel);
            }
            else
            {
                // 2014.02.04 bug fix, paste on multi-line columnar selection uses the cursor position 
                //                     instead of the beginning of selection insert at the begging of selection
                // 2014.02.06 bug fix, revisited
                sel.normalize();
                if (m_curPos == sel.end)
                {
                    pos = sel.start;
                    MoveTo(pos, true);
                }

                {
                    Square sel2;
                    sel2.start = sel2.end = m_curPos;
                    for (const wchar_t* ptr = str; *ptr; ++ptr)
                        if (*ptr == '\n') sel2.end.line++;

                    UntabifyForColumnarOperation(sel2);
                }

                Storage::NotificationDisabler disabler(m_pStorage);

                wstring buff;
                bool cr;
                int width = 0;
                int tabSize = GetTabSpacing();
                //calculate block width
                {
                    int position = 0;
                    while (getLine(str, position, buff, cr))
                    {
                        untabify(buff.c_str(), buff.length(), buff, 0, tabSize, false);
                        width = max<int>(width, buff.size());
                    }
                }

                int lastLineLen = 0;
                int line = m_curPos.line;
                int position = 0;
                {
                    while (getLine(str, position, buff, cr))
                    {
                        untabify(buff.c_str(), buff.length(), buff, 0, tabSize, false);
                        buff.resize(width, ' ');
                        InsertLinePart(line, m_curPos.column, buff.c_str(), buff.length());
                        line++;
                        lastLineLen = buff.size();
                    }
                }

                disabler.Enable();

                pos.line  += line - m_curPos.line - 1;
                pos.column = inx2pos(pos.line, pos2inx(pos.line, m_curPos.column) + lastLineLen);
                sel.start = m_curPos;
                sel.end   = pos;
                SetSelection(sel);
                if (GetColBlockCursorToStartAfterPaste())
                    MoveTo(pos);
            }
        }
        break;
    }

    if (putSelInUndo)
    {
        GetSelection(sel);
        PushInUndoStack(GetBlockMode(), IsAltColumnarMode(), sel);
    }

    if (hideSelection)
        ClearSelection(true);

    AdjustCaretPosition();
    PushInUndoStack(m_curPos);
}

void EditContext::DeleteBlock (bool putSelInUndo)
{
    _CHECK_ALL_PTR_

    Square sel;
    GetSelection(sel);
    sel.normalize();
    int nLines = GetLineCount();

    if (sel.start.line < nLines 
    && (sel.start.line != nLines-1 || sel.start.column < GetLineLength(nLines-1))
    && !sel.is_empty())
    {
        UndoGroup undoGroup(*this);
        PushInUndoStack(m_curPos);
        if (putSelInUndo) 
            PushInUndoStack(GetBlockMode(), IsAltColumnarMode(), sel);

        switch (GetBlockMode())
        {
        case ebtStream:
            {
                // for single-line selection
                if (sel.start.line == sel.end.line)
                {
                    DeleteLinePart(sel.start.line, sel.start.column, sel.end.column);
                }
                else if (sel.start.line < nLines) // 22/07/2002 bug fix, stream block delete fails below EOF
                {
                    // delete selection, skip first and last lines
                    if (sel.end.line-sel.start.line > 1)
                        m_pStorage->DeleteLines(sel.start.line + 1, 
                            sel.end.line - sel.start.line - 1);

                    DeleteLinePart(sel.start.line, sel.start.column, OEStringA::maxlen);
                    expandVirtualSpace(sel.start.line, sel.start.column);
                    DeleteLinePart(sel.start.line + 1, 0, sel.end.column);
                    m_pStorage->MergeLines(sel.start.line);
                }

                MoveTo(sel.start);
            }
            ClearSelection();
            break;
        case ebtColumn:
            {
                sel.normalize_columns();

                UntabifyForColumnarOperation(sel);

                Storage::NotificationDisabler disabler(m_pStorage);

                bool deleteSpace = GetColBlockDeleteSpaceAfterMove();
                std::wstring space;

                if (!deleteSpace)
                    space.resize(sel.end.column > sel.start.column
                                 ? sel.end.column - sel.start.column
                                 : sel.start.column - sel.end.column, ' ');

                for (int i(sel.start.line); i <= sel.end.line && i < nLines; i++)
                {
                    DeleteLinePart(i, sel.start.column, sel.end.column);

                    if (!deleteSpace)
                        InsertLinePart(i, sel.start.column, space.c_str(), space.length());
                }

                disabler.Enable();

                Position pos;
                pos.line = m_curPos.line;
                pos.column = sel.start.column;
                MoveTo(pos);
            }
            if (GetColBlockEditMode())
            {
                sel.end.column = sel.start.column;
                SetSelection(sel);
            }
            else
                ClearSelection();
            break;
        }

        AdjustCaretPosition();
        
        if (putSelInUndo)
        {
            GetSelection(sel); // save empty selection in undo stack
            PushInUndoStack(GetBlockMode(), IsAltColumnarMode(), sel);
        }
        PushInUndoStack(m_curPos);
    }
    else if (!sel.is_empty())
    {
        if (GetBlockMode() != ebtColumn || !GetColBlockEditMode())
        {
            ClearSelection();
            MoveTo(sel.start);
        }
    }
}

void EditContext::UntabifyForColumnarOperation (Square blk)
{
    Square orgBlkPos;
    GetSelection(orgBlkPos);

    blk.normalize();
    blk.normalize_columns();
    int tabSpacing = GetTabSpacing();
    blk.start.column = (blk.start.column / tabSpacing) * tabSpacing;
    blk.end.column = (blk.end.column / tabSpacing) * tabSpacing + tabSpacing;
    SetSelection(blk);
    ScanAndReplaceText(UntabifyText, false);

    SetSelection(orgBlkPos);
}

void EditContext::ColumnarInsert (wchar_t ch)
{
    ASSERT(GetBlockMode() == ebtColumn);

    Square blk;
    GetSelection(blk);
    blk.normalize();
    blk.normalize_columns();

    // Stage 6 B8/B9: everything below rewrites the Square's WHOLE row range -
    // UntabifyForColumnarOperation scans it, then DeleteBlock / InsertBlock write it
    // - so a row a collapsed fold hides would be space-padded and deleted without
    // the user ever seeing it. A zero-width block never reaches DeleteBlock at all
    // and goes straight to InsertBlock, so the delete funnel does not cover this
    // path either.
    //
    // RevealLine is the virtual EditContext hook (OEContext.h:193): a no-op for any
    // EditContext that is not a folding view, fold-aware in COEditorView. Gated on
    // ColInsertRevealLine so the behaviour can be turned off.
    if (GetColInsertRevealLine())
    {
        // RevealLine rejects out-of-range values, so blk.end.line being the
        // one-past-EOF line under CursorBeyondEOF needs no clamp here.
        for (int line = blk.start.line; line <= blk.end.line; line++)
            RevealLine(line);

        // Re-read the geometry: RevealLine runs ApplyFoldChange, which remaps the
        // caret and both selection corners. By the Stage 6 invariant documented at
        // OEView_2.cpp:1075-1082 that remap is a no-op here - every corner already
        // sits on a visible line - but re-reading costs nothing and keeps this
        // exactly equivalent to revealing before the Square was captured at all.
        GetSelection(blk);
        blk.normalize();
        blk.normalize_columns();
    }

    Position pos = GetPosition();

    int width = blk.end.column - blk.start.column;
    wstring text, line(width ? width : 1, ch);

    line += L"\r\n";
    for (int i = 0; i < blk.end.line - blk.start.line + 1; i++)
        text.append(line);

    UndoGroup undoGroup(*this);
    PushInUndoStack(ebtColumn, IsAltColumnarMode(), blk);
    PushInUndoStack(pos);

    UntabifyForColumnarOperation(blk);
    if (width) DeleteBlock(false);
    MoveTo(blk.start, true/*force*/);
    InsertBlock(text.c_str(), false, width ? true : false);

    if (!width) 
    {
        pos.column++;
        blk.start.column++, blk.end.column++;
        SetSelection(blk);
        PushInUndoStack(ebtColumn, IsAltColumnarMode(), blk);
    }

    MoveTo(pos);
    PushInUndoStack(pos);
}

// Spec "Editor Auto-closing and Auto-surround" §2.2 / §3.4 (decision D6): wrap every line of a
// non-zero-width column selection in its own opener/closer pair, as one undo group.
//
// The view layer decides *whether* to surround - §3.11 pair table, the AutoSurround* settings and
// the §3.2 in-string test all live there (COEditorView::AutoSurroundColumnSelection). This is the
// mechanics, and it has to live here because InsertLinePart is private to EditContext
// (OEContext.h:291-295).
//
// Same shape as the neighbouring columnar edits: normalize, one UndoGroup bracketed by
// before/after selection+cursor records (ColumnarIndent :1097-1099 and :1117-1118), untabify the
// edges, then a single batched per-line pass under a NotificationDisabler - which is what both the
// zero-width branch of InsertBlock (:747-752) and DeleteBlock's column branch (:892-910) do.
//
// Not EditContext::Insert(wchar_t) in a loop, although that would function correctly: it would
// raise 2N storage notifications and move the caret 2N times for what this does in one pass. Not
// InsertBlock either - its column branch either requires a zero-width selection (:734-736) or
// *replaces* the block contents (:764 onwards), and neither of those is a surround.
void EditContext::ColumnarSurround (wchar_t opener, wchar_t closer)
{
    ASSERT(GetBlockMode() == ebtColumn);

    Square blk;
    GetSelection(blk);
    blk.normalize();
    blk.normalize_columns();

    // The caller rejects zero-width blocks: they carry no text to surround, and typing into one
    // means "put this character on every line" (ColumnarInsert's !width branch, :988-994).
    ASSERT(blk.end.column > blk.start.column);
    if (blk.end.column <= blk.start.column)
        return;

    Position pos = GetPosition();
    const int nlines = GetLineCount();

    UndoGroup undoGroup(*this);
    PushInUndoStack(ebtColumn, IsAltColumnarMode(), blk);
    PushInUndoStack(pos);

    // Tabs spanning either edge become spaces first, so blk.start.column / blk.end.column pick out
    // one unambiguous character position on every affected line instead of a spot inside a tab
    // field. Takes Square by value and widens it to whole tab stops (:954-955), so blk itself is
    // left untouched and still means what it did on entry.
    UntabifyForColumnarOperation(blk);

    {
        Storage::NotificationDisabler disabler(m_pStorage);

        const wchar_t openerStr[2] = { opener, 0 };
        const wchar_t closerStr[2] = { closer, 0 };

        for (int line = blk.start.line; line <= blk.end.line && line < nlines; line++)
        {
            // Right edge before left edge. InsertLinePart takes display columns and converts with
            // pos2inx (:119), but only after expandVirtualSpace (:103-114) has materialized the
            // cells the line is short - the common case, since a column block may run past
            // end-of-line. Writing the closer first leaves the opener's column valid, so neither
            // call needs an offset applied to it.
            //
            // That order also makes UndoInsert::Append unreachable. Merging needs
            // m_position.column + len == col (OEUndo.cpp:397-408), i.e. the second insert sitting
            // immediately after the first, and blk.start.column < blk.end.column rules that out.
            // A merged record would claim a 2-character insert where the buffer really gained
            // 1 + width + 1, and undoing it would eat the selected text along with the pair.
            InsertLinePart(line, blk.end.column,   closerStr, 1);
            InsertLinePart(line, blk.start.column, openerStr, 1);
        }

        disabler.Enable();
    }

    // Every affected line gained exactly one column on its left, so the whole block - and the
    // caret inside it - moves right by one, leaving the originally selected text still selected.
    // SetSelection assigns m_blkPos verbatim and never touches m_curPos (:515-528), and MoveTo
    // never touches m_blkPos (OEContext_2.cpp:302-340), so both are needed and this is the order
    // ColumnarIndent uses (:1113-1115).
    blk.start.column++;
    blk.end.column++;
    SetSelection(blk);

    pos.column++;
    MoveTo(pos);

    PushInUndoStack(ebtColumn, IsAltColumnarMode(), blk);
    PushInUndoStack(pos);
}

void EditContext::ColumnarIndent ()
{
    ASSERT(GetBlockMode() == ebtColumn);

    Square blk;
    GetSelection(blk);
    blk.normalize();
    blk.normalize_columns();
    Position pos = GetPosition();

    UndoGroup undoGroup(*this);
    PushInUndoStack(ebtColumn, IsAltColumnarMode(), blk);
    PushInUndoStack(pos);

    int nlines = GetLineCount();
    for (int line = blk.start.line; line <= blk.end.line && line < nlines; line++)
    {
        Position pos2; 
        pos2.line = line;
        pos2.column = blk.start.column;
        MoveTo(pos2);
        DoIndent(false);
    }
    int offset = GetPosition().column - blk.start.column;
    blk.end.column += offset;
    blk.start.column += offset;
    SetSelection(blk);
    pos.column += offset;
    MoveTo(pos);

    PushInUndoStack(ebtColumn, IsAltColumnarMode(), blk);
    PushInUndoStack(pos);
}

void EditContext::ColumnarUndent ()
{
    ASSERT(GetBlockMode() == ebtColumn);

    Square blk;
    GetSelection(blk);
    blk.normalize();
    blk.normalize_columns();

    int indentSize = GetIndentSpacing();
    int newIndent = max(0, 
        (blk.start.column / indentSize - ((blk.start.column % indentSize) ? 0 : 1)) * indentSize);

    if (newIndent == blk.start.column) return;

    int nlines = GetLineCount();
    for (int line = blk.start.line; line <= blk.end.line && line < nlines; line++)
    {
        OEStringW lineBuff;
        GetLineW(line, lineBuff);
        int len = lineBuff.length();
        const wchar_t* str = lineBuff.data();

        int indentInx = pos2inx(str, len, blk.start.column, true);
        int newIndentInx = pos2inx(str, len, newIndent, true);

        if (newIndentInx < len)
        {
            int i = min(indentInx-1, len-1);
            for (; newIndentInx <= i && iswspace(str[i]); i--) {}
            newIndent = max<int>(newIndent, inx2pos(str, len, i + 1));
        }
    }

    if (newIndent == blk.start.column) return;

    UndoGroup undoGroup(*this);
    PushInUndoStack(ebtColumn, IsAltColumnarMode(), blk);
    PushInUndoStack(GetPosition());

    for (int line = blk.start.line; line <= blk.end.line; line++)
        DeleteLinePart(line, newIndent, blk.start.column);

    int offset = blk.start.column - newIndent;
    Position pos = GetPosition();
    pos.column -= offset;
    MoveTo(pos);

    blk.end.column -= offset;
    blk.start.column = newIndent;
    SetSelection(blk);

    PushInUndoStack(ebtColumn, IsAltColumnarMode(), blk);
    PushInUndoStack(GetPosition());
}

void EditContext::ColumnarBackspace ()
{
    ASSERT(GetBlockMode() == ebtColumn);

    Square blk;
    GetSelection(blk);
    blk.normalize();
    blk.normalize_columns();

    if (blk.end.column == blk.start.column)
    {
        if (blk.start.column > 0)
        {
            UndoGroup undoGroup(*this);
            PushInUndoStack(ebtColumn, IsAltColumnarMode(), blk);
            PushInUndoStack(GetPosition());
            blk.start.column--;
            SetSelection(blk);
            DeleteBlock(false);
            blk.end.column--;
            SetSelection(blk);
            PushInUndoStack(ebtColumn, IsAltColumnarMode(), blk);
        }
    }
    else
        DeleteBlock();
}

void EditContext::ColumnarDelete ()
{
    ASSERT(GetBlockMode() == ebtColumn);

    Square blk;
    GetSelection(blk);
    blk.normalize();
    blk.normalize_columns();

    if (blk.end.column == blk.start.column)
    {
        UndoGroup undoGroup(*this);
        PushInUndoStack(ebtColumn, IsAltColumnarMode(), blk);
        PushInUndoStack(GetPosition());
        blk.end.column++;
        SetSelection(blk);
        DeleteBlock(false);
        blk.end.column--;
        SetSelection(blk);
        PushInUndoStack(ebtColumn, IsAltColumnarMode(), blk);
    }
    else
        DeleteBlock();
}

void EditContext::IndentBlock ()
{
    _CHECK_ALL_PTR_

    Square sel;
    GetSelection(sel);
    bool selectionSwapped = sel.normalize(); // 14/08/2002 bug fix, indent/undent block does not work if selection is not normalized
    bool selectionChanged = false;

    if (!sel.is_empty())
    {
        UndoGroup undoGroup(*this);
        PushInUndoStack(m_curPos);
        PushInUndoStack(GetBlockMode(), IsAltColumnarMode(), sel);

        int nLines = GetLineCount();
        int indentSize = GetIndentSpacing();
        int lineLimit = min(nLines, !sel.end.column ? sel.end.line : sel.end.line + 1);

        Storage::NotificationDisabler disabler(m_pStorage);
        
        // 24.10.2004 improvement, indent and undent align each line individually to the neatest indent level
        for (int line = sel.start.line; line < lineLimit; line++)
        {
            OEStringW lineBuff;
            GetLineW(line, lineBuff);
            int len = lineBuff.length();
            const wchar_t* str = lineBuff.data();

            int indentLevel = -1;

            for (int i = 0; i < len; i++)
                if (!iswspace(str[i])) 
                {
                    indentLevel = i;
                    break;
                }

            if (indentLevel != -1)
            {
                indentLevel = inx2pos(str, len, indentLevel);
                int newIndentLevel = (indentLevel / indentSize + 1) * indentSize;

                std::wstring buff;
                space4insertion (buff, indentLevel, newIndentLevel, !GetTabExpand(), GetTabSpacing());
                InsertLinePart(line, indentLevel, buff.c_str(), buff.length());

                // 2011.09.18 improvement, block indent keeps a selection anchored to a text
                // Two `if`s, NOT `else if`: a single-line selection has both endpoints on this line.
                if (line == sel.start.line)
                {
                    if (sel.start.column >= indentLevel)
                    {
                        sel.start.column += newIndentLevel - indentLevel;
                        selectionChanged = true;
                    }
                }

                if (line == sel.end.line)
                {
                    if (sel.end.column >= indentLevel)
                    {
                        sel.end.column += newIndentLevel - indentLevel;
                        selectionChanged = true;
                    }
                }
            }
        }

        if (selectionSwapped)
        {
            sel.swap();
        }

        if (selectionChanged)
        {
            SetSelection(sel);
            MoveTo(sel.end, true);
        }

        disabler.Enable();
        PushInUndoStack(m_curPos);
    }
}

void EditContext::UndentBlock ()
{
    _CHECK_ALL_PTR_

    Square sel;
    GetSelection(sel);
    bool selectionSwapped = sel.normalize(); // 14/08/2002 bug fix, indent/undent block does not work if selection is not normalized
    bool selectionChanged = false;

    if (!sel.is_empty())
    {
        UndoGroup undoGroup(*this);
        PushInUndoStack(m_curPos);
        PushInUndoStack(GetBlockMode(), IsAltColumnarMode(), sel);

        int nLines = GetLineCount();
        int tabSize = GetTabSpacing();
        int indentSize = GetIndentSpacing();
        int lineLimit = min(nLines, !sel.end.column ? sel.end.line : sel.end.line + 1);

        Storage::NotificationDisabler disabler(m_pStorage);

        // 24.10.2004 improvement, indent and undent align each line individually to the neatest indent level
        for (int line = sel.start.line; line < lineLimit; line++)
        {
            OEStringW lineBuff;
            GetLineW(line, lineBuff);
            int len = lineBuff.length();
            const wchar_t* str = lineBuff.data();

            int indentLevel = -1;

            for (int i = 0; i < len; i++)
                if (!iswspace(str[i])) 
                {
                    indentLevel = i;
                    break;
                }

            if (indentLevel > 0)
            {
                indentLevel = inx2pos(str, len, indentLevel);
                int newIndentLevel = max(0, 
                    (indentLevel / indentSize - ((indentLevel  % indentSize) ? 0 : 1)) * indentSize);

                int newTabLevel = (newIndentLevel / tabSize) * tabSize;

                // 2011.09.18 bug fix, block undent does not work correctly if tab size > indent size
                if (tabSize <= indentSize || newTabLevel == newIndentLevel)
                {
                    DeleteLinePart(line, newIndentLevel, indentLevel);
                }
                else
                {
                    ASSERT(newIndentLevel - newTabLevel > 0);
                    std::wstring padding(max(0, newIndentLevel - newTabLevel), ' ');

                    m_pStorage->ReplaceLinePart(line, 
                        !newTabLevel ? 0 : pos2inx(line, newTabLevel), 
                        !indentLevel ? 0 : pos2inx(line, indentLevel), 
                        padding.c_str(), padding.length()
                    );
                }

                // 2011.09.18 improvement, block indent keeps a selection anchored to a text
                // Two `if`s, NOT `else if` - see IndentBlock. Shift+Tab makes single-line the norm.
                if (line == sel.start.line)
                {
                    if (sel.start.column >= indentLevel)
                    {
                        sel.start.column += newIndentLevel - indentLevel;
                        selectionChanged = true;
                        ASSERT(sel.start.column >= 0);
                        if (sel.start.column < 0) 
                            sel.start.column = 0;
                    }
                }

                if (line == sel.end.line)
                {
                    if (sel.end.column >= indentLevel)
                    {
                        sel.end.column += newIndentLevel - indentLevel;
                        selectionChanged = true;
                        ASSERT(sel.end.column >= 0);
                        if (sel.end.column < 0) 
                            sel.end.column = 0;
                    }
                }
            }
        }

        if (selectionSwapped)
        {
            sel.swap();
        }

        if (selectionChanged)
        {
            SetSelection(sel);
            MoveTo(sel.end, true);
        }

        disabler.Enable();
        PushInUndoStack(m_curPos);
    }
}


    struct LineRef 
    {
        int seq;
        std::wstring str;
        LineRef () : seq(0) {}
    };

    class SortFunctor
    {
        const SortCtx& m_ctx;
    public:

        SortFunctor (const SortCtx& ctx) : m_ctx(ctx) {}

        bool operator () (const LineRef& e1, const LineRef& e2) const
        {
            bool res = least(e1, e2);
            return m_ctx.mKeyOrder == 0 ? res : !res;
        }

        bool least (const LineRef& e1, const LineRef& e2) const
        {
            int minlen = min(e1.str.length(), e2.str.length());

            if (!minlen)
            {
                if (e1.str.length() == e2.str.length())
                    return e1.seq < e2.seq ? true : false;

                return e1.str.length() < e2.str.length() ? true : false;
            }

            int res = m_ctx.mIgnoreCase 
                ? wcsnicmp(e1.str.c_str(), e2.str.c_str(), minlen)
                : wcsncmp(e1.str.c_str(), e2.str.c_str(), minlen);
            
            if (!res)
            {
                if (e1.str.length() == e2.str.length())
                    return e1.seq < e2.seq ? true : false;

                return e1.str.length() < e2.str.length() ? true : false;
            }

            return res < 0 ? true : false;
        }
    };

void EditContext::Sort (const SortCtx& ctx)
{
    _CHECK_ALL_PTR_

    Square sel;
    GetSelection(sel);
    
    UndoGroup undoGroup(*this);
    PushInUndoStack(m_curPos);
    PushInUndoStack(GetBlockMode(), IsAltColumnarMode(), sel);

    if (sel.is_empty())
    {
        sel.start.column = 
        sel.end.column = 
        sel.start.line = 0;
        sel.end.line = GetLineCount();
        SetSelection(sel);
    }

    ConvertSelectionToLines();
    GetSelection(sel);
    sel.normalize();
    MoveTo(sel.start);

    int nLines = GetLineCount();
    int start  = min(sel.start.line, nLines);
    int end    = min(sel.end.line, nLines);
    int keypos = ctx.mKeyStartColumn -1;
    int keylen = !ctx.mKeyLength ? INT_MAX : ctx.mKeyLength;
    int tabSpacing = GetTabSpacing();

    vector<LineRef> lines(end - start);

    for (unsigned long i = 0; i < lines.size(); i++)
    {
        OEStringW lineBuff;
        GetLineW(i + start, lineBuff);
        int len = lineBuff.length();
        const wchar_t* str = lineBuff.data();

        lines.at(i).seq = i + start;

        if (keypos < len)
        {
            if (wmemchr(lineBuff.data(), L'\t', len) == nullptr)
                lines[i].str.assign(str + keypos, min(len - keypos, keylen));
            else
            {
                wstring buff;
                untabify(str, len, buff, 0, tabSpacing, false);
                lines[i].str.assign(buff.c_str() + keypos, min((int)buff.length() - keypos, keylen));
            }
        }
    }

    Global::SetStatusText("Sorting...");
    sort(lines.begin(), lines.end(), SortFunctor(ctx));

    vector<int> order(lines.size());

    vector<LineRef>::const_iterator it = lines.begin();
    for (int i = 0; it != lines.end(); ++it, i++)
        order.at(i) = it->seq;

    Global::SetStatusText("Moving...");
    m_pStorage->Reorder(start, order);

    int removedCounter = 0;

    if (ctx.mRemoveDuplicates)
    {
        Global::SetStatusText("Removing duplicates (it may take more time than sorting)...");
        Storage::NotificationDisabler disabler(m_pStorage);

        OEStringW prevStr;
        bool firstCycle = true;

        for (int i = start; i < end; i++)
        {
            OEStringW curStr;
            GetLineW(i, curStr);


            if (!firstCycle 
            && prevStr.length() == curStr.length() 
            && (!prevStr.length() || !wcsncmp(prevStr.data(), curStr.data(), prevStr.length())))
            {
                m_pStorage->DeleteLine(i--);
                removedCounter++;
                end--;
            }
            else 
            // 30.03.2005 bug fix, sorting does not completely remove duplicate rows if a number of dups is even
            {
                prevStr = curStr;
                firstCycle = false;
            }
        }

        if (removedCounter)
        {
            sel.end.line -= removedCounter;
            SetSelection(sel);
            disabler.Enable();
            m_pStorage->Notify_ChangedLinesQuantity(sel.start.line, -removedCounter);
        }
    }

    PushInUndoStack(m_curPos);
    PushInUndoStack(GetBlockMode(), IsAltColumnarMode(), sel);

    ostringstream o;
    o << lines.size() << " line(s) sorted";
    
    if (removedCounter) 
        o << ", " << removedCounter << " duplicated line(s) removed." << ends;
    else
        o << '.' << ends;

    Global::SetStatusText(o.str().c_str());
}

void EditContext::CopyBookmarkedLines (std::wstring& buff) const
{
    _CHECK_ALL_PTR_

    vector<int> lines;
    int reserve = 0;
    
    m_pStorage->GetBookmarkedLines(lines, eBmkGroup1);

    vector<int>::const_iterator it = lines.begin();
    for (; it != lines.end(); ++it)
        reserve += m_pStorage->GetLineLengthW(*it) + (sizeof("\r\n")-1);
    
    if (reserve)
    {
        buff.reserve(reserve);
        it = lines.begin();
        for (; it != lines.end(); ++it)
        {
            OEStringW lineBuff;
            GetLineW(*it, lineBuff);
            buff.append(lineBuff.data(), lineBuff.length());
            buff += L"\r\n";
        }
    }
}

void EditContext::DeleteBookmarkedLines ()
{
    _CHECK_ALL_PTR_

    vector<int> lines;
    m_pStorage->GetBookmarkedLines(lines, eBmkGroup1);
    
    UndoGroup undoGroup(*this);
    Storage::NotificationDisabler disabler(m_pStorage);

    vector<int>::reverse_iterator it = lines.rbegin();
    for (; it != lines.rend(); ++it)
        m_pStorage->DeleteLine(*it);
    
    if (lines.size())
    {
        disabler.Enable();
        // Undo/Redo operation does not support this notification :(
        m_pStorage->Notify_ChangedLinesQuantity(lines[0], -(int)lines.size()); // 16.03.2003 bug fix, scroll position fails on delete boolmark lines operation
        AdjustCaretPosition();
        ClearSelection(true);
    }
}

void EditContext::RemoveBlankLines (bool excessiveOnly)
{
    _CHECK_ALL_PTR_

    vector<int> lines;

    Square blk;
    GetSelection(blk);
    blk.normalize();

    if (blk.start.column > 0) 
        blk.start.line++;

    if (blk.end.line > GetLineCount()-1) 
        blk.end.line = GetLineCount()-1;

    bool is_prev_line_blank = false;
    for (int i = blk.start.line; i < blk.end.line; i++)
    {
        OEStringW lineBuff;
        GetLineW(i, lineBuff);

        bool is_line_blank = Common::is_blank_line(lineBuff.data(), lineBuff.length());

        if (is_line_blank && (is_prev_line_blank || !excessiveOnly))
            lines.push_back(i);

        is_prev_line_blank = is_line_blank;
    }

    GetSelection(blk);

    if (!lines.empty())
    {
        UndoGroup undoGroup(*this);
        PushInUndoStack(m_curPos);
        PushInUndoStack(GetBlockMode(), IsAltColumnarMode(), blk);

        Storage::NotificationDisabler disabler(m_pStorage);

        vector<int>::reverse_iterator it = lines.rbegin();
        for (; it != lines.rend(); ++it)
            m_pStorage->DeleteLine(*it);
        
        if (lines.size())
        {
            GetSelection(blk);

            disabler.Enable();
            m_pStorage->Notify_ChangedLinesQuantity(lines[0], -(int)lines.size()); 

            if (blk.start.line < blk.end.line)
                blk.end.line -= lines.size();
            else
                blk.start.line -= lines.size();

            SetSelection(blk);
            MoveTo(blk.end, true);

            PushInUndoStack(m_curPos);
            PushInUndoStack(GetBlockMode(), IsAltColumnarMode(), blk);
        }
    }
}

int EditContext::AlignCodeFragment (const wchar_t* str, std::wstring& text)
{
    if (GetBlockMode() == ebtStream)
    {
        int indent_size = 0;
        bool empty_line = true;
        
        if (GetPosition().line < GetLineCount())
        {
            // calculate the indent of the current line
            OEStringW lineBuff;
            GetLineW(GetPosition().line, lineBuff);
            int len = lineBuff.length();
            const wchar_t* curLineStr = lineBuff.data();

            if (len > 0) empty_line = false;

            int i = 0;
            for (; i < len; ++i)
                if (!iswspace(curLineStr[i]))
                    break;
            indent_size = inx2pos(curLineStr, len, i);
        }

        // align by the current indent or drop position
        if (empty_line)
            indent_size = GetPosition().column;
        else
            indent_size = min(indent_size, GetPosition().column);

        std::wstring line, buffer, indent(indent_size, ' ');
        indent.insert(indent.begin(), '\n');

        bool cr;
        int position = 0;
        if (getLine(str, position, line, cr))
            buffer += line;

        while (getLine(str, position, line, cr))
        {
            if (!line.empty())
                buffer += indent + line;
            else if (cr)
                buffer += '\n';
        }

        text = buffer;

        return indent_size;
    }
    return 0;
}

bool EditContext::ExpandTemplate (OpenEditor::TemplatePtr tmpl, int index)
{
    _CHECK_ALL_PTR_

    Square sqr;
    std::wstring buff;
    if (index != -1 || GetBlockOrWordUnderCursor(buff, sqr, false))
    {
        Position pos;
        string textA;

        bool expand = false;
        
        if (index != -1)
        {
            expand = tmpl->ExpandKeyword(index, textA, pos);
            GetSelection(sqr);
        }
        else
        {
            expand = tmpl->ExpandKeyword(Common::str(buff), textA, pos);
        }

        if (expand)
        {
            EBlockMode orgBlockMode = GetBlockMode();
            if (orgBlockMode != ebtStream)
                SetBlockMode(ebtStream);

            UndoGroup undoGroup(*this);
            SetSelection(sqr);
            DeleteBlock(false);

            std::wstring textW;
            int indent_size = AlignCodeFragment(Common::wstr(textA).c_str(), textW);

            InsertBlock(textW.c_str(), false, false);

            if (!GetTabExpand()) 
                ScanAndReplaceText(TabifyLeadingSpaces, false);

            SetBlockMode(orgBlockMode);
            ClearSelection(true);

            pos.line += sqr.start.line;
            if (pos.line == sqr.start.line)
                pos.column += sqr.start.column;
            else
                pos.column += indent_size;

            MoveTo(pos);
            PushInUndoStack(pos);

            //GetSelection(sqr);
            //PushInUndoStack(orgBlockMode, sqr);

            return true;
        }
    }
    return false;
}


bool EditContext::GetMatchInfo (Position pos, LanguageSupport::Match & match)
{
    _CHECK_ALL_PTR_

    bool retVal = false;

    LanguageSupportPtr ls = m_pStorage->GetLanguageSupport();

    if (ls.get())
    {
        int line = pos.line, 
            offset = pos2inx(pos.line, pos.column);

        if (ls->FindMatch(line, offset, match, true) )
            retVal = true;
        else if (match.broken)
            retVal = true;
        else if (offset > 0) // step back and try again
        {
            match.reset();
            if (ls->FindMatch(line, offset - 1, match, true)) 
                retVal = true;
            else if (match.broken)
                retVal = true;
        }
    }

    if (retVal)
    {
        match.offset[0] = InxToPos(match.line[0], match.offset[0]);
        if (!match.broken)
            match.offset[1] = InxToPos(match.line[1], match.offset[1]);
    }

    return retVal;
}

void EditContext::FindMatch (bool select)
{
    _CHECK_ALL_PTR_
    
    LanguageSupportPtr ls = m_pStorage->GetLanguageSupport();

    if (ls.get())
    {
        int line = m_curPos.line, 
            offset = pos2inx(m_curPos.line, m_curPos.column);

        LanguageSupport::Match match;
        ls->FindMatch(line, offset, match);

        if (offset > 0 && (!match.found || match.partial))
        {
            LanguageSupport::Match match2;
            ls->FindMatch(line, offset-1, match2); // step back and try again
            
            if (match2.found && !match2.partial)
                match = match2;
        }

        if (match.found)
        {
            Position pos;
            pos.line   = match.line[1];
            pos.column = (match.line[1] < m_pStorage->GetLineCount()) ? InxToPos(match.line[1], match.offset[1]) : match.offset[1];
            MoveTo(pos);

            // 2014.02.04 bug fix, selection stays on cursor move by Ctrl+[
            if (!select)
                ClearSelection(true);

            if (select && !match.broken)
            {
                Square sqr;
                GetSelection(sqr);

                if (match.partial)
                {
                    LanguageSupport::Match match2;

                    if (ls->FindMatch(match.line[1], match.offset[1], match2))
                    {
                        sqr.start.line   = match2.line[1];
                        sqr.start.column = InxToPos(match2.line[1], match2.offset[1]);
                    }
                }
                else if (sqr.is_empty()) // adjust a original cursor position to a start token position
                {
                    if (!match.backward) 
                    {
                        sqr.start.line   = match.line[0];
                        sqr.start.column = InxToPos(match.line[0], match.offset[0]);
                    }
                    else
                    {
                        sqr.end.line   = match.line[0];
                        sqr.end.column = InxToPos(match.line[0], match.offset[0] + match.length[0]);
                    }
                }

                if (match.backward) 
                {
                    sqr.start.line   = match.line[1];
                    sqr.start.column = InxToPos(match.line[1], match.offset[1]);
                }
                else
                {
                    sqr.end.line   = match.line[1];
                    sqr.end.column = InxToPos(match.line[1], match.offset[1] + match.length[1]);
                }
                SetSelection(sqr);
                MoveTo(sqr.end);
            }
            
            if (match.broken) 
                MessageBeep((UINT)-1);

            Global::SetStatusText(!match.broken ? "Match found." : "Match brocken.");
        }
        else
            Global::SetStatusText("Match not found.");
    }
}

};



