/* 
	SQLTools is a tool for Oracle database developers and DBAs.
	Copyright (C) 1997-2026 Aleksey Kochetov

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
#include <algorithm>
#include "OESnippetsWnd.h"

#include "OpenEditor/OEDocument.h"
#include "OpenEditor/OEView.h"
#include "OpenEditor/OESettings.h"
#include "OpenEditor/OETemplatesDlg.h"
#include "OpenEditor/OETemplatesPage.h"
#include <COMMON/AppUtilities.h>
#include <COMMON/PropertySheetMem.h>
#include "OpenEditorInc.rh"
#ifndef OE_NO_HELP
#include "Help/HTMLDefines.h"
#endif // OE_NO_HELP

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

using namespace OpenEditor;
using namespace Common;

namespace {

// The status strip, in 96dpi pixels. Scaled rather than fixed because its text is
// drawn in a scaled font and a strip that does not grow with it clips the descenders.
const int STATUS_HEIGHT = 18;

const COLORREF COLOR_FRESH = RGB(  0,   0,   0);
const COLORREF COLOR_NONE  = RGB(100, 100, 100);

int StatusHeight ()
{
	return CSnippetListWnd::ScaleMetric(STATUS_HEIGHT);
}

// True when the group holds at least one entry the pane would card: deleted ones stay in
// the vector until save, and excluded ones count only while the pane is showing them.
bool HasLiveEntries (const TemplatePtr& tmpl, bool showExcluded)
{
	if (!tmpl)
		return false;

	const std::vector<Template::Entry>& entries = tmpl->GetEntries();

	for (size_t i = 0, n = entries.size(); i < n; i++)
		if (!entries[i].deleted && (showExcluded || !entries[i].excludedFromSnippets))
			return true;

	return false;
}

// The language class of a name, or a null pointer. SettingsManager::FindByName throws on
// a miss; a class the settings do not describe is a fallback, not an error.
ClassSettingsPtr FindClassByName (const SettingsManager& mgr, const std::string& name)
{
	if (name.empty())
		return ClassSettingsPtr();

	for (int i = 0, n = mgr.GetClassCount(); i < n; i++)
	{
		ClassSettingsPtr pClass = mgr.GetClassByPos(i);

		if (pClass && pClass->GetName() == name)
			return pClass;
	}

	return ClassSettingsPtr();
}

// Alphabetical by the card's header, case insensitively, the way CVisualAttributesPageTree
// sorts its font names. No tie-break: names are unique per language, and the sort is stable.
bool HeaderLess (const SnippetItem& a, const SnippetItem& b)
{
	return _wcsicmp(a.header.c_str(), b.header.c_str()) < 0;
}

// The keyword and where its emphasis goes, returned together so the two cannot disagree:
// [0,split) is what has to be typed before it expands, per ExpandKeyword()'s minLength.
std::wstring FormatBadge (const std::string& keyword, int minLength, size_t& split)
{
	const std::wstring s = wstr(keyword);

	// A typed character is the least that can match, so 0 - the default, and what a missing
	// MinLength reads as - emphasises one character rather than none of the keyword at all.
	const size_t n = (size_t)std::max(1, minLength);

	split = std::min(n, s.length());

	return s;
}

} // unnamed namespace

///////////////////////////////////////////////////////////////////////////////

OESnippetsWnd::OESnippetsWnd ()
{
}

OESnippetsWnd::~OESnippetsWnd ()
{
}

BOOL OESnippetsWnd::Create (CWnd* pParent, UINT nID)
{
	static LPCTSTR pszClass = AfxRegisterWndClass(
		CS_HREDRAW | CS_VREDRAW,
		::LoadCursor(NULL, IDC_ARROW),
		(HBRUSH)(COLOR_BTNFACE + 1),
		NULL);

	return CWnd::Create(pszClass, NULL,
		WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
		CRect(0, 0, 0, 0), pParent, nID);
}

BEGIN_MESSAGE_MAP(OESnippetsWnd, CWnd)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_WM_SHOWWINDOW()
	ON_MESSAGE(WM_SQLT_SNIPPETLIST, OnSnippetListNotify)
	ON_MESSAGE_VOID(WM_IDLEUPDATECMDUI, OnIdleUpdateCmdUI)
	ON_MESSAGE(WM_COMMANDHELP, OnCommandHelp)
END_MESSAGE_MAP()

int OESnippetsWnd::OnCreate (LPCREATESTRUCT lpcs)
{
	if (CWnd::OnCreate(lpcs) == -1)
		return -1;

	// Before the children, so the list is handed the editor's fonts at creation and never
	// builds its own fallbacks. NULL means BuildFonts() found no "Text" attribute to copy.
	BuildFonts();

	if (!m_list.Create(CRect(0, 0, 0, 0), this, AFX_IDW_PANE_FIRST))
		return -1;

	m_list.SetHeaderFont(m_headerFont.GetSafeHandle() ? &m_headerFont : NULL);
	m_list.SetBodyFont  (m_bodyFont.GetSafeHandle()   ? &m_bodyFont   : NULL);

	if (!m_status.Create(NULL, WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
			CRect(0, 0, 0, StatusHeight()), this, AFX_IDW_PANE_FIRST + 1))
		return -1;

	m_status.ModifyStyle(0, SS_OWNERDRAW);
	m_status.AlignLeft();

	// The stock GUI font, not GetFont(): this pane was never sent WM_SETFONT, so GetFont()
	// returns NULL and CWnd::SetFont() dereferences its argument before it sends anything.
	m_status.SetFont(CFont::FromHandle((HFONT)::GetStockObject(DEFAULT_GUI_FONT)));

	Populate();

#ifndef OE_NO_HELP
	SetWindowContextHelpId(HID_VIEW_SNIPPETS);
	m_list.SetWindowContextHelpId(HID_VIEW_SNIPPETS);
#endif // !OE_NO_HELP

	return 0;
}

void OESnippetsWnd::OnSize (UINT nType, int cx, int cy)
{
	CWnd::OnSize(nType, cx, cy);

	if (!m_list.GetSafeHwnd() || cx <= 0 || cy <= 0)
		return;

	const int sh = StatusHeight();
	const int lh = max(0, cy - sh);

	m_list.SetWindowPos  (nullptr, 0, 0,  cx, lh, SWP_NOZORDER | SWP_NOACTIVATE);
	m_status.SetWindowPos(nullptr, 0, lh, cx, sh, SWP_NOZORDER | SWP_NOACTIVATE);
}

void OESnippetsWnd::OnShowWindow (BOOL bShow, UINT nStatus)
{
	CWnd::OnShowWindow(bShow, nStatus);

	// Shown means re-read: the template dialog, the settings sheet and a workspace load can
	// all have changed the collection, and there is no notification to hang a refresh on.
	if (bShow)
		Refresh();
}

void OESnippetsWnd::OnDestroy ()
{
	// The list holds the two font pointers, and a font deleted under a window that still
	// paints with it is a crash rather than a fallback. NULL makes it build its own again.
	if (m_list.GetSafeHwnd())
	{
		m_list.SetHeaderFont(NULL);
		m_list.SetBodyFont(NULL);
	}

	CWnd::OnDestroy();
}

void OESnippetsWnd::SetStatus (LPCTSTR text, COLORREF color)
{
	if (!m_status.GetSafeHwnd())
		return;

	m_status.SetText(text, color);
}

void OESnippetsWnd::Refresh ()
{
	BuildFonts();

	if (m_list.GetSafeHwnd())
	{
		m_list.SetHeaderFont(m_headerFont.GetSafeHandle() ? &m_headerFont : NULL);
		m_list.SetBodyFont  (m_bodyFont.GetSafeHandle()   ? &m_bodyFont   : NULL);
	}

	Populate();
}

///////////////////////////////////////////////////////////////////////////////
// Fonts

void OESnippetsWnd::BuildFonts ()
{
	const SettingsManager& mgr = COEDocument::GetSettingsManager();

	// The language whose "Text" attribute the cards are drawn with: the active document's,
	// and the first class when no editor is open. The attributes are per language class.
	ClassSettingsPtr pClass;

	// Not mgr.FindByName(), which throws on a miss - see FindClassByName.
	if (COEDocument* pDoc = GetActiveDocument())
		pClass = FindClassByName(mgr, pDoc->GetSettings().GetName());

	if (!pClass && mgr.GetClassCount() > 0)
		pClass = mgr.GetClassByPos(0);

	if (!pClass)
		return;

	const VisualAttributesSet& vaset = pClass->GetVisualAttributesSet();

	// Walked rather than FindByName("Text"): that throws std::out_of_range when the set has
	// no such attribute, and a hand-edited settings file is not worth an exception frame.
	const VisualAttribute* pAttr = nullptr;

	for (int i = 0, n = vaset.GetCount(); i < n; i++)
		if (vaset[i].m_Name == "Text")
		{
			pAttr = &vaset[i];
			break;
		}

	if (!pAttr)
		return;

	LOGFONT lf;
	memset(&lf, 0, sizeof(lf));

	// PointToPixel, the conversion VisualAttribute::NewFont() uses, so a card gets the
	// same pixel height the editor gives the same point size at this dpi.
	lf.lfHeight         = -VisualAttribute::PointToPixel(pAttr->m_FontSize) + 2;
	lf.lfCharSet        = DEFAULT_CHARSET;
	lf.lfOutPrecision   = OUT_DEFAULT_PRECIS;
	lf.lfClipPrecision  = CLIP_DEFAULT_PRECIS;
	lf.lfQuality        = DEFAULT_QUALITY;
	lf.lfPitchAndFamily = FIXED_PITCH;
	lf.lfItalic         = pAttr->m_FontItalic ? 1 : 0;
	lf.lfUnderline      = pAttr->m_FontUnderline ? 1 : 0;
	_tcsncpy(lf.lfFaceName, wstr(pAttr->m_FontName).c_str(), LF_FACESIZE - 1);

	// Recreated rather than reused: CreateFontIndirect on a CFont that still holds a handle
	// fails instead of replacing it, and the settings sheet can bring back a different face.
	lf.lfWeight = pAttr->m_FontBold ? FW_BOLD : FW_NORMAL;

	if (m_bodyFont.GetSafeHandle())
		m_bodyFont.DeleteObject();
	m_bodyFont.CreateFontIndirect(&lf);

	// Headers take the stock GUI font, not a shrunk "Text" face: a card name is chrome, and
	// a fixed-pitch code face reads too wide for one. Copied, not Attach()ed - this owns it.
	if (m_headerFont.GetSafeHandle())
		m_headerFont.DeleteObject();

	const HFONT hStockGui = (HFONT)::GetStockObject(DEFAULT_GUI_FONT);

	LOGFONT lfGui;
	memset(&lfGui, 0, sizeof(lfGui));

	if (!hStockGui || !::GetObject(hStockGui, sizeof(lfGui), &lfGui))
	{
		// Near-dead: GetObject() fails only on a bad handle. Kept so a failure cannot leave
		// m_headerFont handle-less and drop the header onto the control's own fallback.
		lf.lfWeight = FW_BOLD;

		if (lf.lfHeight < 0)
			lf.lfHeight += 2;

		m_headerFont.CreateFontIndirect(&lf);
	}
	else
	{
		//lfGui.lfHeight -= 2;
		m_headerFont.CreateFontIndirect(&lfGui);
	}
}

///////////////////////////////////////////////////////////////////////////////
// Content

void OESnippetsWnd::Populate ()
{
	// Taken before the walk below, so a mutation landing during a rebuild is seen as a
	// further change on the next tick rather than absorbed by this one.
	m_generation = TemplateCollection::GetGeneration();

	const SettingsManager& mgr = COEDocument::GetSettingsManager();
	const TemplateCollection& coll = mgr.GetTemplateCollection();

	// Which language's snippets to show, decided up front because the fallback has to be
	// known then: a class with no group of its own shows every language, not an empty pane.
	m_language = CurrentLanguage();

	std::string language = m_language;

	// mgr.GetTemplateByName rather than TemplateCollection::Find: the latter throws when
	// the document's language has no group, and a null reads as "nothing to show".
	if (m_showAllLanguages || language.empty()
	    || !HasLiveEntries(mgr.GetTemplateByName(language), m_showExcluded))
		language.clear();

	// What was open, by name, so a refresh does not close it. Row indices are no use as the
	// key: the list is rebuilt.
	std::map<std::wstring, bool> wasExpanded;

	for (int i = 0, n = m_list.GetItemCount(); i < n; i++)
		if (m_list.IsExpanded(i))
		{
			const SnippetItem& old = m_list.GetItem(i);
			wasExpanded[old.group + L"|" + old.header] = true;
		}

	std::vector<SnippetItem> items;

	m_payloads.clear();

	int nLangs    = 0;
	int nExcluded = 0;
	int nHidden   = 0;

	// The collection is a map, so the languages come out in name order and one language's
	// cards are contiguous - which is what makes the list emit one caption row per language.
	for (TemplateCollection::ConstIterator it = coll.Begin(); it != coll.End(); ++it)
	{
		const TemplatePtr& tmpl = it->second;

		if (!tmpl)
			continue;

		if (!language.empty() && it->first != language)
			continue;

		const std::wstring lang = wstr(it->first);
		const std::vector<Template::Entry>& entries = tmpl->GetEntries();

		// Where this language's cards start, so they can be sorted as one block: sorting the
		// whole vector would reorder the languages too, and they are already alphabetical.
		const size_t nGroupStart = items.size();

		bool counted = false;

		for (size_t e = 0, ne = entries.size(); e < ne; e++)
		{
			const Template::Entry& entry = entries[e];

			// An entry marked deleted stays in the vector until the collection is saved;
			// showing it would offer a snippet the settings no longer have.
			if (entry.deleted)
				continue;

			// Opted out of the pane. Kept as a dimmed card while the user has asked to see
			// excluded entries, because dropping it would take the undo command with it.
			if (entry.excludedFromSnippets && !m_showExcluded)
			{
				nHidden++;
				continue;
			}

			SnippetItem item;

			item.group  = lang;
			item.header = wstr(entry.name);
			item.badge  = FormatBadge(entry.keyword, entry.minLength, item.badgeSplit);
			item.body   = wstr(entry.text);
			item.dimmed = entry.excludedFromSnippets;

			// dragText is left empty, so the list carries the body verbatim. The {code} marker
			// travels beside it as its own clipboard format: the editor looks for a format, not a prefix.
			item.data = (LPARAM)m_payloads.size();

			// Identity, not the group and the index: the group is found again when a command runs,
			// so a card either keeps working across a rebuild or stops working visibly.
			Payload payload;
			payload.language = it->first;
			payload.uuid     = entry.uuid;
			m_payloads.push_back(payload);

			if (item.dimmed)
				nExcluded++;

			items.push_back(item);
			counted = true;
		}

		// Alphabetical within the language. Per group, because a global sort would interleave the
		// languages and caption every card. Safe for insertion: data travels with its own card.
		if (m_sortByName && items.size() - nGroupStart > 1)
			std::stable_sort(items.begin() + nGroupStart, items.end(), HeaderLess);

		if (counted)
			nLangs++;
	}

	// The scroll offset too, so switching away from the pane and back does not send the
	// user to the top of a list they had already scrolled down.
	const int nScroll = m_list.GetScrollOffset();

	if (m_list.GetSafeHwnd())
		m_list.SetItems(items);

	// Re-open what was open. SetExpanded() relayouts per call, so this is quadratic - a few
	// hundred rows of arithmetic once per refresh, not worth a second way to set expansion.
	for (int i = 0, n = m_list.GetItemCount(); i < n; i++)
	{
		const SnippetItem& item = m_list.GetItem(i);

		if (wasExpanded.count(item.group + L"|" + item.header))
			m_list.SetExpanded(i, true);
	}

	// After the expansion is restored, not before: restoring it changes the content
	// height, and the offset has to be clamped against the height the list ends up with.
	if (m_list.GetSafeHwnd())
		m_list.SetScrollOffset(nScroll);

	if (items.empty())
	{
		// In the pane rather than a message box: an empty list is a state the user can read
		// while they go and fix it. Split, because the two have different remedies.
		if (nHidden)
			m_summary = L"All snippets excluded - turn on \"Show excluded\"";
		else
			m_summary = L"No templates defined - use Settings... to add them";
	}
	else if (!language.empty())
	{
		// Named, because a filtered list that does not say what it was filtered by reads as the
		// whole collection.
		m_summary.Format(L"%d %s snippet%s",
			(int)items.size(), wstr(language).c_str(), items.size() == 1 ? L"" : L"s");
	}
	else
	{
		m_summary.Format(L"%d snippet%s in %d language%s",
			(int)items.size(), items.size() == 1 ? L"" : L"s",
			nLangs,            nLangs == 1 ? L"" : L"s");
	}

	// Only ever nonzero while excluded entries are being carded, so this cannot report
	// cards that are not there. One suffix rather than three, hence SetStatus moved down.
	if (nExcluded)
	{
		CString suffix;
		suffix.Format(L", %d excluded", nExcluded);
		m_summary += suffix;
	}

	SetStatus(m_summary, items.empty() ? COLOR_NONE : COLOR_FRESH);
}

///////////////////////////////////////////////////////////////////////////////
// The editor this pane acts on

COEDocument* OESnippetsWnd::GetActiveDocument ()
{
	// The same walk COEDocument::ShowTemplateDialog does: the active MDI child's document, or
	// the frame's when the app is not running MDI. Not shared - that one is private and static.
	CWnd* pWnd = AfxGetMainWnd();

	if (!pWnd)
		return nullptr;

	CDocument* pDoc = nullptr;

	if (pWnd->IsKindOf(RUNTIME_CLASS(CMDIFrameWnd)))
	{
		if (CMDIChildWnd* pChild = ((CMDIFrameWnd*)pWnd)->MDIGetActive())
			pDoc = pChild->GetActiveDocument();
	}
	else if (pWnd->IsKindOf(RUNTIME_CLASS(CFrameWnd)))
		pDoc = ((CFrameWnd*)pWnd)->GetActiveDocument();

	return dynamic_cast<COEDocument*>(pDoc);
}

std::string OESnippetsWnd::CurrentLanguage ()
{
	// The document's class name, which is the key the template collection is stored under.
	// Not Settings::GetLanguage(): that is the lexer's name, a different string for one document.
	COEDocument* pDoc = GetActiveDocument();

	return pDoc ? pDoc->GetSettings().GetName() : std::string();
}

void OESnippetsWnd::OnIdleUpdateCmdUI ()
{
	// WM_IDLEUPDATECMDUI is broadcast to permanent descendants whether the pane is on screen
	// or not. Hidden there is nothing to do: OnShowWindow(TRUE) re-reads unconditionally.
	if (!IsWindowVisible())
		return;

	// The store first. An edit made somewhere else - the editor's own template dialog, a
	// settings load, a merge at startup - is one nobody would think to forward here.
	if (TemplateCollection::GetGeneration() != m_generation)
	{
		Refresh();
		return;
	}

	// Only the language's value is compared and no document pointer is cached, so a closed
	// document simply changes the answer to "". Polled, because a language change fires nothing.
	const std::string language = CurrentLanguage();

	if (language == m_language)
		return;

	Refresh();
}

COEditorView* OESnippetsWnd::GetActiveEditorView ()
{
	COEDocument* pDoc = GetActiveDocument();

	if (!pDoc)
		return nullptr;

	// The document's first view, which is what CodeOutlineWnd::NavigateToItem uses. A split
	// editor would make "the" view ambiguous; the app does not split.
	POSITION pos = pDoc->GetFirstViewPosition();

	while (pos)
	{
		COEditorView* pView = dynamic_cast<COEditorView*>(pDoc->GetNextView(pos));

		if (pView && pView->GetSafeHwnd())
			return pView;
	}

	return nullptr;
}

///////////////////////////////////////////////////////////////////////////////
// Acting on a card

bool OESnippetsWnd::InsertSelected ()
{
	const int item = m_list.GetSelectedItem();

	if (item < 0 || item >= m_list.GetItemCount())
		return false;

	const LPARAM data = m_list.GetItemData(item);

	if (data < 0 || data >= (LPARAM)m_payloads.size())
		return false;

	// By value, not by reference: the Refresh() below rebuilds the vector this came from,
	// and a reference into it would not survive that.
	const Payload payload = m_payloads[(size_t)data];

	TemplatePtr tmpl;
	int index = -1;

	if (!ResolveEntry(payload, tmpl, index))
	{
		// The card describes an entry the settings no longer have. Rebuilding is the honest
		// answer: a stale index would insert the wrong snippet, and silence reads as ignored.
		SetStatus(L"Snippet has changed - list refreshed", COLOR_NONE);
		Refresh();
		return false;
	}

	COEditorView* pView = GetActiveEditorView();

	if (!pView)
	{
		// In the pane rather than a message box: a box would take the focus the user is about to
		// want back, and "no editor is open" is a state rather than an error.
		SetStatus(L"No editor to insert into", COLOR_NONE);
		return false;
	}

	// Activate before inserting, so the caret the snippet lands at is the one the user can
	// see move. CodeOutlineWnd::NavigateToItem does the same.
	if (CFrameWnd* pFrame = pView->GetParentFrame())
		pFrame->ActivateFrame();

	pView->SetFocus();

	if (!pView->DoInsertTemplateEntry(tmpl, index))
	{
		SetStatus(L"Cannot insert this snippet", COLOR_NONE);
		return false;
	}

	SetStatus(L"Inserted", COLOR_FRESH);
	return true;
}

void OESnippetsWnd::CopyName ()
{
	const int item = m_list.GetSelectedItem();

	if (item < 0 || item >= m_list.GetItemCount())
		return;

	const std::wstring& name = m_list.GetItem(item).header;

	if (name.empty())
		return;

	// The same open/empty/close sequence the list uses, because AppSetClipboardText only
	// calls SetClipboardData, which fails on a clipboard nobody has opened.
	if (OpenClipboard())
	{
		if (EmptyClipboard())
			AppSetClipboardText(name.c_str(), (int)name.length(), CF_UNICODETEXT);

		CloseClipboard();
	}
}

///////////////////////////////////////////////////////////////////////////////
// Template commands
//
// Add, edit and delete go straight to COETemplatesDlg over a copy of the entry, so the
// pane is the whole interaction. Nothing is written until IDOK, and then in place.

// See the declaration for why this walks the attribute set instead of calling
// VisualAttributesSet::FindByName("Text") the way COETemplatesPage does.
VisualAttribute OESnippetsWnd::GetTextAttribute (const std::string& language)
{
	const SettingsManager& mgr = COEDocument::GetSettingsManager();

	ClassSettingsPtr pClass = FindClassByName(mgr, language);

	if (!pClass)
		if (COEDocument* pDoc = GetActiveDocument())
			pClass = FindClassByName(mgr, pDoc->GetSettings().GetName());

	if (!pClass && mgr.GetClassCount() > 0)
		pClass = mgr.GetClassByPos(0);

	if (pClass)
	{
		const VisualAttributesSet& vaset = pClass->GetVisualAttributesSet();

		for (int i = 0, n = vaset.GetCount(); i < n; i++)
			if (vaset[i].m_Name == "Text")
				return vaset[i];
	}

	// Default constructed rather than nothing: COETemplatesDlg reads m_FontSize and
	// m_FontName without checking, so it has to be handed an attribute to copy.
	return VisualAttribute();
}

bool OESnippetsWnd::GetSelectedPayload (Payload& out)
{
	const int item = m_list.GetSelectedItem();

	if (item < 0 || item >= m_list.GetItemCount())
		return false;

	const LPARAM data = m_list.GetItemData(item);

	if (data < 0 || data >= (LPARAM)m_payloads.size())
		return false;

	// By value, as InsertSelected() takes it: every one of these commands ends in a
	// refresh, which clears and refills the vector this copy came from.
	out = m_payloads[(size_t)data];
	return true;
}

bool OESnippetsWnd::ResolveEntry (const Payload& payload, TemplatePtr& tmpl, int& index)
{
	tmpl.reset();
	index = -1;

	if (payload.uuid.empty())
		return false;

	// GetTemplateByName reports a miss by returning null instead of throwing, where
	// TemplateCollection::Find throws when the language has no group.
	tmpl = COEDocument::GetSettingsManager().GetTemplateByName(payload.language);

	if (!tmpl)
		return false;

	const std::vector<Template::Entry>& entries = tmpl->GetEntries();

	for (size_t i = 0, n = entries.size(); i < n; i++)
		// Entry::operator==(const string&) compares the uuid - the same test the streamer uses to
		// match a saved node to its entry. Deleted ones are skipped: Populate() never cards them.
		if (!entries[i].deleted && entries[i] == payload.uuid)
		{
			index = (int)i;
			return true;
		}

	// Not found: hand back a null group, so a caller that ignores the return value
	// still cannot dereference a group it has no entry in.
	tmpl.reset();
	return false;
}

void OESnippetsWnd::EditTemplate ()
{
	Payload payload;

	if (!GetSelectedPayload(payload))
		return;

	TemplatePtr tmpl;
	int index = -1;

	if (!ResolveEntry(payload, tmpl, index))
	{
		SetStatus(L"Snippet has changed - list refreshed", COLOR_NONE);
		Refresh();
		return;
	}

	// A copy, and the dialog writes into the copy: OnOk assigns through the Entry& it was
	// handed, so Cancel is free and a bad name stays out of the live entry.
	Template::Entry entry = tmpl->GetEntry(index);

	// The index stays good across the dialog: it is modal, and COETemplatesDlg writes only
	// through the Entry& it was given.
	if (COETemplatesDlg(this, COETemplatesDlg::emEdit, *tmpl, entry,
	                    GetTextAttribute(payload.language)).DoModal() != IDOK)
		return;

	tmpl->UpdateEntry(index, entry);

	COEDocument::SaveSettingsManager();

	Refresh();
}

void OESnippetsWnd::ExcludeTemplate ()
{
	Payload payload;

	if (!GetSelectedPayload(payload))
		return;

	TemplatePtr tmpl;
	int index = -1;

	if (!ResolveEntry(payload, tmpl, index))
	{
		SetStatus(L"Snippet has changed - list refreshed", COLOR_NONE);
		Refresh();
		return;
	}

	// No dialog: one flag, and the card the user picked says which entry it applies to.
	// UpdateEntry is what marks the entry modified, so the writer below persists it.
	Template::Entry entry = tmpl->GetEntry(index);
	entry.excludedFromSnippets = !entry.excludedFromSnippets;

	tmpl->UpdateEntry(index, entry);

	COEDocument::SaveSettingsManager();

	Refresh();
}

void OESnippetsWnd::AddTemplate ()
{
	// The group to append to: the selected card's language when there is one, so adding from
	// a card of another language puts the snippet where the user is looking. Else the document's.
	std::string language;

	Payload payload;

	if (GetSelectedPayload(payload))
		language = payload.language;

	if (language.empty())
		language = CurrentLanguage();

	if (language.empty())
	{
		// In the pane, as with "No editor to insert into": no card and no document is a
		// state the user can read for themselves, not an error to be told about.
		SetStatus(L"No language to add a snippet to", COLOR_NONE);
		return;
	}

	SettingsManager& mgr = COEDocument::GetSettingsManagerForUpdate();

	// GetTemplateByName, and TemplateCollection::Add only when that came back empty: the
	// collection accessor is const-only, and Add throws if the group is already there.
	TemplatePtr tmpl = mgr.GetTemplateByName(language);

	if (!tmpl)
		tmpl = mgr.Add(language);

	if (!tmpl)
		return;

	Template::Entry entry;

	// Before the dialog, as OnBnClicked_AddTempl does: ValidateUniqueness forgives a repeated
	// name only when the uuid matches too, so a new entry needs one of its own first.
	Template::GenUUID(entry);

	// The same five arguments OnBnClicked_AddTempl passes, so the dialog validates the new
	// name against this group exactly as the Settings page does.
	if (COETemplatesDlg(this, COETemplatesDlg::emInsert, *tmpl, entry,
	                    GetTextAttribute(language)).DoModal() != IDOK)
		return;

	tmpl->AppendEntry(entry);

	COEDocument::SaveSettingsManager();
	Refresh();
}

void OESnippetsWnd::DeleteTemplate ()
{
	Payload payload;

	if (!GetSelectedPayload(payload))
		return;

	TemplatePtr tmpl;
	int index = -1;

	if (!ResolveEntry(payload, tmpl, index))
	{
		// Already gone somewhere else. Rebuild and say so, rather than put up a
		// confirmation for an entry that no longer exists.
		SetStatus(L"Snippet has changed - list refreshed", COLOR_NONE);
		Refresh();
		return;
	}

	Template::Entry entry = tmpl->GetEntry(index);

	// emDelete rather than a message box of our own: it is the confirmation
	// COETemplatesPage::OnBnClicked_DelTempl shows for this same action.
	if (COETemplatesDlg(this, COETemplatesDlg::emDelete, *tmpl, entry,
	                    GetTextAttribute(payload.language)).DoModal() != IDOK)
		return;

	// Marks the entry deleted in place; the writer drops it on the next save. Populate()
	// skips it meanwhile, so the refresh below is what makes the card go away.
	tmpl->DeleteEntry(index);

	COEDocument::SaveSettingsManager();
	Refresh();
}

void OESnippetsWnd::OnSettings ()
{
	SettingsManager mgr(COEDocument::GetSettingsManager());

	std::string category;

	if (COEDocument* pDoc = GetActiveDocument())
		category = pDoc->GetSettings().GetName();

	COETemplatesPage page(mgr, category.c_str());
	page.m_psp.pszTitle = L"Templates";
	page.m_psp.dwFlags |= PSP_USETITLE;

	static UINT gStarPage = 0;
	CPropertySheetMem sheet(L"Settings", gStarPage);
	sheet.m_psh.dwFlags |= PSH_NOAPPLYNOW;
	sheet.AddPage(&page);

	const bool saved = (sheet.DoModal() == IDOK);

	if (saved)
	{
		COEDocument::GetSettingsManagerForUpdate() = mgr;
		COEDocument::SaveSettingsManager();
	}

	// Unconditional: the sheet can have changed the collection and then been cancelled, and
	// even a Cancel leaves this pane reading a manager that is no longer the live one.
	Refresh();
}

void OESnippetsWnd::ShowContextMenu (CPoint ptScreen)
{
	CMenu menu;

	if (!menu.CreatePopupMenu())
		return;

	// Local ids, as in CodeOutlineWnd::OnContextMenu: the strings live here and so does
	// the switch, and a resource id would be a third place to keep in step with them.
	const UINT MID_INSERT     = 1;
	const UINT MID_COPY       = 2;
	const UINT MID_COPY_NAME  = 3;
	const UINT MID_EXPAND_ALL = 4;
	const UINT MID_COLLAPSE   = 5;
	const UINT MID_SORT_NAME  = 6;
	const UINT MID_ALL_LANGS  = 7;
	const UINT MID_EDIT       = 8;
	const UINT MID_REFRESH    = 9;
	const UINT MID_SETTINGS   = 10;
	const UINT MID_ADD        = 11;
	const UINT MID_DELETE     = 12;
	const UINT MID_EXCLUDE    = 13;
	const UINT MID_SHOW_EXCL  = 14;

	const int  item      = m_list.GetSelectedItem();
	const bool hasItems  = m_list.GetItemCount() > 0;
	const UINT flagsItem = MF_STRING | (item >= 0 ? MF_ENABLED : MF_GRAYED);
	const UINT flagsList = MF_STRING | (hasItems  ? MF_ENABLED : MF_GRAYED);

	// The exclude toggle mirrors the selected card's own flag, so its check mark says which
	// way the click will go rather than only that a click will do something.
	bool excludedSel = false;
	{
		Payload     payload;
		TemplatePtr tmpl;
		int         index = -1;

		if (GetSelectedPayload(payload) && ResolveEntry(payload, tmpl, index))
			excludedSel = tmpl->GetEntry(index).excludedFromSnippets;
	}

	menu.AppendMenu(flagsItem, MID_INSERT,     L"Insert at caret\t<Ctrl>+<Enter>, <DblClick>");
	menu.AppendMenu(flagsItem, MID_COPY,       L"Copy\t<Ctrl>+<C>");
	//menu.AppendMenu(flagsItem, MID_COPY_NAME,  L"Copy name");
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(flagsList, MID_EXPAND_ALL, L"Expand all");
	menu.AppendMenu(flagsList, MID_COLLAPSE,   L"Collapse all");

	// Add is the one of the three that does not need a card: with nothing selected it goes to
	// the active document's language, so it is grayed only when there is neither.
	const UINT flagsAdd = MF_STRING
		| ((item >= 0 || !CurrentLanguage().empty()) ? MF_ENABLED : MF_GRAYED);

	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(flagsAdd,  MID_ADD,        L"Add template...");
	menu.AppendMenu(flagsItem, MID_EDIT,       L"Edit template...");
	menu.AppendMenu(flagsItem, MID_DELETE,     L"Delete template...");
	menu.AppendMenu(flagsItem | (excludedSel ? MF_CHECKED : 0), MID_EXCLUDE, L"Exclude from snippets");

	menu.AppendMenu(MF_SEPARATOR);
	// menu.AppendMenu(flagsList | (m_sortByName ? MF_CHECKED : 0), MID_SORT_NAME, L"Sort by name");
	menu.AppendMenu(MF_STRING | (m_showExcluded ? MF_CHECKED : 0), MID_SHOW_EXCL, L"Show excluded");
	menu.AppendMenu(MF_STRING | (m_showAllLanguages ? MF_CHECKED : 0), MID_ALL_LANGS, L"Show all languages");

	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, MID_REFRESH,    L"Refresh");
	//menu.AppendMenu(MF_STRING, MID_SETTINGS,   L"Settings...");

	// TPM_RETURNCMD, so the id comes back from the call instead of arriving as a
	// WM_COMMAND the frame's accelerator table could match first.
	const UINT nCmd = menu.TrackPopupMenu(
		TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN,
		ptScreen.x, ptScreen.y, this);

	switch (nCmd)
	{
	case MID_INSERT:     InsertSelected();        break;
	case MID_COPY:       m_list.CopySelected();   break;
	case MID_COPY_NAME:  CopyName();              break;
	case MID_EXPAND_ALL: m_list.ExpandAll(true);  break;
	case MID_COLLAPSE:   m_list.ExpandAll(false); break;
	case MID_SORT_NAME:
		m_sortByName = !m_sortByName;
		Refresh();
		break;
	case MID_ALL_LANGS:
		// Rebuilt here rather than left to the idle tick: the tick compares the document's
		// language, which this does not change, so it would never notice.
		m_showAllLanguages = !m_showAllLanguages;
		Refresh();
		break;
	case MID_SHOW_EXCL:
		// Rebuilt here for the same reason: neither toggle moves the document's language or
		// the store's generation, so the idle tick would see nothing to do.
		m_showExcluded = !m_showExcluded;
		Refresh();
		break;
	case MID_EDIT:       EditTemplate();          break;
	case MID_EXCLUDE:    ExcludeTemplate();       break;
	case MID_DELETE:     DeleteTemplate();        break;
	case MID_ADD:        AddTemplate();           break;
	case MID_REFRESH:    Refresh();               break;
	case MID_SETTINGS:   OnSettings();            break;
	}
}

LRESULT OESnippetsWnd::OnSnippetListNotify (WPARAM wParam, LPARAM lParam)
{
	// TRUE from every case acted on: the list falls back to its own default when the reply is
	// 0, which for SLN_CONTEXTMENU would mean a second menu.
	switch (wParam)
	{
	case SLN_ACTIVATED:
		InsertSelected();
		return TRUE;

	case SLN_CONTEXTMENU:
		// Signed extraction rather than GET_X_LPARAM: windowsx.h is not included in this project,
		// and a screen coordinate is negative on a monitor that sits left of the primary.
		ShowContextMenu(CPoint((int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam)));
		return TRUE;

	case SLN_SELCHANGED:
		// Back to the summary, so a transient "Inserted" does not stay on the bar describing a
		// card the user has since clicked away from.
		SetStatus(m_summary, m_list.GetItemCount() ? COLOR_FRESH : COLOR_NONE);
		return TRUE;
	}

	return FALSE;
}

LRESULT OESnippetsWnd::OnCommandHelp (WPARAM, LPARAM lParam)
{
	if (lParam == 0)
		lParam = GetWindowContextHelpId();

	if (lParam != 0)
	{
		AfxGetApp()->WinHelpInternal(lParam);
		return TRUE;
	}

	return FALSE;
}

BOOL OESnippetsWnd::PreTranslateMessage (MSG* pMsg)
{
#ifndef OE_NO_HELP
	if (pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_F1
		&& (pMsg->hwnd == m_hWnd || ::IsChild(m_hWnd, pMsg->hwnd)))
	{
		return TRUE; // swallows F1 — WM_COMMANDHELP is never generated
	}
#endif // !OE_NO_HELP

	return CWnd::PreTranslateMessage(pMsg);
}

////////////////////////////////////////////////////////////////////////////////
// Exception frames
//
// Every handler runs inside one of these two, so none needs its own try block: messages
// arrive through WindowProc, commands and UI updates through OnCmdMsg.

LRESULT OESnippetsWnd::WindowProc (UINT message, WPARAM wParam, LPARAM lParam)
{
	try {
		EXCEPTION_FRAME;

		return CWnd::WindowProc(message, wParam, lParam);
	}
	_OE_DEFAULT_HANDLER_;

	return 0;
}

BOOL OESnippetsWnd::OnCmdMsg (UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo)
{
	try {
		EXCEPTION_FRAME;

		return CWnd::OnCmdMsg(nID, nCode, pExtra, pHandlerInfo);
	}
	_OE_DEFAULT_HANDLER_;

	return TRUE;
}
