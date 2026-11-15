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

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __SnippetListWnd_h__
#define __SnippetListWnd_h__

#include <string>
#include <vector>

// Forward declared rather than included: gdiplus.h has to come after afxwin.h, so
// it belongs in the .cpp. Every GDI+ file in this project does it that way.
namespace Gdiplus { class Graphics; }

namespace Common {

////////////////////////////////////////////////////////////////////////////////
// What the list tells its parent. Sent in wParam of WM_SQLT_SNIPPETLIST.
//
// One message with a code in wParam rather than a message per verb, for the same
// reason CPropTreeSearchBar does it: the pane handles them in one place and the
// codes read as a list of the control's verbs.
enum SnippetListNotify
{
    // The selection moved. lParam is the item index, -1 when nothing is selected.
    SLN_SELCHANGED   = 1,

    // A card was expanded or collapsed by the user. lParam is the item index; the
    // new state is read back with IsExpanded(), so the pane never has to decode it
    // from the message.
    SLN_ITEMEXPANDED = 2,

    // The user asked for the item to be used: double click, Ctrl+Enter, or the
    // pane's own menu. lParam is the item index. The list does not know what
    // "use" means - for the snippet pane it is "insert at the caret".
    SLN_ACTIVATED    = 3,

    // Right button up over a card. lParam is the SCREEN point packed with
    // MAKELONG, because a menu has to be tracked in screen coordinates and the
    // pane is the one that owns the strings to put in it.
    // The one code whose reply matters - see Notify().
    SLN_CONTEXTMENU  = 4,
};

// WM_APP + 0x23 is the next free slot: 0x21 is WM_SQLT_SEARCH_FIRST_HIT in
// PropertySheetMem.cpp and 0x22 is WM_SQLT_SEARCHBAR in PropTreeSearchBar.h. The
// WM_APP + 41/42 and WM_APP + 0x46 elsewhere in the tree go to windows of other
// classes - property pages and the find dialog - and never reach a pane hosting
// this list.
const UINT WM_SQLT_SNIPPETLIST = WM_APP + 0x23;

////////////////////////////////////////////////////////////////////////////////
// One card. The control owns no semantics beyond text: the pane decides what a
// badge means, what gets dragged and what `data` points at.
struct SnippetItem
{
    // Section caption. A group header row is emitted wherever this differs from
    // the previous item's, so the caller has to keep the items of one group
    // together.
    std::wstring group;

    std::wstring header;    // first line, one line, ellipsised if too wide
    std::wstring badge;     // right-aligned in the header font, may be empty
    std::wstring body;      // the payload; '\n' separated lines, nothing wraps
    std::wstring dragText;  // what a drag or a copy carries; body when empty

    LPARAM data = 0;        // pane payload, passed back untouched

    // [0, badgeSplit) of `badge` is drawn bold and in the emphasis colour, the rest plain in
    // the badge's; 0 means one weight and one colour. A hint like `dimmed`, not a meaning.
    size_t badgeSplit = 0;

    // Painted in the muted palette. A presentation hint like `badge`: the control does
    // not know why a card is out of play, only that the parent wants it read that way.
    bool dimmed = false;

    const std::wstring& PayloadText () const { return dragText.empty() ? body : dragText; }
};

// What the pointer is over. The parts behave differently: the chevron and the
// header toggle, the body does not - clicking a body only selects and arms a drag.
enum SnippetPart
{
    SNP_NONE = 0,
    SNP_CHEVRON,
    SNP_HEADER,
    SNP_BODY,
};

struct SnippetHit
{
    int         item = -1;   // -1 when the point is not on a card
    SnippetPart part = SNP_NONE;
};

////////////////////////////////////////////////////////////////////////////////
// A vertical list of rounded cards, each expandable to show a multiline body.
//
// A custom CWnd and not a list or a tree control, because neither can do the one
// thing this is for: a variable-height row that is a card - border, hover tint, a
// badge on the right of the header and a monospaced body below it. Owner-draw
// against CListCtrl or CTreeCtrl would mean fighting their item rect and their
// scroll model to get there.
//
// Everything is painted; there are no child windows. Only the rows that intersect
// the client rect are drawn, and their tops come from a prefix-sum vector, so a
// large list costs the same per frame as a small one.
//
// It is deliberately dumb: it owns the rows, the expansion state, the selection,
// the scroll position and the clipboard/drag payload. What an activation means
// stays in the parent, which drives it with SetItems()/SetExpanded() and reads it
// back through the accessors.
class CSnippetListWnd : public CWnd
{
public:
    CSnippetListWnd ();
    virtual ~CSnippetListWnd ();

    BOOL Create (const CRect& rc, CWnd* pParentWnd, UINT nID);

    // ---- fonts ----
    // Not owned, and must outlive the control. Passing NULL leaves the internal
    // fallback in place - a bold system message font for headers and a fixed-pitch
    // font for bodies - so the control is usable before a pane has worked out
    // which editor attributes to hand it.
    void SetHeaderFont (CFont* pFont);
    void SetBodyFont   (CFont* pFont);

    // ---- content ----
    // Replaces the whole list and resets expansion, selection and scroll.
    void SetItems (const std::vector<SnippetItem>& items);
    void RemoveAllItems ();

    int  GetItemCount () const               { return (int)m_vecItems.size(); }
    const SnippetItem& GetItem (int i) const { return m_vecItems.at(i); }
    LPARAM GetItemData (int i) const         { return m_vecItems.at(i).data; }

    // ---- selection ----
    int  GetSelectedItem () const;
    void SetSelectedItem (int item, bool scrollIntoView = true);
    void EnsureVisible (int item);

    // The scroll offset in content coordinates, so a caller that rebuilds the list can
    // put the view back where the user left it. SetItems() resets it, and a pane that
    // re-reads its data every time it is shown would otherwise lose the user's place on
    // every tab switch. The setter clamps, so a list that has just shrunk cannot be left
    // scrolled past its own end.
    int  GetScrollOffset () const          { return m_nScrollPos; }
    void SetScrollOffset (int nOffset)     { ScrollTo(nOffset); }

    // ---- expansion ----
    bool IsExpanded (int item) const;
    void SetExpanded (int item, bool expanded);
    void ExpandAll (bool expanded);

    // ---- copy ----
    // Puts the payload of the selected item on the clipboard as CF_UNICODETEXT
    // plus the {code} marker the editor's paste path re-indents by. False when
    // nothing is selected or the clipboard will not open.
    bool CopySelected ();

    // Which part of which card a client point is over. Public because the pane
    // needs it to decide what to put in the context menu it tracks itself.
    SnippetHit HitTest (CPoint ptClient) const;

    // Scales a 96dpi metric to the scale the control is drawn at, so every pixel
    // constant here grows with the font instead of clipping it. Static because it
    // reads nothing from the instance. Same helper, same reason, as
    // CPropTreeSearchBar::ScaleMetric.
    static int ScaleMetric (int nAt96dpi);

protected:
    virtual BOOL PreCreateWindow (CREATESTRUCT& cs);

    // Claims the list's own keys before the frame does. MFC walks PreTranslateMessage
    // from the focused window up through its parents, so this runs before the frame's
    // IsDialogMessage(), which would otherwise turn Enter and the arrows into pane
    // navigation and the list would never see them. CodeOutlineWnd::PreTranslateMessage
    // claims Enter for its tree for exactly this reason.
    virtual BOOL PreTranslateMessage (MSG* pMsg);

    // The frame every handler runs inside: messages arrive through WindowProc, and
    // commands and UI updates through OnCmdMsg, so no handler below needs its own
    // try block and Common::CheckExceptionFrame stays satisfied. CodeOutlineWnd does
    // the same.
    virtual LRESULT WindowProc (UINT message, WPARAM wParam, LPARAM lParam);
    virtual BOOL OnCmdMsg (UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo);

    // One painted row: a card (item >= 0) or an inert group caption (item < 0).
    struct Row
    {
        int     item;      // index into m_vecItems; -1 for a group header
        CString text;      // group caption; unused by cards
        int     y;         // top, content coordinates: 0 is the first pixel of row 0
        int     height;
        bool    expanded;  // cards only

        bool IsCard () const { return item >= 0; }
    };

    // All at 96dpi, all scaled through ScaleMetric() rather than used raw. They
    // live in one enum instead of being spread over the functions that read them
    // because the layout and the painting have to agree on every one of them, and
    // a second place to keep in step is how a chevron ends up drawn off its card.
    enum
    {
        CARD_MARGIN  = 6/2, // client edge to the card border
        CARD_GAP     = 4/2, // card to card
        CARD_RADIUS  = 6/2, // corner radius; GDI+ arcs take a DIAMETER, so pass 2x
        CARD_PAD     = 8/2, // border to text
        CHEVRON_COL  = 16,  // width of the glyph column at the left of a card
        BODY_INDENT  = 16,  // body lines up under the header text, not under the glyph
        BADGE_GAP    = 8/2, // gap kept clear to the left of the right-aligned badge
        BODY_GAP     = 6/2, // header line to first body line, inside an open card
        GROUP_PAD    = 4/2, // above and below a group caption
        GROUP_INDENT = 2,   // caption inset, so it reads as outside the cards
    };

    // ---- layout ----
    // RebuildRows() rebuilds the row list from the items, group headers included;
    // RecalcMetrics() re-reads the two fonts' TEXTMETRICs; LayoutRows() gives every
    // row a height and a running y. Nothing wraps, so heights do not depend on the
    // width: a resize calls UpdateScrollBars() and repaints, never LayoutRows().
    // That is what keeps the hit-test and the pixels in agreement after a resize.
    void RebuildRows ();
    void RecalcMetrics ();
    void LayoutRows ();

    int   RowHeight     (const Row&) const;
    int   RowFromY      (int yContent) const;   // binary search over the prefix sums
    int   RowFromItem   (int item) const;
    CRect RowRect       (int row) const;        // client coords, full width
    CRect CardRect      (int row) const;        // client coords, inset by CARD_MARGIN
    void  UpdateScrollBars ();
    void  ScrollTo      (int pos);

    // ---- painting ----
    // One Gdiplus::Graphics per frame, created in PaintRows and handed down, so the
    // shapes are anti-aliased without paying for a Graphics per card.
    void PaintRows  (CDC& dc, const CRect& rcClip);
    void PaintGroup (Gdiplus::Graphics& g, CDC& dc, const Row&, const CRect& rc);
    void PaintCard  (Gdiplus::Graphics& g, CDC& dc, int row, const CRect& rcCard);
    void DrawChevron (Gdiplus::Graphics& g, const CRect& rcGlyph, bool expanded, bool hot);

    // ---- state ----
    void SetHoverRow (int row);      // invalidates only the two cards that changed
    void InvalidateRow (int row);    // one card plus the pixel its border bleeds into
    bool ToggleRow   (int row);      // false when the row is not a card
    void MoveFocus   (int delta);    // skips group rows, keeps the card in view

    // Tells the parent and returns its reply, as CPropTree::SendNotify does:
    // nonzero = handled, 0 = nobody did. Only a code with default behaviour worth
    // suppressing needs to look at the result.
    LRESULT Notify   (int code, LPARAM lParam);

    CFont* HeaderFont ();            // the pane's font, or the fallback
    CFont* BoldHeaderFont ();        // ... the same face bolded, for a badge's required part
    CFont* BodyFont   ();
    void CreateFallbackFonts ();

    void StartDrag ();               // called once the press has moved far enough

    CFont* m_pHeaderFont;            // not owned
    CFont* m_pBodyFont;              // not owned
    CFont  m_headerFontOwn;          // used while the pointer above is NULL
    CFont  m_bodyFontOwn;
    CFont  m_boldHeaderFont;         // derived from HeaderFont(), rebuilt when it changes
    HGDIOBJ m_hBoldBase;             // the handle m_boldHeaderFont was last built from

    std::vector<SnippetItem> m_vecItems;
    std::vector<Row>         m_vecRows;

    int  m_cyHeaderLine;             // TEXTMETRIC.tmHeight of the header font
    int  m_cyBodyLine;               // ... of the body font
    int  m_cyGroupLine;
    int  m_nScrollPos;               // top of the view, content coords
    int  m_nContentHeight;           // all rows plus the trailing gap
    int  m_nHoverRow;
    int  m_nSelRow;
    bool m_bTrackingMouse;           // a TRACKMOUSEEVENT is outstanding

    // Press-and-drag arming: the button goes down on a card but a click must not
    // turn into a drag, so the drag only starts once the pointer has left the
    // SM_CXDRAG x SM_CYDRAG box around the anchor.
    bool   m_bDragArmed;
    int    m_nDragRow;
    CPoint m_ptDragAnchor;

    DECLARE_MESSAGE_MAP ()

    afx_msg void OnPaint ();
    afx_msg BOOL OnEraseBkgnd (CDC* pDC);
    afx_msg void OnSize (UINT nType, int cx, int cy);
    afx_msg void OnVScroll (UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
    afx_msg BOOL OnMouseWheel (UINT nFlags, short zDelta, CPoint pt);

    afx_msg void OnMouseMove (UINT nFlags, CPoint point);
    afx_msg void OnMouseLeave ();
    afx_msg void OnLButtonDown (UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp (UINT nFlags, CPoint point);
    afx_msg void OnLButtonDblClk (UINT nFlags, CPoint point);
    afx_msg void OnRButtonUp (UINT nFlags, CPoint point);

    afx_msg void OnKeyDown (UINT nChar, UINT nRepCnt, UINT nFlags);

    // DLGC_WANTARROWS, or the frame's IsDialogMessage() eats the arrows before the
    // list sees them and focus jumps to the next pane instead of moving the
    // selection.
    afx_msg UINT OnGetDlgCode ();

    afx_msg void OnSetFocus (CWnd* pOldWnd);
    afx_msg void OnKillFocus (CWnd* pNewWnd);
    afx_msg void OnDestroy ();
};

}; // namespace Common

#endif//__SnippetListWnd_h__
