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

// 30.06.2004 improvement/bug fix, text search/replace interface has been changed
// 2016.06.14 improvement, Alternative Columnar selection mode 

#include "stdafx.h"
#include <algorithm>
#include <COMMON/ExceptionHelper.h>
#include <OpenEditor/OEContext.h>
#include <OpenEditor/OELanguage.h>


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define _CHECK_ALL_PTR_ _CHECK_AND_THROW_(m_pStorage, "Editor has not been initialized properly!")

namespace OpenEditor
{
    using namespace std;

EditContext::EditContext ()
{
    m_pStorage = 0;
    m_BlockMode = ebtStream;

    Init ();
}

EditContext::~EditContext ()
{
    try {
        if (m_pStorage)
            m_pStorage->UnlinkContext(this);
    }
    _DESTRUCTOR_HANDLER_;
}

void EditContext::Init ()
{
    m_orgHrzPos     =
    m_curPos.line   =
    m_curPos.column = 0;

    m_blkPos.start.line   =
    m_blkPos.start.column =
    m_blkPos.end.line     =
    m_blkPos.end.column   = 0;

    m_bDirtyPosition = false;
    m_bCurrentLineHighlighted = true;
    m_AltColumnarMode = false;
}

void EditContext::SetStorage (Storage* pStorage)
{
    if (m_pStorage)
        m_pStorage->UnlinkContext(this);

    m_pStorage = pStorage;

    if (m_pStorage)
        m_pStorage->LinkContext(this);

    _CHECK_ALL_PTR_;

    Init ();
}

void EditContext::OnSettingsChanged ()
{
    InvalidateWindow ();
    adjustCursorPosition();
}

int EditContext::GetLineLength (int line) const
{
    _CHECK_ALL_PTR_;

    //_ASSERTE(line < GetLineCount());

    if (line < GetLineCount())
    {
        OEStringW str;
        GetLineW(line, str);
        return EditContext::inx2pos(str.data(), str.length(), str.length());
    }

    return 0;
}

bool EditContext::Find (const EditContext*& pEditContext, FindCtx& ctx) const
{
    _ASSERTE(m_pStorage);

    ctx.m_storage = m_pStorage;

    ctx.m_start = pos2inx(ctx.m_line, ctx.m_start, GetCursorBeyondEOL());
    ctx.m_end   = pos2inx(ctx.m_line, ctx.m_end, GetCursorBeyondEOL());

    bool retVal = m_pStorage->Find(ctx);

    if (retVal)
    {
        if (ctx.m_storage != m_pStorage)
            pEditContext = (EditContext*)ctx.m_storage->GetFistEditContext();
        else
            pEditContext = this;

        ctx.m_start = pEditContext->inx2pos(ctx.m_line, ctx.m_start);
        ctx.m_end   = pEditContext->inx2pos(ctx.m_line, ctx.m_end);
    }

    return retVal;
}

bool EditContext::Replace (FindCtx& ctx)
{
    _ASSERTE(m_pStorage);

    ctx.m_start = pos2inx(ctx.m_line, ctx.m_start, GetCursorBeyondEOL());
    ctx.m_end   = pos2inx(ctx.m_line, ctx.m_end, GetCursorBeyondEOL());

    bool retVal = m_pStorage->Replace(ctx);

    if (retVal)
    {
        ctx.m_start = inx2pos(ctx.m_line, ctx.m_start);
        ctx.m_end   = inx2pos(ctx.m_line, ctx.m_end);

        Square blk;
        blk.start.column = ctx.m_start;
        blk.end.column = ctx.m_end;
        blk.end.line = blk.start.line = ctx.m_line;

        SetSelection(blk);
        MoveTo(blk.end);
    }

    return retVal;
}

int EditContext::SearchBatch (const wchar_t* text, ESearchBatch mode)
{
    _ASSERTE(m_pStorage);

    // D8: Replace All rewrites its occurrences through Storage::Replace, so every
    // hit that sits on a hidden row modifies text a fold is hiding and the user
    // never sees it. Ask DoBatch which BUFFER lines it actually changed and open
    // the folds that hid them.
    //
    // Not the returned Square: DoBatch overwrites `blk` on every pass
    // (OEHelpers_3.cpp:594-596), so it holds only the LAST match and would leave
    // all earlier hidden edits unrevealed.
    //
    // Revealing here rather than inside DoBatch is forced by layering - RevealLine
    // is an EditContext virtual (OEContext.h:193) while Searcher only holds a
    // Storage*, whose GetFistEditContext() returns the base class that has no such
    // member. It is also the correct moment: DoBatch keeps a NotificationDisabler
    // and an UndoGroup open across the whole batch, and RevealLine must run
    // outside that window or ApplyFoldChange is deferred and the reveal lands
    // after the caret has already moved.
    vector<int> changedLines;

    Square blk;
    int retVal = m_pStorage->SearchBatch(text, mode, blk, &changedLines);

    if (mode == esbReplace && retVal > 0)
    {
        // Before SetSelection/MoveTo, so the caret is never parked on a row the
        // reveal has not made visible yet - the ordering MoveToAndCenter states at
        // OEView_1.cpp:3241-3243.
        //
        // The collected line numbers cannot go stale: Searcher::Replace rejects a
        // replacement containing CR or LF outright (OEHelpers_3.cpp:478-479), so a
        // batch replace never changes the line count and every index recorded on an
        // earlier pass still addresses the same BUFFER line by the time this runs.
        // Buffer lines, not rows - the buffer stays whole behind its folds (D1).
        for (size_t i = 0; i < changedLines.size(); i++)
            RevealLine(changedLines[i]);

        blk.start.column = inx2pos(blk.start.line, blk.start.column);
        blk.end.column = inx2pos(blk.end.line, blk.end.column);
        SetSelection(blk);
        MoveTo(blk.end);
    }

    return retVal;
}

};
