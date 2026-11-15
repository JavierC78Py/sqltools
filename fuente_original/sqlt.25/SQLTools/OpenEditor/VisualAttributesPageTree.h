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

#ifndef __VisualAttributesPageTree_h__
#define __VisualAttributesPageTree_h__
#pragma once

#include <vector>
#include "Common/VisualAttributes.h"
#include "Common/PropTreePage.h"
#include "PropTree/PropTreeItemCombo.h"
#include "PropTree/PropTreeItemEdit.h"
#include "PropTree/PropTreeItemStatic.h"
#include "PropTree/PropTreeItemColor.h"

// The read only "Sample" row of an attribute - the one item class this page needs
// that CPropTree does not have. It is defined in the .cpp, next to the page.
class CPropTreeItemFontSample;

////////////////////////////////////////////////////////////////////////////////
// "SQLTools::Font and Color" - the CPropTree based ("new style") page that
// replaces the classic CVisualAttributesPage (IDD_OE_PROP_FONT_COLORS, template
// caption "Editor::Fonts and Colors") for the fonts of the SQLTools windows: the
// VisualAttributesSet collection SQLToolsSettings::GetVASets() returns.
//
// The classic page stays in the project, it is only unhooked from the sheets that
// showed it. It is not shown at all now: the color rows this page gained - see "The
// color rows" below - are what the last host still needed, so
// COEDocument::ShowSettingsDialog() builds this class for the other collection too,
// the visual attributes of the editor classes ("Editor::Fonts and Colors"), whose
// masks do hold the color bits. The classic page remains as a reference and as a
// rollback path.
//
// The page is shown from FOUR sheets, which hand it two different collections:
//   - the main settings sheet (SettingsDialogCallback, App\SQLTools_4.cpp) and
//     GridView::OnGridSettings() / GridWnd::OnGridSettings(), the two copies of the
//     "Grid settings" sheet: all three hand it the same five sets and use the title
//     below to hang the page under its "SQLTools" tree node;
//   - COEDocument::ShowSettingsDialog(), which hands it the visual attributes of the
//     editor classes, the title the classic template carried, and the name of the
//     class of the active document as the set to open first.
//
// The data. SQLToolsSettings' constructor builds five sets - "Data Grid Window",
// "Statistics Window", "Output Window", "History Window" and "Bind Window" - each
// holding ONE attribute, "Text", created with the VisualAttribute() default mask
// vaoFont: font name, size, bold, italic and underline, and NO color bit.
// XmlStreamerBase::read() matches the attributes of a set by name and never adds
// one, so this layout is fixed by the code and not by the settings file.
//
// The editor classes collection is different: one set per class, holding several
// attributes - "Text", "User Objects", "Highlighting", "Error Highlighting",
// "Find Match" (the step 4 all-occurrences overlay background) and the
// syntax classes - whose masks carry the color bits as well. OESettingsXmlStreamer.cpp
// adds the four named ones when a settings file does not have them yet: "User
// Objects" with mask 0x170, the bold/italic/underline bits plus vaoForeground, and the
// three background attributes with vaoBackground alone.
//
// The color rows. Foreground and Background are CPropTreeItemColor rows, built only while
// the mask of the attribute carries vaoForeground / vaoBackground. The classic
// ShowAttribute() built both CColourPickerXP buttons always and disabled the one whose bit
// was missing - EnableWindow(curr->m_Mask & vaoXxx) - which showed a colour no property of
// the attribute owns. Which rows are built is decided in BuildAttributeRows().
//
// The palette popup of the item holds its 40 named colors plus a "More Colors" button
// that opens ChooseColor(). The 16 custom slots of that dialog are this page's
// m_customColours, seeded by BuildColourPresets() with the colors the sets hold now, so
// the picker offers as presets the colors that are currently used - and ChooseColor()
// writes the colors the user defines back into the same array, which keeps them for the
// rest of the life of the page.
//
// The tree. The category tree of the classic page (a node per set, a child per
// attribute) plus the one font editor on its right become one group per set holding
// the rows of that editor. A set holding more than one attribute gets a sub-group
// per attribute, so the same class can show a collection whose sets hold several
// attributes; the five sets above hold one each, hence no sub-group.
//
// The sample row. Every attribute gets a read only "Sample" row whatever its mask holds -
// it previews the effective font and colours, the inherited properties included, so it is
// the one row worth showing for an attribute that owns nothing at all. It draws the
// text the classic page's IDC_PFC_SAMPLE static drew, in the font AND in the two
// colors the rows above it select - SetFont() plus OnCtlColor() of the classic page.
// The row keeps the regular item height: a font too tall for it is scaled
// down to fit, so the shape stays true and no third party change is needed, and the
// label then states the percentage of its true size the sample is drawn at. See
// CPropTreeItemFontSample in the .cpp.
//
// One open group at a time. Opening a set group closes the others, so the set being
// edited is the only one on screen: five sets of six rows do not fit the tree pane
// of the sheet. The accordion is a base class feature - see GetOneGroupOpenAtATime()
// below and Common::CPropTreePage::CloseOtherGroups().
//
// Colors for the sets that have them. The five SQLTools window sets carry no color bit,
// and the one consumer of those sets, CGridViewPaintAccessories::OnSettingsChanged() -
// which every data grid, bind grid, output, statistics, history and explain plan view
// goes through - builds a font only (VisualAttribute::NewFont()). Those sets therefore get
// no color row at all: the classic page showed two disabled pickers holding the window
// colors, a value no property of theirs owns and nothing of theirs reads. The editor
// classes sets do carry the bits and their rows open the palette.
//
// What the tree cannot reproduce from the classic dialog, and how it is handled:
//   - the "Font" combo box was a sorted drop down LIST of the enumerated fonts
//     (CBS_DROPDOWNLIST|CBS_SORT) and showed nothing while the stored name was not
//     in the list, in which case OnFontChanged() skipped the name and the stored
//     one survived. The tree cell is an editable combo box holding the same
//     enumerated list, so the stored name is always shown, a name that is not
//     enumerated (a font that was uninstalled) still survives, and an exact name
//     can be typed.
//   - the "Size" combo box listed the sizes the selected font supports -
//     enumFontSizesFunc() offers 8..24 for a TrueType font and the point sizes of a
//     raster font - and ShowFont() re-filled it on every font change. The tree cell
//     is a spin edit: any size can be typed and is clamped to the item limits, so
//     there is nothing to rebuild when the font name row changes - the only row that
//     depends on another one is the sample row, see OnItemChanged().
//   - a property whose mask bit is not set gets NO ROW, where the classic page built the
//     control and disabled it - EnableWindow(curr->m_Mask & vaoXxx). The bit being clear
//     means the property is inherited from the base attribute of the set, the FIRST one,
//     see BuildAttributeRows(), so a disabled row only ever restated a value the attribute
//     does not own; the sample row previews the effective result instead. It also leaves
//     ApplyValues() nothing to write, so an inherited property cannot be turned into a
//     stored copy of the base value. In the editor classes sets only the base "Text"
//     attribute carries vaoFontName, so a class shows one "Font" row instead of one per
//     attribute - which is also most of what made that page slow to open.
//   - the mnemonics of the template captions ("&Font:", "&Size:", "B&old",
//     "&Italic", "&Underline") are dropped: CPropTree draws its labels with
//     DT_NOPREFIX, so an ampersand would be shown literally.
//
// The classic page had no help topic (it cleared PSP_HASHELP, and
// Help\sqltools.hhp has no [ALIAS] entry for HIDD_OE_PROP_FONT_COLORS), hence the
// 0 help id passed to the base class.
//
// See SQLTools/md/proptree_settings_page_conversion.md.
////////////////////////////////////////////////////////////////////////////////
class CVisualAttributesPageTree : public Common::CPropTreePage
{
public:
	// vasets: the sets to edit. They are NOT owned: the page writes into the
	// attributes they point to, exactly like the classic page did, so they have to
	// outlive it - every host keeps them in a settings object of its own scope.
	// pszTitle: the sheet tree node in the "Parent::Child" notation; the default is
	// the title all three hosts gave the classic page.
	// pszOpenSet: the name of the set whose group opens first, or NULL to take the
	// remembered expansion state. This is the classic page's m_className, which its
	// OnInitDialog() matched against the set names to select the category of the
	// document the sheet was opened from.
	CVisualAttributesPageTree (const std::vector<Common::VisualAttributesSet*>& vasets,
		LPCTSTR pszTitle = _T("SQLTools::Font and Color"), LPCTSTR pszOpenSet = NULL);
	virtual ~CVisualAttributesPageTree ();

protected:
	// Builds the groups and the rows from the sets the page was given; called once
	// from Common::CPropTreePage::OnInitDialog().
	virtual void BuildTree ();

	// Writes the tree values back into the attributes the sets point to.
	virtual BOOL ApplyValues ();

	// Remembers which groups were expanded, so the page reopens as it was closed.
	virtual void SaveTreeState ();

	// PTN_ITEMCHANGED / PTN_CHECKCLICK of a font row: rebuilds the sample row of the
	// attribute the changed row belongs to. The base class has already marked the
	// page modified when this runs.
	virtual void OnItemChanged (CPropTreeItem* pItem);

	// Two levels of gutter instead of the default one. This page is the only one that nests a
	// sub-group between a set and its attribute rows - BuildTree() creates one whenever a set
	// holds more than a single attribute, and no other page's sets do - so it is the only one
	// with chevrons at two depths. Without the second level the sub-group's chevron, which steps
	// in with its row, would sit on the white item fill beside the gray band instead of on it.
	virtual int GetTreeGutterLevels () const { return 2; }

	// And the indent that goes with it, so the sub-group actually steps in. Without this the
	// control hands a top level group's children the group's own x: the sub-group's chevron would
	// land at the same 2 pixels as the group's above it, showing no depth, and its label would
	// start at x=21 - inside the widened gutter, half on gray.
	virtual BOOL GetTreeIndentsRootChildren () const { return TRUE; }

	// One open group at a time. Opening a set group closes the others, so the set being
	// edited is the only one on screen: five sets of six rows do not fit the tree pane
	// of the sheet. The accordion itself lives in the base class now - see
	// Common::CPropTreePage::GetOneGroupOpenAtATime() / CloseOtherGroups() - because
	// COEClassPageTree needs the same one; s_oneGroupOpenAtATime in the .cpp still decides
	// whether this page asks for it, BuildTree() included.
	virtual BOOL GetOneGroupOpenAtATime () const;

	DECLARE_MESSAGE_MAP ()

private:
	// The rows of ONE VisualAttribute. The pointers are null until BuildTree() ran, and
	// Common::CPropTreePage::OnApply() calls ApplyValues() only after it. A pointer stays
	// null when the mask of the attribute lacks the bit of the property it belongs to: that
	// property is inherited and gets no row at all, see BuildAttributeRows(). That is what
	// makes the null test and the mask test the same test in ApplyValues().
	struct AttributeRows
	{
		Common::VisualAttribute* m_attr;      // the attribute edited, not owned
		CPropTreeItemCombo*      m_fontName;
		CPropTreeItemEdit*       m_fontSize;
		CPropTreeItemStatic*     m_bold;
		CPropTreeItemStatic*     m_italic;
		CPropTreeItemStatic*     m_underline;
		CPropTreeItemColor*      m_foreground;
		CPropTreeItemColor*      m_background;
		CPropTreeItemFontSample* m_sample;    // read only, no value of its own

		// The attribute as it is SHOWN: itself where its mask carries a bit, the base
		// attribute of its set - the first one - where it does not, the mix
		// VisualAttribute::Construct() resolved in BuildTree(). A copy, so it does not
		// depend on the lifetime of the temporary handed down. UpdateSample() starts from
		// it, which is how a property that has no row of its own still reaches the sample,
		// and its mask - Construct() ORs the bits of the attribute with the base's - is what
		// tells the sample row to draw in the colours of the attribute rather than in the
		// window colours.
		Common::VisualAttribute  m_shown;

		AttributeRows ()
			: m_attr(0), m_fontName(0), m_fontSize(0),
			m_bold(0), m_italic(0), m_underline(0),
			m_foreground(0), m_background(0), m_sample(0)
		{
		}
	};

	// Fills m_fontNames and m_fixedPitchFontNames with ::EnumFonts(), once per page.
	void EnumerateFonts ();

	// Fills m_customColours with the colours the sets hold right now and hands the array to
	// CPropTreeItemColor::SetDefaultColorsList(), so that the 16 custom slots of the
	// ChooseColor() dialog the "More Colors" button opens offer them as presets. Called from
	// BuildTree(), before the first colour row is built.
	void BuildColourPresets ();

	// Builds the rows of one attribute under pParent. attr is the attribute itself - its
	// mask decides which rows are built AT ALL and which values are written back - and
	// shown is attr mixed with the base attribute of its set, which is what the classic
	// ShowFont() displayed and what every row that is built is seeded with.
	//
	// pGroupSample is the level 2 node BuildTree() created for an attribute of a set that
	// nests them, already inserted and labelled with the attribute name, and null for a set
	// of one attribute whose rows sit in the set group itself. When it is there the node's
	// own attribute cell is the sample area of the attribute - the sample shows while the
	// node is closed too - and no sample row is built under it.
	void BuildAttributeRows (Common::VisualAttribute& attr,
		const Common::VisualAttribute& shown, CPropTreeItem* pParent,
		CPropTreeItemFontSample* pGroupSample);

	// Rebuilds the font of the sample row of rows from the values its five font rows
	// hold now, the value of the cell being edited included.
	void UpdateSample (AttributeRows& rows);

	// ChooseColor() takes exactly 16 custom colour slots, and
	// CPropTreeItemColor::SetDefaultColorsList() hands it this array as lpCustColors.
	enum { customColourSlots = 16 };

	std::vector<Common::VisualAttributesSet*> m_vasets;  // not owned
	std::vector<AttributeRows>                m_rows;
	std::vector<CPropTreeItem*>               m_groups;  // the set groups, for SaveTreeState()
	std::vector<CString>                      m_fontNames;
	std::vector<CString>                      m_fixedPitchFontNames;

	// The set whose group opens first; empty takes the remembered expansion state. See the
	// constructor.
	CString m_openSet;

	// The custom colour slots of the picker, seeded with the colours in use and then kept
	// up to date by ChooseColor() itself, which writes the colours the user defines back
	// into lpCustColors. A member and not a local so that it outlives every popup the page
	// opens; the destructor unhooks it again.
	COLORREF m_customColours[customColourSlots];
};

//{{AFX_INSERT_LOCATION}}

#endif//__VisualAttributesPageTree_h__
