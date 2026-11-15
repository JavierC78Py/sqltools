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

// In-editor code folding: fold regions derived from the PL/SQL outline
// snapshot. See md/CodeFolding.md - that document is the authority for the
// decisions referenced below (D9, D10, D11, C1).
//
// Stage 1: the model only. Nothing outside this file reads it yet.

#include "stdafx.h"
#include "OpenEditor/OEFolding.h"

#include <algorithm>
#include <cstdio>

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace OpenEditor
{
    ///////////////////////////////////////////////////////////////////////////
    // FoldRegion

    FoldRegion::FoldRegion ()
        : startLine(-1), endLine(-1), depth(0)
    {
    }

    FoldRegion::FoldRegion (int start, int end, int depth_)
        : startLine(start), endLine(end), depth(depth_)
    {
    }

    ///////////////////////////////////////////////////////////////////////////
    // FoldModel

    FoldModel::FoldModel ()
        : m_pResolver(NULL), m_bSnapshotClean(true), m_parseCutLine(-1)
    {
    }

    ///////////////////////////////////////////////////////////////////////////
    // Buffer-bound clamp for an outline item's end line.
    //
    // An end line of GetLineCount() - one past the last real line - is a normal
    // product of the parser, not a corruption. PlSqlSupport::buildBatch stamps
    // the EOF token with its line cursor, which equals GetLineCount() once the
    // loop has run to completion, and a statement
    // still open when EOF arrives is completed BY that token (ePendingSql and
    // ePendingSqlPlusCmd, OEPlsSqlSyntaxImpl.cpp:1786,1792), so GetOwnExtent
    // reports the phantom line as the node's last. A file whose final statement
    // is unterminated - a trailing "show errors" with no ';' - is exactly that,
    // and the phantom line is also what makes a ONE-line trailing statement look
    // two lines tall and so survive FilterItems' D10 test.
    //
    // ILineIdResolver::GetLineId indexes Storage's line array and raises
    // std::out_of_range past the end. BuildAvailableFromFiltered runs inside
    // COEditorView::OnIdleUpdateCmdUI, where a throw repeats on every idle pass
    // and leaves the application unable to answer at all. Clamp before mapping;
    // an end that lands on its own anchor is then dropped by the caller's D10
    // test, which is the right answer for a one-line trailing statement, while a
    // genuinely multi-line unterminated one keeps folding over its real lines.
    //
    // ResyncIds has always applied this rule on the edit path. Sharing one helper
    // is what stops the build path and the edit path disagreeing about it again.
    static int ClampEndLine (int endLine, int nlines)
    {
        return (nlines > 0 && endLine >= nlines) ? nlines - 1 : endLine;
    }

    // A region whose lines the buffer cannot address: start outside it, or an end
    // that clamped down onto the anchor so nothing is left to hide. ResyncEntry
    // drops both cases; the build path has to as well, or a range the buffer cannot
    // map reaches the gutter and GetLineId throws from inside it.
    static bool Unaddressable (int start, int end, int nlines)
    {
        return start < 0 || start >= nlines || end <= start;
    }

    ///////////////////////////////////////////////////////////////////////////
    // Clamped end line for every item of a list already in SortItems order
    // (start asc, end desc), so out[i] is the end line region i will get.
    //
    // ClampEndLine is the clamp about the BUFFER: an end may legitimately sit one
    // past the last line. This adds the clamp about the PARSE. An item whose
    // rangeTrusted is false was force-closed rather than matched, so its end is
    // wherever the parser gave up - a line the construct does not own. Case 38
    // measured exactly that: `END IF;` cut, the IF force-closed onto `END p1;`, and
    // collapsing it hid a line belonging to its PARENT (FOLDING.good:1177-1183 shows
    // `fold 2 collapse` then `hidden 7 1`).
    //
    // So hide only what the parse confirmed. The test is deliberately NARROW: an
    // untrusted range is shortened only when it ends EXACTLY ON the cut line, and
    // then only to the line before it. That equality is the signature of a range the
    // parser terminated WITH the offending token rather than with the construct's own
    // terminator - it force-closes everything still open at the failure and stamps
    // each with that token's position, so the invented end and the cut are the same
    // line by construction. Case 38 measures the distinction: cutline=7 (1-based),
    // Procedure and If both end at 7:7 and are the force-closed pair, while
    // PackageBody ends at 9:1 - past the cut, at EOF - and its end is a line the
    // parser really did reach.
    //
    // Clamping every untrusted range to the cut, without the equality, was tried and
    // is also wrong, in the opposite direction. It shortens ranges whose ends are
    // accurate, and when such a range is the only one in the buffer the clamp runs it
    // onto its own anchor and the region is dropped outright: case
    // stream-cut-spanning-a-fold-takes-the-hidden-rows lost its single package-body
    // fold (regions 1 -> 0) because a syntax error on line 2 had nothing to do with
    // the body's real `END pkg;` on line 5. An untrusted flag says "something inside
    // me failed", not "my end is invented"; only ending on the failure line says that.
    //
    // The region still exists, still anchors, still collapses - it just stops short,
    // and it grows back on its own the moment the construct is really closed.
    //
    // That distinction is the whole of step 5's design and it was measured, not
    // assumed. DROPPING untrusted items in FilterItems instead was implemented first
    // and the suite refuted it: a failed node sits in the subtree of EVERY ancestor,
    // so one broken construct marks the whole enclosing chain untrusted even where
    // the ancestors' own ends are accurate. Dropping them removed the RANGES, and a
    // range that is not in the snapshot cannot be refreshed: D16 adoption keeps the
    // user's fold alive over it, but holding the end line it had BEFORE the error,
    // so folds that were correct and nowhere near the error end up hiding the wrong
    // lines. Case 33
    // (columnar-cut-outside-a-fold-leaves-it-collapsed) failed there too - a
    // columnar cut inside p2 destroyed the user's collapse of an IF whose own
    // `END IF;` was intact and read trusted=1 - and cases 31 and 38 lost every region
    // they had. Clamping keeps the range present, so adoption matches it exactly and
    // takes the corrected end from the parse.
    //
    // The pass runs BACKWARDS because the sort order puts every ancestor before its
    // descendants: by the time an item is reached, the clamped ends of everything
    // nested inside it are already known. That is not a refinement, it is required.
    // Clamping a parent on its own can pull its end ABOVE a child's, and two regions
    // that overlap without nesting are exactly what LineMapper cannot represent - it
    // builds one prefix sum over the hidden runs. So an end is floored at the deepest
    // clamped end inside it, which also means a parent can never be clamped shorter
    // than something the parse confirmed it contains.
    //
    // parseCutLine is 0-based, or -1 when the parse reached the end of the text
    // (PlSqlSupport::GetParseCutLine). With no cut there is nothing to clamp to and
    // every end passes through as ClampEndLine left it. Note the separate EOF blind
    // spot: an item force-closed at end-of-buffer with no failed node in its own
    // subtree reads rangeTrusted=TRUE, so neither the flag nor the cut identifies it
    // (md/CodeFolding_tests.md 7.2). The equality test leaves those ends alone too,
    // which is the same gap seen from this side.
    static void RegionEndLines (const FoldCandidates& sorted, int nlines, int parseCutLine,
                                std::vector<int>& out)
    {
        out.assign(sorted.size(), 0);

        for (size_t i = sorted.size(); i-- > 0; )
        {
            const FoldCandidate& item = sorted.at(i);

            int end = ClampEndLine(item.pos.end.line, nlines);

            // deepest clamped end inside this item; its own anchor is the floor. With
            // the equality test above a clamp can never actually reach below the
            // anchor - FilterItems' D10 already guaranteed end > start, so end - 1 is
            // at worst start - but the floor stays because it is what makes the
            // nesting rule below total rather than merely usual.
            int inner = item.pos.start.line;

            // descendants form a contiguous run after i in this sort order
            for (size_t j = i + 1;
                 j < sorted.size() && sorted.at(j).pos.start.line <= item.pos.end.line;
                 j++)
            {
                if (out.at(j) > inner)
                    inner = out.at(j);
            }

            if (!item.rangeTrusted
                && parseCutLine >= 0
                && item.pos.end.line == parseCutLine
                && parseCutLine - 1 < end)
            {
                end = parseCutLine - 1;
            }

            out.at(i) = end < inner ? inner : end;
        }
    }

    void FoldModel::FilterItems (const FoldCandidates& in, FoldCandidates& out)
    {
        out.clear();
        out.reserve(in.size());

        for (size_t i = 0; i < in.size(); i++)
        {
            const FoldCandidate& item = in.at(i);

            // D9 - a SELECT folds as ONE region. Its FROM/WHERE children would
            // only produce overlapping sub-folds inside it.
            if (item.type == oitFromClause || item.type == oitWhereClause)
                continue;

            // an unrecognized construct has no trustworthy range
            if (item.type == oitUnknown)
                continue;

            // NOTE: rangeTrusted is deliberately NOT tested here. An untrusted
            // range still becomes a region - it is CLAMPED instead, by
            // RegionEndLine. Dropping it here was tried and measured, and it
            // regresses the anti-thrash guarantee; see that helper.

            // D10 - a single-line item has nothing to hide. The anchor is
            // always visible, so start == end would hide nothing at all.
            if (item.pos.end.line <= item.pos.start.line)
                continue;

            out.push_back(item);
        }
    }

    void FoldModel::SortItems (FoldCandidates& items)
    {
        std::stable_sort(items.begin(), items.end(),
            [] (const FoldCandidate& a, const FoldCandidate& b)
            {
                if (a.pos.start.line != b.pos.start.line)
                    return a.pos.start.line < b.pos.start.line;
                return a.pos.end.line > b.pos.end.line;
            });
    }

    void FoldModel::BuildFromOutline (const FoldCandidates& items)
    {
        // A from-scratch build is a reset, not an update: the user's folds go
        // with whatever document they belonged to. The re-parse path is
        // RebuildFromSnapshot, which adopts them instead.
        m_collapsed.clear();

        FoldCandidates filtered;
        FilterItems(items, filtered);
        BuildAvailableFromFiltered(filtered);
    }

    void FoldModel::Clear ()
    {
        m_available.clear();
        // D16's reset scope. Folding switched off, the language changed, the
        // document went away: in every one of those the ranges the user folded
        // stop describing anything, so the state goes too rather than being
        // carried into a document that cannot honour it.
        m_collapsed.clear();
        m_bSnapshotClean = true;
        m_parseCutLine   = -1;
    }

    void FoldModel::BuildAvailableFromFiltered (const FoldCandidates& filtered)
    {
        // Wholesale, and only m_available. Nothing here is allowed to look at
        // m_collapsed: the snapshot says what is foldable NOW and the user's
        // folds are not part of that question.
        m_available.clear();

        FoldCandidates sorted (filtered);
        SortItems(sorted);

        // See ClampEndLine. Zero when no resolver is attached: the model is then
        // purely line-based, maps no ids and so cannot raise - both the clamp and
        // the bounds test below are skipped in that case.
        const int nlines = m_pResolver ? m_pResolver->GetLineCount() : 0;

        // endLines of the accepted regions that still enclose the candidate
        std::vector<int> openEnds;
        openEnds.reserve(sorted.size());

        // Clamped ends for the whole sorted list, computed together because an
        // item's clamp is bounded from below by the clamped ends nested inside it.
        // See RegionEndLines.
        std::vector<int> endLines;
        RegionEndLines(sorted, nlines, m_parseCutLine, endLines);

        for (size_t i = 0; i < sorted.size(); i++)
        {
            const FoldCandidate& item = sorted.at(i);

            // C1 - these are buffer lines, copied verbatim. InxToPos is a
            // column-only conversion and must never be applied to them.
            const int start = item.pos.start.line;
            const int end   = endLines.at(i);

            while (!openEnds.empty() && start > openEnds.back())
                openEnds.pop_back();

            // An end that clamped onto its own anchor, or a start the buffer
            // cannot address. Dropped rather than mapped, because GetLineId
            // would raise out_of_range from inside the view's idle handler.
            if (m_pResolver && Unaddressable(start, end, nlines))
            {
                TRACE("FoldModel: dropped unaddressable fold region [%d..%d], buffer has %d line(s)\n",
                      start, end, nlines);
                continue;
            }

            // starts inside an accepted region but runs past its end: an
            // overlap rather than a nesting. Folding cannot render that, so
            // drop the offender instead of letting last-one-wins corrupt the
            // line mapping.
            if (!openEnds.empty() && end > openEnds.back())
            {
                TRACE("FoldModel: dropped overlapping fold region [%d..%d]\n", start, end);
                continue;
            }

            // exact duplicate of the region accepted immediately before
            if (!m_available.empty()
                && m_available.back().startLine == start
                && m_available.back().endLine == end)
            {
                TRACE("FoldModel: dropped duplicate fold region [%d..%d]\n", start, end);
                continue;
            }

            FoldRegion region (start, end, item.depth);

            if (m_pResolver)
            {
                region.anchorId = m_pResolver->GetLineId(start);
                region.endId    = m_pResolver->GetLineId(end);
            }

            m_available.push_back(region);
            openEnds.push_back(end);
        }
    }

    bool FoldModel::SameIdentity (const FoldRegion& a, const FoldRegion& b)
    {
        if (a.anchorId.Valid() || b.anchorId.Valid())
            return a.anchorId.Valid() && b.anchorId.Valid()
                && (a.anchorId == b.anchorId) != 0
                && (a.endId    == b.endId)    != 0;

        return a.startLine == b.startLine && a.endLine == b.endLine;
    }

    bool FoldModel::SameAnchor (const FoldRegion& a, const FoldRegion& b)
    {
        if (a.anchorId.Valid() || b.anchorId.Valid())
            return a.anchorId.Valid() && b.anchorId.Valid()
                && (a.anchorId == b.anchorId) != 0;

        return a.startLine == b.startLine;
    }

    int FoldModel::FindAvailableMatch (const FoldRegion& e, bool bExact,
                                       const std::vector<char>& taken) const
    {
        for (size_t i = 0; i < m_available.size(); i++)
        {
            if (taken.at(i))
                continue;

            if (bExact ? SameIdentity(e, m_available.at(i))
                       : SameAnchor(e, m_available.at(i)))
                return (int)i;
        }

        return -1;
    }

    void FoldModel::MatchCollapsed (std::vector<int>& match,
                                    std::vector<char>& taken) const
    {
        match.assign(m_collapsed.size(), -1);
        taken.assign(m_available.size(), 0);

        // Exact identities first, then anchors alone, in two passes. One pass in
        // m_collapsed order would let an entry that only matches by anchor claim
        // a range a LATER entry matches exactly, so whether a fold survived would
        // depend on the order the user happened to fold in. Two passes make the
        // pairing a function of the two collections and nothing else.
        for (int pass = 0; pass < 2; pass++)
        {
            for (size_t c = 0; c < m_collapsed.size(); c++)
            {
                if (match.at(c) >= 0)
                    continue;

                const int i = FindAvailableMatch(m_collapsed.at(c), pass == 0, taken);

                if (i >= 0)
                {
                    match.at(c) = i;
                    taken.at((size_t)i) = 1;
                }
            }
        }
    }

    void FoldModel::SortRegions (Regions& regions)
    {
        std::stable_sort(regions.begin(), regions.end(),
            [](const FoldRegion& a, const FoldRegion& b)
            {
                if (a.startLine != b.startLine)
                    return a.startLine < b.startLine;

                return a.endLine > b.endLine;
            });
    }

    void FoldModel::RemoveCollapsedAt (const FoldRegion& e)
    {
        // Identity OR anchor, so expanding at a line leaves nothing folded there
        // even when an orphan and a fresh range share the anchor and disagree
        // about the end.
        for (size_t i = m_collapsed.size(); i-- > 0; )
        {
            if (SameIdentity(e, m_collapsed.at(i)) || SameAnchor(e, m_collapsed.at(i)))
                m_collapsed.erase(m_collapsed.begin() + (ptrdiff_t)i);
        }
    }

    // D16's per-item reconciliation, and the whole reason the two collections
    // exist. The retired MergeFrom asked "is the new snapshot structurally
    // identical to the old one" and answered for every fold at once, so the first
    // deleted procedure re-expanded the file. This asks the question once per
    // folded range, which is the only granularity the answer is meaningful at:
    //
    //   exact (anchorId, endId) match  -> keep. The range the user folded is the
    //                                     range the parse just confirmed.
    //   anchor match, end moved        -> keep. Tier 1's useful half, applied to
    //                                     the one entry it describes instead of
    //                                     being gated on every other entry.
    //   no match at all                -> the construct is gone, or the parse has
    //                                     not reached it yet. Survive on its own
    //                                     iff ResyncEntry still accepts it, i.e.
    //                                     its anchor resolves and something is
    //                                     left to hide.
    //
    // That last row is the one that needs defending, because keeping a region the
    // outline no longer lists looks like the bug ToDoNotes 102 reported. It is
    // not: 102 was a region whose STRUCTURE had been deleted holding its place
    // forever, and the fix for that was to rebuild from the snapshot - which is
    // exactly what m_available now does, wholesale, on every parse. What survives
    // here is the user's fold, on lines the buffer can still address, and it
    // survives only until its own identity stops resolving: a deleted construct's
    // anchor line stops resolving as soon as the storage compacts its ids, and
    // ResyncEntry drops it then.
    //
    // Geometry comes from m_available in both matching rows, never from the old
    // entry, so a fold cannot keep describing a construct that changed shape.
    void FoldModel::AdoptCollapsed ()
    {
        if (m_collapsed.empty())
            return;

        std::vector<int>  match;
        std::vector<char> taken;
        MatchCollapsed(match, taken);

        Regions adopted;
        adopted.reserve(m_collapsed.size());

        for (size_t c = 0; c < m_collapsed.size(); c++)
        {
            const int i = match.at(c);

            if (i >= 0)
            {
                // The AVAILABLE geometry verbatim, so a construct that grew or
                // shrank keeps its fold but cannot go on describing the shape it
                // used to have.
                adopted.push_back(m_available.at((size_t)i));
                continue;
            }

            FoldRegion orphan = m_collapsed.at(c);

            if (ResyncEntry(orphan))
            {
                adopted.push_back(orphan);
            }
            else
            {
                TRACE("FoldModel: dropped collapsed region [%d..%d], its range no longer resolves\n",
                      orphan.startLine, orphan.endLine);
            }
        }

        m_collapsed.swap(adopted);

        // A matched entry took its geometry from m_available and an orphan had
        // its lines rewritten by ResyncEntry, so neither arrives in the order the
        // collection is required to keep. See SortCollapsed.
        SortCollapsed();
    }

    void FoldModel::SortCollapsed ()
    {
        SortRegions(m_collapsed);
    }

    // The joined view, and the last thing standing of the union D16 materialized.
    // Every distinct range across both layers:
    //
    //   - a collapsed entry that MATCHES an available one appears once, in that
    //     available entry's place, marked folded. Appearing twice would hide the
    //     same lines twice and LineMapper's run merge would count them twice;
    //     carrying the collapsed entry's geometry instead would let user state
    //     outrank the parse about where a construct ends.
    //   - an UNMATCHED collapsed entry - an orphan - appears on its own. It is
    //     still the user's fold and still hiding its lines, and a range that
    //     vanished from here would be invisible AND unopenable.
    //
    // Computed on demand rather than stored, and that is the point of the
    // refactor: a stored copy held a second answer to a question m_collapsed
    // already answers, and every mutator had to end in a full re-derivation to
    // stop the two disagreeing. The readers left are the oracle and
    // DebugSelfTest, neither of which is on a paint or edit path.
    //
    // MatchCollapsed is kept rather than replaced by a range-equality dedup.
    // Adoption makes a matched entry a verbatim copy, so ranges would usually
    // agree - but an orphan whose lines happen to coincide with a range it did
    // NOT match by identity would then collapse into it, and the union never did
    // that. Identity is also the D16 contract; line equality is only its
    // resolver-less spelling.
    void FoldModel::MergeRanges (Regions& out, std::vector<char>& folded) const
    {
        std::vector<int>  match;
        std::vector<char> taken;
        MatchCollapsed(match, taken);

        // available index -> does a collapsed entry stand for it?
        std::vector<char> owner (m_available.size(), 0);

        for (size_t c = 0; c < match.size(); c++)
            if (match.at(c) >= 0)
                owner.at((size_t)match.at(c)) = 1;

        out.clear();
        folded.clear();
        out.reserve(m_available.size() + m_collapsed.size());

        for (size_t a = 0; a < m_available.size(); a++)
        {
            out.push_back(m_available.at(a));
            folded.push_back(owner.at(a));
        }

        for (size_t c = 0; c < m_collapsed.size(); c++)
        {
            if (match.at(c) >= 0)
                continue;

            out.push_back(m_collapsed.at(c));
            folded.push_back(1);
        }

        // Each half above is in order but the concatenation is not: an orphan
        // sorts by line like everything else. The union got its order from
        // SortRegions over exactly this sequence, so the same stable comparator
        // over the same sequence is what keeps the enumeration - and the oracle
        // blessed against it - unchanged. `folded` has to travel with its region,
        // which is why the sort goes through an index permutation.
        std::vector<size_t> order (out.size());

        for (size_t i = 0; i < order.size(); i++)
            order.at(i) = i;

        std::stable_sort(order.begin(), order.end(),
            [&out] (size_t a, size_t b)
            {
                if (out.at(a).startLine != out.at(b).startLine)
                    return out.at(a).startLine < out.at(b).startLine;

                return out.at(a).endLine > out.at(b).endLine;
            });

        Regions           sortedRegions;
        std::vector<char> sortedFolded;
        sortedRegions.reserve(order.size());
        sortedFolded.reserve(order.size());

        for (size_t i = 0; i < order.size(); i++)
        {
            sortedRegions.push_back(out.at(order.at(i)));
            sortedFolded.push_back(folded.at(order.at(i)));
        }

        out.swap(sortedRegions);
        folded.swap(sortedFolded);
    }


    bool FoldModel::RebuildFromSnapshot (const FoldCandidates& items, bool bSnapshotClean,
                                         int parseCutLine)
    {
        // Recorded, never gated on, so IsSnapshotClean() - and the `modelclean` oracle
        // that reads it - keeps meaning `quality == oqClean` rather than "the regions
        // are current" (ToDoNotes 107 step 2). Every snapshot is actionable now.
        m_bSnapshotClean = bSnapshotClean;

        // The parse cut of the snapshot handed in, or -1 when the parse ran to the end
        // of the text. RegionEndLines uses it to clamp an untrusted range whose end
        // lands exactly on the cut - the construct the parser force-closed there rather
        // than matched. That per-item clamp is what makes a truncated parse safe to
        // FOLD instead of refuse: the prefix below the cut is real and folds normally,
        // and only the one range spanning the cut has its invented end pulled back
        // (ToDoNotes 107 step 6).
        m_parseCutLine = parseCutLine;

        // Read before the rebuild, because the change report means "was there
        // anything to preserve": a model that already had regions reports a change
        // whenever it is rebuilt, and a model that had none reports whether it now
        // has any. That is the pre-D16 contract OEDocument's repaint decision is
        // written against, so it is kept verbatim rather than tightened into a real
        // comparison - fewer repaints is a separate question from this one.
        // Empty() is the union test it used to be, spelled over the two layers.
        const bool bHadRegions = !Empty();

        // Two steps, in this order, and the order is the design: the snapshot
        // replaces the derived collection outright, then the user's state is
        // adopted into it per item. Nothing downstream can tell the difference
        // between "the outline was rebuilt" and "the outline was rebuilt and the
        // folds kept" - and since the union is gone there is no third step
        // materializing a copy for consumers to read.
        FoldCandidates filtered;
        FilterItems(items, filtered);
        BuildAvailableFromFiltered(filtered);
        AdoptCollapsed();

        return bHadRegions ? true : !Empty();
    }

    const FoldRegion* FoldModel::FindAnchorRange (int line) const
    {
        // m_collapsed first, and it wins a tie. Two ranges can share an anchor - a
        // construct nested on the line its parent starts on - and the folded one is
        // the answer that matters: its state is what a caller is about to act on,
        // and an orphan exists ONLY in this layer, so consulting m_available first
        // would hide the very fold the split exists to keep.
        //
        // Within a layer the first match wins. Both layers are sorted endLine
        // descending per anchor, so "first" is the outermost range on that line -
        // the same one FindAnchorAt named when it scanned the union.
        //
        // An orphan and an available range sharing an anchor but DISAGREEING about
        // the end cannot reach here: SameAnchor matches on the anchor id, and two
        // ranges on one buffer line have one anchor id, so adoption would already
        // have paired them. The only way to get here with both layers holding line
        // `line` is both holding the SAME range - in which case the collapsed entry
        // is a verbatim copy and the choice is not observable.
        for (size_t i = 0; i < m_collapsed.size(); i++)
            if (m_collapsed.at(i).startLine == line)
                return &m_collapsed.at(i);

        for (size_t i = 0; i < m_available.size(); i++)
            if (m_available.at(i).startLine == line)
                return &m_available.at(i);

        return NULL;
    }

    bool FoldModel::IsCollapsedAt (int line) const
    {
        // "Is the range anchored at `line` folded", which is exactly the question
        // the retired `Get(FindAnchorAt(line)).collapsed` answered - including its
        // behaviour on a line INSIDE a folded body, which is not an anchor and so
        // is not a collapsed anchor either. That is the second meaning a
        // flag-on-every-region representation invited, and reporting the model's
        // own answer is what stops a caller inventing it.
        //
        // Equivalent to FindAnchorRange(line) being an m_collapsed entry, because
        // that layer is consulted first: found here means found there, and not
        // found here means FindAnchorRange either returns an available range or
        // nothing, both of which are "not folded".
        for (size_t i = 0; i < m_collapsed.size(); i++)
            if (m_collapsed.at(i).startLine == line)
                return true;

        return false;
    }

    bool FoldModel::IsFolded (const FoldRegion& r) const
    {
        // Membership by identity. Adoption makes a matched entry a verbatim copy
        // of its available range, so identity holds for exactly the ranges the
        // union used to stamp `collapsed` on. An orphan is not in m_available, so
        // the question is never asked of one from there.
        for (size_t i = 0; i < m_collapsed.size(); i++)
            if (SameIdentity(r, m_collapsed.at(i)))
                return true;

        return false;
    }

    int FoldModel::FindEnclosingCollapsed (int line) const
    {
        // m_collapsed directly. Every entry in it is folded by definition, so the
        // flag test the union scan needed is not a question here - and this is the
        // reader that makes keeping the layer sorted load-bearing rather than
        // tidy, since the run merge below it is a single pass.
        //
        // Sorted startLine asc / endLine desc, so among the ranges hiding `line`
        // the LAST match is the innermost one - the fold the user means when they
        // jump here.
        int found = -1;

        for (size_t i = 0; i < m_collapsed.size(); i++)
            if (m_collapsed.at(i).Hides(line))
                found = (int)i;

        return found;
    }

    int FoldModel::EnclosingCollapsedAnchor (int line) const
    {
        const int i = FindEnclosingCollapsed(line);

        return i < 0 ? -1 : m_collapsed.at((size_t)i).startLine;
    }

    const FoldRegion* FoldModel::FindEnclosingAvailable (int line) const
    {
        // m_available is sorted outermost-first, so the LAST match is the innermost.
        const FoldRegion* found = NULL;

        for (size_t i = 0; i < m_available.size(); i++)
        {
            const FoldRegion& r = m_available.at(i);

            if (r.startLine > line)
                break;
            if (r.Spans(line))
                found = &r;
        }

        return found;
    }

    bool FoldModel::AnyCollapsed () const
    {
        // m_collapsed IS the state, so its emptiness is the answer. Scanning a
        // derived collection for a flag would work too but would re-derive from a
        // copy what is already known at the source.
        return !m_collapsed.empty();
    }

    void FoldModel::JoinedView (Regions& ranges, std::vector<char>& folded) const
    {
        // One derivation for a caller that wants the whole enumeration, instead
        // of one per element: Count, Get and IsCollapsed below each run this same
        // merge to answer a single question, so walking the set through them
        // pays for it three times over per region. See the header for why there
        // is no cache behind this.
        MergeRanges(ranges, folded);
    }

    int FoldModel::Count () const
    {
        Regions           merged;
        std::vector<char> folded;
        MergeRanges(merged, folded);

        return (int)merged.size();
    }

    FoldRegion FoldModel::Get (int i) const
    {
        Regions           merged;
        std::vector<char> folded;
        MergeRanges(merged, folded);

        _ASSERTE(i >= 0 && i < (int)merged.size());

        if (i < 0 || i >= (int)merged.size())
            return FoldRegion();

        return merged.at((size_t)i);
    }

    void FoldModel::ExpandAll ()
    {
        // Expanding is clearing the user's state, not walking a collection
        // flipping flags - which is what the single-list model had to do. With no
        // union to rematerialize, clearing IS the whole operation.
        m_collapsed.clear();
    }

    void FoldModel::CollapseAll ()
    {
        // A copy of the snapshot, which is already in the required order.
        m_collapsed = m_available;
    }

    void FoldModel::CollapseToLevel (int level)
    {
        // Region level is depth + 1, so "fold level >= N" is "fold depth >= N-1".
        // A subsequence of m_available taken in order, so the sort invariant carries
        // over for free. level < 1 clamps to CollapseAll rather than folding nothing.
        if (level < 1)
            level = 1;

        m_collapsed.clear();

        for (size_t i = 0; i < m_available.size(); i++)
        {
            if (m_available.at(i).depth >= level - 1)
                m_collapsed.push_back(m_available.at(i));
        }
    }

    bool FoldModel::ExpandSubtree (int line)
    {
        // Geometric containment, not layer membership: an orphan hiding lines inside
        // the subtree must go too, or the "everything under here is visible" promise
        // the command makes would be a lie. The spanning region's own entry is inside
        // its own span, so it is erased by the same predicate.
        const FoldRegion* r = FindEnclosingAvailable(line);

        if (!r)
            return false;

        const size_t before = m_collapsed.size();
        size_t       kept   = 0;

        for (size_t i = 0; i < m_collapsed.size(); i++)
        {
            const FoldRegion& c = m_collapsed.at(i);

            if (c.startLine < r->startLine || c.endLine > r->endLine)
                m_collapsed.at(kept++) = c;
        }

        m_collapsed.resize(kept);

        return kept != before;
    }

    bool FoldModel::SetCollapsedAt (int line, bool collapsed)
    {
        const FoldRegion* pAnchor = FindAnchorRange(line);

        if (!pAnchor)
            return false;

        // A COPY, and not the reference FindAnchorRange returned: it may point
        // into m_collapsed, which RemoveCollapsedAt below erases from. This is the
        // same hazard the union version had, moved from "the collection is
        // rebuilt" to "the collection is edited".
        const FoldRegion entry = *pAnchor;

        // Removed on the fold side too, so folding the same anchor twice cannot
        // leave two entries where only ExpandAll would ever find the second.
        RemoveCollapsedAt(entry);

        if (collapsed)
        {
            // Appended at the end, so the sort invariant has to be restored
            // explicitly - the entry belongs wherever its lines put it, not
            // wherever the user happened to click last.
            m_collapsed.push_back(entry);
            SortCollapsed();
        }

        return collapsed;
    }

    bool FoldModel::ToggleAt (int line)
    {
        if (!IsAnchorAt(line))
            return false;

        // Both reads happen before SetCollapsedAt mutates anything, and asking the
        // model for the current state is what replaced dereferencing a flag off
        // the union entry the old version had just looked up.
        return SetCollapsedAt(line, !IsCollapsedAt(line));
    }

    void FoldModel::ShiftInserted (Regions& regions, int line, int count)
    {
        for (size_t i = 0; i < regions.size(); i++)
        {
            FoldRegion& region = regions.at(i);

            // an insertion at the anchor line pushes the anchor down; an
            // insertion strictly inside the region only pushes its end.
            if (region.startLine >= line)
                region.startLine += count;

            if (region.endLine >= line)
                region.endLine += count;
        }
    }

    void FoldModel::ShiftDeleted (Regions& regions, int line, int count)
    {
        const int last = line + count - 1;

        for (size_t i = 0; i < regions.size(); )
        {
            FoldRegion& region = regions.at(i);
            bool drop = false;

            if (region.startLine >= line && region.startLine <= last)
            {
                // the anchor line itself is gone: the construct it folded no
                // longer exists, so the region goes with it (D11).
                drop = true;
            }
            else
            {
                if (region.startLine > last)
                    region.startLine -= count;

                if (region.endLine > last)
                {
                    region.endLine -= count;
                }
                else if (region.endLine >= line)
                {
                    // the last line of the region is gone: clamp to the line
                    // above the hole and let ResyncIds() re-adopt the id.
                    region.endLine = line - 1;
                    region.endId   = LineId();
                }

                // nothing left to hide
                drop = region.endLine <= region.startLine;
            }

            if (drop)
                regions.erase(regions.begin() + (ptrdiff_t)i);
            else
                i++;
        }
    }

    void FoldModel::OnLinesInserted (int line, int count)
    {
        if (count <= 0 || line < 0)
            return;

        // BOTH collections. Shifting only the derived one would leave the user's
        // folds keyed to pre-edit lines, and since D16 matches by id first and by
        // line only when there are no ids, an unshifted m_collapsed is exactly the
        // state that would silently stop matching on the next parse.
        ShiftInserted(m_available, line, count);
        ShiftInserted(m_collapsed, line, count);

        // LineIds travel with their lines, so existing ids stay valid; call
        // ResyncIds() to pick up ids for regions that never had one.
        //
        // No re-sort. A shift is MONOTONIC: it adds the same `count` to every line
        // at or after `line` and leaves the rest alone, so two entries in order
        // stay in order - including two sharing an anchor, whose ends either move
        // together or not at all. ResyncIds is the mutator that needs the sort,
        // because it REWRITES lines from ids rather than moving them.
    }

    void FoldModel::OnLinesDeleted (int line, int count)
    {
        if (count <= 0 || line < 0)
            return;

        // Both collections, for the reason OnLinesInserted gives, and monotonic in
        // the same way: every surviving line either keeps its value or loses the
        // same `count`. Dropping entries cannot reorder the ones left.
        ShiftDeleted(m_available, line, count);
        ShiftDeleted(m_collapsed, line, count);
    }

    bool FoldModel::ResyncEntry (FoldRegion& region) const
    {
        if (!m_pResolver)
        {
            // Purely line-based model: there is no id to re-resolve, so the only
            // thing left to ask is whether the range still hides anything. That
            // keeps AdoptCollapsed's orphan branch honest in DebugSelfTest, which
            // builds models with no resolver attached.
            return region.endLine > region.startLine;
        }

        const int nlines = m_pResolver->GetLineCount();
        int line = -1;

        // --- anchor line ---
        if (region.anchorId.Valid())
        {
            // The anchor's line is gone from the buffer. This is where a fold
            // whose construct was deleted finally ends: the storage stops
            // resolving the id and the entry goes with it.
            if (!m_pResolver->FindById(region.anchorId, line))
                return false;

            region.startLine = line;
        }
        else if (region.startLine >= 0 && region.startLine < nlines)
        {
            // built before a resolver was attached: adopt the current line
            region.anchorId = m_pResolver->GetLineId(region.startLine);
        }
        else
        {
            return false;
        }

        // --- end line ---
        region.endLine = ClampEndLine(region.endLine, nlines);

        if (region.endId.Valid() && m_pResolver->FindById(region.endId, line))
            region.endLine = line;

        // re-adopt the id, which also repairs one invalidated by OnLinesDeleted()
        if (region.endLine < 0 || region.endLine >= nlines)
            return false;

        region.endId = m_pResolver->GetLineId(region.endLine);

        // nothing left to hide
        return region.endLine > region.startLine;
    }

    void FoldModel::ResyncCollection (Regions& regions) const
    {
        for (size_t i = 0; i < regions.size(); )
        {
            if (ResyncEntry(regions.at(i)))
                i++;
            else
                regions.erase(regions.begin() + (ptrdiff_t)i);
        }
    }

    void FoldModel::ResyncIds ()
    {
        if (!m_pResolver)
            return;

        // Both collections, for the same reason OnLinesInserted shifts both: the
        // user's folds are keyed by id, and an id that is not re-resolved after an
        // edit points at whatever line now happens to carry it.
        ResyncCollection(m_available);
        ResyncCollection(m_collapsed);

        // Sort BOTH, and this is the one place on the edit path that has to.
        // ResyncEntry rewrites an entry's lines FROM ITS IDS, which is a jump and
        // not a shift: an entry can land anywhere in the buffer, so the incoming
        // order stops implying the sorted one. Each layer has a reader that
        // depends on that order holding - LineMapper::Rebuild's single-pass run
        // merge over m_collapsed, and the connector-band loop's early `break` over
        // m_available. The retired union re-sorted as a side effect of
        // RebuildUnion and so hid the requirement; with the union gone the
        // invariant is the model's own to keep.
        SortRegions(m_available);
        SortCollapsed();
    }

    ///////////////////////////////////////////////////////////////////////////
    // LineMapper

    LineMapper::LineMapper ()
        : m_pModel(NULL), m_lineCount(0), m_hiddenTotal(0), m_bValid(false)
    {
    }

    void LineMapper::SetModel (const FoldModel* pModel)
    {
        if (m_pModel != pModel)
        {
            m_pModel = pModel;
            Invalidate();
        }
    }

    void LineMapper::SetLineCount (int lineCount)
    {
        if (lineCount < 0)
            lineCount = 0;

        if (m_lineCount != lineCount)
        {
            m_lineCount = lineCount;
            Invalidate();
        }
    }

    void LineMapper::Invalidate ()
    {
        m_bValid = false;
    }

    void LineMapper::EnsureCache () const
    {
        if (!m_bValid)
            Rebuild();
    }

    void LineMapper::Rebuild () const
    {
        m_runs.clear();
        m_hiddenTotal = 0;
        m_bValid      = true;

        if (!m_pModel)
            return;

        // The collapsed layer itself. Every entry in it is folded by definition,
        // so the `region.collapsed` test the union scan needed is not a question
        // here, and there is nothing left to skip - this walks exactly the ranges
        // that hide lines and nothing else. Reading the layer directly is also
        // what makes its sort invariant load-bearing rather than tidy.
        const FoldModel::Regions& regions = m_pModel->GetCollapsed();

        for (size_t i = 0; i < regions.size(); i++)
        {
            const FoldRegion& region = regions.at(i);

            // the anchor stays visible, so the hidden run starts on the next
            // line and ends on the region's inclusive end
            const int first = region.startLine + 1;
            const int last  = region.endLine;

            if (last < first)
                continue;

            if (!m_runs.empty() && first <= m_runs.back().last + 1)
            {
                // nested inside the run being built, or immediately after it:
                // extend rather than start a new one, so a parent collapsed
                // over an already collapsed child stays a single run. Regions
                // are sorted startLine asc / endLine desc, so `first` never
                // goes backwards and one pass is enough.
                if (last > m_runs.back().last)
                    m_runs.back().last = last;

                continue;
            }

            HiddenRun run;
            run.first        = first;
            run.last         = last;
            run.hiddenBefore = 0;
            m_runs.push_back(run);
        }

        // prefix sum, filled in once the merging above has settled
        int hidden = 0;

        for (size_t i = 0; i < m_runs.size(); i++)
        {
            const HiddenRun& run = m_runs.at(i);

            m_runs.at(i).hiddenBefore = hidden;
            hidden += run.last - run.first + 1;
        }

        m_hiddenTotal = hidden;
    }

    int LineMapper::FindRun (int line) const
    {
        EnsureCache();

        // last run whose first hidden line is at or below `line`
        size_t lo = 0, hi = m_runs.size();

        while (lo < hi)
        {
            const size_t mid = (lo + hi) / 2;

            if (m_runs.at(mid).first <= line)
                lo = mid + 1;
            else
                hi = mid;
        }

        if (lo == 0)
            return -1;

        const int i = (int)lo - 1;

        return line <= m_runs.at((size_t)i).last ? i : -1;
    }

    int LineMapper::HiddenBefore (int line) const
    {
        EnsureCache();

        if (line <= 0 || m_runs.empty())
            return 0;

        // last run whose first hidden line is strictly below `line`
        size_t lo = 0, hi = m_runs.size();

        while (lo < hi)
        {
            const size_t mid = (lo + hi) / 2;

            if (m_runs.at(mid).first < line)
                lo = mid + 1;
            else
                hi = mid;
        }

        if (lo == 0)
            return 0;

        const HiddenRun& run = m_runs.at(lo - 1);

        // the whole run lies below `line`
        if (line > run.last)
            return run.hiddenBefore + (run.last - run.first + 1);

        // `line` sits inside the run, so only the part below it counts
        return run.hiddenBefore + (line - run.first);
    }

    int LineMapper::BufferToVisual (int line) const
    {
        if (line <= 0)
            return 0;

        const int i = FindRun(line);

        if (i >= 0)
        {
            // A hidden line maps onto the row of the anchor above its run, the
            // one carrying the [...] marker. `line - HiddenBefore(line)` would
            // give the same row for every hidden line in the run, but that row
            // is the one BELOW the run and does not exist when the run reaches
            // the end of the buffer - which is exactly what a package body
            // collapsed over its own `end;` does. Naming the anchor row keeps
            // the result inside [0, GetVisualLineCount()).
            const HiddenRun& run = m_runs.at((size_t)i);

            return run.first - 1 - run.hiddenBefore;
        }

        return line - HiddenBefore(line);
    }

    int LineMapper::VisualToBuffer (int row) const
    {
        EnsureCache();

        if (row <= 0)
            return 0;

        // A run's anchor row is where its [...] marker is drawn, that is
        // first - 1 - hiddenBefore. Anchor rows ascend with the runs, so the
        // runs lying wholly above `row` come from one binary search: run i is
        // consumed when its anchor row is strictly above `row`. Consuming adds
        // the run's length to the offset, which is exactly the prefix sum the
        // forward direction subtracts.
        size_t lo = 0, hi = m_runs.size();

        while (lo < hi)
        {
            const size_t     mid = (lo + hi) / 2;
            const HiddenRun& run = m_runs.at(mid);

            if (run.first - 1 - run.hiddenBefore < row)
                lo = mid + 1;
            else
                hi = mid;
        }

        int hidden = 0;

        if (lo > 0)
        {
            const HiddenRun& run = m_runs.at(lo - 1);

            hidden = run.hiddenBefore + (run.last - run.first + 1);
        }

        int line = row + hidden;

        // Clamp: a row past the last visible one - the view asking about the row
        // below the end while scrolling, or a buffer that shrank under it -
        // must still resolve to something the view can show. The last buffer
        // line may itself be hidden (a body collapsed over its own `end;`), so
        // settle on the last visible line rather than merely the last line.
        if (m_lineCount > 0 && line >= m_lineCount)
            line = PrevVisibleLine(m_lineCount - 1);

        return line;
    }

    int LineMapper::GetVisualLineCount () const
    {
        EnsureCache();

        const int n = m_lineCount - m_hiddenTotal;

        return n > 0 ? n : 0;
    }

    bool LineMapper::IsLineHidden (int line) const
    {
        return FindRun(line) >= 0;
    }

    int LineMapper::NextVisibleLine (int line) const
    {
        EnsureCache();

        if (line < 0)
            line = 0;

        const int i = FindRun(line);

        // a hidden line jumps to the first line below its run
        if (i >= 0)
            line = m_runs.at((size_t)i).last + 1;

        return line < m_lineCount ? line : m_lineCount;
    }

    int LineMapper::PrevVisibleLine (int line) const
    {
        EnsureCache();

        if (m_lineCount > 0 && line >= m_lineCount)
            line = m_lineCount - 1;

        const int i = FindRun(line);

        // a hidden line jumps to the anchor above its run
        if (i >= 0)
            line = m_runs.at((size_t)i).first - 1;

        return line;
    }

    int LineMapper::GetEnclosingCollapsedRegion (int line) const
    {
        return m_pModel ? m_pModel->FindEnclosingCollapsed(line) : -1;
    }

#ifdef _DEBUG

    // ---- self-test reporting -------------------------------------------------
    //
    // A plain _ASSERTE raises a modal dialog. Under /AutomatedTesting nobody is
    // there to dismiss it, so InitInstance blocks and every suite queued behind
    // it times out - a real failure shows up only as a hung test run. So each
    // failed check is appended to %TEMP%\SQLTools_FoldSelfTest.log and checking
    // carries on, which lets one run report every broken scenario instead of
    // just the first. The dialog is still raised for an interactive run, where
    // somebody is watching.
    //
    // Same reasoning as the assert redirection in OEPlsSqlFormatterTree.cpp,
    // which keeps a COM caller from deadlocking on the UI thread.

    static bool s_bSelfTestUnattended = false;
    static int  s_selfTestFailures    = 0;

    // bTruncate starts a fresh log; otherwise failures accumulate in it.
    // Returns NULL if the log cannot be written - never fatal, the checks
    // themselves still run and still report through TRACE/OutputDebugString.
    static FILE* OpenSelfTestLog (bool bTruncate)
    {
        wchar_t dir[MAX_PATH] = L"";

        const DWORD len = ::GetTempPathW(_countof(dir), dir);

        // fall back to the working directory if %TEMP% is unavailable
        std::wstring path = (len > 0 && len < _countof(dir)) ? std::wstring(dir, len)
                                                             : std::wstring(L".\\");

        path += L"SQLTools_FoldSelfTest.log";

        FILE* f = NULL;

        if (_wfopen_s(&f, path.c_str(), bTruncate ? L"wt" : L"at") != 0)
            return NULL;

        return f;
    }

    static void ReportFailure (const char* what, const char* expr, const char* file, int line)
    {
        s_selfTestFailures++;

        FILE* f = OpenSelfTestLog(false);

        if (f)
        {
            fprintf(f, "FAIL %s(%d): %s   [%s]\n", file, line, expr, what);
            fclose(f);
        }

        wchar_t dbg[1024];

        _snwprintf_s(dbg, _countof(dbg), _TRUNCATE,
            L"%hs(%d): folding self-test FAILED: %hs   [%hs]\n", file, line, expr, what);
        ::OutputDebugStringW(dbg);

        if (!s_bSelfTestUnattended)
            _ASSERTE(!"OEFolding self-test failed - see %TEMP%\\SQLTools_FoldSelfTest.log");
    }

    // `what` names the scenario, so a failure says which shape broke rather
    // than only which expression did.
    #define FOLD_CHECK(what, expr) \
        do { if (!(expr)) ReportFailure((what), #expr, __FILE__, __LINE__); } while (0)

    namespace
    {
        // A fold candidate for the self-test. Geometry only, because that is all
        // FoldCandidate carries now: the label this helper used to take and stamp had
        // no reader, since D16 made folding match on range identity rather than on
        // text. Every call site kept its L"..." argument until the product split, so
        // dropping it here drops it everywhere at once.
        FoldCandidate MakeFoldItem (int startLine, int endLine, int depth)
        {
            FoldCandidate item;

            item.pos.clear();
            item.pos.start.column = 0;
            item.pos.start.line   = startLine;
            item.pos.end.column   = 0;
            item.pos.end.line     = endLine;
            item.depth            = depth;
            item.type             = oitBlock;

            return item;
        }

        // MakeFoldItem with rangeTrusted forced false - the shape the outline walk
        // hands over for a construct it force-closed at the parse cut, so its end
        // line is an artifact rather than the user's text (ToDoNotes 107 step 3).
        FoldCandidate MakeUntrustedFoldItem (int startLine, int endLine, int depth)
        {
            FoldCandidate item = MakeFoldItem(startLine, endLine, depth);

            item.rangeTrusted = false;

            return item;
        }

        // The endLine of the range anchored at `line`, or -1 when nothing is
        // anchored there. DebugSelfTest asks that question a dozen times, and
        // FindAnchorRange returns a POINTER so a caller can tell "no anchor" from
        // "an anchor holding a default range" - correct, but noisy to spell out at
        // every one of those dozen sites. -1 is the honest failure value: no real
        // endLine is negative, so a missing anchor fails the comparison loudly
        // instead of being mistaken for a range that happens to end at 0.
        int AnchorEndLine (const FoldModel& model, int line)
        {
            const FoldRegion* pRegion = model.FindAnchorRange(line);

            return pRegion ? pRegion->endLine : -1;
        }

        // Builds the model, collapses the listed anchor lines, then checks the
        // invariants every later stage relies on. `what` names the scenario so
        // a failed assert says which shape broke.
        void CheckMapper (const char* what, const FoldCandidates& items, int lineCount,
                          const std::vector<int>& collapseAnchors)
        {
            // so the verdict below reports THIS scenario, not the running total
            const int failuresBefore = s_selfTestFailures;

            FoldModel model;
            model.BuildFromOutline(items);

            for (size_t i = 0; i < collapseAnchors.size(); i++)
                FOLD_CHECK(what, model.SetCollapsedAt(collapseAnchors.at(i), true)); // scenario named a missing anchor

            LineMapper mapper;
            mapper.SetModel(&model);
            mapper.SetLineCount(lineCount);

            // --- the visible count is the buffer minus the hidden lines ---
            int hidden = 0;

            for (int l = 0; l < lineCount; l++)
                if (mapper.IsLineHidden(l))
                    hidden++;

            FOLD_CHECK(what, mapper.GetVisualLineCount() == lineCount - hidden);
            FOLD_CHECK(what, mapper.GetLineCount() == lineCount);

            // --- forward: monotonic, inside the row range, round-trips -------
            int prevRow = -1;

            for (int l = 0; l < lineCount; l++)
            {
                const int  row     = mapper.BufferToVisual(l);
                const bool bHidden = mapper.IsLineHidden(l);

                FOLD_CHECK(what, row >= 0 && row >= prevRow);
                FOLD_CHECK(what, row < mapper.GetVisualLineCount()); // a row the view can actually show
                prevRow = row;

                if (bHidden)
                {
                    // a hidden line lands on its anchor's row, and that row
                    // resolves back to the anchor - never to another hidden
                    // line, which is where a caret would get stuck
                    const int anchor = mapper.VisualToBuffer(row);

                    FOLD_CHECK(what, !mapper.IsLineHidden(anchor));
                    FOLD_CHECK(what, anchor < l);
                    FOLD_CHECK(what, mapper.BufferToVisual(anchor) == row);
                    FOLD_CHECK(what, mapper.PrevVisibleLine(l) == anchor);
                }
                else
                {
                    FOLD_CHECK(what, mapper.VisualToBuffer(row) == l);
                }
            }

            // --- inverse: every row resolves to a visible line, and back ----
            const int rows = mapper.GetVisualLineCount();

            for (int r = 0; r < rows; r++)
            {
                const int l = mapper.VisualToBuffer(r);

                FOLD_CHECK(what, l >= 0 && l < lineCount);
                FOLD_CHECK(what, !mapper.IsLineHidden(l));
                FOLD_CHECK(what, mapper.BufferToVisual(l) == r);
            }

            // --- navigation stays on visible lines and never goes the wrong way
            for (int l = 0; l < lineCount; l++)
            {
                const int  next    = mapper.NextVisibleLine(l);
                const int  prev    = mapper.PrevVisibleLine(l);
                const bool bHidden = mapper.IsLineHidden(l);

                FOLD_CHECK(what, next >= l && next <= lineCount);
                FOLD_CHECK(what, next == lineCount || !mapper.IsLineHidden(next));
                FOLD_CHECK(what, bHidden ? next > l : next == l);

                FOLD_CHECK(what, prev >= -1 && prev <= l);
                FOLD_CHECK(what, prev < 0 || !mapper.IsLineHidden(prev));
                FOLD_CHECK(what, bHidden ? prev < l : prev == l);
            }

            // --- one caret step is exactly one ROW ---------------------------
            // The property COEditorView::NextNavigableLine relies on when it
            // answers EditContext::GoToUp / GoToDown (md/CodeFolding.md stage 5,
            // D4). GoToUp asks for PrevVisibleLine(l - 1) and GoToDown for
            // NextVisibleLine(l + 1), and both have to land on the row right
            // beside l's - so one arrow press moves one visible row whether or
            // not the line next to l is hidden. The block above proves those two
            // calls stay on visible lines and never go backwards; this is the
            // stronger claim that they do not SKIP a row either, which is what
            // would show up as an arrow press doing nothing or jumping two lines.
            for (int l = 0; l < lineCount; l++)
            {
                if (mapper.IsLineHidden(l))
                    continue;                       // the caret cannot rest here

                const int row = mapper.BufferToVisual(l);

                // Down. From the last visible line there is nothing below and
                // NextVisibleLine says so with lineCount - the one-past-EOF line
                // GoToDown accepts only when "cursor beyond end of file" is on.
                const int down = mapper.NextVisibleLine(l + 1);

                if (down < lineCount)
                    FOLD_CHECK(what, mapper.BufferToVisual(down) == row + 1);
                else
                    FOLD_CHECK(what, down == lineCount && row == rows - 1);

                // Up. -1 from the first visible line, which GoToUp rejects. A
                // hidden run always starts at line 1 or later, so this is the
                // only line that can produce it.
                const int up = mapper.PrevVisibleLine(l - 1);

                if (up >= 0)
                    FOLD_CHECK(what, mapper.BufferToVisual(up) == row - 1);
                else
                    FOLD_CHECK(what, row == 0);
            }


            // --- rows past the end still resolve to something showable ------
            // the view asks about these while scrolling; the last buffer line
            // may be hidden, so "clamp to lineCount - 1" is not good enough
            if (rows > 0)
            {
                const int past = mapper.VisualToBuffer(rows + 5);

                FOLD_CHECK(what, past >= 0 && past < lineCount);
                FOLD_CHECK(what, !mapper.IsLineHidden(past));
            }

            TRACE("LineMapper self-test %hs: %hs\n", what,
                  s_selfTestFailures > failuresBefore ? "FAILED (see log)" : "ok");
        }
    }

    void LineMapper::DebugSelfTest (bool bUnattended)
    {
        s_bSelfTestUnattended = bUnattended;
        s_selfTestFailures    = 0;

        // start from an empty log, so a FAIL left behind by an earlier run is
        // not mistaken for one from this run
        FILE* fLog = OpenSelfTestLog(true);

        if (fLog)
            fclose(fLog);

        const std::vector<int> none;

        // 1. empty model - the mapping must be the identity
        {
            const FoldCandidates items;
            CheckMapper("empty model", items, 20, none);
        }

        // 2. a single collapsed range anchored on line 0
        {
            FoldCandidates items;
            items.push_back(MakeFoldItem(0, 9, 0));

            std::vector<int> collapse;
            collapse.push_back(0);

            CheckMapper("single range at line 0, collapsed", items, 20, collapse);
            CheckMapper("single range at line 0, expanded",  items, 20, none);
        }

        // 3. nesting, in every combination that matters
        {
            FoldCandidates items;
            items.push_back(MakeFoldItem(0,  19, 0));
            items.push_back(MakeFoldItem(2,  8,  1));
            items.push_back(MakeFoldItem(4,  6,  2));
            items.push_back(MakeFoldItem(11, 18, 1));

            std::vector<int> outer;
            outer.push_back(0);
            CheckMapper("nested, outer only", items, 20, outer);

            std::vector<int> inner;
            inner.push_back(4);
            CheckMapper("nested, innermost only", items, 20, inner);

            std::vector<int> parentOverChild;
            parentOverChild.push_back(0);
            parentOverChild.push_back(2);
            CheckMapper("nested, parent over a collapsed child", items, 20, parentOverChild);

            std::vector<int> all;
            all.push_back(0);
            all.push_back(2);
            all.push_back(4);
            all.push_back(11);
            CheckMapper("nested, everything collapsed", items, 20, all);
        }

        // 4. adjacent siblings - the anchors between them stay visible, so the
        //    hidden runs must NOT merge into one
        {
            FoldCandidates items;
            items.push_back(MakeFoldItem(0,  4,  0));
            items.push_back(MakeFoldItem(5,  9,  0));
            items.push_back(MakeFoldItem(10, 14, 0));

            std::vector<int> all;
            all.push_back(0);
            all.push_back(5);
            all.push_back(10);

            CheckMapper("adjacent siblings, all collapsed", items, 15, all);
        }

        // 5. a region whose endLine is the last line of the buffer
        {
            FoldCandidates items;
            items.push_back(MakeFoldItem(0,  19, 0));
            items.push_back(MakeFoldItem(15, 19, 1));

            std::vector<int> tail;
            tail.push_back(15);
            CheckMapper("range touching EOF", items, 20, tail);
        }

        // 6. D10 - a single-line item must not become a region at all
        {
            FoldCandidates items;
            items.push_back(MakeFoldItem(3, 3, 0));
            items.push_back(MakeFoldItem(5, 9, 0));

            FoldModel model;
            model.BuildFromOutline(items);

            FOLD_CHECK("D10 single-line item", model.Count() == 1);

            if (model.Count() == 1)
                FOLD_CHECK("D10 single-line item", model.Get(0).startLine == 5);
        }

        // 7. D9 - FROM/WHERE fold with their SELECT, not inside it
        {
            FoldCandidates items;

            FoldCandidate select = MakeFoldItem(0, 9, 0);
            select.type = oitSelect;
            items.push_back(select);

            FoldCandidate from = MakeFoldItem(3, 5, 1);
            from.type = oitFromClause;
            items.push_back(from);

            FoldCandidate where = MakeFoldItem(6, 8, 1);
            where.type = oitWhereClause;
            items.push_back(where);

            FoldModel model;
            model.BuildFromOutline(items);

            FOLD_CHECK("D9 SELECT absorbs FROM/WHERE", model.Count() == 1);
            // The one region is the SELECT's, not the FROM's: anchored on line 0
            // and spanning the whole statement. This used to read a `type` field
            // off the region, which the two-layer model no longer carries - it was
            // copied from the outline item at build time and nothing but this
            // assertion ever read it back. The geometry pins the same fact.
            FOLD_CHECK("D9 SELECT absorbs FROM/WHERE",
                       model.Count() == 1 && model.Get(0).startLine == 0
                                          && model.Get(0).endLine == 9);
            FOLD_CHECK("D9 SELECT absorbs FROM/WHERE", model.ToggleAt(0));

            LineMapper mapper;
            mapper.SetModel(&model);
            mapper.SetLineCount(10);

            // nine hidden lines leave the anchor row as the only visible one,
            // and the hidden last line maps back onto it rather than onto a row
            // below the end of the buffer
            FOLD_CHECK("D9 SELECT absorbs FROM/WHERE", mapper.GetVisualLineCount() == 1);
            FOLD_CHECK("D9 SELECT absorbs FROM/WHERE", mapper.BufferToVisual(9) == 0);
            FOLD_CHECK("D9 SELECT absorbs FROM/WHERE", mapper.VisualToBuffer(0) == 0);
            FOLD_CHECK("D9 SELECT absorbs FROM/WHERE", mapper.GetEnclosingCollapsedRegion(5) == 0);
        }

        // 8. Stage 3 - a CLEAN snapshot is what actually builds the regions.
        //    These three scenarios are the runtime proof for the stage 3 wiring
        //    in COEDocument, which has no UI to exercise yet.
        {
            FoldCandidates full;
            full.push_back(MakeFoldItem(0,  19, 0));
            full.push_back(MakeFoldItem(2,   8, 1));
            full.push_back(MakeFoldItem(10, 18, 1));

            FoldModel model;

            // first parse: nothing to preserve, so the regions are simply built
            FOLD_CHECK("stage 3 clean snapshot", model.RebuildFromSnapshot(full, true, -1));
            FOLD_CHECK("stage 3 clean snapshot", model.IsSnapshotClean());
            FOLD_CHECK("stage 3 clean snapshot", model.Count() == 3);

            // a second clean parse of the SAME structure matches that region
            // exactly, so a region the user collapsed stays collapsed instead of
            // popping open on every keystroke
            FOLD_CHECK("stage 3 clean snapshot", model.SetCollapsedAt(2, true));

            model.RebuildFromSnapshot(full, true, -1);

            FOLD_CHECK("stage 3 clean snapshot", model.Count() == 3);
            FOLD_CHECK("stage 3 clean snapshot", model.IsAnchorAt(2));
            // What the old `Get(FindAnchorAt(2)).collapsed` asserted, asked of the
            // model instead - and with no guard around it, because there is no
            // index to keep in range any more.
            FOLD_CHECK("stage 3 clean snapshot", model.IsCollapsedAt(2));
        }

        // 9. Stage 3 - a TRUNCATED snapshot is ACTIONABLE and folded PER ITEM. This
        //    reverses step 2's wholesale refusal (ToDoNotes 107 step 6). Truncation
        //    does leave the range spanning the cut with an INVENTED end, but step 5's
        //    RegionEndLines clamp already handles that one range per item, so refusing
        //    the whole snapshot was over-broad: it froze the fully-parsed prefix too,
        //    which is the symptom where an edit inside a fold the parse really read
        //    never reached the gutter while the file sat over a line/token limit.
        //    Truncation now flows through the same rebuild path as the syntax error
        //    in 9b - items wholly below the cut fold, the item spanning it is clamped,
        //    and items in the unparsed tail are simply absent. `bSnapshotClean` is
        //    recorded, not gated on, so IsSnapshotClean() still reports false.
        {
            FoldCandidates full;
            full.push_back(MakeFoldItem(0,  19, 0));
            full.push_back(MakeFoldItem(2,   8, 1));
            full.push_back(MakeFoldItem(10, 18, 1));

            // A limit cut at line 10: the parse read 0..9 and force-closed pkg there,
            // so pkg SPANS the cut (untrusted, end exactly ON it) while a completed
            // below the cut (trusted). b lives entirely in the unparsed tail and is
            // absent from the snapshot - the parse never looked at it.
            FoldCandidates truncated;
            truncated.push_back(MakeUntrustedFoldItem(0, 10, 0));
            truncated.push_back(MakeFoldItem(2,  8, 1));

            FoldModel model;
            model.RebuildFromSnapshot(full, true, -1);
            model.SetCollapsedAt(2, true);

            FOLD_CHECK("stage 3 truncated snapshot", model.Count() == 3);

            // Actionable: the region set FOLLOWS the truncated parse instead of
            // freezing, and D16 is what makes that safe to do. b is below the cut, so
            // its range is gone; a is above it and unchanged, so a's fold is adopted
            // per item and survives. The retired all-or-nothing merge saw the unit
            // count change 3 -> 2 and re-expanded a as well - the honest D11 cost it
            // called it, and the cost D16 exists to remove.
            FOLD_CHECK("stage 3 truncated snapshot", model.RebuildFromSnapshot(truncated, false, 10));
            FOLD_CHECK("stage 3 truncated snapshot", model.Count() == 2);
            // b is gone: it was below the cut and the parse never reached it, so there
            // is nothing trustworthy left to fold there.
            FOLD_CHECK("stage 3 truncated snapshot", !model.IsAnchorAt(10));
            // pkg spanned the cut, so step 5 pulled its invented end 10 -> 9 (floored
            // at the clamped end of a nested inside it). a completed below the cut and
            // keeps the extent the parse really read.
            FOLD_CHECK("stage 3 truncated snapshot", model.IsAnchorAt(0));
            FOLD_CHECK("stage 3 truncated snapshot", AnchorEndLine(model, 0) == 9);

            FOLD_CHECK("stage 3 truncated snapshot", model.IsAnchorAt(2));
            FOLD_CHECK("stage 3 truncated snapshot", AnchorEndLine(model, 2) == 8);
            // ...and it is still folded, which is the D16 half of this scenario
            FOLD_CHECK("stage 3 truncated snapshot", model.IsCollapsedAt(2));
            // ...and the model still REPORTS the parse as unclean: IsSnapshotClean()
            // means quality==oqClean, and this parse was truncated.
            FOLD_CHECK("stage 3 truncated snapshot", !model.IsSnapshotClean());

            // The anti-thrash half, and why this is not just 9b again: a STABLE cut
            // whose prefix shape is unchanged matches per item on its anchor, so a
            // collapse the user set on a parsed region survives a re-truncation. Only
            // an end moved (a grew 8 -> 9), so truncation does not pop the parsed
            // prefix open on every keystroke - the guarantee step 2's refusal was
            // protecting, now kept by the per-item clamp instead of by refusing the
            // snapshot.
            model.SetCollapsedAt(2, true);

            FoldCandidates sameCut;
            sameCut.push_back(MakeUntrustedFoldItem(0, 10, 0));
            sameCut.push_back(MakeFoldItem(2,  9, 1));

            FOLD_CHECK("stage 3 truncated snapshot", model.RebuildFromSnapshot(sameCut, false, 10));
            FOLD_CHECK("stage 3 truncated snapshot", model.Count() == 2);
            FOLD_CHECK("stage 3 truncated snapshot", model.IsCollapsedAt(2));
        }

        // 9b. Stage 3 - a SYNTAX-ERROR snapshot is ACTIONABLE, not refused. The
        //     other half of the two-bool gate (ToDoNotes 107 step 2): the whole
        //     buffer WAS parsed and one construct failed, so the clean siblings are
        //     accurate and it is the retained regions that are the fiction - keeping
        //     them is what let a deleted procedure hold its fold region
        //     indefinitely while the pane listed it (ToDoNotes 102).
        {
            FoldCandidates full;
            full.push_back(MakeFoldItem(0,  19, 0));
            full.push_back(MakeFoldItem(2,   8, 1));
            full.push_back(MakeFoldItem(10, 18, 1));

            // what a syntax-error parse hands over: b lost its END, so b is gone
            // from the tree while pkg and a are still accurate
            FoldCandidates broken;
            broken.push_back(MakeFoldItem(0, 19, 0));
            broken.push_back(MakeFoldItem(2,  8, 1));

            FoldModel model;
            model.RebuildFromSnapshot(full, true, -1);
            model.SetCollapsedAt(2, true);

            FOLD_CHECK("stage 3 syntax-error snapshot", model.Count() == 3);

            // actionable, so the region set FOLLOWS the parse. The cut is where b's
            // missing END was noticed; it clamps nothing here because both surviving
            // items are trusted, which is the point - a cut bounds invented ends, not
            // read ones.
            FOLD_CHECK("stage 3 syntax-error snapshot", model.RebuildFromSnapshot(broken, false, 10));
            FOLD_CHECK("stage 3 syntax-error snapshot", model.Count() == 2);
            // ...and b's region is genuinely gone rather than kept stale
            FOLD_CHECK("stage 3 syntax-error snapshot", !model.IsAnchorAt(10));
            // ...while the model still REPORTS the snapshot as unclean, because
            // IsSnapshotClean() means quality==oqClean and this parse was not clean.
            // That separation is the entire reason quality arrives as two bools:
            // `modelclean` must keep describing the parse, not the gate's verdict.
            FOLD_CHECK("stage 3 syntax-error snapshot", !model.IsSnapshotClean());
            // D16, and the assertion this scenario used to invert. `a` was folded,
            // `b` vanished from the parse, and the two questions are now separate:
            // b's RANGE is gone (checked above) while a's FOLD is not. The retired
            // all-or-nothing merge had to re-expand a to stay consistent with the
            // snapshot it was comparing against, which is why deleting one
            // procedure popped the whole file open.
            FOLD_CHECK("stage 3 syntax-error snapshot", model.AnyCollapsed());
            FOLD_CHECK("stage 3 syntax-error snapshot", model.IsAnchorAt(2));

            // A POINTER into one of the two layers, and which layer it points
            // into is the whole question this scenario asks. IsAnchorAt above is
            // what makes the dereference safe; the guard below is belt and braces
            // rather than a case to skip, because the check above already reported
            // a missing anchor.
            const FoldRegion* pSurvivor = model.FindAnchorRange(2);

            FOLD_CHECK("stage 3 syntax-error snapshot", model.IsCollapsedAt(2));

            if (pSurvivor)
            {
                // and its geometry came from the NEW snapshot, not from the fold's
                // memory of the old one - user state decides collapsed, the parse
                // decides where the construct ends.
                FOLD_CHECK("stage 3 syntax-error snapshot", pSurvivor->startLine == 2);
                FOLD_CHECK("stage 3 syntax-error snapshot", pSurvivor->endLine == 8);
            }

            // The COMMON mid-typing case is the one that matters most: the unit set
            // did not change, only an end line moved, so the fold matches on its
            // anchor alone and survives an unclean parse. Without this, making
            // syntax errors actionable would pop folds open on every keystroke.
            FoldCandidates sameShape;
            sameShape.push_back(MakeFoldItem(0,  20, 0));
            sameShape.push_back(MakeFoldItem(2,   9, 1));
            sameShape.push_back(MakeFoldItem(10, 19, 1));

            FoldModel typing;
            typing.RebuildFromSnapshot(full, true, -1);
            typing.SetCollapsedAt(2, true);

            FOLD_CHECK("stage 3 syntax-error snapshot", typing.RebuildFromSnapshot(sameShape, false, 20));
            FOLD_CHECK("stage 3 syntax-error snapshot", !typing.IsSnapshotClean());
            FOLD_CHECK("stage 3 syntax-error snapshot", typing.Count() == 3);
            FOLD_CHECK("stage 3 syntax-error snapshot", typing.IsAnchorAt(2));
            FOLD_CHECK("stage 3 syntax-error snapshot", typing.IsCollapsedAt(2));
            // and the refreshed end line really came from the UNCLEAN snapshot,
            // which is the point of acting on it (8 -> 9, no resolver attached so
            // ClampEndLine passes it through)
            FOLD_CHECK("stage 3 syntax-error snapshot", AnchorEndLine(typing, 2) == 9);
        }

        // 9c. Step 5 - an UNTRUSTED range is CLAMPED, not dropped. Its end was
        //     force-closed rather than read, so RegionEndLines pulls it back to the
        //     last line the parse confirmed: the line before the cut. The region
        //     still exists, still anchors, still collapses - it just stops short, and
        //     grows back on the next parse that reaches a real terminator.
        //
        //     The clamp fires only when the range ends EXACTLY ON the cut. That
        //     equality is what separates a range the parser closed WITH the offending
        //     token from one it closed at EOF, and scenarios (i) and (v) below are
        //     the two halves of that distinction - (i) clamps the pair force-closed
        //     at the error and leaves the body that ends past the cut alone, (v) is
        //     what breaking that rule cost.
        //
        //     Clamping rather than dropping is the MEASURED answer, not the obvious
        //     one. Dropping untrusted items in FilterItems was implemented first and
        //     the suite refuted it: a failed node is in EVERY ancestor's subtree, so
        //     one broken construct marks the whole enclosing chain untrusted, the
        //     ranges leave m_available, and a fold whose range is gone has nothing to
        //     be refreshed from - D16 keeps it alive as an orphan, but holding the end
        //     line it had BEFORE the error, so folds that were accurate and nowhere
        //     near the error hide the wrong lines. Case 33
        //     (columnar-cut-outside-a-fold-leaves-it-collapsed) lost the user's
        //     collapse of an IF whose own `END IF;` was intact and read trusted=1.
        //     Keeping the ranges present is what protects that, and (iv) below is the
        //     reduced form of it.
        {
            // (i) Case 38's MEASURED shape (FOLDING.good:1159-1183). After `END IF;`
            //     is cut, all three items read trusted=0 while the suite still showed
            //     regions=3, region 2 (the IF) ending on `END p1;`. The transcript
            //     demonstrates the defect rather than asserting it: `fold 2 collapse`
            //     then `expect hidden 7 1` - collapsing the IF hid the user's real
            //     `END p1;`. Measured cutline=7, which is 1-based there and 6 here.
            //     0-based lines; no resolver is attached so ClampEndLine passes ends
            //     through: PackageBody 0..8, Procedure 1..6, If 3..6.
            FoldCandidates atCut;
            atCut.push_back(MakeUntrustedFoldItem(0, 8, 0));
            atCut.push_back(MakeUntrustedFoldItem(1, 6, 1));
            atCut.push_back(MakeUntrustedFoldItem(3, 6, 2));

            FoldModel untrusted;
            untrusted.RebuildFromSnapshot(atCut, false, 6);

            // All three regions survive. D16 adoption matches a fold against a RANGE,
            // so holding the ranges is what keeps an unrelated collapse alive.
            FOLD_CHECK("step 5 clamped range", untrusted.Count() == 3);

            // The defect, closed: the IF stops before the cut, so `END p1;` on line 6
            // stays visible instead of being hidden by a range that never owned it.
            if (untrusted.IsAnchorAt(3))
                FOLD_CHECK("step 5 clamped range", AnchorEndLine(untrusted, 3) == 5);

            // p1 ends on the cut too, so it is clamped as well even though `END p1;`
            // really is on that line. Neither the per-subtree flag nor the cut can
            // tell "your end is wrong" from "something inside you is wrong": both
            // were force-closed by the same token. So p1 folds one line short until
            // `END IF;` comes back. That is the honest cost, and it beats the
            // alternative - dropping them removes two correct regions to remove one
            // incorrect one, during ORDINARY TYPING, since an open construct is
            // exactly this state.
            if (untrusted.IsAnchorAt(1))
                FOLD_CHECK("step 5 clamped range", AnchorEndLine(untrusted, 1) == 5);

            // The package body is untrusted as well and is NOT clamped: its end is
            // line 8, past the cut, because the parser reached EOF and closed it
            // there. That end is a line the parse really did read, so shortening it
            // would lose folding for no reason - and when the body is the only item
            // in the buffer, shortening it runs the region onto its own anchor and
            // the fold disappears altogether. Scenario (v) is that case.
            if (untrusted.IsAnchorAt(0))
                FOLD_CHECK("step 5 clamped range", AnchorEndLine(untrusted, 0) == 8);

            // Contrast: the SAME ranges with the flag left at its default are not
            // clamped even though the cut is still 6, so the checks above cannot also
            // pass on a model that simply shortens every end it is handed.
            FoldCandidates sameShapeTrusted;
            sameShapeTrusted.push_back(MakeFoldItem(0, 8, 0));
            sameShapeTrusted.push_back(MakeFoldItem(1, 6, 1));
            sameShapeTrusted.push_back(MakeFoldItem(3, 6, 2));

            FoldModel trusted;
            trusted.RebuildFromSnapshot(sameShapeTrusted, false, 6);

            FOLD_CHECK("step 5 clamped range", trusted.Count() == 3);
            FOLD_CHECK("step 5 clamped range", trusted.IsAnchorAt(3));

            if (trusted.IsAnchorAt(3))
                FOLD_CHECK("step 5 clamped range", AnchorEndLine(trusted, 3) == 6);

            // (ii) PER ITEM, which is the granularity step 5 exists for and the thing
            //      an all-or-nothing snapshot gate cannot express: the untrusted
            //      parent ends on the cut and is clamped, while its trusted child
            //      keeps the extent the parser actually read.
            FoldCandidates mixed;
            mixed.push_back(MakeUntrustedFoldItem(0, 6, 0));
            mixed.push_back(MakeFoldItem(2, 4, 1));

            FoldModel partial;
            partial.RebuildFromSnapshot(mixed, false, 6);

            FOLD_CHECK("step 5 clamped range", partial.Count() == 2);

            if (partial.IsAnchorAt(0))
                FOLD_CHECK("step 5 clamped range", AnchorEndLine(partial, 0) == 5);
            if (partial.IsAnchorAt(2))
                FOLD_CHECK("step 5 clamped range", AnchorEndLine(partial, 2) == 4);

            // (iii) The nesting floor, and the reason the pass runs backwards. Same
            //       shape, but the trusted child now ends where the parent does, so
            //       clamping the parent to 5 would put its end ABOVE its own child's.
            //       Regions that overlap without nesting are exactly what LineMapper
            //       cannot represent - it builds one prefix sum over the hidden runs
            //       - so the child holds the parent at 6 instead.
            FoldCandidates floored;
            floored.push_back(MakeUntrustedFoldItem(0, 6, 0));
            floored.push_back(MakeFoldItem(2, 6, 1));

            FoldModel held;
            held.RebuildFromSnapshot(floored, false, 6);

            FOLD_CHECK("step 5 clamped range", held.Count() == 2);

            if (held.IsAnchorAt(0))
                FOLD_CHECK("step 5 clamped range", AnchorEndLine(held, 0) == 6);
            if (held.IsAnchorAt(2))
                FOLD_CHECK("step 5 clamped range", AnchorEndLine(held, 2) == 6);

            // (iv) The rebuild path clamps too, and - the entire point of clamping
            //      rather than dropping - every range is still in the snapshot to be
            //      matched against, so a collapse the user set on an UNRELATED region
            //      survives a sibling becoming untrusted. This is case 33 reduced: the
            //      cut is inside b, the collapse is on a, and a must not be the
            //      casualty.
            FoldCandidates full;
            full.push_back(MakeFoldItem(0,  19, 0));
            full.push_back(MakeFoldItem(2,   8, 1));
            full.push_back(MakeFoldItem(10, 18, 1));

            FoldModel merged;
            merged.RebuildFromSnapshot(full, true, -1);
            merged.SetCollapsedAt(2, true);

            FOLD_CHECK("step 5 clamped range", merged.Count() == 3);
            FOLD_CHECK("step 5 clamped range", merged.AnyCollapsed());

            FoldCandidates bGaveUp;
            bGaveUp.push_back(MakeUntrustedFoldItem(0,  19, 0));
            bGaveUp.push_back(MakeFoldItem(2,   8, 1));
            bGaveUp.push_back(MakeUntrustedFoldItem(10, 18, 1));

            // Adoption takes the geometry from the snapshot and the collapsed flag
            // from the user, so this is true: same anchors, only ends moved, and a's
            // fold intact. Dropping b instead would have left a with no range to match
            // against and its fold an orphan.
            FOLD_CHECK("step 5 clamped range", merged.RebuildFromSnapshot(bGaveUp, false, 18));
            FOLD_CHECK("step 5 clamped range", merged.Count() == 3);

            if (merged.IsAnchorAt(2))
            {
                // a is trusted and its end did not move - the collapse is intact
                FOLD_CHECK("step 5 clamped range", merged.IsCollapsedAt(2));
                FOLD_CHECK("step 5 clamped range", AnchorEndLine(merged, 2) == 8);
            }
            // b ends on the cut at 18, so it stops at 17. pkg is untrusted as well
            // but ends at 19, past the cut, so the clamp leaves it alone and it keeps
            // the extent the parse really reached.
            if (merged.IsAnchorAt(0))
                FOLD_CHECK("step 5 clamped range", AnchorEndLine(merged, 0) == 19);
            if (merged.IsAnchorAt(10))
                FOLD_CHECK("step 5 clamped range", AnchorEndLine(merged, 10) == 17);

            // (v) The regression the equality test exists to prevent, reproduced.
            //     stream-cut-spanning-a-fold-takes-the-hidden-rows leaves ONE item in
            //     the buffer: an untrusted package body whose `END pkg;` is the last
            //     line while the syntax error is near the top. Its end is nowhere
            //     near the cut, so nothing clamps and the fold survives. Clamping
            //     every untrusted range to the cut instead ran it onto its own
            //     anchor, Unaddressable dropped it, and the case lost its only region
            //     (regions 1 -> 0) - a fold lost to an error that had nothing to do
            //     with the body's extent.
            FoldCandidates onlyBody;
            onlyBody.push_back(MakeUntrustedFoldItem(0, 4, 0));

            FoldModel survivor;
            survivor.RebuildFromSnapshot(onlyBody, false, 1);

            FOLD_CHECK("step 5 clamped range", survivor.Count() == 1);

            if (survivor.IsAnchorAt(0))
                FOLD_CHECK("step 5 clamped range", AnchorEndLine(survivor, 0) == 4);

            // Residual, and it is the blind spot md/CodeFolding_tests.md 7.2 records:
            // case 45's p1 is force-closed at EOF with no failed node in its own
            // subtree, so it reads trusted=1 and its invented end survives both the
            // flag and the cut. That needs an explicit force-closed-at-EOF signal on
            // the node, not a different line comparison.
        }

        // 10. Stage 3 - one edit notification shifts the regions by exactly that
        //     edit. COEDocument de-duplicates the split siblings so that only
        //     one view forwards (Storage notifies every linked EditContext);
        //     these checks pin down what that single call must do, so a double
        //     forward would show up as regions landing one edit too far down.
        {
            FoldCandidates items;
            items.push_back(MakeFoldItem(10, 20, 0));

            FoldModel model;
            model.BuildFromOutline(items);

            FOLD_CHECK("stage 3 shift once", model.Count() == 1);

            // 3 lines inserted ABOVE the anchor move it down by exactly 3
            model.OnLinesInserted(5, 3);
            FOLD_CHECK("stage 3 shift once", model.Get(0).startLine == 13);
            FOLD_CHECK("stage 3 shift once", model.Get(0).endLine   == 23);

            // an insert strictly INSIDE the region moves only its end
            model.OnLinesInserted(15, 2);
            FOLD_CHECK("stage 3 shift once", model.Get(0).startLine == 13);
            FOLD_CHECK("stage 3 shift once", model.Get(0).endLine   == 25);

            // 2 lines deleted above move it back by exactly 2
            model.OnLinesDeleted(5, 2);
            FOLD_CHECK("stage 3 shift once", model.Get(0).startLine == 11);
            FOLD_CHECK("stage 3 shift once", model.Get(0).endLine   == 23);

            // deleting the anchor line itself drops the region: the construct it
            // folded no longer exists (D11)
            model.OnLinesDeleted(11, 1);
            FOLD_CHECK("stage 3 shift once", model.Count() == 0);
        }
        // 11. Stage 5 - revealing a line that sits inside NESTED collapsed folds.
        //     This is the model half of COEditorView::RevealLine, which loops
        //     FindEnclosingCollapsed -> SetCollapsedAt(anchor) until nothing hides
        //     its target any more, so a Go To line, a bookmark jump or the
        //     debugger's execution line never lands somewhere the user cannot see
        //     (D5). Three things have to hold for that loop to be right:
        //     FindEnclosingCollapsed names the INNERMOST region (opening an outer
        //     one first would pop folds the jump never asked about), the loop
        //     terminates with the target visible, and folds off the path to it stay
        //     closed.
        {
            FoldCandidates items;
            items.push_back(MakeFoldItem(0,  19, 0));
            items.push_back(MakeFoldItem(2,   8, 1));
            items.push_back(MakeFoldItem(4,   6, 2));
            items.push_back(MakeFoldItem(11, 18, 1));

            FoldModel model;
            model.BuildFromOutline(items);

            FOLD_CHECK("stage 5 nested reveal", model.Count() == 4);

            LineMapper mapper;
            mapper.SetModel(&model);
            mapper.SetLineCount(20);

            // everything collapsed: the only line left on screen is the outer
            // anchor, so line 5 is hidden behind three nested regions at once
            FOLD_CHECK("stage 5 nested reveal", model.SetCollapsedAt(0,  true));
            FOLD_CHECK("stage 5 nested reveal", model.SetCollapsedAt(2,  true));
            FOLD_CHECK("stage 5 nested reveal", model.SetCollapsedAt(4,  true));
            FOLD_CHECK("stage 5 nested reveal", model.SetCollapsedAt(11, true));

            FOLD_CHECK("stage 5 nested reveal", mapper.IsLineHidden(5));

            // innermost first - line 5 is in the IF block, not merely inside a.
            // EnclosingCollapsedAnchor names the fold by the line the caller
            // already has, which is what SetCollapsedAt wants and what the view
            // uses; comparing it to 4 directly is the assertion the old
            // `FindEnclosingCollapsed(5) == FindAnchorAt(4)` made through an index.
            FOLD_CHECK("stage 5 nested reveal", model.EnclosingCollapsedAnchor(5) == 4);

            // an anchor is never hidden by its own region, so a jump to one needs
            // no reveal at all and must leave every fold exactly as it was
            FOLD_CHECK("stage 5 nested reveal", model.FindEnclosingCollapsed(0) < 0);

            // the reveal loop, run the way the view runs it and bounded the same way
            int passes = 0;

            for (; passes <= model.CollapsedCount(); passes++)
            {
                const int anchor = model.EnclosingCollapsedAnchor(5);

                if (anchor < 0)
                    break;

                model.SetCollapsedAt(anchor, false);
            }

            // one pass per region on the path - 0, 2 and 4, and not 11
            FOLD_CHECK("stage 5 nested reveal", passes == 3);
            FOLD_CHECK("stage 5 nested reveal", !model.IsCollapsedAt(0));
            FOLD_CHECK("stage 5 nested reveal", !model.IsCollapsedAt(2));
            FOLD_CHECK("stage 5 nested reveal", !model.IsCollapsedAt(4));

            // the unrelated sibling keeps its state: a reveal is not an ExpandAll
            FOLD_CHECK("stage 5 nested reveal", model.IsCollapsedAt(11));

            // and the mapper, asked the way the view asks it, agrees
            mapper.Invalidate();
            FOLD_CHECK("stage 5 nested reveal", !mapper.IsLineHidden(5));
            FOLD_CHECK("stage 5 nested reveal", mapper.GetEnclosingCollapsedRegion(5) < 0);
            FOLD_CHECK("stage 5 nested reveal", mapper.IsLineHidden(12));
        }

        // 12. D16 - the fold set is reconciled PER REGION against the snapshot, so
        //     rebuilding the outline neither resurrects a fold the user opened nor
        //     drops one the parse can still account for. Every check below is one
        //     the single-list model failed for a different reason: it answered "did
        //     the structure change" once, for all folds at a time.
        {
            FoldCandidates full;
            full.push_back(MakeFoldItem(0,  29, 0));
            full.push_back(MakeFoldItem(2,   8, 1));
            full.push_back(MakeFoldItem(11, 18, 1));

            FoldModel model;
            model.RebuildFromSnapshot(full, true, -1);

            FOLD_CHECK("D16 per-region folds", model.Count() == 3);
            FOLD_CHECK("D16 per-region folds", model.SetCollapsedAt(2, true));

            // (a) A fold the user made is not re-emitted alongside the range it
            //     belongs to. The union is materialized, not concatenated: a
            //     matched pair appears once, with the parse's geometry and the
            //     user's flag. Twice would hide the same lines twice and
            //     LineMapper's prefix sum would count them twice.
            model.RebuildFromSnapshot(full, true, -1);

            FOLD_CHECK("D16 per-region folds", model.Count() == 3);

            int atAnchor2 = 0;

            // One derivation, not one per element. The loop this replaces tested
            // `i < model.Count()` and called `model.Get(i)` inside, so counting a
            // single anchor rebuilt the whole enumeration twice per region.
            FoldModel::Regions joined;
            std::vector<char>  marks;
            model.JoinedView(joined, marks);

            for (size_t i = 0; i < joined.size(); i++)
                if (joined.at(i).startLine == 2)
                    atAnchor2++;

            FOLD_CHECK("D16 per-region folds", atAnchor2 == 1);

            // (b) Only the END moved, so the fold matches on its anchor and takes
            //     the new extent from the snapshot. The retired Tier 1 refreshed
            //     ends in place for exactly this shape; D16 reaches the same
            //     answer without asking the whole snapshot to be identical.
            FoldCandidates longer;
            longer.push_back(MakeFoldItem(0,  29, 0));
            longer.push_back(MakeFoldItem(2,   9, 1));
            longer.push_back(MakeFoldItem(11, 18, 1));

            model.RebuildFromSnapshot(longer, true, -1);

            FOLD_CHECK("D16 per-region folds", model.Count() == 3);
            FOLD_CHECK("D16 per-region folds", model.IsAnchorAt(2));

            // The two halves of this case now come from the two layers that own
            // them: the fold is the model's answer about line 2, and the extent is
            // the range anchored there - the one that MOVED, which is what makes
            // this the per-item case rather than the whole-snapshot one.
            FOLD_CHECK("D16 per-region folds", model.IsCollapsedAt(2));
            FOLD_CHECK("D16 per-region folds", AnchorEndLine(model, 2) == 9);

            // (c) The range leaves the snapshot and comes back. While it is away
            //     the fold survives on its own - that is the point of keeping the
            //     two collections apart - and when the range returns they are
            //     paired again rather than emitted side by side.
            FoldCandidates aGone;
            aGone.push_back(MakeFoldItem(0,  29, 0));
            aGone.push_back(MakeFoldItem(11, 18, 1));

            model.RebuildFromSnapshot(aGone, false, -1);

            FOLD_CHECK("D16 per-region folds", model.AnyCollapsed());
            // Still reachable at its line even though the snapshot offers no range
            // there: the orphan half of the two-layer design, and the reason
            // FindAnchorRange consults m_collapsed before m_available.
            FOLD_CHECK("D16 per-region folds", model.IsAnchorAt(2));
            FOLD_CHECK("D16 per-region folds", model.IsCollapsedAt(2));

            model.RebuildFromSnapshot(full, true, -1);

            atAnchor2 = 0;

            // Re-TAKE the view, do not re-walk it through Count()/Get(). The
            // rebuild above changed the enumeration, so a view held across it is
            // stale - the index-expiry argument in miniature. Same one-derivation
            // rule as the count in (a), and JoinedView clears its out-params, so
            // reusing them costs no second allocation.
            model.JoinedView(joined, marks);

            for (size_t i = 0; i < joined.size(); i++)
                if (joined.at(i).startLine == 2)
                    atAnchor2++;

            FOLD_CHECK("D16 per-region folds", model.Count() == 3);
            FOLD_CHECK("D16 per-region folds", atAnchor2 == 1);
            FOLD_CHECK("D16 per-region folds", model.IsAnchorAt(2));
            FOLD_CHECK("D16 per-region folds", model.IsCollapsedAt(2));
            // ...and its extent is the snapshot's again, not the orphan's
            FOLD_CHECK("D16 per-region folds", AnchorEndLine(model, 2) == 8);
        }





        // leave the verdict in the log too, so an unattended run that produced
        // no FAIL line can still be told apart from one that never ran
        // 13. D16 - reset is a distinct operation from rebuild, and the three ways
        //     to ask for one all empty the same collection. Keeping them separate
        //     is what lets RebuildFromSnapshot preserve folds without any of these
        //     silently preserving them too.
        {
            FoldCandidates full;
            full.push_back(MakeFoldItem(0,  29, 0));
            full.push_back(MakeFoldItem(2,   8, 1));
            full.push_back(MakeFoldItem(4,   6, 2));
            full.push_back(MakeFoldItem(11, 18, 1));

            // (a) ExpandAll. It clears the state rather than walking the union
            //     flipping flags, so the next rebuild has nothing to adopt and
            //     cannot put the folds back.
            FoldModel expanded;
            expanded.RebuildFromSnapshot(full, true, -1);
            expanded.SetCollapsedAt(2, true);
            expanded.SetCollapsedAt(11, true);
            expanded.ExpandAll();

            FOLD_CHECK("D16 reset semantics", !expanded.AnyCollapsed());
            FOLD_CHECK("D16 reset semantics", expanded.Count() == 4);
            FOLD_CHECK("D16 reset semantics", !expanded.IsCollapsedAt(2));

            expanded.RebuildFromSnapshot(full, true, -1);

            FOLD_CHECK("D16 reset semantics", !expanded.AnyCollapsed());

            // (b) BuildFromOutline, the from-scratch path. A document being
            //     (re)built has no business inheriting the previous one's folds.
            FoldModel rebuilt;
            rebuilt.RebuildFromSnapshot(full, true, -1);
            rebuilt.SetCollapsedAt(2, true);
            rebuilt.BuildFromOutline(full);

            FOLD_CHECK("D16 reset semantics", rebuilt.Count() == 4);
            FOLD_CHECK("D16 reset semantics", !rebuilt.AnyCollapsed());

            // (c) Clear, which is what folding-off, a language change and document
            //     teardown all call. Ranges AND state, because a range the user
            //     folded stops describing anything in every one of those.
            FoldModel cleared;
            cleared.RebuildFromSnapshot(full, true, -1);
            cleared.SetCollapsedAt(2, true);
            cleared.Clear();

            FOLD_CHECK("D16 reset semantics", cleared.Count() == 0);
            FOLD_CHECK("D16 reset semantics", !cleared.AnyCollapsed());

            // (d) CollapseToLevel reads what CAN be folded and writes what IS, so
            //     its result is state a rebuild then preserves per item - the level
            //     itself is not remembered, and must not be. Level 2 folds every
            //     region nested at depth >= 1 and leaves the top level open.
            FoldModel levels;
            levels.RebuildFromSnapshot(full, true, -1);
            levels.CollapseToLevel(2);

            FOLD_CHECK("D16 reset semantics", levels.AnyCollapsed());
            FOLD_CHECK("D16 reset semantics", !levels.IsCollapsedAt(0));
            FOLD_CHECK("D16 reset semantics", levels.IsCollapsedAt(2));
            FOLD_CHECK("D16 reset semantics", levels.IsCollapsedAt(4));

            levels.RebuildFromSnapshot(full, true, -1);

            FOLD_CHECK("D16 reset semantics", levels.IsCollapsedAt(2));
            FOLD_CHECK("D16 reset semantics", levels.IsCollapsedAt(4));

            // (d2) ExpandSubtree opens the spanning region and the folds nested
            //      inside it; the parent and the sibling keep their state.
            FoldModel subtree;
            subtree.RebuildFromSnapshot(full, true, -1);
            subtree.CollapseAll();

            FOLD_CHECK("D16 reset semantics", subtree.ExpandSubtree(2));
            FOLD_CHECK("D16 reset semantics", !subtree.IsCollapsedAt(2));
            FOLD_CHECK("D16 reset semantics", !subtree.IsCollapsedAt(4));
            FOLD_CHECK("D16 reset semantics", subtree.IsCollapsedAt(0));
            FOLD_CHECK("D16 reset semantics", subtree.IsCollapsedAt(11));
            FOLD_CHECK("D16 reset semantics", !subtree.ExpandSubtree(30));

            // (e) D11 under the new model: deleting the anchor line drops the fold
            //     with the range, so an edit cannot leave a collapse with nothing
            //     left to anchor it. The notification now runs over BOTH
            //     collections - shifting only the derived one would leave the
            //     user's state keyed to pre-edit lines.
            FoldModel edited;
            edited.RebuildFromSnapshot(full, true, -1);
            edited.SetCollapsedAt(11, true);
            edited.OnLinesDeleted(11, 1);

            FOLD_CHECK("D16 reset semantics", !edited.IsAnchorAt(11));
            FOLD_CHECK("D16 reset semantics", !edited.AnyCollapsed());
        }


        fLog = OpenSelfTestLog(false);

        if (fLog)
        {
            if (s_selfTestFailures)
                fprintf(fLog, "%d folding self-test check(s) FAILED\n", s_selfTestFailures);
            else
                fprintf(fLog, "all folding self-test checks passed\n");

            fclose(fLog);
        }

        TRACE("LineMapper::DebugSelfTest: %d failure(s)%hs\n", s_selfTestFailures,
              s_selfTestFailures ? " - see %TEMP%\\SQLTools_FoldSelfTest.log" : "");
    }

#endif // _DEBUG

}
