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

/***************************************************************************/
/*      Purpose: Text data source implementation for grid component        */
/*      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~           */
/*      (c) 1996-1999,2003 Aleksey Kochetov                                */
/***************************************************************************/
// 16.12.2004 (Ken Clubok) Add CSV prefix option
// 09.09.2011 bug fix, grid painting is extreamally slow 
//            if grid is loaded with very long strings (> 1mb) 
// 2017-11-28 implemented range selection
// 2017-11-28 added 3 new export formats

#include "stdafx.h"
#include <sstream>
#include <COMMON/SimpleDragDataSource.h>         // for Drag & Drop
#include "GridSourceBase.h"
#include "CellEditor.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace OG2
{
    using Common::nvector;

    ETextFormat IGridTextSource::m_OutputFormat = etfPlainText;
    char IGridTextSource::m_FieldDelimiterChar = ',';
    char IGridTextSource::m_QuoteChar          = '"';
    char IGridTextSource::m_QuoteEscapeChar    = '"';
	char IGridTextSource::m_PrefixChar         = '=';
    bool IGridTextSource::m_OutputWithHeader   = false;
    bool IGridTextSource::m_JsonPretty         = true;    // JD13
    string IGridTextSource::m_XmlRootElement     ("data");
    string IGridTextSource::m_XmlRecordElement   ("record");
    int    IGridTextSource::m_XmlFieldElementCase = 1;

GridSourceBase::GridSourceBase ()
: m_ColumnInfo("GridSourceBase::m_ColumnInfo")
{
    SetFixSize(eoVert, edHorz, 6);
    SetFixSize(eoVert, edVert, 1);
    SetFixSize(eoHorz, edHorz, 30);
    SetFixSize(eoHorz, edVert, 1);
    m_Editor = 0;
    m_editRow = m_editCol = -1;
    m_ShowFixCol = m_ShowFixRow =
    m_ShowRowEnumeration = true;
    m_ShowTransparentFixCol = false;
    m_MaxShowLines = 1;
    m_ImageList = 0;
    m_bDeleteImageList = false;
    m_Orientation = eoVert;//eoHorz;
}

GridSourceBase::~GridSourceBase ()
{
    try { EXCEPTION_FRAME;

    if (m_bDeleteImageList)
        delete m_ImageList;
    }
    _DESTRUCTOR_HANDLER_;
}

// Cell management
int GridSourceBase::GetFirstFixCount (eDirection dir) const
{
    return (dir == edHorz && m_ShowFixCol || dir == edVert && m_ShowFixRow) ? 1 : 0;
}

int GridSourceBase::GetLastFixCount (eDirection) const
{
    return 0;
}

int GridSourceBase::GetSize (int pos, eDirection dir) const
{
    if (IsTableOrientation() && dir == edHorz)
        return GetColCharWidth(pos);
    else if (IsFormOrientation() && dir == edVert)
        return GetColCharHeight(pos);

    return 1;
}

namespace
{
    // Form mode's fixed strip is a field-name column, not a row gutter. 60 matches the export cap.
    const int FORM_NAME_COL_MIN_CHARS = 8;
    const int FORM_NAME_COL_MAX_CHARS = 60;
}

int GridSourceBase::GetFirstFixSize (int pos, eDirection dir) const
{
    _ASSERTE(!pos);

    if (IsFormOrientation() && dir == edHorz)
    {
        int width = FORM_NAME_COL_MIN_CHARS;

        for (int col = 0; col < GetColumnCount(); ++col)
            width = max(width, static_cast<int>(GetColHeader(col).size()));

        return min(width, FORM_NAME_COL_MAX_CHARS);
    }

    return GetFixSize(m_Orientation, dir);
}

int GridSourceBase::GetLastFixSize (int, eDirection) const
{
    _ASSERTE(0);
    return 1;
}

namespace
{
    // Sort-glyph metrics, all derived from the header font so the arrow scales with it. Clamped at
    // both ends: a tiny font still leaves a visible mark, a huge one must not swallow the header.
    const int SORT_GLYPH_MARGIN = 3;   // clear space either side of the arrow, in device pixels

    int SortGlyphHeight (const DrawCellContexts& dcc)
    {
        const int h = dcc.m_CharSize[edVert] / 3;
        return h < 3 ? 3 : (h > 9 ? 9 : h);
    }

    int SortGlyphWidth (const DrawCellContexts& dcc)
    {
        return SortGlyphHeight(dcc) * 2 - 1;
    }
}

bool GridSourceBase::HeaderCarriesSortGlyph (const DrawCellContexts& dcc) const
{
    // Table orientation only. Rotating does not clear a sort, but the column-header band then
    // carries the table's column names, so an arrow there would point at the wrong thing.
    if (!IsTableOrientation())
        return false;

    const int sorted = GetSortColumn();
    return 0 <= sorted && sorted == dcc.m_Col;
}

int GridSourceBase::SortGlyphReserve (const DrawCellContexts& dcc) const
{
    return SortGlyphWidth(dcc) + 2 * SORT_GLYPH_MARGIN;
}

void GridSourceBase::DrawSortGlyph (DrawCellContexts& dcc) const
{
    const int h = SortGlyphHeight(dcc);
    const int w = SortGlyphWidth(dcc);

    // Right-aligned in the header cell and vertically centred on it, clear of the frame that
    // DrawDecFrame already put down the right-hand edge.
    const int cy   = dcc.m_Rect.CenterPoint().y;
    const int top  = cy - h / 2;
    const int left = dcc.m_Rect.right - SORT_GLYPH_MARGIN - w;

    // Filled with the header's own text colour and stroked with a pen of the same colour, because
    // Polygon outlines as well as fills. Leaving the caller's pen puts a 1px fringe round the arrow.
    CBrush  brush(dcc.m_FixTextColor);
    CPen    pen(PS_SOLID, 1, dcc.m_FixTextColor);
    CBrush* oldBrush = dcc.m_Dc->SelectObject(&brush);
    CPen*   oldPen   = dcc.m_Dc->SelectObject(&pen);

    // Ascending draws apex DOWN, descending apex UP - the Explorer convention, chosen deliberately.
    // The flag is not a rendering choice: it is the same `ascending` ValueTable::Sort orders rows with.
    POINT pts[3];
    if (GetSortAscending())
    {
        pts[0].x = left;         pts[0].y = top;          // apex down
        pts[1].x = left + w - 1; pts[1].y = top;
        pts[2].x = left + w / 2; pts[2].y = top + h - 1;
    }
    else
    {
        pts[0].x = left + w / 2; pts[0].y = top;          // apex up
        pts[1].x = left + w - 1; pts[1].y = top + h - 1;
        pts[2].x = left;         pts[2].y = top + h - 1;
    }
    dcc.m_Dc->Polygon(pts, 3);

    dcc.m_Dc->SelectObject(oldBrush);
    dcc.m_Dc->SelectObject(oldPen);
}

void GridSourceBase::PaintCell (DrawCellContexts& dcc) const
{
    CPen* oldPen;
    CRect _rc = dcc.m_Rect;
    _rc.InflateRect(dcc.m_CellMargin);
    int bkClrIndex = IsTableOrientation() ? dcc.m_Row & 1 : dcc.m_Col & 1;

    if (dcc.IsDataCell())
    {
        if (IsEmpty() || dcc.m_Select == esNone)
        {
            dcc.m_Dc->FillRect(dcc.m_Rect, &dcc.m_BkBrush[bkClrIndex]);
            dcc.m_Dc->SetBkColor(dcc.m_BkColor[bkClrIndex]);
            dcc.m_Dc->SetTextColor(dcc.m_TextColor);
        }
        else if (dcc.m_Select == esRect)
        {
            dcc.m_Dc->SetBkColor(dcc.m_HighlightColor);
            dcc.m_Dc->SetTextColor(dcc.m_HighlightTextColor);
            dcc.m_Dc->FillRect(dcc.m_Rect, &dcc.m_HighlightBrush);
        }
        else if (dcc.m_Select == esBorder)
        {
            oldPen = dcc.m_Dc->SelectObject(&dcc.m_HighlightPen);
            dcc.m_Dc->Rectangle(dcc.m_Rect);
            dcc.m_Dc->SelectObject(oldPen);
        }
        if (!IsEmpty())
        {
            //_ASSERTE(GetCount(edVert));
            if (GetCount(edVert))
            {
                std::wstring text;
                GetCellStr(text, dcc.m_Row, dcc.m_Col);

                // A value the fetch cut short and a NULL cell are each drawn in their own colour, on
                // unselected cells only. Truncation is asked first: a NULL holds no payload, so it
                // cannot also have been cut short, and the order documents that rather than picking.
                // Set and restored here: m_Dcc is one static context shared by every cell.
                COLORREF tint = CLR_INVALID;
                if (dcc.m_Select == esNone)
                {
                    if (IsCellTruncated(dcc.m_Row, dcc.m_Col))
                        tint = dcc.m_TruncatedTextColor;
                    else if (IsCellNull(dcc.m_Row, dcc.m_Col))
                        tint = dcc.m_NullTextColor;
                }
                const COLORREF prevText = tint != CLR_INVALID ? dcc.m_Dc->SetTextColor(tint)
                                                              : CLR_INVALID;

                if (text.length() <= 1024)
                {
                    ::DrawText(dcc.m_Dc->m_hDC, text.data(), text.length(), _rc,
                        GetColAlignment((IsTableOrientation() ? dcc.m_Col : dcc.m_Row)) == ecaLeft
                        ? DT_LEFT|DT_VCENTER|DT_EXPANDTABS|DT_NOPREFIX|DT_WORD_ELLIPSIS
                        : DT_RIGHT|DT_VCENTER|DT_EXPANDTABS|DT_NOPREFIX|DT_WORD_ELLIPSIS);             
                }
                // 09.09.2011 bug fix, grid painting is extreamally slow if grid is loaded with very long strings (> 1mb) 
                // 2016.06.05 bug fix, if multiline clob with very long first line is shown as single-lined 
                else
                {
                    std::wstring line, buffer;
                    std::wistringstream in(text);
                    
                    int nrows = dcc.m_Rect.Height() / dcc.m_CharSize[edVert];
                    int ncols = dcc.m_Rect.Width() / dcc.m_CharSize[edHorz] * 2;

                    int i = 0;
                    for (; getline(in, line) && i < nrows; ++i)
                    {
                        buffer += line.substr(0, ncols);
                        if ((int)line.length() > ncols) 
                            buffer += L"...";
                        buffer += '\n';
                    }
                    if (i)
                        buffer += L"...";
                    
                    ::DrawText(dcc.m_Dc->m_hDC, buffer.data(), buffer.length(), _rc,
                        GetColAlignment((IsTableOrientation() ? dcc.m_Col : dcc.m_Row)) == ecaLeft
                        ? DT_LEFT|DT_VCENTER|DT_EXPANDTABS|DT_NOPREFIX|DT_WORD_ELLIPSIS
                        : DT_RIGHT|DT_VCENTER|DT_EXPANDTABS|DT_NOPREFIX|DT_WORD_ELLIPSIS);             
                }

                if (tint != CLR_INVALID)
                    dcc.m_Dc->SetTextColor(prevText);
            }
        }
    } else {
        dcc.m_Dc->SetBkColor(dcc.m_FixBkColor);
        dcc.m_Dc->SetTextColor(dcc.m_FixTextColor);
        if (IsEmpty())
        {
            DrawDecFrame(dcc);
        }
        else
        {
            if (dcc.m_Type[IsTableOrientation() ? 0 : 1] == efNone && dcc.m_Type[IsTableOrientation() ? 1 : 0] == efFirst)
            {
                DrawDecFrame(dcc);

                // Narrow the text rect before drawing, so a long header ellipsises against the space
                // that is actually free. CalculateCell reserves the same width, so auto-fit agrees.
                CRect rcText = _rc;
                const bool glyph = HeaderCarriesSortGlyph(dcc);
                if (glyph)
                    rcText.right -= SortGlyphReserve(dcc);

                const string& text = GetColHeader(IsTableOrientation() ? dcc.m_Col : dcc.m_Row);
                std::wstring wtext = Common::wstr(text);
                ::DrawTextW(dcc.m_Dc->m_hDC, wtext.data(), wtext.length(), rcText, DT_LEFT|DT_VCENTER|DT_NOPREFIX|DT_WORD_ELLIPSIS);

                if (glyph)
                    DrawSortGlyph(dcc);
            }
            else if (dcc.m_Type[IsTableOrientation() ? 1 : 0] == efNone && dcc.m_Type[IsTableOrientation() ? 0 : 1] == efFirst)
            {
                if (!m_ShowTransparentFixCol)
                    DrawDecFrame(dcc);
                else
                    dcc.m_Dc->FillRect(dcc.m_Rect, &dcc.m_BkBrush[bkClrIndex]);
                if (m_ShowRowEnumeration)
                {
                    char buffer[18];
                    ::DrawTextA(dcc.m_Dc->m_hDC, itoa(IsTableOrientation() ? dcc.m_Row + 1 : dcc.m_Col + 1, buffer, 10), -1, _rc, DT_RIGHT|DT_VCENTER|DT_NOPREFIX|DT_WORD_ELLIPSIS);
                }
                if (m_ImageList)
                {
                    int nIndex = GetImageIndex(dcc.m_Row);
                    if (nIndex != -1)
                        m_ImageList->Draw(dcc.m_Dc, nIndex, _rc.TopLeft(), ILD_TRANSPARENT);
                }
            }
            else
            {
                DrawDecFrame(dcc);
            }
        }
    }
}

void GridSourceBase::CalculateCell (DrawCellContexts& dcc, size_t maxTextLength) const
{
    dcc.m_Rect.SetRectEmpty();

    if (!IsEmpty())
    {
        if (dcc.IsDataCell())
        {
            _ASSERTE(GetCount(edVert));
            if (GetCount(edVert))
            {
                string text;
                GetCellStr(text, dcc.m_Row, dcc.m_Col);

                // cell text is UTF-8; PaintCell draws it as UTF-16, so measure the same way
                std::wstring wtext = Common::wstr(text);

                ::DrawTextW(dcc.m_Dc->m_hDC, wtext.data(), min(wtext.length(), maxTextLength), dcc.m_Rect, DT_CALCRECT|DT_EXPANDTABS|DT_NOPREFIX);

                dcc.m_Rect.InflateRect(-dcc.m_CellMargin.cx, -dcc.m_CellMargin.cy);
            }
        }
        else
        {
            if (dcc.m_Type[IsTableOrientation() ? 0 : 1] == efNone
            && dcc.m_Type[IsTableOrientation() ? 1 : 0] == efFirst)
            {
                const string& text = GetColHeader(IsTableOrientation() ? dcc.m_Col : dcc.m_Row);

                // header text is UTF-8 too; PaintCell draws it as UTF-16
                std::wstring wtext = Common::wstr(text);

                ::DrawTextW(dcc.m_Dc->m_hDC, wtext.data(), min(wtext.length(), maxTextLength), dcc.m_Rect, DT_CALCRECT|DT_NOPREFIX);

                dcc.m_Rect.InflateRect(-dcc.m_CellMargin.cx, -dcc.m_CellMargin.cy);

                // PaintCell draws the sort glyph over the right-hand end of this cell, so the
                // measurement has to pay for it here or the column auto-fits one glyph too narrow.
                if (HeaderCarriesSortGlyph(dcc))
                    dcc.m_Rect.right += SortGlyphReserve(dcc);
            }
        }
    }
}

void GridSourceBase::DrawDecFrame (DrawCellContexts& dcc) const
{
    //dcc.m_Dc->FillRect(dcc.m_Rect, (dcc.m_Select == esNone) ? &dcc.m_FixBrush : &dcc.m_HighlightBrush);
    dcc.m_Dc->FillRect(dcc.m_Rect, &dcc.m_FixBrush);

    CPen lgtPen(PS_SOLID|PS_JOIN_ROUND, 1,  (dcc.m_Select == esNone) ? dcc.m_LightColor : dcc.m_FixShdColor);
    CPen* oldPen = dcc.m_Dc->SelectObject(&lgtPen);

    //dcc.m_Dc->SetROP2(R2_COPYPEN);
    {
        POINT pts[3] = { dcc.m_Rect.left, dcc.m_Rect.bottom, dcc.m_Rect.left, dcc.m_Rect.top, dcc.m_Rect.right, dcc.m_Rect.top };
        dcc.m_Dc->Polyline(pts, 3);
    }

    CPen shdPen(PS_SOLID, 1, (dcc.m_Select == esNone) ? dcc.m_FixShdColor : dcc.m_LightColor);
    dcc.m_Dc->SelectObject(&shdPen);

    {
        POINT pts[3] = { dcc.m_Rect.left + 1, dcc.m_Rect.bottom - 1, dcc.m_Rect.right - 1, dcc.m_Rect.bottom - 1, dcc.m_Rect.right - 1, dcc.m_Rect.top };
        dcc.m_Dc->Polyline(pts, 3);
    }

    dcc.m_Dc->SelectObject(*oldPen);
}

    // Undoes the fold BeginEdit applies: CRLF back to the bare '\n' a cell holds. A '\r' not
    // followed by '\n' is data and is kept.
    static
    void UnfoldCrLf (std::string& str)
    {
        std::string out;
        out.reserve(str.size());

        for (size_t i = 0; i < str.size(); i++)
            if ('\r' == str[i] && i + 1 < str.size() && '\n' == str[i+1])
                continue;           // the '\n' behind it is the one that survives
            else
                out += str[i];

        str.swap(out);
    }

CWnd* GridSourceBase::BeginEdit (int row, int col, CWnd* parent, const CRect& rect)  // get editor for grid window
{
    _ASSERTE(!m_Editor && !IsEmpty());

    m_editRow = row;
    m_editCol = col;
    m_editInitial.clear();

    const bool editable = IsCellEditable(row, col);

    string text;
    GetCellStr(text, row, col);

    // An editable cell opens the editor even when it displays nothing, which is the only way a NULL
    // or an empty value can be given one. A read-only cell keeps the old rule and opens nothing.
    if (text.empty() && !editable)
        return m_Editor;

    // CEdit wants CRLF between lines; the cell holds bare '\n'.
    std::string folded;
    int nlines = 0;

    {
        std::istringstream istr(text);
        std::string buff;

        while (std::getline(istr, buff, '\n'))
        {
            if (nlines++)
                folded += "\r\n";
            folded += buff;
        }
    }

    DWORD dwStyle = WS_VISIBLE|WS_CHILD|
                    ES_AUTOHSCROLL|ES_AUTOVSCROLL|
                    ES_NOHIDESEL;
    if (!editable)
        dwStyle |= ES_READONLY;
    if (nlines > 1)
        dwStyle |= ES_MULTILINE|WS_VSCROLL|WS_HSCROLL;

    m_Editor = new CellEditor;
    m_Editor->Create(dwStyle, rect, parent, 999);
    std::wstring wstr = Common::wstr(folded);
    m_Editor->SetWindowText(wstr.c_str());

    if (nlines <= 1)
        m_Editor->SetSel(0, wstr.length());

    m_editInitial = folded;
    return m_Editor;
}

void GridSourceBase::PostEdit () // event for commit edit result &
{
    _ASSERTE(!IsEmpty());

    // Read the editor before destroying it: this is the only moment the typed text still exists, and
    // reaching PostEdit rather than CancelEdit is what says the user meant to keep it.
    if (m_Editor && m_editRow >= 0 && m_editCol >= 0 && IsCellEditable(m_editRow, m_editCol))
    {
        CString wtext;
        m_Editor->GetWindowText(wtext);

        std::string now = Common::str(wtext);

        // Against what BeginEdit loaded, never against GetCellStr: a NULL cell displays the null
        // marker, so comparing to the cell would report an untouched NULL as an edit to empty.
        if (now != m_editInitial)
        {
            UnfoldCrLf(now);
            CommitCellEdit(m_editRow, m_editCol, now);
        }
    }

    if (m_Editor)
        m_Editor->DestroyWindow();
    delete m_Editor;
    m_Editor = 0;
    m_editRow = m_editCol = -1;
    m_editInitial.clear();
}

void GridSourceBase::CancelEdit () // end edit session
{
    _ASSERTE(!IsEmpty());
    if (m_Editor)
        m_Editor->DestroyWindow();
    delete m_Editor;
    m_Editor = 0;
    m_editRow = m_editCol = -1;
    m_editInitial.clear();
}

// The comparison PostEdit makes, asked early so an untouched cell can be told from a changed one.
bool GridSourceBase::IsEditDirty () const
{
    if (!m_Editor || m_editRow < 0 || m_editCol < 0)
        return false;

    CString wtext;
    m_Editor->GetWindowText(wtext);
    return Common::str(wtext) != m_editInitial;
}

    // TODO: convert to unicode
    static
    void CopyHTML (const char *html) {
        // Create temporary buffer for HTML header...
        char *buf = new char [400 + strlen(html)];
        if(!buf) return;

        // Get clipboard id for HTML format...
        static int cfid = 0;
        if(!cfid) cfid = RegisterClipboardFormat(L"HTML Format");

        // Create a template string for the HTML header...
        strcpy(buf,
            "Version:0.9\r\n"
            "StartHTML:00000000\r\n"
            "EndHTML:00000000\r\n"
            "StartFragment:00000000\r\n"
            "EndFragment:00000000\r\n"
            "<html><body>\r\n"
            "<!--StartFragment -->\r\n");

        // Append the HTML...
        strcat(buf, html);
        strcat(buf, "\r\n");
        // Finish up the HTML format...
        strcat(buf,
            "<!--EndFragment-->\r\n"
            "</body>\r\n"
            "</html>");

        // Now go back, calculate all the lengths, and write out the
        // necessary header information. Note, wsprintf() truncates the
        // string when you overwrite it so you follow up with code to replace
        // the 0 appended at the end with a '\r'...
        char *ptr = strstr(buf, "StartHTML");
        wsprintfA(ptr+10, "%08u", strstr(buf, "<html>") - buf);
        *(ptr+10+8) = '\r';

        ptr = strstr(buf, "EndHTML");
        wsprintfA(ptr+8, "%08u", strlen(buf));
        *(ptr+8+8) = '\r';

        ptr = strstr(buf, "StartFragment");
        wsprintfA(ptr+14, "%08u", strstr(buf, "<!--StartFrag") - buf);
        *(ptr+14+8) = '\r';

        ptr = strstr(buf, "EndFragment");
        wsprintfA(ptr+12, "%08u", strstr(buf, "<!--EndFrag") - buf);
        *(ptr+12+8) = '\r';

        // Now you have everything in place ready to put on the
        // clipboard.

        // Open the clipboard...
/*        if(OpenClipboard(0))*/ {
        
            // Empty what's in there...
/*            EmptyClipboard();*/
        
            // Allocate global memory for transfer...
            HGLOBAL hText = GlobalAlloc(GMEM_MOVEABLE |GMEM_DDESHARE, strlen(buf)+4);
        
            // Put your string in the global memory...
            char *ptr = (char *)GlobalLock(hText);
            strcpy(ptr, buf);
            GlobalUnlock(hText);
        
            ::SetClipboardData(cfid, hText);
        
/*            CloseClipboard();*/
            // Free memory...
            GlobalFree(hText);
        
        }

        // Clean up...
        delete [] buf;
    }

// clipboard operation support
// need Open & Close
void GridSourceBase::DoEditCopy (int row, int nrows, int col, int ncols, ECopyWhat what, int format)
{
    if (EmptyClipboard())
    {
        std::string buff;
        int actualFormat = CopyToString(buff, row, nrows, col, ncols, what, format);
        if (!buff.empty())
        {
            if (actualFormat == etfHtml)
                CopyHTML(buff.c_str());

            std::wstring wbuff = Common::wstr(buff);
            int size = (wbuff.length() + 1)  * sizeof(wchar_t);
            HGLOBAL hData = GlobalAlloc(GMEM_MOVEABLE|GMEM_DDESHARE, size);
            char* dest = (char*)GlobalLock(hData);
            if (dest) memcpy(dest, wbuff.c_str(), size);
            SetClipboardData(CF_UNICODETEXT, hData);

            if (!(ncols == -1 || ncols == GetCount(edHorz)))
            {
                HGLOBAL hCodeSignature =  NULL;
                hCodeSignature = GlobalAlloc(GMEM_MOVEABLE|GMEM_DDESHARE, sizeof("{code}"));
                char* codeSignature = (char*)GlobalLock(hCodeSignature);
                if (codeSignature) memcpy(codeSignature, "{code}", sizeof("{code}"));
                SetClipboardData(CF_PRIVATEFIRST, hCodeSignature);
            }
        }
    }
}

void GridSourceBase::CopyColumnNames (std::string& buff, int from, int to) const
{
    int nCols = m_ColumnInfo.size();
    for (int i = from; i < nCols && i <= to; i++)
    {
        buff += GetColHeader(i);
        if (i != nCols - 1 && i != to)
            buff += ", ";
    }
}

int GridSourceBase::CopyToString (std::string& buff, int row, int nrows, int col, int ncols, ECopyWhat what, int format)
{
    int actualFormat = etfPlainText;

    if (!IsEmpty())
    {
        if (!IsTableOrientation())
        {
            switch (what)
            {
            case ecDragRowHeader:
            case ecRowHeader:
                what = ecColumnHeader;
                col = row;
                ncols = nrows;
                row = nrows = -1;
                break;
            case ecDragColumnHeader:
            case ecColumnHeader:
                what = ecRow; // getCellStr transposes itself so no need change columns and rows like for headers
                //what = ecRowHeader;
                //row = col;
                //nrows = ncols;
                //col = ncols = -1;
                break;
            }
        }

        switch (what)
        {
        case ecFieldNames:
        case ecDragTopLeftCorner:
            CopyColumnNames(buff, 0, std::numeric_limits<int>::max());
            break;
        case ecColumnHeader:
        case ecDragColumnHeader:
            CopyColumnNames(buff, col, col + ncols - 1);
            break;
        case ecRowHeader:
            {
                std::stringstream of;
                of << row;
                buff = of.str();
            }
            break;
        case ecDataCell:
        case ecColumn:
        case ecRow:
        case ecDragRowHeader:
        case ecEverything:
            switch (what)
            {
            case ecColumn:
                row = nrows = -1;
                break;
            case ecRow:
            case ecDragRowHeader:
                break;
            case ecEverything:
                row = nrows = -1;
                col = ncols = -1;
            }
            // XE1: a clipboard carries text and a worksheet is a zip, and the format setting is
            // shared with the file export, so a copy falls back to what a spreadsheet reads.
            const int resolved = (format != etfDefault) ? format
                                                        : (int)IGridTextSource::GetOutputFormat();
            if (IsBinaryExportFormat(resolved))
                format = etfQuoteDelimited;

            std::stringstream of;
            actualFormat = ExportText(of, row, nrows, col, ncols, format);
            buff = of.str();
            break;
        }
    }
    
    return actualFormat;
}

void GridSourceBase::DoEditPaste ()
{
    // Not implement
}

void GridSourceBase::DoEditCut ()
{
    // Not implement
}

    class GridStringOleSource : public COleDataSource
    {
        GridSourceBase* m_pSource;
        int m_row, m_nrows, m_col, m_ncols;
        ECopyWhat m_what;
    public:
        GridStringOleSource(GridSourceBase* pSource, int row, int nrows, int col, int ncols, ECopyWhat what)
            : m_pSource(pSource), m_row(row), m_nrows(nrows), m_col(col), m_ncols(ncols), m_what(what)
        {
            DelayRenderData(CF_TEXT);
            if (!(ncols == -1 || ncols == pSource->GetCount(edHorz)))
                DelayRenderData(CF_PRIVATEFIRST);
        }

        virtual BOOL OnRenderGlobalData(LPFORMATETC lpFormatEtc, HGLOBAL* phGlobal)
        {
            CWaitCursor wait;

            string text;

            switch (lpFormatEtc->cfFormat)
            {
            case CF_TEXT:
                {
                    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
                    bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                    m_pSource->CopyToString(text, m_row, m_nrows, m_col, m_ncols, m_what, 
                        !shift && !ctrl ? etfPlainText : (
                                shift && !ctrl ? etfPLSQL1 : (
                                        !shift && ctrl ? etfPLSQL2 : etfPLSQL3
                                    )
                            )
                    );
                }
                break;
            case CF_PRIVATEFIRST:
                text = "{code}";
                break;
            }

            HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, text.size() + 1);

            if (!text.empty())
            {
                if (hGlobal)
                {
                    char* buff = (char*)GlobalLock(hGlobal);
                    if (buff)
                    {
                        memcpy(buff, text.c_str(), text.size() + 1);
                        GlobalUnlock(hGlobal);
                        *phGlobal = hGlobal;
                        return TRUE;
                    }
                    GlobalFree(hGlobal);
                }
            }
            return FALSE;
        }
    };

BOOL GridSourceBase::DoDragDrop (int row, int nrows, int col, int ncols, ECopyWhat what)
{
    if (!IsEmpty())
    {
        GridStringOleSource(this, row, nrows, col, ncols, what).DoDragDrop(DROPEFFECT_COPY|DROPEFFECT_MOVE|DROPEFFECT_LINK);
        return TRUE;
    }
    return FALSE;
}

DROPEFFECT GridSourceBase::DoDragOver (COleDataObject*, DWORD)
{
    // Not implement
    return DROPEFFECT_NONE;
}

BOOL GridSourceBase::DoDrop (COleDataObject*, DROPEFFECT)
{
    // Not implement
    return FALSE;
}

//const char* GridSourceBase::GetTooltipString (TooltipCellContexts& tcc) const
//{
//    tcc.m_CellRecall = false;
//    if (tcc.IsDataCell())
//    {
//        return GetCellStr(tcc.m_Row, tcc.m_Col);
//    }
//    else if (tcc.m_Type[0] == efNone && tcc.m_Type[1] == efFirst)
//    {
//        return GetColHeader(tcc.m_Col).c_str();
//    }
//    else if (tcc.m_Type[1] == efNone && tcc.m_Type[0] == efFirst)
//    {
//        char buffer[18];
//        return itoa(tcc.m_Row + 1, buffer, 10);
//    }
//    return "";
//}

void GridSourceBase::SetCols (int cols)
{
    m_ColumnInfo.clear();
    m_ColumnInfo.resize(cols, ColumnInfo());
    SetEmpty(false);
}

void GridSourceBase::SetImageList (CImageList* imageList, BOOL deleteImageList)
{
    if (m_bDeleteImageList)
        delete m_ImageList;

    m_ImageList = imageList;
    m_bDeleteImageList = deleteImageList;
}

////////////////////////////////////////////////////////////////////////

int GridStringSource::GetCount (eDirection dir) const
{
    return /*(IsEmpty()) ? 1 :*/ ((IsTableOrientation() ? dir == edHorz : dir == edVert) ? m_table.GetColumnCount() : m_table.GetRowCount());
}

void GridStringSource::SetCols (int cols)
{
    GridSourceBase::SetCols(cols);
    m_table.Format(cols);
}

void GridStringSource::Clear ()
{
    m_table.Clear();
    SetEmpty(true);
}

int GridStringSource::Append ()
{
    int line = m_table.Append();
    Notify_ChangedRowsQuantity(line, 1);
    return line;
}

int GridStringSource::Insert (int row)
{
    m_table.Insert(row);
    Notify_ChangedRowsQuantity(row, 1);
    return row;
}

void GridStringSource::Delete (int row)
{
    ASSERT(m_table.GetRowCount() > 0);
    if (m_table.GetRowCount() > 0)
    {
        m_table.Delete(row);
        Notify_ChangedRowsQuantity(row, -1);
    }
}

void GridStringSource::DeleteAll ()
{
    if (int count = m_table.GetRowCount())
    {
        m_table.DeleteAll();
	    Notify_ChangedRowsQuantity(0, -count);
    }
}

int GridStringSource::GetSize (int pos, eDirection dir) const
{
    if (IsTableOrientation())
    {
        if (dir == edVert)
        {
            if (GetMaxShowLines() > 1)
                return m_table.GetMaxLinesInRow(pos, GetMaxShowLines());
            return 1;
        }
    }
    else // IsFormOrientation()
    {
        if (dir == edVert)
        {
            if (GetMaxShowLines() > 1)
                return m_table.GetMaxLinesInColumn(pos, GetMaxShowLines());
            return 1;
        }
        return 100;
    }

    return GridSourceBase::GetSize(pos, dir);
}

void GridStringSource::GetMaxColumnWidthByFirstLine (nvector<size_t>& maxLengths, int from, int to) const
{
    if (IsTableOrientation())
        m_table.GetMaxColumnWidthByFirstLine(maxLengths, from, to);
    else
        m_table.GetMaxRowWidthByFirstLine(maxLengths, from, to);
}

//////////////////////////////////////////////////////////////////////
// ValueGridSource - mirrors GridStringSource above member for member, so the only differences are
// the storage each forwards to and the NULL-text hook in GetCellStr. Keep the two in step.

int ValueGridSource::GetCount (eDirection dir) const
{
    return ((IsTableOrientation() ? dir == edHorz : dir == edVert) ? m_table.GetColumnCount() : m_table.GetRowCount());
}

void ValueGridSource::SetCols (int cols)
{
    GridSourceBase::SetCols(cols);
    m_table.Format(cols);
    ApplyNullText();
}

void ValueGridSource::Clear ()
{
    m_table.Clear();
    SetEmpty(true);
    ApplyNullText();    // a no-op in the ctor that calls this, where m_nullText is still empty
}

void ValueGridSource::SetNullText (const std::string& text)
{
    m_nullText = text;

    // Parked straight away once the table is formatted; before that ApplyNullText() in SetCols()
    // does it. Best effort either way: on failure GetCellStr() falls back to m_nullText.
    if (m_table.GetColumnCount() > 0)
        m_table.SetNullText(text);
}

void ValueGridSource::ApplyNullText ()
{
    // Skipped when there is no marker, so a grid that wants NULLs drawn blank pays nothing here.
    if (!m_nullText.empty())
        m_table.SetNullText(m_nullText);
}

int ValueGridSource::Append ()
{
    int line = m_table.Append();
    Notify_ChangedRowsQuantity(line, 1);
    return line;
}

int ValueGridSource::Insert (int row)
{
    m_table.Insert(row);
    Notify_ChangedRowsQuantity(row, 1);
    return row;
}

void ValueGridSource::Delete (int row)
{
    ASSERT(m_table.GetRowCount() > 0);
    if (m_table.GetRowCount() > 0)
    {
        m_table.Delete(row);
        Notify_ChangedRowsQuantity(row, -1);
    }
}

void ValueGridSource::DeleteAll ()
{
    if (int count = m_table.GetRowCount())
    {
        m_table.DeleteAll();
	    Notify_ChangedRowsQuantity(0, -count);
    }
}

bool ValueGridSource::SortByColumn (int col, bool ascending)
{
    // Order of the three refusals matters: CanSort() is consulted before touching the table, so a
    // veto cannot race a permutation of rows a fetch worker is concurrently appending to.
    if (!IsTableOrientation())
        return false;

    if (!CanSort())
        return false;

    // Sort() permutes m_rowMap only; cells, payloads and the arena are untouched, and it validates
    // `col` and the row count itself, returning false rather than leaving a half-built permutation.
    if (!m_table.Sort(col, ascending))
        return false;

    // Fired only once the table is already in its new order, so a manager repainting from within
    // the notification draws sorted data. Not Notify_ChangedRows: that keeps the height cache.
    Notify_RowsReordered();
    return true;
}

bool ValueGridSource::ClearSort ()
{
    // IsSorted() already implies a sort ran, which brought every row in, so there is no fetch to
    // prepare for and no payload to read - dropping the permutation is UI-thread metadata only.
    if (!IsTableOrientation() || !m_table.IsSorted())
        return false;

    m_table.ClearSort();
    Notify_RowsReordered();   // the same repaint path SortByColumn fires
    return true;
}

bool ValueGridSource::MarkSortStale ()
{
    if (!IsTableOrientation() || m_table.GetSortColumn() < 0)
        return false;

    m_table.MarkSortStale();
    Notify_RowsReordered();   // no row moved; this is the repaint that clears the glyph
    return true;
}

int ValueGridSource::GetSize (int pos, eDirection dir) const
{
    if (IsTableOrientation())
    {
        if (dir == edVert)
        {
            if (GetMaxShowLines() > 1)
                return m_table.GetMaxLinesInRow(pos, GetMaxShowLines());
            return 1;
        }
    }
    else // IsFormOrientation()
    {
        if (dir == edVert)
        {
            if (GetMaxShowLines() > 1)
                return m_table.GetMaxLinesInColumn(pos, GetMaxShowLines());
            return 1;
        }
        return 100;
    }

    return GridSourceBase::GetSize(pos, dir);
}

void ValueGridSource::GetCellStr (std::string& str, int row, int col) const
{
    const int r = IsTableOrientation() ? row : col;
    const int c = IsTableOrientation() ? col : row;

    // A NULL cell's text IS the marker, parked on the shared payload, so there is no NULL branch
    // here and the answer matches what the layout metrics measured. The fallback covers a full arena.
    m_table.GetText(r, c, str);
    if (str.empty() && m_table.IsNullCell(r, c))
        str = m_nullText;
}

bool ValueGridSource::IsCellTruncated (int row, int col) const
{
    // The same swap GetCellStr performs: PaintCell hands this the grid-space pair it is drawing
    // while the table is stored rows-as-records, so a transposed index is what finds the cell.
    const int r = IsTableOrientation() ? row : col;
    const int c = IsTableOrientation() ? col : row;
    return m_table.IsTruncatedCell(r, c);
}

bool ValueGridSource::IsCellNull (int row, int col) const
{
    // IsCellTruncated's swap, for IsCellTruncated's reason.
    const int r = IsTableOrientation() ? row : col;
    const int c = IsTableOrientation() ? col : row;
    return m_table.IsNullCell(r, c);
}

bool ValueGridSource::GetCellSqlValue (Common::SqlValue& out, int row, int col) const
{
    // Same swap as IsCellTruncated and GetCellStr above. Getting it backwards is silent and total:
    // in form orientation the vertical axis IS the table's columns.
    const int r = IsTableOrientation() ? row : col;
    const int c = IsTableOrientation() ? col : row;
    out = m_table.GetValue(r, c);
    return true;
}

void ValueGridSource::GetMaxColumnWidthByFirstLine (nvector<size_t>& maxLengths, int from, int to) const
{
    if (IsTableOrientation())
        m_table.GetMaxColumnWidthByFirstLine(maxLengths, from, to);
    else
        m_table.GetMaxRowWidthByFirstLine(maxLengths, from, to);
}

void IGridTextSource::GetCellStrForExport (string& str, int line, int col, int) const
{
    GetCellStr(str, line, col);
}

/** int countTruncatedCells (const GridSourceBase* source, int startRow, int rowCount, int startCol, int colCount)
 * @brief Counts the cells an export of this range would write from a truncated payload (JD9).
 *
 * A free function in the caller's hands rather than a streamer hook, because ExportText has no status
 * channel: it returns the *format*, and five of its six call sites discard even that. Counting here is
 * format-agnostic, so it fixes CSV, HTML and XML too, which silently export truncated cells today.
 *
 * Cheap: IsCellTruncated is an in-memory flag read on the payload, so this second pass over the range
 * costs nothing next to the formatting and the I/O the export itself just did. It takes grid-space
 * indices and needs no orientation swap of its own - IsCellTruncated performs that internally, the
 * same one GetCellStr and GetCellSqlValue perform.
 *
 * @arg source   Grid to scan, in the AGridSource type GetGridSource() returns. May be null, and a
 *               source that is not GridSourceBase-derived counts as nothing truncated.
 * @arg startRow First row, or -1 for all rows - the sentinel Streamer's constructor honours.
 * @arg rowCount Rows to scan; read only when startRow != -1, exactly as Streamer reads it.
 * @arg startCol First column, or -1 for all columns.
 * @arg colCount Columns to scan; read only when startCol != -1.
 * @return Number of truncated cells in the range.
 */
int countTruncatedCells (const AGridSource* source, int startRow, int rowCount, int startCol, int colCount)
{
    // IsCellTruncated is declared on GridSourceBase, not on AGridSource, because the flag is a
    // payload bit and only payload-owning sources have one.
    const GridSourceBase* grid = dynamic_cast<const GridSourceBase*>(source);

    if (!grid)
        return 0;

    // Per-argument normalization matching the export streamer's, so the count covers precisely the
    // cells the export covered - including the -1/-1/-1/-1 "everything" case.
    int row0, rowN, col0, colN;

    if (startCol != -1) { col0 = startCol; colN = colCount; }
    else                { col0 = 0;        colN = grid->GetCount(edHorz); }

    if (startRow != -1) { row0 = startRow; rowN = rowCount; }
    else                { row0 = 0;        rowN = grid->GetCount(edVert); }

    int truncated = 0;

    for (int row = row0; row < row0 + rowN; row++)
        for (int col = col0; col < col0 + colN; col++)
            if (grid->IsCellTruncated(row, col))
                truncated++;

    return truncated;
}

}//namespace OG2

