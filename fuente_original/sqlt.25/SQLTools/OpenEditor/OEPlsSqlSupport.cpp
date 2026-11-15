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

#include "stdafx.h"
#include "OpenEditor/OEContext.h"
#include "OpenEditor/OEPlsSqlSupport.h"
#include "OpenEditor/OECommonLanguageSupport.h"

#include <utility>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace OpenEditor
{

class StorageTextSource : public OutlineTextSource
{
public:
    explicit StorageTextSource (const Storage& storage) : m_storage(storage) {}

    int GetLineCount () const override
    {
        return m_storage.GetLineCount();
    }

    void GetLineW (int line, Common::OEStringW& s) const override
    {
        m_storage.GetLineW(line, s);
    }

private:
    const Storage& m_storage;
};

void SnapshotTextSource::Capture (const Storage& storage, int firstLine, int count)
{
    m_firstLine       = firstLine;
    m_lineCount       = count > 0 ? count : 0;
    m_totalLineCount  = storage.GetLineCount();

    if (m_firstLine + m_lineCount > m_totalLineCount)
        m_lineCount = m_totalLineCount - m_firstLine;

    if (m_lineCount < 0)
        m_lineCount = 0;

    m_offsets.clear();     // clear(), not swap(): keep the capacity for the next batch
    m_buffer.clear();
    m_offsets.reserve(m_lineCount + 1);

    Common::OEStringW scratch;

    for (int i = 0; i < m_lineCount; i++)
    {
        m_offsets.push_back(m_buffer.size());

        storage.GetLineW(m_firstLine + i, scratch);

        m_buffer.insert(m_buffer.end(), scratch.data(), scratch.data() + scratch.length());
    }

    m_offsets.push_back(m_buffer.size());   // one-past-the-end sentinel
}

void SnapshotTextSource::GetLineW (int line, Common::OEStringW& s) const
{
    const int index = line - m_firstLine;

    if (index < 0 || index >= m_lineCount)
    {
        s.erase(0);
        return;
    }

    const size_t from = m_offsets[index];
    const size_t to   = m_offsets[index + 1];

    if (to == from)
    {
        s.erase(0);
        return;
    }

    s.assign_static(m_buffer.data() + from, to - from);
}

int PlSqlSupport::s_instanceCounter = 0;

PlSqlSupport::PlSqlSupport (Storage* pstorage)
    : CommonLanguageSupport(pstorage)
{
    m_instanceId = ++s_instanceCounter;
    m_batchLimit = 1500;
    m_outlineStale = false;
    m_ownerToken = std::make_shared<ParseOwnerToken>();

    auto& settings =  m_pStorage->GetSettings();
    m_OutlineEnabled = settings.GetPlSqlOutlineEnabled();
    m_totalLineLimit = settings.GetPlSqlFileMaxLines()  ? settings.GetPlSqlFileMaxLines()  : INT_MAX;
    m_tokenLimit     = settings.GetPlSqlFileMaxTokens() ? settings.GetPlSqlFileMaxTokens() : INT_MAX;

    beginGeneration();
}

PlSqlSupport::~PlSqlSupport ()
{
    m_ownerToken.reset();                    // expire first: the worker drops whatever we can no longer drain

    ParseQueue::Get().RemoveByOwner(this);   // both queues, while no idle tick of ours can refill them

    m_context.reset();
}

void PlSqlSupport::OnSettingsChanged ()
{
    _CHECK_AND_THROW_(m_pStorage, "PlSqlSupport has not been initialized!")

    const auto& settings = m_pStorage->GetSettings();

    m_OutlineEnabled = settings.GetPlSqlOutlineEnabled();

    m_outlineStale = true;

    const int totalLineLimit = settings.GetPlSqlFileMaxLines()  ? settings.GetPlSqlFileMaxLines()  : INT_MAX;
    const int tokenLimit     = settings.GetPlSqlFileMaxTokens() ? settings.GetPlSqlFileMaxTokens() : INT_MAX;

    if (totalLineLimit == m_totalLineLimit && tokenLimit == m_tokenLimit)
        return;                     // nothing that bounds the parse has moved

    m_totalLineLimit = totalLineLimit;
    m_tokenLimit     = tokenLimit;
    beginGeneration();
}

bool PlSqlSupport::FindMatch (int line, int offset, Match& match, bool quickExactMatch /*= false*/)
{
    _CHECK_AND_THROW_(m_pStorage, "PlSqlSupport has not been initialized!")

    if (!quickExactMatch)
    {
	    int index;
	    const SyntaxNode* node = 0;

	    if (m_parseProduct.FindToken(line, offset, node, index))
	    {
            match.found   = true;
            match.broken  = node->IsFailed();
            match.partial = false;
            node->GetToken(index, match.line[0], match.offset[0], match.length[0]);
		    node->GetNextToken(index, match.line[1], match.offset[1], match.length[1], match.backward);
            return true;
	    }
    }

    return CommonLanguageSupport(m_pStorage).FindMatch(line, offset, match, true/*quickExactMatch*/);
}

bool PlSqlSupport::GetFormatSeed (int line, int offset, SectionHit& hit) const
{
    _CHECK_AND_THROW_(m_pStorage, "PlSqlSupport has not been initialized!")

    if (!m_done)
        return false;

    if (HasUnparsedTail() && line >= GetProcessedLineCount())
        return false;

    if (!m_parseProduct.FindSection(line, offset, hit))
        return false;

    return hit.section != SyntaxNode::shHeader;
}

HANDLE PlSqlSupport::GetParseCompletionEvent () const
{
    return (m_batchesInFlight && m_context) ? (HANDLE)m_context->completion : NULL;
}

bool PlSqlSupport::GetParseProgress (int& linesDone, int& linesTotal) const
{
    if (m_done)
        return false;

    const int fileLines = m_pStorage->GetLineCount();

    linesTotal = fileLines < m_totalLineLimit ? fileLines : m_totalLineLimit;

    if (linesTotal < m_batchLimit)
        return false;

    linesDone = m_currLine < linesTotal ? m_currLine : linesTotal;

    return linesTotal > 0;
}

void PlSqlSupport::GetLineStatus (LineStatusMap& statusMap, int baseLine, int count) const
{
    if (statusMap.IsSameProviderAction(this, m_actionId)
    && baseLine == statusMap.GetBaseLine()
    && count == statusMap.GetLineCount()
    )
        return;

    statusMap.Reset(baseLine, count);
    m_parseProduct.GetLineStatus(statusMap);

    if (HasUnparsedTail())
    {
        const int mapBase = statusMap.GetBaseLine();
        const int mapEnd  = mapBase + statusMap.GetLineCount();   // exclusive

        int line = m_currLine > mapBase ? m_currLine : mapBase;
        for (; line < mapEnd; line++)
            statusMap[line].unparsed = true;

        if (m_currLine >= mapBase && m_currLine < mapEnd)
            statusMap[m_currLine].unparsedBoundary = true;
    }

    if (m_done)
        statusMap.SetProviderAction(this, m_actionId);
}

bool PlSqlSupport::OnIdle ()
{
    return processText();
}

bool PlSqlSupport::processText ()
{
    collectResults();

    if (!m_batchesInFlight && m_pStorage->GetActionId() != m_storageActionId)
        beginGeneration();

    if (!m_done && !m_batchesInFlight)
    {
        if (!m_context)
            beginGeneration();

        submit(m_currLine);
    }

    return  !m_done;
}

void PlSqlSupport::collectResults ()
{
    ParseQueue& q = ParseQueue::Get();

    if (WaitForSingleObject(q.GetResultEvent(), 0) != WAIT_OBJECT_0)
        return;                              // nothing completed anywhere, so take no lock

    while (ParseTaskPtr base = q.PopResult(this))
    {
        PlSqlParseTask* task = dynamic_cast<PlSqlParseTask*>(base.get());

        if (!task || task->m_kind == PlSqlParseTask::kindRetire)
            continue;                        // a retire shell carries no counter and no product

        --m_batchesInFlight;

        if (m_context)
            m_context->spare = std::move(task->m_text);   // recycle the buffer, never its contents

        if (task->m_context != m_context)
            continue;

        if (task->m_context->storageActionId != m_pStorage->GetActionId())
            continue;

        if (task->m_firstLine != m_currLine)
            continue;

        task->PublishInForeground();
    }
}

void PlSqlSupport::beginGeneration ()
{
    m_done            = false;
    m_currLine        = 0;
    m_limitTruncated  = false;
    m_storageActionId = m_pStorage->GetActionId();

    ++m_parseGeneration;

    ParseContextPtr previous = m_context;

    m_context.reset(new ParseContext(m_totalLineLimit, m_tokenLimit, m_batchLimit,
                                     m_instanceId, m_storageActionId));

    if (previous)
        retireContext(previous);
}

void PlSqlSupport::retireContext (const ParseContextPtr& ctx)
{
    PlSqlParseTaskPtr task(new PlSqlParseTask(this, ctx, 0, 0, PlSqlParseTask::kindRetire));

    task->SetOwnerToken(m_ownerToken);

    ParseQueue::Get().Push(task);
}

bool PlSqlSupport::submit (int firstLine)
{
    if (m_batchesInFlight)
        return false;

    ParseContext& ctx = *m_context;

    const int remaining = m_pStorage->GetLineCount() - firstLine;
    const int count     = remaining < ctx.batchLimit ? remaining : ctx.batchLimit;

    ctx.spare.Capture(*m_pStorage, firstLine, count > 0 ? count : 0);

    PlSqlParseTaskPtr task(new PlSqlParseTask(this, m_context, firstLine, count,
                                              PlSqlParseTask::kindBatch));

    task->SetOwnerToken(m_ownerToken);
    task->m_text = std::move(ctx.spare);     // ownership passes to the task with the snapshot

    ctx.completion.ResetEvent();             // before Push: the worker may set it at once

    if (!ParseQueue::Get().Push(task))
    {
        m_done           = true;
        m_limitTruncated = m_currLine < m_pStorage->GetLineCount();
        return false;
    }

    ++m_batchesInFlight;

    task.reset();                            // the queue is the only holder from here on

    return true;
}

PlSqlSupport::ParseBatch PlSqlSupport::buildBatch (ParseContext& ctx, const OutlineTextSource& text, int startLine)
{
    ParseBatch batch;
    batch.nextLine = startLine;

    const int nlines = text.GetLineCount();
    int line = startLine;

    for (int i = 0; i < ctx.batchLimit && line < nlines; i++, line++)
    {
        if (line >= ctx.totalLineLimit
        || ctx.analyzer.GetTokenCount() >= ctx.tokenLimit) break;
        Common::OEStringW wbuff;
        text.GetLineW(line, wbuff);
        ctx.parser.PutLine(line, wbuff.data(), wbuff.length());
    }

    batch.nextLine = line;

    if (line >= nlines || line >= ctx.totalLineLimit
    || ctx.analyzer.GetTokenCount() >= ctx.tokenLimit)
    {
        batch.done = true;

        batch.limitTruncated = (line < nlines);

        ctx.parser.PutEOF(line);
        batch.product = ctx.analyzer.TakeProduct();
    }

    return batch;
}

void PlSqlSupport::commitBatch (ParseBatch&& batch)
{
    m_currLine = batch.nextLine;

    m_actionId++;

    if (batch.done)
    {
        m_done           = true;
        m_limitTruncated = batch.limitTruncated;

        ParseContextPtr finished = m_context;

        if (finished)
        {
            finished->retired = std::move(m_parseProduct);
            m_context.reset();
        }

        m_parseProduct = std::move(batch.product);

        m_outlineStale = m_OutlineEnabled;

        if (finished)
            retireContext(finished);
    }

    m_pStorage->Notify_ChangedLinesStatus(0, m_currLine);

}

void PlSqlSupport::PlSqlParseTask::Run ()
{
    if (m_kind == kindRetire)
    {
        m_context->retired = PlSqlParseProduct();   // the heavy release, on the worker

        m_context.reset();                          // and so is the generation's own teardown

        return;
    }

    m_batch = PlSqlSupport::buildBatch(*m_context, m_text, m_firstLine);
}

void PlSqlSupport::PlSqlParseTask::PublishInForeground ()
{
    if (m_context->instanceId != m_instanceId)      // generation identity, without touching m_support
        return;

    PlSqlSupport* support = m_support;              // live: collectResults popped this by owner

    if (IsFailed())
    {
        support->m_done           = true;
        support->m_limitTruncated = true;

        ParseContextPtr failed = m_context;

        if (support->m_context == failed)
            support->m_context.reset();

        support->retireContext(failed);

        TRACE("PlSqlParseTask: batch at line %d FAILED, generation abandoned\n", m_firstLine);
        return;
    }

    support->commitBatch(std::move(m_batch));
}

void PlSqlSupport::EnsureOutline () const
{
    if (!m_outlineStale)
        return;

    if (!m_OutlineEnabled)
    {
        m_outlineStale = false;
        return;
    }

    if (!m_done)
        return;

    m_outlineStale = false;

    const auto& settings = m_pStorage->GetSettings();
    const int maxTopItems = settings.GetPlSqlOutlineMaxTopItems();
    const OutlineSettings outlineSettings {
        settings.GetPlSqlOutlineSelectEnabled(),
        settings.GetPlSqlOutlineSelectOnly(),
        maxTopItems ? maxTopItems : INT_MAX
    };

    FoldCandidates folds;
    OutlineItems   pane;
    StorageTextSource text(*m_pStorage);
    m_parseProduct.GetOutlineProducts(text, outlineSettings, folds, &pane);

    bool truncated      = m_limitTruncated;
    bool hasSyntaxError = m_parseProduct.HasAnyFailedNode();
    OutlineQuality quality = truncated       ? oqTruncated
                             : hasSyntaxError ? oqSyntaxError
                             : oqClean;

    m_parseCutLine = -1;

    if (hasSyntaxError)
        m_parseCutLine = m_parseProduct.GetFailureLine();

    if (truncated && m_currLine >= 0
                 && (m_parseCutLine < 0 || m_currLine < m_parseCutLine))
        m_parseCutLine = m_currLine;

    m_freshPane        = std::move(pane);
    m_freshPaneQuality = quality;

    m_committedFolds          = std::move(folds);
    m_committedOutlineQuality = quality;
}

};
