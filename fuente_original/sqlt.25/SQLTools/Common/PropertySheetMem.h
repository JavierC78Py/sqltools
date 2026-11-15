/*
    Copyright (C) 2004 Aleksey Kochetov

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

#pragma once
#ifndef __PropertySheetMem_h__
#define __PropertySheetMem_h__

#include <CTreePropSheet/TreePropSheet.h>

// (ak) PropTreeSearchField and the term helpers, so that EnableSearch() can default
// to PSF_DEFAULT and RunSearch() can match a page title by the very same rule
// CPropTreePage::CollectMatches() matches a row.
#include "Common/PropTreePage.h"

// (ak) CPropTreeSearchBar, the field this sheet builds above its page tree when it
// is in tree view mode. Held by value: it lives exactly as long as the sheet does
// and is destroyed with it, so there is nothing to own and nothing to leak.
#include "Common/PropTreeSearchBar.h"

#include <vector>

namespace Common {

class CPropertySheetMem : public TreePropSheet::CTreePropSheet
{
    UINT& m_selectPage;
public:
    CPropertySheetMem(LPCTSTR pszCaption, UINT& selectPage, CWnd* pParentWnd = NULL);

    virtual CTreeCtrl* CreatePageTreeObject ();

    // (ak) puts the page tree on the Explorer theme class, so comctl32 draws chevron expand
    // glyphs instead of [+]/[-] boxes. Tab-mode sheets have no tree and are left alone.
    virtual BOOL OnInitDialog();

    // ---- settings search ----

    // (ak) Turns on search for this sheet. Call it before DoModal().
    //
    // Sheets opt in one at a time rather than all inheriting the keys, because the
    // sheets that are not settings dialogs - DB source, TreeViewer, CodeOutline -
    // have no rows to search and F3 already means something to their content.
    //
    // What "search" means depends on the mode the sheet is in when it opens, and
    // EnableSearch() cannot know that yet - callers do SetTreeViewMode() either side
    // of it:
    //
    //   tree mode  a Common::CPropTreeSearchBar is built above the page tree in
    //              OnInitDialog(). Ctrl+F puts the caret in it, typing searches as
    //              the user pauses, Enter / F3 / the down arrow step to the next
    //              hit and Shift+Enter / Shift+F3 / the up arrow to the previous.
    //
    //   tab mode   no bar, because there is no tree column to put one above. Ctrl+F
    //              and F3 fall back to the Common::CInputDlg prompt and the walk it
    //              starts, which is how this worked before the bar existed.
    //
    // dwFields combines PropTreeSearchField and says what a row is searched by - a
    // starting point, because the user's own preferences are applied over it next.
    // PSF_DEFAULT leaves PSF_VALUE off: a settings page is mostly numbers and colours,
    // and matching those turns a search for "10" into a hit on every row that happens to
    // hold a 10. A label, a description or a group name is what a setting is actually
    // remembered by. The profile defaults the other way, values on, and that is not a
    // contradiction - the mask speaks for a caller with no opinion, the profile for a user
    // who has one. See LoadSearchSettings().
    //
    // The three optional bits - PSF_INFO, PSF_VALUE and PSF_CHOICES - are overwritten from
    // the profile there, and OnDestroy() writes them back on the way out. That is what
    // makes the field list a starting point rather than the answer: every sheet that enables
    // search shares one set of preferences, so unticking "Including descriptions" in the
    // settings dialog leaves it unticked in the DDL dialog too. A caller that wants a fixed
    // field set whatever the user last chose calls the SetSearchInclude*() setters after
    // EnableSearch().
    //
    // pszSearchCue is the hint the bar shows in an empty field. NULL takes the
    // default, "Search settings". Ignored in tab mode, which has no field to show
    // it in.
    void EnableSearch (DWORD dwFields = PSF_DEFAULT, LPCTSTR pszSearchCue = NULL);

    // Which of the optional fields a search matches - descriptions (PSF_INFO), values
    // (PSF_VALUE) and dropdown entries (PSF_CHOICES) - on top of the labels and group
    // names it always matches. Split out from EnableSearch() rather than folded into its
    // dwFields because these are the fields the user can change while the sheet is open,
    // from the search bar's settings menu, and because they outlive the sheet: they are
    // the three bits LoadSearchSettings() and SaveSearchSettings() persist.
    //
    // A setter pushes its value into the bar as well, so a tick and the field set cannot
    // disagree; before the bar exists there is nothing to push to, and CreateSearchBar()
    // seeds all three from m_dwSearchFields when it runs. Safe to call either side of
    // DoModal(), and each getter reads the field set back afterwards.
    //
    // One pair per field rather than a single call taking a mask, because each has its own
    // profile entry and its own default - descriptions and entries start on, values off,
    // which is the split PSF_DEFAULT already draws - and a mask argument would push both
    // of those onto every caller.
    void SetSearchIncludeDescriptions (BOOL bInclude);
    BOOL GetSearchIncludeDescriptions () const;

    void SetSearchIncludeValues (BOOL bInclude);
    BOOL GetSearchIncludeValues () const;

    void SetSearchIncludeChoices (BOOL bInclude);
    BOOL GetSearchIncludeChoices () const;

    // (ak) Ctrl+F, F3 and Shift+F3. See the implementation for why these have to be
    // claimed here rather than left to fall through, and for why the bar's own keys
    // - Enter, Esc, Up, Down - are claimed in CPropTreeSearchBar instead.
    virtual BOOL PreTranslateMessage(MSG* pMsg);

protected:
    // (ak) One thing the query matched: a row of a tree page, or a page as a whole.
    //
    // (nPage, nCtrlID) and NOT a CPropTreeItem* is the durable identity, for the
    // reason PropTreeSearchHit gives: CVisualAttributesPageTree deletes and recreates
    // its sample rows when the sample text changes, so a pointer kept between two
    // F3s can dangle and the next one would walk freed memory. nCtrlID == 0 means
    // "the page itself" - what a title match produces, and the only kind of hit a
    // classic page with no tree can produce.
    struct SheetSearchHit
    {
        SheetSearchHit (int nP = -1, UINT nID = 0) : nPage(nP), nCtrlID(nID) {}

        int  nPage;
        UINT nCtrlID;
    };

    // Builds the search field above the page tree, and moves the tree down to make
    // room for it. Called from OnInitDialog() once CTreePropSheet has created the
    // tree, and only in tree view mode - see EnableSearch() for what a tabbed sheet
    // does instead. Leaves m_bSearchBar FALSE on any failure, and the sheet then
    // simply behaves as it did before the bar existed.
    void CreateSearchBar ();

    // Asks for the query with Common::CInputDlg and jumps to its first hit. The
    // tab-mode path: a sheet with a search bar reads the query from the bar and
    // never opens this.
    BOOL PromptForQuery ();

    // Creates the window of every page of the sheet, at most once, so that a search
    // reaches pages the user has never clicked. See the implementation for why the
    // active page and the Apply state have to be put back afterwards.
    void EnsurePagesCreated ();

    // Refills m_hits from the live pages and returns how many hits it found.
    int  RunSearch ();

    // Moves to the next (nDelta > 0) or previous (nDelta < 0) hit, wrapping at both
    // ends, after re-running the search so the list is current. Ends by telling the
    // search bar where the walk now stands.
    //
    // bQuiet is TRUE when the answer can go in the bar instead of in a box, and it
    // silences everything that would interrupt the user mid-keystroke: the modal
    // "not found" box, which would otherwise pop up over the sheet on every pause in
    // typing a query that has not matched anything yet, and the CInputDlg prompt an
    // empty query would otherwise open. In place of both, the bar's own counter says
    // "0/0" in red - the same information, without taking the keyboard away.
    //
    // bBeepIfNone adds a sound to that red counter, and is consulted on the quiet path
    // only - the box the other path puts up already makes one. It is for the steps a
    // user asks for deliberately, Enter and F3 and Shift+F3 and the two arrows, where
    // they are waiting for the sheet to move and "0/0" in a corner is easy to miss
    // with their eyes on the tree. Not passed by the as-you-type path, where a query
    // matches nothing at almost every pause in being typed and a beep at each one
    // would make the bar unbearable, nor by a settings tick, where the user has just
    // clicked a menu item and is looking at the bar already.
    void GotoHit (int nDelta, BOOL bQuiet, BOOL bBeepIfNone = FALSE);

    // Forgets where the walk stood, so the next GotoHit() starts from the first hit
    // rather than stepping on from the previous query's last one.
    void ResetSearchWalk ();

    // Pushes the query and the walk's position into the bar. Called after every step
    // and every search, and a no-op when there is no bar.
    void UpdateSearchBar (int nCurrent, int nTotal, BOOL bNoMatch);

    // Activates the hit's page and, for a row hit, reveals the row on it.
    void ShowHit (const SheetSearchHit& hit);

    void ReportNotFound ();

    // The caption of page nPage, "Parent::Child". CPropTreePage keeps its own; a
    // classic page's lives only in the protected CPropertyPage::m_strCaption, so that
    // one is read back from the tab control - the same TCITEM CTreePropSheet takes
    // the captions it builds its tree from, and one that is still populated in tree
    // view mode, where the tab control is merely hidden.
    CString GetPageCaption (int nPage) const;

    // The three optional field bits in the profile, under their own [PropertySheetSearch]
    // section, all three defaulting to on. Read by
    // EnableSearch() over the caller's mask, written by OnDestroy() from the field set -
    // which is during EndDialog()'s DestroyWindow(), so the save covers both ways out of
    // DoModal(). That is deliberate: the ticks say how the user wants to search, not one
    // of the settings any particular dialog edits, so cancelling still remembers them.
    //
    // Here rather than in whichever dialog owns the sheet, because the preferences are
    // about searching a settings tree and every sheet has one. Left to the callers it
    // meant each dialog remembered its own, and the DDL sheet - whose caller never read
    // or wrote them - had three ticks that worked live and were forgotten the moment it
    // closed, so it always reopened on PSF_DEFAULT whatever the user had chosen.
    void LoadSearchSettings ();
    void SaveSearchSettings () const;

    BOOL    m_bSearchEnabled;
    DWORD   m_dwSearchFields;
    CString m_strQuery;

    // The field above the page tree, and whether it was actually built. m_bSearchBar
    // and not m_searchBar.GetSafeHwnd() is what the rest of the sheet tests, because
    // a bar that failed to create must fall back to the CInputDlg prompt rather than
    // leave Ctrl+F doing nothing at all.
    CPropTreeSearchBar m_searchBar;
    BOOL    m_bSearchBar;

    // The hint shown in an empty field. Kept from EnableSearch() to CreateSearchBar(),
    // which runs later and is the only place that can pass it on.
    CString m_strSearchCue;

    std::vector<SheetSearchHit> m_hits;
    int  m_nLastPage;       // -1 while no hit has been shown yet
    UINT m_nLastCtrlID;

    // TRUE while a search is running. SetActivePage() pumps messages - the comment on
    // WM_SQLT_SEARCH_FIRST_HIT explains exactly how - so a debounce timer that comes
    // due during a search, or a second F3 pressed while one is still walking pages,
    // would otherwise start another search on top of the first and the two would
    // fight over which page the sheet ends up on.
    BOOL m_bSearching;

    BOOL m_bPagesForced;    // EnsurePagesCreated() has run

    // TRUE only while the Ctrl+F prompt is up. MFC's CWinThread::PreTranslateMessage()
    // walks from the focused window up through GetParent(), and a modal dialog's parent
    // is its owner - this sheet - so without the flag a second Ctrl+F typed into the
    // prompt would open another prompt on top of it, and an F3 would run a search
    // against a sheet that is currently disabled.
    BOOL m_bPromptOpen;

protected:
	DECLARE_MESSAGE_MAP()
    afx_msg void OnDestroy();

    // Runs the first jump of a new query. Posted by PromptForQuery() rather than called
    // from it; see WM_SQLT_SEARCH_FIRST_HIT in the implementation for why.
    afx_msg LRESULT OnSearchFirstHit (WPARAM wParam, LPARAM lParam);

    // The other end of CPropTreeSearchBar::Notify(). wParam is a PropTreeSearchBarNotify:
    // the user paused typing, stepped forward, stepped back, or cleared the field.
    afx_msg LRESULT OnSearchBar (WPARAM wParam, LPARAM lParam);
};

}; // namespace Common

#endif//__PropertySheetMem_h__