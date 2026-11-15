#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __PropTreeSearchBar_h__
#define __PropTreeSearchBar_h__

namespace Common {

////////////////////////////////////////////////////////////////////////////////
// What the bar tells its parent. Sent in wParam of WM_SQLT_SEARCHBAR.
//
// One message with a code in wParam rather than a message per verb, because the sheet
// handles them in one place and the codes read as a list of the bar's verbs.
enum PropTreeSearchBarNotify
{
	// The user stopped typing and the text differs from the last report. Not sent
	// on every WM_CHAR: see the debounce timer in the implementation.
	PSBN_TEXTCHANGED = 1,

	// Walk one hit forward (Enter, Down, the down arrow) ...
	PSBN_NEXT        = 2,

	// ... and back (Shift+Enter, Up, the up arrow).
	PSBN_PREV        = 3,

	// The field was emptied by Esc, so the sheet can drop its query and counter
	// without treating "no text" as a search that found nothing.
	PSBN_CLEARED     = 4,

	// A settings-menu checkbox moved, so what a query matches moved with it. The sheet
	// reads the whole set back through the GetInclude*() accessors and re-runs the
	// search, exactly as it would for a query the user had just edited. One code for
	// the menu rather than one per item: the handler wants the resulting set, not which
	// tick moved, and a menu reports one item at a time in any case.
	PSBN_SETTINGS    = 5,
};

// Bar -> parent. wParam is a PropTreeSearchBarNotify, lParam is unused.
//
// WM_APP + 0x22 is the next free slot on a property sheet: WM_APP + 0x21 is
// WM_SQLT_SEARCH_FIRST_HIT in PropertySheetMem.cpp, and the WM_APP + 41/42 that
// pages use go to page windows, never to the sheet.
const UINT WM_SQLT_SEARCHBAR = WM_APP + 0x22;

////////////////////////////////////////////////////////////////////////////////
// The search field that sits above the page tree of a tree-mode settings sheet.
//
// A custom CWnd rather than a dialog template, because there is no room in these
// sheets for one: the bar goes into a strip carved off the top of the page tree's
// column, which is 180-340px wide depending on the sheet, and a template could
// not be resized to follow it. Everything except the text is painted - the frame,
// the magnifier, the counter and the two arrows - so the whole thing is one
// child window plus one edit, and the layout adapts to whatever width it is given.
//
// It is deliberately dumb: it owns the text and the counter, and nothing else.
// What a search matches, which page a hit is on and how a hit is revealed stay in
// CPropertySheetMem, which drives the bar with SetCounter()/SetNoMatch() and reads
// it back with GetText().
//
// Parent contract: the parent must handle WM_SQLT_SEARCHBAR. The bar never
// searches anything itself.
class CPropTreeSearchBar : public CWnd
{
public:
	CPropTreeSearchBar ();
	virtual ~CPropTreeSearchBar ();

	// The height the bar wants, given the font it will be drawn with. Static and
	// font-taking because the caller has to know the height BEFORE Create() - it
	// is the height of the strip the page tree gets moved down by.
	static int GetPreferredHeight (CFont* pFont);

	// pszCueBanner is the grayed hint shown in an empty field, "Search settings".
	// Ignored, and the field simply shows no hint, on a comctl32 too old for
	// EM_SETCUEBANNER.
	BOOL Create (const CRect& rc, CWnd* pParentWnd, LPCTSTR pszCueBanner);

	// ---- text ----
	CString GetText () const;
	void    SetText (LPCTSTR pszText);

	// Focuses the edit and selects what is in it, so Ctrl+F on a sheet that has
	// already been searched types over the old query instead of appending to it.
	void FocusAndSelect ();

	// TRUE while the caret is in this bar's edit. CPropertySheetMem asks before a
	// page switch and gives the focus back afterwards, because switching pages
	// ends with CTreePropSheet::OnPageTreeSelChanging() focusing the page tree.
	BOOL HasFieldFocus () const;

	// Puts the caret back without touching the selection - the restore that
	// follows a page switch, as opposed to FocusAndSelect() above.
	void RestoreFocus ();

	// ---- settings ----
	// What the menu's checkboxes say a search should match beyond the labels and group
	// names it always matches: descriptions, the rows' current values, and every entry
	// of a dropdown rather than only the one picked.
	//
	// The bar holds the ticks and reports a change as PSBN_SETTINGS; it does not decide
	// what a search matches. That belongs to CPropertySheetMem, as PSF_INFO, PSF_VALUE
	// and PSF_CHOICES in the field set EnableSearch() was given, and the sheet seeds the
	// bar from that set when it builds it - so a value handed to the sheet before
	// DoModal() still reaches a bar created later, in OnInitDialog().
	//
	// All three default on here, which matches the profile defaults in
	// CPropertySheetMem::LoadSearchSettings() and not PSF_DEFAULT - that leaves values off,
	// because a settings page is mostly numbers and colours and matching them turns a search
	// for "10" into a hit on every row that happens to hold one. The mask speaks for a caller
	// with no opinion of its own; a preference in the profile speaks for a user, and by the
	// time one is being read "which setting is 10" has been asked. Either way these three
	// members only ever hold for the moment before the sheet seeds them from its own field
	// set, which CreateSearchBar() does immediately after Create().
	//
	// Choices are on by default and get a tick anyway, and the reason is worth keeping:
	// with no way to turn them off, a row whose dropdown merely lists the word is a hit
	// the user cannot get rid of, and unticking everything else does not help. That is
	// precisely the surprise this menu exists to remove, so the menu has to cover every
	// field that can produce it - PSF_PATH is the one exception, and it stays out because
	// a row that matches only through its path is dropped again by the bPath && !bSelf
	// rule in EnumCollectMatches(), so it cannot add a hit of its own.
	BOOL GetIncludeDescriptions () const;
	void SetIncludeDescriptions (BOOL bInclude);

	BOOL GetIncludeValues () const;
	void SetIncludeValues (BOOL bInclude);

	BOOL GetIncludeChoices () const;
	void SetIncludeChoices (BOOL bInclude);

	// ---- counter ----
	// nCurrent is the 1-based index of the hit on screen. nCurrent == 0 with
	// nTotal == 0 is the "nothing typed yet" state, which draws no counter at all.
	void SetCounter (int nCurrent, int nTotal);

	// TRUE paints the counter in the no-match colour. Kept apart from the numbers
	// because "0 of 0" before anyone types is not a failure and must not look like
	// one.
	void SetNoMatch (BOOL bNoMatch);

protected:
	// Where everything goes, recomputed on every paint and every hit test so that
	// one width serves both. bShowCounter is FALSE when the counter would squeeze
	// the edit below a usable width, which is what a 180px column and a wide
	// "1234/5678" would otherwise do.
	struct Layout
	{
		CRect rcField;
		CRect rcIcon;
		CRect rcEdit;
		CRect rcCounter;
		CRect rcUp;
		CRect rcDown;

		// The search-settings button. Always laid out, unlike the counter: what a
		// search matches is worth choosing before the first search rather than only
		// once one is running.
		CRect rcSettings;

		BOOL  bShowCounter;
	};

	Layout ComputeLayout () const;

	// Which button of the strip the mouse is over or holding, for the hover and
	// pressed backgrounds. BTN_NONE when it is over none of them.
	//
	// BTN_ names the strip rather than only the arrows. The settings button joins
	// the enum instead of growing a parallel pair of "is it hot / is it down" flags
	// beside m_nBtnHot, because it shares all of that machinery - the same hover
	// tracking, the same capture-on-press, the same background fill - and a second set
	// of states would be a second thing to keep in step with the first.
	enum { BTN_NONE = 0, BTN_UP = 1, BTN_DOWN = 2, BTN_SETTINGS = 3 };

	// Same test the paint uses, so a click cannot land on an arrow that was not
	// drawn where the click thinks it is.
	int HitButton (const CPoint& pt) const;

	void Notify (UINT nCode);

	// Starts the debounce timer, called on every EN_CHANGE. The timer, not the
	// notification, is what reaches the parent.
	void RestartDebounce ();

	void PaintField  (CDC& dc, const Layout& lay);
	void PaintIcon   (CDC& dc, const CRect& rc);
	void PaintCounter(CDC& dc, const CRect& rc);
	void PaintArrow  (CDC& dc, const CRect& rc, BOOL bUp, BOOL bEnabled);

	// The search-settings button, and the menu it drops. PaintButtonBg() is the hover
	// and pressed fill that both it and PaintArrow() need, factored out so the two
	// families of button cannot drift apart in how they light up.
	void PaintButtonBg (CDC& dc, const CRect& rc, int nWhich, BOOL bEnabled);
	void PaintSettings (CDC& dc, const CRect& rc);
	void ShowSettingsMenu ();

	CFont   m_font;         // copy of the parent's font, so the bar can be measured
	                        // before it exists and painted after the parent is gone
	CBrush  m_brushBack;    // the sheet's background, outside the field
	CBrush  m_brushField;   // the field's fill, also handed to the edit

	int     m_nCurrent;
	int     m_nTotal;
	BOOL    m_bNoMatch;

	// The settings menu's checkboxes. Copies of what the sheet was told, kept here only
	// so the ticks can be painted without asking the parent: PSF_INFO, PSF_VALUE and
	// PSF_CHOICES in the sheet's field set are the values a search actually reads, and
	// the setters are what keep the two in step. Nothing here persists them - the profile
	// writes sit with the dialog that owns the settings, in
	// COEDocument::ShowSettingsDialog().
	BOOL    m_bIncludeDesc;
	BOOL    m_bIncludeValues;
	BOOL    m_bIncludeChoices;

	BOOL    m_bFieldFocus;  // EN_SETFOCUS / EN_KILLFOCUS from the edit
	BOOL    m_bTracking;    // a TME_LEAVE is outstanding
	int     m_nBtnHot;      // BTN_* under the mouse
	int     m_nBtnDown;     // BTN_* being held

	CEdit   m_edit;

	// Metrics, in one place because both the paint and the hit test read them and
	// a second copy would be a second thing to keep in step. All of them go
	// through ScaleMetric() rather than being used raw.
	enum
	{
		IDC_BAR_EDIT   = 4101,   // private to the bar, so no resource.h entry
		IDM_INCLUDE_DESC    = 4102, // the settings menu's items, private for the same reason
		IDM_INCLUDE_VALUES  = 4103,
		IDM_INCLUDE_CHOICES = 4104,
		TIMER_DEBOUNCE = 1,

		DEBOUNCE_MS    = 250,    // typing pause that counts as "done typing"

		PAD_TOP        = 3,      // sheet background above and below the field
		PAD_FIELD      = 6,      // field edge to the magnifier
		ICON_SIZE      = 14,
		ICON_GAP       = 6,      // magnifier to the edit
		BTN_SIZE       = 17,     // one arrow button, square
		BTN_GAP        = 2,
		SETTINGS_SIZE  = 17,     // the search-settings button, square like an arrow
		SETTINGS_GAP   = 6,      // arrows to settings: wider than BTN_GAP because it
		                         // separates two groups, not two buttons of one pair
		EDIT_MIN       = 52,     // below this the counter goes, not the edit
		COUNTER_GAP    = 6,
		CORNER         = 4,      // field corner radius
	};

	// Scales a 96dpi metric to the scale the bar is drawn at. These sheets are
	// fixed size and the app has no dpi helper of its own, so the bar takes its
	// scale from the screen's logical DPI instead: at 150% every pixel constant
	// here has to grow with the font, or the field clips its own text.
	//
	// Static because it reads nothing from the instance, and GetPreferredHeight()
	// has to work before one exists - the caller needs the height in order to know
	// how big a strip to build a bar in.
	static int ScaleMetric (int nAt96dpi);

protected:
	virtual BOOL PreCreateWindow (CREATESTRUCT& cs);

	// Claimed here and not in the sheet, because these keys have to be handled
	// before the dialog's IsDialogMessage() sees them: Enter would press the
	// default button and Esc would close the sheet. MFC's PreTranslateMessage()
	// walk goes from the focused window up through its parents, and the edit's
	// parent is this bar, so it runs before both.
	virtual BOOL PreTranslateMessage (MSG* pMsg);

	afx_msg void OnDestroy ();
	afx_msg void OnPaint ();
	afx_msg BOOL OnEraseBkgnd (CDC* pDC);
	afx_msg void OnSize (UINT nType, int cx, int cy);
	afx_msg void OnTimer (UINT_PTR nIDEvent);

	afx_msg void OnMouseMove (UINT nFlags, CPoint point);
	afx_msg void OnMouseLeave ();
	afx_msg void OnLButtonDown (UINT nFlags, CPoint point);
	afx_msg void OnLButtonUp (UINT nFlags, CPoint point);

	// WM_COMMAND from the settings menu. ShowSettingsMenu() tracks it against this
	// window, so the commands come back here rather than to the sheet. One handler per
	// item because each toggles a different member, but both end in the same
	// Notify(PSBN_SETTINGS) - what the sheet wants is the resulting set, not the item.
	afx_msg void OnMenuIncludeDesc ();
	afx_msg void OnMenuIncludeValues ();
	afx_msg void OnMenuIncludeChoices ();

	// EN_* from the edit. EN_SETFOCUS/EN_KILLFOCUS are what let the bar draw the
	// focused border without subclassing the edit to see its WM_SETFOCUS.
	afx_msg void OnEditChange ();
	afx_msg void OnEditSetFocus ();
	afx_msg void OnEditKillFocus ();

	// The edit has to paint on the field's fill, not on the sheet's background, or
	// the white rounded rect would stop dead at the edit's left edge.
	afx_msg HBRUSH OnCtlColor (CDC* pDC, CWnd* pWnd, UINT nCtlColor);

	DECLARE_MESSAGE_MAP ()
};

}; // namespace Common

#endif//__PropTreeSearchBar_h__
