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
     9/04/2002 bug fix, paint fail if delete block which start above the top of page
    25/09/2002 bug fix, smart scrolling on insert/delete for views which are linked to the same storage
    09.03.2003 bug fix, horz scroll bar does not appear when it's supposed to do
    09.03.2003 bug fix, sroll limit should be maximum of line count and the last line on the screen
    17.03.2003 bug fix, cursor position become wrong after document type was changed
    16.02.2004 bug fix, exception on PgDn if "Cursor beyond end of file" is unchecked
    24.05.2004 bug fix, scrolling stops at one line before EOF if "Cursor beyond end of file" is checked
    11.10.2004 improvement, Ruler for columns has been added
    2016.06.14 improvement, Alternative Columnar selection mode 
    2016.06.15 improvement, mouse selectin by words
    2026.10 improvement, OnHScroll/OnVScroll (and split-view sibling sync) now
        use ScrollWindow to reuse on-screen pixels for small scroll steps instead
        of a full Invalidate(FALSE); large jumps still fall back to a full repaint

*/
#include "stdafx.h"
#include "OpenEditor/OEDocument.h"
#include "OpenEditor/OEView.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

using namespace OpenEditor;

// TODO: add a parameter to enable/disable auto-scrolling on highligting
// Line-based form - full-row band (columns stay at the -1/-1 sentinel). Kept
// unchanged for its existing callers (the worksheet's jump-to-error, the
// modeless Replace dialog's scope band); the Square overload below is the
// column-aware alternative.
void COEditorView::SetHighlighting  (int fistHighlightedLine, int lastHighlightedLine, bool updateWindow /*= false*/)
{
    SetHighlightingRange(fistHighlightedLine, lastHighlightedLine, -1, -1, updateWindow);
}

// Column-aware alternative: a single Square - two Positions in the same
// display space GetSelection/GetPosition report, so a caller hands over its
// selection/region square as-is. normalize() orders the ends line-first
// (Position::operator<), so an inverted square paints like the forward one;
// an empty square paints nothing - it bounds an area, not marks lines.
void COEditorView::SetHighlighting  (const Square& square, bool updateWindow /*= false*/)
{
    Square sq = square;
    sq.normalize();

    SetHighlightingRange(sq.start.line, sq.end.line,
                         max(0, sq.start.column), max(0, sq.end.column), updateWindow);
}

// Shared body of both public forms: store the band (lines plus the optional
// column bounds), invalidate, run the legacy autoscroll, refresh the idle
// maintenance caches.
void COEditorView::SetHighlightingRange (int firstLine, int lastLine, int firstColumn, int lastColumn, bool updateWindow)
{
    HideHighlighting();
    
    if (firstLine >= 0 && lastLine >= 0)
    {
        // Store the ends ordered: the line-based form (-1/-1 columns) and any
        // forward square keep their order; an inverted square swaps BOTH lines
        // and columns. A SINGLE-line square must compare columns - the paint
        // takes rcHl.left from the first column and rcHl.right from the last
        // on that one row, so swapping them here would invert the rect.
        if (firstLine < lastLine || (firstLine == lastLine && firstColumn <= lastColumn))
        {
            m_nFistHighlightedLine   = firstLine;
            m_nLastHighlightedLine   = lastLine;
            m_nFistHighlightedColumn = firstColumn;
            m_nLastHighlightedColumn = lastColumn;
        }
        else
        {
            m_nFistHighlightedLine   = lastLine;
            m_nLastHighlightedLine   = firstLine;
            m_nFistHighlightedColumn = lastColumn;
            m_nLastHighlightedColumn = firstColumn;
        }

        InvalidateLines(m_nFistHighlightedLine, m_nLastHighlightedLine);
        
        if (updateWindow)
        {
            UpdateWindow();
            
            if (m_autoscrollOnHighlighting)
            {
                bool needRepainting = false;
                if (m_nFistHighlightedLine < m_Rulers[1].m_Topmost)
                {
                    int topmost = m_Rulers[1].m_Topmost;
                    m_Rulers[1].m_Topmost = max(0, m_nFistHighlightedLine - 1);
                    Invalidate();
                    UpdateWindow();
                    Sleep(750);
                    m_Rulers[1].m_Topmost = topmost;
                    needRepainting = true;
                }
                if (m_nLastHighlightedLine >= m_Rulers[1].m_Topmost + m_Rulers[1].m_Count)
                {
                    int topmost = m_Rulers[1].m_Topmost;
                    m_Rulers[1].m_Topmost = max(0, m_nLastHighlightedLine-m_Rulers[1].m_Count + (1 + 1));
                    Invalidate();
                    UpdateWindow();
                    Sleep(750);
                    m_Rulers[1].m_Topmost = topmost;
                    needRepainting = true;
                }
                if (needRepainting)
                {
                    Invalidate();
                    UpdateWindow();
                }
            }
        }
    }

    m_highlightingPosCache = GetPosition();
    m_highlightingActionSeqCache = GetActionId();
}

void COEditorView::SetBraceHighlighting (const BraceHighlighting& highlighting)
{
    if (highlighting == m_braceHighlighting)
        return;

    HideBraceHighlighting();

    if (highlighting.found || highlighting.broken)
    {
        m_braceHighlighting = highlighting;
        if (m_braceHighlighting.broken)
            m_braceHighlighting.line[1] = -1;

        for (int j(0); j < sizeof(highlighting.line)/sizeof(highlighting.line[0]); j++)
            if (m_braceHighlighting.line[j] != -1)
            {
                if (m_hWnd)
                {
                    Square sqr;
                    sqr.start.line   = sqr.end.line = m_braceHighlighting.line[j];
                    sqr.start.column = m_braceHighlighting.offset[j];
                    sqr.end.column   = m_braceHighlighting.offset[j] + m_braceHighlighting.length[j];
                    InvalidateSquare(sqr);
                }
            }
    }

    m_highlightingPosCache = GetPosition();
    m_highlightingActionSeqCache = GetActionId();
}

void COEditorView::HideHighlighting ()
{
    if (m_nFistHighlightedLine >= 0 && m_nLastHighlightedLine >= 0)
        InvalidateLines(m_nFistHighlightedLine, m_nLastHighlightedLine);

    m_nFistHighlightedLine = -1;
    m_nLastHighlightedLine = -1;
    m_nFistHighlightedColumn = -1;
    m_nLastHighlightedColumn = -1;

    HideBraceHighlighting();
}

void COEditorView::HideBraceHighlighting ()
{
    for (int j(0); j < sizeof(m_braceHighlighting.line)/sizeof(m_braceHighlighting.line[0]); j++)
        if (m_braceHighlighting.line[j] != -1)
        {
            Square sqr;
            sqr.start.line   = sqr.end.line = m_braceHighlighting.line[j];
            sqr.start.column = m_braceHighlighting.offset[j];
            sqr.end.column   = m_braceHighlighting.offset[j] + m_braceHighlighting.length[j];
            InvalidateSquare(sqr);
            m_braceHighlighting.line[j] = -1;
        }
}

// Deliberately leaves the band's auto-clear caches alone: this layer is not auto-cleared.
void COEditorView::SetScopeRegion (const Square& region)
{
    Square sq = region;
    sq.normalize();   // orders the ends line-first, so an inverted region paints like the forward one
    sq.start.column = max(0, sq.start.column);
    sq.end.column   = max(0, sq.end.column);

    if (m_scopeRegionValid)
        InvalidateLines(m_scopeRegion.start.line, m_scopeRegion.end.line);

    m_scopeRegion      = sq;
    m_scopeRegionValid = true;

    InvalidateLines(sq.start.line, sq.end.line);
}

void COEditorView::ClearScopeRegion ()
{
    if (!m_scopeRegionValid)
        return;

    m_scopeRegionValid = false;
    InvalidateLines(m_scopeRegion.start.line, m_scopeRegion.end.line);
    m_scopeRegion.clear();
}

void COEditorView::EnableSyntaxHighlight (BOOL enable)
{
    if (m_bSyntaxHighlight != enable)
    {
        m_bSyntaxHighlight = enable;
        Invalidate(FALSE);
    }
}

void COEditorView::SetPaleIfLocked (BOOL pale)
{
    if (m_bPaleIfLocked != pale)
    {
        m_bPaleIfLocked = pale;
        
        if (IsLocked())
            InvalidateWindow();
    }
}

void COEditorView::SetExecutionLine (int line)
{
    if (m_nExecutionLine != line)
    {
        if (m_nExecutionLine != -1)
            OnChangedLinesStatus(m_nExecutionLine, m_nExecutionLine);

        m_nExecutionLine = line;

        if (m_nExecutionLine != -1)
        {
            // The debugger stepping into a statement inside a collapsed block has
            // to open it. The execution arrow is the entire point of this call,
            // and painting it on a row the user cannot see is worse than not
            // painting it - the session looks stalled with no way to tell why.
            // Revealed before the status change so the repaint that follows uses
            // the final row geometry.
            RevealLine(m_nExecutionLine);

            OnChangedLinesStatus(m_nExecutionLine, m_nExecutionLine);
        }
    }
}

void  COEditorView::DelayedScrollTo (int line)
{
    if (m_nDelayedScrollLine == -1)
        SetTimer(DELAYED_SCROLL_TIMER, DELAYED_SCROLL_TIMER_ELAPSE, NULL);

    m_nDelayedScrollLine = line;
}

void COEditorView::ScrollTo (int line)
{
    // Row space, for the same reason AdjustCaretPosition is: the viewport test and the distance to
    // move are both measured in rows, and m_Topmost is then set to the visible line the target row
    // shows. Scrolling to a line hidden inside a collapsed run therefore brings that run's anchor row
    // into view - the line itself is not on screen, but the fold containing it is, which is the most a
    // scroll can honestly do.
    // With folding off row is line and topRow is m_Rulers[1].m_Topmost, so both branches are the old
    // arithmetic. The one difference is that RowToLine clamps its result to a real visible line, so a
    // degenerate viewport of fewer than four rows scrolled to a line within two of EOF now stops at
    // the last line instead of scrolling past it into a blank viewport with the caret parked off screen.
    const int row    = LineToRow(line);
    const int topRow = GetTopmostRow();

    if (row < topRow)
    {
        m_Rulers[1].m_Topmost = RowToLine(max(0, row - 1));
        SetCaretPosition();
        AdjustVertScroller();
        Invalidate(FALSE);
    }
    else if (row >= topRow + m_Rulers[1].m_Count)
    {
        m_Rulers[1].m_Topmost = RowToLine(max(0, row - m_Rulers[1].m_Count/2 + (1 + 1)));
        SetCaretPosition();
        AdjustVertScroller();
        Invalidate(FALSE);
    }
}

void COEditorView::SetTopmostLine (int line)
{
    // Every caller is restoring a line saved at some earlier moment - OEWorkspaceManager on workspace
    // load, RecentFileList on MRU open, ExecuteTask on the way back from running a statement - so the
    // value can predate the current fold state and name a line that is now inside a collapsed run.
    // m_Topmost stays a BUFFER line on purpose, but a HIDDEN one would start the viewport on a row
    // that does not exist: GetTopmostRow() clamps to the anchor for the paint and the scrollbar, while
    // the direct readers of m_Topmost keep seeing a line that is never on screen. Snap it, the way
    // ApplyFoldChange snaps a topmost that has just gone hidden under a fold.
    //
    // VisibleLine reads the mapper, whose cached prefix sum is only as fresh as the last Invalidate,
    // and GetVisibleLineCount is what refreshes its line count from storage - the same pairing
    // ApplyFoldChange uses before it snaps. Behind m_bAttached because both reach the document, and a
    // view with no document has no fold state to snap against.
    //
    // Out-of-range values are deliberately NOT clamped: VisibleLine leaves them alone so a restored
    // topmost beyond EOF still works under "cursor beyond end of file", exactly as before. And with
    // folding off - the default - the mapper is an identity function, so this hands the line straight
    // back and the restore path is unchanged.
    if (m_bAttached)
    {
        GetVisibleLineCount();
        line = VisibleLine(line);
    }

    if (line >= 0 && m_Rulers[1].m_Topmost != line)
    {
        m_nDelayedScrollLine = -1;
        m_Rulers[1].m_Topmost = line;
        SetCaretPosition();
        AdjustVertScroller();
        Invalidate(FALSE);
    }
}

void COEditorView::SetTopmostColumn (int column)
{
    if (column >= 0 && m_Rulers[0].m_Topmost != column)
    {
        m_Rulers[0].m_Topmost = column;
        SetCaretPosition();
        AdjustVertScroller();
        Invalidate(FALSE);
    }
}

// The scroll arithmetic both rulers share, factored out of Ruler::OnScroll so
// that the vertical path can run it over VISUAL ROWS while the horizontal path
// keeps running it over columns (md/CodeFolding.md section 6, choke points 2
// and 4). `top` is updated in place and the result says whether it moved.
// Every branch below is the original one verbatim; the code names are
// directional (LINELEFT/LINERIGHT) because the vertical ruler has always reused
// the horizontal ones as up/down.
bool COEditorView::ScrollTopmost (int& top, UINT code, UINT pos, int count, int last, bool beyond)
{
    int old_topmost = top;

    switch (code)
    {
    case SB_LINELEFT:
        if (top > 0) top--;
        break;

    case SB_LINERIGHT:
        if (beyond) {
            top++;
        } else {
            // 24.05.2004 bug fix, scrolling stops at one line before EOF if "Cursor beyond end of file" is checked
            if (top + count - 1 < last) 
                top++;
        }
        break;

    case SB_PAGEUP:
        if (top > 0)
            top = max(0, top - count + 1);
        break;

    case SB_PAGEDOWN:
        if (beyond) {
            top = top + count - 1;
        } else {
            // 16.02.2004 bug fix, exception on PgDn if "Cursor beyond end of file" is unchecked
            if (last > top + count - 1) // eof not is visible
                top = min(last - count + 1, top + count - 1);
        }
        break;

    case SB_THUMBPOSITION:
    case SB_THUMBTRACK:
        if (beyond) {
            top = max(0, (int)pos);
        } else {
            top = min(max(0, (int)pos), last);
        }
        break;
    }

    _ASSERTE(top >= 0);

    return (old_topmost != top) ? true : false;
}

// helper function for scrolling service
bool COEditorView::Ruler::OnScroll (UINT code, UINT pos, int last, bool beyond)
{
    // Columns, for the horizontal ruler. The vertical ruler no longer comes
    // through here: DoVScroll runs ScrollTopmost over rows and resolves the
    // result back to a visible buffer line.
    return ScrollTopmost(m_Topmost, code, pos, m_Count, last, beyond);
}

void COEditorView::SetCaretPosition ()
{
    HWND hWnd = ::GetFocus();

    if (hWnd == m_hWnd || m_autocompleteList.m_hWnd)
    {
        Position pos = GetPosition();

        // The "is it inside the viewport" test and the pixel both move to rows
        // (choke point 3). A caret on a hidden line therefore shows on that
        // fold's [...] row rather than off screen, which is what the mapper's
        // contract promises. Horizontal stays on the column ruler.
        if (LineToRow(pos.line) >= GetTopmostRow()
        && pos.column >= m_Rulers[0].m_Topmost)
            ::SetCaretPos(m_Rulers[0].PosToPix(pos.column)/* + 1*/, LineToPixY(pos.line));
        else
            ::SetCaretPos(-10000, -10000);
    }
}

bool COEditorView::GetCaretPosition (CRect& rc) const
{
    Position pos = GetPosition();

    if (LineToRow(pos.line) >= GetTopmostRow()
    && pos.column >= m_Rulers[0].m_Topmost)
    {
        rc.left    = m_Rulers[0].PosToPix(pos.column);
        rc.top     = LineToPixY(pos.line);
        rc.right   = m_Rulers[0].PosToPix(pos.column + 1);
        // One row tall rather than LineToPixY(pos.line + 1): equal to it while
        // folding is off, and still a full row when the next line is hidden
        // inside the same collapsed run, where the two would otherwise coincide
        // and hand back a zero-height rectangle.
        rc.bottom  = rc.top + m_Rulers[1].m_CharSize;

        return (rc.right <= m_Rulers[0].m_ClientSize && rc.bottom <= m_Rulers[1].m_ClientSize) ? true : false;
    }

    return false;
}

void COEditorView::AdjustCaretPosition ()
{
    bool redraw = false;
    Position pos = GetPosition();

    // Horizontal: columns are untouched by folding, so this is as it always was.
    if (m_Rulers[0].m_Topmost > pos.column)
    {
        m_Rulers[0].m_Topmost = pos.column;
        _ASSERTE(m_Rulers[0].m_Topmost >= 0);
        redraw = true;
    }
    else if (pos.column >= m_Rulers[0].m_Topmost + m_Rulers[0].m_Count)
    {
        m_Rulers[0].m_Topmost = pos.column - m_Rulers[0].m_Count + 1;
        _ASSERTE(m_Rulers[0].m_Topmost >= 0);
        redraw = true;
    }

    // Vertical, in rows. The question is unchanged - is the caret above the first visible row or below
    // the last - but it has to be asked in rows, and the new m_Topmost is the visible LINE that row
    // shows. Assigning pos.line itself would be wrong once folds exist: the caret can sit on a line
    // hidden inside a collapsed run, and a hidden m_Topmost would start the viewport inside that run.
    // RowToLine only ever returns a visible line, so the caret ends up on screen either way.
    //
    // With folding off caretRow is pos.line and topRow is m_Rulers[1].m_Topmost, so both branches
    // assign exactly what the old two-axis loop did at i == 1. The second branch cannot go negative:
    // it runs only when caretRow >= topRow + m_Count >= m_Count, so caretRow - m_Count + 1 >= 1.
    const int caretRow = LineToRow(pos.line);
    const int topRow   = GetTopmostRow();

    if (topRow > caretRow)
    {
        m_Rulers[1].m_Topmost = RowToLine(caretRow);
        _ASSERTE(m_Rulers[1].m_Topmost >= 0);
        redraw = true;
    }
    else if (caretRow >= topRow + m_Rulers[1].m_Count)
    {
        m_Rulers[1].m_Topmost = RowToLine(caretRow - m_Rulers[1].m_Count + 1);
        _ASSERTE(m_Rulers[1].m_Topmost >= 0);
        redraw = true;
    }

    SetCaretPosition();

    if (redraw)
    {
        Invalidate(FALSE);
        AdjustVertScroller();
        AdjustHorzScroller();
        AdjustVertSibling();
        AdjustHorzSibling();
    }
}

void COEditorView::AdjustVertSibling ()
{
    if (m_Rulers[1].m_sibling && m_Rulers[1].m_sibling->m_hWnd
    && m_Rulers[1].m_sibling->m_Rulers[1].m_Topmost != m_Rulers[1].m_Topmost)
    {
        COEditorView* sibling = m_Rulers[1].m_sibling;

        // Same reasoning as DoVScroll's fast path, but the delta is measured on
        // the SIBLING's own row mapping (its fold state can differ from ours,
        // even though both read the same document mapper - D15) before its
        // m_Topmost is overwritten.
        int oldTopRow = sibling->GetTopmostRow();

        sibling->m_Rulers[1].m_Topmost = m_Rulers[1].m_Topmost;

        int newTopRow = sibling->GetTopmostRow();
        int deltaRows = oldTopRow - newTopRow;

        if (deltaRows != 0 && abs(deltaRows) < sibling->m_Rulers[1].m_Count)
        {
            CRect rc;
            rc.left   = 0;
            rc.right  = sibling->m_Rulers[0].m_Indent + sibling->m_Rulers[0].m_ClientSize;
            rc.top    = sibling->m_Rulers[1].m_Indent;
            rc.bottom = sibling->m_Rulers[1].m_Indent + sibling->m_Rulers[1].m_ClientSize;

            sibling->ScrollWindow(0, deltaRows * sibling->m_Rulers[1].m_CharSize, &rc, &rc);
        }
        else
            sibling->Invalidate(FALSE);
    }
}

void COEditorView::AdjustHorzSibling ()
{
    if (m_Rulers[0].m_sibling && m_Rulers[0].m_sibling->m_hWnd
    && m_Rulers[0].m_sibling->m_Rulers[0].m_Topmost != m_Rulers[0].m_Topmost)
    {
        COEditorView* sibling = m_Rulers[0].m_sibling;

        int deltaCols = sibling->m_Rulers[0].m_Topmost - m_Rulers[0].m_Topmost;

        sibling->m_Rulers[0].m_Topmost = m_Rulers[0].m_Topmost;

        if (deltaCols != 0 && abs(deltaCols) < sibling->m_Rulers[0].m_Count)
        {
            CRect rc;
            rc.left   = sibling->m_Rulers[0].m_Indent;
            rc.right  = sibling->m_Rulers[0].m_Indent + sibling->m_Rulers[0].m_ClientSize;
            rc.top    = 0;
            rc.bottom = sibling->m_Rulers[1].m_Indent + sibling->m_Rulers[1].m_ClientSize;

            sibling->ScrollWindow(deltaCols * sibling->m_Rulers[0].m_CharSize, 0, &rc, &rc);
        }
        else
            sibling->Invalidate(FALSE);
    }
}

const OpenEditor::LineMapper& COEditorView::GetMapper ()
{
    // The document owns the one mapper the split siblings share (D15), so both
    // views resolve rows against the same hidden-line set and cannot drift.
    return GetDocument()->GetLineMapper();
}

int COEditorView::GetVisibleLineCount ()
{
    // Through the document rather than GetMapper().GetVisualLineCount(): the
    // document refreshes the mapper's line count from storage first, and the
    // view paints and scrolls on messages that can arrive before the idle tick
    // which would otherwise have done it.
    return GetDocument()->GetVisibleLineCount();
}

const OpenEditor::LineMapper& COEditorView::GetMapper () const
{
    // GetDocument() is non-const (it hides CView's), so the const path reads the
    // same member directly and casts it. Only LineMapper's const queries are
    // reachable through what this returns - the cache inside is mutable but
    // nothing else is - so fold state cannot be changed from a const member.
    // As with the non-const overload above, callers are attached views.
    return ((const COEDocument*)m_pDocument)->GetLineMapper();
}

int COEditorView::LineToPixY (int line) const
{
    // Ruler::PosToPix's formula with the buffer-line difference replaced by the
    // row difference. Identical to it while folding is off, because then
    // LineToRow(line) is line and GetTopmostRow() is m_Rulers[1].m_Topmost.
    return (LineToRow(line) - GetTopmostRow()) * m_Rulers[1].m_CharSize
         + m_Rulers[1].m_Indent;
}

int COEditorView::PixToLineY (int y) const
{
    // The inverse: the row the pixel falls on, then the visible line that row
    // shows. RowToLine never hands back a hidden line, so a y inside a collapsed
    // run resolves to the run's anchor.
    return RowToLine(GetTopmostRow()
                   + (y - m_Rulers[1].m_Indent) / m_Rulers[1].m_CharSize);
}

int COEditorView::RowToLineUnclamped (int row)
{
    const int lastRow = GetVisibleLineCount() - 1;

    // Degenerate empty buffer: lastRow is -1, every row is "below" it and the
    // expression reduces to `row`, which is what the caller's old line
    // arithmetic gave too.
    return row <= lastRow ? RowToLine(row)
                          : GetLineCount() + (row - lastRow - 1);
}
// Where a one-line vertical caret step from `line` lands, asked for by EditContext::GoToUp and
// GoToDown. Folding off - the default, and by far the common case - takes the early return and hands
// back the caller's own line + delta, so ordinary arrow-key navigation is untouched down to the
// arithmetic. With folds collapsed the mapper answers instead:
//   Up    the last visible line at or above line - 1, so stepping up from just below a collapsed run
//         lands on that run's ANCHOR rather than on a line hidden inside it, and -1 when nothing
//         visible is above - the same "cannot move" answer the old m_curPos.line > 0 test produced.
//   Down  the first visible line at or below line + 1, so one press from an anchor clears the whole
//         hidden run. GetLineCount() when nothing visible is below; that is the one-past-EOF line
//         "cursor beyond end of file" parks the caret on, so it must be reported honestly and left for
//         GoToDown to accept or reject rather than clamped away here.
// Out-of-range inputs are safe: PrevVisibleLine(-1) walks no iterations and returns -1, and
// NextVisibleLine past the end returns the line count, so neither needs a clamp of its own.
int COEditorView::NextNavigableLine (int line, int delta)
{
    if (!IsFoldingActive())
        return line + delta;

    const OpenEditor::LineMapper& mapper = GetMapper();

    return delta < 0 ? mapper.PrevVisibleLine(line - 1)
                     : mapper.NextVisibleLine(line + 1);
}



void COEditorView::AdjustVertScroller (int maxPos)
{
    SCROLLINFO scrollInfo;
    scrollInfo.cbSize    = sizeof SCROLLINFO;
    scrollInfo.fMask     = SIF_POS|SIF_RANGE|SIF_PAGE;
    scrollInfo.nMin      = 0;
    // Row space (md/CodeFolding.md section 6, choke point 2). The extent is the
    // number of VISIBLE rows and the position is the row the top line sits on,
    // so a collapsed region shortens the scrollbar instead of leaving dead
    // space that scrolls past hidden text. m_Rulers[1].m_Count is already a row
    // count and stays one. m_Rulers[1].m_Topmost remains a buffer line - it is
    // translated here, on the way to the scrollbar, and nowhere else.
    //
    // With folding off GetVisibleLineCount() is GetLineCount() and
    // GetTopmostRow() is m_Rulers[1].m_Topmost, so both expressions below
    // evaluate to exactly what they did before.
    const int topRow = GetTopmostRow();
    scrollInfo.nMax      = max(maxPos, max(topRow + m_Rulers[1].m_Count, GetVisibleLineCount()) - 1);
    scrollInfo.nPage     = m_Rulers[1].m_Count;
    scrollInfo.nPos      = topRow;
    scrollInfo.nTrackPos = 0;

    SetScrollInfo(SB_VERT, &scrollInfo, TRUE);
}

void COEditorView::AdjustHorzScroller (int maxPos)
{
    SCROLLINFO scrollInfo;
    scrollInfo.cbSize    = sizeof SCROLLINFO;
    scrollInfo.fMask     = SIF_POS|SIF_RANGE|SIF_PAGE;
    scrollInfo.nMin      = 0;
    scrollInfo.nMax      = max(maxPos, max(m_Rulers[0].m_Topmost + m_Rulers[0].m_Count, m_nMaxLineLength) - 1);
    //scrollInfo.nMax      = max(m_nMaxLineLength, 1) - 1;
    scrollInfo.nPage     = m_Rulers[0].m_Count;
    scrollInfo.nPos      = m_Rulers[0].m_Topmost;
    scrollInfo.nTrackPos = 0;

    SetScrollInfo(SB_HORZ, &scrollInfo, TRUE);
}

void COEditorView::DoSize (UINT nType, int cx, int cy)
{
    if (m_bAttached && cx > 0 && cy > 0
    && (nType == SIZE_MAXIMIZED || nType == SIZE_RESTORED))
    {
        m_Rulers[0].m_ClientSize = (cx - m_Rulers[0].m_Indent);
        m_Rulers[1].m_ClientSize = (cy - m_Rulers[1].m_Indent);

        m_Rulers[0].m_Count = m_Rulers[0].m_ClientSize / m_Rulers[0].m_CharSize;
        m_Rulers[1].m_Count = m_Rulers[1].m_ClientSize / m_Rulers[1].m_CharSize;

        AdjustVertScroller();
        AdjustHorzScroller();
    }
}

void COEditorView::SelectByMouse (CPoint point)
{
    if (m_nMouseSelStartLine >= 0) // this is line selection mode
    {
        Square selection;
        GetSelection(selection);
        selection.end = GetPosition();

        Position pos = MouseToPos(point);
        pos.column = 0;

        MoveTo(pos);

        selection.start.column = 0;
        if (selection.end.line < m_nMouseSelStartLine)
            selection.start.line = m_nMouseSelStartLine + 1;
        else
        {
            selection.start.line = m_nMouseSelStartLine;
            selection.end.line++;
        }

        SetSelection(selection);

        m_bMouseSelectionStart = FALSE;
    }
    else if (m_bMouseSelectionByWord)
    {
        // When selecting a text with your mouse, double-click on the first word, 
        // hold down the mouse on the second click and then select your text. 
        // It will now select text by words, not characters.
        if (GetBlockMode() != ebtStream)    
            SetBlockMode(ebtStream);

        Square selection(m_startWord);
        Position pos = MouseToPos(point);

        const char* delims = !GetMouseSelectionDelimiters().empty() ? GetMouseSelectionDelimiters().c_str() : 0;

        Square currWord;
        if (WordOrSpaceFromPoint(pos, currWord, delims))
        {
            currWord.normalize();
            if (selection.start < currWord.start)
                pos = selection.end = currWord.end;
            else
                pos = selection.start = currWord.start;

            SetSelection(selection);
            MoveTo(pos);
        }
        else
        {
            int ncount = GetLineCount();
            if (pos.line >= ncount) {
                pos.line = ncount;
                pos.column = 0;
            }
            else {
                pos.column = min(pos.column, InxToPos(pos.line, GetLineLength(pos.line)));
            }

            if (selection.start < pos )
                selection.end = pos;
            else
                selection.start = pos;

            SetSelection(selection);
            MoveTo(pos);
        }
    }
    else
    {
        bool _alt = (0xFF00 & GetKeyState(VK_MENU)) ? true : false;
        if (_alt && GetBlockMode() != ebtColumn) // convert to columnar mode
            SetBlockMode(ebtColumn, true);

        Square selection;
        GetSelection(selection);
        Position pos = MouseToPos(point, GetBlockMode() == ebtColumn);

        if (m_bMouseSelectionStart)
        {
            selection.start = selection.end = pos;
            m_bMouseSelectionStart = FALSE;
        }

        MoveTo(pos, (GetBlockMode() == ebtColumn));
        selection.end = GetPosition();

        SetSelection(selection);
    }
}

OpenEditor::Position
COEditorView::MouseToPos (CPoint point, bool force) const
{
    Position pos;
    //pos.column = (point.x - m_Rulers[0].m_Indent + m_Rulers[0].m_CharSize / 2)
    //             / m_Rulers[0].m_CharSize
    //             + m_Rulers[0].m_Topmost;
    pos.column = (point.x - m_Rulers[0].m_Indent) / m_Rulers[0].m_CharSize
                 + m_Rulers[0].m_Topmost;

    if (pos.column < 0) pos.column = 0;

    // Row first, then the visible line that row shows (choke point 5). The pixel
    // arithmetic is untouched - only the "+ m_Topmost" that used to treat the
    // row offset as a line offset becomes a mapper lookup from the top ROW. A
    // click on a collapsed run's [...] row lands on that run's anchor and a
    // click below the last visible row on the last visible line, never on a line
    // that is not on screen. With folding off GetTopmostRow() is
    // m_Rulers[1].m_Topmost and RowToLine is an identity, so this is the old
    // expression and the clamp below still bites for a negative row.
    pos.line = PixToLineY(point.y);

    if (pos.line < 0) pos.line = 0;

    AdjustPosition(pos, force);

    return pos;
}

void COEditorView::InvalidateLines (int start, int end)
{
    if (start > end)
        std::swap(start, end);
    RECT rc;
    rc.left   = 0;
    rc.right  = m_Rulers[0].m_Indent + m_Rulers[0].m_ClientSize;
    rc.top    = LineToPixY(start);
    rc.bottom = LineToPixY(end+1);
    // Never an empty band: when every line in the range is hidden inside one
    // collapsed run both ends map to the run's anchor row and coincide. That row
    // is the one showing the run's [...] marker, so it is the right thing to
    // repaint. With folding off end >= start already guarantees a row, making
    // this a no-op.
    rc.bottom = max(rc.bottom, rc.top + m_Rulers[1].m_CharSize);
    InvalidateRect(&rc);
}

void COEditorView::InvalidateSquare (const OpenEditor::Square& sqr)
{
    CRect rc;
    rc.left   = m_Rulers[0].PosToPix(min(sqr.start.column, sqr.end.column));
    rc.right  = m_Rulers[0].PosToPix(max(sqr.end.column, sqr.start.column) + 1);
    rc.top    = LineToPixY(min(sqr.start.line, sqr.end.line));
    rc.bottom = LineToPixY(max(sqr.end.line, sqr.start.line) + 1);
    rc.bottom = max(rc.bottom, rc.top + m_Rulers[1].m_CharSize); // as above
    InvalidateRect(&rc);
}

void COEditorView::OnChangedLinesQuantity (int line, int quantity)
{
    if (quantity < 0) // line deleting
    {
        // invalidate lines between old & new EOF mark
        int lines = GetLineCount();
        InvalidateLines(lines, lines - quantity);

        if (line <= m_Rulers[1].m_Topmost // the first deleted line is above viewport
        && line - quantity >= m_Rulers[1].m_Topmost + m_Rulers[1].m_Count 
            - (m_Rulers[1].m_ClientSize % m_Rulers[1].m_CharSize ? 1 : 0)) // the last deleted line is below viewport
        {
            // move deleted lines position in the center of viewport
            m_Rulers[1].m_Topmost = line - m_Rulers[1].m_Count/2;
            if (m_Rulers[1].m_Topmost < 0) m_Rulers[1].m_Topmost = 0;

            InvalidateLines(m_Rulers[1].m_Topmost, m_Rulers[1].m_Topmost + m_Rulers[1].m_Count);
        }
        else if (m_Rulers[1].m_Topmost >= line) // the line is above the topmost visible line
        {
                if (m_Rulers[1].m_Topmost >= line - quantity) // all deleted lines are above topmost
                {
                    m_Rulers[1].m_Topmost += quantity;
                    if (m_Rulers[1].m_Topmost < 0) 
                        m_Rulers[1].m_Topmost = 0;
                    
                    // repainting is not required
                }
                else if (m_Rulers[1].m_Topmost + m_Rulers[1].m_Count > line - quantity) // the last deleted line is visible
                {
                    // the next line on the screen should save its position
                    m_Rulers[1].m_Topmost += quantity - 1;
                    if (m_Rulers[1].m_Topmost < 0) 
                        m_Rulers[1].m_Topmost = 0;

                    InvalidateLines(m_Rulers[1].m_Topmost, m_Rulers[1].m_Topmost + m_Rulers[1].m_Count);
                }
        }

        // adjust the current line
        int curLine = GetCursorLine();
        if (curLine >= line - quantity) // the current line is below the last deleted line
        {
            curLine += quantity;
            if (curLine < 0) curLine = 0;
        }
        else if (curLine > line && curLine < line - quantity) // the current line is in the deleted lines
            curLine = line;
        
        moveCurrentLine(curLine);
    }
    else // inserting
    {
        if (line < m_Rulers[1].m_Topmost) // the first inserted line is above viewport
        {
            m_Rulers[1].m_Topmost += quantity;
            // repainting is not required
        }
        else if (line < m_Rulers[1].m_Topmost + m_Rulers[1].m_Count) // the first inserted line is above of viewport bottom
        {
            // just repainting
            InvalidateLines(line, m_Rulers[1].m_Topmost + m_Rulers[1].m_Count);
        }

        // adjust the cursor line
        int curLine = GetCursorLine();
        if (line < curLine)
        {
            curLine += quantity;
            moveCurrentLine(curLine);
        }
    }
    
    ClearSelection(); // either clear or recalculate selection
    AdjustVertScroller();

    if (GetSettings().GetLineNumbers())
    {
        int digits = CalculateDigitsInLatsLineNumber();
        if (m_nDigitsInLatsLineNumber != digits)
        {
            m_nDigitsInLatsLineNumber = digits;

            int leftMargin = CalculateLeftMargin(digits);

            if (m_Rulers[0].m_Indent != leftMargin)
            {
                m_Rulers[0].m_Indent = leftMargin;
                AdjustCaretPosition(); // 17.03.2003 bug fix, cursor position become wrong after document type was changed
                Invalidate(FALSE);
            }
        }
    }

    m_curCharCache.Reset();

    // lines were inserted or deleted: the recorded columns cannot be trusted any more, so
    // every tracked auto-inserted closer is dropped (spec §3.5)
    if (!m_autoPairEditing)
        ResetAutoPairs();

    // Keep the document's fold regions in step with the edit. Hooked HERE and deliberately not in
    // OnChangedLines as well - a single line insert fires both notifications, so shifting in both would
    // move every region below the edit twice.
    //
    // The AutoPairEditGuard is not consulted: m_autoPairEditing only suppresses the coarse auto-pair
    // reset above so that the precise ShiftAutoPairs can run instead, and the fold model has no coarse
    // path to suppress because OnLinesInserted/OnLinesDeleted IS the precise one. Skipping the shift
    // during an auto-close or auto-surround edit would leave every region below it pointing at the
    // wrong line.
    //
    // Split siblings: Storage notifies every linked context, so the document picks one sender to act on
    // - see OnFoldLinesQuantityChanged.
    if (COEDocument* pDoc = GetDocument())
        pDoc->OnFoldLinesQuantityChanged(this, line, quantity);
}

void COEditorView::OnChangedLines (int startLine, int endLine)
{
    // The same viewport-intersection test OnChangedLinesStatus below uses, and
    // in rows for the same reason: once a region is collapsed the buffer lines
    // on screen are neither contiguous nor bounded by m_Topmost + m_Count, so a
    // line-space test can miss a change that is plainly visible and skip the
    // repaint. InvalidateLines still takes buffer lines - it maps them itself.
    // With folding off the rows are the lines and this is the old test.
    const int topRow   = GetTopmostRow();
    const int firstRow = LineToRow(startLine);
    const int lastRow  = LineToRow(endLine);

    if ( ((firstRow <= topRow) && (topRow <= lastRow))
       || (topRow <= firstRow) && (firstRow <= (topRow + m_Rulers[1].m_Count))
       )
    {
        InvalidateLines(startLine, endLine);
    }
    AdjustHorzScroller();

    m_curCharCache.Reset();

    // auto-close tracking: an edit this view did not make itself invalidates the affected
    // lines (spec §3.5). OnChar's own single-character edit keeps its records and shifts
    // them instead - see ShiftAutoPairs.
    if (!m_autoPairEditing)
        ResetAutoPairs(startLine, endLine);
}

// TODO: make sure that we enfoce repainting only for a vertical ruler
void COEditorView::OnChangedLinesStatus (int startLine, int endLine)
{
    // Both the "does this touch the viewport" test and the band to repaint are
    // in rows, because with folds on screen the buffer lines the viewport shows
    // are neither contiguous nor bounded by m_Topmost + m_Count. With folding
    // off the rows are the lines and this is the old test unchanged.
    const int topRow   = GetTopmostRow();
    const int firstRow = LineToRow(startLine);
    const int lastRow  = LineToRow(endLine);

    if ( ((firstRow <= topRow) && (topRow <= lastRow))
       || (topRow <= firstRow) && (firstRow <= (topRow + m_Rulers[1].m_Count))
       )
    {
        RECT rc;
        rc.left   = 0;
        rc.right  = m_Rulers[0].m_Indent;
        rc.top    = LineToPixY(startLine);
        rc.bottom = LineToPixY(endLine+1);
        rc.bottom = max(rc.bottom, rc.top + m_Rulers[1].m_CharSize); // never empty
        InvalidateRect(&rc);
    }
}
