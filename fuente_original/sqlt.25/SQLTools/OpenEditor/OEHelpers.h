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

#ifndef __OEHelpers_h__
#define __OEHelpers_h__

#include <string>
#include <vector>
#include <map>
#include <COMMON/QuickArray.h>
#include <COMMON/OEString.h>
#include <COMMON/Fastmap.h>
#include <COMMON/NVector.h>


namespace OpenEditor
{
    using std::string;
    using std::vector;
    using std::pair;
    using std::map;
    using std::shared_ptr;
    using Common::FastmapW;
    using Common::OEString;

    class Storage;

    enum EBlockMode { ebtStream, /*ebtLine,*/ ebtColumn, ebtUndefined };

    enum EIndent { eiNone = 0, eiAuto = 1, eiSmart = 2 };

    enum ELineStatus { elsNothing = 0, elsUpdated = 1, elsUpdatedSaved = 2, elsRevertedBevoreSaved = 3 };

    const int STRING_ID_SIZE         = 24;
    const int STRING_STATUS_SIZE     =  2;
    const int BOOKMARK_GROUPS_SIZE   =  2;
    const int RANDOM_BOOKMARK_SIZE   =  4;
    const unsigned RANDOM_BOOKMARK_NUMBER = 10U;

    enum EBookmarkGroup { eBmkGroup1 = 0, /*eBmkGroup2, eBmkGroup3,*/ eRandomBmkGroup };

    class EditContextBase
    {
    public:
        virtual void OnChangedLines (int, int) = 0;
        virtual void OnChangedLinesQuantity (int, int) = 0;
        virtual void OnChangedLinesStatus (int, int) = 0;
        virtual void OnSettingsChanged () = 0;
    };

    union Position
    {
        struct
        {
            int column;
            int line;
        };
        int coordinate[2];

        bool operator == (const Position other) const;
        bool operator != (const Position other) const;
        bool operator < (const Position other) const;
    };

    union Square
    {
        struct
        {
            Position start, end;
        };
        int coordinate[4];

        void clear ()                       { memset(this, 0, sizeof(*this)); }
        bool is_empty () const              { return !(start != end); }
        bool normalize ()                   { if (!(start < end)) { std::swap(start, end); return true; } return false; }
        void swap ()                        { std::swap(start, end); }
        void normalize_columns ()           { if (!(start.column < end.column)) std::swap(start.column, end.column); }
        bool line_in_rect (int line) const  { return start.line <= line && line <= end.line; }
        bool column_in_rect (int col) const { return start.column <= col && col <= end.column; }
        bool operator != (const Square& other) const;
    };

    enum EOutlineItemType
    {
        oitBlock,
        oitFunction,
        oitProcedure,
        oitPackage,
        oitPackageBody,
        oitTypeBody,
        oitType,            // CREATE TYPE ... AS OBJECT
        oitTypeMemberFunction,  // FUNCTION member/static/constructor spec inside a type
        oitTypeMemberProcedure, // PROCEDURE member/static/constructor spec inside a type
        oitSelect,          // top-level SELECT / UNION leg
        oitFromClause,    
        oitWhereClause,
        oitComment,         // a run of consecutive comment lines - folding only

        // Folding-only statement kinds (2026-09-23). Every one of these is a node
        // the PL/SQL parser ALREADY builds - they simply returned the base
        // oitUnknown, fell to `default:` in the outline walk and were recursed
        // through, so they were transparent to BOTH consumers and nothing inside
        // them could fold. They are now emitted to the FOLD sink only, which
        // is what keeps the Code Outline pane showing exactly what it always
        // showed while folding gains every multiline IF / CASE / loop / DML /
        // cursor / trigger / generic statement in the file. Single-line ones are
        // not emitted at all (D10 would drop them anyway).
        //
        // Appended AFTER oitComment so that no existing enumerator changes value,
        // and still ahead of the oitUnknown sentinel that FoldModel::FilterItems
        // uses to reject an item whose range cannot be trusted.
        oitIf,              // IF .. END IF;
        oitCase,            // CASE .. END CASE; and the CASE .. END expression
        oitLoop,            // FOR / WHILE / bare LOOP .. END LOOP;
        oitInsert,          // INSERT
        oitUpdate,          // UPDATE
        oitDelete,          // DELETE
        oitOpenCursor,      // OPEN cur [FOR ...];
        oitTrigger,         // CREATE [OR REPLACE] TRIGGER
        oitStatement,       // any other recognised statement: GRANT, ALTER, JAVA ...

        oitMerge,           // MERGE INTO .. USING .. ON .. WHEN .. THEN .. ;

        oitUnknown          // sentinel, keep last
    };

    // ONE outline walk, TWO products (md/CodeFolding.md section 3.1).
    //
    // The tree traversal has always had two consumers wanting opposite things
    // from it, and for most of this feature's life they shared a single vector:
    // the walk built a fully enriched item for every construct it saw, folding
    // read geometry out of it and ignored the rest, and the pane read a
    // `paneVisible` projection of it. That made the EXPENSIVE payload the common
    // case and the cheap one the exception, which is the wrong way round -
    // folding wants many more items than the pane does, so the enriched path ran
    // over the LARGER set and was then thrown away for most of it. The cost could
    // not be avoided either: folding is what keeps the walk warm
    // (OEDocument.cpp:297-301 pulls the snapshot even with the pane hidden, which
    // is what stops a hidden pane starving folding), so a CLOSED Code Outline pane
    // still paid for every BuildNodeLabel and every ExtractSnippet on every
    // keystroke and discarded all of it.
    //
    // They are separate models now, filled by the same walk:
    //
    //   FoldCandidate - geometry only. POD, no heap, and significantly MORE of
    //                   them: anonymous blocks, comment runs, every multiline
    //                   statement, and every SELECT leg at whatever depth it
    //                   sits. This is all folding ever consumed - `label` had no
    //                   reader anywhere in OEFolding.cpp, and neither did a
    //                   column.
    //   OutlineItem   - the enriched PANE entry: a display label plus geometry,
    //                   built only for what the pane is allowed to list.
    //
    // The split is what makes the label work SKIPPABLE rather than merely
    // discarded: PlSqlParseProduct::GetOutlineProducts takes the pane sink as a
    // POINTER, and a null one means no label builder and no snippet extraction
    // runs at all. PlSqlSupport passes null unless the pane is really going to
    // look, which is where the saving comes from.
    //
    // What the STORED `paneVisible` field used to encode is now structural. It was
    // false for oitBlock and oitComment and inherited by everything nested inside a
    // block, so a subprogram that only became reachable through an anonymous DECLARE /
    // BEGIN..END stayed out of the pane while becoming foldable. The same rule holds -
    // the walk simply emits such an item to the fold sink and not to the pane one.
    //
    // Be precise about what was removed, because the NAME survives. Gone is the
    // per-item field on OutlineItem and the read-time projection that filtered on it.
    // Still very much present is `paneVisible` as a PARAMETER of the walk
    // (GetOutlineItemsWithCtx and the Emit* helpers): it is the inherited answer to
    // "may the pane show what this subtree produces", seeded true at the root and
    // forced false by an anonymous block, and it is what each emit site tests before
    // pushing to the pane sink. That is not a leftover - it is the mechanism.
    //
    // The difference is where the answer lives. It used to be recorded on the item and
    // re-interpreted later by a reader, so the field and the vector the reader built
    // could drift apart. Now it is consumed at the emit site and the only record of it
    // is which sink the item landed in, so there is no stored flag left that could
    // disagree with that. Grepping `paneVisible` therefore finds live code, not a
    // fossil; what a grep will NOT find is any reader filtering on it after the fact.
    //
    // `rangeTrusted` is ToDoNotes 107 step 3. It says whether `pos` was measured
    // from real syntax or invented by a parse that gave up early. Two ways the
    // parse gives up, and both leave a range whose END is fiction:
    //   - a construct FAILED. PlSqlAnalyzer::PutToken sets m_error and stops
    //     feeding the tree (OEPlsSqlSyntax.cpp:311), then CloseAll force-closes
    //     every node below level 1 with Colapse() and marks the level-1 unit
    //     failed (OEPlsSqlSyntax.cpp:233-243). So the node carrying IsFailed() is
    //     the enclosing top-level unit, while the items whose ends were actually
    //     invented are the ones Colapse()d underneath it - which is why this flag
    //     is per item rather than per snapshot.
    //   - a configured LIMIT cut the parse short (PlSqlFileMaxLines /
    //     PlSqlFileMaxTokens), so nothing past the cut was ever looked at.
    // The walk stamps it false when the item's own subtree contains a failed
    // node. Defaults to true, so every emit site that has nothing to say about
    // trust - comment runs, whose extents come from real comment tokens the
    // lexer recorded rather than from parse structure - keeps folding exactly as
    // it did before this field existed.
    //
    // MEASUREMENT STATUS: measured, and the rule the transcript chose is CLAMP,
    // NOT DROP - which is why this field has a reader in the folding layer and
    // not a filter.
    //
    // The reader is RegionEndLines (OEFolding.cpp:165-202), which tests the flag
    // at :192 and pulls an untrusted end landing EXACTLY on the parse cut back to
    // the line before it, so the construct the parser force-closed there stops
    // short of the end it invented. FilterItems deliberately does NOT test it
    // (:222-225): dropping the range was tried and measured, and it regressed the
    // anti-thrash guarantee, because removing a region removes its identity and
    // so pops the user's fold open on the next rebuild.
    //
    // The snapshot-level gate this field was originally measured against is gone
    // as well - step 6 removed the wholesale refusal in RebuildFromSnapshot once
    // the per-item clamp made it redundant AND harmful (md/CodeFolding.md, the
    // stage 8 note at :464-467). The folding suite's `.outlinetrust` directive
    // still prints the flag, now as a regression oracle on the rule above rather
    // than as the measurement probe it started life as.
    // `pos` keeps its columns even though OEFolding.cpp reads only `.line` out of
    // it. They are free here in a way a label is not: every emit site already has
    // the token's offset in hand, whereas a label needs a scan over the subtree or
    // a copy of the source text. Keeping them costs 8 bytes of POD and preserves
    // the automated suite's `.outlinetrust` transcript, which renders a candidate
    // with the same FmtSquare spelling `.expect region` uses, so a range there and
    // a region here stay the same thing on the page.
    //
    // `type` is transient in the same way: FilterItems' D9 and D10 rules ask which
    // construct a candidate is, and so does the pane-leak oracle, but no FoldRegion
    // carries one. It had a field of its own there once and was removed for having
    // no reader (OEFolding.h:49-53).
    struct FoldCandidate
    {
        Square           pos;      // start/end positions (column first, then line)
        int              depth;    // 0 = top-level, 1 = nested inside package
        EOutlineItemType type;
        bool             rangeTrusted = true;
    };
    using FoldCandidates = std::vector<FoldCandidate>;

    // The Code Outline PANE's view of one entry: what the pane lists, and what
    // clicking a listed entry navigates to. Both halves of `pos` are read here -
    // the pane indents by depth and navigates to the exact offset - which is the
    // difference from FoldCandidate above.
    struct OutlineItem
    {
        std::wstring     label;    // e.g. L"PACKAGE myschema.mypkg"
        Square           pos;      // start/end positions (column first, then line)
        int              depth;    // 0 = top-level, 1 = nested inside package
        EOutlineItemType type;
    };
    using OutlineItems = std::vector<OutlineItem>;

    // True for the outline item types that exist purely so that folding gets a
    // region: anonymous blocks, comment runs and the folding-only statement
    // kinds. None of them may ever reach the Code Outline pane, so THIS is the one
    // list that says "folding only" - and with the two products split apart it is
    // the list the walk consults to decide WHICH SINK an item goes to, rather than
    // a flag it stamps onto a shared item. Adding a type here can change what folds
    // but never what the pane lists, and the .paneoutline directive in the editor
    // script harness still fails the run if an item of one of these types ever
    // turns up in the pane's vector: the same guarantee, expressed against the new
    // shape.
    inline bool IsFoldingOnlyOutlineType (EOutlineItemType type)
    {
        switch (type)
        {
        case oitBlock:
        case oitComment:
        case oitIf:
        case oitCase:
        case oitLoop:
        case oitInsert:
        case oitUpdate:
        case oitDelete:
        case oitMerge:
        case oitOpenCursor:
        case oitTrigger:
        case oitStatement:
            return true;
        default:
            return false;
        }
    }

#pragma pack(push, 1)
    struct Tag
    {
        unsigned status   : STRING_STATUS_SIZE;
        unsigned id       : STRING_ID_SIZE;
        unsigned bookmark : RANDOM_BOOKMARK_SIZE;
        unsigned bmkmask  : BOOKMARK_GROUPS_SIZE;
    };

    template <class T>
    class StorageString : public Common::OEString<T>
    {
    public:
        // overflow of 24-bit id is theoretically possible 
        // - we can either expand it to 32-bit integer 
        // - or assign id only on request "GetLineId"
        Tag tag;

        StorageString ();
        StorageString (const StorageString& other);
        void operator = (const StorageString& other);

        ELineStatus get_status () const      { return (ELineStatus)tag.status; }
        void set_status (ELineStatus status) { tag.status = status; }
    };
#pragma pack(pop)

    typedef StorageString<char> StorageStringA;
    typedef Common::QuickArray<StorageStringA> StringArrayA;
    typedef StorageString<wchar_t> StorageStringW;
    typedef Common::QuickArray<StorageStringW> StringArrayW;

    struct DelimitersMap : FastmapW<bool>
    {
        DelimitersMap ();
        DelimitersMap (const char*); // utf8 string

        void Set (const char*);
        void Get (string&);

        static const char* m_cszDefDelimiters;
    };

    class LineId
    {
    public:
        LineId ()                                               : m_id(UINT_MAX) {}
        explicit LineId (unsigned id)                           : m_id(id) {}

        unsigned GetId () const                                 { return m_id; }
        bool Valid() const                                      { return m_id != UINT_MAX; }
        int  operator != (const LineId& other) const            { return m_id != other.m_id; }
        int  operator == (const LineId& other) const            { return m_id == other.m_id; }
    private:
        unsigned m_id;
    };

    class SequenceId
    {
    public:
        SequenceId ()                                           : m_id(NONE_VAL) {}
        int  operator != (const SequenceId& other) const        { return m_id != other.m_id; }
        int  operator == (const SequenceId& other) const        { return m_id == other.m_id; }
        unsigned operator - (const SequenceId& other) const     { return m_id - other.m_id; }
        unsigned ToUInt () const                                { return m_id; } 
        static const unsigned NONE_VAL  = UINT_MAX;
        static const unsigned START_VAL = 1000U;
    protected:
        SequenceId (unsigned seq)                               : m_id(seq) {}
        unsigned GetNextVal ()                                  { _ASSERTE(m_id >= START_VAL); return ++m_id; }
        void operator ++ (int)                                  { _ASSERTE(m_id >= START_VAL); m_id++; }
        unsigned m_id;
    };

    class Sequence : public SequenceId
    {
    public:
        Sequence ()                                             : SequenceId(START_VAL) {}
        void Reset ()                                           { m_id = START_VAL; }
        using SequenceId::GetNextVal;
        using SequenceId::operator ++;
    private:
        Sequence (const Sequence&);
        Sequence& operator = (const Sequence&);
    };

    class RandomBookmark
    {
    public:
        enum Range { FIRST = 0, LAST = RANDOM_BOOKMARK_NUMBER - 1 };

        RandomBookmark ()                                       : m_id(0) {}
        explicit RandomBookmark (unsigned char val)             : m_id(val) { _ASSERTE(Valid()); }
        explicit RandomBookmark (Range val)                     : m_id(static_cast<unsigned char>(val)) {}

        unsigned char GetId () const                            { return m_id; }

        bool Valid() const                                      { return m_id <= LAST; }
        void operator ++ (int)                                  { m_id++; }
    private:
        unsigned char m_id;
    };

    class RandomBookmarkArray
    {
    public:
        RandomBookmarkArray ()                                  { Reset(); }

        int  GetLine (RandomBookmark bookmark) const            { return bookmark.Valid() ? m_lines[bookmark.GetId()] : -1; }
        void SetLine (RandomBookmark bookmark, int line)        { if (bookmark.Valid()) m_lines[bookmark.GetId()] = line; }

        const SequenceId& GetActionId () const                  { return m_actionId; }
        void  SetActionNumber (const SequenceId& val)           { m_actionId = val; }

        void Reset ()                                           { for (size_t i = 0; i < sizeof(m_lines)/sizeof(m_lines[0]); i++) m_lines[i] = -1; }

    private:
        SequenceId m_actionId;
        int m_lines[RANDOM_BOOKMARK_NUMBER];

        RandomBookmarkArray (const RandomBookmarkArray&);
        RandomBookmarkArray& operator = (const RandomBookmarkArray&);
    };

    class LineTokenizer
    {
    public:
        static const wchar_t cbSpaceChar;
        static const wchar_t cbVirtSpaceChar;
        static const wchar_t cbTabChar;

        LineTokenizer (bool showWhiteSpace, int tabSpacing, const DelimitersMap&);

        void StartScan (const wchar_t* str, int len);
        bool Next ();
        bool IsSpace () const   { return iswspace(m_Buffer[m_Offset]) ? true : false; }
        bool Eol () const       { return m_Offset == m_Length; }

        void GetCurentWord (const wchar_t*& str, int& pos, int& len) const;

        void EnableProcessSpaces (bool enable) { m_processSpaces = enable; }

    private:
        const DelimitersMap m_delimiters;
        const wchar_t* m_Buffer;
        int   m_Length,
              m_Offset,
              m_Position,
              m_TabSpacing;
        bool  m_showWhiteSpace, m_processSpaces;
        wchar_t m_spaceChar;
        wchar_t m_virtSpaceChar;
        wchar_t m_tabChar;
        mutable std::wstring m_Whitespaces;
    };

    class MultilineQuotesScanner
    {
    public:
        MultilineQuotesScanner (Storage&);

        bool IsCaseSensitive () const;
        void SetCaseSensitive (bool);

        void ClearSettings ();
        void SetParsingQuotes    (const pair<string, string>&);
        int  AddStartLineQuotes  (const string&);
        int  AddSingleLineQuotes (const string&);
        int  AddMultilineQuotes  (const pair<string, string>&);
        void AddEscapeChar       (const string&);
        void SetDelimitersMap    (const DelimitersMap&);
        const DelimitersMap& GetDelimitersMap () const;

        // Oracle alternative quoting: q'X ... X' and nq'X ... X'.
        // See md/Oracle alternative quoting (q-quote).md, section 4.
        //
        // `quote` is the character that both opens an ordinary string and follows
        // the q of a q-literal - the language's StringPairFirst, i.e. exactly what
        // CommonHighlighter::qQuoteChar() derives - and 0 disables the construct.
        // A single value rather than a bool plus a character, so the gate and the
        // quote cannot drift apart. Languages that do not opt in (P4) keep `q` as an
        // ordinary identifier letter and never get the fastmap bit.
        void SetAlternativeQuotes (wchar_t quote);

        // Cross-line quote state reported by Scan/ScanLine and consumed by
        // HighlighterBase::SetMultilineQuotesState.
        //
        // qsAlternative is a *distinct state value* and not a sentinel quoteId:
        // SetMultilineQuotesState remaps quoteId positionally onto the language's
        // comment/string/char pairs, and a q-literal's delimiter is chosen at
        // runtime, so it has no slot in that table. Keeping the states apart is what
        // leaves that mapping intact (section 4.5 item 6). For qsAlternative,
        // quoteId is meaningless and qClose carries the delimiter; for every other
        // state qClose is 0.
        enum EQuoteState
        {
            qsPlain       = 0,
            qsMultiline   = 1,
            qsAlternative = 2,
        };

        void Invalidate (int from_line);
        void Scan (int to_line, int& state, int& quoteId, bool& parsing, wchar_t& qClose);
        // `qClose` is a reference parameter and never a member: ScanLine is const and
        // QuoteBalanceChecker runs two independent scans through one instance
        // (section 4.5 item 5).
        //
        // Probe: lets one walk answer "is index N code, or inside a quote/comment?"
        // for MANY N at once. `positions` must be sorted ascending and hold no
        // duplicates; `isCode` receives one byte per position and the caller must
        // pre-size it. ScanLine only ever writes 1, so a zero-filled `isCode` already
        // means "not code" for every index the walk never reached - the ones it stepped
        // over with `i += ...`, and the ones past an `ignoreRemainder` break. That is
        // the same verdict the per-position `parsingLength` test gave, which is why
        // `parsingLength` is redundant once a probe is supplied.
        //
        // This exists because answering the question per position by restarting the
        // scan is O(N*L): CommonLanguageSupport::readTokens used to call ScanLine once
        // per brace token with len = that token's offset, so a single 435 K-character
        // line - one JSON document, ~20 K braces - summed to ~10^10 character steps and
        // froze the idle handler that HighlightBraces runs in. One probed walk is O(L).
        struct ScanProbe
        {
            const int* positions;
            size_t     count;
            char*      isCode;
        };

        bool ScanLine (const wchar_t* str, int len, int& state, int& quoteId, bool& parsing, wchar_t& qClose, int* parsingLength = 0, const ScanProbe* probeReq = 0) const;

    private:
        void buildMap (int to_line);
        void buildOpeningFastMap () const;
        bool is_equal (const wchar_t* str, int length, int offset, const std::wstring& shape) const;
        // Length of the q-quote prefix ("q" or "nq") at `offset`, 0 when there is
        // none; on success `closer` receives the character that terminates the
        // literal.
        int  qQuotePrefix (const wchar_t* str, int len, int offset, wchar_t& closer) const;

        Storage& m_Storage;
        DelimitersMap m_delimiters;

        bool m_bCaseSensitive;
        vector<std::wstring> m_StartLineQuotes;  // actual from line start to end
        vector<std::wstring> m_SingleLineQuotes; // actual any line position to end
        vector<pair<std::wstring, std::wstring> > m_MultilineQuotes; // opening & closing quotes pair
        std::wstring m_escapeChar;
        wchar_t m_qQuoteChar; // 0 when the language has no alternative quoting

        enum eQuoteType
        {
            eqtStartLine = 0x01,
            // A dedicated bit rather than eqtOpeningMultiline: reusing it would push
            // every q/Q in the file - including the ones inside UNIQUE, SEQUENCE,
            // FREQUENCY - through the linear m_MultilineQuotes scan (section 4.2).
            eqtQQuote = 0x02,
            eqtSingleLine = 0x04,
            eqtOpeningMultiline = 0x08,
            eqtOpeningParse = 0x10,
            eqtClosingParse = 0x40,
        };
        mutable FastmapW<char> m_OpeningFastMap;
        mutable bool m_OpeningFastMapDirty;

        struct Entry
        {
            int state;
            int quoteId;
            bool parsing;
            wchar_t qClose;
            Entry (int _state, int _quoteId, bool _parsing, wchar_t _qClose)
                : state(_state), quoteId(_quoteId), parsing(_parsing), qClose(_qClose) {}
        };
        map<int, Entry> m_mapQuotes;
        int m_nQuotesValidLimit;

        bool m_bParsingAlways;
        pair<std::wstring, std::wstring> m_ParsingQuotes; // <> for XML
    };

    enum ESearchBatch { esbCount, esbMark, esbReplace };

    struct FindCtx
    {
        // in-out
        Storage* m_storage;
        int m_line;
        int m_start; 
        int m_end;
        // in
        bool m_thruEof;
        bool m_skipFirstNull;
        // out
        bool m_eofPassed;
        // in
        std::wstring m_text;

        // Per-call search direction (modeless conversion plan, Steps 1+2.5:
        // md/editor_search_modeless_conversion_plan.md). false = forward (toward
        // EOF, Find Next), true = backward (toward BOF, Find Previous).
        //
        // The searcher is an app-wide singleton (COEDocument::m_searcher,
        // OEDocument.cpp:98) shared by every Storage, so the direction MUST
        // travel per call: a modeless widget allows another view's search to
        // run mid-operation, and any persistent flag would be observed (and
        // raced) across operations. Since Step 2.5 this is the only direction
        // source - Searcher keeps no direction state at all.
        //
        // Text/case/regexp/wholeWords/searchAll remain persistent settings,
        // changed only deliberately (dialog SaveOption).
        bool m_backward;

        FindCtx () 
        { 
            m_storage = 0;
            m_line = m_start = m_end = 0;
            m_thruEof = true;
            m_skipFirstNull = true;
            m_eofPassed = false;
            m_backward = false;
        }
    };

    class Searcher
    {
    public:
        Searcher (bool persistent = false);
        virtual ~Searcher ();

        void AddRef    (Storage*);
        void RemoveRef (Storage*);

        bool IsTextEmpty () const;
        const wchar_t* GetText () const;
        void SetText (const wchar_t* str);
        // Step 2.5 cleanup: no direction here - it is per-call in FindCtx
        // (m_backward). GetOption/IsBackwardSearch/SetBackwardSearch died with
        // the persistent flag (their only remaining reader was a commented-out
        // block in the Find dialog ctor) and are deleted.
        void SetOption (bool wholeWords, bool matchCase, bool regExpr, bool searchAll);

        // Step 3.2 follow-up ("n of m" counter, md/editor_search_modeless_conversion_plan.md):
        // a read-only probe of the committed searchAll option, so RepeadSearch can
        // mark a per-document counter with the " in this file" suffix when "Search
        // All Windows" is checked. Deliberately NOT a revival of the general
        // GetOption (deleted in Step 2.5): options stay write-only through
        // SetOption; this exposes the single flag the counter's display policy
        // needs and mutates nothing.
        bool IsSearchAll () const { return m_bSearchAll; }

        bool Find (FindCtx&) const;
        bool Replace (FindCtx&) const; // m_end is in/out
        // changedLines, when non-null, receives the BUFFER line number of every
        // occurrence esbReplace actually rewrote IN `_storage`, under the same
        // `_storage == ctx.m_storage` guard that protects `last` - so "Search All
        // Windows" never reports another storage's lines to this one's view.
        //
        // Callers need it because `last` is overwritten on every pass and holds only
        // the FINAL match, so it cannot bound a reveal: a Replace All that touched
        // hidden rows earlier in the document would leave them modified behind a
        // fold that never opened (md/CodeFolding.md D8).
        //
        // Ignored for esbCount (read-only) and esbMark (bookmark NAVIGATION already
        // reveals via MoveToAndCenter, D5; marking alone does not).
        int  DoBatch (Storage*, const wchar_t* text, ESearchBatch mode, Square& last,
                      vector<int>* changedLines = 0);

    private:
        void compileExpression () const;
        bool isSelectionMatched (const FindCtx&) const;
        bool isMatched (const wchar_t* str, int offset, int len, const DelimitersMap&, int& start, int& end) const;
        bool isMatchedBackward (const wchar_t* str, int offset, int len, const DelimitersMap&, int& start, int& end) const;

        Searcher (const Searcher&);   // not supported
        void operator = (const Searcher&); // not supported

        std::wstring m_strText;

        bool m_bWholeWords;
        bool m_bMatchCase;
        bool m_bRegExpr;
        bool m_bSearchAll;

        struct RegexpCxt; // for isolation from "boost"
        mutable RegexpCxt* m_cxt;
        mutable bool m_bPatternCompiled;

        bool m_bPersistent;
        vector<Storage*> m_refList;
    };

    
    struct LineStatus
    {
        bool syntaxError;
        int  beginCounter;
        int  endCounter;
        int  level;

        // True for a buffer line the language support never PARSED, because the parse
        // stopped short of the end of the buffer on a configured limit
        // (PlSqlFileMaxLines / PlSqlFileMaxTokens). ToDoNotes 107 step 4, answering
        // note 102's gutter question.
        //
        // It exists because the four fields above cannot express it. They are filled by
        // a walk over the syntax tree, and a line past the cut is in no subtree, so it
        // keeps these defaults - level 0, syntaxError false - which is precisely how a
        // CLEAN TOP-LEVEL line reads. The gutter then renders the tail as valid
        // top-level code, and under truncation that is not a transient "parse still
        // running" state but the permanent one: the parse is finished and will never
        // look at those lines. This flag is the difference between "no structure here"
        // and "nothing is known about here".
        //
        // Deliberately NOT accompanied by a level. Extending the last parsed line's
        // level into the tail would assert knowledge the parse does not have and would
        // draw as a real indent guide - a worse lie than the blank. Consumers paint the
        // band instead of the guides.
        bool unparsed;

        // True on the FIRST unparsed line only, and only when that line falls inside
        // the window the map was filled for. It marks the cut itself, so the gutter can
        // draw one boundary rule where the parsed text ends rather than banded or
        // unbanded rows everywhere. Not set when the window starts below the cut, or a
        // view scrolled wholly into the tail would stamp its own top row and the
        // marker would appear to travel as the user scrolls.
        bool unparsedBoundary;

        LineStatus ()
            : syntaxError(false), 
            beginCounter(0), 
            endCounter(0),
            level(0),
            unparsed(false),
            unparsedBoundary(false)
            {}
    };

    class LineStatusMap
    {
    public:
        LineStatusMap (int baseLine, int count)
            : m_data("LineStatusMap", count),
            m_baseLine(baseLine), m_provider(0) {}

        LineStatusMap () 
            : m_data("LineStatusMap"),
            m_baseLine(-1), m_provider(0) {}

        void Reset (int baseLine, int count)             
        { 
            m_baseLine = baseLine;
            m_data.clear();
            m_data.resize(count); 
        }

        LineStatus& operator [] (int inx)               { return m_data.at(inx - m_baseLine); }
        const LineStatus& operator [] (int inx) const   { return m_data.at(inx - m_baseLine); }
        int GetBaseLine () const                        { return m_baseLine; }
        int GetLineCount () const                       { return m_data.size(); }

        void SetProviderAction (const void* provider, SequenceId providerAction)          { m_provider = provider; m_providerAction = providerAction; }
        bool IsSameProviderAction (const void* provider, SequenceId providerAction) const { return (m_provider == provider) && (m_providerAction == providerAction); }

    private:
        int m_baseLine;
        Common::nvector<LineStatus> m_data;
        const void* m_provider;
        SequenceId m_providerAction;

        // prohibited
        LineStatusMap (const LineStatusMap&);
        void operator = (const LineStatusMap&);
    };

    ///////////////////////////////////////////////////////////////////////////////
    // Square inline methods //
    inline
    bool intersect_not_null (const Square& r1, const Square& r2)
    {
        return (r1.line_in_rect(r2.start.line) || r2.line_in_rect(r1.start.line))
           && (r1.column_in_rect(r2.start.column) || r2.column_in_rect(r1.start.column));
    }

    inline
    bool Square::operator != (const Square& other) const
    {
        return (start != other.start || end != other.end);
    }


    // Position inline methods //
    inline
    bool Position::operator != (Position other) const
    {
        return column != other.column || line != other.line;
    }

    inline
    bool Position::operator == (Position other) const
    {
        return column == other.column && line == other.line;
    }

    inline
    bool Position::operator < (Position other) const
    {
        return (line < other.line) || (line == other.line && column < other.column);
    }

    // String inline methods //
    template <class T>
    inline
    StorageString<T>::StorageString ()
    {
        memset(&tag, 0, sizeof(tag));
    }

    template <class T>
    inline
    StorageString<T>::StorageString (const StorageString& other)
    : OEString<T> (other)
    {
        *this = other;
    }

    template <class T>
    inline
    void StorageString<T>::operator = (const StorageString& other)
    {
        OEString<T>::assign(other);
        tag = other.tag;
    }

    // MultilineQuotesScanner inline methods //
    inline
    void MultilineQuotesScanner::Invalidate (int from_line)
    {
        _ASSERTE(from_line >= 0);
        if (from_line < m_nQuotesValidLimit)
            m_nQuotesValidLimit = max(0, from_line);
    }

    inline
    const DelimitersMap& MultilineQuotesScanner::GetDelimitersMap () const
    {
        return m_delimiters;
    }

    // Searcher inline methods //
    inline
    bool Searcher::IsTextEmpty () const
    {
        return m_strText.empty();
    }

    inline
    const wchar_t* Searcher::GetText () const
    {
        return m_strText.c_str();
    }
}

#endif//__OEHelpers_h__
