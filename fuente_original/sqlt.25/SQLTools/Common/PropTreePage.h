#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __PropTreePage_h__
#define __PropTreePage_h__

#include "PropTree/PropTree.h"

#include <vector>

namespace Common {

////////////////////////////////////////////////////////////////////////////////
// What a settings search matches against - see CPropTreePage::CollectMatches().
//
// Values are opt-in and off by default: a settings page is mostly numbers and
// colours, and matching those turns a search for "10" into a hit on every row that
// happens to hold a 10. A label, a description or a group name is what a user
// actually remembers a setting by.
enum PropTreeSearchField
{
	PSF_LABEL   = 0x0001,   // the row's label
	PSF_INFO    = 0x0002,   // its description - the text the info pane shows
	PSF_PATH    = 0x0004,   // the labels of the groups the row sits in
	PSF_CHOICES = 0x0008,   // every entry of a combo row, not just the picked one
	PSF_VALUE   = 0x0010,   // the row's current value text

	PSF_DEFAULT = PSF_LABEL | PSF_INFO | PSF_PATH | PSF_CHOICES,
};

// A GROUP row - one that has children - is matched on PSF_LABEL and PSF_PATH only. Its
// info text describes what the group holds instead of naming a setting, so matching it
// promotes a header above the rows inside it; see EnumCollectMatches() in
// PropTreePage.cpp. Every field still applies to ordinary rows.

////////////////////////////////////////////////////////////////////////////////
// One row a search matched.
//
// nCtrlID and NOT pItem is the durable identity: CVisualAttributesPageTree deletes
// and recreates its sample rows when the sample text changes, so a pointer taken
// before such an edit dangles and the next F3 would walk freed memory. Resolve the
// id again with CPropTree::FindItem(UINT) when it is time to reveal a hit that was
// found earlier. pItem is still filled in, because the common case - reveal the hit
// that was just found - is safe with it and saves the lookup.
struct PropTreeSearchHit
{
	PropTreeSearchHit () : pItem(0), nCtrlID(0) {}

	CPropTreeItem* pItem;
	UINT           nCtrlID;
	CString        sPath;   // "Page title > Group", for a message or a tooltip
};

////////////////////////////////////////////////////////////////////////////////
// The rule CollectMatches() matches with, exposed so that a caller searching page
// titles - CPropertySheetMem does - applies the very same test and the two cannot
// drift apart. A title that matched by one rule while rows matched by another
// would make F3 skip pages, or land on them for a term no row there contains.

// Splits pszQuery on whitespace into lowercased terms. FALSE when pszQuery holds no
// term at all, so a caller can leave a previous hit list alone instead of treating
// an empty query as one that matches everything.
bool PropTreeSplitSearchTerms (LPCTSTR pszQuery, std::vector<CString>& terms);

// TRUE when every term of an earlier PropTreeSplitSearchTerms() call appears in
// pszText, as a case insensitive substring. FALSE when there are no terms, which is
// what a caller that skipped the split's return value wants to happen.
bool PropTreeTermsMatch (LPCTSTR pszText, const std::vector<CString>& terms);

////////////////////////////////////////////////////////////////////////////////
// Base class for the "new style" settings pages: their content is a CPropTree
// built in code instead of classic dialog controls.
//
// All such pages share ONE empty dialog template (IDD_PROPTREE_PAGE, see
// PropTreePage.rc). A page owns no designed controls, so there is nothing to
// keep in a per-page template, and adding a new page needs no .rc/.rh entry.
//
// What a page supplies instead, in its constructor:
//   - the title: CTreePropSheet splits it on "::" to build the tree of pages,
//     hence the "Parent::Child" notation ("Editor::Editing"). It is stored in
//     CPropertyPage::m_strCaption, which outlives the sheet.
//   - the help id: MFC derives it from the integer template id, which is shared
//     now, so it has to be assigned explicitly. Keep the page's historical
//     IDD_xxx symbol in the resource header even though no template uses it:
//     Help/HTMLDefines.h maps HID_BASE_RESOURCE + id to a help topic.
//
// What the base class does: creates and lays out the tree, marks the page
// modified on item changes, commits the cell being edited before values are
// read on Apply, and repaints the tree including its in-place child controls.
// A derived page implements BuildTree() / ApplyValues() and, optionally,
// SaveTreeState() / OnItemChanged().
//
// See SQLTools/md/proptree_settings_page_conversion.md.
////////////////////////////////////////////////////////////////////////////////
class CPropTreePage : public CPropertyPage
{
public:
	CPropTreePage (LPCTSTR pszTitle, UINT nIDHelp);
	virtual ~CPropTreePage ();

	// Enables/disables the sheet's Help button while this page is active. Turn
	// it off for a page that has no help topic yet (no [ALIAS] entry in
	// Help/sqltools.hhp), otherwise Help reports "topic not found".
	void SetPageHelp (BOOL bEnable);

	// ---- settings search ----
	//
	// The primitives a sheet uses to search its pages. They live here and not in
	// CPropertySheetMem so that every page inherits them: one sheet level F3 handler
	// then works for all of them, and a page that builds its rows in an unusual way
	// can override CollectMatches() without the sheet having to know about it.

	// The tree control. A sheet needs it to resolve a stored control id back to a
	// row - CPropTree::FindItem(UINT) - once a page may have rebuilt its rows.
	CPropTree& GetPropTree () { return m_tree; }

	// The page title, "Parent::Child" as the constructor took it. A sheet matches
	// titles itself, so that a classic page with no tree at all is still reachable
	// by name.
	LPCTSTR GetPageTitle () const { return m_strCaption; }

	// TRUE once OnInitDialog() has created the tree and BuildTree() has filled it.
	// MFC creates a property page's window only when that page is first shown, so a
	// page the user has not visited yet holds no rows and has nothing to search -
	// a sheet has to force page creation before it searches.
	BOOL IsTreeBuilt () const { return m_tree.GetSafeHwnd() != NULL; }

	// Appends every row of this page that matches pszQuery to hits, in tree order,
	// and returns how many it added. pszQuery is split on whitespace into terms that
	// are ANDed, and matching is a case insensitive substring test, so a short term
	// works and no index has to be kept in step with a tree that pages rebuild while
	// the sheet is open. dwFields combines PropTreeSearchField.
	//
	// The page title is deliberately NOT part of a row's searchable text: it is the
	// same for every row on the page, so including it would turn a search for the
	// page name into one hit per row. PSF_PATH contributes the group labels only.
	//
	// A group row is matched on its label and its path, never on its description, so a
	// group whose blurb merely mentions a word cannot outrank the rows that hold it.
	int CollectMatches (LPCTSTR pszQuery, DWORD dwFields,
	                    std::vector<PropTreeSearchHit>& hits);

	// Focuses, selects and scrolls pItem into view and repaints, so the info pane
	// shows the matched row's description. FALSE when there is nothing to reveal.
	//
	// The caller must make this page the sheet's active page FIRST. Revealing a row
	// on a page that is created but hidden only scrolls a window nobody can see, and
	// EnsureVisible() takes its scroll rectangle from the list's client rect - empty
	// for a page that has never been laid out, so nothing would scroll at all.
	BOOL RevealItem (CPropTreeItem* pItem);

protected:
	// Page content; called from OnInitDialog() once the tree has been created.
	virtual void BuildTree () = 0;

	// Writes the tree values into the settings. Called from OnApply() after the
	// cell being edited has been committed, so item values can be read as-is.
	// Return FALSE to keep the sheet open.
	virtual BOOL ApplyValues () { return TRUE; }

	// Remembers the group expansion state in statics; called from OnApply() and
	// OnCancel(). Guard against a page that was never built (null items).
	virtual void SaveTreeState () {}

	// PTN_ITEMCHANGED / PTN_CHECKCLICK for pItem; the base already marked the
	// page modified. Override to repaint the rows that depend on the new value.
	virtual void OnItemChanged (CPropTreeItem* pItem) {}

	// Tree geometry.
	// GetTreeColumn() is the label/attribute splitter position (CPropTree::SetColumn)
	// in pixels. 0 - the default - means "two thirds of the tree client width" and is
	// resolved in OnInitDialog(), once the tree has been created: a proportional
	// split follows the size of the shared template and the system font/DPI instead
	// of being a hard-coded pixel value. A page that wants a fixed label column
	// overrides this and returns a positive pixel value.
	virtual int GetTreeColumn () const { return 0; }
	virtual int GetTreeInfoHeight () const { return 72; }

	// GetTreeGutterLevels() is how many indent levels the gutter - the gray band down the left
	// edge of the tree - spans. One level, the default, is what the control ships with and holds
	// the chevron of a root level group. A page whose rows nest deeper than that returns more, so
	// a chevron that has stepped in with its row still sits on the band rather than on the white
	// beside it. Passed to CPropTree::SetGutterLevels() in OnInitDialog().
	virtual int GetTreeGutterLevels () const { return 1; }

	// GetTreeIndentsRootChildren() is whether the rows below a top level group step in by one
	// level. FALSE, the default, is what the control ships with and what a two level page wants:
	// the group's gray shading is the hierarchy cue and its rows line up under it. A page that
	// nests a third level returns TRUE, so each level's chevron and label show its depth - and
	// wants GetTreeGutterLevels() raised to match, or the stepped in chevron lands beside the
	// gray band instead of on it. Passed to CPropTree::SetIndentRootChildren().
	virtual BOOL GetTreeIndentsRootChildren () const { return FALSE; }

	// GetOneGroupOpenAtATime() turns the group list into an accordion: opening a group
	// closes the groups beside it. FALSE, the default, is the plain CPropTree behaviour,
	// every group open or closed on its own. A page whose groups are data driven - one per
	// category, one per window, one per attribute set - returns TRUE, because several of
	// them open at once are taller than the tree pane of the sheet and the group being
	// edited is the only one worth reading. OnItemExpanding() calls CloseOtherGroups() for
	// the item about to open.
	//
	// This lives here and not in the pages that use it: CVisualAttributesPageTree had it
	// first and COEClassPageTree is the second page that needs it, and the conversion
	// recipe (md/proptree_settings_page_conversion.md) says a helper a second page needs
	// moves to Common rather than gets copied.
	virtual BOOL GetOneGroupOpenAtATime () const { return FALSE; }

	// Collapses every sibling of pOpening - every other child of the parent it shares with
	// it - then clamps the scroll offset of the list, which the shorter tree may have left
	// past its end. Walking the parent rather than a page's own list of top level groups is
	// what extends the accordion to every nesting level: opening a group closes the other
	// groups of its level, and a sub-group closes the other sub-groups beside it.
	void CloseOtherGroups (CPropTreeItem* pOpening);

	// Repaints the whole page including the tree's in-place children (edit,
	// combo, spin). RDW_ALLCHILDREN is what makes a value change in one row
	// visibly update a dependent row.
	void RedrawTree ();

	CPropTree m_tree;

	virtual BOOL OnInitDialog ();
	virtual BOOL OnApply ();
	virtual void OnCancel ();
	virtual void DoDataExchange (CDataExchange* pDX);

	afx_msg void OnPropTreeNotify (NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnItemExpanding (NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg void OnSize (UINT nType, int cx, int cy);

	DECLARE_MESSAGE_MAP()
};

}; // namespace Common

#endif//__PropTreePage_h__
