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

#include "stdafx.h"
#include "Common/PropertySheetMem.h"

// (ak) CXPTheme::SetWindowTheme, for the page tree's expand glyph - see OnInitDialog()
#include "ThirdParty/XPTheme.h"

// (ak) Common::CInputDlg, for the Ctrl+F query prompt
#include "Common/InputDlg.h"

// (ak) ID_APPLY_NOW. Resource.h here pulls in only the project's own .rh files, so
// this is not already on the include path the way it usually is in an MFC app.
#include <afxres.h>

namespace {

// (ak) Tells the sheet to jump to the first hit of a query that was just typed.
//
// Posted, not called. PromptForQuery() runs inside PreTranslateMessage(), straight
// after CInputDlg::DoModal() has ended its nested message loop, torn the prompt down,
// re-enabled the sheet and put focus back on it - and SetActivePage() itself pumps
// messages, because CTreePropSheet routes PSM_SETCURSEL through OnSetCurSel(), which
// re-selects the page tree and so sends TVN_SELCHANGING back through SetActivePage().
// Switching pages in the middle of all of that is what left the sheet on the page the
// search started from: EnsurePagesCreated() had visibly walked every page and put the
// original one back, and the jump to the first hit that followed it did not stick.
//
// A posted message is handled on the next turn of the sheet's own loop, by which time
// the prompt is gone and the sheet has settled, so the round-robin ends on the first
// hit rather than on the page it began on.
const UINT WM_SQLT_SEARCH_FIRST_HIT = WM_APP + 0x21;

// (ak) Holds CPropertySheetMem::m_bSearching for the lifetime of one search, and
// releases it on every way out. GotoHit() returns from three places and runs code
// that pumps messages, so a flag set and cleared by hand would leak on the first
// early return and leave the sheet unable to search again.
class CSearchRunGuard
{
public:
    CSearchRunGuard (BOOL& rbBusy) : m_rbBusy(rbBusy), m_bOurs(!rbBusy)
    {
        if (m_bOurs)
            m_rbBusy = TRUE;
    }

    ~CSearchRunGuard ()
    {
        if (m_bOurs)
            m_rbBusy = FALSE;
    }

    // FALSE when a search was already running, which is the caller's cue to do
    // nothing at all rather than start a second one.
    BOOL Entered () const { return m_bOurs; }

private:
    CSearchRunGuard (const CSearchRunGuard&);             // not copyable: two guards
    CSearchRunGuard& operator = (const CSearchRunGuard&); // on one flag would clear
                                                          // it twice
    BOOL& m_rbBusy;
    BOOL  m_bOurs;
};

} // unnamed namespace

namespace Common {

    class CustomTreeCtrl : public CTreeCtrl {
    protected:
        virtual BOOL PreCreateWindow(CREATESTRUCT& cs) {
            //cs.style |= TVS_SHOWSELALWAYS|TVS_SINGLEEXPAND|TVS_DISABLEDRAGDROP;
            //cs.style &= ~(TVS_TRACKSELECT|TVS_HASLINES|TVS_LINESATROOT|TVS_HASBUTTONS);
            cs.style |= TVS_SHOWSELALWAYS|TVS_DISABLEDRAGDROP|TVS_LINESATROOT|TVS_HASBUTTONS|TVS_FULLROWSELECT;
            // (ak) CTreePropSheet::OnInitDialog() asks for TVS_HASLINES|TVS_LINESATROOT in the
            // style it hands CreateEx(), and TVS_HASLINES is what draws the dotted lines, so it
            // is cleared here rather than in the third-party file.
            //
            // TVS_LINESATROOT has to stay. Its name is about lines, but in comctl32 it is also
            // the flag that gives root items an expand button at all - and the groups this sheet
            // builds are root items, the pages their children. Clear it and every node loses its
            // glyph, which is what the tree looked like when this line cleared both flags.
            // (MSDN's "no effect unless TVS_HASLINES is specified" describes the lines, not the
            // buttons.) TVS_HASBUTTONS stays for the same reason: it is what asks for a glyph on
            // a node that has children. Which glyph that is comes from comctl32 and the window's
            // theme class rather than from us - see OnInitDialog() below.
            //
            // TVS_FULLROWSELECT was already on, and reads better once the lines are gone.
            cs.style &= ~(TVS_TRACKSELECT|TVS_HASLINES);
            cs.dwExStyle |= WS_EX_CLIENTEDGE;
            return CTreeCtrl::PreCreateWindow(cs);
        }
    };

CPropertySheetMem::CPropertySheetMem(LPCTSTR pszCaption, UINT& selectPage, CWnd* pParentWnd)
    : TreePropSheet::CTreePropSheet(pszCaption, pParentWnd, selectPage), m_selectPage(selectPage),
      m_bSearchEnabled(FALSE), m_dwSearchFields(0), m_bSearchBar(FALSE),
      m_nLastPage(-1), m_nLastCtrlID(0), m_bSearching(FALSE),
      m_bPagesForced(FALSE), m_bPromptOpen(FALSE)
{
	SetEmptyPageText(L"Please select a child item of '%s'.");
    SetTreeViewMode(/*bTreeViewMode =*/FALSE, /*bPageCaption =*/FALSE, /*bTreeImages =*/FALSE);
    SetAllExpand(TRUE);
}

CTreeCtrl* CPropertySheetMem::CreatePageTreeObject () 
{ 
    return new CustomTreeCtrl; 
}

// (ak) The expand glyph of a SysTreeView32 is drawn by comctl32 from the window's theme class,
// and there is no style that asks for a particular shape. The default TreeView class still
// draws the classic [+]/[-] box; "Explorer" selects the Explorer::TreeView class, which draws
// chevrons - what Explorer's own navigation pane uses - and double-buffers the painting.
//
// CTreePropSheet::OnInitDialog() is what creates and fills the tree, so this runs after it.
// GetPageTreeControl() returns NULL when the sheet is in tab mode rather than tree view mode -
// the DB source, TreeViewer and CodeOutline sheets all are - hence the guard. And
// CXPTheme::SetWindowTheme resolves uxtheme.dll dynamically, falling back to a stub that
// returns E_FAIL, so a system without themes keeps the box instead of failing here.
BOOL CPropertySheetMem::OnInitDialog()
{
    BOOL bResult = CTreePropSheet::OnInitDialog();

    if (CTreeCtrl* pTree = GetPageTreeControl())
        CXPTheme::SetWindowTheme(pTree->GetSafeHwnd(), L"Explorer", NULL);

    // (ak) The search field goes above the page tree the base call just created, so
    // it cannot be built any earlier than here. A no-op unless EnableSearch() was
    // called and the sheet is in tree view mode - see CreateSearchBar().
    CreateSearchBar();

    return bResult;
}

// CPropertySheetMem
BEGIN_MESSAGE_MAP(CPropertySheetMem, TreePropSheet::CTreePropSheet)
    ON_WM_DESTROY()
    ON_MESSAGE(WM_SQLT_SEARCH_FIRST_HIT, OnSearchFirstHit)
    ON_MESSAGE(WM_SQLT_SEARCHBAR,        OnSearchBar)
END_MESSAGE_MAP()

// CPropertySheetMem message handlers
void CPropertySheetMem::OnDestroy()
{
    m_selectPage = GetActiveIndex();

    // (ak) The search preferences go out with the window. Saving here rather than after
    // DoModal() returns is what covers Cancel as well as OK without the caller having to
    // hold the result and remember to write: EndDialog() destroys the window on both
    // paths. Only for a sheet that enabled search, since the mask of one that did not is
    // zero and would overwrite three good entries with three zeros.
    if (m_bSearchEnabled)
        SaveSearchSettings();

    // (ak) Before the base, which deletes the page tree the bar was laid out against.
    // Done explicitly rather than left to CWnd::~CWnd(): that only detaches, and MFC
    // then warns about a window that reached its destructor still standing.
    if (m_bSearchBar)
    {
        m_bSearchBar = FALSE;

        if (::IsWindow(m_searchBar.GetSafeHwnd()))
            m_searchBar.DestroyWindow();
    }

    CTreePropSheet::OnDestroy();
}

/////////////////////////////////////////////////////////////////////////////
// (ak) Settings search. In tree view mode a CPropTreeSearchBar above the page
// tree holds the query and searches as the user types; in tab mode Ctrl+F asks
// for one with CInputDlg instead. Either way F3 / Shift+F3 cycle the hits.
//
// Matching a row and revealing it are CPropTreePage's job (CollectMatches /
// RevealItem). This is only the sheet-level plumbing around them - which pages to
// scan, in what order, and how to get back onto a hit that was found earlier.
// See SQLTools/md/proptree_search.md.

void CPropertySheetMem::EnableSearch (DWORD dwFields, LPCTSTR pszSearchCue)
{
    m_bSearchEnabled = TRUE;
    m_dwSearchFields = dwFields;

    // Over the caller's mask rather than instead of it: PSF_LABEL and PSF_PATH have no
    // profile entry, neither having a tick, so they stay whatever dwFields said.
    LoadSearchSettings();

    // Kept rather than passed straight on: the bar is not built until OnInitDialog(),
    // long after this call has returned, and the cue is the one thing it needs from
    // here. Empty is resolved to the default in CreateSearchBar() rather than here,
    // so the wording lives next to the field it describes.
    m_strSearchCue = (pszSearchCue != NULL) ? pszSearchCue : L"";
}

void CPropertySheetMem::SetSearchIncludeDescriptions (BOOL bInclude)
{
    // PSF_INFO is the description bit, and m_dwSearchFields is the only place a search
    // reads what it matches - RunSearch() hands it straight to CollectMatches(), which
    // is what decides whether a row's info text joins its haystack. One flag, so the
    // tick in the bar and what the next query matches cannot drift apart.
    if (bInclude)
        m_dwSearchFields |= PSF_INFO;
    else
        m_dwSearchFields &= ~static_cast<DWORD>(PSF_INFO);

    // Pushed rather than left for the bar to ask, because the bar paints its tick from
    // its own member. A no-op while there is no bar, which is the normal case for a call
    // made before DoModal(): CreateSearchBar() seeds it from m_dwSearchFields later.
    if (m_bSearchBar)
        m_searchBar.SetIncludeDescriptions(bInclude);
}

BOOL CPropertySheetMem::GetSearchIncludeDescriptions () const
{
    // Read from the field set rather than from a copy, so it cannot disagree with what
    // the last search actually matched - which is what a caller persisting it wants.
    return (m_dwSearchFields & PSF_INFO) != 0;
}

void CPropertySheetMem::SetSearchIncludeValues (BOOL bInclude)
{
    // PSF_VALUE, and the same single-flag argument as SetSearchIncludeDescriptions()
    // above: the field set is the only thing a search reads, so it is the only thing
    // written. EnumCollectMatches() gates AppendSearchValue() on this bit.
    if (bInclude)
        m_dwSearchFields |= PSF_VALUE;
    else
        m_dwSearchFields &= ~static_cast<DWORD>(PSF_VALUE);

    if (m_bSearchBar)
        m_searchBar.SetIncludeValues(bInclude);
}

BOOL CPropertySheetMem::GetSearchIncludeValues () const
{
    return (m_dwSearchFields & PSF_VALUE) != 0;
}

void CPropertySheetMem::SetSearchIncludeChoices (BOOL bInclude)
{
    // PSF_CHOICES, and the same single-flag shape again. This one closed a gap rather than
    // filled in a spare: PSF_CHOICES is in PSF_DEFAULT, so it was always on, and
    // EnumCollectMatches() hands AppendSearchChoices() every entry of a combo row's list -
    // so a row whose dropdown merely contained the term was a hit, and with no tick for it
    // anywhere there was no way to say otherwise. Unticking the other two did not stop it.
    if (bInclude)
        m_dwSearchFields |= PSF_CHOICES;
    else
        m_dwSearchFields &= ~static_cast<DWORD>(PSF_CHOICES);

    if (m_bSearchBar)
        m_searchBar.SetIncludeChoices(bInclude);
}

BOOL CPropertySheetMem::GetSearchIncludeChoices () const
{
    return (m_dwSearchFields & PSF_CHOICES) != 0;
}

void CPropertySheetMem::LoadSearchSettings ()
{
    // Through the setters and not straight into m_dwSearchFields, so that a sheet already
    // carrying a bar - EnableSearch() is safe to call again after DoModal() has started -
    // has its ticks moved to match. Before the bar exists the push is a no-op and
    // CreateSearchBar() seeds from the field set instead.
    //
    // All three default on - one bit more than the PSF_DEFAULT that EnableSearch() starts
    // from, which leaves PSF_VALUE off. So a profile that has never been written does not
    // leave the caller's mask alone: it lets values in as well. Deliberate. The mask is the
    // starting point for a caller with no opinion of its own, and by the time a preference
    // is being read there is a user with one - "which setting is 10" is as real a question
    // as "which setting is named 10". The section is its own rather than [Settings], which
    // is the caption several sheets are constructed with and would mix these three keys in
    // with whatever else that name comes to mean.
    SetSearchIncludeDescriptions(
        AfxGetApp()->GetProfileInt(L"PropertySheetSearch", L"SearchIncludeDescriptions", TRUE) != 0);
    SetSearchIncludeValues(
        AfxGetApp()->GetProfileInt(L"PropertySheetSearch", L"SearchIncludeValues", TRUE) != 0);
    SetSearchIncludeChoices(
        AfxGetApp()->GetProfileInt(L"PropertySheetSearch", L"SearchIncludeChoices", TRUE) != 0);
}

void CPropertySheetMem::SaveSearchSettings () const
{
    // Read back off the field set through the getters rather than off the bar: the field
    // set is what the last search actually matched, and the setters are the only way into
    // it, so the two cannot have drifted. It also means this works for a sheet with no bar
    // at all - a tab-mode one, say - whose mask came from the caller and the profile.
    AfxGetApp()->WriteProfileInt(L"PropertySheetSearch", L"SearchIncludeDescriptions",
                                 GetSearchIncludeDescriptions());
    AfxGetApp()->WriteProfileInt(L"PropertySheetSearch", L"SearchIncludeValues",
                                 GetSearchIncludeValues());
    AfxGetApp()->WriteProfileInt(L"PropertySheetSearch", L"SearchIncludeChoices",
                                 GetSearchIncludeChoices());
}

BOOL CPropertySheetMem::PreTranslateMessage (MSG* pMsg)
{
    // Claimed here, BEFORE CPropertySheet::PreTranslateMessage(), and that ordering
    // is the whole point. The sheet is modal, so MFC's
    // CWinThread::PreTranslateMessage() walks from the focused window up to the main
    // frame calling PreTranslateMessage() on each one; returning TRUE stops that walk
    // at the sheet, so F3 never reaches the main frame and its ID_EDIT_FIND_NEXT
    // accelerator. Left to fall through, Find-next pressed in a settings dialog would
    // search the editor sitting behind it. IsDialogMessage() does not claim F3.
    //
    // The walk also means this runs whatever has focus - a page, the PropTree, or an
    // in-place edit inside a row - so the keys work everywhere in the sheet.
    if (m_bSearchEnabled && !m_bPromptOpen && pMsg->message == WM_KEYDOWN)
    {
        const BOOL bCtrl  = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const BOOL bShift = (::GetKeyState(VK_SHIFT)   & 0x8000) != 0;
        const BOOL bAlt   = (::GetKeyState(VK_MENU)    & 0x8000) != 0;

        // Alt is left out of both, so Alt+F3 and the like keep their meaning.
        //
        // F3 steps the walk, and is quiet when there is a bar: the bar's counter says
        // what a modal box would, without taking the keyboard away from the field the
        // user is standing in. A beep goes with that counter, since it is the only
        // answer left when there is nothing to step to.
        if (!bAlt && !bCtrl && pMsg->wParam == VK_F3)
        {
            // With nothing searched for yet, F3 means "go and search for something" -
            // the caret in the field when the sheet has one, the CInputDlg prompt when
            // it does not.
            if (m_bSearchBar && m_strQuery.IsEmpty())
                m_searchBar.FocusAndSelect();
            else
                GotoHit(bShift ? -1 : +1, /*bQuiet =*/m_bSearchBar,
                        /*bBeepIfNone =*/m_bSearchBar);

            return TRUE;
        }

        // Ctrl+F is the way in, and what "in" means depends on the sheet's mode: the
        // caret in the bar when the page tree gave us a column to build one in, the
        // app's one-line prompt when it did not.
        if (!bAlt && bCtrl && !bShift && pMsg->wParam == 'F')
        {
            if (m_bSearchBar)
                m_searchBar.FocusAndSelect();
            else
                PromptForQuery();

            return TRUE;
        }
    }

    return CTreePropSheet::PreTranslateMessage(pMsg);
}

void CPropertySheetMem::EnsurePagesCreated ()
{
    if (m_bPagesForced)
        return;

    // Set first, not last: SetActivePage() pumps messages, so anything that could
    // re-enter a search must see this as done rather than start a second round-robin
    // on a half-created sheet.
    m_bPagesForced = TRUE;

    const int nPages  = GetPageCount();
    const int nActive = GetActiveIndex();

    if (nPages <= 0)
        return;

    // MFC's CPropertyPage keeps no readable "modified" flag - SetModified() only
    // sends PSM_CHANGED / PSM_UNCHANGED at the parent sheet and comctl32 tracks the
    // result internally. The Apply button is the one visible reflection of "some page
    // is modified", so that is what gets snapshotted. A disabled Apply means no page
    // was modified, which makes the restore below exact. A sheet built with
    // PSH_NOAPPLYNOW - the Settings and DDL ones - has no such button, and then there
    // is no visible state to protect either.
    CWnd* const pApply       = GetDlgItem(ID_APPLY_NOW);
    const BOOL  bApplyWasOn  = (pApply != NULL && pApply->IsWindowEnabled());

    // Round-robin. Each SetActivePage() creates that page's window, which runs its
    // OnInitDialog() and so its BuildTree() - the only way a page the user never
    // clicked gets any rows to search at all. MFC creates a property page's HWND the
    // first time it is shown and not before.
    for (int i = 0; i < nPages; i++)
    {
        if (i != nActive)
            SetActivePage(i);
    }

    // Put the sheet back the way the user had it. Not cosmetic: OnDestroy() stores
    // GetActiveIndex() into the caller's selectPage, which is the page the dialog
    // opens on next time, so without this a plain search would silently move it.
    if (nActive >= 0 && nActive < nPages)
        SetActivePage(nActive);

    if (!bApplyWasOn && pApply != NULL && pApply->IsWindowEnabled())
    {
        // Building a page marked it modified for nothing the user did. Clear all of
        // them: Apply was off before, so none of them held a real change worth
        // keeping, and leaving Apply lit would invite a click that rewrites settings.
        for (int i = 0; i < nPages; i++)
        {
            if (CPropertyPage* pPage = GetPage(i))
                pPage->SetModified(FALSE);
        }
    }
}

CString CPropertySheetMem::GetPageCaption (int nPage) const
{
    CString sCaption;

    // The tab control is still there in tree view mode, merely hidden, and still
    // carries the captions comctl32 was given from each PROPSHEETPAGE's pszTitle.
    // This is the same TCITEM CTreePropSheet reads to build the tree of pages, so it
    // is the one place a page title can be got for a page of any kind: a
    // CPropTreePage keeps its own copy, but a classic page's lives only in the
    // protected CPropertyPage::m_strCaption.
    CTabCtrl* pTab = GetTabControl();

    if (pTab == NULL || pTab->GetSafeHwnd() == NULL)
        return sCaption;

    if (nPage < 0 || nPage >= pTab->GetItemCount())
        return sCaption;

    TCITEM ti;
    ZeroMemory(&ti, sizeof(ti));
    ti.mask       = TCIF_TEXT;
    ti.cchTextMax = MAX_PATH;
    ti.pszText    = sCaption.GetBuffer(ti.cchTextMax);

    if (!pTab->GetItem(nPage, &ti))
        sCaption.Empty();

    sCaption.ReleaseBuffer();

    return sCaption;
}

int CPropertySheetMem::RunSearch ()
{
    m_hits.clear();

    std::vector<CString> terms;

    if (!PropTreeSplitSearchTerms(m_strQuery, terms))
        return 0;

    const int nPages = GetPageCount();

    // Page order, which is the order the pages appear in the sheet's tree, so F3
    // walks the dialog front to back and the hits of one page stay together.
    for (int i = 0; i < nPages; i++)
    {
        CPropertyPage* pPage = GetPage(i);

        if (pPage == NULL)
            continue;

        const int nFirst = (int)m_hits.size();

        // dynamic_cast, not IsKindOf(): CPropTreePage declares no RUNTIME_CLASS of
        // its own and the project builds with RTTI on, which is also how the rest of
        // the codebase narrows MFC base pointers.
        if (CPropTreePage* pTreePage = dynamic_cast<CPropTreePage*>(pPage))
        {
            // CollectMatches() returns 0 for a page whose window was never built, and
            // EnsurePagesCreated() has made sure every page's is. The row pointer it
            // fills in is dropped here on purpose - only the control id is kept, see
            // SheetSearchHit.
            std::vector<PropTreeSearchHit> rows;

            pTreePage->CollectMatches(m_strQuery, m_dwSearchFields, rows);

            for (std::vector<PropTreeSearchHit>::size_type r = 0; r < rows.size(); r++)
                m_hits.push_back(SheetSearchHit(i, rows[r].nCtrlID));
        }

        // The page's own title is matched here, at the sheet, and not inside
        // CollectMatches(): that would put the same title in every row's haystack and
        // turn one search for a page name into a hit per row. Added only when no row
        // of the page matched, otherwise F3 would stop on the page and then
        // immediately again on its first row - the same place twice. This is also
        // what makes a classic page with no tree at all reachable by name.
        //
        // Gated on PSF_PATH like everything else, because the page title is the outermost
        // part of the path a row sits in, and a field the mask cannot reach is a hit the
        // user cannot turn off. No caller passes a mask without PSF_PATH today, so this
        // changes nothing yet - it keeps that true for the first one that does.
        if ((int)m_hits.size() == nFirst && (m_dwSearchFields & PSF_PATH) &&
            PropTreeTermsMatch(GetPageCaption(i), terms))
            m_hits.push_back(SheetSearchHit(i, 0));
    }

    return (int)m_hits.size();
}

void CPropertySheetMem::ShowHit (const SheetSearchHit& hit)
{
    if (hit.nPage < 0 || hit.nPage >= GetPageCount())
        return;

    // The page first, always. RevealItem() on a page that is created but hidden only
    // scrolls a window nobody can see, and CPropTree::EnsureVisible() takes its
    // rectangle from the list's client rect - empty for a page that has not been laid
    // out yet, so nothing would scroll at all.
    SetActivePage(hit.nPage);

    // Then make sure it took. PSM_SETCURSEL is intercepted by CTreePropSheet, whose
    // OnSetCurSel() re-selects the page tree and so drives TVN_SELCHANGING back into
    // SetActivePage(), with a re-entrancy flag and a SetFocus() to the tree on the way.
    // Two page switches back to back - EnsurePagesCreated() putting the original page
    // back, then this one - can leave the second behind in that churn, and the sheet
    // then stays where the search started. One retry against a settled sheet is enough.
    if (GetActiveIndex() != hit.nPage)
        SetActivePage(hit.nPage);

    if (hit.nCtrlID == 0)
        return;     // a title match: the page itself is the hit, nothing to reveal

    CPropTreePage* pTreePage = dynamic_cast<CPropTreePage*>(GetPage(hit.nPage));

    if (pTreePage == NULL)
        return;

    // Re-resolve the control id instead of trusting a CPropTreeItem* carried over
    // from the search that found it. CVisualAttributesPageTree deletes and recreates
    // its sample rows when the sample text changes, so such a pointer dangles and
    // revealing it would walk freed memory. FindItem() walks the live tree.
    CPropTreeItem* pItem = pTreePage->GetPropTree().FindItem(hit.nCtrlID);

    if (pItem != NULL)
        pTreePage->RevealItem(pItem);
}

void CPropertySheetMem::ReportNotFound ()
{
    if (m_strQuery.IsEmpty())
        return;

    CString sMsg;
    sMsg.Format(L"No setting matches \"%s\".", (LPCTSTR)m_strQuery);

    AfxMessageBox(sMsg, MB_OK | MB_ICONINFORMATION);
}

BOOL CPropertySheetMem::PromptForQuery ()
{
    if (m_bPromptOpen)
        return FALSE;       // Ctrl+F reached us from inside the prompt itself

    // Common::CInputDlg, the app's one-line prompt. No new dialog template:
    // IDD_INPUT has no room for a "search values" check box, which is why the fields
    // are chosen once per sheet in EnableSearch() rather than per query.
    CInputDlg dlg(this);

    dlg.m_title  = L"Find Setting";
    dlg.m_prompt = L"Find setting by name, description or group:";
    dlg.m_value  = m_strQuery;      // the last query, so Ctrl+F after F3 can refine it

    m_bPromptOpen = TRUE;
    const INT_PTR nResult = dlg.DoModal();
    m_bPromptOpen = FALSE;

    if (nResult != IDOK)
        return FALSE;

    CString sQuery(dlg.m_value.c_str());
    sQuery.Trim();

    if (sQuery.IsEmpty())
        return FALSE;

    m_strQuery = sQuery;

    // A new query starts a new walk. Without this, a query whose first hit happened
    // to be the row the old query left us on would skip that row.
    m_nLastPage   = -1;
    m_nLastCtrlID = 0;

    // Posted, so the walk over every page that EnsurePagesCreated() does - the visible
    // round-robin the user sees on a first search - ends on the first hit rather than
    // on the page it started from. See WM_SQLT_SEARCH_FIRST_HIT.
    PostMessage(WM_SQLT_SEARCH_FIRST_HIT);

    return TRUE;
}

// The other end of the PostMessage() in PromptForQuery() above. Not quiet: this is
// the CInputDlg path, where a "no setting matches" box is the answer the user asked
// for and there is no bar to show it in instead.
LRESULT CPropertySheetMem::OnSearchFirstHit (WPARAM, LPARAM)
{
    GotoHit(+1, /*bQuiet =*/FALSE);
    return 0;
}

void CPropertySheetMem::GotoHit (int nDelta, BOOL bQuiet, BOOL bBeepIfNone)
{
    if (!m_bSearchEnabled)
        return;

    // One search at a time, and the guard rather than a hand-set flag because this
    // function returns from three places. See m_bSearching.
    CSearchRunGuard guard(m_bSearching);

    if (!guard.Entered())
        return;

    // F3 with no query yet asks for one, the way Find-next does in the editor.
    // Skipped when quiet: a keystroke that left the bar empty is the user clearing
    // their search, not a request to be prompted for another one.
    if (m_strQuery.IsEmpty())
    {
        if (bQuiet)
            UpdateSearchBar(0, 0, /*bNoMatch =*/FALSE);
        else
            PromptForQuery();

        return;
    }

    // Asked for before anything that switches pages, and given back at the end.
    // Switching pages ends in CTreePropSheet::OnPageTreeSelChanging(), which calls
    // SetFocus() on the page tree - so without this, a query typed into the bar would
    // throw the caret out of it on the very first hit and the next keystroke would go
    // to the tree instead of to the field being typed into.
    const BOOL bBarHadFocus = (m_bSearchBar && m_searchBar.HasFieldFocus());

    EnsurePagesCreated();

    // Re-run on every press rather than walking a stored list: rows change while the
    // sheet is open - the visual-attributes sample text rebuilds its rows, and an edit
    // changes what a value search matches. The walk is then always against what is on
    // screen, and no index has to be kept in step with it.
    const int nHits = RunSearch();

    if (nHits <= 0)
    {
        // The counter is the quiet answer: "0/0" in red says what the box would say,
        // without stealing the keyboard from a field the user is still typing into.
        if (bQuiet)
        {
            UpdateSearchBar(0, 0, /*bNoMatch =*/TRUE);

            // And a sound behind it when this was a step the user asked for and is
            // waiting on. The plain beep rather than MB_ICONSTOP: nothing has gone
            // wrong, the query simply matched nothing, and the error sound is kept
            // for the failures elsewhere in the app.
            if (bBeepIfNone)
                MessageBeep((UINT)-1);
        }
        else
        {
            ReportNotFound();
        }

        if (bBarHadFocus)
            m_searchBar.RestoreFocus();

        return;
    }

    // Where the walk stands: the stored identity looked up in the fresh list, not an
    // offset carried over. A page that rebuilt its rows can have put the same control
    // id at a different offset.
    int nAt = -1;

    for (int i = 0; i < nHits; i++)
    {
        if (m_hits[i].nPage == m_nLastPage && m_hits[i].nCtrlID == m_nLastCtrlID)
        {
            nAt = i;
            break;
        }
    }

    int nNext;

    if (nAt >= 0)
        nNext = nAt + nDelta;
    else if (m_nLastPage < 0)
        nNext = (nDelta > 0) ? 0 : nHits - 1;      // first press of this query
    else
    {
        // The row we stood on is gone, so restart from the page the sheet shows: the
        // first hit after it going forward, the last one at or before it going back,
        // and the opposite end when there is none on that side.
        const int nPage = GetActiveIndex();

        nNext = (nDelta > 0) ? 0 : nHits - 1;

        for (int i = 0; i < nHits; i++)
        {
            const BOOL bAtOrBefore = (m_hits[i].nPage <= nPage);

            if ((nDelta > 0) ? !bAtOrBefore : bAtOrBefore)
            {
                nNext = i;

                if (nDelta > 0)
                    break;      // forward wants the first of these, back the last
            }
        }
    }

    if (nNext < 0)
        nNext = nHits - 1;      // wrap past the start
    else if (nNext >= nHits)
        nNext = 0;              // wrap past the end

    // Copied out before ShowHit(): activating a page can make it rebuild rows, and
    // what has to be remembered is the hit that was asked for, not whatever the list
    // ends up holding afterwards.
    const SheetSearchHit hit = m_hits[nNext];

    ShowHit(hit);

    m_nLastPage   = hit.nPage;
    m_nLastCtrlID = hit.nCtrlID;

    // 1-based, because "0/17" reads as a count rather than as a position and the bar
    // is showing which hit the user is looking at.
    UpdateSearchBar(nNext + 1, nHits, /*bNoMatch =*/FALSE);

    if (bBarHadFocus)
        m_searchBar.RestoreFocus();
}

/////////////////////////////////////////////////////////////////////////////
// (ak) The search field above the page tree.
//
// The bar is the tree-mode half of the search: it holds the query, shows where the
// walk stands, and says nothing about what a search matches. This is the half that
// searches. Everything below is reached from OnSearchBar(), the other end of
// CPropTreeSearchBar::Notify().

void CPropertySheetMem::CreateSearchBar ()
{
    m_bSearchBar = FALSE;

    if (!m_bSearchEnabled)
        return;

    // Tree mode only, and not because a bar would be useless in a tabbed sheet - it
    // would be homeless. CTreePropSheet carves the tree's column out of the left of
    // the sheet by widening the window and moving every child right; a tabbed sheet
    // has no such column, and no strip of bare background a field could go into
    // without covering part of the page. Those sheets keep the CInputDlg prompt.
    if (!IsTreeViewMode())
        return;

    CTreeCtrl* pTree = GetPageTreeControl();

    if (pTree == NULL || !::IsWindow(pTree->GetSafeHwnd()))
        return;

    CRect rcTree;
    pTree->GetWindowRect(rcTree);
    ScreenToClient(rcTree);

    const int nBarH = CPropTreeSearchBar::GetPreferredHeight(GetFont());

    // The gap CTreePropSheet leaves between the tree and the page frame, so the strip
    // reads as part of the column rather than as something dropped on top of it.
    const int nGap = 5;

    const CRect rcBar(rcTree.left, rcTree.top, rcTree.right, rcTree.top + nBarH);

    // Carved out of the tree's own column, with the tree giving up the height: there
    // is nothing above it to take the strip from. Growing the sheet instead would mean
    // re-running the layout CTreePropSheet::OnInitDialog() does - widen the window,
    // move every child, re-centre - from a subclass, and losing a few rows of tree is
    // the cheaper trade by a long way.
    rcTree.top += nBarH + nGap;
    pTree->MoveWindow(rcTree);

    LPCTSTR pszCue = m_strSearchCue.IsEmpty() ? L"Search settings" : (LPCTSTR)m_strSearchCue;

    // If this fails the sheet is left exactly as it was before the bar existed, with
    // m_bSearchBar FALSE and Ctrl+F falling back to the prompt. The tree has already
    // been moved down by then and the gap stays, which is a bare strip of background
    // and nothing worse.
    if (!m_searchBar.Create(rcBar, this, pszCue))
        return;

    m_bSearchBar = TRUE;

    // Seeded from the field set rather than from defaults of their own: a caller that
    // asked for descriptions to be left out, or for values to be let in, before DoModal()
    // gets a bar whose ticks already say so, and the two are never reconciled afterwards.
    m_searchBar.SetIncludeDescriptions((m_dwSearchFields & PSF_INFO) != 0);
    m_searchBar.SetIncludeValues((m_dwSearchFields & PSF_VALUE) != 0);
    m_searchBar.SetIncludeChoices((m_dwSearchFields & PSF_CHOICES) != 0);

    // Agrees the counter with the empty field: nothing typed, nothing counted, and no
    // red, because "no query yet" is not a failed search.
    UpdateSearchBar(0, 0, /*bNoMatch =*/FALSE);
}

void CPropertySheetMem::ResetSearchWalk ()
{
    m_nLastPage   = -1;
    m_nLastCtrlID = 0;
}

void CPropertySheetMem::UpdateSearchBar (int nCurrent, int nTotal, BOOL bNoMatch)
{
    if (!m_bSearchBar)
        return;

    // Colour first: SetCounter() returns early when the numbers have not changed, and
    // a query that went from no hits to still no hits after an edit would otherwise
    // keep whatever colour it was last painted in.
    m_searchBar.SetNoMatch(bNoMatch);
    m_searchBar.SetCounter(nCurrent, nTotal);
}

LRESULT CPropertySheetMem::OnSearchBar (WPARAM wParam, LPARAM /*lParam*/)
{
    switch (wParam)
    {
    case PSBN_TEXTCHANGED:
    {
        // Read back rather than tracked: the bar owns the text, and a second copy kept
        // here would be a second thing to keep in step with what is in the field.
        CString sQuery = m_searchBar.GetText();
        sQuery.Trim();

        // An empty field is not a query that matched nothing, it is no query at all.
        // Telling the two apart is what leaves the counter blank instead of red once
        // the user deletes what they typed.
        if (sQuery.IsEmpty())
        {
            m_strQuery.Empty();
            ResetSearchWalk();
            UpdateSearchBar(0, 0, /*bNoMatch =*/FALSE);
            return 0;
        }

        // A different query starts a new walk from its first hit, so that typing the
        // next character of a term shows that term's first match rather than stepping
        // on from wherever the shorter term had got to.
        const BOOL bNewQuery = (sQuery.CompareNoCase(m_strQuery) != 0);

        m_strQuery = sQuery;

        if (bNewQuery)
            ResetSearchWalk();

        GotoHit(+1, /*bQuiet =*/TRUE);
        return 0;
    }

    // The two steps a user asks for by name, and the two that beep when the query
    // matched nothing: Enter and the arrow keys in the field, or a click on the arrow
    // buttons, all of which arrive here as one of these two notifications. F3 takes a
    // separate route through PreTranslateMessage() and asks for the same beep there.
    // The typed and settings paths leave bBeepIfNone at its default - see GotoHit()
    // for why a beep from those would be noise rather than an answer.
    case PSBN_NEXT:
        GotoHit(+1, /*bQuiet =*/TRUE, /*bBeepIfNone =*/TRUE);
        return 0;

    case PSBN_PREV:
        GotoHit(-1, /*bQuiet =*/TRUE, /*bBeepIfNone =*/TRUE);
        return 0;

    case PSBN_CLEARED:
        // Esc in the field. The bar has already emptied itself; this only drops the
        // query the sheet was carrying, so an F3 afterwards means "search for
        // something" again instead of repeating the one that was just cleared.
        m_strQuery.Empty();
        ResetSearchWalk();
        UpdateSearchBar(0, 0, /*bNoMatch =*/FALSE);
        return 0;

    case PSBN_SETTINGS:
        // A tick moved, so what a query matches moved with it. Both are read back rather
        // than tracked here: m_dwSearchFields stays the single record of the field set and
        // the setters are the only way into it. Taking the whole set on either item's
        // command is what lets one notify code serve the whole menu - this handler cannot
        // tell which tick moved, and does not need to.
        SetSearchIncludeDescriptions(m_searchBar.GetIncludeDescriptions());
        SetSearchIncludeValues(m_searchBar.GetIncludeValues());
        SetSearchIncludeChoices(m_searchBar.GetIncludeChoices());

        // Re-searched only when there is a query to re-search. GotoHit() on an empty one
        // would open the CInputDlg prompt, which is not what a checkbox is for - and
        // bQuiet does not silence that, it silences the "not found" box. The counter is
        // left alone for the same reason: it describes the last search, and with no query
        // there has not been one.
        if (!m_strQuery.IsEmpty())
        {
            // A different field set is a different query as far as the walk is concerned:
            // the hit the user was standing on may no longer be a hit at all.
            ResetSearchWalk();
            GotoHit(+1, /*bQuiet =*/TRUE);
        }
        return 0;
    }

    return 0;
}

}; // namespace Common
