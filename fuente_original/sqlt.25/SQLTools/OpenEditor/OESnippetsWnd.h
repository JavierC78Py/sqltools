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

#pragma once

#include <string>
#include <vector>

#include "Common/SnippetListWnd.h"
#include "Common/LabelWithWaitBar.h"
#include "Common/VisualAttributes.h"	// VisualAttribute
#include "OpenEditor/OETemplates.h"		// TemplatePtr, TemplateCollection

class COEDocument;
class COEditorView;

// OESnippetsWnd — dockable pane that lists the user's code templates as cards.
//
// Layout:  [  Common::CSnippetListWnd (fills, resizes)  ]
//          [  LabelWithWaitBar         (fixed height)   ]
//
// The list owns the painting, the scrolling, the selection and the clipboard; this
// class owns the meaning. It turns the TemplateCollection into cards, hands the
// list the editor's own "Text" font so a snippet looks like the code it becomes,
// and answers the list's notifications: activate means "expand this entry at the
// caret of the active editor", which is the same EditContext::ExpandTemplate call
// that typing the keyword would have made.
//
// The active document is resolved when it is needed rather than cached. MainFrm pushes
// the active document into CodeOutlineWnd because that pane's whole content belongs to
// one document; this pane's content belongs to the settings, which no document owns.
//
// The document still decides two things. The caret an activation inserts at is always
// the current one, and the list is filtered to the current document's language - the
// template collection is keyed by the document class name, so a snippet from another
// class is one the editor cannot expand where the caret is. That filter is re-checked
// on the idle tick rather than hooked, because the language can change under a document
// that stays open: the editor's own properties dialog has a Language combo, and changing
// it fires nothing MainFrm could forward. See CurrentLanguage() and Populate().
//
class OESnippetsWnd : public CWnd
{
public:
	OESnippetsWnd ();
	virtual ~OESnippetsWnd ();

	BOOL Create (CWnd* pParent, UINT nID);

	// Re-reads the collection and rebuilds the cards. Nothing notifies this pane, so it
	// re-reads; the idle tick polls the store's generation to decide when that is needed.
	void Refresh ();

private:
	// Which entry a card stands for: the collection key plus the entry's uuid. Identity
	// rather than position or pointer, so nothing held here can dangle across a rebuild.
	struct Payload
	{
		std::string language;
		std::string uuid;	// Template::Entry::uuid
	};

	Common::CSnippetListWnd m_list;
	LabelWithWaitBar        m_status;

	std::vector<Payload> m_payloads;	// indexed by SnippetItem::data

	CFont m_bodyFont;		// the editor's "Text" face
	CFont m_headerFont;		// the stock GUI font: card names are chrome, not code

	// The "n snippets in m languages" line, kept so a transient message can be cleared by
	// the next selection change instead of staying on the bar until the pane is rebuilt.
	CString m_summary;

	// The document language the list was last built for, and the user's override of the
	// filter it implies. Compared on every idle tick, so a switch of either re-fills.
	std::string m_language;
	bool        m_showAllLanguages = false;

	// The other filter's override: entries opted out of the pane. Off, they are invisible;
	// on, they are carded dimmed, which is what makes the opt-out undoable from here.
	bool        m_showExcluded = false;

	// The order one language's cards appear in: alphabetical by name, or the collection's
	// own curated order. On by default, because this pane is read as a browser.
	bool        m_sortByName = true;

	// The generation this list was built from. The idle tick compares it with the store's,
	// so an edit made anywhere else re-fills the pane without anyone having to say so.
	OpenEditor::TemplateCollection::Generation m_generation = 0;

	void Populate ();
	void BuildFonts ();
	void SetStatus (LPCTSTR text, COLORREF color);

	bool InsertSelected ();
	void CopyName ();

	// The three template commands. Each drives COETemplatesDlg directly: the route through
	// COEDocument::ShowTemplateDialog ignores its TemplatePtr and hosts a property sheet.
	void EditTemplate ();
	void AddTemplate ();
	void DeleteTemplate ();

	// Not one of the three: it flips a single flag, so it writes the entry rather than asks.
	void ExcludeTemplate ();

	// Resolves the selected card to its payload. False when nothing is selected, or when
	// the list and the payload vector have drifted apart.
	bool GetSelectedPayload (Payload& out);

	// Turns a payload back into the live group and the index ExpandKeyword wants, by looking
	// both up. False when the card has outlived its entry: callers rebuild, not report.
	bool ResolveEntry (const Payload& payload, OpenEditor::TemplatePtr& tmpl, int& index);

	void OnSettings ();
	void ShowContextMenu (CPoint ptScreen);

	// Both static: they read the MDI state and nothing from this pane, and the pane
	// deliberately does not cache what they return.
	static COEDocument*  GetActiveDocument ();
	static COEditorView* GetActiveEditorView ();

	// The active document's class name - "SQL", "Text" - the key the template collection is
	// stored under, and what COEDocument::ShowSettingsDialog() hands to COETemplatesPage.
	static std::string CurrentLanguage ();

	// The "Text" attribute of a language class, by value, for COETemplatesDlg's code face.
	// The card's own language, not the document's, so a snippet is edited as it will read.
	static Common::VisualAttribute GetTextAttribute (const std::string& language);

	DECLARE_MESSAGE_MAP()
	afx_msg int  OnCreate (LPCREATESTRUCT lpcs);
	afx_msg void OnSize (UINT nType, int cx, int cy);
	afx_msg void OnDestroy ();
	afx_msg void OnShowWindow (BOOL bShow, UINT nStatus);

	// The tick CodeOutlineWnd polls on. Re-fills the pane when the document's language
	// has moved since the last build; a string compare when it has not.
	afx_msg void OnIdleUpdateCmdUI ();

	afx_msg LRESULT OnSnippetListNotify (WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnCommandHelp (WPARAM, LPARAM);

	// The frame every handler runs inside: messages arrive through WindowProc, commands and
	// UI updates through OnCmdMsg, so none of the handlers above needs its own try block.
	virtual LRESULT WindowProc (UINT message, WPARAM wParam, LPARAM lParam);
	virtual BOOL OnCmdMsg (UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo);

	// F1, answered here so that it stays this pane's business instead of climbing to the
	// frame, whose own help id resolves to main_window.htm. See the implementation.
	virtual BOOL PreTranslateMessage (MSG* pMsg);
};
