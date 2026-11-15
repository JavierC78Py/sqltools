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

#ifndef __OEFolding_h__
#define __OEFolding_h__

#include <vector>
#include "OpenEditor/OEHelpers.h"


namespace OpenEditor
{
    ///////////////////////////////////////////////////////////////////////////
    // FoldRegion - one foldable RANGE. Geometry and identity, and nothing else.
    //
    // Semantics are fixed by md/CodeFolding.md and must not drift:
    //  - `startLine` is the ANCHOR. It is always visible and carries the
    //    [+]/[-] gutter glyph.
    //  - `endLine` is INCLUSIVE. A folded region hides
    //    startLine+1 .. endLine, that is `endLine - startLine` lines.
    //  - Both are BUFFER lines copied verbatim from FoldCandidate::pos.
    //    `InxToPos` is a COLUMN-only conversion and must never be applied to
    //    them (md/CodeFolding.md, correction C1).
    //  - There is NO mutable state in here any more. A range describes what CAN
    //    be folded; whether the user HAS folded it lives in the collection that
    //    owns that answer - FoldModel::m_collapsed - and is asked of the model.
    //
    // Two fields used to ride along and were dropped, for the same reason:
    // neither was a property of a range.
    //
    //  - `type` had no reader. It was stamped from the FoldCandidate that produced
    //    the range and never consulted again: FilterItems does all its type work
    //    on FoldCandidate, before a FoldRegion exists, and nothing downstream ever
    //    asked a range which construct it came from. EOutlineItemType itself
    //    stays - FoldCandidate needs it to apply D9 - it is just not fold geometry.
    //  - `collapsed` was STATE about a range, not part of it. D16 moved the
    //    user's folds into their own collection but kept stamping a copy of the
    //    flag onto a materialized union, so consumers could go on reading it
    //    where they always had. Keeping that copy is what made the union
    //    necessary in the first place, and the union is what made every mutator
    //    end in a full re-derivation. Both are gone; see FoldModel below.
    //
    // A method went with them. `HiddenLineCount()` was `endLine - startLine`
    // gated on `collapsed`, so losing the flag left it either lying - a count of
    // lines a range nobody folded is not hiding - or a subtraction every caller
    // can spell itself. Its one real reader was LineMapper::Rebuild, and the
    // rule it existed to warn about (UNION the folded ranges, never sum their
    // per-region counts) now sits in the code that implements it; see LineMapper.
    struct FoldRegion
    {
        // Stable ids of the anchor and of the last line. These are IDENTITY, and
        // BOTH collections carry them - for different reasons, and neither reason
        // is optional.
        //
        // m_collapsed needs them to survive an edit or a re-parse: ResyncEntry
        // re-resolves them into lines, which is how a fold outlives the text
        // moving underneath it.
        //
        // m_available needs them as the OTHER HALF OF THE MATCHING KEY. Adopting
        // the user's folds onto a fresh parse compares a collapsed entry against
        // an available one (SameIdentity / SameAnchor), and an entry WITH ids
        // never falls back to a line comparison - by design, see those two. So an
        // id-less m_available matches nothing at all: every fold orphans, the
        // adopted-geometry path in AdoptCollapsed never runs, and JoinedView then
        // lists the same range twice, once folded and once not. It is also where a
        // NEW fold acquires its identity - SetCollapsedAt copies out whatever
        // FindAnchorRange returned, which for a first fold is an m_available
        // entry.
        //
        // Measured, not argued. Dropping the stamping in BuildFromSnapshot (and
        // the m_available pass in ResyncIds) fails 5 FOLDING checks - region count
        // 4->5 at FOLDING.stp:460 and 3->4 at :1329, with `2:8` reported both as
        // :0 and as :1. DebugSelfTest does NOT catch it: it builds models with no
        // resolver, so both sides are already id-less and everything legitimately
        // takes the line-matching fallback. The .stp suites are the only guard.
        LineId anchorId;
        LineId endId;
        int    startLine;
        int    endLine;
        int    depth;      // FoldCandidate::depth, 0 = top level

        FoldRegion ();
        FoldRegion (int start, int end, int depth_);

        bool IsAnchor (int line) const { return line == startLine; }
        bool Spans (int line) const    { return startLine <= line && line <= endLine; }
        bool Hides (int line) const    { return startLine < line && line <= endLine; }
    };

    ///////////////////////////////////////////////////////////////////////////
    // ILineIdResolver - lets FoldModel re-anchor its LineIds after an edit
    // without depending on EditContext/Storage. Stage 3 supplies an
    // implementation on top of EditContext::GetLineId / FindById
    // (OEContext.h:58,198). Mirrors the LineId-keyed approach already used by
    // the auto-pair records.
    class ILineIdResolver
    {
    public:
        virtual ~ILineIdResolver () {}

        virtual int    GetLineCount () const = 0;
        virtual LineId GetLineId (int line) const = 0;
        virtual bool   FindById (const LineId& id, int& line) const = 0;
    };

    ///////////////////////////////////////////////////////////////////////////
    // FoldModel - the per-document set of fold regions plus their collapsed
    // state. Owned by the document, shared by split sibling views
    // (md/CodeFolding.md, decisions D15 and D16).
    //
    // TWO collections, and telling them apart is the whole of D16. Before it
    // there was one, `m_regions`, holding the snapshot's regions with a
    // `collapsed` flag folded into each - and so every re-parse had to answer,
    // for the set as a whole, whether that flag could be carried over.
    // `MergeFrom` answered it all-or-nothing: Tier 1 kept every fold when the
    // unit set was structurally identical, Tier 2 re-expanded all of them when it
    // was not. Deleting one procedure therefore popped every fold in the file
    // open, and no per-item refinement could fix that without growing the
    // all-or-nothing test a per-item case list.
    //
    // The fix is to stop asking one question of two different things:
    //
    //   m_available  DERIVED. What the last outline snapshot says is foldable.
    //                Rebuilt wholesale on every parse, never merged, and holding
    //                no state worth keeping.
    //
    //   m_collapsed  USER STATE. The ranges the user folded, each with the
    //                identity it was folded under. Survives every rebuild of
    //                m_available. Only an explicit reset (Clear, ExpandAll,
    //                folding switched off, language changed) or a failed adoption
    //                removes an entry. Membership in this collection IS the
    //                collapsed state - there is no flag left to disagree with it.
    //
    // Both are sorted startLine ascending, endLine descending, so an enclosing
    // range always precedes the ones nested inside it - the order
    // LineMapper::Rebuild's run merge and the connector-band loop both rely on.
    //
    // D16 also materialized a third collection, the UNION of those two with a
    // `collapsed` flag copied back onto each entry, and made it the only thing
    // consumers read. That kept the change behind the model - the gutter and the
    // mapper did not move - but it left the design holding two answers to one
    // question, and every mutator had to end in a full re-derivation of the copy
    // to stop them diverging. The union is gone. Consumers now ask the layer
    // that owns the answer: IsFolded / IsCollapsedAt for state, GetAvailable for
    // what can be folded, GetCollapsed for what is. JoinedView/Count/Get still
    // describe the joined set, because the oracle and DebugSelfTest were written
    // against that enumeration and pin behaviour through it, but they compute it
    // on demand and nothing on a paint or edit path calls them.
    //
    // A range in m_collapsed with no counterpart in m_available - its construct
    // was deleted, or the parse has not caught up with it yet - still owns its
    // glyph and its hidden lines. That is deliberate. It is what "a region stays
    // collapsed until its own begin/end id pair stops matching" looks like from
    // the view, and a fold that sprang open the instant the outline disagreed
    // with it would be precisely the flicker D16 exists to remove. It is also the
    // affordance that lets the user unhide it: FindAnchorRange consults
    // m_collapsed first, so an orphan is as clickable as any other anchor.
    class FoldModel
    {
    public:
        typedef std::vector<FoldRegion> Regions;

        FoldModel ();

        // May be null; ResyncIds() then only drops regions it cannot repair.
        void SetLineIdResolver (const ILineIdResolver* pResolver) { m_pResolver = pResolver; }

    // Construction:
        // Build from scratch, discarding collapsed state. This is a RESET, not an
        // update: a re-parse of the same document goes through
        // RebuildFromSnapshot, which adopts the folds instead of dropping them.
        void BuildFromOutline (const FoldCandidates& items);

        // Stage 3 entry point: one call per completed background parse, from
        // COEDocument::UpdateFoldsFromOutline.
        //
        // EVERY snapshot is actionable, on every quality (ToDoNotes 107 step 6).
        // Quality arrives as one bool, passed as a bool so this header stays free of
        // OEPlsSqlSupport.h:
        //
        //   bSnapshotClean   quality == oqClean. RECORDED ONLY, gates nothing, so
        //                    IsSnapshotClean() - and the `modelclean` oracle reading
        //                    it - keeps meaning "the parse was clean" rather than
        //                    "the regions are current".
        //
        // Truncation used to be refused wholesale here, in lockstep with a keep-prior
        // rule in PlSqlSupport::EnsureOutline, on the theory that a range spanning the
        // cut has an INVENTED end and merging it would drop every region below the cut
        // and pop the user's folds open. That was over-broad. Step 5's RegionEndLines
        // already clamps PER ITEM - only the one range whose end lands exactly on the
        // cut is pulled back, while everything the parse really read below the cut
        // folds normally - so refusing the whole snapshot froze the parsed prefix too.
        // The symptom: an edit inside a fully-parsed fold never reached the gutter
        // while the file sat over a line/token limit. Folding now follows the parsed
        // prefix on a truncated parse exactly as it does on a syntax error.
        //
        // oqSyntaxError means the WHOLE buffer was parsed and one construct failed, so
        // every clean sibling is accurate and it is the STALE regions that are the
        // fiction; keeping them is what let a deleted procedure hold its fold region
        // indefinitely (ToDoNotes 102). Both unclean qualities therefore rebuild, and
        // since D16 neither one costs the user their folds: m_available is replaced
        // outright and m_collapsed is adopted into it PER ITEM, so only the range whose
        // identity really stopped matching is affected while its siblings stay folded.
        // That was the known cost of the retired all-or-nothing MergeFrom - Tier 1
        // matched on size, so one vanished unit, or a tail the parse never reached,
        // re-expands every fold in the file.
        //
        // A FAILED or force-closed node's own range can be wrong while sitting inside
        // the buffer, because the parser closes it instead of matching it. Step 3
        // stamps that per item as rangeTrusted; step 5 clamps such a region's end to
        // the last line the parse actually confirmed. That clamp is the whole reason a
        // truncated snapshot is now safe to fold rather than refuse.
        //
        // `parseCutLine` is PlSqlSupport::GetParseCutLine() for the SAME snapshot: the
        // 0-based buffer line where the parse stopped, or -1 when it ran to the end of
        // the text. See RegionEndLines in OEFolding.cpp for why it bounds an untrusted
        // end. Passed in rather than queried because the model must not reach back into
        // the support object for a value that has to describe the snapshot it was just
        // handed - not whatever the live parse says now, which by the time an idle
        // rebuild runs may already be a later one.
        //
        // Returns true when the region set changed, so the caller can
        // invalidate the line mapper and repaint.
        bool RebuildFromSnapshot (const FoldCandidates& items, bool bSnapshotClean,
                                  int parseCutLine);

        // `modelclean` in the .outlinequality transcript: the quality of the
        // snapshot the model was last rebuilt from. Recorded, never gated on.
        bool IsSnapshotClean () const { return m_bSnapshotClean; }

        // The reset D16 names: folding switched off, language changed, document
        // discarded. Takes the user's folds with it, deliberately - a reset is not
        // a re-parse.
        void Clear ();

    // The two layers themselves. Both sorted startLine asc, endLine desc.
        const Regions& GetAvailable () const { return m_available; }
        const Regions& GetCollapsed () const { return m_collapsed; }

        // Count() is the joined set, so it exceeds AvailableCount() exactly while
        // a collapsed entry has no available counterpart - the D16 orphan case the
        // split exists to make visible rather than to paper over.
        int  AvailableCount () const       { return (int)m_available.size(); }
        int  CollapsedCount () const       { return (int)m_collapsed.size(); }

    // State. These replaced the `collapsed` flag that used to be stamped onto a
    // materialized union: the answer lives in m_collapsed, and asking it is now
    // the only way to get one.
        bool Empty () const { return m_available.empty() && m_collapsed.empty(); }
        bool AnyCollapsed () const;

        // The range anchored at `line`, or NULL. m_collapsed is consulted FIRST
        // and wins a tie, so the answer is the range whose state actually matters
        // and an orphan - folded, but no longer offered by the snapshot - is found
        // like any other anchor. This is what the glyph, the [...] marker, hover
        // and a gutter click all ask.
        const FoldRegion* FindAnchorRange (int line) const;
        bool IsAnchorAt (int line) const { return FindAnchorRange(line) != NULL; }

        // Is the range anchored at `line` folded? False when nothing is anchored
        // there at all: a line INSIDE a folded body is not a collapsed anchor, and
        // reporting the model's own answer avoids inventing a second meaning for
        // it. That is also what the old flag-on-every-region representation
        // invited, by making the question look like a field read.
        bool IsCollapsedAt (int line) const;

        // Is THIS range one the user folded - membership in m_collapsed, by
        // identity. The connector-band loop walks m_available and asks it once per
        // range in the window, which is exactly the question the retired
        // `r.collapsed` answered. Linear in CollapsedCount(): the number of folds
        // a user makes is small and the number of ranges intersecting one repaint
        // is smaller, so the product is not worth an index.
        bool IsFolded (const FoldRegion& r) const;

        // Index into GetCollapsed() of the innermost folded range hiding `line`,
        // or -1. The index space moved with the union: callers that only test it
        // against 0 are unaffected, and the one that dereferenced it now asks
        // EnclosingCollapsedAnchor instead.
        int  FindEnclosingCollapsed (int line) const;
        // The anchor line of that range, or -1. What RevealLine actually wants: it
        // opens folds BY ANCHOR, and holding an index into a collection it is
        // about to mutate is exactly the thing to avoid.
        int  EnclosingCollapsedAnchor (int line) const;

        // The innermost m_available region that Spans `line`, or NULL - the fold the caret is "in".
        const FoldRegion* FindEnclosingAvailable (int line) const;

    // The joined view - every distinct range across both layers, in the order the
    // retired union had, folded ones marked. DIAGNOSTIC AND TEST OBSERVATION ONLY:
    // computed on demand, so each call is O(n), and no paint or edit path uses
    // them. The .expect region / .expect regions oracle and DebugSelfTest were
    // written against this enumeration and keep pinning behaviour through it,
    // which is why it survives the union that used to materialize it.
    //
    // `FindAnchorAt` did NOT survive it, and neither did `IsCollapsed(i)`. Both
    // keyed their answer to an index into this enumeration, and an index into a
    // set that is rebuilt on every call is a value that expires the moment
    // anything changes - holding one across a toggle was already wrong against
    // the union, and is worse against a temporary. `FindAnchorAt`'s one consumer
    // asked "which range is anchored here", and FindAnchorRange answers that by
    // returning the range itself, so the index was a detour on the way to data
    // the caller can now get directly. `IsCollapsed(i)`'s last reader was the
    // .expect region oracle, which now takes the whole view in one call and reads
    // the folded mark out of it - so it had none left, and its question is better
    // asked of a line (IsCollapsedAt) or a range (IsFolded), neither of which
    // expires when the model moves.
        // The whole enumeration in ONE call: `ranges`, plus a `folded` mark
        // parallel to it. Count and Get are conveniences over exactly this merge
        // and EACH re-derives it, so a caller walking the set must take it once
        // here. The natural loop form
        //     for (i = 0; i < Count(); i++) ... Get(i) ...
        // pays two derivations per element - one of them in the iteration test -
        // and each derivation allocates five vectors. That is O(n^2) in the
        // region count for a walk that is O(n) of information.
        //
        // There is deliberately no cache behind this. A materialized joined set
        // that every mutator has to invalidate IS m_regions - the union - under
        // another name, and retiring it is the whole of this change. Deriving on
        // demand is what keeps the two layers the only state there is.
        void JoinedView (Regions& ranges, std::vector<char>& folded) const;

        int        Count () const;
        // By VALUE. There is no collection behind this enumeration to point into,
        // and returning a reference to a temporary would be a dangling one.
        FoldRegion Get (int i) const;

    // Collapsed state - writes m_collapsed, which IS the state:
        void ExpandAll ();
        void CollapseAll ();
        // VS Code's "Fold Level N", 1-based: every region nested at level N or
        // deeper folds, shallower ones expand - level 1 is CollapseAll, level 2
        // keeps top-level bodies open. It first shipped as the mirror image ("fold
        // depth <= maxDepth"), under which every level past 1 looks identical to
        // CollapseAll because a folded top level hides its children whatever their
        // own state; md/CodeFolding.md stage 7 redefined it before any caller
        // existed. Reads `depth` off m_available and writes m_collapsed, so the
        // bulk commands - this, CollapseAll and ExpandAll - never touch the joined
        // enumeration. One that did want orphans included would take them from
        // JoinedView, whose entries carry depth and the folded mark together.
        void CollapseToLevel (int level);
        // Expand the innermost available region spanning `line` and every fold
        // inside its span; true when anything opened. VS Code's "Unfold Recursively".
        bool ExpandSubtree (int line);
        // Toggle the region anchored at `line`; returns its new state, or
        // false when there is no region anchored there.
        bool ToggleAt (int line);
        bool SetCollapsedAt (int line, bool collapsed);

    // Edit notifications, applied to BOTH source collections: a collapsed entry's
    // lines move with the text exactly like an available one's, and an entry that
    // stopped moving with it would stop matching by line the moment the user typed.
        // `count` lines were inserted immediately BEFORE `line`.
        void OnLinesInserted (int line, int count);
        // `count` lines starting AT `line` were deleted. Regions anchored on a
        // deleted line are dropped - their structure is gone (D11).
        void OnLinesDeleted (int line, int count);
        // Re-resolve anchorId/endId against the live buffer. Call after the
        // two above once the storage has settled.
        void ResyncIds ();

    private:
        FoldModel (const FoldModel&);              // no copy
        FoldModel& operator = (const FoldModel&);

        // Applies D9 (a SELECT folds as ONE region, so oitFromClause and
        // oitWhereClause are dropped) and D10 (single-line candidates are not
        // foldable). oitUnknown is dropped too - its range is unreliable.
        // This is folding's ONLY filter. The pane's is not a filter at all any
        // more: the walk decides pane membership while it emits, so a folding-only
        // type never reaches an OutlineItems vector to be screened out later.
        static void FilterItems (const FoldCandidates& in, FoldCandidates& out);
        // startLine asc, then endLine desc, so an enclosing region always
        // precedes the regions nested inside it. Stable, so snapshot order is
        // preserved among equals.
        static void SortItems (FoldCandidates& items);
        // The same order for regions. Stable, so sorting never disturbs the
        // snapshot's order among ranges that start and end alike.
        static void SortRegions (Regions& regions);

        // Reject overlapping non-nested regions, exact duplicates, and any
        // region whose lines the buffer cannot address (a fold candidate may end
        // one line past the buffer - see ClampEndLine in OEFolding.cpp), then
        // install the result as the WHOLE of m_available, everything expanded.
        // m_collapsed is not touched: replacing the snapshot is not a reset of
        // the user's folds, and confusing the two is what D16 exists to prevent.
        void BuildAvailableFromFiltered (const FoldCandidates& filtered);

        // Reconcile m_collapsed against the m_available that was just built, one
        // entry at a time - the per-item replacement for MergeFrom's
        // all-or-nothing test. An entry that still matches a range adopts that
        // range's geometry, so a construct that grew or shrank keeps its fold;
        // one that matches nothing survives on its own iff it still passes
        // ResyncEntry. See AdoptCollapsed in OEFolding.cpp for the exact rules.
        void AdoptCollapsed ();

        // The joined view the observation API enumerates: every distinct range
        // across both layers, in the order the retired union had, with `folded`
        // marking the ones the user folded. A collapsed entry matching an
        // available one appears ONCE, in that available entry's place; an
        // unmatched one appears on its own so it keeps the glyph that lets the
        // user unhide it. This is m_regions computed on demand rather than
        // stored - see the class comment for why storing it is what the
        // two-layer refactor removed.
        void MergeRanges (Regions& out, std::vector<char>& folded) const;

        // Restore the sort invariant on m_collapsed. Most mutators preserve it,
        // but ResyncEntry rewrites an entry's lines FROM ITS IDS and so can move
        // it anywhere in the buffer, and LineMapper::Rebuild's single-pass run
        // merge depends on the order holding. Cheap: this collection is the
        // user's folds, not the snapshot.
        void SortCollapsed ();

        // Index of the m_available entry `e` stands for, or -1. bExact asks for
        // the whole (anchorId, endId) pair; otherwise the anchor alone suffices,
        // which is how a collapsed range follows its construct when only the end
        // moved. `taken` stops a second collapsed entry claiming the same range.
        int  FindAvailableMatch (const FoldRegion& e, bool bExact,
                                 const std::vector<char>& taken) const;
        // The pairing AdoptCollapsed and MergeRanges both work from: for every
        // m_collapsed entry, the m_available index it stands for or -1. Shared so
        // the two can never disagree about which fold belongs to which range.
        void MatchCollapsed (std::vector<int>& match,
                             std::vector<char>& taken) const;
        // Drop every m_collapsed entry standing for the same range as `e`.
        void RemoveCollapsedAt (const FoldRegion& e);

        // Re-resolve one entry's lines from its ids. Returns false when the entry
        // must be dropped: its anchor no longer resolves, or nothing is left for
        // it to hide. This is the rule ResyncIds has always applied, factored out
        // so the edit path and adoption's unmatched branch cannot drift apart.
        bool ResyncEntry (FoldRegion& r) const;
        // ResyncEntry over a whole collection, erasing what it rejects.
        void ResyncCollection (Regions& regions) const;

        static void ShiftInserted (Regions& regions, int line, int count);
        static void ShiftDeleted  (Regions& regions, int line, int count);

        // Identity, D16's key. Two entries are the SAME RANGE when both carry ids
        // and the ids agree, or when neither does and the line pair agrees. The
        // line pair is that same rule expressed in the only identity a model with
        // no resolver has, which is what DebugSelfTest builds. A mixed pair is
        // never a match - an entry without an identity cannot vouch for one that
        // has it.
        static bool SameIdentity (const FoldRegion& a, const FoldRegion& b);
        // The same, relaxed to the anchor alone: the range whose END moved.
        static bool SameAnchor (const FoldRegion& a, const FoldRegion& b);

        Regions                m_available;
        Regions                m_collapsed;
        const ILineIdResolver* m_pResolver;
        // quality of the snapshot m_available came from - see RebuildFromSnapshot
        bool                   m_bSnapshotClean;
        // parse cut of that same snapshot, 0-based, -1 when the parse reached the
        // end of the text. An untrusted range whose end lands EXACTLY on this line
        // was terminated by the failure rather than by its own terminator, so its
        // end is pulled back one line - see RegionEndLines in OEFolding.cpp.
        int                    m_parseCutLine;
    };

    ///////////////////////////////////////////////////////////////////////////
    // LineMapper - the ONLY translation authority between buffer lines and
    // visual rows (md/CodeFolding.md §4). Storage, EditContext, undo/redo,
    // search, the clipboard and the syntax tree never see a visual row; only
    // the view's four choke points do.
    //
    // A collapsed region hides a contiguous run of buffer lines, so the mapping
    // is a prefix sum over those runs and both directions reduce to one binary
    // search plus an offset. The sum is cached and rebuilt lazily after
    // Invalidate(), which the owner must call on any toggle or storage change.
    //
    // Those runs are a UNION, never a sum of per-region counts, and the
    // distinction is the whole of Rebuild. Regions may legitimately share an end
    // line - an inner block ending where its parent ends - so a parent folded
    // over an already folded child must contribute the child's lines exactly
    // once. Rebuild therefore merges a run into its predecessor whenever they
    // overlap or touch, in a single pass, which is why GetCollapsed()'s sort
    // order (startLine ascending, endLine descending, so an enclosing range
    // always precedes the ones nested inside it) is load-bearing rather than
    // merely tidy.
    class LineMapper
    {
    public:
        LineMapper ();

        // The model is NOT owned and must outlive the mapper. Either setter
        // invalidates the cache.
        void SetModel (const FoldModel* pModel);
        void SetLineCount (int lineCount);
        void Invalidate ();

    // Row <-> line:
        // Visible rows, i.e. GetLineCount() minus the hidden lines.
        int GetVisualLineCount () const;
        // Visual row a buffer line is drawn on. A visible line maps to its own
        // row; a HIDDEN line maps to the row of the anchor above its run, the
        // one carrying the [...] marker. That is what puts a caret moved into a
        // collapsed region somewhere sensible instead of off screen.
        //
        // The anchor row is named explicitly rather than derived from
        // line - hiddenBefore(line): that expression is constant across a run
        // and equal to the row BELOW it, which does not exist when the run
        // reaches the end of the buffer - a package body collapsed over its own
        // `end;`. Naming the anchor keeps every result inside
        // [0, GetVisualLineCount()), which is what lets the view index a row
        // array with it unchecked.
        int BufferToVisual (int line) const;
        // Inverse. Always returns a VISIBLE line - the anchor when `row` is a
        // marker row.
        int VisualToBuffer (int row) const;

    // Visibility:
        bool IsLineHidden (int line) const;
        // First visible line at or after `line`, or GetLineCount() when there
        // is none. "Move down" calls this with line + 1.
        int  NextVisibleLine (int line) const;
        // Last visible line at or before `line`, or -1 when there is none.
        int  PrevVisibleLine (int line) const;
        // Index into FoldModel::GetCollapsed() of the innermost folded range
        // hiding `line`, or -1. Delegates to FoldModel::FindEnclosingCollapsed.
        // The index names a LAYER, not a joined enumeration, so it is only
        // meaningful as an index into GetCollapsed() - or as a "is anything
        // hiding this line" test, which is all most callers want.
        int  GetEnclosingCollapsedRegion (int line) const;

        int GetLineCount () const { return m_lineCount; }

#ifdef _DEBUG
        // Builds synthetic region sets - nested, adjacent, touching EOF, empty
        // model, a single collapsed range at line 0 - and asserts the round
        // trip, the hidden count and monotonicity. Called once from
        // CSQLToolsApp::InitInstance; md/CodeFolding.md stage 2 checks the
        // mapper in isolation, before any rendering exists to blame.
        // bUnattended: append failures to %TEMP%\SQLTools_FoldSelfTest.log and
        // carry on, instead of raising a modal assert dialog. Pass the harness's
        // /AutomatedTesting state - under the automated suites nobody is there
        // to dismiss a dialog, and a blocked InitInstance hangs every suite
        // queued behind it.
        static void DebugSelfTest (bool bUnattended);
#endif

    private:
        LineMapper (const LineMapper&);              // no copy
        LineMapper& operator = (const LineMapper&);

        // One maximal run of hidden buffer lines, [first..last] inclusive.
        // `hiddenBefore` is the prefix sum - the hidden lines strictly below
        // `first`. Runs ascend and never touch, because adjacent ones are
        // merged at build time.
        struct HiddenRun
        {
            int first;
            int last;
            int hiddenBefore;
        };

        void EnsureCache () const;
        void Rebuild () const;
        // Index of the run containing `line`, or -1.
        int  FindRun (int line) const;
        // Hidden lines strictly below `line`.
        int  HiddenBefore (int line) const;

        const FoldModel* m_pModel;
        int              m_lineCount;

        // mutable so the const queries can refresh the cache lazily
        mutable std::vector<HiddenRun> m_runs;
        mutable int                    m_hiddenTotal;
        mutable bool                   m_bValid;
    };

}

#endif // __OEFolding_h__
