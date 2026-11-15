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

#include "stdafx.h"
#include "OpenEditor/OEHighlighter.h"
#include "OpenEditor/OEViewPaintAccessories.h"
#include "Common/MyUtf.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

//#define ADJUST_FONT_BY_WIDEST

    using namespace std;
    using namespace Common;
    using namespace OpenEditor;

    typedef map<string, COEViewPaintAccessoriesPtr> COEViewPaintAccessoriesMap;
    
    static COEViewPaintAccessoriesMap g_PaintAccessoriesMap;

    //COLORREF COEViewPaintAccessories::m_COLOR_ERROR_HLIGHT = RGB(255,196,196); 
    COLORREF COEViewPaintAccessories::m_COLOR_BTNHIGHLIGHT;
    COLORREF COEViewPaintAccessories::m_COLOR_BTNFACE;
    COLORREF COEViewPaintAccessories::m_COLOR_BTNTEXT;
    COLORREF COEViewPaintAccessories::m_COLOR_BTNSHADOW;
    CPen COEViewPaintAccessories::m_RulerPen;
    CPen COEViewPaintAccessories::m_IndentGuidelinePen;
    CPen COEViewPaintAccessories::m_BtnShadowPen;
    CBrush COEViewPaintAccessories::m_BtnFaceBrush;


COEViewPaintAccessoriesPtr COEViewPaintAccessories::GetPaintAccessories (const std::string& category)
{
    COEViewPaintAccessoriesMap::iterator it = g_PaintAccessoriesMap.find(category);
    
    if (g_PaintAccessoriesMap.end() == it)
    {
        COEViewPaintAccessoriesPtr ptr(new COEViewPaintAccessories);

        g_PaintAccessoriesMap.insert(COEViewPaintAccessoriesMap::value_type(category, ptr));

        return ptr;
    }

    return it->second;
}

COEViewPaintAccessories::COEViewPaintAccessories ()
{ 
    memset(&m_FontCharacterExtra, 0, sizeof(m_FontCharacterExtra));
    m_TextFont = 0;
    // Sane before the first OnSettingsChanged, so a paint that somehow beat the
    // settings notification cannot fill with an uninitialised COLORREF. The real
    // value is derived there, from the resolved system colours.
    m_UnparsedBandBackground = RGB(216,216,216);
    // Sane before the first OnSettingsChanged for the same reason; the real
    // value comes from the "Find Match" attribute (soft amber, see
    // OESettingsXmlStreamer's default).
    m_FindMatchBackground = RGB(255,236,150);
}

void COEViewPaintAccessories::OnSettingsChanged (CWnd* pwnd, const VisualAttributesSet& set_)
{
    const VisualAttribute& textAttr = set_.FindByName("Text");
    m_TextFont = (textAttr.m_FontBold ? HighlighterBase::efmBold : HighlighterBase::efmNormal)
            | (textAttr.m_FontItalic ? HighlighterBase::efmItalic : HighlighterBase::efmNormal)
            | (textAttr.m_FontUnderline ? HighlighterBase::efmUnderline : HighlighterBase::efmNormal);

    m_TextForeground = textAttr.m_Foreground;
    m_TextBackground = textAttr.m_Background;
    m_TextForegroundPen.DeleteObject();
    m_TextForegroundPen.CreatePen(PS_SOLID, 1, m_TextForeground);

    try
    {
        const VisualAttribute& highlightingAttr = set_.FindByName("Highlighting");
        m_HighlightingBackground = highlightingAttr.m_Background;
    }
    catch (const std::out_of_range&)
    {
        m_HighlightingBackground = textAttr.m_Background;
    }

    try
    {
        const VisualAttribute& highlightingAttr = set_.FindByName("Error Highlighting");
        m_ErrorHighlightingBackground = highlightingAttr.m_Background;
    }
    catch (const std::out_of_range&)
    {
        m_ErrorHighlightingBackground = RGB(255,196,196);
    }

    // Step 4 "highlight all occurrences" overlay. The attribute is defaulted
    // into every editor class set by OESettingsXmlStreamer; the fallback covers
    // a set that predates it (or one built outside the XML load path) and keeps
    // the overlay visible with the pre-"Find Match" look.
    try
    {
        const VisualAttribute& findMatchAttr = set_.FindByName("Find Match");
        m_FindMatchBackground = findMatchAttr.m_Background;
    }
    catch (const std::out_of_range&)
    {
        m_FindMatchBackground = m_HighlightingBackground;
    }


    const VisualAttribute& selTextAttr = set_.FindByName("Selected Text");
    m_SelTextForeground = selTextAttr.m_Foreground;
    m_SelTextBackground = selTextAttr.m_Background;
    m_SelTextForegroundPen.DeleteObject();
    m_SelTextForegroundPen.CreatePen(PS_SOLID, 1, m_COLOR_BTNFACE);

    const VisualAttribute& curLinetAttr = set_.FindByName("Current Line");
    m_CurrentLineBackground = curLinetAttr.m_Background;

    const VisualAttribute& bmkAttr = set_.FindByName("Bookmark");
    m_BmkBackground = bmkAttr.m_Background;
    m_BmkBrush.DeleteObject();
    m_BmkBrush.CreateSolidBrush(bmkAttr.m_Background);
    m_BmkPen.DeleteObject();
    m_BmkPen.CreatePen(PS_SOLID, 1, RGB(0,0,0));

    const VisualAttribute& rdmBmkAttr = set_.FindByName("Random Bookmark");
    m_RndBmkForeground = rdmBmkAttr.m_Foreground;
    m_RndBmkBackground = rdmBmkAttr.m_Background;
    m_RndBmkBrush.DeleteObject();
    m_RndBmkBrush.CreateSolidBrush(rdmBmkAttr.m_Background);
    m_RndBmkPen.DeleteObject();
    m_RndBmkPen.CreatePen(PS_SOLID, 1, rdmBmkAttr.m_Foreground);

    m_EOFForeground = set_.FindByName("End of File").m_Foreground;


    TEXTMETRIC tm;
    CClientDC dc(pwnd);

#ifdef ADJUST_FONT_BY_WIDEST
    int widest = 0;
#else
    int narrowest = INT_MAX;
#endif
    int highest = 0;

    int widths[8];

    LOGFONT logfont;
    memset(&logfont, 0, sizeof(logfont));

    logfont.lfHeight         = -10 * textAttr.m_FontSize;
    logfont.lfCharSet        = DEFAULT_CHARSET;
    logfont.lfOutPrecision   = CLIP_DEFAULT_PRECIS;
    logfont.lfClipPrecision  = CLIP_DEFAULT_PRECIS;
    logfont.lfQuality        = DEFAULT_QUALITY;
    logfont.lfPitchAndFamily = FIXED_PITCH;
    _tcsncpy(logfont.lfFaceName, Common::wstr(textAttr.m_FontName).c_str(), LF_FACESIZE-1);

    for (int i = 0; i < 8; i++)
    {
        logfont.lfWeight    = (0x01 & i) ? FW_SEMIBOLD : FW_NORMAL;
        logfont.lfItalic    = (0x02 & i) ? 1 : 0;
        logfont.lfUnderline = (0x04 & i) ? 1 : 0;

        m_Fonts[i].DeleteObject();
        m_Fonts[i].CreatePointFontIndirect(&logfont, &dc);
        dc.SelectObject(m_Fonts[i]);
        dc.GetTextMetrics(&tm);

        widths[i] = tm.tmAveCharWidth;

#ifdef ADJUST_FONT_BY_WIDEST
        if (widest < tm.tmAveCharWidth)
            widest = tm.tmAveCharWidth;
#else
        if (narrowest > tm.tmAveCharWidth)
            narrowest = tm.tmAveCharWidth;
#endif
        if (highest < tm.tmHeight + tm.tmExternalLeading)
            highest = tm.tmHeight + tm.tmExternalLeading;
    }

#ifdef ADJUST_FONT_BY_WIDEST
    for (int i = 0; i < 8; i++)
        m_FontCharacterExtra[i] = widest - widths[i];

    m_CharSize.cx = widest;
#else
    for (int i = 0; i < 8; i++)
        m_FontCharacterExtra[i] = narrowest - widths[i];

    m_CharSize.cx = narrowest;
#endif

    m_CharSize.cy = highest;

// ruler
    m_COLOR_BTNHIGHLIGHT  = GetSysColor(COLOR_BTNHIGHLIGHT);
    m_COLOR_BTNFACE = GetSysColor(COLOR_BTNFACE);
    m_COLOR_BTNTEXT = GetSysColor(COLOR_BTNTEXT);
    m_COLOR_BTNSHADOW = GetSysColor(COLOR_BTNSHADOW);

    m_RulerPen.DeleteObject();
    m_RulerPen.CreatePen(PS_SOLID, 1, m_COLOR_BTNFACE);

    m_IndentGuidelinePen.DeleteObject();

    LOGBRUSH logbrush;
    logbrush.lbStyle = BS_SOLID;
    logbrush.lbColor = GetSysColor(/*COLOR_BTNFACE*/COLOR_BTNSHADOW);
    logbrush.lbHatch = HS_VERTICAL;
    m_IndentGuidelinePen.CreatePen(PS_GEOMETRIC|PS_DOT, 1, &logbrush, 0, 0);

    m_BtnShadowPen.DeleteObject();
    m_BtnShadowPen.CreatePen(PS_SOLID, 1, m_COLOR_BTNSHADOW);

    // The unparsed-tail band (ToDoNotes 107 step 4). Derived rather than hard-coded,
    // and derived HERE so it follows the system palette: the strip it lands in is
    // system-coloured - BTNFACE for the standard gutter, a halftone dither over it for
    // the extended one (OEView_2.cpp:304-305) - so a literal RGB would read as a
    // highlight on one theme and as a hole on another.
    //
    // Halfway from the face to the shadow is dimmer than the gutter around it on every
    // stock theme without becoming a second STATUS colour competing with the bookmark
    // and syntax-error fills, which is the point: the band says "nothing is known about
    // this line", not "something is wrong with it". The marker rule takes the shadow
    // outright so it stays visible against the band it sits on.
    m_UnparsedBandBackground =
        RGB((GetRValue(m_COLOR_BTNFACE) + GetRValue(m_COLOR_BTNSHADOW)) / 2,
            (GetGValue(m_COLOR_BTNFACE) + GetGValue(m_COLOR_BTNSHADOW)) / 2,
            (GetBValue(m_COLOR_BTNFACE) + GetBValue(m_COLOR_BTNSHADOW)) / 2);

    m_UnparsedBandPen.DeleteObject();
    m_UnparsedBandPen.CreatePen(PS_SOLID, 1, m_COLOR_BTNSHADOW);

    memset(&logfont, 0, sizeof(logfont));

    logfont.lfHeight         = -5 * textAttr.m_FontSize;
    logfont.lfCharSet        = DEFAULT_CHARSET;
    logfont.lfOutPrecision   = CLIP_DEFAULT_PRECIS;
    logfont.lfClipPrecision  = CLIP_DEFAULT_PRECIS;
    logfont.lfQuality        = DEFAULT_QUALITY;
    logfont.lfPitchAndFamily = DEFAULT_PITCH;
    wcsncpy(logfont.lfFaceName, textAttr.m_FontSize <= 14 ? L"Small Fonts" : Common::wstr(textAttr.m_FontName).c_str(), LF_FACESIZE-1);

    m_RulerFont.DeleteObject();
    m_RulerFont.CreatePointFontIndirect(&logfont, &dc);
    dc.SelectObject(m_RulerFont);
    dc.GetTextMetrics(&tm);

    m_RulerCharSize.cx = tm.tmAveCharWidth;
    m_RulerCharSize.cy = tm.tmHeight + tm.tmExternalLeading;

    m_BtnFaceBrush.DeleteObject();
    m_BtnFaceBrush.CreateSysColorBrush(COLOR_BTNFACE);
}

