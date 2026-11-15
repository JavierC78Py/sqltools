/* 
    Copyright (C) 2002-2005 Aleksey Kochetov

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
    28.03.2003 improvement, optimization of GDI resources usage - it's a very critical issue for Win95/Win98
    29.06.2003 bug fix, selected text foreground color cannot be changed for "Text" class
    11.10.2004 improvement, Ruler for columns has been added
    11.10.2004 improvement, Columnar markers has been added (can be defined in class property page)
    16.04.2005 improvement, optimization of DrawHorzRuler/DrawVertRuler
    2016.05.10 improvement, implemented Read-Only mode
    2016.06.12 improvement, Highlighting for changed lines
*/

#include "stdafx.h"
namespace MyMemDC 
{
#include "MemDC.h"
};
using namespace MyMemDC;

#include "OpenEditor/OEDocument.h"
#include "OpenEditor/OEView.h"
#include "OpenEditor/OEHighlighter.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

using namespace OpenEditor;

/////////////////////////////////////////////////////////////////////////////
// COEditorView drawing

    // arguments should be normalized
    // it's necessary to decrement blk.end.column
    inline
    bool _is_block_visible (const Square& scr, const Square& blk, EBlockMode mode)
    {
        switch (mode)
        {
        case ebtColumn:
            return intersect_not_null(scr, blk);
        case ebtStream:
            if (blk.start.line == blk.end.line) return intersect_not_null(scr, blk);// 1-line block
        }
        return scr.line_in_rect(blk.start.line) || blk.line_in_rect(scr.start.line);
    }

void COEditorView::DrawLineNumber (CDC& dc, RECT rc, int line)
{
    rc.right -= (m_Rulers[0].m_CharSize*3)/4 + LINE_STATUS_WIDTH;

    char buff[40];
    itoa(line + 1, buff, 10);
    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(m_paintAccessories->m_COLOR_BTNTEXT);
    
    CFont* pOrgFont = m_paintAccessories->SelectFontEx(dc, 0);
    DrawTextA(dc.m_hDC, buff, strlen(buff), &rc, DT_RIGHT|DT_TOP);
    dc.SelectObject(pOrgFont);
}

void COEditorView::DrawBookmark (CDC& dc, RECT rc)
{
    rc.left  += 2; 
    rc.right -= 2 + LINE_STATUS_WIDTH;
    CBrush* pOrgBrush = dc.SelectObject(&m_paintAccessories->m_BmkBrush);
    CPen* pOrgPen = dc.SelectObject(&m_paintAccessories->m_BtnShadowPen/*m_BmkPen*/);
    dc.RoundRect(&rc, CPoint(3, 3));
    dc.SelectObject(pOrgBrush);
    dc.SelectObject(pOrgPen);
}

static // move it in Common namespace
void fill_gradient (HDC hDC, LPCRECT lpRect, COLORREF colorStart, COLORREF colorFinish, BOOL bHorz/* = TRUE*/)
{
    // this will make 2^6 = 64 fountain steps
    int nShift = 6;
    int nSteps = 1 << nShift;

    RECT r2;
    r2.top = lpRect->top;
    r2.left = lpRect->left;
    r2.right = lpRect->right;
    r2.bottom = lpRect->bottom;

    int nHeight = lpRect->bottom - lpRect->top;
    int nWidth = lpRect->right - lpRect->left;

    for (int i = 0; i < nSteps; i++)
    {
        // do a little alpha blending
        BYTE bR = (BYTE) ((GetRValue(colorStart) * (nSteps - i) +
                   GetRValue(colorFinish) * i) >> nShift);
        BYTE bG = (BYTE) ((GetGValue(colorStart) * (nSteps - i) +
                   GetGValue(colorFinish) * i) >> nShift);
        BYTE bB = (BYTE) ((GetBValue(colorStart) * (nSteps - i) +
                   GetBValue(colorFinish) * i) >> nShift);

        HBRUSH hBrush = ::CreateSolidBrush(RGB(bR, bG, bB));
        
        // then paint with the resulting color

        if (!bHorz)
        {
            r2.top = lpRect->top + ((i * nHeight) >> nShift);
            r2.bottom = lpRect->top + (((i + 1) * nHeight) >> nShift);
            if ((r2.bottom - r2.top) > 0)
                ::FillRect(hDC, &r2, hBrush);
        }
        else
        {
            r2.left = lpRect->left + ((i * nWidth) >> nShift);
            r2.right = lpRect->left + (((i + 1) * nWidth) >> nShift);
            if ((r2.right - r2.left) > 0)
                ::FillRect(hDC, &r2, hBrush);
        } //if
        
        if (NULL != hBrush)
        {
            ::DeleteObject(hBrush);
            hBrush = NULL;
        } //if
    } //for
} //End FillGradient

void COEditorView::DrawRandomBookmark (CDC& dc, RECT rc, int index, bool halfclip)
{
    rc.left  += 2; 
    rc.right -= 2 + LINE_STATUS_WIDTH;

    if (halfclip)
    {
        rc.left = rc.right / 2;
        dc.IntersectClipRect(&rc);
        rc.left = 0;
    }

    CBrush* pOrgBrush = dc.SelectObject(&m_paintAccessories->m_RndBmkBrush);
    CPen* pOrgPen = dc.SelectObject(&m_paintAccessories->m_BtnShadowPen/*m_BmkPen*/);
    dc.RoundRect(&rc, CPoint(3, 3));
    dc.SelectObject(pOrgBrush);
    dc.SelectObject(pOrgPen);

    if (halfclip)
    {
        if (halfclip)
            dc.SelectClipRgn(NULL);
        
        RECT _rc = rc;
        ::InflateRect(&_rc, -3, -1);
        fill_gradient(dc, &_rc, m_paintAccessories->m_BmkBackground, m_paintAccessories->m_RndBmkBackground, TRUE);
    }

    char buff[40];
    itoa(index, buff, 10);
    dc.SetBkMode(TRANSPARENT);
    dc.SetTextColor(m_paintAccessories->m_RndBmkForeground);

    //CFont* pOrgFont = m_paintAccessories->SelectFont(dc, GetSettings().GetLineNumbers() ? HighlighterBase::efmBold : HighlighterBase::efmNormal);
    CFont* pOrgFont = m_paintAccessories->SelectFontEx(dc, HighlighterBase::efmNormal);

    DrawTextA(dc.m_hDC, buff, strlen(buff), &rc, DT_CENTER|DT_TOP);
    dc.SelectObject(pOrgFont);
}

void COEditorView::DrawHorzRuler (CDC& realDc, const CRect& rc, int start, int end)
{
    MyMemDC::CMemDC dc(&realDc, &rc);
    dc.FillRect(&rc, &m_paintAccessories->m_BtnFaceBrush);

    COLORREF bkColor = dc.SetBkColor(m_paintAccessories->m_COLOR_BTNFACE);
    COLORREF txtColor = dc.SetTextColor(m_paintAccessories->m_COLOR_BTNTEXT);
    CFont* pOrgFont = dc.SelectObject(&m_paintAccessories->m_RulerFont);
    CPen* pOrgPen = dc.SelectObject(&m_paintAccessories->m_BmkPen);

    dc.MoveTo(rc.left, rc.bottom-1);
    dc.LineTo(rc.right, rc.bottom-1);

    for (int i = start; i <= end; i++)
    {
        int column = m_Rulers[0].m_Topmost + i;

        int x = m_Rulers[0].m_Indent + m_Rulers[0].m_CharSize * i;

        int hight = (!(column % 5)) ? 4 : 2;

        dc.MoveTo(x, rc.bottom-1-hight);
        dc.LineTo(x, rc.bottom-1);

        if (!(column % 10))
        {
            char buffer[80];
            itoa(column/*+1*/, buffer, 10);
            int buflen = strlen(buffer);
            int width = buflen * m_paintAccessories->m_RulerCharSize.cx;
            DrawTextA(dc.m_hDC, buffer, buflen, 
                CRect(x - width, 0, x + width, m_paintAccessories->m_RulerCharSize.cy), 
                DT_CENTER|DT_TOP
            );
        }
    }

    dc.SetBkColor(bkColor);
    dc.SetTextColor(txtColor);
    dc.SelectObject(pOrgFont);
    dc.SelectObject(pOrgPen);
}

#pragma warning ( push ) 
#pragma warning ( disable : 4701 ) 
// Anchors found while walking the gutter. Boxes are drawn AFTER the connectors so
// the box fill covers the rule instead of the rule striking through the glyph.
struct FoldGlyphAt
{
    int  rowTop;
    bool collapsed;
};

void COEditorView::DrawVertRuler (CDC& realDc, const CRect& rc, int start, int end, bool lineNumbers)
{
    // start/end are viewport ROWS; the line each row describes comes from the
    // mapper, so numbering skips folded runs.
    const int topRow      = GetTopmostRow();
    const int visibleRows = GetVisibleLineCount();

    MyMemDC::CMemDC dc(&realDc, &rc);
    COLORREF bkColor = dc.SetBkColor(m_paintAccessories->m_COLOR_BTNHIGHLIGHT);
    COLORREF txtColor = dc.SetTextColor(m_paintAccessories->m_COLOR_BTNFACE);

    RECT stdGutterRc, extGutterRc;
    stdGutterRc.left   = 0;
    stdGutterRc.right  = m_Rulers[0].m_Indent;
    stdGutterRc.top    = rc.top;
    stdGutterRc.bottom = rc.bottom;

    CPen* pOrgPen = dc.SelectObject(&m_paintAccessories->m_BtnShadowPen);

    LanguageSupportPtr ls = GetLanguageSupport();
    bool syntaxExtendedSupport = m_syntaxGutter && ls.get() && ls->HasExtendedSupport(); // it can be a memeber

    // Must stay in step with the condition CalculateLeftMargin reserves the band
    // on. Driven by the SETTING, not by whether regions exist yet - the parse lands
    // on an idle tick and the margin must not shift under the caret afterwards.
    const bool foldingGutter = m_foldingGutter;

    const OpenEditor::FoldModel* pFolds =
        foldingGutter ? &GetDocument()->GetFoldModel() : 0;

    // GetCurrentFoldHighlight, not GetCurrentFoldRegion: that setting gates the tint only.
    int curRegionStart = -1, curRegionEnd = -1;
    if (pFolds)
        GetCurrentFoldHighlight(curRegionStart, curRegionEnd);

    if (syntaxExtendedSupport || foldingGutter)
    {
        if (syntaxExtendedSupport)
        {
            // Keyed by BUFFER line, so the range has to cover the lines the rows
            // resolve to - wider than m_Count with a collapsed region on screen.
            // Widened to the old range so it can never under-cover.
            const int firstLine = RowToLine(topRow + max(start, 0));
            const int lastLine  = RowToLine(topRow + max(end - 1, start));
            const int rangeFrom = min(firstLine, m_Rulers[1].m_Topmost);
            const int rangeTo   = max(lastLine, m_Rulers[1].m_Topmost + m_Rulers[1].m_Count);

            ls->GetLineStatus(m_statusMap, rangeFrom, rangeTo - rangeFrom + 1);
        }

        extGutterRc = stdGutterRc;
        stdGutterRc.right -= EXT_SYN_GUTTER_WIDTH;
        extGutterRc.left   = stdGutterRc.right;
        dc.FillRect(&extGutterRc, dc.GetHalftoneBrush());
        dc.FillSolidRect(&stdGutterRc, m_paintAccessories->m_COLOR_BTNFACE);
        extGutterRc.right--;
        stdGutterRc.right--;

        dc.MoveTo(extGutterRc.right, extGutterRc.top);
        dc.LineTo(extGutterRc.right, extGutterRc.bottom);
    }
    else
    {
        dc.FillRect(&stdGutterRc, dc.GetHalftoneBrush());
        stdGutterRc.right--;
    }
    dc.MoveTo(stdGutterRc.right, stdGutterRc.top);
    dc.LineTo(stdGutterRc.right, stdGutterRc.bottom);
    dc.SelectObject(pOrgPen);

    dc.SetBkColor(bkColor);
    dc.SetTextColor(txtColor);

    int lim = min(visibleRows - topRow, end);   // describable rows left

    std::vector<FoldGlyphAt> glyphs;
    if (pFolds)
    {
        // SIGNED: a negative one cast to size_t makes reserve throw from inside
        // OnPaint. It happens for real on a paint that lands after a fold but
        // before the scroll position has caught up.
        const int glyphRows = lim - start;

        if (glyphRows > 0)
            glyphs.reserve((size_t)glyphRows);
    }

    for (int i = start; i < lim; i++)
    {
        const int line = RowToLine(topRow + i);
        stdGutterRc.top    = m_Rulers[1].m_Indent + m_Rulers[1].m_CharSize * i;
        stdGutterRc.bottom = stdGutterRc.top + m_Rulers[1].m_CharSize;

        RandomBookmark bookmarkId;
        bool isBmkLine = IsBookmarked(line, eBmkGroup1);
        bool isRandomBmkLine = IsRandomBookmarked(line, bookmarkId);
        ELineStatus status = GetLineStatus(line);

        if (status != elsNothing)
        {
            CRect gtRc = stdGutterRc;
            gtRc.right -= 1;
            gtRc.left  = gtRc.right - LINE_STATUS_WIDTH;
            dc.FillSolidRect(gtRc, 
                    status == elsUpdated ? RGB(255, 255, 0) 
                        : status == elsRevertedBevoreSaved ? RGB(255, 215, 0) 
                            : RGB(124, 252, 0));
        }

        if (isRandomBmkLine)
        {
            if (isBmkLine) DrawBookmark(dc, stdGutterRc);
            DrawRandomBookmark(dc, stdGutterRc, bookmarkId.GetId(), isBmkLine);
        }
        else
        {
            if (isBmkLine) DrawBookmark(dc, stdGutterRc);
            if (lineNumbers) DrawLineNumber(dc, stdGutterRc, line);
        }
        //TODO#2: move in procedure
        if (m_nExecutionLine != -1 && m_nExecutionLine == line)
        {
            //TODO#2: maybe to add the color attributes?
            CBrush* pOrgBrush = dc.SelectObject(&m_paintAccessories->m_RndBmkBrush);
            CPen* pOrgPen2 = dc.SelectObject(&m_paintAccessories->m_RndBmkPen);

            POINT pt[3] = { 
                { stdGutterRc.left + 2, stdGutterRc.top + 2 },
                { stdGutterRc.left + 2, stdGutterRc.bottom - 2 },
                { min((stdGutterRc.bottom - stdGutterRc.top) / 2, stdGutterRc.right - stdGutterRc.left - 4), (stdGutterRc.bottom + stdGutterRc.top) / 2 }
            };
            dc.Polygon(pt, sizeof(pt)/sizeof(pt[0]));

            dc.SelectObject(pOrgBrush);
            dc.SelectObject(pOrgPen2);
        }

        // Current-region tint, under the error fills and the connectors/glyphs.
        if (curRegionStart >= 0 && line >= curRegionStart && line <= curRegionEnd)
        {
            extGutterRc.top    = stdGutterRc.top;
            extGutterRc.bottom = stdGutterRc.bottom;
            dc.FillSolidRect(&extGutterRc, m_paintAccessories->m_HighlightingBackground);
        }

        if (syntaxExtendedSupport) 
        {
            CPen* pOrgPen2 = dc.SelectObject(&m_paintAccessories->m_BtnShadowPen);

            extGutterRc.top    = stdGutterRc.top;
            extGutterRc.bottom = stdGutterRc.bottom;

            // Unparsed-tail band, filled FIRST so it sits under the error fill and the guides.
            if (m_statusMap[line].unparsed)
                dc.FillSolidRect(&extGutterRc, m_paintAccessories->m_UnparsedBandBackground);

            if (m_statusMap[line].syntaxError)
                dc.FillSolidRect(&extGutterRc, m_paintAccessories->m_ErrorHighlightingBackground);

            // Level guides are the non-folding rendering of the outline; with folding
            // on the connectors drawn after this loop replace them.
            if (!pFolds)
            {
                if (int level = m_statusMap[line].level)
                {
                    int lim2 = min(EXT_SYN_NESTED_LIMIT, level);
                    for (int i2 = 1; i2 <= lim2; i2++) {
                        dc.MoveTo(extGutterRc.left + i2 * EXT_SYN_LINE_INDENT, extGutterRc.top);
                        dc.LineTo(extGutterRc.left + i2 * EXT_SYN_LINE_INDENT, extGutterRc.bottom);

                        if (level - m_statusMap[line].beginCounter < i2) {
                            dc.MoveTo(extGutterRc.left + i2 * EXT_SYN_LINE_INDENT, extGutterRc.top);
                            dc.LineTo(extGutterRc.left + (i2+1) * EXT_SYN_LINE_INDENT, extGutterRc.top);
                        }
                        if (level - m_statusMap[line].endCounter < i2) {
                            dc.MoveTo(extGutterRc.left + i2 * EXT_SYN_LINE_INDENT, extGutterRc.bottom-1);
                            dc.LineTo(extGutterRc.left + (i2+1) * EXT_SYN_LINE_INDENT, extGutterRc.bottom-1);
                        }
                    }
                }
            }
            dc.SelectObject(pOrgPen2);

            // Horizontal rule on the boundary row: the one shape in this band that
            // cannot be mistaken for a vertical level guide. Outside the BtnShadowPen
            // scope above because it wants its own colour. `right` stops one pixel
            // short of the separator, LineTo excluding its endpoint.
            if (m_statusMap[line].unparsedBoundary)
            {
                CPen* pOrgPen3 = dc.SelectObject(&m_paintAccessories->m_UnparsedBandPen);

                dc.MoveTo(extGutterRc.left, extGutterRc.top);
                dc.LineTo(extGutterRc.right, extGutterRc.top);

                dc.SelectObject(pOrgPen3);
            }
        }

        if (pFolds)
        {
            // Only the ANCHOR row carries a glyph.
            if (pFolds->IsAnchorAt(line))
            {
                FoldGlyphAt g;
                g.rowTop    = stdGutterRc.top;
                g.collapsed = pFolds->IsCollapsedAt(line);
                glyphs.push_back(g);
            }
        }
    }

    // --- fold connectors, then the boxes on top of them -----------------------
    if (pFolds)
    {
        const int charSize = m_Rulers[1].m_CharSize;
        const int rowTopY  = m_Rulers[1].m_Indent;    // y of row `topRow`

        // Buffer-line bounds of the window, matched first so only intersecting
        // regions pay for a LineToRow.
        const int firstLine = RowToLine(topRow);
        const int lastLine  = RowToLine(topRow + visibleRows - 1);

        // Template row: FoldGlyphBox only needs the height, top is filled in per region.
        CRect bandRc(extGutterRc.left, 0, extGutterRc.right, charSize);

        CPen* pOrgPen = dc.SelectObject(&m_paintAccessories->m_BtnShadowPen);

        // The AVAILABLE layer, not Count()/Get(i): connectors are drawn for what can
        // be folded, and that layer is the one sorted by startLine the break below needs.
        // An orphan (folded but no longer offered) has no terminator row to draw to;
        // its glyph and [...] come from the state queries, which do consult m_collapsed.
        const OpenEditor::FoldModel::Regions& available = pFolds->GetAvailable();

        for (size_t i = 0; i < available.size(); i++)
        {
            const OpenEditor::FoldRegion& r = available.at(i);

            // The layer ascends by startLine.
            if (r.startLine > lastLine)
                break;
            if (r.endLine < firstLine)
                continue;                         // entirely above the window
            if (pFolds->IsFolded(r))
                continue;                         // [+] only: its end row is hidden

            const int rowA = LineToRow(r.startLine);
            const int rowE = LineToRow(r.endLine);

            // An enclosing collapsed region swallowed this one - hidden lines map onto its anchor.
            if (rowE <= rowA)
                continue;

            bandRc.top    = rowTopY + charSize * (rowA - topRow);
            bandRc.bottom = bandRc.top + charSize;

            const CRect boxA = FoldGlyphBox(bandRc);
            if (boxA.IsRectEmpty())
                continue;

            // One shared column at every depth, so nested connectors overlap and read as a single line.
            const int x = (boxA.left + boxA.right) / 2;
            const int yTop = bandRc.top + charSize / 2;
            const int yBot = rowTopY + charSize * (rowE - topRow) + charSize / 2;

            dc.MoveTo(x, yTop);
            dc.LineTo(x, yBot + 1);               // LineTo excludes its endpoint

            // End-tick at the terminator row; +2 over the indent unit to stay visible.
            dc.MoveTo(x, yBot);
            dc.LineTo(x + EXT_SYN_LINE_INDENT + 2, yBot);
        }

        dc.SelectObject(pOrgPen);

        for (size_t k = 0; k < glyphs.size(); k++)
        {
            CRect glyphRc(extGutterRc.left, glyphs[k].rowTop,
                          extGutterRc.right, glyphs[k].rowTop + charSize);
            DrawFoldGlyph(dc, glyphRc, glyphs[k].collapsed);
        }
    }
}
#pragma warning ( pop ) 

CRect COEditorView::FoldGlyphBox (const RECT& rc) const
{
    // One centred column at every depth, so nested boxes and their connectors line up.
    const int availW = rc.right - rc.left;
    const int availH = rc.bottom - rc.top;

    int sz = min(availW, availH - 2) - 4; // with -4 the box looks better than -3
    if (sz < 4)
        sz = min(4, availW);
    if (sz < 3)
        return CRect(0, 0, 0, 0);         // band too narrow to draw anything

    const int left = rc.left + (availW - sz) / 2;
    const int top  = rc.top  + (availH - sz) / 2;

    return CRect(left, top, left + sz, top + sz);
}

void COEditorView::DrawFoldGlyph (CDC& dc, const RECT& rc, bool collapsed)
{
    const CRect box = FoldGlyphBox(rc);
    if (box.IsRectEmpty())
        return;

    const int sz = box.Width();

    dc.FillSolidRect(&box, m_paintAccessories->m_COLOR_BTNFACE);

    CPen*   pOrgPen   = dc.SelectObject(&COEViewPaintAccessories::m_BtnShadowPen);
    CBrush* pOrgBrush = (CBrush*)dc.SelectStockObject(NULL_BRUSH);

    dc.Rectangle(&box);

    const int midX = (box.left + box.right) / 2;
    const int midY = (box.top  + box.bottom) / 2;
    const int leg  = max(sz / 4, 1);

    dc.MoveTo(midX - leg, midY);
    dc.LineTo(midX + leg + 1, midY);        // LineTo excludes its endpoint

    // Collapsed shows [+], expanded [-].
    if (collapsed)
    {
        dc.MoveTo(midX, midY - leg);
        dc.LineTo(midX, midY + leg + 1);
    }

    dc.SelectObject(pOrgBrush);
    dc.SelectObject(pOrgPen);
}

// ------------------------------ Folding interaction ------------------------------

bool COEditorView::InFoldBand (CPoint point) const
{
    if (!m_foldingGutter)
        return false;

    const int bandLeft = m_Rulers[0].m_Indent - EXT_SYN_GUTTER_WIDTH;

    return point.x >= bandLeft && point.x < m_Rulers[0].m_Indent;
}

int COEditorView::RowLineAtPoint (CPoint point)
{
    // Above the first row.
    if (point.y < m_Rulers[1].m_Indent)
        return -1;

    const int rowOfs = (point.y - m_Rulers[1].m_Indent) / m_Rulers[1].m_CharSize;
    const int row    = GetTopmostRow() + rowOfs;

    // RowToLine clamps, so a point below the last row has to be rejected here.
    if (row >= GetVisibleLineCount())
        return -1;

    // A row inside a collapsed run resolves to its anchor - the line carrying the glyph.
    return RowToLine(row);
}

CRect COEditorView::FoldMarkerRect (int line)
{
    const CRect nowhere(0, 0, 0, 0);

    // Cheap gates first: FoldHoverAtPoint calls this on every mouse move.
    if (!m_foldingGutter)
        return nowhere;

    const OpenEditor::FoldModel& folds = GetDocument()->GetFoldModel();

    if (!folds.IsCollapsedAt(line))
        return nowhere;

    // One space past the last visible character, from the same width inputs the paint loop uses.
    OEStringW lineBuff;
    GetLineW(line, lineBuff);

    const int len          = (int)lineBuff.length();
    const int displayWidth = len ? inx2pos(lineBuff.data(), len, len - 1) : 0;
    const int markerPos    = displayWidth + 1;

    const int row = LineToRow(line) - GetTopmostRow();

    CRect rc;
    rc.left   = m_Rulers[0].PosToPix(markerPos);
    rc.right  = m_Rulers[0].PosToPix(markerPos + FOLD_MARKER_LEN);
    rc.top    = m_Rulers[1].m_Indent + row * m_Rulers[1].m_CharSize;
    rc.bottom = rc.top + m_Rulers[1].m_CharSize;

    return rc;
}

bool COEditorView::FoldHoverAtPoint (CPoint point)
{
    // m_foldingGutter alone does not prove a document is attached.
    if (!m_bAttached || !m_foldingGutter)
        return false;

    const bool inBand = InFoldBand(point);

    // Both targets sit at or right of the text edge; reject the rest before reaching the mapper.
    if (!inBand && point.x < m_Rulers[0].m_Indent)
        return false;

    const int line = RowLineAtPoint(point);

    if (line < 0)
        return false;

    // In the band an EXPANDED anchor is a target too: its [-] collapses on click.
    if (inBand)
        return GetDocument()->GetFoldModel().IsAnchorAt(line);

    const CRect marker = FoldMarkerRect(line);

    return !marker.IsRectEmpty() && marker.PtInRect(point);
}

bool COEditorView::ExpandFoldMarkerAtPoint (CPoint point)
{
    // OnLButtonDown's double-click case does not check m_bAttached first.
    if (!m_bAttached || !m_foldingGutter)
        return false;

    // The marker is in the text area, never in the band.
    if (point.x < m_Rulers[0].m_Indent)
        return false;

    const int line = RowLineAtPoint(point);

    if (line < 0)
        return false;

    const CRect marker = FoldMarkerRect(line);

    if (marker.IsRectEmpty() || !marker.PtInRect(point))
        return false;

    // Expand, not toggle: a [...] only exists on a collapsed row.
    GetDocument()->GetFoldModel().SetCollapsedAt(line, false);
    ApplyFoldChange();

    return true;
}

bool COEditorView::ToggleFoldAtPoint (CPoint point)
{
    if (!InFoldBand(point))
        return false;

    const int line = RowLineAtPoint(point);

    if (line < 0)
        return false;

    // Only the anchor row responds; elsewhere the click falls through to gutter line selection.
    OpenEditor::FoldModel& folds = GetDocument()->GetFoldModel();

    if (!folds.IsAnchorAt(line))
        return false;

    folds.ToggleAt(line);
    ApplyFoldChange();

    return true;
}

bool COEditorView::SetFoldCollapsed (int line, bool collapsed)
{
    if (!m_bAttached)
        return false;

    COEDocument* pDoc = GetDocument();

    if (!pDoc)
        return false;

    OpenEditor::FoldModel& folds = pDoc->GetFoldModel();

    // Gate on the anchor, NOT on SetCollapsedAt's return: it hands back the region's
    // NEW state, so expanding returns false even though it just changed something.
    if (!folds.IsAnchorAt(line))
        return false;

    // Already in the requested state - skip the repaint.
    if (folds.IsCollapsedAt(line) == collapsed)
        return false;

    folds.SetCollapsedAt(line, collapsed);
    ApplyFoldChange();

    return true;
}

// --- Outline fold commands: mutate the model, then ONE ApplyFoldChange pass -------

OpenEditor::FoldModel* COEditorView::CommandFoldModel ()
{
    if (!m_bAttached || !m_foldingGutter)
        return NULL;

    COEDocument* pDoc = GetDocument();

    return pDoc ? &pDoc->GetFoldModel() : NULL;
}

bool COEditorView::FoldCollapseCurrentRegion ()
{
    int start = -1, end = -1;

    return GetCurrentFoldRegion(start, end) && SetFoldCollapsed(start, true);
}

bool COEditorView::FoldExpandCurrentRegion ()
{
    int start = -1, end = -1;

    return GetCurrentFoldRegion(start, end) && SetFoldCollapsed(start, false);
}

bool COEditorView::FoldToggleCurrentRegion ()
{
    int start = -1, end = -1;

    if (!GetCurrentFoldRegion(start, end))
        return false;

    // SetFoldCollapsed, not ToggleAt: the shared bookkeeping must run.
    return SetFoldCollapsed(start, !GetDocument()->GetFoldModel().IsCollapsedAt(start));
}

bool COEditorView::FoldExpandCurrentSubtree ()
{
    OpenEditor::FoldModel* pFolds = CommandFoldModel();

    if (!pFolds || !pFolds->ExpandSubtree(GetPosition().line))
        return false;

    ApplyFoldChange();
    return true;
}

bool COEditorView::FoldCollapseAllRegions ()
{
    OpenEditor::FoldModel* pFolds = CommandFoldModel();

    if (!pFolds || pFolds->GetAvailable().empty())
        return false;

    pFolds->CollapseAll();
    ApplyFoldChange();

    return true;
}

bool COEditorView::FoldExpandAllRegions ()
{
    OpenEditor::FoldModel* pFolds = CommandFoldModel();

    if (!pFolds || !pFolds->AnyCollapsed())
        return false;

    pFolds->ExpandAll();
    ApplyFoldChange();

    return true;
}

bool COEditorView::FoldToggleAllRegions ()
{
    OpenEditor::FoldModel* pFolds = CommandFoldModel();

    if (!pFolds)
        return false;

    return pFolds->AnyCollapsed() ? FoldExpandAllRegions() : FoldCollapseAllRegions();
}

bool COEditorView::FoldCollapseToLevel (int level)
{
    OpenEditor::FoldModel* pFolds = CommandFoldModel();

    if (!pFolds || pFolds->GetAvailable().empty())
        return false;

    pFolds->CollapseToLevel(level);
    ApplyFoldChange();

    return true;
}

void COEditorView::OnEditFoldCollapseCurrentRegion () { FoldCollapseCurrentRegion(); }
void COEditorView::OnEditFoldExpandCurrentRegion   () { FoldExpandCurrentRegion();   }
void COEditorView::OnEditFoldToggleCurrentRegion   () { FoldToggleCurrentRegion();   }
void COEditorView::OnEditFoldExpandCurrentSubtree  () { FoldExpandCurrentSubtree();  }
void COEditorView::OnEditFoldCollapseAllRegions    () { FoldCollapseAllRegions();    }
void COEditorView::OnEditFoldExpandAllRegions      () { FoldExpandAllRegions();      }
void COEditorView::OnEditFoldToggleAllRegions      () { FoldToggleAllRegions();      }
void COEditorView::OnEditFoldCollapseToLevel1      () { FoldCollapseToLevel(1);      }
void COEditorView::OnEditFoldCollapseToLevel2      () { FoldCollapseToLevel(2);      }
void COEditorView::OnEditFoldCollapseToLevel3      () { FoldCollapseToLevel(3);      }

void COEditorView::OnUpdate_FoldCurrentRegion (CCmdUI* pCmdUI)
{
    int start = -1, end = -1;

    pCmdUI->Enable(GetCurrentFoldRegion(start, end));
}

void COEditorView::OnUpdate_FoldGroup (CCmdUI* pCmdUI)
{
    OpenEditor::FoldModel* pFolds = CommandFoldModel();

    pCmdUI->Enable(pFolds && !pFolds->GetAvailable().empty());
}

// Last item of the gutter context menu. Written through the settings manager with
// notify = true so the same chain the settings dialogs use runs to completion.
void COEditorView::OnEditFoldToggleEnabled ()
{
    OpenEditor::GlobalSettingsPtr gs =
        COEDocument::GetSettingsManagerForUpdate().GetGlobalSettings();

    if (gs.get())
        gs->SetPlSqlOutlineFoldingEnabled(!gs->GetPlSqlOutlineFoldingEnabled());
}

void COEditorView::OnUpdate_FoldToggleEnabled (CCmdUI* pCmdUI)
{
    // Always enabled: this item is how folding gets switched ON.
    pCmdUI->Enable();
    pCmdUI->SetCheck(GetSettings().GetPlSqlOutlineFoldingEnabled() ? 1 : 0);
}

bool COEditorView::GetCurrentFoldRegion (int& startLine, int& endLine)
{
    startLine = endLine = -1;

    if (!m_bAttached || !m_foldingGutter)
        return false;

    COEDocument* pDoc = GetDocument();
    if (!pDoc)
        return false;

    // ApplyFoldChange keeps the caret on a visible line, so the region is always renderable.
    const OpenEditor::FoldRegion* r =
        pDoc->GetFoldModel().FindEnclosingAvailable(GetPosition().line);

    if (!r)
        return false;

    startLine = r->startLine;
    endLine   = r->endLine;
    return true;
}

// Paint-side gate for the current-region tint only. The fold commands and their
// CCmdUI enablers must keep using GetCurrentFoldRegion, or the highlight switch
// would silently disable them too.
bool COEditorView::GetCurrentFoldHighlight (int& startLine, int& endLine)
{
    startLine = endLine = -1;

    if (!GetSettings().GetPlSqlOutlineFoldingHighlightCurrentRegion())
        return false;

    return GetCurrentFoldRegion(startLine, endLine);
}

void COEditorView::UpdateCurrentFoldHighlight ()
{
    int start = -1, end = -1;
    GetCurrentFoldHighlight(start, end);

    if (start == m_currentFoldRegionStart && end == m_currentFoldRegionEnd)
        return;

    const int oldStart = m_currentFoldRegionStart;
    const int oldEnd   = m_currentFoldRegionEnd;
    m_currentFoldRegionStart = start;
    m_currentFoldRegionEnd   = end;

    // Repaint only the gutter rows the tint moved between - old and new.
    if (oldStart >= 0)
        InvalidateGutterLines(oldStart, oldEnd);
    if (start >= 0)
        InvalidateGutterLines(start, end);
}

void COEditorView::InvalidateGutterLines (int startLine, int endLine)
{
    if (!m_hWnd)
        return;

    // Same gutter rect OnChangedLinesStatus invalidates; hidden lines map onto their anchor row.
    RECT rc;
    rc.left   = 0;
    rc.right  = m_Rulers[0].m_Indent;
    rc.top    = LineToPixY(startLine);
    rc.bottom = LineToPixY(endLine + 1);
    rc.bottom = max(rc.bottom, rc.top + m_Rulers[1].m_CharSize); // never empty
    InvalidateRect(&rc);
}

void COEditorView::ApplyFoldChange ()
{
    // Nothing below may measure a line before this. GetLineMapper() rather than
    // GetMapper(): the latter is const and Invalidate is not.
    GetDocument()->GetLineMapper().Invalidate();

    // Refreshes the mapper's line count from storage; the pixel helpers below assume it.
    GetVisibleLineCount();

    // Pull the caret onto the fold's anchor, at the anchor's EOL - not column 0, which
    // would drag the anchor's own text into or out of a selection depending on which
    // endpoint moved. EOL is also a fixed point of the CursorBeyondEOL clamp.
    // Columnar keeps its COLUMN (the block's geometry) and only remaps the row.
    // Not force=true: that sets m_bDirtyPosition and the first arrow key is eaten.
    Position pos = GetPosition();

    const bool columnar   = (GetBlockMode() == ebtColumn);
    const int  caretLine  = VisibleLine(pos.line);
    bool       caretMoved = false;

    if (caretLine != pos.line)
    {
        pos.line = caretLine;

        if (!columnar)
            pos.column = GetLineLength(caretLine);

        MoveTo(pos);
        caretMoved = true;
    }

    // Corners ride along with the caret, or SelectByCursor's invariant
    // (m_blkPos.end == GetPosition()) breaks and the next Shift+arrow jumps.
    // Each corner is mapped independently: copying `end` from the caret would
    // clobber a Square installed by SetSelection without a matching MoveTo.
    // A corner already on a visible row is left alone, so two corners landing on
    // the same anchor and column clear the selection - it is all inside the fold.
    // AdjustPosition on moved corners applies the same adjustPosByTab MoveTo does.
    if (!IsSelectionEmpty())
    {
        Square blk;
        GetSelection(blk);

        const int startLine = VisibleLine(blk.start.line);
        const int endLine   = VisibleLine(blk.end.line);

        if (startLine != blk.start.line)
        {
            blk.start.line = startLine;

            if (!columnar)
                blk.start.column = GetLineLength(startLine);

            AdjustPosition(blk.start, false);
        }

        if (endLine != blk.end.line)
        {
            blk.end.line = endLine;

            if (!columnar)
                blk.end.column = GetLineLength(endLine);

            AdjustPosition(blk.end, false);
        }

        SetSelection(blk);
    }

    // m_Topmost is a BUFFER line and can itself have just become hidden. The sibling's
    // is snapped here too: AdjustVertSibling only copies ours across when they differ.
    m_Rulers[1].m_Topmost = VisibleLine(m_Rulers[1].m_Topmost);

    COEditorView* sibling = m_Rulers[1].m_sibling;

    if (sibling && sibling->m_hWnd)
        sibling->m_Rulers[1].m_Topmost = sibling->VisibleLine(sibling->m_Rulers[1].m_Topmost);

    // Only scroll the caret back into view if the fold actually moved it: toggling a
    // fold after wheeling away must keep the viewport where the user left it.
    AdjustVertScroller();

    if (caretMoved)
        AdjustCaretPosition();
    else
        SetCaretPosition();

    // Both panes: they share the model and mapper but have their own viewports.
    Invalidate(FALSE);

    if (sibling && sibling->m_hWnd)
        sibling->Invalidate(FALSE);
}

void COEditorView::OnFoldModelChanged ()
{
    // The document rebuilt or cleared the model on an idle tick. All the view owes is a
    // scrollbar extent matching the new visible-ROW count and a repaint - no caret or
    // viewport movement, so ApplyFoldChange is the wrong tool here.
    if (!m_bAttached)
        return;

    AdjustVertScroller();
    Invalidate(FALSE);

    // Refresh the tint cache - the view is fully dirty anyway and a stale cache invalidates wrong rows.
    GetCurrentFoldHighlight(m_currentFoldRegionStart, m_currentFoldRegionEnd);
}

bool COEditorView::RevealLine (int line)
{
    if (!m_foldingGutter)
        return false;

    if (line < 0 || line >= GetLineCount())
        return false;

    OpenEditor::FoldModel& folds = GetDocument()->GetFoldModel();

    if (folds.Empty())
        return false;

    bool changed = false;

    // Innermost first, until nothing hides the line; the counter only guards a malformed
    // region set spinning forever. EnclosingCollapsedAnchor rather than an index: each
    // pass erases from the very collection an index would be into.
    for (int guard = folds.CollapsedCount(); guard >= 0; guard--)
    {
        const int anchor = folds.EnclosingCollapsedAnchor(line);

        if (anchor < 0)
            break;

        folds.SetCollapsedAt(anchor, false);

        changed = true;
    }

    // One ApplyFoldChange for the whole reveal, once the fold state is final.
    if (changed)
        ApplyFoldChange();

    return changed;
}

bool COEditorView::RevealColumnarEditRows ()
{
    // Cheapest gates first: this sits on the keystroke path.
    if (!m_foldingGutter)
        return false;

    if (GetBlockMode() != ebtColumn)
        return false;

    if (!IsFoldingActive())
        return false;

    Square blk;
    GetSelection(blk);
    blk.normalize();

    // A Square's end can be the one-past-EOF line when CursorBeyondEOF is on.
    const int lastLine = GetLineCount() - 1;

    bool changed = false;

    // RevealLine only makes lines visible, so by the time its ApplyFoldChange runs every
    // corner is already on a visible line and the remap is a no-op - a caller's Square
    // does not shift underneath it. One ApplyFoldChange per region OPENED, not per row.
    for (int line = blk.start.line; line <= blk.end.line; line++)
    {
        if (line > lastLine)
            break;

        if (RevealLine(line))
            changed = true;
    }

    return changed;
}



    inline
    int get_text_width (CDC& dc, const wchar_t* str, int len)
    {
        CSize sz;
        GetTextExtentPoint32(dc.m_hDC, str, len, &sz);
        return sz.cx;
    }

inline
void COEditorView::TextOutToScreen (CDC& dc, Ruler rulers[2], int pos, const wchar_t* str, int len, const CRect& rcBar, bool selection)
{
    if (!m_unsupportedAsianChars)
    {
        int expectedWidth =  rulers[0].m_CharSize * len;
        int actualWidth = get_text_width(dc, str, len);
        if (actualWidth - expectedWidth > rulers[0].m_CharSize/3 
        || Common::is_right_to_left_lang(str[0]))
        {
            CRect rc; 
            rc.top = rcBar.top;
            rc.bottom = rcBar.bottom -1;
            for (int charInx = 0; charInx < len; charInx++)
            {
                rc.left = rulers[0].PosToPix(pos + charInx);
                rc.right = rulers[0].PosToPix(pos + charInx + 1) - 1;
                int charWidth = get_text_width(dc, str + charInx, 1);

                if (charWidth - rulers[0].m_CharSize > rulers[0].m_CharSize/3 
                || Common::is_right_to_left_lang(str[charInx]))
                {
                    CBrush* pOrgBrush = (CBrush*)dc.SelectStockObject(NULL_BRUSH);
                    CPen* pOrgdPen = dc.SelectObject(selection
                        ? &m_paintAccessories->m_SelTextForegroundPen
                        : &m_paintAccessories->m_TextForegroundPen);
                    dc.Rectangle(CRect(rc.left + 1, rc.top + 2, rc.right - 1, rc.bottom - 2));
                    dc.SelectObject(pOrgdPen);
                    dc.SelectObject(pOrgBrush);
                }
                else
                    dc.TextOut(rulers[0].PosToPix(pos + charInx), rcBar.top, str + charInx, 1);
            }
        }
        else if (actualWidth != expectedWidth)
        {
            for (int charInx = 0; charInx < len; charInx++)
                TextOut(dc.m_hDC, rulers[0].PosToPix(pos + charInx), rcBar.top, str + charInx, 1);
        }
        else
            TextOut(dc.m_hDC, rulers[0].PosToPix(pos), rcBar.top, str, len);
    }
    else
        TextOut(dc.m_hDC, rulers[0].PosToPix(pos), rcBar.top, str, len);
}

CRect COEditorView::RegionRowRect (const CRect& rcRow, const CRect& rcLine, int line,
                                   int firstLine, int lastLine, int firstColumn, int lastColumn) const
{
    CRect rc = rcRow;   // the row's top/bottom
    rc.left  = (line == firstLine)
             ? m_Rulers[0].InxToPix(firstColumn - m_Rulers[0].m_Topmost)
             : rcLine.left;
    rc.right = (line == lastLine)
             ? m_Rulers[0].InxToPix(lastColumn - m_Rulers[0].m_Topmost)
             : rcLine.right;
    return rc;
}

void COEditorView::OnPaint ()
{
    CPaintDC dc(this);

    if (dc.m_ps.rcPaint.left == dc.m_ps.rcPaint.right
    || dc.m_ps.rcPaint.top == dc.m_ps.rcPaint.bottom)
        return;

    if (!m_bAttached)
    {
        ::FillRect(dc, &dc.m_ps.rcPaint, (HBRUSH)GetStockObject(WHITE_BRUSH));
        return;
    }

    int maxLineLength = 0;
    const Settings& settings = GetSettings();
    bool locked = IsLocked() && m_bPaleIfLocked;

    // Row space. GetVisibleLineCount() first on purpose: it refreshes the mapper's line
    // count from storage, and a paint can arrive before the idle tick that would have.
    const int visibleLines = GetVisibleLineCount();
    const int topRow       = GetTopmostRow();

    Square scr;
    scr.start.line   = max(0, (int)(dc.m_ps.rcPaint.top - m_Rulers[1].m_Indent) / m_Rulers[1].m_CharSize);
    scr.end.line     = max(0, (int)(dc.m_ps.rcPaint.bottom - m_Rulers[1].m_Indent - 1) / m_Rulers[1].m_CharSize);
    scr.start.column = max(0, (int)(dc.m_ps.rcPaint.left - m_Rulers[0].m_Indent) / m_Rulers[0].m_CharSize);
    scr.end.column   = max(0, (int)(dc.m_ps.rcPaint.right - m_Rulers[0].m_Indent - 1) / m_Rulers[0].m_CharSize);

    if (dc.m_ps.rcPaint.left < m_Rulers[0].m_Indent)
    {
        CRect rc(dc.m_ps.rcPaint);
        rc.right = m_Rulers[0].m_Indent;
        rc.top   = m_Rulers[1].m_Indent;
        DrawVertRuler(dc, rc, scr.start.line, scr.end.line+1, settings.GetLineNumbers());
    }

    if (dc.m_ps.rcPaint.top < m_Rulers[1].m_Indent)
    {
        CRect rc(dc.m_ps.rcPaint);
        rc.bottom = m_Rulers[1].m_Indent;
        DrawHorzRuler(dc, rc, scr.start.column, scr.end.column+1);
    }

    // ">" not ">=": OnChangedLinesStatus invalidates a gutter-only rect whose right edge
    // is exactly m_Rulers[0].m_Indent, and that case has no text rows to paint.
    if (dc.m_ps.rcPaint.right > m_Rulers[0].m_Indent
    && dc.m_ps.rcPaint.bottom >= m_Rulers[1].m_Indent)
    {
        Square blk;
        GetSelection(blk);
        bool blockExist    = !blk.is_empty();
        EBlockMode blkMode = GetBlockMode();

        if (blockExist)
        {
            blk.normalize();

            // A selection is stored in buffer lines but compared against `i`, a row.
            blk.start.line    = LineToRow(blk.start.line) - topRow;
            blk.end.line      = LineToRow(blk.end.line)   - topRow;
            blk.start.column -= m_Rulers[0].m_Topmost;
            blk.end.column   -= m_Rulers[0].m_Topmost;

            if (blkMode == ebtColumn)
                blk.normalize_columns();
        }

        blockExist = blockExist && _is_block_visible(scr, blk, blkMode);

        HighlighterBase* phighlighter = m_bSyntaxHighlight ? m_highlighter.get() : 0;

        LineTokenizer tokenizer(
            settings.GetVisibleSpaces(),
            GetTabSpacing(), 
            phighlighter ? phighlighter->GetDelimiters() : GetDelimiters()
            );

        if (phighlighter)
        {
            int status(0), quoteId(0);
            bool parsing(false);
            // qClose carries the delimiter of a q-literal opened above the visible range.
            wchar_t qClose(0);
            ScanMultilineQuotes(RowToLine(topRow + scr.start.line), status, quoteId, parsing, qClose);
            phighlighter->SetMultilineQuotesState(status, quoteId, parsing, qClose);
        }

        CRect rcLine, rcBar;
        rcLine.top    = rcBar.top    = 0;
        rcLine.bottom = rcBar.bottom = m_Rulers[1].m_CharSize;
        rcLine.left   = max(dc.m_ps.rcPaint.left,  (long)m_Rulers[0].m_Indent);
        rcLine.right  = max(dc.m_ps.rcPaint.right, (long)m_Rulers[0].m_Indent);

        CDC memDc, memSelDc;
        CBitmap bitmap, selBitmap;
        CBitmap *pOrgBitmap = 0, *pOrgSelBitmap = 0;

        memDc.CreateCompatibleDC(&dc);
        bitmap.CreateCompatibleBitmap(&dc, rcLine.right - rcLine.left, m_Rulers[1].m_CharSize);
        pOrgBitmap = memDc.SelectObject(&bitmap);
        memDc.SetBkMode(TRANSPARENT);
        memDc.SetViewportOrg(-rcLine.left, 0);

        CPen* pOrgdMemDcPen = memDc.SelectObject(&m_paintAccessories->m_RulerPen);
        memDc.SetROP2(R2_NOTXORPEN);

        bool columnMarkers = settings.GetColumnMarkers();
        const vector<int>& columnMarkersSet = settings.GetColumnMarkersSet();
        bool indentGuide = settings.GetIndentGuide();
        int indentSpacing = GetIndentSpacing();

        if (blockExist)
        {
            memSelDc.CreateCompatibleDC(&dc);
            selBitmap.CreateCompatibleBitmap(&dc, rcLine.right - rcLine.left, m_Rulers[1].m_CharSize);
            pOrgSelBitmap = memSelDc.SelectObject(&selBitmap);
            memSelDc.SetBkMode(TRANSPARENT);
            memSelDc.SetTextColor(m_paintAccessories->m_SelTextForeground);
            memSelDc.SetViewportOrg(-rcLine.left, 0);
        }

        if (!phighlighter)
        {
            m_paintAccessories->SelectTextFont(memDc);
            memDc.SetTextColor(m_paintAccessories->m_TextForeground);

            if (blockExist)
            {
                m_paintAccessories->SelectTextFont(memSelDc);
                // 29.06.2003 bug fix, selected text foreground color cannot be changed for "Text" class
                memSelDc.SetTextColor(m_paintAccessories->m_SelTextForeground);
            }
        }

        // The viewport row EOF sits on: one past the last visible row.
        int endFileLine = visibleLines - topRow;
        int curLine     = LineToRow(GetPosition().line) - topRow;

        // -1 is HideHighlighting's "no range" sentinel and must NOT go through the
        // mapper: BufferToVisual clamps negatives to row 0.
        const int hlFirstRow = m_nFistHighlightedLine < 0 ? m_nFistHighlightedLine
                                                          : LineToRow(m_nFistHighlightedLine) - topRow;
        const int hlLastRow  = m_nLastHighlightedLine  < 0 ? m_nLastHighlightedLine
                                                          : LineToRow(m_nLastHighlightedLine)  - topRow;

        // Column-limited band (the Square overload of SetHighlighting); -1 keeps the
        // legacy full-width path.
        const bool hlColumnar = m_nFistHighlightedColumn >= 0;

        // The dialog's scope region in rows; -1 is the "no region" sentinel, as for the band above.
        const int scopeFirstRow = m_scopeRegionValid ? LineToRow(m_scopeRegion.start.line) - topRow : -1;
        const int scopeLastRow  = m_scopeRegionValid ? LineToRow(m_scopeRegion.end.line)   - topRow : -1;

        // Fold markers. AnyCollapsed once per paint, not per row; null while folding is
        // off so the row loop below pays one pointer test.
        const OpenEditor::FoldModel* pFoldMarks = 0;
        if (m_foldingGutter)
        {
            const OpenEditor::FoldModel& folds = GetDocument()->GetFoldModel();
            if (folds.AnyCollapsed())
                pFoldMarks = &folds;
        }

        // Re-seed the lexical state across a fold boundary: NextLine only ever sees
        // VISIBLE rows, so a fold hiding a multi-line construct's closing delimiter
        // leaves the highlighter stuck inside it for every row below. When two
        // consecutive rows are not consecutive BUFFER lines, read the state from
        // storage instead. -1 is "nothing painted yet", so the first row keeps the
        // seed from above.
        int prevPaintedLine = -1;

        // Overlay cursor. Squares are in scan order and the row loop ascends, so one
        // never-rewinding index gives O(hits) per paint, not O(rows*hits).
        size_t ovlNext = 0;

        for (int i(scr.start.line); i <= scr.end.line; i++)
        {
            int firstWordPos = -1;
            // Display width of the row's line, hoisted so the fold marker can reuse it.
            int lineDisplayWidth = 0;

            // Rows below EOF clamp to the last visible line, so every use of it stays
            // behind the endFileLine test.
            const int line = RowToLine(topRow + i);

            COLORREF color = 0;

            if (i >= hlFirstRow && i <= hlLastRow && !hlColumnar)
                color = m_paintAccessories->m_HighlightingBackground;
            else
                if (i == curLine)
                    color = m_paintAccessories->m_CurrentLineBackground;
                else
                    color = m_paintAccessories->m_TextBackground;

            memDc.FillSolidRect(&rcLine, color);

            // Column-limited band and the scope region: separate layers painted after the base fill.
            // Rows below EOF are out - their clamped `line` could repaint a region on empty rows.
            if (hlColumnar && i >= hlFirstRow && i <= hlLastRow && i < endFileLine)
            {
                CRect rcHl = RegionRowRect(rcBar, rcLine, line, m_nFistHighlightedLine, m_nLastHighlightedLine,
                                           m_nFistHighlightedColumn, m_nLastHighlightedColumn);
                if (rcHl.right > rcHl.left)
                    memDc.FillSolidRect(&rcHl, m_paintAccessories->m_HighlightingBackground);
            }

            if (m_scopeRegionValid && i >= scopeFirstRow && i <= scopeLastRow && i < endFileLine)
            {
                CRect rcScope = RegionRowRect(rcBar, rcLine, line, m_scopeRegion.start.line, m_scopeRegion.end.line,
                                              m_scopeRegion.start.column, m_scopeRegion.end.column);
                if (rcScope.right > rcScope.left)
                    memDc.FillSolidRect(&rcScope, m_paintAccessories->m_HighlightingBackground);
            }

            for (int j(0); j < sizeof(m_braceHighlighting.line)/sizeof(m_braceHighlighting.line[0]); j++)
                if (m_braceHighlighting.line[j] != -1
                && (LineToRow(m_braceHighlighting.line[j]) - topRow) == i)
                {
                    rcBar.left   = m_Rulers[0].InxToPix(m_braceHighlighting.offset[j] - m_Rulers[0].m_Topmost);
                    rcBar.right  = m_Rulers[0].InxToPix(m_braceHighlighting.offset[j] - m_Rulers[0].m_Topmost + m_braceHighlighting.length[j]);
                    if (!m_braceHighlighting.broken)
                        memDc.FillSolidRect(&rcBar, m_paintAccessories->m_HighlightingBackground);
                    else
                        memDc.FillSolidRect(&rcBar, m_paintAccessories->m_ErrorHighlightingBackground);
                }

            // "Highlight all occurrences" overlay: per-row rects filled before the text
            // pass so glyphs land on top; stale squares drop lazily, folded matches have no row.
            size_t ovlRowFirst = 0, ovlRowLast = 0;   // this row's squares, for the tint below

            if (m_matchOverlayValid && !m_matchOverlay.empty() && i < endFileLine)
            {
                if (GetActionId() == m_matchOverlayActionId)
                {
                    while (ovlNext < m_matchOverlay.size() && m_matchOverlay[ovlNext].end.line < line)
                        ++ovlNext;

                    ovlRowFirst = ovlNext;

                    for (size_t k = ovlNext;
                         k < m_matchOverlay.size() && m_matchOverlay[k].start.line <= line;
                         ++k)
                    {
                        const OpenEditor::Square& sq = m_matchOverlay[k];
                        ovlRowLast = k + 1;

                        if (sq.start.line != line || sq.end.line != line)
                            continue;   // the searcher is line-based; guard only

                        CRect rcOvl = rcBar;   // the row's top/bottom
                        rcOvl.left  = m_Rulers[0].InxToPix(sq.start.column - m_Rulers[0].m_Topmost);
                        rcOvl.right = m_Rulers[0].InxToPix(sq.end.column   - m_Rulers[0].m_Topmost);
                        if (rcOvl.right > rcOvl.left)
                            memDc.FillSolidRect(&rcOvl, m_paintAccessories->m_FindMatchBackground);
                    }
                }
                else
                    m_matchOverlayValid = false;   // edited since collection: stale, drop it
            }

            if (blockExist && blk.line_in_rect(i))
            {
                switch (blkMode)
                {
                case ebtColumn:
                    rcBar.left   = m_Rulers[0].InxToPix(blk.start.column);
                    rcBar.right  = m_Rulers[0].InxToPix(blk.end.column);
                    break;
                case ebtStream:
                default: // because of warning
                    if (blk.start.line == i)
                        rcBar.left = m_Rulers[0].InxToPix(blk.start.column);
                    else
                        rcBar.left = rcLine.left;

                    if (blk.end.line == i)
                        rcBar.right = m_Rulers[0].InxToPix(blk.end.column);
                    else
                        rcBar.right = rcLine.right;
                    break;
                }
                if (rcBar.left == rcBar.right) rcBar.right++; // make it visible

                memSelDc.FillSolidRect(&rcBar, m_paintAccessories->m_SelTextBackground);
            }
            else
                rcBar.left = rcBar.right = -1;

            if (i < endFileLine)
            {
                OEStringW lineBuff;
                GetLineW(line, lineBuff);
                const wchar_t* currentLine = lineBuff.data(); 
                int currentLineLength = lineBuff.length();

                // recalculation for maximum of visible line lengthes
                if (currentLineLength)
                {
                    // No tab => column == index, so wmemchr beats inx2pos's per-character
                    // branch. -1 yields the column OF the last character, as inx2pos does.
                    lineDisplayWidth = wmemchr(currentLine, L'\t', currentLineLength)
                                     ? inx2pos(currentLine, currentLineLength, currentLineLength - 1)
                                     : currentLineLength - 1;

                    if (maxLineLength < lineDisplayWidth)
                        maxLineLength = lineDisplayWidth;
                }

                if (phighlighter)
                {
                    // Fold-boundary re-seed, see prevPaintedLine. Before NextLine so the
                    // line is tokenized from the state storage says it starts in. Updated
                    // only inside the endFileLine test: `line` clamps below EOF and a
                    // clamped line would fake a boundary on the next paint.
                    if (prevPaintedLine >= 0 && line != prevPaintedLine + 1)
                    {
                        int status(0), quoteId(0);
                        bool parsing(false);
                        wchar_t qClose(0); // q-literal delimiter, see COEditorView::Paint
                        ScanMultilineQuotes(line, status, quoteId, parsing, qClose);
                        phighlighter->SetMultilineQuotesState(status, quoteId, parsing, qClose);
                    }
                    prevPaintedLine = line;

                    phighlighter->NextLine(currentLine, currentLineLength);
                    tokenizer.EnableProcessSpaces(phighlighter->IsSpacePrintable() || m_paintAccessories->m_TextFont & 0x04);
                }
                else
                {
                    tokenizer.EnableProcessSpaces(m_paintAccessories->m_TextFont & 0x04 ? true : false);
                }

                // Column >= index always, so nothing at or beyond lastVisibleColumn can
                // reach the screen; the word loop culls those tokens anyway, but only
                // after tokenizing them. Bounding the scan rather than breaking the loop
                // keeps firstWordPos and the len clamp intact.
                //
                // Plain text only: with a highlighter the walk also derives the next
                // row's state, so cutting it early would hide a multiline literal
                // opened to the right of the viewport.
                int scanLength = currentLineLength;

                if (!phighlighter)
                {
                    const int lastVisibleColumn = scr.end.column + m_Rulers[0].m_Topmost;
                    const int scanLimit = lastVisibleColumn + 1 + m_Rulers[0].m_Count + 2;

                    // > 0 doubles as an overflow guard: on wrap the bound is simply
                    // not applied and the row scans the full line - slow, never wrong.
                    if (scanLimit > 0 && scanLimit < scanLength)
                        scanLength = scanLimit;
                }

                tokenizer.StartScan(currentLine, scanLength);

                while (!tokenizer.Eol())
                {
                    int pos, len;
                    const wchar_t* str;

                    tokenizer.GetCurentWord(str, pos, len);

                    if (firstWordPos == -1) 
                        firstWordPos = pos;

                    if (phighlighter)
                    {
                        phighlighter->NextWord(str, len, pos);
                        tokenizer.EnableProcessSpaces(phighlighter->IsSpacePrintable() || m_paintAccessories->m_TextFont & 0x04);
                    }

                    if (pos <= scr.end.column + m_Rulers[0].m_Topmost)
                    {
                        if (pos + len > scr.start.column + m_Rulers[0].m_Topmost
                        && pos <= scr.end.column + m_Rulers[0].m_Topmost)
                        {
                            // bug fix, very long lines w/o space or delimiters are shown as blank
                            if (pos < m_Rulers[0].m_Topmost)
                            {
                                int shift = m_Rulers[0].m_Topmost - pos;
                                pos += shift;
                                str += shift;
                                len -= shift;
                                ASSERT(len >= 0);
                                if (len < 0) 
                                    len = 0;
                            }
                            if (len > m_Rulers[0].m_Count+1)
                                len = m_Rulers[0].m_Count+1;

                            if (!blockExist || (rcBar.left == rcBar.right)
                            || !(pos >= blk.start.column + m_Rulers[0].m_Topmost
                              && pos + len <= blk.end.column + m_Rulers[0].m_Topmost))
                            {
                                if (phighlighter)
                                {
                                    m_paintAccessories->SelectFontEx(memDc, phighlighter->GetFontIndex());
                                    memDc.SetTextColor(phighlighter->GetTextColor());
                                }

                                // Highligting MatchingText {
                                if (m_highlightedText.length() > 0
                                && (int)m_highlightedText.length() == len
                                && !wcsnicmp(m_highlightedText.c_str(), str, len))
                                {
                                    CRect rc = rcBar;
                                    rc.left = m_Rulers[0].PosToPix(pos);
                                    rc.right = m_Rulers[0].PosToPix(pos + len);
                                    memDc.FillSolidRect(&rc, m_paintAccessories->m_HighlightingBackground);

                                    // Paint priority: the find-match overlay wins over this coarser
                                    // tint - re-fill the row's squares the token covers (glyphs come last).
                                    for (size_t k = ovlRowFirst; k < ovlRowLast; ++k)
                                    {
                                        const OpenEditor::Square& sq = m_matchOverlay[k];

                                        if (sq.start.line != line || sq.end.line != line)
                                            continue;

                                        const int from = max(pos, sq.start.column);
                                        const int to   = min(pos + len, sq.end.column);

                                        if (to > from)
                                        {
                                            CRect rcOvl = rcBar;
                                            rcOvl.left  = m_Rulers[0].PosToPix(from);
                                            rcOvl.right = m_Rulers[0].PosToPix(to);
                                            memDc.FillSolidRect(&rcOvl, m_paintAccessories->m_FindMatchBackground);
                                        }
                                    }
                                }
                                // Highligting MatchingText }
                                
                                TextOutToScreen(memDc, m_Rulers, pos, str, len, rcBar, false);
                            }

                            if (blockExist && (rcBar.left != rcBar.right)
                                && ((blkMode != ebtColumn)
                                    || ((blkMode == ebtColumn)
                                        && pos + len > blk.start.column + m_Rulers[0].m_Topmost
                                        && pos <= blk.end.column + m_Rulers[0].m_Topmost
                                       )
                                   )
                               )
                            {
                                if (phighlighter)
                                {
                                    m_paintAccessories->SelectFontEx(memSelDc, phighlighter->GetFontIndex());
                                }
                                //TextOut(memSelDc.m_hDC, m_Rulers[0].PosToPix(pos), rcLine.top, str, len);
                                TextOutToScreen(memSelDc, m_Rulers, pos, str, len, rcBar, true);
                            }
                        }
                    }
                    tokenizer.Next();
                }
            }
            else if (i == endFileLine && settings.GetEOFMark())
            {
                static const char str[] = ">> EOF <<";

                if (sizeof(str)-1 > m_Rulers[0].m_Topmost)
                {
                    m_paintAccessories->SelectTextFont(memDc);
                    memDc.SetTextColor(m_paintAccessories->m_EOFForeground);
                    TextOutA(memDc.m_hDC, m_Rulers[0].PosToPix(0), rcLine.top, str, sizeof(str)-1);

                    if (blockExist)
                    {
                        m_paintAccessories->SelectTextFont(memSelDc);
                        TextOutA(memSelDc.m_hDC, m_Rulers[0].PosToPix(0), rcLine.top, str, sizeof(str)-1);
                    }
                }
            }

            if (rcBar.left != rcBar.right)
            {
                int width = rcBar.right - rcBar.left;
                if (blkMode == ebtStream && i < endFileLine
                && !GetCursorBeyondEOL())
                {
                    int lineLength = GetLineLength(line);

                    if (blk.start.line == blk.end.line
                    && rcBar.left >= m_Rulers[0].PosToPix(lineLength))
                    {
                        width = 0;
                    }
                    else
                    {
                        rcBar.left = min(rcBar.left, (long)m_Rulers[0].PosToPix(lineLength));
                        width = min(rcBar.right, (long)m_Rulers[0].PosToPix(lineLength + 1)) - rcBar.left;
                    }
                }
                if (width)
                    memDc.BitBlt(rcBar.left, 0,
                                 width, m_Rulers[1].m_CharSize,
                                 &memSelDc, rcBar.left, 0, SRCCOPY);
            }

            // The [...] of a collapsed anchor, drawn AFTER the selection overlay so a
            // selected anchor row does not wipe its own marker. Straight onto memDc, not
            // through the tokenizer loop: it is chrome, not document text.
            //
            // Font and colour are restored, unlike ">> EOF <<" above - this is not the
            // last row, and a leaked colour would tint every row below it.
            if (pFoldMarks && i < endFileLine)
            {
                if (pFoldMarks->IsCollapsedAt(line))
                {
                    // One space past the last visible character of the anchor.
                    const int markerPos = lineDisplayWidth + 1;

                    CFont*   pOrgFont  = m_paintAccessories->SelectTextFont(memDc);
                    COLORREF orgColor  = memDc.SetTextColor(m_paintAccessories->m_EOFForeground);

                    TextOutToScreen(memDc, m_Rulers, markerPos,
                                    FOLD_MARKER_TEXT, FOLD_MARKER_LEN, rcBar, false);

                    memDc.SetTextColor(orgColor);
                    if (pOrgFont)
                        memDc.SelectObject(pOrgFont);
                }
            }

            if (columnMarkers)
            {
                memDc.SelectObject(&m_paintAccessories->m_RulerPen);
                for (vector<int>::const_iterator it = columnMarkersSet.begin();
                    it != columnMarkersSet.end();
                    ++it)
                {
                    memDc.MoveTo(m_Rulers[0].PosToPix(*it), 0);
                    memDc.LineTo(m_Rulers[0].PosToPix(*it), m_Rulers[1].m_CharSize);
                }
            }

            if (indentGuide
            && firstWordPos > indentSpacing)
            {
                memDc.SelectObject(&m_paintAccessories->m_IndentGuidelinePen);
                for (int pos = indentSpacing; pos < firstWordPos /*+ m_Rulers[0].m_Topmost*/; pos += indentSpacing)
                {
                    memDc.MoveTo(m_Rulers[0].PosToPix(pos), -m_Rulers[1].InxToPix(i) % 2);
                    memDc.LineTo(m_Rulers[0].PosToPix(pos), m_Rulers[1].m_CharSize);
                }
            }

            if (locked)
            {
                /* Alpha blending -  for read only painting */
                static BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255-32, 0/*AC_SRC_ALPHA*/};

                dc.FillSolidRect(rcLine.left, m_Rulers[1].InxToPix(i),
                        rcLine.right - rcLine.left, m_Rulers[1].m_CharSize,
                        RGB(95,95,95) 
                    );

                dc.AlphaBlend(rcLine.left, m_Rulers[1].InxToPix(i),
                        rcLine.right - rcLine.left, m_Rulers[1].m_CharSize,
                        &memDc, rcLine.left, 0, rcLine.right - rcLine.left, m_Rulers[1].m_CharSize,
                        blend
                    );
            }
            else
                dc.BitBlt(rcLine.left, m_Rulers[1].InxToPix(i),
                        rcLine.right - rcLine.left, m_Rulers[1].m_CharSize,
                        &memDc, rcLine.left, 0, SRCCOPY);
        }

        memDc.SelectObject(pOrgBitmap);
        memDc.SelectObject(pOrgdMemDcPen);

        if (blockExist && pOrgSelBitmap)
            memSelDc.SelectObject(pOrgSelBitmap);
    }

    if (m_nMaxLineLength < maxLineLength)
    {
        m_nMaxLineLength = maxLineLength;
        AdjustHorzScroller();
    }
}

void COEditorView::OnSettingsChanged ()
{
    const Settings& settings = GetSettings();
    string language = settings.GetLanguage();
    const VisualAttributesSet& set = settings.GetVisualAttributesSet();

    m_syntaxGutter = settings.GetSyntaxGutter();
    m_unsupportedAsianChars = settings.GetUnsupportedAsianChars();
    m_foldingGutter = GetDocument() && GetDocument()->IsFoldingEnabled();

    GetCurrentFoldHighlight(m_currentFoldRegionStart, m_currentFoldRegionEnd);

    ResetAutoPairs();

    m_paintAccessories = COEViewPaintAccessories::GetPaintAccessories(language);
    m_paintAccessories->OnSettingsChanged(this, set);

    m_highlighter = HighlighterFactory::CreateHighlighter(language);
    
    if (m_highlighter.get())
    {
        m_highlighter->InitStorageScanner(GetMultilineQuotesScanner());
        m_highlighter->Init(set);
    }

    const VisualAttribute& textAttr = set.FindByName("Text");
    const VisualAttribute& curLinetAttr = set.FindByName("Current Line");
    HighlightCurrentLine(textAttr.m_Background != curLinetAttr.m_Background ? true : false);

    bool recalcSize = false;

    if (m_Rulers[0].m_CharSize != m_paintAccessories->m_CharSize.cx
    || m_Rulers[1].m_CharSize != m_paintAccessories->m_CharSize.cy)
    {
        m_Rulers[0].m_CharSize = m_paintAccessories->m_CharSize.cx;
        m_Rulers[1].m_CharSize = m_paintAccessories->m_CharSize.cy;
        recalcSize = true;
    }

    int leftMargin = CalculateLeftMargin();

    if (m_Rulers[0].m_Indent != leftMargin)
    {
        m_Rulers[0].m_Indent = leftMargin;
        recalcSize = true;
    }

// ruler
    int rulerHight = settings.GetRuler() ? m_paintAccessories->m_RulerCharSize.cy + 4 : 0;
    if (m_Rulers[1].m_Indent != rulerHight)
    {
        m_Rulers[1].m_Indent = rulerHight;
        recalcSize = true;
    }

    if (recalcSize)
    {
        CRect rc;
        GetClientRect(rc);
        DoSize(SIZE_RESTORED, rc.Width(), rc.Height());
        ShowCaret(::GetFocus() == m_hWnd, true/*dont_destroy_caret*/);
        AdjustCaretPosition();
    }

    Invalidate(FALSE);
}
