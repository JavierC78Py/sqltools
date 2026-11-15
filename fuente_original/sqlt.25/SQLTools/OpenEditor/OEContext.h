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

// 2011.09.13 bug fix, the application hangs if user sets either Indent or Tab size to 0

#pragma once
#ifndef __OEContext_h__
#define __OEContext_h__

#include <sstream>
#include "OpenEditor/OEHelpers.h"
#include "OpenEditor/OEStorage.h"

#define OEC_DECLARE_GET_PROPERTY(T,N) \
        const T& Get##N () const \
        { _ASSERTE(m_pStorage); return m_pStorage->GetSettings().Get##N(); };

namespace OpenEditor
{
    using std::map;
    using std::vector;
    using std::string;
    using std::istream;
    using std::ostream;

    class GeneralMatchSearcher;
    class PlSqlMatchSearcher;
    struct SortCtx;

    class EditContext : public EditContextBase
    {
    public:
        EditContext ();
        virtual ~EditContext ();

        bool IsLocked () const;
        void Lock (bool lock);

        void Init ();
        void SetStorage (Storage*);

        int  GetLineCount () const;
        LineId GetLineId (int line) const;
        int  GetLineLength (int line) const;
        void GetLineA (int line, const char*& ptr, int& len) const;
        void GetLineW (int line, OEStringW&) const;

        int GetCursorLine () const;
        int GetCursorColumn () const;
        Position GetPosition () const;

        int InxToPos (int, int) const;
        int PosToInx (int, int, bool = false) const;

        /*
           all navigation methods return true
           if action is successful and false if it is not,
           but cursor position may be changed by adjustment in any case
        */
        bool GoToUp (bool force = false);
        bool GoToDown (bool force = false);
        bool GoToLeft (bool force = false);
        bool GoToRight (bool force = false);

        bool GoToTop ();
        bool GoToBottom ();
        bool GoToStart (bool force = false);
        bool GoToEnd (bool force = false);

        bool WordRight ();
        bool WordLeft ();

        bool MoveTo (Position, bool force = false);
        void AdjustPosition (Position& pos, bool force = false) const;
        bool SmartGoToStart ();
        bool SmartGoToEnd ();

        void DoIndent (bool moveOnly = false);
        void DoUndent ();
        void DoCarriageReturn ();

        void Insert     (wchar_t);
        void Overwrite  (wchar_t);
        void Backspace  ();
        void Delete     ();

        void DeleteLine ();
        void DeleteWordToLeft ();
        void DeleteWordToRight ();

        bool CanUndo () const;
        bool CanRedo () const;
        void Undo ();
        void Redo ();

        bool IsSelectionEmpty () const;
        void SetSelection (const Square&, bool clearAltColumnarMode = false);
        void GetSelection (Square&) const;
        void ClearSelection (bool clearAltColumnarMode = false);
        void ConvertSelectionToLines ();

        void SelectAll ();
        void SelectLine (int line);
        void SelectByCursor (Position prevPos);

        bool WordFromPoint (Position, Square&, const char* delims = 0) const;
        bool GetBlockOrWordUnderCursor (std::wstring& buff, Square& sqr, bool onlyOneLine, const char* delims = 0);
        bool WordOrSpaceFromPoint (Position, Square&, const char* delims = 0) const;

        void UntabifyForColumnarOperation (Square);
        void ColumnarInsert (wchar_t ch);
        // wrap every line of a non-zero-width column selection in an opener/closer pair
        void ColumnarSurround (wchar_t opener, wchar_t closer);
        void ColumnarIndent ();
        void ColumnarUndent ();
        void ColumnarBackspace ();
        void ColumnarDelete ();

        virtual void GetBlock    (std::wstring&, const Square* = 0) const;
        virtual void InsertBlock (const wchar_t*);
        virtual void InsertBlock (const wchar_t*, bool hideSelection, bool putSelInUndo = true);
        virtual void DeleteBlock (bool putSelInUndo = true);

        void IndentBlock ();
        void UndentBlock ();
        void Sort (const SortCtx&);

        void CopyBookmarkedLines (std::wstring&) const;
        void DeleteBookmarkedLines ();
        void RemoveBlankLines (bool excessiveOnly);

        int AlignCodeFragment (const wchar_t*, std::wstring&);

        virtual bool ExpandTemplate (OpenEditor::TemplatePtr, int index = -1);

        bool GetMatchInfo(Position pos, LanguageSupport::Match& match);
        void FindMatch (bool select = false);

        void SetBlockMode (EBlockMode mode, bool altColumnarMode = false);
        EBlockMode GetBlockMode () const;
        bool IsAltColumnarMode () const;

        void ScanMultilineQuotes (int to_line, int& state, int& quoteId, bool& parsing, wchar_t& qClose) const;
        MultilineQuotesScanner& GetMultilineQuotesScanner ();
        SequenceId GetActionId () const;

        void OnSettingsChanged ();

        virtual void AdjustCaretPosition() = 0;
        virtual void InvalidateWindow () = 0;
        virtual void InvalidateLines  (int start, int end) = 0;
        virtual void InvalidateSquare (const Square&) = 0;
        virtual void MoveToAndCenter (Position pos) = 0;

        // The line a one-line vertical caret step from `line` lands on.
        //
        // EditContext works in BUFFER lines only and must never see a visual row
        // (md/CodeFolding.md section 4, constraint C3), so it cannot work out for
        // itself which lines are currently hidden inside a collapsed fold - it
        // has to ask the view, the same way it already asks for AdjustCaretPosition
        // and MoveToAndCenter. The default is the plain line + delta that vertical
        // navigation has always done, so every EditContext that is not a view -
        // and every view while folding is off - behaves exactly as before.
        // COEditorView overrides it to skip hidden lines, which is what makes one
        // arrow press move exactly one visible ROW.
        virtual int NextNavigableLine (int line, int delta) { return line + delta; }

        // Makes `line` visible if a collapsed fold is hiding it, and reports whether
        // anything opened.
        //
        // Same reason as NextNavigableLine: an edit that rewrites a range of BUFFER
        // lines cannot tell which of them the user can actually see, so a columnar
        // write over rows 2..9 space-pads and deletes rows a fold hides without the
        // user ever seeing it (md/CodeFolding.md B8/B9). The default does nothing, so
        // every EditContext that is not a view - and every view while folding is off -
        // behaves exactly as before. COEditorView overrides it with the fold-aware
        // reveal that also re-snaps the caret and repaints.
        virtual bool RevealLine (int line) { return false; }

        // Text searching
        bool IsSearchTextEmpty () const;
        const wchar_t* GetSearchText () const;
        void SetSearchText (const wchar_t* str);
        void SetSearchOption (bool wholeWords, bool matchCase, bool regExpr, bool searchAll);

        bool Find (const EditContext*&, FindCtx&) const;
        bool Replace (FindCtx&);
        int  SearchBatch (const wchar_t* text, ESearchBatch mode);

        // Bookmark supporting
        bool IsBookmarked      (int line, EBookmarkGroup group) const;
        void SetBookmark       (int line, EBookmarkGroup group, bool on);
        void RemoveAllBookmark (EBookmarkGroup group);
        int  GetBookmarkNumber (EBookmarkGroup group);
        bool NextBookmark      (int& line, EBookmarkGroup group) const;
        bool PreviousBookmark  (int& line, EBookmarkGroup group) const;

        bool IsRandomBookmarked(int line, RandomBookmark& bookmarkId) const;
        void SetRandomBookmark (RandomBookmark bookmark, int line, bool on);
        bool GetRandomBookmark (RandomBookmark bookmark, int& line) const;
        // the bookmarks will be valid untill storage content is not changed
        const RandomBookmarkArray& GetRandomBookmarks () const;

        ELineStatus GetLineStatus (int line) const;
        bool FindById (LineId id, int& line) const;

        // Scan for reading and writing (see OpenEditor_Context_5.cpp)
        void StartScan (EBlockMode, const Square* = 0);
        bool Next ();
        bool Eof () const;
        void GetScanLine (std::wstring&) const;
        void GetScanPosition (int& line, int& start, int& end) const;
        void PutLine (const std::wstring&);
        void StopScan ();

        // Text manipulation utilities (see OpenEditor_Context_5.cpp)
        void ScanAndReplaceText (bool (pmfnDo)(const EditContext&, std::wstring&), bool curentWord);
        static bool LowerText (const EditContext&, std::wstring&);
        static bool UpperText (const EditContext&, std::wstring&);
        static bool CapitalizeText (const EditContext&, std::wstring&);
        static bool InvertCaseText (const EditContext&, std::wstring&);
        static bool TabifyText (const EditContext&, std::wstring&);
        static bool TabifyLeadingSpaces (const EditContext&, std::wstring&);
        static bool UntabifyText (const EditContext&, std::wstring&);
        static bool UntabifyLeadingSpaces (const EditContext&, std::wstring&);
        static bool ColumnLeftJustify (const EditContext&, std::wstring&);
        static bool ColumnCenterJustify (const EditContext&, std::wstring&);
        static bool ColumnRightJustify (const EditContext&, std::wstring&);
        static bool TrimLeadingSpaces (const EditContext&, std::wstring&);
        static bool TrimExcessiveSpaces (const EditContext&, std::wstring&);
        static bool TrimTrailingSpaces (const EditContext&, std::wstring&);

        // Settings
        const Settings& GetSettings () const;
        const DelimitersMap& GetDelimiters() const;

        //OEC_DECLARE_GET_PROPERTY(int, TabSpacing);
        //OEC_DECLARE_GET_PROPERTY(int, IndentSpacing);

        static const int MIN_TAB_SIZE = 1;
        static const int MAX_TAB_SIZE = 32;

        const int GetTabSpacing ()   const;
        const int GetIndentSpacing () const;

        OEC_DECLARE_GET_PROPERTY(int, IndentType);
        OEC_DECLARE_GET_PROPERTY(bool, TabExpand);
        OEC_DECLARE_GET_PROPERTY(bool, CursorBeyondEOL);
        OEC_DECLARE_GET_PROPERTY(bool, CursorBeyondEOF);

        OEC_DECLARE_GET_PROPERTY(bool, BlockKeepMarking               );
        OEC_DECLARE_GET_PROPERTY(bool, BlockKeepMarkingAfterDragAndDrop);
        OEC_DECLARE_GET_PROPERTY(bool, BlockDelAndBSDelete            );
        OEC_DECLARE_GET_PROPERTY(bool, BlockTypingOverwrite           );
        OEC_DECLARE_GET_PROPERTY(bool, BlockTabIndent                 );
        OEC_DECLARE_GET_PROPERTY(bool, ColBlockDeleteSpaceAfterMove   );
        OEC_DECLARE_GET_PROPERTY(bool, ColBlockCursorToStartAfterPaste);
        OEC_DECLARE_GET_PROPERTY(bool, ColBlockEditMode);
        OEC_DECLARE_GET_PROPERTY(bool, ColInsertRevealLine);
        OEC_DECLARE_GET_PROPERTY(string, MouseSelectionDelimiters     );

        OEC_DECLARE_GET_PROPERTY(bool, AutoCloseBrackets              );
        OEC_DECLARE_GET_PROPERTY(bool, AutoCloseBeforeWhitespaceOnly  );
        OEC_DECLARE_GET_PROPERTY(bool, AutoSurroundQuotes             );
        OEC_DECLARE_GET_PROPERTY(bool, AutoSurroundBrackets           );
        OEC_DECLARE_GET_PROPERTY(bool, AutoSurroundPlsqlQuoting       );

        OEC_DECLARE_GET_PROPERTY(string, PrintHeader);
        OEC_DECLARE_GET_PROPERTY(string, PrintFooter);
        OEC_DECLARE_GET_PROPERTY(int,    PrintMarginMeasurement);
        OEC_DECLARE_GET_PROPERTY(double, PrintLeftMargin);
        OEC_DECLARE_GET_PROPERTY(double, PrintRightMargin);
        OEC_DECLARE_GET_PROPERTY(double, PrintTopMargin);
        OEC_DECLARE_GET_PROPERTY(double, PrintBottomMargin);

        void HighlightCurrentLine (bool);
        bool IsCurrentLineHighlighted () const;

        LanguageSupportPtr GetLanguageSupport () const { return m_pStorage->GetLanguageSupport(); }


    private:
        Square m_ScanSquare;
        EBlockMode m_ScanBlockMode;
        int m_nScanCurrentLine, m_nScanStartInx, m_nScanEndInx;
        void calculateScanLine ();
    public:

        class UndoGroup : public Storage::UndoGroup
        {
        public:
            UndoGroup (EditContext&);
        };

        void PushInUndoStack (Position);
        void PushInUndoStack (EBlockMode, bool, Square);

        // Memory usage reporting
        void GetMemoryUsage (unsigned& usage, unsigned& allocated, unsigned& undo) const;
    private:
        friend UndoGroup;

        // string mustn't have '\r' or '\n'
        void InsertLinePart (int, int, const wchar_t*, int);
        void DeleteLinePart (int, int, int);
        void SplitLine      (int, int);

        void expandVirtualSpace (int line, int column);

        // Indentation on Enter, shared by eiAuto and eiSmart; the base is a POSITION
        // column, not a character index, so tabs count as their width.
        bool computeAutoIndentBase (Position&) const;
        bool applySmartIndent (Position caret, Position& base) const;

        // Index just past a line's leading whitespace, or -1 when the line is not in the
        // buffer; `length` receives its length. Shared by Enter's two whitespace rewrites.
        int  leadingWhitespaceEnd (int line, int& length) const;
        void clearLeadingWhitespace (int line);
        void clearLineIfBlank (int line);

        // Stage 3(a) dedent-while-typing. smartDedentColumn is a pure QUERY, so the caller opens an
        // undo group only once something will change; setLineIndent then performs the rewrite.
    protected:
        // Protected rather than private: the View calls both, and protected inheritance does
        // not make a PRIVATE base member reachable from a derived class.
        int  smartDedentColumn (Position caret) const;
        int  setLineIndent (int line, int wantColumn, Position caret);
        void insertAutoIndent (const Position&);

        // Shift+Tab with no selection: the POSITION column `line` outdents to, or -1 to leave it.
        // Pure query, rewritten via setLineIndent so both outdent routes share one tab handling.
        int  undentTargetColumn (int line) const;

        // True when `ch` would complete a line-start dedent token - smartDedentColumn's pre-filter
        // run one character EARLY, because the merge barrier must precede the character (§3.6).
        bool completesDedentToken (wchar_t ch, Position caret) const;

    private:
        bool adjustCursorPosition ();

    protected:
        int inx2pos (int, int) const;
        int inx2pos (const wchar_t*, int, int) const;
        int pos2inx (int, int, bool = false) const;
        int pos2inx (const wchar_t*, int, int, bool = false) const;
        void moveCurrentLine (int);
    private:
        int adjustPosByTab (int, int, bool = false) const;

        void line2buff (int, int, int, std::wstring&, bool = false) const;

        Position wordRight (bool hurry);
        Position wordLeft (bool hurry);

    protected:
        static bool getLine (const wchar_t* str, int& position, std::wstring&, bool&);

        // Step 3.1 find-as-you-type preview: COEditorView::PreviewSearch drives a
        // TRANSIENT Searcher directly against this context's storage - the shared
        // COEDocument::m_searcher must keep the committed text/options while the
        // modeless dialog is typing.
        Storage* GetStorage () const { return m_pStorage; }

    private:
        Storage* m_pStorage;

        Position m_curPos;
        int      m_orgHrzPos;
        bool     m_bDirtyPosition;
        bool     m_bCurrentLineHighlighted;

        Square  m_blkPos;
        EBlockMode m_BlockMode;
        bool    m_AltColumnarMode; // columnar mode was activated by pressed Alt key

        static bool tabify (const wchar_t*, int, std::wstring&, int startPos, int tabSpacing, bool leading);
        static bool untabify (const wchar_t*, int, std::wstring&, int startPos, int tabSpacing, bool leading);
    };

    ///////////////////////////////////////////////////////////////////////////

    struct SortCtx
    {
        int mKeyOrder;
        int mKeyStartColumn;
        int mKeyLength;
        bool mRemoveDuplicates;
        bool mIgnoreCase;

        SortCtx () { memset(this, 0, sizeof(*this)); }
    };

    ///////////////////////////////////////////////////////////////////////////


    inline
    bool EditContext::IsLocked  () const
    {
        return m_pStorage->IsLocked();
    }

    inline
    void EditContext::Lock (bool lock)
    {
        m_pStorage->Lock(lock);
    }

    inline
    int EditContext::GetCursorLine () const
    {
        return m_curPos.line;
    }

    inline
    int EditContext::GetCursorColumn () const
    {
        return m_curPos.column;
    }

    inline
    Position EditContext::GetPosition () const
    {
        return m_curPos;
    }

    inline
    int EditContext::InxToPos (int line, int col) const
    {
        if (line < GetLineCount())
            return inx2pos(line, col);
        return col;
    }

    inline
    int EditContext::PosToInx (int line, int col, bool force) const
    {
        if (line < GetLineCount())
            return pos2inx(line, col, force);
        return col;
    }

    inline
    int EditContext::GetLineCount () const
    {
        return m_pStorage->GetLineCount();
    }

    inline
    LineId EditContext::GetLineId (int line) const
    {
        return m_pStorage->GetLineId(line);
    }

    inline
    void EditContext::GetLineA (int line, const char*& ptr, int& len) const
    {
        m_pStorage->GetLineA(line, ptr, len);
    }

    inline
    void EditContext::GetLineW (int line, OEStringW& str) const
    {
        m_pStorage->GetLineW(line, str);
    }

    inline
    bool EditContext::CanUndo () const
    {
        return !IsLocked() && m_pStorage->CanUndo();
    }

    inline
    bool EditContext::CanRedo () const
    {
        return !IsLocked() && m_pStorage->CanRedo();
    }

    inline
    bool EditContext::IsSelectionEmpty () const
    {
        return m_blkPos.is_empty();
    }

    inline
    void EditContext::GetSelection (Square& pos) const
    {
        if (m_blkPos.is_empty())
            pos.start = pos.end = m_curPos;
        else
            pos = m_blkPos;
    }

    inline
    void EditContext::SetBlockMode (EBlockMode mode, bool altColumnarMode /*= false*/)
    {
        m_BlockMode = mode;
        m_AltColumnarMode = altColumnarMode;
        InvalidateLines(m_blkPos.start.line, m_blkPos.end.line);
    }

    inline
    EBlockMode EditContext::GetBlockMode () const
    {
        return m_BlockMode;
    }

    inline
    bool EditContext::IsAltColumnarMode () const
    {
        return m_BlockMode == ebtColumn && m_AltColumnarMode;
    }

    inline
    void EditContext::ScanMultilineQuotes (int to_line, int& state, int& quoteId, bool& parsing, wchar_t& qClose) const
    {
        _ASSERTE(m_pStorage);
        m_pStorage->ScanMultilineQuotes(to_line, state, quoteId, parsing, qClose);
    }

    inline
    SequenceId EditContext::GetActionId () const
    {
        _ASSERTE(m_pStorage);
        return m_pStorage->GetActionId();
    }

    inline
    MultilineQuotesScanner& EditContext::GetMultilineQuotesScanner ()
    {
        _ASSERTE(m_pStorage);
        return m_pStorage->GetMultilineQuotesScanner();
    }

    // Text searching
    inline
    bool EditContext::IsSearchTextEmpty () const
    {
        _ASSERTE(m_pStorage);
        return m_pStorage->IsSearchTextEmpty();
    }

    inline
    const wchar_t* EditContext::GetSearchText () const
    {
        _ASSERTE(m_pStorage);
        return m_pStorage->GetSearchText();
    }

    inline
    void EditContext::SetSearchText (const wchar_t* str)
    {
        _ASSERTE(m_pStorage);
        m_pStorage->SetSearchText(str);
    }

    inline
    void EditContext::SetSearchOption (bool wholeWords, bool matchCase, bool regExpr, bool searchAll)
    {
        _ASSERTE(m_pStorage);
        m_pStorage->SetSearchOption(wholeWords, matchCase, regExpr, searchAll);
    }

    // Bookmark supporting
    inline
    bool EditContext::IsBookmarked (int line, EBookmarkGroup group) const
    {
        _ASSERTE(m_pStorage);
        return m_pStorage->IsBookmarked(line, group);
    }

    inline
    void EditContext::SetBookmark (int line, EBookmarkGroup group, bool on)
    {
        _ASSERTE(m_pStorage);
        m_pStorage->SetBookmark(line, group, on);
    }

    inline
    void EditContext::RemoveAllBookmark (EBookmarkGroup group)
    {
        _ASSERTE(m_pStorage);
        m_pStorage->RemoveAllBookmark(group);
    }

    inline
    int EditContext::GetBookmarkNumber (EBookmarkGroup group)
    {
        _ASSERTE(m_pStorage);
        return m_pStorage->GetBookmarkNumber(group);
    }

    inline
    bool EditContext::NextBookmark (int& line, EBookmarkGroup group) const
    {
        _ASSERTE(m_pStorage);
        return m_pStorage->NextBookmark(line, group);
    }

    inline
    bool EditContext::PreviousBookmark (int& line, EBookmarkGroup group) const
    {
        _ASSERTE(m_pStorage);
        return m_pStorage->PreviousBookmark(line, group);
    }

    inline
    bool EditContext::IsRandomBookmarked (int line, RandomBookmark& bookmarkId) const
    {
        _ASSERTE(m_pStorage);
        return m_pStorage->IsRandomBookmarked(line, bookmarkId);
    }

    inline
    void EditContext::SetRandomBookmark (RandomBookmark bookmark, int line, bool on)
    {
        _ASSERTE(m_pStorage);
        m_pStorage->SetRandomBookmark(bookmark, line, on);
    }

    inline
    bool EditContext::GetRandomBookmark (RandomBookmark bookmark, int& line) const
    {
        _ASSERTE(m_pStorage);
        return m_pStorage->GetRandomBookmark(bookmark, line);
    }

    inline
    const RandomBookmarkArray& EditContext::GetRandomBookmarks () const
    {
        _ASSERTE(m_pStorage);
        return m_pStorage->GetRandomBookmarks();
    }

    inline
    bool EditContext::FindById (LineId id, int& line) const
    {
        _ASSERTE(m_pStorage);
        return m_pStorage->FindById(id, line);
    }

    inline
    ELineStatus EditContext::GetLineStatus (int line) const
    {
        _ASSERTE(m_pStorage);
        return m_pStorage->GetLineStatus(line);
    }

    inline
    EditContext::
    UndoGroup::UndoGroup (EditContext& context)
    : Storage::UndoGroup(*context.m_pStorage)
    {
    }

    // Memory usage reporting
    inline
    void EditContext::GetMemoryUsage (unsigned& usage, unsigned& allocated, unsigned& undo) const
    {
        _ASSERTE(m_pStorage);
        m_pStorage->GetMemoryUsage(usage, allocated, undo);
    }

    inline
    const Settings& EditContext::GetSettings () const
    {
        _ASSERTE(m_pStorage);
        return m_pStorage->GetSettings();
    }

    inline
    const DelimitersMap& EditContext::GetDelimiters() const
    {
        _ASSERTE(m_pStorage);
        return m_pStorage->GetDelimiters();
    }

    // the current line highlighting 
    inline
    void EditContext::HighlightCurrentLine (bool enable)
    {
        m_bCurrentLineHighlighted = enable;
    }

    inline
    bool EditContext::IsCurrentLineHighlighted () const
    {
        return m_bCurrentLineHighlighted;
    }

    inline
    void EditContext::moveCurrentLine (int line)
    {
        m_curPos.line = line;
    }

    inline
    const int EditContext::GetTabSpacing () const 
    { 
        _ASSERTE(m_pStorage); 
        return min(MAX_TAB_SIZE, max(MIN_TAB_SIZE, m_pStorage->GetSettings().GetTabSpacing())); 
    }
    
    inline
    const int EditContext::GetIndentSpacing () const 
    { 
        _ASSERTE(m_pStorage); 
        return min(MAX_TAB_SIZE, max(MIN_TAB_SIZE, m_pStorage->GetSettings().GetIndentSpacing())); 
    }

};

#endif//__OEContext_h__
