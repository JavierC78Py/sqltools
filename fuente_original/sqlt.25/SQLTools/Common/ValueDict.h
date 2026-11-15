/*
    SQLTools is a tool for Oracle database developers and DBAs.
    Copyright (C) 1997-2004 Aleksey Kochetov

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
#ifndef __ValueDict_h__
#define __ValueDict_h__

#include "SqlValue.h"
#include <vector>

namespace Common
{

// Worker-private intern table (D9), freed at AllRowsFetched. Resolution never needs it - a
// cell is an arena offset - and entries hold {hash, offset} only, the key being re-read from
// the arena payload on a hash match so an entry stays 8 bytes.
class ValueDict
{
public:
    enum
    {
        MaxInternLen = 64,        // longer values rarely repeat; probing them is pure cost
        InitialSlots = 1 << 10,
        MaxSlots     = 1 << 22    // 4 M entries = 32 MB, still far cheaper than the payloads
    };

    ValueDict ();
    ~ValueDict ();

    // Per-column switch, decided once and never revised. Turning it off later stops the
    // probing but keeps the entries already made: they stay valid, they simply stop growing.
    void BeginColumn (unsigned col, bool internable);
    bool IsInternable (unsigned col) const;

    // The offset of an existing payload with the same (col, kind, bytes, truncated), or of a new
    // one carrying `flags`. Returns NullOffset only when the arena is exhausted or kind is Null.
    //
    // evfTruncated is part of that key and evfInterned is not. Truncation is known before the probe
    // and belongs to the value, so leaving it out would let a cut value and a complete one holding
    // the same bytes settle on one payload and both report the wrong flag. evfInterned is decided
    // by the probe's outcome, so it is written after the key is computed and must stay out of it.
    SqlArena::CellOffset Intern (SqlArena& arena, unsigned col, const void* bytes,
                                 size_t len, SqlValueKind kind, unsigned flags = evfNone);

    // The fetch path's entry point, and the reason the arena grew a watermark. A cursor has to
    // materialise a value before anybody can tell whether it is a repeat, so the payload at
    // `stored` already exists and `mark` is arena.Mark() taken immediately before it was stored.
    // On a hit that payload is a duplicate and is given back - arena.Retract(mark) - and the offset
    // already in the table is returned, so a repeat costs no arena at all. On a miss `stored` is
    // entered in the table, flagged evfInterned to match what Intern() would have produced, and
    // returned unchanged.
    //
    // NullOffset passes straight through: a NULL stores no payload, every NULL cell already shares
    // the table's one Null payload, and there is nothing to probe or to retract.
    SqlArena::CellOffset InternStored (SqlArena& arena, unsigned col,
                                       SqlArena::CellOffset stored, size_t mark);

    void Clear ();      // AllRowsFetched: drop the table, the offsets it produced stay valid

    size_t   Entries () const { return m_count; }
    size_t   Slots () const { return m_slots; }
    unsigned Hits () const { return m_hits; }
    unsigned Misses () const { return m_misses; }
    unsigned Stored () const { return m_stored; }

private:
    ValueDict (const ValueDict&);               // owns a raw table: not copyable
    ValueDict& operator = (const ValueDict&);

    struct Entry
    {
        unsigned           hash;
        SqlArena::CellOffset offset;             // NullOffset == slot is empty
    };

    bool Alloc (size_t slots);
    void Grow ();
    bool Matches (SqlArena& arena, SqlArena::CellOffset offset,
                  SqlValueKind kind, const void* bytes, size_t len, bool truncated) const;
    static unsigned HashOf (unsigned col, SqlValueKind kind, const void* bytes, size_t len,
                            bool truncated);

    Entry*                m_table;
    size_t                m_slots;              // always a power of two, 0 == not allocated yet
    size_t                m_count;
    std::vector<unsigned char> m_internable;     // one byte per column, index is the column ordinal
    unsigned              m_hits;
    unsigned              m_misses;
    unsigned              m_stored;
};

}

#endif // __ValueDict_h__
