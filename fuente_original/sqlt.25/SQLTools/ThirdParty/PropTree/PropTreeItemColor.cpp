// PropTreeItemColor.cpp : implementation file
//
//  Copyright (C) 1998-2001 Scott Ramsay
//	sramsay@gonavi.com
//	http://www.gonavi.com
//
//  This material is provided "as is", with absolutely no warranty expressed
//  or implied. Any use is at your own risk.
// 
//  Permission to use or copy this software for any purpose is hereby granted 
//  without fee, provided the above notices are retained on all copies.
//  Permission to modify the code and to distribute modified code is granted,
//  provided the above notices are retained, and a notice that the code was
//  modified is included with the above copyright notice.
// 
//	If you use this code, drop me an email.  I'd like to know if you find the code
//	useful.
//
//	(ak) MODIFIED - the palette popup was rewritten. What shipped was a 40 swatch 8x5 grid
//	under a "More Colors" button, reachable with the mouse only, and it had four problems a
//	colour picker cannot have:
//
//	  - No keyboard. It answered WM_GETDLGCODE with the CWnd default, so IsDialogMessage()
//	    never sent it the arrows, and it had no WM_KEYDOWN handler either. The arrows either
//	    did nothing or moved the focus out of the popup, which committed it.
//	  - Nothing showed the current colour. Opening it on an attribute that was already red
//	    looked exactly like opening it on one that was black.
//	  - OnLButtonDown() committed m_nSpot, the index OnMouseMove() had last set, instead of
//	    hit testing the click. A click in the padding between two swatches picked whichever
//	    one the pointer had crossed on the way there.
//	  - OnCancel() hid the popup but left m_cColor at whatever the pointer had last touched,
//	    so Escape and clicking away were the same thing.
//
//	It is now a cell model - a theme matrix, the standard colours, the custom slots
//	ChooseColor() fills, and a "More Colors..." row, with an Automatic row ahead of them for a
//	host that enables one - laid out and painted by this file, driven by the mouse and by the
//	arrows, committed by Enter or Space and thrown away by Escape. Every cell is named in a
//	tooltip and the one holding the item's value is ringed when the popup opens. The popup
//	itself keeps the owned top level window it already was, but loses the WS_DLGFRAME caption
//	band that framed a palette with no caption, gains a drop shadow, and is now positioned
//	against the work area instead of dropped at the top left of the cell whatever is there.
//	ChooseColor() is still the escape hatch for a colour the palette does not hold.

#include "stdafx.h"
#include "PropTree.h"
#include "PropTree.rh"
#include "PropTreeItemColor.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// (ak) Popup geometry, in physical pixels like everything else PropTree draws - the control
// has no DPI story of its own and PropTreeList sizes its rows and its gutter in raw pixels.
//
// These are the UNSCALED bases. Nothing below reads them directly any more: every consumer
// goes through the Pop* accessors, which multiply by the item's scale percentage - see
// SetPopupScale() in the header and Scaled() below. Edit these to change the size the popup
// ships at; use the scale to change how big a given host wants it.

#define CLRPOP_COLUMNS		10		// swatches in a grid row
#define CLRPOP_MARGIN		6		// popup margin
#define CLRPOP_CELL			18		// grid pitch
#define CLRPOP_SWATCH		14		// painted swatch inside a cell - 2px of cell padding on
									// each side, which is what the selection ring draws into
#define CLRPOP_LABEL		17		// section label band
#define CLRPOP_WIDEROW		24		// Automatic / More Colors row
#define CLRPOP_GAP			6		// gap above a section label
#define CLRPOP_THEME_ROWS	5		// lightness steps of a theme hue
#define CLRPOP_CUSTOM_MAX	16		// slots ChooseColor() has

// Width of the popup client area, unscaled - the size it is at CLRPOP_SCALE_MIN, and the value
// the constructor seeds m_sizePopup with before anything reads it. The real width at an item's
// own scale is PopWidth(); the height comes out of LayoutCells(), which counts the sections
// that are really there - no custom colours and no Automatic row means a shorter popup.
#define CLRPOP_W			(CLRPOP_MARGIN * 2 + CLRPOP_COLUMNS * CLRPOP_CELL)

// (ak) Geometry of the attribute cell - the preview and the label DrawAttribute() paints in
// the tree, not in the popup.
//
// The preview is a rectangle this many times as wide as it is tall. What shipped was a square
// one row height across, which at 17 pixels is too small to tell two neighbouring hues apart
// and too small to read as the colour it stands for.
#define CLRATT_PREVIEW_WIDEN	3

// Gap between the preview and the label. PROPTREEITEM_SPACE, the gap the rest of the row uses,
// is private to PropTreeItem.cpp, so it is spelled out here rather than reached for.
#define CLRATT_TEXT_GAP			5

/////////////////////////////////////////////////////////////////////////////
// (ak) The palette.
//
// Ten hues times five lightness steps, the shape Office has used since 2007 and the one a
// user of any modern Windows application expects from a colour picker. Eight of the hues are
// hues the shipped 40 swatch table already held - the reds, the orange, the greens, the
// teals, the blue, the indigo, the purple and the grey - so the colours that were one click
// away before are still one click away; azure and dark yellow fill the two gaps that table
// had, and the lightness steps replace the four hand picked tints of each hue it offered.

static const COLORREF s_themeHue[CLRPOP_COLUMNS] =
{
	RGB(0xC0, 0x00, 0x00),	// dark red
	RGB(0xFF, 0x68, 0x20),	// orange
	RGB(0xBF, 0x8F, 0x00),	// dark yellow
	RGB(0x00, 0x93, 0x00),	// green
	RGB(0x38, 0x8E, 0x8E),	// teal
	RGB(0x00, 0x7A, 0xCC),	// azure
	RGB(0x00, 0x00, 0x8B),	// dark blue
	RGB(0x4B, 0x00, 0x82),	// indigo
	RGB(0x80, 0x00, 0x80),	// purple
	RGB(0x7F, 0x7F, 0x7F),	// grey
};

static const TCHAR* s_themeHueName[CLRPOP_COLUMNS] =
{
	_T("Dark Red"),  _T("Orange"),     _T("Dark Yellow"), _T("Green"),  _T("Teal"),
	_T("Azure"),     _T("Dark Blue"),  _T("Indigo"),      _T("Purple"), _T("Grey"),
};

// Percentage pulled towards white (negative) or towards black, per lightness step.
static const int s_themeShade[CLRPOP_THEME_ROWS] = { -60, -30, 0, 25, 50 };

static const TCHAR* s_themeShadeName[CLRPOP_THEME_ROWS] =
{
	_T("Lightest"), _T("Light"), _T(""), _T("Dark"), _T("Darkest"),
};

// The ten standard colours, the row Office puts under the theme matrix.
static const COLORREF s_stdColor[CLRPOP_COLUMNS] =
{
	RGB(0x00, 0x00, 0x00), RGB(0x80, 0x00, 0x00), RGB(0x00, 0x80, 0x00),
	RGB(0x80, 0x80, 0x00), RGB(0x00, 0x00, 0x80), RGB(0x80, 0x00, 0x80),
	RGB(0x00, 0x80, 0x80), RGB(0xC0, 0xC0, 0xC0), RGB(0x80, 0x80, 0x80),
	RGB(0xFF, 0xFF, 0xFF),
};

static const TCHAR* s_stdColorName[CLRPOP_COLUMNS] =
{
	_T("Black"),  _T("Maroon"), _T("Dark Green"), _T("Olive"), _T("Navy"),
	_T("Purple"), _T("Teal"),   _T("Silver"),     _T("Grey"),  _T("White"),
};

// Section label, indexed by CPropTreeItemColor::CellSection. The Automatic and the More
// Colors rows span the popup and carry no label of their own.
static const TCHAR* s_sectionLabel[] =
{
	NULL,					// SEC_AUTOMATIC
	_T("Theme Colors"),		// SEC_THEME
	_T("Standard Colors"),	// SEC_STANDARD
	_T("Custom Colors"),	// SEC_CUSTOM
	NULL,					// SEC_MORE
};

// (ak) Pull c towards target by percent, 0..100.
static COLORREF ShadeColor(COLORREF c, COLORREF target, int percent)
{
	const int dr = (int)GetRValue(target) - (int)GetRValue(c);
	const int dg = (int)GetGValue(target) - (int)GetGValue(c);
	const int db = (int)GetBValue(target) - (int)GetBValue(c);

	return RGB((int)GetRValue(c) + dr * percent / 100,
			   (int)GetGValue(c) + dg * percent / 100,
			   (int)GetBValue(c) + db * percent / 100);
}


// (ak) One cell of the theme matrix.
static COLORREF ThemeColor(int nCol, int nRow)
{
	const int nShade = s_themeShade[nRow];

	return (nShade < 0)
		? ShadeColor(s_themeHue[nCol], RGB(255, 255, 255), -nShade)
		: ShadeColor(s_themeHue[nCol], RGB(0, 0, 0), nShade);
}


// (ak) The 1px frame of a swatch. Taken from the fill rather than from a system colour, so
// that a white swatch on the light popup face still has an edge and so that the frame never
// fights the colour it holds.
static COLORREF FrameColor(COLORREF c)
{
	return ShadeColor(c, RGB(0, 0, 0), 35);
}


// (ak) Tooltip text: the name, then the value in hex - "#46C246" is what a user who wants to
// reproduce a colour somewhere else needs, and the old popup named nothing at all.
static CString CellTip(LPCTSTR pszName, COLORREF c)
{
	CString s(pszName);

	if (c != CLR_INVALID)
	{
		CString hex;
		hex.Format(_T("  #%02X%02X%02X"), GetRValue(c), GetGValue(c), GetBValue(c));
		s += hex;
	}

	return s;
}


// (ak) One 1px ring around a swatch, drawn nOffset pixels outwards into the padding of its own
// cell, so that the swatch keeps the same size in every state and the grid does not jiggle as
// the pointer moves over it. The cell leaves 2px of padding on each side, which is room for
// exactly the two rings DrawCell() uses: one at 1 for the position, one at 2 for the value.
static void DrawRing(CDC* pDC, const CRect& rcSwatch, COLORREF clr, int nOffset)
{
	CRect rc(rcSwatch);

	rc.InflateRect(nOffset, nOffset);
	pDC->Draw3dRect(&rc, clr, clr);
}


/////////////////////////////////////////////////////////////////////////////
// CPropTreeItemColor

COLORREF* CPropTreeItemColor::s_pColors = NULL;

// (ak) The scale a CPropTreeItemColor starts at. CLRPOP_SCALE_MIN, so an item whose host never
// calls SetDefaultPopupScale() is the size the popup has always been.
int CPropTreeItemColor::s_nDefaultScalePct = CLRPOP_SCALE_MIN;

// (ak) The one message hook the thread has, and the popup it is watching. See InstallPopupHook().
HHOOK				CPropTreeItemColor::s_hPopupHook = NULL;
CPropTreeItemColor*	CPropTreeItemColor::s_pHookOwner = NULL;

// (ak) The popup that is on screen, or NULL. See HidePopup() and the static in the header.
CPropTreeItemColor*	CPropTreeItemColor::s_pOpenPopup = NULL;

CPropTreeItemColor::CPropTreeItemColor() :
	m_cColor(0),
	m_cPrevColor(0),
	m_bInDialog(FALSE),
	m_bAllowAutomatic(FALSE),	// (ak) off - see SetAllowAutomatic() in the header
	m_nScalePct(s_nDefaultScalePct),
	m_nCellCount(0),
	m_nCurrent(-1),
	m_nHot(-1),
	m_bMoreDown(FALSE),
	m_bTracking(FALSE),
	// Placeholder at the unscaled size. LayoutCells() writes the real one - the item's own
	// scale, and a height that counts only the sections this popup has - before anything reads
	// it, and OnActivate() always runs BuildCells() (which ends in LayoutCells()) first.
	m_sizePopup(CLRPOP_W, CLRPOP_W)
{
}

CPropTreeItemColor::~CPropTreeItemColor()
{
	// (ak) Backstop. OnDestroy() is the normal place the hook is taken down; this covers an
	// item torn down without its popup window ever being destroyed. A hook outliving the popup
	// would keep eating every click the thread gets.
	RemovePopupHook();

	if (s_pOpenPopup == this)
		s_pOpenPopup = NULL;

	// (ak) And the window itself, if it is somehow still there. An item deleted with its
	// palette on screen would leave an HWND behind that no CPropTreeItemColor answers to any
	// more - nothing could ever hide it, because the object that hid it is the one going. The
	// handle map detaches m_hWnd on WM_NCDESTROY, so this is NULL on the normal teardown where
	// the owner took the window with it.
	if (m_hWnd != NULL)
		DestroyWindow();
}


BEGIN_MESSAGE_MAP(CPropTreeItemColor, CWnd)
	//{{AFX_MSG_MAP(CPropTreeItemColor)
	ON_WM_KILLFOCUS()
	ON_WM_PAINT()
	ON_WM_ERASEBKGND()
	ON_WM_CLOSE()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSELEAVE()
	ON_WM_SETCURSOR()
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_GETDLGCODE()
	ON_WM_KEYDOWN()
	ON_WM_CHAR()
	//}}AFX_MSG_MAP
	// (ak) Hand added: the mouse capture that lets the popup see a click outside itself, and
	// the two teardown notifications that come with holding one.
	ON_WM_RBUTTONDOWN()
	ON_WM_DESTROY()
END_MESSAGE_MAP()


/////////////////////////////////////////////////////////////////////////////
// CPropTreeItemColor - the item

void CPropTreeItemColor::SetDefaultColorsList(COLORREF* pColors)
{
	s_pColors = pColors;
}


void CPropTreeItemColor::SetAllowAutomatic(BOOL bAllow)
{
	m_bAllowAutomatic = bAllow;
}


COLORREF CPropTreeItemColor::GetItemValue()
{
	return m_cColor;
}


void CPropTreeItemColor::SetItemValue(COLORREF color)
{
	m_cColor = color;
}


void CPropTreeItemColor::AppendSearchValue(CString& sText)
{
	COLORREF color = GetItemValue();

	// CLR_INVALID is the "Automatic" row: no colour of its own, nothing to find.
	if (color == CLR_INVALID)
		return;

	// #RRGGBB is the only stable spelling of a colour. The swatch names in the
	// popup belong to the shared palette rather than to this row, and are the same
	// for every colour row, so matching them would match every colour row at once.
	CString sValue;
	sValue.Format(_T("#%02X%02X%02X"), GetRValue(color), GetGValue(color), GetBValue(color));

	sText += sValue;
	sText += _T(' ');
}



void CPropTreeItemColor::DrawAttribute(CDC* pDC, const RECT& rc)
{
	ASSERT(m_pProp!=NULL);

	CRect r(rc);
	
	// (ak) Normal weight, where the shipped control drew an editable attribute in bold and a
	// read only one in normal. The hex value below is data, not a heading, and bold made it
	// compete with the label on the left of the row. CPropTreeItemEdit::DrawAttribute() makes
	// the same call for the same reason.
	pDC->SelectObject(m_pProp->GetNormalFont());

	// (ak) Grayed while the whole tree is disabled AND while this row is read only - the
	// attribute has no colour bit in its mask, so the cell shows the inherited colour but
	// CPropTreeList::OnLButtonDown() never activates it. This is the colour side of the same
	// signal the font used to carry on its own, and it is what the disabled CColourPickerXP
	// of the classic page looked like.
	if (!m_pProp->IsWindowEnabled() || IsReadOnly())
		pDC->SetTextColor(GetSysColor(COLOR_GRAYTEXT));
	else
		pDC->SetTextColor(RGB(0,0,0));

	// (ak) CLR_INVALID is the value the Automatic row commits, for a host that enables one.
	// The cell shows the glyph that row shows instead of a colour, because such an attribute
	// does not have one - filling the preview with the colour it inherits would claim that it
	// does, and would make an inherited foreground indistinguishable from a black one. A host
	// that leaves the row off, as this page does, never gets here.
	const BOOL bAutomatic = (m_cColor == CLR_INVALID);

	CString s;

	if (bAutomatic)
		s = _T("Automatic");
	else
		// (ak) Hex, the way CSS and the toolbars that set these attributes spell a colour, and
		// the way the popup's own tooltips in CellTip() already do. "R = 255, G = 255, B = 255"
		// was three numbers to read where one value is now, and it was the widest label in the
		// column.
		s.Format(_T("#%02X%02X%02X"), GetRValue(m_cColor), GetGValue(m_cColor), GetBValue(m_cColor));

	// (ak) Measured before the preview is laid out, so the preview knows what it has to leave
	// room for. DrawItem() clips this cell to the attribute column, so a label pushed past the
	// right edge is not wrapped and not shrunk - it is simply cut, to "#FF".
	CRect rcText(rc);
	pDC->DrawText(s, rcText, DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_CALCRECT);

	// (ak) Inset one pixel at the top and the bottom. The top inset was already there; without
	// the bottom one the rect ran into the lower edge of the row and came out a pixel taller
	// than nSquare, so the height and the unit the width was measured in never agreed.
	r.top    += 1;
	r.bottom -= 1;

	// Height is the unit the square used, and keeping it as the unit keeps the proportions
	// whatever the font metrics of the row turn out to be.
	const int nSquare = r.Height();
	int nWid = nSquare*CLRATT_PREVIEW_WIDEN;

	// (ak) Clamped down to what is left of the column once the label has its measured width and
	// a gap either side. The label is the half that has to survive. The floor is one square -
	// what the shipped control drew at any width - so a pathologically narrow column degrades
	// to the old look instead of to nothing.
	const int nAvail = rc.right - rc.left - rcText.Width() - CLRATT_TEXT_GAP*2;

	if (nAvail < nWid)
		nWid = (nAvail > nSquare) ? nAvail : nSquare;

	r.right = r.left + nWid - 1;

	DrawSwatch(pDC, r, m_cColor, bAutomatic);

	r = rc;
	r.left += nWid + CLRATT_TEXT_GAP;

	pDC->DrawText(s, r, DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);
}


/////////////////////////////////////////////////////////////////////////////
// CPropTreeItemColor - the popup model

// (ak) The cell list the popup is laid out and painted from. Rebuilt on every activation:
// ChooseColor() writes the colours the user defines back into s_pColors, so the custom
// section a second opening shows is not the one the first one showed.
void CPropTreeItemColor::BuildCells()
{
	m_nCellCount = 0;

	// The "no colour of its own" row, for a host that asked for one - see SetAllowAutomatic()
	// in the header for why SQLTools' own page does not. Its value is CLR_INVALID, so
	// committing it tells that host the attribute has no colour of its own and inherits.
	if (m_bAllowAutomatic && m_nCellCount < CLRPOP_MAXCELLS)
	{
		ColorCell& c = m_cells[m_nCellCount++];
		c.kind    = CELL_AUTOMATIC;
		c.section = SEC_AUTOMATIC;
		c.color   = CLR_INVALID;
		c.col     = 0;
		c.row     = 0;
		c.name    = _T("Automatic - inherit the colour of the base attribute");
	}

	// Theme matrix: ten hues, five lightness steps each.
	for (int nRow = 0; nRow < CLRPOP_THEME_ROWS; nRow++)
	{
		for (int nCol = 0; nCol < CLRPOP_COLUMNS && m_nCellCount < CLRPOP_MAXCELLS; nCol++)
		{
			const COLORREF clr = ThemeColor(nCol, nRow);

			CString sName(s_themeHueName[nCol]);

			if (s_themeShadeName[nRow][0])
			{
				sName += _T(", ");
				sName += s_themeShadeName[nRow];
			}

			ColorCell& c = m_cells[m_nCellCount++];
			c.kind    = CELL_COLOR;
			c.section = SEC_THEME;
			c.color   = clr;
			c.col     = nCol;
			c.row     = nRow;
			c.name    = CellTip(sName, clr);
		}
	}

	// Standard colours.
	for (int nCol = 0; nCol < CLRPOP_COLUMNS && m_nCellCount < CLRPOP_MAXCELLS; nCol++)
	{
		ColorCell& c = m_cells[m_nCellCount++];
		c.kind    = CELL_COLOR;
		c.section = SEC_STANDARD;
		c.color   = s_stdColor[nCol];
		c.col     = nCol;
		c.row     = 0;
		c.name    = CellTip(s_stdColorName[nCol], s_stdColor[nCol]);
	}

	// The custom slots. A slot still holding the white the host seeded it with is not a
	// colour anybody chose, so it is left out and the section goes away when none was - a
	// page whose sets carry no colour at all would otherwise show a row of ten white
	// swatches. White really chosen in the dialog cannot be told apart from that, and does
	// not have to be: the standard row offers White one section up.
	if (s_pColors)
	{
		int nCustom = 0;

		for (int i = 0; i < CLRPOP_CUSTOM_MAX && m_nCellCount < CLRPOP_MAXCELLS; i++)
		{
			if (s_pColors[i] == RGB(255, 255, 255))
				continue;

			nCustom++;

			CString sName;
			sName.Format(_T("Custom Color %d"), nCustom);

			ColorCell& c = m_cells[m_nCellCount++];
			c.kind    = CELL_COLOR;
			c.section = SEC_CUSTOM;
			c.color   = s_pColors[i];
			c.col     = (nCustom - 1) % CLRPOP_COLUMNS;
			c.row     = (nCustom - 1) / CLRPOP_COLUMNS;
			c.name    = CellTip(sName, s_pColors[i]);
		}
	}

	// The escape hatch to the system colour dialog.
	if (m_nCellCount < CLRPOP_MAXCELLS)
	{
		ColorCell& c = m_cells[m_nCellCount++];
		c.kind    = CELL_MORE;
		c.section = SEC_MORE;
		c.color   = CLR_INVALID;
		c.col     = 0;
		c.row     = 0;
		c.name    = _T("More Colors... - the system colour dialog");
	}

	LayoutCells();
}


/////////////////////////////////////////////////////////////////////////////
// (ak) Popup scale
//
// One percentage drives everything about the popup's size: the rectangles LayoutCells()
// builds, the pixels the painting reads back out of those rectangles, and the face the text is
// drawn in. Scaling the cells without the text would leave the section labels and the "More
// Colors..." caption stranded in bands twice their height - not a bigger picker so much as a
// sparse one - so EnsurePopupFont() is part of this and not an afterthought.

// (ak) Neither setter can be handed a value that puts the popup past the ceiling or under the
// size it ships at.
//
// The floor is there because the swatch is CLRPOP_SWATCH inside a CLRPOP_CELL pitch, and the
// 2px of padding that leaves on each side is exactly the room the two selection rings
// DrawCell() paints need - one at offset 1 for the position, one at 2 for the value. Below
// 100% that padding goes to 1px and then to none, the rings start landing on the swatch
// itself, and the marking that says "this is the colour you already have" stops being
// distinguishable from the swatch it surrounds.
//
// The ceiling is CLRPOP_SCALE_MAX for the reason the header gives: PositionPopup() clamps to
// the work area, so past some size a "bigger" popup is just a cropped one.
static int ClampScale(int nPercent)
{
	if (nPercent < CLRPOP_SCALE_MIN)
		return CLRPOP_SCALE_MIN;

	if (nPercent > CLRPOP_SCALE_MAX)
		return CLRPOP_SCALE_MAX;

	return nPercent;
}


// (ak) MulDiv rather than a float, because the rest of the control is integer arithmetic and
// because the two ends of the range that matter are exact under it: 100% returns the constant
// unchanged, 200% returns precisely twice it. Rounding is to nearest, which is what keeps the
// swatch centred in its cell at the rungs between - LayoutCells() halves the leftover padding,
// and a fraction that always rounded the same way would sit the swatch visibly off centre at
// 125% and 175%.
int CPropTreeItemColor::Scaled(int nBase) const
{
	return MulDiv(nBase, m_nScalePct, 100);
}


int CPropTreeItemColor::PopMargin() const
{
	return Scaled(CLRPOP_MARGIN);
}


int CPropTreeItemColor::PopCell() const
{
	return Scaled(CLRPOP_CELL);
}


int CPropTreeItemColor::PopSwatch() const
{
	return Scaled(CLRPOP_SWATCH);
}


int CPropTreeItemColor::PopLabel() const
{
	return Scaled(CLRPOP_LABEL);
}


int CPropTreeItemColor::PopWideRow() const
{
	return Scaled(CLRPOP_WIDEROW);
}


int CPropTreeItemColor::PopGap() const
{
	return Scaled(CLRPOP_GAP);
}


// (ak) Width of the popup client area at this item's scale. The height is not a constant - it
// comes out of LayoutCells(), which counts only the sections this popup really has.
int CPropTreeItemColor::PopWidth() const
{
	return PopMargin() * 2 + CLRPOP_COLUMNS * PopCell();
}


// (ak) Set the size of the palette this item opens. See the header for the contract.
void CPropTreeItemColor::SetPopupScale(int nPercent)
{
	const int nClamped = ClampScale(nPercent);

	if (nClamped == m_nScalePct)
		return;

	m_nScalePct = nClamped;

	// The derived face belongs to the scale that has just gone. Dropped here rather than left
	// for EnsurePopupFont() to notice, so that "does m_fontPopup hold a font" keeps meaning
	// "does it hold one at the current scale" and the paint path stays a single test.
	if (m_fontPopup.GetSafeHandle() != NULL)
		m_fontPopup.DeleteObject();

	// Every rectangle in the model, and m_sizePopup through them, is a product of the scale.
	LayoutCells();

	if (IsWindow(m_hWnd))
	{
		// The tools are registered against the rectangles the old scale laid out, so they now
		// sit over the wrong cells. BuildToolTips() deletes the whole id range before it adds
		// anything, so a second call moves them rather than doubling them up.
		BuildToolTips();

		// FALSE, not TRUE: the popup is already on screen and showing it again is not what was
		// asked for. PositionPopup() reads the m_sizePopup LayoutCells() has just written, so
		// this resizes the window where it stands and re-clamps it against the work area - a
		// scale up can push a popup that fitted before off the bottom or the right of the
		// screen, and that test only runs in here.
		PositionPopup(FALSE);

		// FALSE: no erase. OnEraseBkgnd() fills, but the cells repaint themselves over their
		// own padding in DrawCell(), and a full erase would flash the face colour first.
		Invalidate(FALSE);
	}
}


// (ak) The font-size style entry point: rung 0 is the floor, rung CLRPOP_SCALE_STEPS is the
// ceiling, and the rungs between are evenly spaced. The spacing is derived from the two
// constants rather than spelled out as 25, so changing the number of rungs keeps the top one
// at exactly double.
void CPropTreeItemColor::SetPopupScaleStep(int nStep)
{
	if (nStep < 0)
		nStep = 0;

	if (nStep > CLRPOP_SCALE_STEPS)
		nStep = CLRPOP_SCALE_STEPS;

	SetPopupScale(CLRPOP_SCALE_MIN +
		MulDiv(CLRPOP_SCALE_MAX - CLRPOP_SCALE_MIN, nStep, CLRPOP_SCALE_STEPS));
}


// (ak) The nearest rung to the percentage held, so a scale set directly - 137%, say - reports
// the step a ladder control would be showing. MulDiv rounds, which is what makes 137 come back
// as rung 1: 125% is 12 away and 150% is 13.
int CPropTreeItemColor::GetPopupScaleStep() const
{
	return MulDiv((m_nScalePct - CLRPOP_SCALE_MIN) * CLRPOP_SCALE_STEPS,
				  1, CLRPOP_SCALE_MAX - CLRPOP_SCALE_MIN);
}


void CPropTreeItemColor::SetDefaultPopupScale(int nPercent)
{
	s_nDefaultScalePct = ClampScale(nPercent);
}


int CPropTreeItemColor::GetDefaultPopupScale()
{
	return s_nDefaultScalePct;
}


// (ak) The face the popup's text is drawn in: the tree's normal font at this item's scale.
//
// At CLRPOP_SCALE_MIN there is nothing to derive, so the shared font is handed straight back
// and an unscaled popup - every one of them, until a host asks otherwise - costs no GDI object
// of its own.
//
// Only the height is scaled. lfWidth is a request the font mapper honours approximately at
// best, and scaling it alongside the height makes the mapper pick a different face on some
// systems instead of a larger one: the text grows and changes shape, which is not what "a
// bigger picker" means. lfHeight is negative for a font specified by character height, which
// the tree's is; MulDiv carries the sign through, so the scaled height is still a character
// height and still negative.
CFont* CPropTreeItemColor::EnsurePopupFont()
{
	CFont* pNormal = CPropTree::GetNormalFont();

	if (m_nScalePct == CLRPOP_SCALE_MIN)
		return pNormal;

	if (m_fontPopup.GetSafeHandle() != NULL)
		return &m_fontPopup;

	if (pNormal == NULL)
		return NULL;

	LOGFONT lf;

	if (!pNormal->GetLogFont(&lf))
		return pNormal;

	const LONG nScaled = MulDiv(lf.lfHeight, m_nScalePct, 100);

	// A scale close to 100% can round back to the height the font already has - 101% of a 12px
	// face is still 12px - and CreateFontIndirect() on an identical LOGFONT would buy a second
	// GDI object that draws exactly the same glyphs. Hand the shared font back and leave
	// m_fontPopup empty, which is what the test above then keeps doing on every later paint.
	if (nScaled == lf.lfHeight)
		return pNormal;

	lf.lfHeight = nScaled;

	if (!m_fontPopup.CreateFontIndirect(&lf))
		return pNormal;

	return &m_fontPopup;
}


// (ak) Rectangles and popup size, straight out of the cell list. Sections are walked in
// CellSection order whether or not the list holds them, so an absent section - no custom
// colours, no Automatic row - costs nothing and the ones below it simply move up.
void CPropTreeItemColor::LayoutCells()
{
	int nRows[SEC_COUNT];

	ZeroMemory(nRows, sizeof(nRows));

	for (int i = 0; i < m_nCellCount; i++)
	{
		if (m_cells[i].row + 1 > nRows[m_cells[i].section])
			nRows[m_cells[i].section] = m_cells[i].row + 1;
	}

	// Pass one: the height, so PositionPopup() knows how much room to look for.
	//
	// (ak) Every dimension here comes from the Pop* accessors rather than the CLRPOP_ constants
	// they scale, so the whole popup - margins, pitch, bands, gaps - moves together and the
	// proportions the shipped geometry had are the proportions it has at 200%.
	int y = PopMargin();

	for (int s = 0; s < SEC_COUNT; s++)
	{
		if (!nRows[s])
			continue;

		if (y > PopMargin())
			y += PopGap();

		if (s_sectionLabel[s])
			y += PopLabel();

		y += (s == SEC_AUTOMATIC || s == SEC_MORE) ? PopWideRow() : nRows[s] * PopCell();
	}

	m_sizePopup.cx = PopWidth();
	m_sizePopup.cy = y + PopMargin();

	// Pass two: the rectangles, walked the same way so the two passes cannot disagree.
	y = PopMargin();

	for (int s = 0; s < SEC_COUNT; s++)
	{
		if (!nRows[s])
			continue;

		if (y > PopMargin())
			y += PopGap();

		// The top of the section's own cells - below its label band, if it has one.
		const int nTop = y + (s_sectionLabel[s] ? PopLabel() : 0);

		y = nTop + ((s == SEC_AUTOMATIC || s == SEC_MORE)
			? PopWideRow() : nRows[s] * PopCell());

		const BOOL bWide = (s == SEC_AUTOMATIC || s == SEC_MORE);

		for (int i = 0; i < m_nCellCount; i++)
		{
			ColorCell& c = m_cells[i];

			if (c.section != s)
				continue;

			if (bWide)
			{
				c.rcCell.SetRect(PopMargin(), nTop,
								 PopMargin() + CLRPOP_COLUMNS * PopCell(),
								 nTop + PopWideRow());
			}
			else
			{
				const int x = PopMargin() + c.col * PopCell();
				const int t = nTop + c.row * PopCell();

				c.rcCell.SetRect(x, t, x + PopCell(), t + PopCell());
			}

			// The swatch sits centred in its cell, except on the two rows that span the
			// popup - there it is the glyph at the left of a label, not a cell of a grid.
			// The padding around it is what the hover and selection rings are drawn into, and
			// because both the cell and the swatch are scaled by the same percentage that
			// padding scales with them: 2px a side at 100%, 4px at 200%, always room for the
			// two rings DrawCell() puts there.
			if (bWide)
			{
				const int nGlyph = Scaled(4);
				const int t = c.rcCell.top + (c.rcCell.Height() - PopSwatch()) / 2;

				c.rcSwatch.SetRect(c.rcCell.left + nGlyph, t,
								   c.rcCell.left + nGlyph + PopSwatch(), t + PopSwatch());
			}
			else
			{
				const int nInset = (c.rcCell.Width() - PopSwatch()) / 2;

				c.rcSwatch.SetRect(c.rcCell.left + nInset, c.rcCell.top + nInset,
								   c.rcCell.left + nInset + PopSwatch(),
								   c.rcCell.top + nInset + PopSwatch());
			}
		}
	}
}


// (ak) One tooltip per cell, holding the colour's name and its value in hex. The 40 swatches
// of the old popup named nothing, which is most of the reason picking from it felt like
// guessing.
void CPropTreeItemColor::BuildToolTips()
{
	if (!IsWindow(m_hWnd))
		return;

	if (!IsWindow(m_toolTip.m_hWnd))
	{
		if (!m_toolTip.Create(this, TTS_NOPREFIX|TTS_ALWAYSTIP))
			return;
	}

	// (ak) Tool ids run from 1, never from 0, and AddTool() and DelTool() are given the same
	// range. CToolTipCtrl does not read zero as an id that happens to be zero, it reads it as
	// "no id": FillInToolInfo() then registers a TTF_IDISHWND tool whose window is the popup's
	// *parent* and whose id is the popup's own HWND - a whole-window tooltip, not a cell. And
	// AddTool() asserts that a rect and an id are either both supplied or both absent, so the
	// zero the first cell used to be handed tripped that assert as soon as the popup opened.
	const UINT nIdFirst = 1;
	const UINT nIdLast  = CLRPOP_MAXCELLS;	// inclusive; 80 cells map onto ids 1..80

	// The id range above is only wide enough if BuildCells() really does cap the array at
	// CLRPOP_MAXCELLS. It does - every insert tests the count - but assert it here, because a
	// cell that lands outside the range would keep a tooltip from the previous opening that
	// DelTool() can no longer reach.
	ASSERT(m_nCellCount >= 0 && m_nCellCount <= CLRPOP_MAXCELLS);

	// The cells were rebuilt, so the rectangles and the names of the previous opening are
	// stale - drop them all and register the ones that are there now. Deleting an id that was
	// never added is harmless: TTM_DELTOOL simply finds nothing to match.
	for (UINT nId = nIdFirst; nId <= nIdLast; nId++)
		m_toolTip.DelTool(this, nId);

	for (int i = 0; i < m_nCellCount; i++)
		m_toolTip.AddTool(this, m_cells[i].name, &m_cells[i].rcCell, nIdFirst + (UINT)i);

	m_toolTip.SetMaxTipWidth(260);
}


// (ak) The cell under a point, or -1 for the padding and the labels around the cells. Every
// mouse handler hit tests through this, which is the fix for OnLButtonDown() committing the
// index OnMouseMove() had last set.
int CPropTreeItemColor::CellFromPoint(const CPoint& pt) const
{
	for (int i = 0; i < m_nCellCount; i++)
	{
		if (m_cells[i].rcCell.PtInRect(pt))
			return i;
	}

	return -1;
}


// (ak) The cell the popup opens on: the one that holds the item's value, so the user sees at
// once what the attribute is set to. A value the palette does not hold - one ChooseColor()
// returned - has no cell of its own, so the nearest colour cell stands in for it and Enter
// still does something predictable.
int CPropTreeItemColor::CellForColor(COLORREF color) const
{
	int nNearest = -1;
	int nBest = 0;

	for (int i = 0; i < m_nCellCount; i++)
	{
		if (m_cells[i].color == color)
			return i;

		if (m_cells[i].kind != CELL_COLOR || color == CLR_INVALID)
			continue;

		const int dr = (int)GetRValue(m_cells[i].color) - (int)GetRValue(color);
		const int dg = (int)GetGValue(m_cells[i].color) - (int)GetGValue(color);
		const int db = (int)GetBValue(m_cells[i].color) - (int)GetBValue(color);
		const int d  = dr * dr + dg * dg + db * db;

		if (nNearest < 0 || d < nBest)
		{
			nBest = d;
			nNearest = i;
		}
	}

	if (nNearest >= 0)
		return nNearest;

	return m_nCellCount ? 0 : -1;
}


int CPropTreeItemColor::FirstCellOfSection(int nSection) const
{
	for (int i = 0; i < m_nCellCount; i++)
	{
		if (m_cells[i].section == nSection)
			return i;
	}

	return -1;
}


/////////////////////////////////////////////////////////////////////////////
// CPropTreeItemColor - popup navigation

void CPropTreeItemColor::SetCurrent(int nCell)
{
	if (nCell < 0 || nCell >= m_nCellCount || nCell == m_nCurrent)
		return;

	const int nOld = m_nCurrent;

	m_nCurrent = nCell;

	if (!IsWindow(m_hWnd))
		return;

	// Only the two cells that changed state are repainted, so holding an arrow key down
	// walks the grid without redrawing it.
	if (nOld >= 0 && nOld < m_nCellCount)
		InvalidateRect(&m_cells[nOld].rcCell, FALSE);

	InvalidateRect(&m_cells[nCell].rcCell, FALSE);
	UpdateWindow();
}


// (ak) Arrow key navigation. The popup is not a uniform grid - the Automatic and the More
// Colors rows span it, the custom section is one or two rows - so the target is found
// geometrically: of the cells that lie in the direction of travel, the one whose off axis
// offset is smallest, and of those the nearest along the axis. Wrapping at the edges keeps
// every cell reachable with the arrows alone, without the user having to know the shape.
void CPropTreeItemColor::MoveCurrent(int dCol, int dRow)
{
	if (!m_nCellCount)
		return;

	if (m_nCurrent < 0)
	{
		SetCurrent(CellForColor(m_cColor));
		return;
	}

	const CRect& rcFrom = m_cells[m_nCurrent].rcCell;

	// A full width row has no column of its own, so vertical travel aims at the first one
	// rather than at whatever the middle of the row happens to be over.
	const int cx = (m_cells[m_nCurrent].kind == CELL_COLOR)
		? rcFrom.CenterPoint().x
		: rcFrom.left + PopCell() / 2;
	const int cy = rcFrom.CenterPoint().y;

	int nBest = -1;
	int nBestOff = 0;
	int nBestAxis = 0;

	for (int i = 0; i < m_nCellCount; i++)
	{
		if (i == m_nCurrent)
			continue;

		const CPoint ptMid = m_cells[i].rcCell.CenterPoint();
		const int dx = ptMid.x - cx;
		const int dy = ptMid.y - cy;

		// Behind us, or exactly level, is not in the direction of travel.
		if (dCol && dx * dCol <= 0)
			continue;

		if (dRow && dy * dRow <= 0)
			continue;

		const int nOff  = dCol ? abs(dy) : abs(dx);
		const int nAxis = dCol ? abs(dx) : abs(dy);

		if (nBest < 0 || nOff < nBestOff || (nOff == nBestOff && nAxis < nBestAxis))
		{
			nBest = i;
			nBestOff = nOff;
			nBestAxis = nAxis;
		}
	}

	if (nBest < 0)
	{
		// Nothing in that direction: wrap to the far end of the axis.
		for (int i = 0; i < m_nCellCount; i++)
		{
			if (i == m_nCurrent)
				continue;

			const CPoint ptMid = m_cells[i].rcCell.CenterPoint();
			const int nOff  = dCol ? abs(ptMid.y - cy) : abs(ptMid.x - cx);
			const int nAxis = dCol ? abs(ptMid.x - cx) : abs(ptMid.y - cy);

			if (nBest < 0 || nOff < nBestOff || (nOff == nBestOff && nAxis < nBestAxis))
			{
				nBest = i;
				nBestOff = nOff;
				nBestAxis = nAxis;
			}
		}
	}

	SetCurrent(nBest);
}


// (ak) Page Up and Page Down jump a whole section, which is how the popup is read. Sections
// the model does not hold are skipped, so a page without custom colours still steps cleanly
// between Automatic, Theme, Standard and More Colors.
void CPropTreeItemColor::MoveCurrentSection(int dSection)
{
	if (!m_nCellCount)
		return;

	int nSection = (m_nCurrent >= 0) ? m_cells[m_nCurrent].section : SEC_THEME;

	for (int nStep = 0; nStep < SEC_COUNT; nStep++)
	{
		nSection += dSection;

		if (nSection < 0)
			nSection = SEC_COUNT - 1;
		else if (nSection >= SEC_COUNT)
			nSection = 0;

		const int nCell = FirstCellOfSection(nSection);

		if (nCell >= 0 && nCell != m_nCurrent)
		{
			SetCurrent(nCell);
			return;
		}
	}
}


/////////////////////////////////////////////////////////////////////////////
// CPropTreeItemColor - committing a cell

// (ak) The one place a cell is turned into a committed value, reached from the mouse and
// from Enter/Space alike so the two cannot drift apart.
void CPropTreeItemColor::CommitCell(int nCell)
{
	if (nCell < 0 || nCell >= m_nCellCount)
		return;

	if (m_cells[nCell].kind == CELL_MORE)
	{
		ChooseMoreColors();
		return;
	}

	// The Automatic row commits CLR_INVALID, which is exactly what a colour cell would
	// commit if it held that value - the caller reads it back through GetItemValue().
	m_cColor = m_cells[nCell].color;

	CommitChanges();

	// CommitChanges() -> OnCommit() hid the popup, which took the focus with it. Hand the
	// focus back to the tree the way CPropTreeItemEdit::OnKeyDown() does after Enter, or the
	// next arrow key goes wherever Windows happened to leave the focus.

	ASSERT(m_pProp != NULL);
	m_pProp->GetCtrlParent()->SetFocus();
}


// (ak) The "More Colors..." row. ChooseColor() stays the escape hatch for a colour the
// palette does not hold; the custom slots the user defines in it go back into s_pColors,
// which is why the cells are rebuilt on the next opening.
//
// It is modal, so the popup hides under it and m_bInDialog keeps the WM_KILLFOCUS that
// follows from committing a half chosen value. Cancelling brings the popup back on the value
// it had, unchanged; picking commits and closes, the same as any colour cell.
void CPropTreeItemColor::ChooseMoreColors()
{
	CHOOSECOLOR cc;
	COLORREF clr[CLRPOP_CUSTOM_MAX];

	// A host that never called SetDefaultColorsList() still gets 16 slots, so its user can
	// define colours inside this dialog - they simply are not remembered between runs.
	memset(clr, 0xff, sizeof(COLORREF) * CLRPOP_CUSTOM_MAX);
	clr[0] = (m_cColor == CLR_INVALID) ? RGB(0,0,0) : m_cColor;

	ZeroMemory(&cc, sizeof(CHOOSECOLOR));
	cc.Flags = CC_FULLOPEN|CC_ANYCOLOR|CC_RGBINIT;
	cc.lStructSize = sizeof(CHOOSECOLOR);
	cc.hwndOwner = m_hWnd;
	cc.rgbResult = (m_cColor == CLR_INVALID) ? RGB(0,0,0) : m_cColor;
	cc.lpCustColors = s_pColors ? s_pColors : clr;

	m_bInDialog = TRUE;

	ASSERT(m_pProp != NULL);
	m_pProp->DisableInput();

	// (ak) The hook goes before the dialog comes up. ChooseColor() runs its own modal loop
	// on THIS thread, so a hook left installed would see every click in that dialog as a click
	// outside the palette and cancel the item from underneath it.
	RemovePopupHook();

	// Hide rather than let the modal dialog sit on top of a palette that is dead for the
	// length of its own loop. m_bInDialog, not SW_HIDE, is what stops the kill focus from
	// committing.
	ShowWindow(SW_HIDE);

	const BOOL bPicked = ChooseColor(&cc) ? TRUE : FALSE;

	m_pProp->DisableInput(FALSE);

	m_bInDialog = FALSE;
	m_bMoreDown = FALSE;

	if (bPicked)
	{
		m_cColor = cc.rgbResult;
		CommitChanges();
	}
	else
	{
		// Cancelled: the popup comes back where it was, on the cell it had open, and the
		// attribute keeps the colour it had before the dialog was opened.
		PositionPopup(TRUE);

		// (ak) It is the open popup again, so it is recorded as one - the hide before the dialog
		// went out through ShowWindow() rather than HidePopup() precisely so that this record,
		// and the identity of the popup the hook belongs to, survived the modal loop.
		s_pOpenPopup = this;

		m_nHot = -1;
		m_nCurrent = -1;
		SetCurrent(CellForColor(m_cColor));

		SetFocus();
		Invalidate(FALSE);

		// (ak) The popup is open again, so it needs the hook again - without it a click
		// outside would leave it sitting there, which is the bug this mechanism exists for.
		InstallPopupHook();
		return;
	}

	m_pProp->GetCtrlParent()->SetFocus();
}


/////////////////////////////////////////////////////////////////////////////
// CPropTreeItemColor - painting the popup

void CPropTreeItemColor::OnPaint()
{
	CPaintDC dc(this);

	// (ak) The tree's normal font at this item's scale - the shared font itself at 100%, so an
	// unscaled popup pays for no GDI object of its own. The section labels and the two
	// full-width row captions are the only text in here, and a popup whose cells had doubled
	// while its text stayed put would read as sparse rather than as larger.
	CFont* pFont = EnsurePopupFont();

	if (pFont != NULL)
		dc.SelectObject(pFont);

	dc.SetBkMode(TRANSPARENT);

	for (int i = 0; i < m_nCellCount; i++)
		DrawCell(&dc, m_cells[i]);

	// (ak) A one pixel border in the shadow colour, which is what reads as the edge of a
	// popup on a light face - the dialog frame the old WS_DLGFRAME style provided drew a
	// whole title bar band around a palette that has no caption.
	CRect rcClient;
	GetClientRect(&rcClient);
	dc.Draw3dRect(&rcClient, GetSysColor(COLOR_3DSHADOW), GetSysColor(COLOR_3DSHADOW));
}


BOOL CPropTreeItemColor::OnEraseBkgnd(CDC* pDC)
{
	CRect rc;
	GetClientRect(&rc);
	pDC->FillSolidRect(&rc, GetSysColor(COLOR_3DFACE));

	return TRUE;
}


// (ak) The band above a section's first cell. Drawn from the cell rather than from a
// rectangle table, so the label of a section that is absent is simply never asked for.
void CPropTreeItemColor::DrawSectionLabel(CDC* pDC, int nSection, int y)
{
	if (nSection < 0 || nSection >= _countof(s_sectionLabel) || !s_sectionLabel[nSection])
		return;

	CRect rc(PopMargin(), y, PopMargin() + CLRPOP_COLUMNS * PopCell(), y + PopLabel());

	pDC->SetTextColor(GetSysColor(COLOR_BTNTEXT));
	pDC->DrawText(s_sectionLabel[nSection], &rc, DT_SINGLELINE|DT_VCENTER|DT_LEFT|DT_NOPREFIX);
}


// (ak) One swatch. bAutomatic draws the "no colour of its own" glyph - a white square with
// the red diagonal that Word and the classic CColourPickerXP use for their Automatic entry -
// so the row reads as the absence of a colour rather than as the colour white.
void CPropTreeItemColor::DrawSwatch(CDC* pDC, const CRect& rc, COLORREF color, BOOL bAutomatic)
{
	if (!bAutomatic)
	{
		pDC->FillSolidRect(&rc, color);
		pDC->Draw3dRect(&rc, FrameColor(color), FrameColor(color));
		return;
	}

	pDC->FillSolidRect(&rc, RGB(255, 255, 255));

	CPen penRed(PS_SOLID, 0, RGB(0xC0, 0x00, 0x00));
	CPen* pOldPen = pDC->SelectObject(&penRed);

	pDC->MoveTo(rc.left, rc.bottom - 1);
	pDC->LineTo(rc.right - 1, rc.top);

	pDC->SelectObject(pOldPen);

	pDC->Draw3dRect(&rc, RGB(0x80, 0x80, 0x80), RGB(0x80, 0x80, 0x80));
}


// (ak) One cell. Two independent marks are painted on top of each other rather than instead of
// each other, so a cell can hold the value and the position at the same time without one
// hiding the other:
//
//	the value the item has			-> 1px ring in the accent colour, on the cell's outer edge
//	the cell the arrows/pointer is on	-> 1px ring in a darkened accent, around the swatch
//
// A cell that is both shows two nested rectangles, which is the marking the colour picker this
// replaced used and the one users of it recognise.
//
// Both are solid. The position mark used to be DrawFocusRect(), and that was the wrong tool
// twice over: its dotted XOR pattern is all but invisible against the light popup face, and
// because XOR inverts instead of replacing, it left the trails that the background fill below
// had to be added to cover.
void CPropTreeItemColor::DrawCell(CDC* pDC, const ColorCell& cell)
{
	const int  nIndex    = (int)(&cell - m_cells);
	const BOOL bHot      = (m_nHot == nIndex);
	const BOOL bCurrent  = (m_nCurrent == nIndex);

	// The More Colors row carries CLR_INVALID like the Automatic row does, and must not read
	// as selected just because the item has no colour of its own.
	const BOOL bSelected = (cell.kind != CELL_MORE) && (cell.color == m_cColor);

	const COLORREF clrAccent = GetSysColor(COLOR_HIGHLIGHT);
	const COLORREF clrFocus  = ShadeColor(clrAccent, RGB(0, 0, 0), 45);

	// (ak) Paint the cell's own background before anything else. Every mark a cell carries -
	// the hover ring, the selection ring, the focus ring - is drawn OUTSIDE the swatch, in the
	// padding between the swatch and the edge of the cell. The callers repaint one cell at a
	// time with InvalidateRect(..., FALSE), which asks for no erase, so without this fill the
	// padding keeps the rings of the state the cell had before: the trails that were left
	// behind as the arrows or the pointer walked the grid. Filling here rather than passing
	// TRUE keeps the repaint to the single cell and does not depend on WM_ERASEBKGND reaching
	// us at all.
	pDC->FillSolidRect(&cell.rcCell, GetSysColor(COLOR_3DFACE));

	// The label of the section this cell opens, in the band above it.
	if (cell.col == 0 && cell.row == 0)
		DrawSectionLabel(pDC, cell.section, cell.rcCell.top - PopLabel());

	if (cell.kind == CELL_MORE)
	{
		// A flat row across the popup. It fills rather than rings, because there is no swatch
		// here to ring. The separator sits just above it, in the gap the layout left.
		pDC->FillSolidRect(cell.rcCell.left, cell.rcCell.top - PopMargin(),
						   cell.rcCell.Width(), 1, GetSysColor(COLOR_3DSHADOW));

		const BOOL bOn = bHot || bCurrent || m_bMoreDown;

		if (bOn)
			pDC->FillSolidRect(&cell.rcCell, clrAccent);

		// A four quadrant glyph: the hint that this row leaves the popup instead of picking
		// from it.
		CRect rcGlyph(cell.rcSwatch);
		const int nHalf = rcGlyph.Width() / 2;

		pDC->FillSolidRect(CRect(rcGlyph.left, rcGlyph.top,
								 rcGlyph.left + nHalf, rcGlyph.top + nHalf), RGB(0xE0,0x20,0x20));
		pDC->FillSolidRect(CRect(rcGlyph.left + nHalf, rcGlyph.top,
								 rcGlyph.right, rcGlyph.top + nHalf), RGB(0x30,0xA0,0x30));
		pDC->FillSolidRect(CRect(rcGlyph.left, rcGlyph.top + nHalf,
								 rcGlyph.left + nHalf, rcGlyph.bottom), RGB(0x20,0x80,0xD0));
		pDC->FillSolidRect(CRect(rcGlyph.left + nHalf, rcGlyph.top + nHalf,
								 rcGlyph.right, rcGlyph.bottom), RGB(0xF0,0xC0,0x20));

		pDC->Draw3dRect(&rcGlyph, GetSysColor(bOn ? COLOR_HIGHLIGHTTEXT : COLOR_3DSHADOW),
								  GetSysColor(bOn ? COLOR_HIGHLIGHTTEXT : COLOR_3DSHADOW));

		pDC->SetTextColor(GetSysColor(bOn ? COLOR_HIGHLIGHTTEXT : COLOR_BTNTEXT));

		CRect rcText(cell.rcCell);
		rcText.left  = rcGlyph.right + PopMargin();
		rcText.right -= PopMargin();
		pDC->DrawText(_T("More Colors..."), &rcText,
					  DT_SINGLELINE|DT_VCENTER|DT_LEFT|DT_NOPREFIX);
	}
	else
	{
		DrawSwatch(pDC, cell.rcSwatch, cell.color, cell.kind == CELL_AUTOMATIC);

		// (ak) The offsets are scaled, and then capped at the padding the layout actually left
		// around the swatch - half of what is over once the scaled swatch is taken out of the
		// scaled cell, which is 2px a side at 100% and 4px at 200% but 2px at 125%, 150% and
		// 175%, where MulDiv rounds the pitch and the swatch to the same whole pixel.
		//
		// Without the cap the outer ring is asked for at offset 2 where only 1px or none exists,
		// InflateRect() carries it past the edge of the cell, and the ring is clipped - or
		// drawn across the neighbouring swatch. The cap costs nothing at 100%, where the padding
		// is exactly the two offsets the rings have always used, and at 200%, where 4px of
		// padding takes both unclipped.
		const int nPad = (cell.rcCell.Width() - cell.rcSwatch.Width()) / 2;
		const int nOuter = min(Scaled(2), nPad);
		const int nInner = min(Scaled(1), nOuter);

		// The value, on the outer edge of the cell. This is the mark that stays put while the
		// user browses: it is the colour the attribute has now, and the colour the popup leaves
		// behind if it is closed without committing.
		if (bSelected && nOuter > 0)
			DrawRing(pDC, cell.rcSwatch, clrAccent, nOuter);

		// The position, immediately around the swatch. Hovering moves it as well -
		// OnMouseMove() calls SetCurrent() - so this one ring covers the arrows and the
		// pointer, and a cell that holds the value and the position shows the two nested.
		// At the floor rung the two offsets coincide, so only the inner one is drawn and it is
		// the value ring that is dropped rather than the position: where one can be seen, it is
		// the one tracking what the pointer is over.
		if ((bCurrent || bHot) && nInner > 0)
			DrawRing(pDC, cell.rcSwatch, clrFocus, nInner);

		if (cell.kind == CELL_AUTOMATIC)
		{
			const BOOL bOn = bHot || bCurrent || bSelected;

			pDC->SetTextColor(GetSysColor(bOn ? COLOR_HIGHLIGHT : COLOR_BTNTEXT));

			CRect rcText(cell.rcCell);
			rcText.left  = cell.rcSwatch.right + PopMargin();
			rcText.right -= PopMargin();
			pDC->DrawText(_T("Automatic"), &rcText,
						  DT_SINGLELINE|DT_VCENTER|DT_LEFT|DT_NOPREFIX);
		}
	}
}


/////////////////////////////////////////////////////////////////////////////
// CPropTreeItemColor - popup lifetime

void CPropTreeItemColor::OnMove()
{
	// (ak) The attribute cell moved - the tree scrolled, was relaid out, or the page resized.
	// Follow it, or the popup hangs over the row that used to be there. The shipped control
	// left this empty, which is why the palette stayed behind when the tree was scrolled
	// while it was open.
	if (IsWindow(m_hWnd) && IsWindowVisible())
		PositionPopup(FALSE);
}


void CPropTreeItemColor::OnRefresh()
{
	// (ak) The host re-read the value out of the attribute. Keep the popup's selection on it,
	// so the ring still marks the colour that is really in effect.
	if (IsWindow(m_hWnd) && IsWindowVisible())
	{
		m_nCurrent = -1;
		SetCurrent(CellForColor(m_cColor));
		Invalidate(FALSE);
	}
}


void CPropTreeItemColor::OnCommit()
{
	// (ak) HidePopup(), not a bare ShowWindow(SW_HIDE). The hook has to come down before the
	// window does - one left running for a popup that is no longer there would swallow every
	// click the thread gets, leaving the tree underneath looking frozen - and the record of
	// which popup is open has to go with it. HidePopup() does all three, in that order, and
	// does them whether or not the item still reads as activated.
	HidePopup();
}


void CPropTreeItemColor::OnActivate()
{
	// (ak) One palette on screen at a time. If another item's popup is still up - its item was
	// deactivated by a path that never reached its window - close it here. Left alone it stays
	// up with no hook watching for it and no activation flag behind it, and there is then no
	// input at all that can dismiss it, which is what "more than one picker, none of them
	// closing" actually looked like.
	if (s_pOpenPopup != NULL && s_pOpenPopup != this)
	{
		s_pOpenPopup->ForceClose();
	}

	s_pOpenPopup = this;

	// (ak) Saved for OnCancel(). The shipped control saved it here too and then never read it
	// back - its OnCancel() only hid the window - so the value sat unused while Escape left
	// the item on whatever colour the pointer had last touched.
	m_cPrevColor = m_cColor;

	ASSERT(m_pProp != NULL);

	// The custom slots can have changed since the last opening - ChooseColor() writes into
	// them - so the model is rebuilt every time the popup comes up.
	BuildCells();

	if (!IsWindow(m_hWnd))
	{
		LPCTSTR pszClassName;

		// (ak) CS_DROPSHADOW gives the popup the drop shadow a top level popup gets, which is
		// what separates the palette from the page behind it once the caption band below is
		// gone. The shipped class did not ask for it.
		pszClassName = AfxRegisterWndClass(CS_VREDRAW|CS_HREDRAW|CS_DROPSHADOW,
			LoadCursor(NULL, IDC_ARROW), (HBRUSH)(COLOR_BTNFACE+1));

		// (ak) WS_POPUP alone instead of WS_POPUP|WS_DLGFRAME. The dialog frame drew a title
		// bar band around a palette that has no caption and no buttons in it. The window stays
		// what it already was otherwise: owned by the tree's list window, so it is destroyed
		// with the tree and always sits above it - ownership, not WS_CHILD, is what the passed
		// parent gives a WS_POPUP.
		const DWORD dwStyle = WS_POPUP;

		CRect r(0, 0, PopWidth(), PopWidth());
		CreateEx(0, pszClassName, _T(""), dwStyle, r, m_pProp->GetCtrlParent(), 0);
	}

	BuildToolTips();

	PositionPopup(TRUE);

	m_nHot      = -1;
	m_bMoreDown = FALSE;
	m_bTracking = FALSE;

	// (ak) Open on the cell that holds the value, so the user sees at once what the attribute
	// is set to, and Enter with no arrow keys at all re-picks that same colour.
	m_nCurrent = -1;
	SetCurrent(CellForColor(m_cColor));

	// PositionPopup() shows with SWP_NOACTIVATE so that the property sheet keeps the
	// foreground and its title bar does not dim while the palette is open. The popup still
	// needs the keyboard, and it is in the sheet's own thread, so an explicit SetFocus() is
	// what hands it the keys.
	SetFocus();

	if (GetFocus() != this)
	{
		// That did not take - a foreground lock, or the window not yet really shown. Activate
		// it rather than leave a palette that looks open and eats no keystroke at all; a dimmed
		// title bar is the lesser of the two.
		SetWindowPos(&wndTop, 0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);
		SetFocus();
	}

	// (ak) Taken last, once the window is really up, really placed, and really has the focus.
	// See InstallPopupHook() for why a popup that is shown without being activated needs it.
	InstallPopupHook();
}


void CPropTreeItemColor::OnCancel()
{
	// (ak) Cancel means "throw the popup away", so the item goes back to the colour it had
	// when the popup was opened. The shipped control hid the window and left m_cColor at
	// whatever the pointer had last hovered over, which is what made Escape look like it had
	// done nothing - and worse, its OnKillFocus() committed on the bare !m_bInDialog test, and
	// Cancel() left m_bCommitOnce clear, so the hide below went straight on and committed that
	// colour into the attribute. Both halves are fixed: the value is restored here, and
	// OnKillFocus() now also tests IsActivated(), which Cancel() has already cleared.
	//
	// Restoring it here rather than in every caller covers each path that gets here: Escape,
	// the tree dropping the item, the page tearing the tree down.
	m_cColor = m_cPrevColor;

	m_nCurrent  = -1;
	m_nHot      = -1;
	m_bMoreDown = FALSE;


	// (ak) Hook, open-popup record and hide, for the reason given in OnCommit().
	HidePopup();
}


void CPropTreeItemColor::OnKillFocus(CWnd* pNewWnd)
{
	CWnd::OnKillFocus(pNewWnd);

	// (ak) Only commit while the item is still activated. After Cancel() the flag is gone, and
	// committing here would push the colour the user just threw away into the attribute - the
	// same guard CPropTreeItemEdit::OnKillfocus() has. m_bInDialog covers ChooseColor(),
	// which hides the popup and takes the focus for the length of its modal loop.
	if (m_bInDialog)
		return;

	if (IsActivated())
		CommitChanges();

	// (ak) And hide, whatever the commit did. CommitChanges() returns early - without ever
	// reaching OnCommit(), the thing that hides - once the base class m_bCommitOnce latch is
	// set, and a popup left on screen after losing the focus is one the user cannot get rid of.
	// The shipped control hid here too, by committing unconditionally; this keeps the hide and
	// drops the commit of a value that was just thrown away. See HidePopup().
	HidePopup();
}


void CPropTreeItemColor::OnClose()
{
	// (ak) The popup window being closed - Alt+F4 while it holds the focus - is the same
	// gesture as clicking away from it. Committing here does not change the attribute: the
	// pointer and the arrow keys only ever move m_nCurrent, so m_cColor is still the value
	// OnActivate() saved into m_cPrevColor and the commit writes back what was already there.
	if (IsActivated())
		CommitChanges();

	// (ak) Unconditional. CommitChanges() can early-return on the base class latch without ever
	// reaching OnCommit(), and this window is going away whichever way the state reads.
	HidePopup();
}


// (ak) Takes the popup off the screen whatever the item's activation state says, and drops the
// hook that goes with it. Idempotent: safe to call twice, and safe on a popup never shown.
//
// This exists because both routes that are supposed to hide the window have a hole in them.
// OnCommit() is reached only through CPropTreeItem::CommitChanges(), and CommitChanges() returns
// early - never calling OnCommit() - once its m_bCommitOnce latch is set; only the next
// Activate() clears that latch. OnCancel() is reached only through Cancel(), and every caller
// guards Cancel() on IsActivated(), which a commit has already cleared. A popup that falls into
// either gap stays on screen with nothing left that can close it, because the tree's own
// EnumDeactivateItems() and EnumDeactivateAll() test IsActivated() too, and so walk straight past
// it. That is how two palettes end up floating over the page at once.
void CPropTreeItemColor::HidePopup()
{
	RemovePopupHook();

	if (s_pOpenPopup == this)
		s_pOpenPopup = NULL;

	// The visibility test is also the re-entrancy guard: ShowWindow(SW_HIDE) on a focused window
	// brings WM_KILLFOCUS back into OnKillFocus(), which calls this again, and by then the window
	// is already invisible.
	if (m_hWnd != NULL && ::IsWindowVisible(m_hWnd))
	{
		ShowWindow(SW_HIDE);

		// (ak) Repaint the attribute cell now, rather than whenever the tree next happens to.
		//
		// Hiding the popup does not invalidate what was underneath it. The popup is a top level
		// window owned by the tree's PARENT, not a sibling of the list, so nothing tells the list
		// that the pixels it is showing are stale - and the list double buffers through
		// m_BackBuffer, so it has no reason to redraw on its own either. The cell kept the colour
		// it had when the popup opened until some unrelated mouse movement repainted the row,
		// which is exactly what "updates only after I move to the next item" was.
		//
		// CommitChanges() does repaint through RefreshItems(), but only on the path that reaches
		// OnCommit(): it returns early once its m_bCommitOnce latch is set, and the Escape,
		// click-outside and kill-focus routes never go through it at all. HidePopup() is the one
		// funnel every dismissal passes, so the repaint belongs here.
		//
		// UpdatedItems() only marks the control and the list dirty; the paint itself happens
		// later, off the message loop, so there is nothing here to re-enter. Safe during
		// OnNcDestroy() too - by the time that deferred paint runs the tree enumerates whatever
		// items it still has.
		if (m_pProp != NULL)
			m_pProp->UpdatedItems();
	}
}


// (ak) Nothing was chosen, so the colour the popup opened with is the colour that stays.
// Cancel() while the item is still activated - it restores m_cPrevColor and clears the popup's
// state - and hide unconditionally afterwards, because Cancel() is not reached at all once the
// activation flag has gone.
void CPropTreeItemColor::ForceClose()
{
	if (IsActivated())
	{
		Cancel();
	}
	else
	{
		m_cColor    = m_cPrevColor;
		m_nCurrent  = -1;
		m_nHot      = -1;
		m_bMoreDown = FALSE;
	}

	HidePopup();
}


// (ak) Under the attribute cell when there is room, above it when there is not, and never
// off the work area of the monitor the cell is on. The shipped control put the top left of
// the popup at the top left of the cell and stopped there - no work area test at all - so on
// a colour row near the bottom of the screen the lower half of the palette hung off the
// desktop, and near the right edge its right hand columns did too.
void CPropTreeItemColor::PositionPopup(BOOL bShow)
{
	if (!IsWindow(m_hWnd))
		return;

	ASSERT(m_pProp != NULL);
	CWnd* pOwner = m_pProp->GetCtrlParent();

	// m_rc is the attribute cell, in the owner's client coordinates.
	CPoint ptTopLeft(m_rc.left, m_rc.top);
	CPoint ptBottomRight(m_rc.right, m_rc.bottom);

	pOwner->ClientToScreen(&ptTopLeft);
	pOwner->ClientToScreen(&ptBottomRight);

	MONITORINFO mi;
	mi.cbSize = sizeof(mi);
	::GetMonitorInfo(::MonitorFromPoint(ptTopLeft, MONITOR_DEFAULTTONEAREST), &mi);

	const CRect rcWork(mi.rcWork);

	int x = ptTopLeft.x;

	if (x + m_sizePopup.cx > rcWork.right)
		x = rcWork.right - m_sizePopup.cx;

	if (x < rcWork.left)
		x = rcWork.left;

	int y = ptBottomRight.y + 1;

	if (y + m_sizePopup.cy > rcWork.bottom)
	{
		const int nAbove = ptTopLeft.y - m_sizePopup.cy - 1;

		y = (nAbove >= rcWork.top) ? nAbove : (rcWork.bottom - m_sizePopup.cy);
	}

	if (y < rcWork.top)
		y = rcWork.top;

	// SWP_NOACTIVATE: taking the activation away from the page would drop the property sheet
	// behind it, and the popup gets the keyboard focus from OnActivate() either way.
	SetWindowPos(NULL, x, y, m_sizePopup.cx, m_sizePopup.cy,
		SWP_NOZORDER|SWP_NOACTIVATE|(bShow ? SWP_SHOWWINDOW : 0));
}


/////////////////////////////////////////////////////////////////////////////
// CPropTreeItemColor - popup mouse handling

// (ak) The popup is a top level window that holds the keyboard focus but NOT the activation:
// PositionPopup() shows it with SWP_NOACTIVATE so the property sheet keeps the foreground and
// its title bar stays lit. That combination is exactly why a click outside it used to leave
// it open. Windows sends WM_KILLFOCUS only when the focus actually moves, and a click on
// something that cannot take the focus - a static label, the sheet's own background, dead
// space between the tree's rows - moves nothing at all, so the popup never heard about it.
// Only the two cases that do move the focus worked: a click on a real control, and a click
// into the tree, which CPropTreeList::OnLButtonDown() answers by deactivating the item.
//
// A WH_GETMESSAGE thread hook is what notices both. It sees every message the thread takes out
// of its queue, whichever window that message is addressed to, so a click on the sheet's dead
// space and an Escape on its way to the sheet are as visible as one addressed to the popup.
//
// WH_GETMESSAGE, and not the WH_MOUSE the name would suggest. WH_MOUSE installed without
// complaint and was then never called - not for a real click, not for a posted one - while
// WH_GETMESSAGE saw both, every time. That was measured, not assumed: a stand-in harness
// reproducing this exact arrangement (CPropertySheet -> CPropertyPage -> tree child -> owned
// WS_POPUP holding the focus but not the activation) installs WH_MOUSE, WH_GETMESSAGE and
// WH_CALLWNDPROC together and logs every call. Only WH_GETMESSAGE fires.
//
// SetCapture() was tried before that, and is not used either. It looked like the obvious
// answer - it is what menus and combo box drop downs do - but capture is taken from inside
// CPropTreeList::OnLButtonDown(), while the left button is physically down over the TREE, and
// the documented contract of SetCapture() covers a window the mouse is over, or a window the
// button went down over. At that moment the popup is neither.
//
// The hook only WATCHES, which is the property that makes it the right tool here: it can tell
// outside from inside, and choose per message whether to eat it or wave it through.
//
// The trade is the standard one for a popup, and worth stating plainly: while the palette is
// open, the first click outside dismisses it and does NOT also reach whatever is underneath.
// The user clicks a second time to act on that control. That is what a menu does, and getting
// both in one click would mean letting the click through to CPropTreeList::OnLButtonDown(),
// which re-enters the tree from inside the hook and reactivates the very item that was just
// dismissed - so clicking the same colour cell again would reopen its palette instead of
// closing it.
void CPropTreeItemColor::InstallPopupHook()
{
	if (s_hPopupHook != NULL)
	{
		if (s_pHookOwner == this)
			return;						// already watching for us

		// A hook left behind by a popup that went away by some other path. Take it over rather
		// than leave the new popup deaf.
		::UnhookWindowsHookEx(s_hPopupHook);
		s_hPopupHook = NULL;
		s_pHookOwner = NULL;
	}

	if (!IsWindow(m_hWnd))
		return;

	// hmod must be NULL: this is a hook for one thread of this process, and PopupHookProc()
	// lives in this module. An instance handle here is what a global hook needs, and a global
	// hook needs its procedure to be in a DLL.
	s_hPopupHook = ::SetWindowsHookEx(WH_GETMESSAGE, PopupHookProc, NULL, ::GetCurrentThreadId());

	if (s_hPopupHook != NULL)
		s_pHookOwner = this;
}


// (ak) Removing is not optional. Every path that hides the popup calls this first -
// OnCommit(), OnCancel(), ChooseMoreColors() and OnDestroy() - because a hook left installed
// would keep eating clicks and Escape keys on behalf of a window that is no longer there, and
// the app underneath would look frozen.
//
// The s_pHookOwner test is what makes this safe to call from anywhere: only the popup the hook
// is actually watching may take it down, so a second popup being constructed cannot unhook
// the first one.
void CPropTreeItemColor::RemovePopupHook()
{
	if (s_pHookOwner != this)
	{
		return;
	}


	if (s_hPopupHook != NULL)
	{
		::UnhookWindowsHookEx(s_hPopupHook);
		s_hPopupHook = NULL;
	}

	s_pHookOwner = NULL;
}


// (ak) The two things the popup has to hear about that are not addressed to it.
//
// A BUTTON DOWN on any other window of the thread. Button down and never button up: the click
// that opens the palette is still physically down when the hook goes in - InstallPopupHook()
// runs from inside the tree's own WM_LBUTTONDOWN handler - so its release arrives afterwards,
// and treating a release as a dismiss would close the palette the instant it appeared.
//
// An ESCAPE on its way to any other window. This is the reason the hook exists at all rather
// than being a nicety. The palette asks for the focus but not the activation, and if the focus
// is anywhere in the property sheet when Escape is pressed, the sheet's dialog manager turns
// that key into IDCANCEL and closes the whole Visual Attributes dialog: the user loses every
// setting on the page, and the palette goes with it. Eating the key here and dismissing the
// palette instead is what Escape means while a palette is open. The hook goes when the palette
// goes, so the next Escape closes the dialog exactly as it always did.
//
// wParam is TRUE only when GetMessage() really took the message out of the queue. MFC's
// RunModalLoop() peeks with PM_NOREMOVE before it pumps, and acting on the peek would dismiss
// the palette over a message that has not been consumed yet - and then unhook, so the real
// pass is never seen and the message goes straight through to whatever is underneath.
LRESULT CALLBACK CPropTreeItemColor::PopupHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	MSG* pMsg = (MSG*)lParam;

	if (nCode >= 0 && wParam != FALSE && pMsg != NULL && s_pHookOwner != NULL)
	{
		CPropTreeItemColor* pSelf = s_pHookOwner;

		// Anything addressed to the popup, or to the tooltip hanging off it, is the popup's own
		// business: OnLButtonDown(), OnRButtonDown() and OnKeyDown() already deal with those.
		// Testing the target window rather than the cursor position keeps the decision
		// independent of what MSG::pt happens to mean for a queued message.
		const BOOL bOurs = (pMsg->hwnd == pSelf->GetSafeHwnd()) ||
						   ::IsChild(pSelf->GetSafeHwnd(), pMsg->hwnd);

		BOOL bEat = FALSE;

		if (pMsg->message == WM_LBUTTONDOWN || pMsg->message == WM_RBUTTONDOWN ||
			pMsg->message == WM_MBUTTONDOWN || pMsg->message == WM_NCLBUTTONDOWN ||
			pMsg->message == WM_KEYDOWN || pMsg->message == WM_SYSKEYDOWN)
		{
		}

		if (!bOurs)
		{
			switch (pMsg->message)
			{
			case WM_LBUTTONDOWN:
			case WM_RBUTTONDOWN:
			case WM_MBUTTONDOWN:
			case WM_NCLBUTTONDOWN:
			case WM_NCRBUTTONDOWN:
			case WM_NCMBUTTONDOWN:
				bEat = TRUE;
				break;

			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
				bEat = (pMsg->wParam == VK_ESCAPE);
				break;
			}
		}

		if (bEat)
		{
			// Unhook BEFORE dismissing. DismissOutsideClick() hides the window and moves the
			// focus, and neither should run with the hook still able to re-enter here.
			pSelf->RemovePopupHook();
			pSelf->DismissOutsideClick();

			// Blanking the message and returning nonzero between them stop it reaching the
			// window it was addressed to: the sheet never sees the Escape, the tree never sees
			// the click. s_hPopupHook is NULL by now, so there is no chain left to pass it on
			// to either.
			pMsg->message = WM_NULL;

			return 1;
		}
	}

	return ::CallNextHookEx(s_hPopupHook, nCode, wParam, lParam);
}


// (ak) Input that arrived from somewhere the popup is not - a click outside it, or an Escape on its
// way to the property sheet. Nothing was chosen, so this is the Escape path rather than the
// commit one, and the two really are the same thing here: hovering and the arrow keys only ever
// move m_nCurrent and never touch m_cColor. The attribute keeps the colour it had before the
// popup was opened.
//
// The focus has to be handed back explicitly. PopupHookProc() stopped the message before it
// reached the window under the pointer, so Windows will not move the focus for us, and hiding
// a focused window leaves the thread with nowhere sensible to put it.
void CPropTreeItemColor::DismissOutsideClick()
{
	// Idempotent, and already done when we arrive from PopupHookProc(). The other caller is
	// OnRButtonDown(), where the hook is still up.
	RemovePopupHook();

	// (ak) ForceClose(), not a Cancel() guarded on IsActivated(). That guard was itself a bug:
	// CommitChanges() clears TreeItemActivated before it does anything else, so a popup whose
	// item had already been committed read as "not activated", the cancel was skipped, and the
	// window stayed on screen with the hook that was supposed to dismiss it already taken down.
	// Nothing was chosen on this path, so throwing the value away is right in either state.
	ForceClose();

	ASSERT(m_pProp != NULL);

	if (m_pProp != NULL)
	{
		CWnd* pOwner = m_pProp->GetCtrlParent();

		if (pOwner != NULL && ::IsWindow(pOwner->GetSafeHwnd()))
			pOwner->SetFocus();
	}
}


// (ak) The popup window is destroyed along with its owner, the tree's list window. This is the
// backstop for a teardown that never went through OnCommit() or OnCancel().
void CPropTreeItemColor::OnDestroy()
{
	RemovePopupHook();

	CWnd::OnDestroy();
}


// (ak) A right click dismisses the popup wherever it lands, inside or out, which is what a
// menu and a combo box drop down both do: there is nothing on a palette for the right button
// to act on, and leaving it up while the user reaches for a context menu is worse.
void CPropTreeItemColor::OnRButtonDown(UINT /*nFlags*/, CPoint /*point*/)
{
	DismissOutsideClick();
}


void CPropTreeItemColor::OnMouseMove(UINT /*nFlags*/, CPoint point)
{
	const int nCell = CellFromPoint(point);

	if (nCell != m_nHot)
	{
		const int nOld = m_nHot;

		m_nHot = nCell;

		if (nOld >= 0 && nOld < m_nCellCount)
			InvalidateRect(&m_cells[nOld].rcCell, FALSE);

		if (nCell >= 0)
			InvalidateRect(&m_cells[nCell].rcCell, FALSE);

		// (ak) The hover moves the keyboard cell too. Enter then commits the swatch the
		// pointer is on and the user sees marked, rather than a cell the pointer crossed on
		// its way there - the mismatch that made the shipped OnLButtonDown() pick the wrong
		// colour. DrawCell() leaves the dotted focus rect off while the pointer is over the
		// cell, so following the mouse does not look like a keyboard focus.
		SetCurrent(nCell);
	}

	// (ak) Ask for the WM_MOUSELEAVE that clears the hover again. The shipped control had
	// none, so a pointer that left the popup through an edge left the last swatch it crossed
	// marked until the next move inside.
	if (!m_bTracking && IsWindow(m_hWnd))
	{
		TRACKMOUSEEVENT tme;

		tme.cbSize    = sizeof(tme);
		tme.dwFlags   = TME_LEAVE;
		tme.hwndTrack = m_hWnd;
		tme.dwHoverTime = 0;

		if (_TrackMouseEvent(&tme))
			m_bTracking = TRUE;
	}
}


void CPropTreeItemColor::OnMouseLeave()
{
	m_bTracking = FALSE;

	if (m_nHot >= 0 && m_nHot < m_nCellCount)
		InvalidateRect(&m_cells[m_nHot].rcCell, FALSE);

	m_nHot = -1;
}


BOOL CPropTreeItemColor::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	if (nHitTest == HTCLIENT)
	{
		CPoint point;
		GetCursorPos(&point);
		ScreenToClient(&point);

		if (CellFromPoint(point) != -1)
		{
			// (ak) IDC_PROPTREE_FPOINT is a NAMED cursor of PropTree.rc, and SQLTools does
			// not compile that .rc - so it is not in the module LoadCursor() searches and the
			// call returns NULL. SetCursor(NULL) HIDES the pointer, which is what the old
			// popup did over every swatch. The system hand is what a clickable cell wants
			// anyway, and a host that does compile PropTree.rc keeps the original finger.
			HCURSOR hCursor = AfxGetApp()->LoadCursor(_T("IDC_PROPTREE_FPOINT"));

			SetCursor(hCursor ? hCursor : ::LoadCursor(NULL, IDC_HAND));
			return TRUE;
		}


	}

	return CWnd::OnSetCursor(pWnd, nHitTest, message);
}


void CPropTreeItemColor::OnLButtonDown(UINT /*nFlags*/, CPoint point)
{
	// (ak) A click that reaches us with a point outside the client area. With the hook up this
	// should not happen - PopupHookProc() eats the click before it is dispatched, wherever it
	// landed - but the test stays. It costs one PtInRect, and it is the backstop for the paths
	// where the hook is down while the window is still up: the hook could not be installed at
	// all, or a message was posted to us before the hook went in.
	CRect rcClient;

	GetClientRect(&rcClient);


	if (!rcClient.PtInRect(point))
	{
		DismissOutsideClick();
		return;
	}

	// (ak) Hit test the click, not the cell the pointer last hovered. The shipped control
	// committed m_nSpot, which OnMouseMove() had set from an earlier position, so a click in
	// the padding between two swatches picked whichever one the pointer had crossed on the
	// way there - and a click outside every swatch picked the last one at all.
	const int nCell = CellFromPoint(point);

	if (nCell < 0)
		return;

	SetFocus();

	m_nHot = nCell;
	SetCurrent(nCell);

	if (m_cells[nCell].kind == CELL_MORE)
	{
		// The row stays pressed until the button comes up, so the click is seen to land
		// before the modal dialog takes over.
		m_bMoreDown = TRUE;
		InvalidateRect(&m_cells[nCell].rcCell, FALSE);
		UpdateWindow();
		return;
	}

	CommitCell(nCell);
}


void CPropTreeItemColor::OnLButtonUp(UINT /*nFlags*/, CPoint point)
{
	if (!m_bMoreDown)
		return;

	m_bMoreDown = FALSE;

	const int nCell = CellFromPoint(point);

	if (nCell >= 0 && m_cells[nCell].kind == CELL_MORE)
		InvalidateRect(&m_cells[nCell].rcCell, FALSE);

	// Released off the row: the click is dropped and the popup stays open, the way a button
	// behaves.
	if (nCell < 0 || m_cells[nCell].kind != CELL_MORE)
		return;

	CommitCell(nCell);
}


/////////////////////////////////////////////////////////////////////////////
// CPropTreeItemColor - popup keyboard handling

BOOL CPropTreeItemColor::PreTranslateMessage(MSG* pMsg)
{
	// (ak) The tooltip wants the popup's mouse messages relayed to it. MFC hands them here
	// because the popup, not the tree, is the window they are addressed to.
	if (IsWindow(m_toolTip.m_hWnd))
		m_toolTip.RelayEvent(pMsg);

	// (ak) Deliberately NOT returning TRUE for the popup's own messages, which looks like the
	// obvious way to keep an ancestor from translating them.
	//
	// CWinThread::PreTranslateMessage() -> CWnd::WalkPreTranslateTree() does walk the owner
	// chain - popup -> property sheet, because ::GetParent() of an owned WS_POPUP returns its
	// owner, and Windows redirects that ownership to the top level sheet rather than leaving it
	// on the tree list that was passed to CreateEx(). But AfxInternalPumpMessage() reads a TRUE
	// from here as "this message has been dealt with" and then skips TranslateMessage() AND
	// DispatchMessage(), so returning TRUE would leave the palette on screen and deaf to every
	// key and click it exists to answer.
	//
	// It is also not needed. IsChild() is FALSE for an owned popup, so the sheet's dialog
	// manager leaves the popup's own keys alone and they reach OnKeyDown() untouched - measured
	// in the harness, with a real CPropertySheet, both with and without an intercept here. What
	// does close the whole sheet is an Escape addressed to the SHEET, which happens when the
	// palette is not the window holding the focus, and PopupHookProc() is what catches that.
	return CWnd::PreTranslateMessage(pMsg);
}


UINT CPropTreeItemColor::OnGetDlgCode()
{
	// (ak) The shipped control answered with the CWnd default, DLGC_WANTTAB and nothing else,
	// so the dialog manager kept every arrow key for itself and the popup was unreachable from
	// the keyboard even if it had had a handler for them - which it did not.
	//
	// Take every key, unconditionally. The popup is a top level window the dialog manager has
	// no business navigating: Tab, the arrows, Enter, Space and Escape all have a case in
	// OnKeyDown(). An earlier version made an exception for VK_TAB so that IsDialogMessage()
	// would move the focus on; that is exactly the wrong answer here, because the popup is not
	// in the sheet's tab order - IsDialogMessage() would have moved the focus to some control
	// on the page and left the palette open and deaf. Tab commits and closes instead, which is
	// what it means on a property cell.
	//
	// PreTranslateMessage() returning TRUE is what keeps IsDialogMessage() away altogether.
	// This answer is the second line of the same defence, for any other path that asks.
	return CWnd::OnGetDlgCode() | DLGC_WANTARROWS | DLGC_WANTALLKEYS;
}


void CPropTreeItemColor::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	switch (nChar)
	{
	case VK_LEFT:
		MoveCurrent(-1, 0);
		return;

	case VK_RIGHT:
		MoveCurrent(1, 0);
		return;

	case VK_UP:
		MoveCurrent(0, -1);
		return;

	case VK_DOWN:
		MoveCurrent(0, 1);
		return;

	case VK_HOME:
		SetCurrent(0);
		return;

	case VK_END:
		SetCurrent(m_nCellCount - 1);
		return;

	case VK_PRIOR:
		MoveCurrentSection(-1);
		return;

	case VK_NEXT:
		MoveCurrentSection(1);
		return;

	case VK_RETURN:
	case VK_SPACE:
		// (ak) Enter and Space pick the cell the focus is on, the way they activate a button.
		// CommitCell() -> CommitChanges() hides the popup and hands the focus back to the
		// tree, so the arrows keep working on the rows below.
		CommitCell(m_nCurrent);
		return;

	case VK_ESCAPE:
		// (ak) Escape throws the popup away and the attribute keeps the colour it had before
		// the popup was opened. Cancel() clears the activated flag, so the WM_KILLFOCUS that
		// follows the SetFocus() below does not commit the colour being thrown away.
		//
		// This is the case where the palette is the window holding the focus. The case where it
		// is not - the key is on its way to the property sheet, which would turn it into
		// IDCANCEL and close the whole dialog - is caught earlier, in PopupHookProc().
		Cancel();

		if (m_pProp && ::IsWindow(m_pProp->GetCtrlParent()->GetSafeHwnd()))
			m_pProp->GetCtrlParent()->SetFocus();

		return;

	case VK_TAB:
		// (ak) Tab has to be handled here rather than left to the dialog manager. The popup is
		// a top level window owned by the tree, not a child of it, so IsDialogMessage() does
		// not own its keys and would never move the focus on - leaving Tab a dead key that
		// traps the user in the palette. Committing is what Tab means on a property cell
		// anyway: it is exactly what clicking another row does, through OnKillFocus().
		CommitCell(m_nCurrent);
		return;
	}

	CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}


void CPropTreeItemColor::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags)
{
	// (ak) WM_CHAR follows WM_KEYDOWN for the keys OnKeyDown() already acted on. Swallow them
	// here so nothing beeps and nothing reaches a window that has already been hidden - the
	// reason CPropTreeItemEdit::OnChar() exists.
	if (nChar == VK_RETURN || nChar == VK_ESCAPE || nChar == VK_TAB || nChar == _T(' '))
		return;

	CWnd::OnChar(nChar, nRepCnt, nFlags);
}
