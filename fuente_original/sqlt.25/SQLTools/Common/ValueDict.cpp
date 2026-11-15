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

#include "ValueDict.h"
#include <cstdlib>
#include <cstring>

namespace Common
{

static const unsigned FnvPrime = 16777619u;
static const unsigned FnvOffset = 2166136261u;

ValueDict::ValueDict ()
    : m_table(0)
    , m_slots(0)
    , m_count(0)
    , m_hits(0)
    , m_misses(0)
    , m_stored(0)
{
}

ValueDict::~ValueDict ()
{
    Clear();
}

void ValueDict::Clear ()
{
    // The table dies; the payloads it pointed at do not. Every offset already published
    // into a cell stays resolvable, because resolution is base + offset with no dictionary.
    free(m_table);
    m_table = 0;
    m_slots = 0;
    m_count = 0;
    m_hits = 0;
    m_misses = 0;
    m_stored = 0;
    m_internable.clear();
}

void ValueDict::BeginColumn (unsigned col, bool internable)
{
    if (m_internable.size() <= static_cast<size_t>(col))
        m_internable.resize(static_cast<size_t>(col) + 1, 0);

    m_internable[col] = internable ? 1 : 0;
}

bool ValueDict::IsInternable (unsigned col) const
{
    return col < m_internable.size() && 0 != m_internable[col];
}

bool ValueDict::Alloc (size_t slots)
{
    Entry* table = static_cast<Entry*>(calloc(slots, sizeof(Entry)));
    if (0 == table)
        return false;

    free(m_table);
    m_table = table;
    m_slots = slots;
    return true;
}

void ValueDict::Grow ()
{
    if (static_cast<size_t>(MaxSlots) <= m_slots)
        return;

    const size_t newSlots = m_slots * 2;
    Entry* table = static_cast<Entry*>(calloc(newSlots, sizeof(Entry)));
    if (0 == table)
        return;                     // keep the old table: it still works, just probes longer

    const size_t mask = newSlots - 1;
    for (size_t i = 0; i < m_slots; ++i)
    {
        const Entry e = m_table[i];
        if (SqlArena::NullOffset == e.offset)
            continue;

        size_t j = e.hash & mask;
        while (SqlArena::NullOffset != table[j].offset)
            j = (j + 1) & mask;
        table[j] = e;
    }

    free(m_table);
    m_table = table;
    m_slots = newSlots;
}

unsigned ValueDict::HashOf (unsigned col, SqlValueKind kind, const void* bytes, size_t len,
                            bool truncated)
{
    unsigned h = FnvOffset;

    const unsigned char* c = reinterpret_cast<const unsigned char*>(&col);
    for (size_t i = 0; i < sizeof(col); ++i)
    {
        h ^= c[i];
        h *= FnvPrime;
    }

    h ^= static_cast<unsigned char>(kind);
    h *= FnvPrime;

    // The truncated bit joins the key up here, beside col and kind and ahead of the bytes, because
    // it is known before the probe and a value cut down to N bytes must not collide with a genuine
    // N-byte one. evfInterned stays out: it is decided by this probe's own outcome.
    h ^= truncated ? static_cast<unsigned char>(evfTruncated)
                   : static_cast<unsigned char>(evfNone);
    h *= FnvPrime;

    // Content-addressed, not column-kind-addressed: a NaN among floats and an Unsupported
    // among named types hash on their own bytes and need no special case.
    const unsigned char* p = static_cast<const unsigned char*>(bytes);
    for (size_t i = 0; i < len; ++i)
    {
        h ^= p[i];
        h *= FnvPrime;
    }

    return h;
}

bool ValueDict::Matches (SqlArena& arena, SqlArena::CellOffset offset,
                         SqlValueKind kind, const void* bytes, size_t len, bool truncated) const
{
    const SqlValueHeader* h = static_cast<const SqlValueHeader*>(arena.At(offset));
    if (0 == h)
        return false;
    if (h->kind != static_cast<unsigned char>(kind))
        return false;
    if (truncated != (0 != (h->flags & evfTruncated)))
        return false;
    if (static_cast<size_t>(h->len) != len)
        return false;

    return 0 == len || 0 == memcmp(h + 1, bytes, len);
}

SqlArena::CellOffset ValueDict::Intern (SqlArena& arena, unsigned col, const void* bytes,
                                        size_t len, SqlValueKind kind, unsigned flags)
{
    const bool truncated = 0 != (flags & evfTruncated);

    // evfInterned is this dictionary's bit to set and no caller's to pass in: it asserts that
    // several cells of `arena` share the payload, which only the probe below can establish. A
    // caller forwarding the flags of a payload that lives in some other arena - AdoptCell() does
    // exactly that with the worker's scratch - could otherwise have the bit copied onto a payload
    // here that nothing else points at. That is not cosmetic: SetText() refuses an interned
    // payload, so the cell would become unrenderable. Stripped once, up front, so every path below
    // stores value properties only and the interned path below is the sole writer of the bit.
    const unsigned valueFlags = flags & ~static_cast<unsigned>(evfInterned);

    if (SqlValueKind::Null == kind || !IsInternable(col) || static_cast<size_t>(MaxInternLen) < len)
    {
        // One payload per cell. The layout is identical, so the resolve side cannot tell
        // the difference and nothing above the arena has to know which policy was used. The
        // caller's flags still have to travel with it: a value too long to intern is exactly
        // the value most likely to have been cut by a fetch limit.
        const SqlArena::CellOffset off = SqlValue::Store(arena, kind, bytes, len, valueFlags);
        if (SqlArena::NullOffset != off)
            ++m_stored;
        return off;
    }

    if (0 == m_table && !Alloc(InitialSlots))
    {
        const SqlArena::CellOffset off = SqlValue::Store(arena, kind, bytes, len, valueFlags);
        if (SqlArena::NullOffset != off)
            ++m_stored;
        return off;
    }

    const unsigned hash = HashOf(col, kind, bytes, len, truncated);
    const size_t mask = m_slots - 1;

    size_t i = hash & mask;
    while (SqlArena::NullOffset != m_table[i].offset)
    {
        if (m_table[i].hash == hash && Matches(arena, m_table[i].offset, kind, bytes, len, truncated))
        {
            ++m_hits;
            return m_table[i].offset;
        }
        i = (i + 1) & mask;
    }

    const SqlArena::CellOffset off = SqlValue::Store(arena, kind, bytes, len, valueFlags | evfInterned);
    if (SqlArena::NullOffset == off)
        return off;

    ++m_stored;
    ++m_misses;

    if ((m_count + 1) * 4 >= m_slots * 3)
        Grow();                     // the table may have moved, so the slot is found again below

    if (m_count < m_slots)          // always leaves one slot empty, so the probes terminate
    {
        const size_t newMask = m_slots - 1;
        size_t j = hash & newMask;
        while (SqlArena::NullOffset != m_table[j].offset)
            j = (j + 1) & newMask;

        m_table[j].hash = hash;
        m_table[j].offset = off;
        ++m_count;
    }

    return off;
}

SqlArena::CellOffset ValueDict::InternStored (SqlArena& arena, unsigned col,
                                              SqlArena::CellOffset stored, size_t mark)
{
    if (SqlArena::NullOffset == stored)
        return stored;              // a NULL: nothing was stored, so there is nothing to probe or give back

    SqlValueHeader* h = static_cast<SqlValueHeader*>(arena.At(stored));
    if (0 == h)
        return stored;              // not a payload this arena handed out; leave the caller's offset alone

    const SqlValueKind kind = static_cast<SqlValueKind>(h->kind);
    const size_t       len  = h->len;
    const void*        body = h + 1;

    // The fetch layer wrote this before it stored the payload, so it arrives already set and has to
    // join the probe. Left out, a hit could hand back a table payload whose truncated bit disagrees
    // with the one this cell just stored, and both cells would then display whichever flag the
    // shared payload happens to carry.
    const bool         truncated = 0 != (h->flags & evfTruncated);

    // The same three refusals Intern() makes, for the same reasons. The Null kind cannot reach the
    // probe (a NULL stores no payload), a column switched off is one payload per cell by decision,
    // and past MaxInternLen the probe costs more than the payload it might save. In every case the
    // caller keeps exactly what it stored, so the resolve side cannot tell which policy ran.
    if (SqlValueKind::Null == kind || !IsInternable(col) || static_cast<size_t>(MaxInternLen) < len)
    {
        ++m_stored;
        return stored;
    }

    if (0 == m_table && !Alloc(InitialSlots))
    {
        ++m_stored;                 // no table to probe: the payload stands on its own
        return stored;
    }

    const unsigned hash = HashOf(col, kind, body, len, truncated);
    const size_t mask = m_slots - 1;

    size_t i = hash & mask;
    while (SqlArena::NullOffset != m_table[i].offset)
    {
        if (m_table[i].hash == hash && Matches(arena, m_table[i].offset, kind, body, len, truncated))
        {
            ++m_hits;
            arena.Retract(mark);    // the duplicate goes back before anything else can allocate over it
            return m_table[i].offset;
        }
        i = (i + 1) & mask;
    }

    // A first sighting. Unlike Intern() there is nothing left to store - the payload is already in
    // the arena - only to flag, so a payload reached either way reads the same to everybody who
    // cares that it is shared by several cells. evfInterned is not part of the key, which is what
    // makes setting it here safe: HashOf and Matches look at (col, kind, bytes, truncated), so no
    // later probe can miss on account of a flag written after its own key was computed.
    h->flags |= static_cast<unsigned char>(evfInterned);

    ++m_stored;
    ++m_misses;

    if ((m_count + 1) * 4 >= m_slots * 3)
        Grow();                     // the table may have moved, so the slot is found again below

    if (m_count < m_slots)          // always leaves one slot empty, so the probes terminate
    {
        const size_t newMask = m_slots - 1;
        size_t j = hash & newMask;
        while (SqlArena::NullOffset != m_table[j].offset)
            j = (j + 1) & newMask;

        m_table[j].hash = hash;
        m_table[j].offset = stored;
        ++m_count;
    }

    return stored;
}

}
