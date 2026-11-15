/* 
    Copyright (C) 2002 Aleksey Kochetov

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

#ifndef __OEView_h__
#define __OEView_h__

#include <set>
#include <map>
#include <vector>
#include <afxole.h>             // for Drag & Drop
#include "OpenEditor/OEContext.h"
#include "OpenEditor/OEFolding.h"
#include "OpenEditor/OEAutocompleteCtrl.h"
#include "OpenEditor/OEViewPaintAccessories.h"

    using std::shared_ptr;
    class COEDocument;
    namespace OpenEditor { class HighlighterBase; struct FormatterPolicy; };


class COEditorView : public CView, protected OpenEditor::EditContext
{
    // Ruler is a struct, which helpes to draw character grid
    struct Ruler
    {
        int m_Topmost,       // topmost visible line or column
            m_Count,         // number of visible lines or columns
            m_CharSize,      // size of character
            m_ClientSize,    // size of client area
            m_Indent;

        COEditorView* m_sibling; // sibling for scrolling

        int  InxToPix (int inx) const;					// screen position to pixel coordinate (inline)
        int  PosToPix (int pos) const;                  // buffer position to pixel coordinate (inline)
        bool OnScroll (UINT code, UINT pos, int last, bool beyond);  // helper function for scrolling service
    }
    m_Rulers[2];
   
    friend COEAutocompleteCtrl;
    static COEAutocompleteCtrl m_autocompleteList; // an embedded control

    static BOOL m_isOverWriteMode;     // overwrite mode for text input
    static UINT m_uWheelScrollLines;   // cached value for MS Weel support
    BOOL m_bAttached;                  // it's FALSE till context initialization
    int  m_nMaxLineLength;		       // max line length (for horisontal scroller)
    BOOL m_bSyntaxHighlight;           
    BOOL m_bMouseSelection;            // it's TRUE after WM_LBUTTONDOWN
    BOOL m_bMouseSelectionStart;       // it's TRUE after WM_LBUTTONDOWN before first WM_MOUSEMOVE
    int  m_nMouseSelStartLine;         // it's not -1 if the left border area is used for a mouse selection
    BOOL m_bMouseSelectionByWord;      //
    OpenEditor::Square m_startWord;    // 
    int  m_nDigitsInLatsLineNumber;    // the number of digits in the last line number

    BOOL m_bDragScrolling; //  data is dragged over the window and scrolling has started after a small delay

    int   m_nClicks;
    CRect m_rcClick;
    DWORD m_tmLastClick;

    BOOL m_bPaleIfLocked; // colorize locked mode
    int  m_nExecutionLine;
    int  m_nDelayedScrollLine;

    struct CurCharCache
    {
        OpenEditor::Position pos;
        std::wstring message;

        CurCharCache () { pos.line = pos.column = -1; }
        void Reset ()   { pos.line = pos.column = -1; }
    }
    m_curCharCache;

    //TODO: put m_posCache & m_selCache in struct and add reset on selection mode switch and settings like in sqltools++
    OpenEditor::Position m_posCache;
    OpenEditor::Square m_selCache;
    OpenEditor::Position m_highlightingPosCache;
    OpenEditor::SequenceId m_highlightingActionSeqCache;
    std::wstring m_highlightedText;

    // Auto-close/auto-surround transient tracking: in-memory only - not a setting,
    // never persisted, takes no part in undo.
    struct AutoPairRec
    {
        OpenEditor::LineId lineId;
        int     openerInx;   // index-space column of the opening character the user typed
        int     closerInx;   // index-space column of the closing character inserted for it
        wchar_t closer;

        AutoPairRec () : openerInx(-1), closerInx(-1), closer(0) {}
        AutoPairRec (const OpenEditor::LineId& id, int opener, int closer_, wchar_t ch)
            : lineId(id), openerInx(opener), closerInx(closer_), closer(ch) {}
    };

    // OpenEditor::LineId provides only == and !=, so a map keyed by it needs a comparator.
    struct AutoPairLineLess
    {
        bool operator () (const OpenEditor::LineId& left, const OpenEditor::LineId& right) const
            { return left.GetId() < right.GetId(); }
    };

    typedef std::map<OpenEditor::LineId, std::vector<AutoPairRec>, AutoPairLineLess> AutoPairMap;

    AutoPairMap m_autoPairs;
    int  m_autoPairsLine;    // line the entries were recorded on, -1 when there are none
    bool m_autoPairEditing;  // true while OnChar performs its own single-character edit

    // RAII guard for the flag above: the OnChangedLines hooks stand down so ShiftAutoPairs'
    // precise bookkeeping runs instead of the coarse reset. Restores the old value - nesting safe.
    class AutoPairEditGuard
    {
    public:
        explicit AutoPairEditGuard (COEditorView& view)
            : m_view(view), m_saved(view.m_autoPairEditing)
        {
            view.m_autoPairEditing = true;
        }

        ~AutoPairEditGuard ()
        {
            m_view.m_autoPairEditing = m_saved;
        }

    private:
        AutoPairEditGuard (const AutoPairEditGuard&);              // not copyable
        AutoPairEditGuard& operator = (const AutoPairEditGuard&);

        COEditorView& m_view;
        bool m_saved;
    };

    int m_nFistHighlightedLine, m_nLastHighlightedLine;
    // Column-limited band (SetHighlighting's Square overload): >= 0 confines the fill to these
    // display columns on the band's first/last line; -1/-1 is the full-row band.
    int m_nFistHighlightedColumn, m_nLastHighlightedColumn;
    bool m_autoscrollOnHighlighting;
    // While a scope region is armed the selection-derived matching-text tint stays off: the tint
    // is document-wide, so it would mark occurrences outside the region the scope excludes.
    bool m_suppressMatchingHighlight;

    // The Find/Replace dialog's "Selection" scope. Its own layer, not a second user of the band
    // above, so the worksheet's transient band and the scope can be visible at once.
    OpenEditor::Square m_scopeRegion;   // normalized: display columns, buffer lines
    bool               m_scopeRegionValid;

    // Shared body of the two public SetHighlighting forms: stores the lines plus the optional
    // column bounds (-1/-1 = full rows), invalidates, autoscrolls, refreshes the idle caches.
    void SetHighlightingRange (int firstLine, int lastLine, int firstColumn, int lastColumn, bool updateWindow);

    // "Highlight all occurrences" overlay: transient squares in DISPLAY column space (the space
    // Position::column and OnPaint's tokenizer use - SetMatchOverlay converts at intake). The
    // action stamp makes any edit stale; no LineId tracking, this is a tick-refreshed visual.
    std::vector<OpenEditor::Square> m_matchOverlay;
    OpenEditor::SequenceId          m_matchOverlayActionId;
    bool                            m_matchOverlayValid;
    
    typedef OpenEditor::LanguageSupport::Match BraceHighlighting;
    BraceHighlighting m_braceHighlighting;
    
    void HighlightBraces ();
    void SetBraceHighlighting (const BraceHighlighting&);
    // Brace-only clear: a brace update must not take the SetHighlighting band with it.
    void HideBraceHighlighting ();

    OpenEditor::LineStatusMap m_statusMap;

    static const UINT SEL_MOUSE_TIMER             = (UINT)-1;
    static const UINT SEL_MOUSE_TIMER_ELAPSE      = 60;
    static const UINT DRAG_MOUSE_TIMER            = (UINT)-2;
    static const UINT DRAG_MOUSE_TIMER_ELAPSE     = 120;
    static const UINT DELAYED_SCROLL_TIMER        = (UINT)-3;
    static const UINT DELAYED_SCROLL_TIMER_ELAPSE = 125;
public:
    static const UINT DOC_TIMER_1                 = (UINT)-101;
    static const UINT DOC_TIMER_2                 = (UINT)-102;
    static const UINT DOC_TIMER_3                 = (UINT)-103;
private:
    static const int EXT_SYN_NESTED_LIMIT = 4;
    static const int EXT_SYN_LINE_INDENT  = 3;
    static const int EXT_SYN_GUTTER_WIDTH = (EXT_SYN_NESTED_LIMIT + 2) * EXT_SYN_LINE_INDENT;
    static const int LINE_STATUS_WIDTH = 4;

    // The [...] drawn after a collapsed anchor's own text. Kept here rather than inline in
    // OnPaint so the hit test and the double-click-to-expand path measure the same string.
    static const wchar_t FOLD_MARKER_TEXT[];  // L"..."
    static const int FOLD_MARKER_LEN = 3;

    int CalculateDigitsInLatsLineNumber ();
    int CalculateLeftMargin (int digits = 0);
    
    COEditorView* GetSibling (ULONG row, ULONG col);
    Ruler* GetSiblingRuler (ULONG row, ULONG col, UINT ruler);
    void AttachSibling (COEditorView*, UINT ruler);
    void DetachSibling (UINT ruler);
    void AdjustVertSibling ();
    void AdjustHorzSibling ();
    void DoPageDown ();
    void DoPageUp ();

    // "n of m" counter text for a committed search hit (the square RepeadSearch just found,
    // POSITION coordinates): re-scans THIS storage with the shared committed searcher. Read-only -
    // FindCtx is per-call and the Searcher keeps no scan state, so the next F3/Shift+F3 is
    // untouched. False when the hit fell outside the display cap or the scan hit its time bound.
    // With "Search All Windows" the count covers this document only, suffixed " in this file".
    bool countCommittedMatches (const OpenEditor::Square& blkPos, CString& counterText);

    // Step 3.2 follow-up: the counter text the last committed search on this view
    // produced ("" when there was none) - see GetSearchCounterText.
    CString m_searchCounterText;

    COEViewPaintAccessoriesPtr m_paintAccessories; // container for shared GDI objects
    shared_ptr<OpenEditor::HighlighterBase> m_highlighter;
    bool m_syntaxGutter;
    bool m_unsupportedAsianChars;
    // Folding reuses the syntax gutter as its margin, so this reserves EXT_SYN_GUTTER_WIDTH in
    // CalculateLeftMargin when the syntax gutter is off. A SETTINGS flag, not "has regions" - the
    // margin must not come and go as the parser catches up, or repaints shift the text.
    bool m_foldingGutter;

    // Cache of what GetCurrentFoldRegion last answered - invalidation only, the paint re-derives it.
    int m_currentFoldRegionStart;
    int m_currentFoldRegionEnd;

    // a settings synchronization handler
    void OnSettingsChanged ();

    // Bookmarks
    void DrawLineNumber (CDC&, RECT, int line);
    void DrawBookmark (CDC&, RECT);
    void DrawRandomBookmark (CDC&, RECT, int index, bool halfclip);

    // The square a fold glyph occupies inside the band row `rc`, empty when the band is too
    // narrow. CENTRED at every depth so the connectors merge into one vertical. Separate from
    // DrawFoldGlyph because the connector pass must place its rule pixel-exactly in the box.
    CRect FoldGlyphBox (const RECT& rc) const;
    void DrawFoldGlyph (CDC&, const RECT& rc, bool collapsed);

    // Row rect a column-limited region covers on `line`: partial on the border rows, full width
    // between them, so a multi-line square reads as one connected stream-selection shape.
    CRect RegionRowRect (const CRect& rcRow, const CRect& rcLine, int line,
                         int firstLine, int lastLine, int firstColumn, int lastColumn) const;

    void DrawHorzRuler (CDC&, const CRect&, int start, int end);
    void DrawVertRuler (CDC&, const CRect&, int start, int end, bool lineNumbers);
    void TextOutToScreen  (CDC& dc, Ruler[2], int pos, const wchar_t* str, int len, const CRect&, bool selection);
    void TextOutToPrinter (CDC& dc, const CPoint& pt, const CSize& chSz, const wchar_t* str, int len);
    //
    //  implementations of this methods are stored in OEView_3.cpp
    //
public:
    void SetHighlighting  (int fistHighlightedLine, int lastHighlightedLine, bool updateWindow = false);
    // Column-aware alternative: the band covers exactly this square - border lines fill
    // partially, interior lines full width. An empty square paints nothing.
    void SetHighlighting  (const OpenEditor::Square& square, bool updateWindow = false);
    void HideHighlighting ();
    void SetAutoscrollOnHighlighting (bool);

    // The Replace dialog's "Selection" scope: a layer of its own, so arming it never disturbs the
    // worksheet's band above and the caret-change auto-clear never disturbs it.
    void SetScopeRegion (const OpenEditor::Square& region);
    void ClearScopeRegion ();

    void EnableSyntaxHighlight (BOOL);
    void SetPaleIfLocked (BOOL);
    void SetExecutionLine (int);
    void DelayedScrollTo (int line);
    void ScrollTo (int line);
    int  GetTopmostLine () const                    { return m_Rulers[1].m_Topmost; }
    void SetTopmostLine (int line);
    int  GetTopmostColumn () const                  { return m_Rulers[0].m_Topmost; }
    void SetTopmostColumn (int column);

    using EditContext::GetLineW;
    //using EditContext::GetLineA;
    using EditContext::GetLineLength;
    using EditContext::GetLineCount;
    using EditContext::GetLineId;
    // Staleness detection: the dialog compares this against the snapshot taken when its preview
    // anchor was captured, to see document edits made while the dialog is open.
    using EditContext::GetActionId;

    // Code folding. m_Topmost stays a BUFFER line and rows are derived on demand, never stored;
    // the document owns the single LineMapper the split siblings share. With folding off the mapper
    // is an identity function, so no caller needs an "if folding" branch. Out of line because
    // COEDocument is only forward declared here; the const overload serves the const members.
    const OpenEditor::LineMapper& GetMapper ();
    const OpenEditor::LineMapper& GetMapper () const;
    int  GetVisibleLineCount ();

    // buffer line -> visual row
    int  LineToRow (int line) const { return GetMapper().BufferToVisual(line); }
    // visual row -> buffer line. Never a hidden line: a row inside a collapsed
    // run resolves to that run's anchor, which is the line the row shows.
    int  RowToLine (int row) const  { return GetMapper().VisualToBuffer(row); }
    // The row the top line sits on - what the vertical scrollbar reports.
    int  GetTopmostRow () const     { return LineToRow(GetTopmostLine()); }

    // Vertical pixel <-> buffer line. Not on Ruler: it has no route to the mapper, and giving it
    // one would drag the view into the print path, which prints folded lines too.
    // Unlike GetVisibleLineCount these do NOT refresh the mapper's line count.
    int  LineToPixY (int line) const;
    int  PixToLineY (int y)    const;

    // Row -> buffer line WITHOUT clamping rows below the last visible one: "cursor beyond end of
    // file" parks the caret on phantom rows, counted on from GetLineCount(). Page motion needs
    // this rather than RowToLine. Non-const: refreshes the mapper's line count first.
    int  RowToLineUnclamped (int row);
    // True while something is actually hidden. From the counts, not the setting, so it stays
    // correct with folding on but nothing folded, and for documents that never build regions.
    bool IsFoldingActive ()       { return GetVisibleLineCount() != GetLineCount(); }

    // Snaps a line onto a visible one: itself, or the anchor of the collapsed run hiding it.
    // Out-of-range values (including the one-past-EOF line) are left alone.
    int  VisibleLine (int line) const
        { return line >= 0 && line < GetLineCount() && GetMapper().IsLineHidden(line)
                 ? RowToLine(LineToRow(line)) : line; }

    // Scroll arithmetic both rulers share, factored out of Ruler::OnScroll so the vertical path
    // can run it over VISUAL ROWS. `top` is updated in place; the result says whether it moved.
    static bool ScrollTopmost (int& top, UINT code, UINT pos, int count, int last, bool beyond);


    using EditContext::FindById;

    using EditContext::GetPosition;
    using EditContext::MoveTo;
    using EditContext::InxToPos;
    using EditContext::PosToInx;

    using EditContext::GetBlockMode;
    using EditContext::SetBlockMode;
    using EditContext::IsAltColumnarMode;
    using EditContext::GetSelection;
    using EditContext::SetSelection;
    using EditContext::IsSelectionEmpty;
    using EditContext::InsertBlock;
    using EditContext::SelectAll;
    using EditContext::ClearSelection;
    
    using EditContext::GetBlockOrWordUnderCursor;

    // Overtype mode: a single process-wide flag (m_isOverWriteMode is static) toggled by VK_INSERT,
    // which gates auto-close/auto-surround entirely. Read-only accessor so the automated harness can
    // establish and assert the mode - there is no other way to observe it from outside the class.
    static BOOL IsOvertypeMode () { return m_isOverWriteMode; }

    // not implemented yet
    void SetQueueBookmark (int, bool = true);

    // cursor navigation for external usage
    // I changed the method name from Go... to Move... 
    // avoiding any impact on the existing code
    void MoveToBottom ();
    void MoveToAndCenter (OpenEditor::Position);

    virtual void GetBlock    (std::wstring&, const OpenEditor::Square* = 0) const;
    virtual void InsertBlock (const wchar_t*, bool hideSelection, bool putSelInUndo = true);
    virtual void DeleteBlock (bool putSelInUndo = true);

    // Searching. Direction is a per-call parameter: false toward EOF, true toward BOF.
    bool RepeadSearch (bool backward, COEditorView*&);

    // Counter text of the last committed search on this view ("n of m", "" when not reportable).
    // The dialog mirrors it into its caption, where the user's eye is.
    const CString& GetSearchCounterText () const { return m_searchCounterText; }
    using EditContext::GetSearchText;
    using EditContext::SetSearchText;
    using EditContext::SetSearchOption;

    bool Find (const COEditorView*&, OpenEditor::FindCtx&) const;

    // Find-as-you-type preview: one search from `anchor` with a TRANSIENT searcher, so the committed
    // text/options behind F3 never drift while the user types. Selects and reveals the hit with no
    // MDIActivate, focus, status or beeps. May THROW on an invalid regex = "not previewable yet".
    // foundRaw gets the hit in RAW INDEX space; selectHit=false is a pure probe; regionPos rejects
    // hits not fully inside it, and backward flips that check to the region TOP.
    bool PreviewSearch (const wchar_t* text, bool wholeWords, bool matchCase, bool regExpr,
                        const OpenEditor::Position& anchor, OpenEditor::Square* foundRaw = 0,
                        bool selectHit = true, const OpenEditor::Square* regionPos = 0,
                        bool backward = false);

    // "n of m" match counter: counts `text`'s matches in THIS view's storage and renders
    // `counterText` as "n of m" ("n of 999+" past the cap), n being the 1-based ordinal of
    // `targetRaw` (RAW INDEX space - PreviewSearch's foundRaw). TRANSIENT searcher, searchAll
    // forced off: a mid-typing count must never touch the committed searcher behind F3.
    // Returns false when nothing meaningful can be reported (empty text, target outside the cap,
    // time bound hit); may THROW on an invalid regex, like PreviewSearch. regionPos clamps the
    // scan to that square, so "n of m" is exactly what a scoped Replace All would touch. collect
    // receives every enumerated match in RAW INDEX space - capped, and PARTIAL on a timeout.
    bool CountSearchMatches (const wchar_t* text, bool wholeWords, bool matchCase, bool regExpr,
                             const OpenEditor::Square& targetRaw, CString& counterText,
                             const OpenEditor::Square* regionPos = 0,
                             std::vector<OpenEditor::Square>* collect = 0);

    // Transient "highlight matching text" injection for the dialog's count-only ticks - opening
    // the dialog must not navigate, so the text is tinted in place. Reuses the built-in selection
    // tint, which covers WHOLE WORD tokens case-insensitively: multi-word, partial-word and regex
    // searches show nothing. NULL/empty clears; the regular caret maintenance re-derives the
    // member on the next interaction, so an injected value is transient by design.
    void HighlightMatchingText (const wchar_t* text);

    // Suppresses the selection-matching tint for the lifetime of a scope region - both feeds: the
    // selection-derived maintenance and HighlightMatchingText's injection. Enabling clears any tint
    // showing; disabling does not re-derive one, the next caret change does that as usual.
    void SuppressMatchingTextHighlight (bool suppress);

    // "Highlight all occurrences": the full-fidelity counterpart of the token tint - every match
    // square, arbitrary fragments and regex included, case-sensitivity honored by construction
    // (the searcher decided the squares). Owned by the dialog: set from the same capped scan that
    // feeds the counter, cleared on miss/close/retarget/commit, invalidated by any edit via the
    // action stamp. Squares arrive single-line in RAW INDEX space and are converted here to
    // display columns; an empty vector clears. Painted in m_FindMatchBackground, BEFORE the text.
    void SetMatchOverlay (const std::vector<OpenEditor::Square>& matches);
    void ClearMatchOverlay ();

    using EditContext::Replace;
    using EditContext::SearchBatch;

    void ShowCaret (bool show, bool dont_destroy_caret=false);
    bool GetCaretPosition (CRect&) const;

    class UndoGroup : public OpenEditor::EditContext::UndoGroup
    {
    public:
        UndoGroup (COEditorView&);
    };
    //void PushInUndoStack (Position);
    //void PushInUndoStack (EBlockMode, Square);
    using EditContext::PushInUndoStack;

    // Collapse or expand the region ANCHORED at `line`, plus the bookkeeping every fold change
    // needs. Public: fold state is document-owned but its consequences (mapper cache, caret,
    // viewport, scrollbar, split sibling) are only visible to a view. The line-addressed entry for
    // callers that know WHICH fold they mean - the commands and the step-script harness, which
    // cannot reach the protected hit tests. False = not an anchor, or already in that state.
    bool SetFoldCollapsed (int line, bool collapsed);

    // Outlining commands: same contract as SetFoldCollapsed - mutate the model, then
    // ApplyFoldChange. The caret-addressed and whole-document groups enable on different
    // questions. False means nothing changed, so callers and the .cmd harness can report a no-op.
    bool FoldCollapseCurrentRegion ();
    bool FoldExpandCurrentRegion ();
    bool FoldToggleCurrentRegion ();
    bool FoldExpandCurrentSubtree ();
    bool FoldCollapseAllRegions ();
    bool FoldExpandAllRegions ();
    bool FoldToggleAllRegions ();
    bool FoldCollapseToLevel (int level);

    // Called by COEDocument::NotifyFoldModelChanged when the model actually changed. The document
    // already invalidated the mapper; the view owes a scrollbar extent and a gutter repaint.
    // Deliberately no caret or viewport movement - nothing the user was looking at has moved.
    void OnFoldModelChanged ();

protected:
    void SetCaretPosition ();
    void AdjustCaretPosition ();

    // Where a one-line vertical step lands - only the view can see visual rows, so one arrow
    // press moves one visible ROW.
    virtual int NextNavigableLine (int line, int delta);

    // Hit-tests a left press against the fold band and flips the region anchored on that row.
    // Returns true when it consumed the click, so OnLButtonDownImpl does not turn it into the
    // line selection the gutter normally starts; a press on any other row keeps that behaviour.
    bool ToggleFoldAtPoint (CPoint point);

    // The outlining context menu for a right-click inside the left margin: the outlining commands
    // plus the item that flips PlSqlOutlineFoldingEnabled. `screenPoint` is in SCREEN coordinates.
    void TrackFoldGutterMenu (const CPoint& screenPoint);

    // The shared bookkeeping every fold-state change needs: drop the mapper's cached prefix sum,
    // pull the caret and the viewport top back onto visible lines, resize the scrollbar, repaint
    // this view and its split sibling. Easy to get wrong - the caret can sit on a vanished line.
    void ApplyFoldChange ();

    // The model the stage 7 outlining commands act on, or NULL when they cannot
    // run at all: not attached, fold gutter off, or no document.
    OpenEditor::FoldModel* CommandFoldModel ();

    // Expands every collapsed region hiding `line`, innermost first. Deliberate jumps call it
    // before moving (Go To, bookmarks, a search hit, Undo's caret restore, the execution line):
    // landing the caret where the user cannot see it looks like the command did nothing.
    // Ordinary caret motion does NOT come through here - GoToUp/GoToDown stop at the anchor.
    // Also overrides the virtual EditContext::RevealLine hook, how a MODEL-side edit asks the
    // view for a reveal (ColumnarInsert uses it, gated on ColInsertRevealLine).
    virtual bool RevealLine (int line);

    // Reveals every hidden line the current COLUMNAR selection spans; call it right before a
    // columnar edit (Surround, Indent, Undent, Delete, Backspace, Untabify) - they loop the
    // Square's whole row range and none reveals for itself. ColumnarInsert is not on the list:
    // it calls the RevealLine hook from the model, so its reveal stays gated on the setting.
    // Needed because a Square is a RECTANGLE over start.line .. end.line - after the caret fix no
    // corner is hidden, but the range between two visible corners can still straddle a collapsed
    // run. Columnar-only: a stream selection already covers the whole hidden run by construction.
    bool RevealColumnarEditRows ();

    // --- fold hit testing ---------------------------------------------------
    // One geometry behind all three fold affordances (gutter click, [...] double-click, hover
    // cursor): they must agree about which row and which pixels a point is on.

    // The fold band is the rightmost EXT_SYN_GUTTER_WIDTH pixels of the left margin - the strip
    // DrawVertRuler lays out as extGutterRc. Everything left of it keeps its existing behaviour.
    bool InFoldBand (CPoint point) const;

    // Client point -> the buffer line whose ROW it sits on, or -1 when it is on no row. Below the
    // last row matters: RowToLine clamps, so blank area under EOF would resolve to a real line.
    int  RowLineAtPoint (CPoint point);

    // Client rect of the [...] a collapsed anchor row draws after its own text,
    // or an empty rect when `line` is not a collapsed anchor.
    CRect FoldMarkerRect (int line);

    // True when a left press at `point` would change fold state - on a glyph in
    // the band, or on a collapsed row's [...]. Drives the hover cursor.
    bool FoldHoverAtPoint (CPoint point);

    // Expands the fold whose [...] is under `point`, and returns false for
    // anywhere else so a double-click there keeps its word selection.
    bool ExpandFoldMarkerAtPoint (CPoint point);

    // --- current-region gutter tint -------------------------------------------

    // Tints the ext band of the fold region the caret sits in, in the "Highlighting" colour.
    bool GetCurrentFoldRegion (int& startLine, int& endLine);
    // GetCurrentFoldRegion gated by the highlight-current-region setting, PAINT side only. The
    // commands call GetCurrentFoldRegion directly: the switch turns the tint off, never folding.
    bool GetCurrentFoldHighlight (int& startLine, int& endLine);
    void UpdateCurrentFoldHighlight ();
    void InvalidateGutterLines (int startLine, int endLine);

    void AdjustVertScroller (int maxPos = 0);
    void AdjustHorzScroller (int maxPos = 0);
    void DoHScroll (UINT nSBCode, BOOL keyboard);
    void DoVScroll (UINT nSBCode, BOOL keyboard);

    void DoSize (UINT nType, int cx, int cy);

    OpenEditor::Position MouseToPos (CPoint, bool force = false) const;
    void SelectByMouse (CPoint);

    // override base methods
    void InvalidateWindow ();
    void InvalidateLines  (int start, int end);
    void InvalidateSquare (const OpenEditor::Square&);

    void OnChangedLines (int startLine, int endLine);
    void OnChangedLinesQuantity (int line, int quantity);
    void OnChangedLinesStatus (int, int);

protected:
    static UINT m_AltColumnarTextFormat;
    // Aligns a {code} payload, restoring the trailing newline AlignCodeFragment drops; one impl for paste and drop.
    int ApplyCodeFragment (const wchar_t* raw, std::wstring& out);
public:
    static UINT GetAltColumnarTextFormat();
    void InsertCodeFragment (const wchar_t* raw);
    void DoEditCopy (const std::wstring&, bool append = false);
    void DoEditExpandTemplate (OpenEditor::TemplatePtr templ, bool canModifyTemplate);

    // Expands one specific template entry at the caret, and reports whether it did.
    // A public wrapper because EditContext::ExpandTemplate is protected in a base this
    // class inherits protected, so nothing outside COEditorView and its friends can
    // reach it - and the snippets pane has to, to insert the entry the user picked
    // rather than the one whose keyword happens to sit under the caret.
    bool DoInsertTemplateEntry (OpenEditor::TemplatePtr templ, int index);


protected:
    //
    //  Drag & Drop implementation (see OEView_3.cpp)
    //
    struct CDragAndDropData
    {
        bool   bVisible;
        CPoint ptCaret, ptCursor;
        CSize  sCursor;
        CView* pSrcWnd, *pDstWnd;
        CDragAndDropData () { memset(this, 0, sizeof (CDragAndDropData)); }
    };

    static int cnScrollThreshold;
    static CDragAndDropData m_DragAndDropData;
    COleDropTarget m_DropTaget;

    void DrawCursor (CPoint, bool erase = false);
    bool CursorOnSelection (CPoint) const;
    void StartDrag ();
    void PositionToPoint (OpenEditor::Position, CPoint&) const;

    virtual DROPEFFECT OnDragEnter (COleDataObject*, DWORD dwKeyState, CPoint);
    virtual DROPEFFECT OnDragOver  (COleDataObject*, DWORD dwKeyState, CPoint);
    virtual BOOL OnDrop            (COleDataObject*, DROPEFFECT, CPoint);
    virtual void OnDragLeave       ();

    // comment/uncomment helper functions
    void  DoCommentText (bool comment); // uncomment on false
    static bool CommentText (const OpenEditor::EditContext&, std::wstring&);
    static bool UncommentText (const OpenEditor::EditContext&, std::wstring&);

    // printing helper
    void GetMargins (CRect& rc) const;
    void MoveToPage (CPrintInfo* pInfo, int* pMaxPage = NULL);
    void PrintHeader (CDC* pDC, CPrintInfo* pInfo);
    void PrintFooter (CDC* pDC, CPrintInfo* pInfo);

    // keyword normalization helpers
    struct NormalizeOnCharCxt 
    {
        bool matched;
        std::wstring keyword; 
        OpenEditor::Square sqr; 
    };
    bool PreNormalizeOnChar (NormalizeOnCharCxt&, wchar_t ch);
    void NormalizeOnChar    (NormalizeOnCharCxt&);
    // it's not comment and string position
    bool IsNormalizeablePos (OpenEditor::Position pos) const;
    static bool NormalizeText (const OpenEditor::EditContext&, std::wstring&);

    // auto-close/auto-surround helper: the position is lexically inside a quoted string
    bool IsInStringAt (OpenEditor::Position pos) const;

    // auto-close/auto-surround tracking, spec §3.5; implemented in OEView_6.cpp
    void ResetAutoPairs ();                             // drop every tracked pair
    void ResetAutoPairs (int firstLine, int lastLine);  // drop the pairs on those lines
    void TrackAutoPair (int line, int openerInx, int closerInx, wchar_t closer);
    void ShiftAutoPairs (int line, int fromInx, int delta);  // delta: +1 insert, -1 delete
    bool FindAutoCloser (int line, int inx, wchar_t& closer);

    // auto-close rules 5.1-5.4; implemented in OEView_1.cpp next to OnChar, which owns the pair table
    wchar_t GetAutoCloser (wchar_t ch, OpenEditor::Position pos) const; // 0 => do not close
    bool SkipOverAutoCloser (wchar_t ch);                    // rule 5.2, caret move only
    void InsertTypedChar (wchar_t ch, bool bAutoClose);      // rules 5.3 + 5.4

    // Stage 3(a) dedent-while-typing, next to OnChar because the undo grouping is OnChar's to give.
    // IsSmartDedentCandidate is the cheap gate licensing ONE group for character plus rewrite.
    bool IsSmartDedentCandidate (wchar_t ch) const;
    void ApplySmartDedent ();

    // Shift+Tab with no selection: outdent the caret's line one stop, as one undo group. Shaped
    // like ApplySmartDedent on purpose, and never consults the engine - the user is overriding it.
    void ApplyManualUndent ();

    // auto-surround rule 4.3, in OEView_1.cpp. Returns false when the character must not
    // surround, leaving the document untouched so the caller can fall through to rule 4.4. Rules
    // 4.1 (overtype) and 4.2 (column mode) are the caller's to test, and 4.2 must come first.
    bool AutoSurroundSelection (wchar_t ch);

    // columnar auto-surround rule 4.2, in OEView_1.cpp; mechanics in EditContext::ColumnarSurround.
    // Same "false means fall through untouched" contract, plus one rejection the stream version does
    // not need: a zero-width block, which has no text to surround and must keep its ColumnarInsert
    // behavior. Records no pair tracking.
    bool AutoSurroundColumnSelection (wchar_t ch);

    // PL/SQL-aware stream surround, in OEView_1.cpp. PlsqlQuotingChar is the language gate: it
    // returns the language's quote character when AutoSurroundPlsqlQuoting is on AND the language
    // declares AlternativeQuotes, else 0 ("run the legacy path"). The two workers share
    // AutoSurroundSelection's contract - one undo group, wrapped text ends up selected - but
    // record no pair tracking.
    wchar_t PlsqlQuotingChar ();
    bool AutoSurroundQQuote (wchar_t ch, wchar_t qCloser, wchar_t quote,
                             const OpenEditor::Square& sel);
    bool AutoSurroundEscapedQuote (wchar_t ch, const OpenEditor::Square& sel,
                                   const std::wstring& escaped);

protected: // create from serialization only
    COEditorView();
    DECLARE_DYNCREATE(COEditorView)

// Attributes
public:
    COEDocument* GetDocument();

    afx_msg void OnUpdate_Mode      (CCmdUI* pCmdUI);
    afx_msg void OnUpdate_Pos       (CCmdUI* pCmdUI);
    afx_msg void OnUpdate_ScrollPos (CCmdUI* pCmdUI);
    afx_msg void OnUpdate_BlockType (CCmdUI* pCmdUI);
    afx_msg void OnUpdate_CurChar   (CCmdUI* pCmdUI);

public:
    //{{AFX_VIRTUAL(COEditorView)
    public:
    virtual void OnInitialUpdate();
    virtual void OnDraw(CDC*) {};  // overridden to draw this view
    virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
    virtual BOOL OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo);
    virtual void OnPrepareDC(CDC* pDC, CPrintInfo* pInfo = NULL);
    protected:
    virtual BOOL OnPreparePrinting(CPrintInfo* pInfo);
    virtual void OnBeginPrinting(CDC* pDC, CPrintInfo* pInfo);
    virtual void OnEndPrinting(CDC* pDC, CPrintInfo* pInfo);
    virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
    virtual void OnPrint(CDC* pDC, CPrintInfo* pInfo);
    //}}AFX_VIRTUAL

public:
    virtual ~COEditorView();

    // A view on its way out reports itself to the dialog singleton, which retargets to the split
    // sibling when there is one. AttachSibling/DetachSibling set/clear it on both rulers together,
    // so either non-null link is the sibling.
    COEditorView* GetSiblingView ()
        { return m_Rulers[0].m_sibling ? m_Rulers[0].m_sibling : m_Rulers[1].m_sibling; }

protected:
    //{{AFX_MSG(COEditorView)
    afx_msg void OnPaint(); // moved to OEView_2.cpp
    afx_msg int  OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnDestroy();
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnHScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
    afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
    afx_msg void OnSetFocus(CWnd* pOldWnd);
    afx_msg void OnKillFocus(CWnd* pNewWnd);
    afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    afx_msg void OnSysKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnLButtonDownImpl(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnLButtonDblClkImpl(UINT nFlags, CPoint point);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnEditToggleColumnarSelection();
    afx_msg void OnEditToggleStreamSelection();
    afx_msg void OnEditToggleSelectionMode();
    afx_msg void OnUpdate_SelectionType(CCmdUI* pCmdUI);
    afx_msg void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
    afx_msg void OnEditCopy();
    afx_msg void OnEditCut();
    afx_msg void OnEditPaste();
    afx_msg void OnUpdate_EditCopy(CCmdUI* pCmdUI);
    afx_msg void OnUpdate_EditPaste(CCmdUI* pCmdUI);
    afx_msg void OnTest();
    afx_msg void OnEditUndo();
    afx_msg void OnEditRedo();
    afx_msg void OnUpdate_EditUndo(CCmdUI* pCmdUI);
    afx_msg void OnUpdate_EditRedo(CCmdUI* pCmdUI);
    afx_msg void OnEditSelectAll();
    afx_msg void OnEditDeleteLine();
    afx_msg void OnEditDeleteWordToLeft();
    afx_msg void OnEditDeleteWordToRight();
    afx_msg void OnEditFind();
    afx_msg void OnEditReplace();
    afx_msg void OnEditFindNext();
    afx_msg void OnEditFindPrevious();
    afx_msg void OnEditFindSeletedNext();
    afx_msg void OnEditFindSeletedPrevious();
    afx_msg void OnEditBookmarkToggle();
    afx_msg void OnEditBookmarkNext();
    afx_msg void OnEditBookmarkPrev();
    afx_msg void OnEditBookmarkRemoveAll();
    // Outlining commands; the two update handlers serve the contiguous ID ranges split at
    // ID_EDIT_FOLD_EXPAND_SUBTREE.
    afx_msg void OnEditFoldCollapseCurrentRegion();
    afx_msg void OnEditFoldExpandCurrentRegion();
    afx_msg void OnEditFoldToggleCurrentRegion();
    afx_msg void OnEditFoldExpandCurrentSubtree();
    afx_msg void OnEditFoldCollapseAllRegions();
    afx_msg void OnEditFoldExpandAllRegions();
    afx_msg void OnEditFoldToggleAllRegions();
    afx_msg void OnEditFoldCollapseToLevel1();
    afx_msg void OnEditFoldCollapseToLevel2();
    afx_msg void OnEditFoldCollapseToLevel3();
    afx_msg void OnUpdate_FoldCurrentRegion(CCmdUI* pCmdUI);
    afx_msg void OnUpdate_FoldGroup(CCmdUI* pCmdUI);
    // The gutter context menu's last item: flips PlSqlOutlineFoldingEnabled. Its ID sits outside
    // both fold ranges on purpose - it enables on a different question and carries a check state.
    afx_msg void OnEditFoldToggleEnabled();
    afx_msg void OnUpdate_FoldToggleEnabled(CCmdUI* pCmdUI);
    afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
    afx_msg void OnInitMenuPopup(CMenu* pPopupMenu, UINT nIndex, BOOL bSysMenu);
    afx_msg void OnUpdate_EditFindNext(CCmdUI* pCmdUI);
    afx_msg void OnEditLower();
    afx_msg void OnUpdate_SelectionOperation(CCmdUI* pCmdUI);
    afx_msg void OnUpdate_StreamSelectionOperation(CCmdUI* pCmdUI);
    afx_msg void OnEditUpper();
    afx_msg void OnBlockTabify();
    afx_msg void OnBlockUntabify();
    afx_msg void OnBlockTabifyLeading();
    afx_msg void OnEditExpandTemplate();
    afx_msg void OnEditCreateTemplate();
    afx_msg void OnOpenFileUnderCursor();
    afx_msg void OnEditIndent();
    afx_msg void OnEditUndent();
    afx_msg void OnEditSort();
    afx_msg void OnEditFindMatch();
    afx_msg void OnEditFindMatchAndSelect();
    afx_msg void OnUpdate_CommentUncomment (CCmdUI* pCmdUI);
    afx_msg void OnEditComment();
    afx_msg void OnEditUncomment();
    afx_msg void OnEditGoto();
    //}}AFX_MSG
    afx_msg void OnSetRandomBookmark       (UINT nBookmark);
    afx_msg void OnGetRandomBookmark       (UINT nBookmark);
    afx_msg void OnUpdate_GetRandomBookmark (CCmdUI* pCmdUI);
    afx_msg void OnUpdate_BookmarkGroup     (CCmdUI* pCmdUI);
    afx_msg void OnUpdate_BookmarkGroupRO   (CCmdUI* pCmdUI);
    afx_msg void OnEditScrollUp ();
    afx_msg void OnEditScrollDown ();
    afx_msg void OnEditScrollCenter ();
    DECLARE_MESSAGE_MAP()
public:
    afx_msg void OnEditCutAndAppend();
    afx_msg void OnEditCutBookmarked();
    afx_msg void OnEditCopyAndAppend();
    afx_msg void OnEditCopyBookmarked();
    afx_msg void OnEditDelete();
    afx_msg void OnEditDeleteBookmarked();
    afx_msg void OnEditSelectWord();
    afx_msg void OnEditSelectLine();
    afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
    afx_msg void OnSettingChange(UINT uFlags, LPCTSTR lpszSection);
    afx_msg void OnEditCapitalize();
    afx_msg void OnEditInvertCase();
protected:
    virtual void OnActivateView(BOOL bActivate, CView* pActivateView, CView* pDeactiveView);
public:
    afx_msg void OnEditNormalizeText();
    afx_msg void OnEditDatetimeStamp();

    afx_msg void OnEditJoinLines ();         
    afx_msg void OnEditSplitLine ();         
    afx_msg void OnEditRemoveBlankLines ();   
    afx_msg void OnRemoveExcessiveBlankLines ();

    afx_msg void OnTrimLeadingSpaces ();
    afx_msg void OnTrimExcessiveSpaces ();
    afx_msg void OnTrimTrailingSpaces ();

    afx_msg void OnEditFormatSql ();
    afx_msg void OnEditFormatSqlWithSettings ();
    // The formatting body, parameterized on a FormatterPolicy. Not an afx_msg: called directly by
    // OnEditFormatSql() and by the automated-testing handler.
    void DoEditFormatSql (const OpenEditor::FormatterPolicy& policy);

    afx_msg void OnEditColumnInsert ();      
    afx_msg void OnEditColumnInsertNumber (); 
    afx_msg void OnEditColumnLeftJustify (); 
    afx_msg void OnEditColumnCenterJustify ();
    afx_msg void OnEditColumnRightJustify ();
    afx_msg void OnEditDupLineOrSelection ();

    afx_msg void OnUpdate_ColumnOperation (CCmdUI* pCmdUI);
    afx_msg void OnUpdate_NotInReadOnly (CCmdUI* pCmdUI);

    afx_msg void OnIdleUpdateCmdUI ();
    afx_msg void OnFilePrintPreview ();
};

inline
COEDocument* COEditorView::GetDocument()
    { return (COEDocument*)m_pDocument; }

inline
void COEditorView::InvalidateWindow ()
    { Invalidate(FALSE); }

inline
int COEditorView::Ruler::InxToPix (int inx) const
    { return inx * m_CharSize + m_Indent; }

inline
int COEditorView::Ruler::PosToPix (int pos) const
    { return (pos - m_Topmost) * m_CharSize + m_Indent; }

inline
COEditorView::
UndoGroup::UndoGroup (COEditorView& view)
: OpenEditor::EditContext::UndoGroup(view)
{
}

inline
void COEditorView::SetAutoscrollOnHighlighting (bool enable) 
    { m_autoscrollOnHighlighting = enable; }

inline
UINT COEditorView::GetAltColumnarTextFormat()
    { return m_AltColumnarTextFormat; }

//{{AFX_INSERT_LOCATION}}

#endif//__OEView_h__
