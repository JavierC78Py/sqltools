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

// 2026-09-22 replaces CVisualAttributesPage for the SQLTools window fonts, see the header

#include "stdafx.h"
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "OpenEditor/VisualAttributesPageTree.h"
#include "Common/MyUtf.h"
#include "Common/PropTreePageHelpers.h"
#include "Common/PropTreePage.rh"     // IDC_PTP_TREE, the id of the page's tree

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

	using namespace Common::PropTreeRows;
	using Common::VisualAttribute;
	using Common::VisualAttributesSet;

// Group expansion state, kept across dialog re-openings: written by SaveTreeState(),
// read by BuildTree(). The number of groups is data driven - one per
// VisualAttributesSet the host hands the page - so this is a vector and not one
// static BOOL per group like the pages with a fixed group layout have.
//
// Keyed by the page title, because the four sheets hand the page two DIFFERENT
// collections: the three SQLTools window sheets five sets, and the editor classes sheet
// of COEDocument::ShowSettingsDialog() one set per editor class. One shared vector would
// let whichever sheet closed last dictate which group the next one opens - the accordion
// leaves exactly one group expanded, at the index of the set that was being edited.
static std::map<CString, std::vector<BOOL>> s_groupExpanded;

// "One open group at a time": opening a set group closes the others, see
// GetOneGroupOpenAtATime() and Common::CPropTreePage::CloseOtherGroups(). Five sets of six
// rows each are taller than the tree pane of the
// sheet, so the accordion keeps the set being edited the only one on screen - and it
// is what makes a sample row per attribute affordable. false gives the plain CPropTree
// behaviour, every group open on its own.
static const bool s_oneGroupOpenAtATime = true;

// The classic "Size" combo box listed the sizes enumFontSizesFunc() offered for the
// selected font: 8..24, every second value from 12 on, for a TrueType font, and the
// point sizes of a raster font. A tree cell has a spin instead, so these limits are
// only a guard against a nonsense value - they are wide enough to keep any size a
// settings file can hold, and CPropTreeItemEdit clamps typed text to them.
static const int s_fontSizeMin  = 1;
static const int s_fontSizeMax  = 255;
static const int s_fontSizeStep = 1;

// The palette popup the colour rows open, as a percentage of the size the vendored
// CPropTreeItemColor ships at. One knob for the whole page: it is handed to
// SetDefaultPopupScale(), a static the item reads in its constructor, so it has to be set
// before the first colour row is built - BuildColourPresets() does that.
//
// The library clamps it into [CLRPOP_SCALE_MIN, CLRPOP_SCALE_MAX], 100% to 200%, so no value
// here can ask for a popup larger than double the shipped one - and double still fits a work
// area, which is the real ceiling: PositionPopup() clamps to the monitor, so past that a
// "bigger" palette would just be a cropped one. 125% is rung 1 of the 4 the step API offers
// (100 / 125 / 150 / 175 / 200). It scales the swatches, the section labels and the text
// together, so the popup reads as a larger picker rather than as a sparse one - and a
// quarter again is enough to do that without the palette outgrowing the page it opens from.
static const int s_colourPopupScalePct = 125;

///////////////////////////////////////////////////////////////////////////////
// The font list.
//
// The classic ShowFont() filled its "Font" combo box again on every selection
// change: ::EnumFonts() with enumFontsFunc(), or with enumFixedFontsFunc() - the
// same callback plus a (3 & lfPitchAndFamily) == FIXED_PITCH test - while the mask
// of the selected attribute held vaoFontFixedPitch. Both callbacks dropped a face
// name the combo box already had (FindStringExact), because EnumFonts reports a
// face once per charset and style.
//
// Here the enumeration runs once per page into the two lists below, and the mask of
// an attribute only picks the list its rows are filled from.

namespace {

	struct FontEnumData
	{
		std::vector<CString>*   m_all;
		std::vector<CString>*   m_fixedPitch;
		std::set<std::wstring>* m_seen;    // the face names collected so far, lower case
	};

	BOOL APIENTRY enumFontsProc (
			LPLOGFONT lpLogFont, LPTEXTMETRIC /*lpTextMetric*/, INT /*nFontType*/, LPVOID lpData
		)
	{
		FontEnumData* pData = (FontEnumData*)lpData;

		std::wstring name = lpLogFont->lfFaceName;

		// One entry per face name, what the FindStringExact() test of the classic
		// callbacks did - a combo box compares its strings case insensitively.
		std::wstring key = name;

		if (!key.empty())
			::CharLowerBuff(&key[0], (DWORD)key.size());

		if (!pData->m_seen->insert(key).second)
			return TRUE;

		pData->m_all->push_back(CString(name.c_str()));

		if ((3 & lpLogFont->lfPitchAndFamily) == FIXED_PITCH)
			pData->m_fixedPitch->push_back(CString(name.c_str()));

		return TRUE;
	}

	// The classic combo box was CBS_SORT, the tree cell is not, so the list is sorted
	// here - case insensitively, the way a combo box compares its strings.
	bool fontNameLess (const CString& a, const CString& b)
	{
		return _tcsicmp((LPCTSTR)a, (LPCTSTR)b) < 0;
	}

	// The info text of a set group. The groups are data driven, so the text is
	// generic; the names SQLToolsSettings' constructor creates get a line telling
	// which window reads the set (SetVisualAttributeSetName() of the view, every one
	// of which ends up in CGridViewPaintAccessories::OnSettingsChanged()).
	CString setGroupInfo (const char* name)
	{
		static const struct { const char* m_name; LPCTSTR m_window; } s_sets[] =
		{
			{ "Data Grid Window",  _T("the data grid a query result is shown in") },
			{ "Statistics Window", _T("the statistics window of a worksheet") },
			{ "Output Window",     _T("the server output window of a worksheet") },
			{ "History Window",    _T("the history window of a worksheet; the explain plan window reads the same set") },
			{ "Bind Window",       _T("the grid of the bind variables of a worksheet") },
		};

		LPCTSTR window = 0;

		for (int i = 0; i < _countof(s_sets) && !window; i++)
			if (strcmp(name, s_sets[i].m_name) == 0)
				window = s_sets[i].m_window;

		CString info;

		if (window)
			info.Format(_T("The font of %s."), window);
		else
			info = _T("The font the text of this window is drawn with.");

		return info;
	}

}//namespace

////////////////////////////////////////////////////////////////////////////////
// The item classes of this page.
//
// Both are local to it: CPropTree has neither a read only row that draws a font
// sample nor a combo box that commits while the pick from its list is still the
// reason of the change, and neither class is general enough to move into Common -
// see md\proptree_settings_page_conversion.md, "A row of its own".
//
// They are defined at file scope and not inside the namespace above because the page
// header forward declares CPropTreeItemFontSample - AttributeRows holds a pointer to
// it - and a forward declaration cannot name a member of an unnamed namespace.
////////////////////////////////////////////////////////////////////////////////

// The text the sample row draws: the caption of the classic page's sample static
// (IDC_PFC_SAMPLE, "Text Sample"), which ShowFont() drew in the font of the selected
// attribute.
static const TCHAR s_sampleText[] = _T("Text Sample");

// The margin above and below that text inside the 20 px high cell.
static const int s_sampleMargin = 1;

// The label of the sample row while the font fits the cell and is drawn at its true
// size. UpdateLabel() appends the percentage of that size the sample is drawn at when
// the font has to be scaled down to fit the cell.
static const TCHAR s_sampleLabel[] = _T("Sample");

// The factor, in per mille, the sample is drawn at: 1000 - its true size - while the
// font fits the cell it is drawn into, less when it has to be scaled down to fit it.
// fontHeight is the height of the font and cellHeight the height of the cell, both in
// the device pixels MM_TEXT measures in, so the two are comparable as they are.
static int SampleScalePerMille (int fontHeight, int cellHeight)
{
	if (fontHeight <= 0 || cellHeight <= 0 || fontHeight <= cellHeight)
		return 1000;

	const int nScale = MulDiv(1000, cellHeight, fontHeight);

	return nScale < 1 ? 1 : nScale;
}

///////////////////////////////////////////////////////////////////////////////
// The read only "Sample" row.
//
// Why it is a regular height row: CPropTreeItem::DrawItem() pins the attribute cell
// of EVERY item to the item height minus its last pixel - 20 px at the default height
// PROPTREEITEM_DEFHEIGHT - so a taller row would get a short cell under a tall label
// and tall hit rectangles. A tall row therefore needs a change in the third party
// control; scaling the sample into the regular cell does not, and it is what the fixed
// height sample static of the classic page did too.
//
// How the scaling works: the font comes from VisualAttribute::NewFont(), the very
// factory VisualAttribute::CreateFont() - and therefore the data grid, the output
// window and the statistics window - use, so the shape is the real one and not an
// approximation of it. Its height is measured in device pixels, the unit the row
// height is in, and only when it does not fit the cell is the DC switched to
// MM_ANISOTROPIC with the fit factor as its viewport/window extent ratio. LOGFONT
// heights are logical units, so the SAME font object then renders at
// tmHeight * factor, that is at the cell height: the proportions stay exact, no
// second font is created, and a font that does fit is drawn at its true size.
//
// The label states that factor: "Sample" while the font fits and the sample is drawn at
// its true size, "Sample (90%)" when it is scaled down to fit the cell. It is set by
// SetSampleAttribute(), which measures the font itself, and NOT by DrawAttribute(),
// where the factor is used: DrawItem() draws the label before it calls DrawAttribute(),
// so a label set while drawing would show one paint too late - and after that paint
// nothing invalidates the row again.
class CPropTreeItemFontSample : public CPropTreeItemStatic
{
public:
	CPropTreeItemFontSample () : m_pFont(0), m_bLabelIsSample(true) {}

	virtual ~CPropTreeItemFontSample () { delete m_pFont; }

	// attr is the effective attribute to preview: the mix of the edited attribute and
	// the base attribute of its set, what the other rows show too. A COPY is kept and
	// the font is created from it, so the row does not depend on the lifetime of attr -
	// the page hands it a temporary.
	void SetSampleAttribute (const VisualAttribute& attr)
	{
		m_attr = attr;

		delete m_pFont;

		// 0 when the face cannot be created at all. The row then stays empty and the
		// "Font" row above shows the name that could not be used.
		m_pFont = m_attr.NewFont();

		UpdateLabel();
	}

	// The row is a GROUP node and not a sample row: the level 2 attribute node of a set
	// holding several attributes, whose attribute cell - the right hand side of the node -
	// draws the sample, so that it is there while the node is closed too. Its label is the
	// name of the attribute, set with SetLabelText() before this is called, and
	// UpdateLabel() leaves it alone from then on: a node renamed to "Sample (90%)" would no
	// longer say which attribute it opens.
	void SetLabelIsAttributeName () { m_bLabelIsSample = false; }

	virtual void DrawAttribute (CDC* pDC, const RECT& rc);

private:
	// The height of the cell the sample is drawn into. CPropTreeItem::DrawItem() stores
	// the item rectangle GetHeight()-1 pixels tall and hands THAT rectangle to
	// DrawAttribute(), which takes the two margins off it - the height the drawing
	// measures the font against, so the label has to be measured against it too.
	int SampleCellHeight () { return (int)GetHeight() - 1 - 2 * s_sampleMargin; }

	// Sets the label of the row from the factor MeasureScale() reports.
	void UpdateLabel ();

	// The factor, in per mille, the sample is drawn at: SampleScalePerMille() of the
	// height of the current font against SampleCellHeight(). 1000 - no scaling - while
	// there is no font to measure at all.
	int MeasureScale ();

	// The tree owns its items - CPropTree::Delete() deletes them - and an item is
	// never copied, so both are private and left unimplemented.
	CPropTreeItemFontSample (const CPropTreeItemFontSample&);
	CPropTreeItemFontSample& operator= (const CPropTreeItemFontSample&);

	VisualAttribute m_attr;
	CFont*          m_pFont;    // created from m_attr, owned

	// False for a group node, whose label is the name of the attribute it opens and not
	// "Sample"; see SetLabelIsAttributeName().
	bool            m_bLabelIsSample;
};

// Called by CPropTreeItem::DrawItem() with the attribute cell of the row - the item
// rectangle minus its last pixel - and with a DC that has the clipping region of the
// item selected. The label, the selection colour and the expand box are the base class'
// business; only the cell is drawn here.
void CPropTreeItemFontSample::DrawAttribute (CDC* pDC, const RECT& rc)
{
	// No font - a face that cannot be created - leaves the cell empty.
	if (!pDC || !m_pFont)
		return;

	// The colours of the attribute, which is what the classic page's OnCtlColor() did for its
	// sample static: SetTextColor(attr.m_Foreground), SetBkColor(attr.m_Background) and a
	// brush of the background. A property whose mask bit is not set is inherited, and the
	// attribute this row was given is the mix Construct() resolved, so the window colours are
	// used only while the attribute carries no colour of its own - the five SQLTools window
	// sets, whose masks hold font bits only.
	//
	// (COLORREF)-1 is the "not set" marker Clear() writes; taken as a colour it would draw the
	// sample in 0xFFFFFF, white on white.
	const bool bOwnForeground = (m_attr.m_Mask & Common::vaoForeground) != 0 &&
		m_attr.m_Foreground != (unsigned)-1;
	const bool bOwnBackground = (m_attr.m_Mask & Common::vaoBackground) != 0 &&
		m_attr.m_Background != (unsigned)-1;

	// The window text colour even though the row is read only: this is an informational
	// row and not a disabled control, and a gray sample would hide the very thing the
	// row shows. Its LABEL is gray, which is what DrawItem() does for every read only row.
	const int oldTextColor = pDC->SetTextColor(bOwnForeground
		? (COLORREF)m_attr.m_Foreground : (COLORREF)::GetSysColor(COLOR_WINDOWTEXT));
	const int oldBkMode    = pDC->SetBkMode(TRANSPARENT);
	CFont*    pOldFont     = pDC->SelectObject(m_pFont);

	// Measured under MM_TEXT, the mapping mode the tree lays its rows out in, so
	// tmHeight is in the device pixels the cell height is in.
	TEXTMETRIC tm;
	pDC->GetTextMetrics(&tm);

	const int cellWidth  = rc.right - rc.left;
	const int cellHeight = rc.bottom - rc.top - 2 * s_sampleMargin;

	if (cellWidth > 0 && cellHeight > 0 && tm.tmHeight > 0)
	{
		// The band the text sits in, filled with the background colour of the attribute. Only
		// while the attribute really has one: painting an inherited COLOR_WINDOW over the cell
		// would cover the selection colour CPropTreeList::DrawItem() has just drawn there. The
		// fill runs under MM_TEXT, before the scaled branch below switches the mapping mode.
		if (bOwnBackground)
			pDC->FillSolidRect(rc.left, rc.top + s_sampleMargin, cellWidth, cellHeight,
				(COLORREF)m_attr.m_Background);

		// The factor the sample is drawn at - and the one the label of the row states.
		const int nScale = SampleScalePerMille(tm.tmHeight, cellHeight);

		if (nScale < 1000)
		{
			// Too tall for the cell: scale it down to fit. A window extent of 1000
			// logical units against a viewport extent of nScale device pixels is the
			// factor nScale/1000, and the two origins are aligned so that logical (0,0)
			// is the top left corner of the cell.
			const int savedDC = pDC->SaveDC();

			pDC->SetMapMode(MM_ANISOTROPIC);
			pDC->SetWindowOrg(0, 0);
			pDC->SetViewportOrg(rc.left, rc.top + s_sampleMargin);
			pDC->SetWindowExt(1000, 1000);
			pDC->SetViewportExt(nScale, nScale);

			// The font has to be selected again under the new mapping for GDI to realize
			// it at the scaled height. Going back to the tree font first makes the second
			// selection a real change of the DC font, and not one GDI may treat as a
			// no-op because the same object is selected again.
			pDC->SelectObject(pOldFont);
			pDC->SelectObject(m_pFont);

			// The cell rectangle, in the scaled logical units DrawText() now wants.
			CRect rcSample(0, 0, MulDiv(cellWidth, 1000, nScale),
				MulDiv(cellHeight, 1000, nScale));

			pDC->DrawText(s_sampleText, -1, rcSample,
				DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);

			// Undoes the mapping mode, both origins, both extents - and with them the
			// font selection made under them.
			pDC->RestoreDC(savedDC);
		}
		else
		{
			// It fits: drawn at its true size, so a small font shows exactly as small as
			// it is. Only the point size is then not visible as such - the "Size, points"
			// row above states it.
			CRect rcSample(rc.left, rc.top + s_sampleMargin, rc.right,
				rc.bottom - s_sampleMargin);

			pDC->DrawText(s_sampleText, -1, rcSample,
				DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
		}
	}

	pDC->SelectObject(pOldFont);
	pDC->SetTextColor(oldTextColor);
	pDC->SetBkMode(oldBkMode);
}

// Sets the label of the row from the factor MeasureScale() reports: "Sample" while the
// font fits the cell and the sample is drawn at its true size, "Sample (90%)" when it
// has to be scaled down to fit it. A percentage and not a factor ("x0.9") keeps the
// decimal separator, which the locale chooses, out of the label.
//
// A group node is skipped: it carries the name of the attribute it opens, and the scaling
// its sample gets is visible in the sample itself.
void CPropTreeItemFontSample::UpdateLabel ()
{
	if (!m_bLabelIsSample)
		return;

	const int nScale = MeasureScale();

	if (nScale >= 1000)
	{
		SetLabelText(s_sampleLabel);
		return;
	}

	CString sLabel;

	// Truncated and not rounded: a rounded 99.5% would read as 100%, which is what the
	// unscaled label above already says.
	sLabel.Format(_T("%s (%d%%)"), s_sampleLabel, MulDiv(nScale, 100, 1000));

	SetLabelText(sLabel);
}

// The factor, in per mille, the sample is drawn at. The font is measured the way
// DrawAttribute() measures it - under MM_TEXT, the mapping mode the tree lays its rows
// out in, so tmHeight is in the device pixels the cell height is in. A memory DC
// compatible with the screen has those pixels: SQLTools is system DPI aware (the
// dpiaware.manifest the linker embeds), so the process has ONE DPI and every DC in it
// measures a font the same, on any monitor. It is used instead of a DC of the tree
// because SetSampleAttribute() runs before the row is inserted, so there is no tree
// window to take one from yet.
int CPropTreeItemFontSample::MeasureScale ()
{
	if (!m_pFont)
		return 1000;

	CDC dc;

	if (!dc.CreateCompatibleDC(NULL))
		return 1000;

	CFont*    pOldFont  = dc.SelectObject(m_pFont);
	TEXTMETRIC tm;
	const BOOL bMetrics = dc.GetTextMetrics(&tm);

	dc.SelectObject(pOldFont);

	if (!bMetrics)
		return 1000;

	return SampleScalePerMille(tm.tmHeight, SampleCellHeight());
}

///////////////////////////////////////////////////////////////////////////////
// The "Font" row.
//
// CPropTreeItemCombo commits its cell when the in-place combo box loses the focus
// (CBN_KILLFOCUS) or when Enter is pressed, and a commit is what raises
// PTN_ITEMCHANGED. Picking a face from the drop down list does neither, so without this
// class the sample row would keep the previous face until the user clicked somewhere
// else. The classic page updated its sample on CBN_SELCHANGE
// (CVisualAttributesPage::OnFontChanged()); this restores that.
//
// The commit is POSTED and not done in the notification handler, because
// CommitChanges() deactivates the cell, that is, hides the very combo box that is
// sending the notification while its drop down list is closing.
class CPropTreeItemFontName : public CPropTreeItemCombo
{
public:
	CPropTreeItemFontName ()
		: m_pFonts(0)
	{
	}

	// (ak) The face names the drop down offers. Held as a pointer to one of the page's two
	// vectors and filled into the combo box on the first activation of the cell rather than
	// where the row is built - see OnActivate() below. The page owns both vectors and
	// outlives every row it creates, so the pointer stays good for the life of the item.
	void SetFontList (const std::vector<CString>* pFonts) { m_pFonts = pFonts; }

	// Fills the drop down the first time the cell is activated, then does what the combo box
	// item does: puts the stored text into the edit control, shows it and focuses it.
	virtual void OnActivate ();

protected:
	afx_msg void    OnSelChange ();
	afx_msg LRESULT OnCommitPosted (WPARAM, LPARAM);

	DECLARE_MESSAGE_MAP ()

private:
	const std::vector<CString>* m_pFonts;   // not owned
};

// Any value in the WM_APP range: the in-place combo box is created and owned by
// CPropTreeItemCombo and has no window messages of its own.
static const UINT WM_VAP_COMMIT = WM_APP + 41;

BEGIN_MESSAGE_MAP(CPropTreeItemFontName, CPropTreeItemCombo)
	ON_CONTROL_REFLECT(CBN_SELCHANGE, OnSelChange)
	ON_MESSAGE(WM_VAP_COMMIT, OnCommitPosted)
END_MESSAGE_MAP()

void CPropTreeItemFontName::OnSelChange ()
{
	PostMessage(WM_VAP_COMMIT, 0, 0);
}

LRESULT CPropTreeItemFontName::OnCommitPosted (WPARAM, LPARAM)
{
	// Raises PTN_ITEMCHANGED, which the page turns into a new sample font.
	CommitChanges();
	return 0;
}

// (ak) The drop down is filled here and not where the row is built. The editor classes
// collection holds 128 attributes over its eight sets, and filling a combo box is a
// CB_ADDSTRING message to a real child window per face - CPropTreeItemCombo IS a CComboBox
// and CreateComboBox() has already created it - so doing that for every attribute at build
// time came to some 30,000 messages and a page that took about five seconds to open. The
// five SQLTools window sets, one attribute each, hid it: five combo boxes instead of 128.
//
// Nothing needs the list before the user opens the cell. The row is in text mode, so the
// cell draws m_sText, OnCommit() reads the face name back with GetWindowText(), and
// ApplyValues() reads GetItemValueText() - a row that is never activated therefore still
// returns the name it was built with, and one that is activated but left alone returns it
// too, because CPropTreeItemCombo::OnActivate() puts m_sText into the edit control whether
// or not the list holds a match.
//
// GetCount() is what makes the fill happen once per row. CB_INITSTORAGE and SetRedraw() take
// the edge off the one fill that does happen: the first lets the list box allocate for the
// whole list instead of growing a reallocation at a time, the second keeps a control that is
// still hidden from repainting per string.
void CPropTreeItemFontName::OnActivate ()
{
	if (m_pFonts && GetCount() == 0)
	{
		const size_t n = m_pFonts->size();

		SetRedraw(FALSE);
		SendMessage(CB_INITSTORAGE, (WPARAM)n, (LPARAM)(n * 32));

		for (size_t i = 0; i < n; i++)
			AddString((LPCTSTR)(*m_pFonts)[i]);

		SetRedraw(TRUE);
	}

	CPropTreeItemCombo::OnActivate();
}

///////////////////////////////////////////////////////////////////////////////
// The page.

BEGIN_MESSAGE_MAP(CVisualAttributesPageTree, Common::CPropTreePage)
	// PTN_ITEMCHANGED, PTN_CHECKCLICK and PTN_ITEMEXPANDING are all entries of the base
	// class map now - the last one drove this page's own accordion until COEClassPageTree
	// needed the same thing and it moved to Common::CPropTreePage. This map stays empty.
END_MESSAGE_MAP()

// The accordion switch of this page. s_oneGroupOpenAtATime is a file static because
// BuildTree() reads it too, to restore at most one expanded group.
BOOL CVisualAttributesPageTree::GetOneGroupOpenAtATime () const
{
	return s_oneGroupOpenAtATime ? TRUE : FALSE;
}

CVisualAttributesPageTree::CVisualAttributesPageTree (
		const std::vector<VisualAttributesSet*>& vasets, LPCTSTR pszTitle, LPCTSTR pszOpenSet)
	: Common::CPropTreePage(pszTitle, 0 /*no help topic*/),
	m_vasets(vasets),
	m_openSet(pszOpenSet ? pszOpenSet : _T(""))
{
	// The 0 help id above clears PSP_HASHELP, which is what the classic page did
	// explicitly (m_psp.dwFlags &= ~PSP_HASHELP): Help\HTMLDefines.h has
	// HIDD_OE_PROP_FONT_COLORS, but Help\sqltools.hhp has no [ALIAS] entry for it.
	//
	// The default title is the one the first three hosts gave the classic page. The fourth,
	// COEDocument::ShowSettingsDialog(), passes the caption the classic template carried -
	// "Editor::Fonts and Colors" - because that sheet edits the visual attributes of the
	// editor classes and not the fonts of the SQLTools windows.
	//
	// The custom colour slots the picker offers as presets. BuildColourPresets() overwrites
	// them with the colours the sets hold, but ChooseColor() always reads all 16 and a page
	// built from sets that carry no colour bit at all - the five SQLTools window sets - never
	// gets a colour into the array, so it starts as the white the library fallback used.
	// High byte zero: ChooseColor() wants 0x00bbggrr.
	for (int i = 0; i < customColourSlots; i++)
		m_customColours[i] = RGB(255, 255, 255);
}

CVisualAttributesPageTree::~CVisualAttributesPageTree ()
{
	// CPropTreeItemColor::s_pColors is a static pointer into this page. Unhook it, so that a
	// colour row of another page - or of a later instance of this one - cannot hand
	// ChooseColor() the address of an m_customColours that is gone. The popup itself is owned
	// by the tree's list window rather than being a child of it, but ownership carries the
	// same guarantee here: Windows destroys an owned window with its owner, so it is long gone
	// before this runs.
	CPropTreeItemColor::SetDefaultColorsList(NULL);
}

void CVisualAttributesPageTree::EnumerateFonts ()
{
	if (!m_fontNames.empty())
		return;                        // enumerated already

	std::set<std::wstring> seen;

	FontEnumData data;
	data.m_all        = &m_fontNames;
	data.m_fixedPitch = &m_fixedPitchFontNames;
	data.m_seen       = &seen;

	// EnumFonts() only needs a DC of the display device to resolve the pitch and the
	// charset of the faces; the classic page passed its own window DC. The desktop
	// window is used here so the enumeration does not depend on the page window
	// existing - VisualAttributes.cpp does the same for its pixels-per-inch constant.
	CWindowDC dc(CWnd::GetDesktopWindow());

	::EnumFonts(dc.GetSafeHdc(), NULL, (FONTENUMPROC)enumFontsProc, (LPARAM)&data);

	std::sort(m_fontNames.begin(),         m_fontNames.end(),         fontNameLess);
	std::sort(m_fixedPitchFontNames.begin(), m_fixedPitchFontNames.end(), fontNameLess);
}

// The colours the picker offers as custom presets: the ones the sets handed to the page
// hold right now, which is what makes "More Colors..." useful here - the colour the user
// wants is often one another attribute of another class already uses, and the theme matrix
// and the ten standard colours of the popup rarely hold it.
//
// A colour is collected per attribute AND per property: a foreground only while the mask of
// its attribute carries vaoForeground, a background only while it carries vaoBackground. A
// property whose bit is not set is inherited from the base attribute of the set
// (VisualAttribute::Construct()), so the value the attribute happens to hold is not one that
// is used - and the base attribute, which does carry the bit, contributes it instead.
//
// Distinct colours only, in the order the sets hold them, and at most the 16 slots
// ChooseColor() has; the slots left over keep the white the constructor put there.
void CVisualAttributesPageTree::BuildColourPresets ()
{
	std::vector<COLORREF> used;

	for (size_t s = 0; s < m_vasets.size(); s++)
	{
		if (!m_vasets[s])
			continue;

		VisualAttributesSet& vaset = *m_vasets[s];

		for (int a = 0; a < vaset.GetCount(); a++)
		{
			VisualAttribute& attr = vaset[a];

			const COLORREF colour[2] =
				{ (COLORREF)attr.m_Foreground, (COLORREF)attr.m_Background };
			const unsigned bit[2] =
				{ Common::vaoForeground,       Common::vaoBackground };

			for (int c = 0; c < 2; c++)
			{
				if (!(attr.m_Mask & bit[c]))
					continue;

				// (COLORREF)-1 is the "not set" marker VisualAttribute::Clear() writes.
				if (colour[c] == (COLORREF)-1)
					continue;

				if (std::find(used.begin(), used.end(), colour[c]) == used.end())
					used.push_back(colour[c]);
			}
		}
	}

	for (int i = 0; i < customColourSlots; i++)
		m_customColours[i] = RGB(255, 255, 255);

	const size_t n = (used.size() < (size_t)customColourSlots)
		? used.size() : (size_t)customColourSlots;

	for (size_t i = 0; i < n; i++)
		m_customColours[i] = used[i];

	// From here on every CPropTreeItemColor of this page hands ChooseColor() THIS array as
	// lpCustColors, so the colours the user defines there are written back into it and
	// survive from one opening of the picker to the next. The array is a member, so it
	// outlives every popup; the destructor unhooks it again.
	CPropTreeItemColor::SetDefaultColorsList(m_customColours);

	// (ak) And the size those popups are drawn at. Both of these are statics read by the
	// item's constructor, which is what makes this the right place: BuildTree() calls
	// BuildColourPresets() before it builds the first colour row, so every row the page makes
	// picks the two up without InsertColor() having to thread them through.
	CPropTreeItemColor::SetDefaultPopupScale(s_colourPopupScalePct);
}

void CVisualAttributesPageTree::BuildTree ()
{
	EnumerateFonts();

	// Before the first colour row is built: the rows only read the array, they do not fill it.
	BuildColourPresets();

	m_rows.clear();
	m_groups.clear();

	// With the accordion of GetOneGroupOpenAtATime() at most one group is open, so at most one
	// is restored: a state saved before the accordion existed could hold several.
	bool groupOpened = false;

	// The set the host asked the page to open, matched by name the way the classic page
	// matched its m_className against the category tree - but only while one of the sets
	// really carries that name. The classic page fell back to its first category when none
	// did, and closing every group instead would leave the page looking empty, so a name
	// that matches nothing is dropped here and the remembered state stays in charge.
	CString openSet;

	if (!m_openSet.IsEmpty())
	{
		for (size_t s = 0; s < m_vasets.size() && openSet.IsEmpty(); s++)
			if (m_vasets[s] && m_vasets[s]->GetCount() > 0 &&
				m_openSet == Common::wstr(m_vasets[s]->GetName()).c_str())
				openSet = m_openSet;
	}

	// The state of THIS page: its title is what the sheet tree node is built from, so the
	// four sheets have two different keys. operator[] inserts an empty vector for a key no
	// SaveTreeState() has written yet, and an empty state expands every group below.
	const std::vector<BOOL>& state = s_groupExpanded[m_strCaption];

	for (size_t s = 0; s < m_vasets.size(); s++)
	{
		VisualAttributesSet& vaset = *m_vasets[s];

		const int attrCount = vaset.GetCount();

		if (attrCount <= 0)
			continue;                    // a set without attributes has nothing to edit

		// One group per set, labelled with the set name: the top level node of the
		// category tree of the classic page. The expansion state is indexed by the
		// group and not by the set, so that a set skipped above cannot shift the
		// indices SaveTreeState() writes.
		const size_t groupIndex = m_groups.size();

		// A host that hands the page a different number of sets than the host that
		// saved the state starts with every group expanded.
		//
		// While the host named a set to open, that name decides instead and every other
		// group stays closed: the sheet then comes up on the editor class of the document
		// it was opened from, which is what selecting that category did in the classic page.
		BOOL expanded;

		if (!openSet.IsEmpty())
			expanded = (openSet == Common::wstr(vaset.GetName()).c_str()) ? TRUE : FALSE;
		else
			expanded = (groupIndex < state.size()) ? state[groupIndex] : TRUE;

		if (expanded && s_oneGroupOpenAtATime && groupOpened)
			expanded = FALSE;

		if (expanded)
			groupOpened = true;

		const CString info  = setGroupInfo(vaset.GetName());

		CPropTreeItem* pGroup = InsertGroup(m_tree,
			Common::wstr(vaset.GetName()).c_str(), info, expanded);

		m_groups.push_back(pGroup);

		for (int a = 0; a < attrCount; a++)
		{
			VisualAttribute& attr = vaset[a];

			// What the classic ShowFont() displayed: the attribute mixed with its base,
			// and the base of an attribute is the FIRST attribute of its own set -
			// GetBaseAttribute() returns &(*set)[0], the "Text" attribute. Construct()
			// takes every property whose mask bit is not set from the base, so a row of
			// an inherited property shows the value that is really used. For a set of
			// one attribute the base is the attribute itself and the mix is a copy.
			VisualAttribute shown;
			attr.Construct(shown, vaset[0]);

			// A set holding several attributes gets a sub-group per attribute: the child
			// node of the classic category tree. The five SQLTools sets hold one
			// attribute each, so their rows sit in the set group itself.
			//
			// The sub-group is a CPropTreeItemFontSample and not the plain item
			// InsertGroup() builds: its attribute cell - the right hand side of the level 2
			// node - IS the sample area of the attribute. The sample is then there whether
			// the node is open or closed, which is what makes an open set readable at a
			// glance, and no sample ROW of its own is needed under it; BuildAttributeRows()
			// takes the node over as rows.m_sample. A root level node cannot do this:
			// CPropTreeItem::DrawItem() skips DrawAttribute() for one, so the single
			// attribute sets keep the row.
			//
			// Only the top level groups are remembered by SaveTreeState(). The base class
			// accordion closes the other sub-groups as one opens, so an open set
			// starts with its first attribute open rather than with all of them at once.
			CPropTreeItem*           pParent      = pGroup;
			CPropTreeItemFontSample* pGroupSample = 0;

			if (attrCount > 1)
			{
				CString subInfo;
				subInfo.Format(_T("The \"%s\" attribute of this window."),
					Common::wstr(attr.m_Name).c_str());

				pGroupSample = new CPropTreeItemFontSample;

				// InsertItem() first, as everywhere: it is what attaches the item to the
				// tree, and SetLabelText() / SetInfoText() are the steps InsertGroup() takes.
				pParent = m_tree.InsertItem(pGroupSample, pGroup);
				pParent->SetLabelText(Common::wstr(attr.m_Name).c_str());
				pParent->SetInfoText(subInfo);

				// Keeps the name above as the label instead of "Sample (100%)". Has to come
				// before the first SetSampleAttribute(), which is what would rename the row.
				pGroupSample->SetLabelIsAttributeName();

				if (expanded && a == 0)
					pParent->Expand();
			}

			BuildAttributeRows(attr, shown, pParent, pGroupSample);
		}
	}
}

void CVisualAttributesPageTree::BuildAttributeRows (VisualAttribute& attr,
	const VisualAttribute& shown, CPropTreeItem* pParent,
	CPropTreeItemFontSample* pGroupSample)
{
	AttributeRows rows;
	rows.m_attr  = &attr;

	// The mix this attribute is shown as. Kept because UpdateSample() needs the value of a
	// property that gets no row of its own; see AttributeRows::m_shown.
	rows.m_shown = shown;

	// A row is built exactly for the properties whose mask bit IS set, and the pointer of a
	// property whose bit is clear stays null. The classic ShowAttribute() built all seven
	// controls and disabled the ones whose bit was missing - EnableWindow(curr->m_Mask &
	// vaoXxx) - but a clear bit means the property comes from the base attribute of the set,
	// so the disabled control only ever restated a value this attribute does not own, and
	// the sample row at the bottom previews the effective result anyway. Not building it is
	// also what keeps an inherited property out of the settings file: there is no row for
	// ApplyValues() to read.
	//
	// In the editor classes sets this is most of the rows. Only the base "Text" attribute
	// carries vaoFontName and vaoFontSize - mask 887 - while the syntax classes carry 368,
	// the bold/italic/underline bits plus vaoForeground, and the two highlighting attributes
	// vaoBackground alone, so a class used to show some twenty five "Font" combos and now
	// shows one.

	// The "Font" combo box of the classic page was filled by ::EnumFonts() with all the
	// faces, or with the fixed pitch ones only while the mask asked for a fixed pitch font
	// (enumFixedFontsFunc instead of enumFontsFunc).
	if (attr.m_Mask & Common::vaoFontName)
	{
		const std::vector<CString>& fonts =
			(attr.m_Mask & Common::vaoFontFixedPitch) ? m_fixedPitchFontNames : m_fontNames;

		// This row is built by hand and not with PropTreeRows::InsertComboText() only because
		// it needs CPropTreeItemFontName above, the combo box that commits as soon as a face
		// is picked so that the sample row follows. The steps are the helper's, and so is its
		// ORDER: InsertItem() has to come first, because it is what attaches the item to the
		// tree (CPropTreeItem::SetPropOwner()) and CreateComboBox() needs m_pProp for the
		// parent window - m_pProp->GetCtrlParent() - and for the normal font. Creating the
		// combo box before inserting the row asserts in debug and dereferences a null pointer
		// in release.
		CPropTreeItemFontName* pFontName =
			(CPropTreeItemFontName*)m_tree.InsertItem(new CPropTreeItemFontName, pParent);

		pFontName->SetLabelText(_T("Font"));
		pFontName->SetInfoText(_T("The face name of the font the text is drawn with. Pick one of the installed fonts or type an exact name."));
		pFontName->CreateComboBox(WS_CHILD | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL);

		// (ak) Was, right here: a loop AddString()-ing every installed face into this combo
		// box, once per attribute - some 30,000 window messages for the editor classes page
		// and about five seconds before it appeared. The list is handed over instead and
		// filled by the item the first time the cell is activated; see
		// CPropTreeItemFontName::OnActivate(). Building this row for the base attribute only,
		// which is the only one whose mask carries vaoFontName, is the other half of that.
		pFontName->SetFontList(&fonts);

		pFontName->SetItemValueText(Common::wstr(shown.m_FontName).c_str());

		rows.m_fontName = pFontName;
	}

	if (attr.m_Mask & Common::vaoFontSize)
	{
		rows.m_fontSize = InsertNumberEdit(m_tree, pParent,
			_T("Size, points"),
			_T("The height of the font, in points."),
			shown.m_FontSize, s_fontSizeStep, s_fontSizeMin, s_fontSizeMax);
	}

	if (attr.m_Mask & Common::vaoFontBold)
	{
		rows.m_bold = InsertCheckedStatic(m_tree, pParent,
			_T("Bold"),
			_T("Draw the text in bold."),
			shown.m_FontBold);
	}

	if (attr.m_Mask & Common::vaoFontItalic)
	{
		rows.m_italic = InsertCheckedStatic(m_tree, pParent,
			_T("Italic"),
			_T("Draw the text slanted."),
			shown.m_FontItalic);
	}

	if (attr.m_Mask & Common::vaoFontUnderline)
	{
		rows.m_underline = InsertCheckedStatic(m_tree, pParent,
			_T("Underline"),
			_T("Draw the text underlined."),
			shown.m_FontUnderline);
	}

	// The two colour rows: the "Color" group box of the classic page, whose "Foreground:"
	// and "Background:" CColourPickerXP buttons ShowAttribute() enabled per
	// curr->m_Mask & vaoForeground / vaoBackground. Built on the same two bits, so a row
	// that is there opens the palette and a colour inherited from the base attribute gets no
	// row - the sample row below shows the colour that results, in the palette's place.
	//
	// The value a row is seeded with is the effective one - shown, the mix Construct()
	// resolved - which for a bit the attribute does carry is its own.
	//
	// (COLORREF)-1 is the "not set" marker Clear() writes, and a set whose attributes carry
	// no colour bit at all - the five SQLTools window sets - leaves it there. Those sets get
	// no colour row, and the sample row falls back to the window colours by itself; see
	// CPropTreeItemFontSample::DrawAttribute().
	if (attr.m_Mask & Common::vaoForeground)
	{
		const COLORREF clrForeground = (shown.m_Foreground == (unsigned)-1)
			? (COLORREF)::GetSysColor(COLOR_WINDOWTEXT) : (COLORREF)shown.m_Foreground;

		rows.m_foreground = InsertColor(m_tree, pParent,
			_T("Foreground"),
			_T("The colour the text is drawn in. The cell opens a palette of named colours that the arrow keys can walk - Enter picks one, Escape closes it unchanged - and its \"More Colors...\" row the colour dialog, whose custom slots offer the colours these settings already use."),
			clrForeground);
	}

	if (attr.m_Mask & Common::vaoBackground)
	{
		const COLORREF clrBackground = (shown.m_Background == (unsigned)-1)
			? (COLORREF)::GetSysColor(COLOR_WINDOW) : (COLORREF)shown.m_Background;

		rows.m_background = InsertColor(m_tree, pParent,
			_T("Background"),
			_T("The colour the text is drawn on. The cell opens a palette of named colours that the arrow keys can walk - Enter picks one, Escape closes it unchanged - and its \"More Colors...\" row the colour dialog, whose custom slots offer the colours these settings already use."),
			clrBackground);
	}

	// The sample: the tree equivalent of the classic page's "Text Sample" static
	// (IDC_PFC_SAMPLE), which ShowFont() drew in a font built the same way and OnCtlColor()
	// in the colours of the attribute. Read only and without an editor, it shows the rows
	// above and has no value of its own, so ApplyValues() does not read it - the attribute
	// is written from those rows.
	//
	// WHERE it is drawn depends on the shape of the set. A set that nests its attributes -
	// the editor classes - draws it in the attribute cell of the level 2 node BuildTree()
	// built as a CPropTreeItemFontSample and handed in as pGroupSample, so the sample is
	// visible while that node is closed as well and the attribute needs no row of its own
	// for it. A set of one attribute has no level 2 node, and a root level node gets no
	// attribute cell at all, so it gets the read only sample ROW instead.
	//
	// Either way it is the one thing built whatever the mask holds: it previews the
	// EFFECTIVE attribute, the inherited properties included, so it is the only thing worth
	// showing for an attribute that owns nothing of its own - "Current Line" and
	// "Bookmark", whose mask is vaoBackground alone, get this and one colour row.
	if (pGroupSample)
	{
		rows.m_sample = pGroupSample;
	}
	else
	{
		rows.m_sample = new CPropTreeItemFontSample;

		rows.m_sample->SetInfoText(_T("A sample of the font the rows above select. A font taller than this row is scaled down to fit it, so its shape stays true; the label then states the percentage of its true size the sample is drawn at."));
		rows.m_sample->ReadOnly();

		m_tree.InsertItem(rows.m_sample, pParent);
	}

	// Sets the label of a sample ROW too: "Sample", or "Sample (90%)" when the font is too
	// tall for the row and the sample is scaled down to fit it. A group node keeps the
	// attribute name it was given.
	rows.m_sample->SetSampleAttribute(shown);

	m_rows.push_back(rows);
}

// PTN_ITEMCHANGED / PTN_CHECKCLICK of one of the font rows: the sample row of the
// attribute that row belongs to is rebuilt from the values its rows hold now. The base
// class has marked the page modified before calling this.
//
// A commit is what raises the notification, so the sample follows the last pick from the
// "Font" list (CPropTreeItemFontName commits that cell on CBN_SELCHANGE), the last typed
// size (CPropTreeItemEdit commits on Enter and on losing the focus) and every click on
// the Bold, Italic and Underline rows (PTN_CHECKCLICK).
void CVisualAttributesPageTree::OnItemChanged (CPropTreeItem* pItem)
{
	if (!pItem)
		return;

	for (size_t i = 0; i < m_rows.size(); i++)
	{
		AttributeRows& rows = m_rows[i];

		if (pItem != rows.m_fontName && pItem != rows.m_fontSize &&
			pItem != rows.m_bold     && pItem != rows.m_italic &&
			pItem != rows.m_underline &&
			pItem != rows.m_foreground && pItem != rows.m_background)
			continue;

		UpdateSample(rows);

		// The sample row is drawn and is not a child window, so the page has to be
		// invalidated with its children for the new font to show.
		RedrawTree();
		return;
	}
}

// Builds the attribute the sample row previews: the mix BuildAttributeRows() kept, with the
// values the rows that DO exist hold now laid over it. They are read exactly the way
// ApplyValues() reads them, so the sample shows what Apply would store - the committed value
// of every row, clamped to the limits of the size row - plus what the properties without a
// row of their own contribute.
void CVisualAttributesPageTree::UpdateSample (AttributeRows& rows)
{
	if (!rows.m_sample)
		return;

	// m_shown is the attribute with every property its own mask does not carry taken from the
	// base attribute of the set, which is the value a property without a row has - and one
	// that cannot change while this page is open, there being no row to change it with. It
	// carries the mask as well, and that is what tells
	// CPropTreeItemFontSample::DrawAttribute() to draw in the colours of the attribute rather
	// than in the window colours: Construct() ORs the bits of the attribute with the base's,
	// so a colour inherited from the base counts as one of its own, and a colour neither of
	// them carries - the five SQLTools window sets - stays at the (UINT)-1 marker Clear()
	// wrote, which that row reads as "use the window colours".
	VisualAttribute attr = rows.m_shown;

	if (rows.m_fontName)
		attr.m_FontName = Common::str(rows.m_fontName->GetItemValueText());

	if (rows.m_fontSize)
	{
		int fontSize = 0;
		rows.m_fontSize->GetItemValue(fontSize);
		attr.m_FontSize = fontSize;
	}

	if (rows.m_bold)
		attr.m_FontBold = rows.m_bold->IsChecked() ? true : false;

	if (rows.m_italic)
		attr.m_FontItalic = rows.m_italic->IsChecked() ? true : false;

	if (rows.m_underline)
		attr.m_FontUnderline = rows.m_underline->IsChecked() ? true : false;

	// The colours the sample is drawn in. GetItemValue() returns the colour the palette popup
	// committed, which is the value ApplyValues() stores too.
	if (rows.m_foreground)
		attr.m_Foreground = rows.m_foreground->GetItemValue();

	if (rows.m_background)
		attr.m_Background = rows.m_background->GetItemValue();

	rows.m_sample->SetSampleAttribute(attr);
}

// Called from Common::CPropTreePage::OnApply(), which has already committed the cell
// being edited and which reports exceptions. There is nothing to validate here: the
// size row clamps its value to the item limit BuildTree() set, and the other rows
// hold a text or a check state.
BOOL CVisualAttributesPageTree::ApplyValues ()
{
	// All three sheets hand the page a COPY of the settings - SettingsDialogCallback
	// owns one and pushes it into the global settings after OK, and the two
	// OnGridSettings() take one at their beginning and do the same - and the pointers
	// of GetVASets() point into the m_vasets of that copy. Writing the attributes is
	// therefore all the page has to do; the single NotifySettingsChanged() is raised
	// by the host after OK, and it is what makes the views rebuild their fonts
	// (CGridViewPaintAccessories::OnSettingsChanged()).
	//
	// Unlike the pages bound to SQLToolsSettings there are no accessors here: the
	// m_FontName, m_FontSize, ... members of VisualAttribute are public, and the
	// classic page wrote them directly too.
	for (size_t i = 0; i < m_rows.size(); i++)
	{
		AttributeRows& rows  = m_rows[i];
		VisualAttribute& attr = *rows.m_attr;

		// Only the properties whose mask bit IS set are written back, and a row is built
		// exactly for the bits that are - see BuildAttributeRows() - so every test below
		// holds twice over: the mask, which is the rule, and the pointer, which that rule
		// leaves non-null. A bit that is not set means the property comes from the base
		// attribute of the set (VisualAttribute::Construct()), so the value the attribute
		// holds is not the value that is used and storing a copy of the base value would
		// only bloat the settings file. With no row built there is nothing that could. For
		// the five SQLTools sets every font bit is set, so every font row is written.
		//
		// The empty text test is the tree equivalent of the "inx != CB_ERR" guard of
		// the classic OnFontChanged(), which kept the stored name while the list-only
		// combo box had no selection: an empty face name is never meaningful, so the
		// stored one is kept.
		if ((attr.m_Mask & Common::vaoFontName) && rows.m_fontName)
		{
			const CString fontName = rows.m_fontName->GetItemValueText();

			if (!fontName.IsEmpty())
				attr.m_FontName = Common::str(fontName);
		}

		if ((attr.m_Mask & Common::vaoFontSize) && rows.m_fontSize)
		{
			// GetItemValue() returns the value clamped to the item limit set in
			// BuildAttributeRows(), which keeps a typed size inside s_fontSizeMin ..
			// s_fontSizeMax - there is no DDV_MinMax* to fall back on in a tree cell.
			int fontSize = 0;
			rows.m_fontSize->GetItemValue(fontSize);
			attr.m_FontSize = fontSize;
		}

		if ((attr.m_Mask & Common::vaoFontBold) && rows.m_bold)
			attr.m_FontBold = rows.m_bold->IsChecked() ? true : false;

		if ((attr.m_Mask & Common::vaoFontItalic) && rows.m_italic)
			attr.m_FontItalic = rows.m_italic->IsChecked() ? true : false;

		if ((attr.m_Mask & Common::vaoFontUnderline) && rows.m_underline)
			attr.m_FontUnderline = rows.m_underline->IsChecked() ? true : false;

		// The two colours, by the same rule: only a bit the attribute carries is written
		// back, so an inherited colour is left alone instead of being copied into the
		// settings file. This is what the classic OnColorChanged() stored, minus its
		// unconditional write of both pickers - it read the disabled one too, which stored
		// the base value as if the attribute had one of its own.
		if ((attr.m_Mask & Common::vaoForeground) && rows.m_foreground)
			attr.m_Foreground = rows.m_foreground->GetItemValue();

		if ((attr.m_Mask & Common::vaoBackground) && rows.m_background)
			attr.m_Background = rows.m_background->GetItemValue();
	}

	return TRUE;
}

// Called from OnApply() and from OnCancel(), so the expansion state survives both.
// The page may never have been built - OnCancel() of a sheet whose page was not
// shown yet - hence the empty test; it also keeps the state a previous host stored
// instead of clearing it.
void CVisualAttributesPageTree::SaveTreeState ()
{
	if (m_groups.empty())
		return;

	std::vector<BOOL>& state = s_groupExpanded[m_strCaption];

	state.assign(m_groups.size(), TRUE);

	for (size_t i = 0; i < m_groups.size(); i++)
		if (m_groups[i])
			state[i] = m_groups[i]->IsExpanded();
}

// The accordion this page needs - one open set, and inside it one open attribute - is a
// base class feature now: Common::CPropTreePage handles PTN_ITEMEXPANDING and calls
// CloseOtherGroups(), which walks the parent of the item being opened and so covers the
// attribute sub-groups too. GetOneGroupOpenAtATime() above is what asks for it. Closing
// the sub-groups costs nothing here because the level 2 node draws the attribute's sample
// on its right, so the attributes stay comparable while only one of them is open.

