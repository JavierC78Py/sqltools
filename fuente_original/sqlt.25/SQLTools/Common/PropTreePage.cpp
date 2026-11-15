#include "stdafx.h"
#include <COMMON/ExceptionHelper.h>
#include "Common/PropTreePage.h"
#include "Common/PropTreePage.rh"

namespace Common {

CPropTreePage::CPropTreePage (LPCTSTR pszTitle, UINT nIDHelp)
	: CPropertyPage(IDD_PROPTREE_PAGE)
{
	// Every page shares IDD_PROPTREE_PAGE, whose caption is meaningless, so the
	// title always comes from the page itself. m_strCaption (CPropertyPage's own
	// title buffer) keeps the string alive as long as the page - and therefore
	// as long as the sheet that points m_psp.pszTitle at it.
	m_strCaption     = pszTitle;
	m_psp.pszTitle   = m_strCaption;
	m_psp.dwFlags   |= PSP_USETITLE;

	// MFC derived the help id from the template id. The template is shared now,
	// so the page's own (historical) dialog id has to be assigned explicitly:
	// Help resolves HID_BASE_RESOURCE + m_nIDHelp to a topic.
	m_nIDHelp = nIDHelp;

	if (nIDHelp)
		m_psp.dwFlags |= PSP_HASHELP;
	else
		m_psp.dwFlags &= ~PSP_HASHELP;
}

CPropTreePage::~CPropTreePage ()
{
}

void CPropTreePage::SetPageHelp (BOOL bEnable)
{
	if (bEnable)
		m_psp.dwFlags |=  PSP_HASHELP;
	else
		m_psp.dwFlags &= ~PSP_HASHELP;
}

void CPropTreePage::DoDataExchange (CDataExchange* pDX)
{
	CPropertyPage::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CPropTreePage, CPropertyPage)
	ON_NOTIFY(PTN_ITEMCHANGED, IDC_PTP_TREE, OnPropTreeNotify)
	ON_NOTIFY(PTN_CHECKCLICK,  IDC_PTP_TREE, OnPropTreeNotify)
	// Expanding a group is no edit, so this one goes to its own handler and not to
	// OnPropTreeNotify(), which marks the page modified.
	ON_NOTIFY(PTN_ITEMEXPANDING, IDC_PTP_TREE, OnItemExpanding)
	ON_WM_SIZE()
END_MESSAGE_MAP()

BOOL CPropTreePage::OnInitDialog ()
{
	CPropertyPage::OnInitDialog();

	CRect rc;
	GetClientRect(&rc);

	m_tree.Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | PTS_NOTIFY, rc, this, IDC_PTP_TREE);
	m_tree.SetInfoHeight(GetTreeInfoHeight());

	// Gutter and indent before the splitter below: CPropTree::SetColumn() clamps to the gutter
	// width, so a page that widened it has to have said so by the time the column is resolved.
	m_tree.SetGutterLevels(GetTreeGutterLevels());
	m_tree.SetIndentRootChildren(GetTreeIndentsRootChildren());

	// Label/attribute splitter: the page's fixed pixel value, or - when it returns
	// 0 (the default) - two thirds of the tree width. Resolved here because the
	// tree's client rect (rc, the page client rect) is known only after Create(),
	// and SetColumn() itself only clamps to a minimum of 16 px.
	int nColumn = GetTreeColumn();

	if (nColumn <= 0)
		nColumn = rc.Width() * 6 / 10;

	m_tree.SetColumn(nColumn);

	BuildTree();

	return TRUE;
}

BOOL CPropTreePage::OnApply ()
{
	try { EXCEPTION_FRAME;

		// The cell being edited must be committed before its value is read,
		// otherwise the last edit is silently lost.
		if (IsWindow(m_tree.m_hWnd))
		{
			CPropTreeItem* pFocused = m_tree.GetFocusedItem();
			if (pFocused)
				pFocused->CommitChanges();
		}

		if (!ApplyValues())
			return FALSE;

		SaveTreeState();
	}
	_COMMON_DEFAULT_HANDLER_;

	return TRUE;
}

void CPropTreePage::OnCancel ()
{
	try { EXCEPTION_FRAME;

		// Keep the group expansion state even when the sheet is cancelled.
		SaveTreeState();
	}
	_COMMON_DEFAULT_HANDLER_;

	CPropertyPage::OnCancel();
}

void CPropTreePage::OnPropTreeNotify (NMHDR* pNMHDR, LRESULT* pResult)
{
	SetModified(TRUE);

	NMPROPTREE* pNMPropTree = (NMPROPTREE*)pNMHDR;
	OnItemChanged(pNMPropTree->pItem);

	*pResult = 0;
}

void CPropTreePage::OnSize (UINT nType, int cx, int cy)
{
	CPropertyPage::OnSize(nType, cx, cy);

	if (IsWindow(m_tree.m_hWnd))
		m_tree.MoveWindow(0, 0, cx, cy);
}

// PTN_ITEMEXPANDING, sent by CPropTreeList BEFORE it toggles the item and from all four
// places that can toggle one: the expand box (OnLButtonDown), a double click on the label
// (OnLButtonDblClk) and the left and the right arrow key (OnKeyDown). A non zero *pResult
// vetoes the toggle; the page never vetoes, it only closes the siblings of the item while
// it is about to open. The UpdateResize(), Invalidate() and CheckVisibleFocus() the list
// calls right after the toggle pick the closed items up in the same pass, so there is
// nothing to repaint here.
void CPropTreePage::OnItemExpanding (NMHDR* pNMHDR, LRESULT* pResult)
{
	*pResult = 0;

	if (!GetOneGroupOpenAtATime() || !pNMHDR)
		return;

	CPropTreeItem* pItem = ((NMPROPTREE*)pNMHDR)->pItem;

	// Closing an item is left alone - the notification is sent for that too, and the item
	// is still expanded while it arrives.
	if (!pItem || pItem->IsExpanded())
		return;

	// Every level that can be opened takes part in the accordion and not the top level
	// alone, which is what keeps an open group short enough to read: a page whose groups
	// nest sub-groups gets "one open sub-group per group" from the same walk. The rows
	// below a group are leaves, so nothing deeper ever sends this notification.
	CloseOtherGroups(pItem);
}

void CPropTreePage::CloseOtherGroups (CPropTreeItem* pOpening)
{
	if (!pOpening)
		return;

	bool closed = false;

	CPropTreeItem* pParent = pOpening->GetParent();

	// Expand(FALSE) is a plain state change and sends no notification, so this cannot
	// re-enter OnItemExpanding(). For a top level group the parent is the invisible root
	// item and the siblings are the other groups.
	for (CPropTreeItem* pSibling = pParent ? pParent->GetChild() : 0;
		pSibling; pSibling = pSibling->GetSibling())
	{
		if (pSibling != pOpening && pSibling->IsExpanded())
		{
			pSibling->Expand(FALSE);
			closed = true;
		}
	}

	if (!closed)
		return;

	// The tree got shorter, so the scroll offset the list keeps may now be past its end,
	// which would leave an unpainted band at the bottom: CPropTreeList::UpdateResize()
	// resets the offset - to zero - only while the whole tree fits the page. Clamp it to
	// the new end here; the UpdateResize() the list calls right after the toggle then
	// reports the clamped offset.
	CWnd* pList = m_tree.GetCtrlParent();      // the CPropTreeList window

	if (!pList || !::IsWindow(pList->m_hWnd))
		return;

	CRect rcList;
	pList->GetClientRect(&rcList);

	// GetContentHeight() is what the list builds its own scrollbar range from. The root
	// item is not content - OnPaint() lays the rows out from its first child with y = 0 -
	// so GetRootItem()->GetTotalHeight() here would have allowed an offset one row past
	// the end, and left exactly the band this clamp exists to prevent.
	//
	// The rows are clipped to the border-deflated rect, so the band that has to hold the
	// content is two pixels shorter than the client area. CPropTreeList::OnVScroll() clamps
	// to the same expression; using the full height here would stop the page two pixels
	// short of the offset the list itself allows.
	LONG maxOffset = m_tree.GetContentHeight() - (rcList.Height() - PROPTREELIST_BORDER);

	if (maxOffset < 0)
		maxOffset = 0;

	if (m_tree.GetOrigin().y > maxOffset)
		m_tree.SetOriginOffset(maxOffset);
}

void CPropTreePage::RedrawTree ()
{
	if (IsWindow(m_hWnd))
		RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
}

////////////////////////////////////////////////////////////////////////////////
// Settings search.
//
// A live scan of the tree on every search rather than an index built once. The
// content is small - a few hundred rows across a whole sheet - it is dynamic,
// because CVisualAttributesPageTree deletes and recreates its sample rows while
// the sheet is open, and an index would have to be rebuilt from every place that
// can change a row. Scanning is also what makes a short term work, which an index
// of whole words cannot do.
//
// See SQLTools/md/proptree_search.md.

namespace {

// One CollectMatches() call, handed to the EnumItems() callback in its LPARAM.
struct SearchContext
{
	DWORD                            dwFields;
	const std::vector<CString>*      pTerms;    // lowercased, whitespace split
	CString                          sPageTitle;
	CPropTreeItem*                   pRoot;     // the tree's invisible root item
	std::vector<PropTreeSearchHit>*  pHits;
};

// Appends pszText and one separating space to sHay, when there is text to add. The
// space is what lets several fields share one haystack without a term straddling
// two of them and matching neither.
void AppendField (CString& sHay, LPCTSTR pszText)
{
	if (pszText && *pszText)
	{
		sHay += pszText;
		sHay += _T(' ');
	}
}

// Adds the labels of the groups pItem sits in. Not the page title: that is the same
// for every row on the page, so it would match every row at once and turn a search
// for a page name into one hit per row. The sheet matches page titles itself.
//
// The walk stops at pRoot - the invisible item every top level group hangs from,
// which is what CPropTreeItem::IsRootLevel() tests for.
void AppendGroupPath (CString& sHay, CPropTreeItem* pItem, CPropTreeItem* pRoot)
{
	for (CPropTreeItem* p = pItem->GetParent(); p && p != pRoot; p = p->GetParent())
		AppendField(sHay, p->GetLabelText());
}

// "Page title > Group > Sub-group" for pItem: the page title, then the groups it
// sits in, outermost first. pItem's own label is left out, because this string is
// shown beside a row that is already named.
CString BuildHitPath (CPropTreeItem* pItem, const CString& sPageTitle, CPropTreeItem* pRoot)
{
	std::vector<CString> groups;

	for (CPropTreeItem* p = pItem->GetParent(); p && p != pRoot; p = p->GetParent())
		groups.push_back(CString(p->GetLabelText()));

	CString sPath(sPageTitle);

	for (std::vector<CString>::size_type i = groups.size(); i-- > 0;)
	{
		sPath += _T(" > ");
		sPath += groups[i];
	}

	return sPath;
}

// The CPropTree::EnumItems() callback. Always TRUE: this is a collection and not a
// lookup that may stop early, because F3 walks the hits in tree order and skipping
// the rest of the tree would leave gaps in that walk.
BOOL CALLBACK EnumCollectMatches (CPropTree*, CPropTreeItem* pItem, LPARAM lParam)
{
	SearchContext* pCtx = (SearchContext*)lParam;

	if (!pItem)
		return TRUE;

	// The row's own text and its group path are matched apart before they are matched
	// together, and a row that matches ONLY through the path is dropped. Searching
	// "Output" would otherwise report the group "Output" and then every row sitting in
	// it - none of which is named "Output" - putting twenty rows the user did not ask
	// for between the group and whatever comes next. The group is the hit; its contents
	// are one click away now that RevealItem() has expanded it.
	//
	// A row that matches on its own text keeps its place even inside a matching group
	// (group "Output", row "Output folder"): it is a hit in its own right. And a row
	// that matches only because one term came from the group and another from its own
	// label ("output" + a row called "Format") still counts - neither half matches on
	// its own, so no single ancestor hit covers it, and dropping it would lose a real
	// result. That is what testing the two halves separately, rather than testing
	// "did the path match", buys.
	// (ak) A GROUP row - anything that has children - is matched on its label and its
	// group path alone. Its info text is a blurb describing what the group holds,
	// "Formats, transaction handling and Dbms_Output of a database session.", so a
	// search for "output" matched that header and made it the first hit, ahead of the
	// rows inside it that the user was actually after. A header is not a setting, and on
	// a page whose first group is open by default it sits at the top of the list already,
	// so revealing it scrolled nothing and the very first Ctrl+F looked dead.
	//
	// PSF_PATH is deliberately untouched: a group still matches through the labels of
	// its own ancestors, and the bPath/bSelf rule below still drops it when an ancestor
	// is the real hit. Rows keep matching on every field.
	const BOOL bIsGroup = (pItem->GetChild() != NULL);

	CString sSelf, sPath;

	if (pCtx->dwFields & PSF_LABEL)
		AppendField(sSelf, pItem->GetLabelText());

	if (!bIsGroup)
	{
		if (pCtx->dwFields & PSF_INFO)
			AppendField(sSelf, pItem->GetInfoText());

		// Both are CPropTreeItem virtuals that the base leaves empty, so a row type with
		// no such text adds nothing, and a row type added later cannot add something
		// wrong - it simply stays out of the search until it overrides them.
		if (pCtx->dwFields & PSF_CHOICES)
			pItem->AppendSearchChoices(sSelf);

		if (pCtx->dwFields & PSF_VALUE)
			pItem->AppendSearchValue(sSelf);
	}

	if (pCtx->dwFields & PSF_PATH)
		AppendGroupPath(sPath, pItem, pCtx->pRoot);

	const BOOL bSelf = PropTreeTermsMatch(sSelf, *pCtx->pTerms);
	const BOOL bPath = PropTreeTermsMatch(sPath, *pCtx->pTerms);

	if (bPath && !bSelf)
		return TRUE;		// covered by an ancestor group, which is a hit itself

	CString sHay(sSelf);
	AppendField(sHay, sPath);

	if (!PropTreeTermsMatch(sHay, *pCtx->pTerms))
		return TRUE;

	PropTreeSearchHit hit;
	hit.pItem   = pItem;
	hit.nCtrlID = pItem->GetCtrlID();
	hit.sPath   = BuildHitPath(pItem, pCtx->sPageTitle, pCtx->pRoot);

	pCtx->pHits->push_back(hit);

	return TRUE;
}

} // unnamed namespace

// See the declarations in PropTreePage.h. These two are the matching rule the sheet
// shares, so a page title and a row are never searched by different rules.

bool PropTreeSplitSearchTerms (LPCTSTR pszQuery, std::vector<CString>& terms)
{
	terms.clear();

	if (!pszQuery)
		return false;

	CString sQuery(pszQuery);
	sQuery.MakeLower();

	const int nLen   = sQuery.GetLength();
	int       nStart = -1;

	for (int i = 0; i <= nLen; i++)
	{
		// One past the end counts as a separator, so the last term is flushed.
		const bool bSpace = (i == nLen) || (_istspace((_TUCHAR)sQuery[i]) != 0);

		if (bSpace)
		{
			if (nStart >= 0)
			{
				terms.push_back(sQuery.Mid(nStart, i - nStart));
				nStart = -1;
			}
		}
		else if (nStart < 0)
			nStart = i;
	}

	return !terms.empty();
}

bool PropTreeTermsMatch (LPCTSTR pszText, const std::vector<CString>& terms)
{
	if (!pszText || terms.empty())
		return false;

	CString sHay(pszText);
	sHay.MakeLower();

	// Every term has to be present: "connection timeout" narrows the result, it does
	// not widen it.
	for (std::vector<CString>::size_type i = 0; i < terms.size(); i++)
	{
		if (_tcsstr(sHay, terms[i]) == 0)
			return false;
	}

	return true;
}

int CPropTreePage::CollectMatches (LPCTSTR pszQuery, DWORD dwFields,
                                   std::vector<PropTreeSearchHit>& hits)
{
	// A page whose window was never created has no rows: MFC builds it in
	// OnInitDialog(), which for a property page runs the first time that page is
	// shown. Nothing to search, and nothing to report.
	if (!IsTreeBuilt())
		return 0;

	std::vector<CString> terms;

	if (!PropTreeSplitSearchTerms(pszQuery, terms))
		return 0;

	const std::vector<PropTreeSearchHit>::size_type nFirst = hits.size();

	SearchContext ctx;
	ctx.dwFields   = dwFields;
	ctx.pTerms     = &terms;
	ctx.sPageTitle = m_strCaption;
	ctx.pRoot      = m_tree.GetRootItem();
	ctx.pHits      = &hits;

	// EnumItems() skips the invisible root and walks depth first, so the hits come
	// out in the order their rows appear on screen - which is the order F3 should
	// move through them.
	m_tree.EnumItems(ctx.pRoot, EnumCollectMatches, (LPARAM)&ctx);

	return (int)(hits.size() - nFirst);
}

BOOL CPropTreePage::RevealItem (CPropTreeItem* pItem)
{
	if (!pItem || !IsTreeBuilt())
		return FALSE;

	// A cell being edited would otherwise keep its editor on screen, and the keyboard
	// focus, while the selection moves away from underneath it. Committing it is what
	// clicking any other row does too, so this is expected behaviour rather than a
	// surprise - but note that it does raise PTN_ITEMCHANGED, and with it
	// SetModified(), for whatever edit was in progress.
	m_tree.DeactivateAll();

	// Focus the row. SetFocusedItem() also expands its ancestors, scrolls it into
	// view and invalidates the tree.
	m_tree.SetFocusedItem(pItem);

	// Then select it, and ONLY it. SelectItems(pItem, TRUE) is not this: it is
	// EnumItems(pItem, EnumSelectAll, ...), so it would select the whole subtree.
	// Clear-then-select is what CPropTree::FocusFirst() does; that function cannot be
	// used here because it is hardwired to the first visible item.
	//
	// PTN_SELCHANGE is deliberately not sent. FocusFirst() sends it because there the
	// user is changing the selection; this is navigation to a row a search found, and
	// the page has no handler for the notification in any case.
	m_tree.SelectItems(0, FALSE);
	pItem->Select();

	// A search that matched a group name should land on that group's contents rather
	// than on a closed header. A leaf has no children, so this is a no-op for the
	// ordinary row.
	if (pItem->GetChild())
		pItem->Expand();

	if (GetOneGroupOpenAtATime())
	{
		// EnsureVisible() - which SetFocusedItem() has just called - expands every
		// ancestor with a raw Expand() loop and sends NO PTN_ITEMEXPANDING, so
		// OnItemExpanding() never runs and the accordion is bypassed entirely. Left at
		// that, revealing a row in an accordion page would leave several groups open at
		// once and break the invariant the page was built on. Repeat here the very call
		// OnItemExpanding() makes: for the row itself when it is a group, then for every
		// group above it, from the top level down.
		if (pItem->GetChild())
			CloseOtherGroups(pItem);

		CPropTreeItem*            pRoot = m_tree.GetRootItem();
		std::vector<CPropTreeItem*> ancestors;

		for (CPropTreeItem* p = pItem->GetParent(); p && p != pRoot; p = p->GetParent())
			ancestors.push_back(p);

		for (std::vector<CPropTreeItem*>::size_type i = ancestors.size(); i-- > 0;)
			CloseOtherGroups(ancestors[i]);
	}

	// Closing groups made the tree shorter and may have left the offset that
	// SetFocusedItem() scrolled to past the row, so scroll again. EnsureVisible() also
	// re-expands the ancestors, which is harmless: they are open already.
	m_tree.EnsureVisible(pItem);

	// CPropTreeInfo::OnPaint() reads GetFocusedItem() and draws its label and info
	// text, so the matched row explains itself in the info pane once repainted - no
	// new drawing code, only the repaint RedrawTree() already does for a value change.
	RedrawTree();

	return TRUE;
}

}; // namespace Common
