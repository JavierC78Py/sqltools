/* 
    Copyright (C) 2003 Aleksey Kochetov

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

#ifndef __OEPlsSqlSupport_h__
#define __OEPlsSqlSupport_h__

#include "OpenEditor/OELanguageSupport.h"
#include "OpenEditor/OECommonLanguageSupport.h"
#include "OpenEditor/OEPlsSqlParser.h"
#include "OpenEditor/OEPlsSqlSyntax.h"
#include "OpenEditor/OEStorage.h"
#include "OpenEditor/OEParseQueue.h"

namespace OpenEditor
{
    // A flat snapshot of one line range, captured on the UI thread and then owned outright
    // by the task that parses it, so the worker never looks at the Storage it came from.
    class SnapshotTextSource : public OutlineTextSource
    {
    public:
        SnapshotTextSource () : m_lineCount(0), m_firstLine(0), m_totalLineCount(0) {}

        SnapshotTextSource (SnapshotTextSource&&) = default;
        SnapshotTextSource& operator= (SnapshotTextSource&&) = default;
        SnapshotTextSource (const SnapshotTextSource&) = delete;
        SnapshotTextSource& operator= (const SnapshotTextSource&) = delete;

        void Capture (const Storage& storage, int firstLine, int count);

        int GetLineCount () const override                 { return m_totalLineCount; }
        void GetLineW (int line, Common::OEStringW& s) const override;

    private:
        std::vector<size_t>  m_offsets;      // m_lineCount + 1 entries, into m_buffer
        std::vector<wchar_t> m_buffer;       // one flat run, no per-line allocation
        int m_lineCount;                     // lines captured
        int m_firstLine;                     // absolute line m_offsets[0] belongs to
        int m_totalLineCount;                // document line count at capture time
    };
    class PlSqlSupport : public CommonLanguageSupport
    {
    public:
        PlSqlSupport (Storage*);
        ~PlSqlSupport ();

        enum OutlineQuality { oqClean, oqTruncated, oqSyntaxError };

        virtual bool FindMatch (int line, int offset, Match&, bool quickExactMatch = false);

        bool GetFormatSeed (int line, int offset, SectionHit& hit) const;
        virtual void GetLineStatus (LineStatusMap&, int, int) const;
        virtual bool HasExtendedSupport () const { return true; }

        virtual LanguageSupportKind GetKind () const override { return lskPlSql; }

        virtual void OnSettingsChanged () override;

        bool IsDone () const { return m_done; }
        SequenceId GetParsedActionId () const { return m_storageActionId; }
        int GetInstanceId () const { return m_instanceId; }

        int GetParseGeneration () const { return m_parseGeneration; }

        virtual bool   IsParseDone () const override             { return m_done; }
        virtual bool   GetParseProgress (int& linesDone, int& linesTotal) const override;
        virtual HANDLE GetParseCompletionEvent () const override;
        const OpenEditor::FoldCandidates& GetLastFolds () const { EnsureOutline(); return m_committedFolds; }
        OutlineQuality GetLastOutlineQuality () const { EnsureOutline(); return m_committedOutlineQuality; }

        const OpenEditor::OutlineItems& GetLastPaneOutline () const { EnsureOutline(); return m_freshPane; }
        OutlineQuality GetPaneOutlineQuality () const { EnsureOutline(); return m_freshPaneQuality; }

        int GetParseCutLine () const { EnsureOutline(); return m_OutlineEnabled ? m_parseCutLine : -1; }

        int  GetProcessedLineCount () const { return m_currLine; }

        bool HasUnparsedTail () const
        {
            return m_done && m_limitTruncated;
        }

        virtual bool OnIdle ();

    protected:
        struct ParseBatch
        {
            bool              done           = false;
            int               nextLine       = 0;
            bool              limitTruncated = false;
            PlSqlParseProduct product;
        };

        // One parse generation. Its identity fields are const, its analyzer and parser
        // belong to the worker, and its completion event is what WaitForParseQuiescent
        // blocks on. Shared by the support and by every task of that generation.
        struct ParseContext : private boost::noncopyable
        {
            ParseContext (int totalLineLimit_, int tokenLimit_, int batchLimit_,
                          int instanceId_, SequenceId storageActionId_)
                : totalLineLimit(totalLineLimit_),
                  tokenLimit(tokenLimit_),
                  batchLimit(batchLimit_),
                  instanceId(instanceId_),
                  storageActionId(storageActionId_),
                  parser(analyzer),
                  completion(FALSE, TRUE /*manual reset*/)
            {}

            const int        totalLineLimit;
            const int        tokenLimit;
            const int        batchLimit;

            const int        instanceId;
            const SequenceId storageActionId;

            SnapshotTextSource spare;        // recycled capture buffer, UI thread only
            PlSqlAnalyzer      analyzer;     // worker thread only
            PlSqlParserEx      parser;       // worker thread only

            CEvent completion;

            PlSqlParseProduct retired;       // the previous generation's product, freed on the worker
        };

        typedef std::shared_ptr<ParseContext> ParseContextPtr;

        // Carries one batch to the worker and back. It owns the text it parses and the
        // batch it produces, so neither side reaches into state the other can still be
        // changing, and a task that is dropped takes its own data with it.
        struct PlSqlParseTask : public ParseTask
        {
            enum Kind { kindBatch, kindRetire };

            PlSqlParseTask (PlSqlSupport* support, const ParseContextPtr& ctx,
                            int firstLine, int count, Kind kind)
                : ParseTask(support, kind == kindRetire ? (HANDLE)NULL : (HANDLE)ctx->completion),
                  m_support(support),
                  m_context(ctx),
                  m_instanceId(support->m_instanceId),
                  m_firstLine(firstLine),
                  m_count(count),
                  m_kind(kind)
            {}

            virtual void Run () override;
            virtual void PublishInForeground () override;

            PlSqlSupport*      m_support;      // identity; PublishInForeground is its only reader
            ParseContextPtr    m_context;      // keeps the generation alive for both sides
            SnapshotTextSource m_text;         // input, moved in by submit()
            ParseBatch         m_batch;        // output, moved out by PublishInForeground()
            int                m_instanceId;   // captured at submit, so identity needs no dereference
            int                m_firstLine;    // absolute buffer line this batch starts at
            int                m_count;        // lines captured; unused by kindRetire
            Kind               m_kind;
        };

        typedef std::shared_ptr<PlSqlParseTask> PlSqlParseTaskPtr;

        bool processText ();

        static ParseBatch buildBatch (ParseContext& ctx, const OutlineTextSource& text, int startLine);
        void              commitBatch (ParseBatch&& batch);

        void collectResults ();
        bool submit (int firstLine);
        void beginGeneration ();
        void retireContext (const ParseContextPtr&);

        void EnsureOutline () const;

        bool m_done;
        Sequence m_actionId;
        SequenceId m_storageActionId;
        int m_parseGeneration = 0;
        int m_currLine, m_batchLimit, m_totalLineLimit, m_tokenLimit;
        bool m_limitTruncated = false;
        bool m_OutlineEnabled;
        int m_instanceId;
        static int s_instanceCounter;
        mutable bool                     m_outlineStale;
        mutable OpenEditor::OutlineItems  m_freshPane;
        mutable OutlineQuality            m_freshPaneQuality = oqClean;
        mutable OpenEditor::FoldCandidates m_committedFolds;
        mutable OutlineQuality            m_committedOutlineQuality = oqClean;
        mutable int                      m_parseCutLine = -1;

        ParseContextPtr                  m_context;
        int                              m_batchesInFlight = 0;   // handed to the queue, not drained back yet
        std::shared_ptr<ParseOwnerToken> m_ownerToken;            // expired in ~PlSqlSupport, before anything else

        PlSqlParseProduct m_parseProduct;
    };

};

#endif//__OEPlsSqlSupport_h__
