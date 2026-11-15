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
#ifndef __SqlArena_h__
#define __SqlArena_h__

#include <cstddef>

namespace Common
{

// Bulk storage for grid payloads: one contiguous region, bump-allocated, freed wholesale.
// Offsets are 32-bit because the region is contiguous; NullOffset means "no payload".
class SqlArena
{
public:
    typedef unsigned CellOffset;

    enum
    {
        NullOffset = 0,
        Align      = 8,
        BlockSize  = 1 << 20
    };

    SqlArena ();
    ~SqlArena ();

    // Reserves address space and commits it lazily, BlockSize at a time. Falls back to one
    // contiguous malloc when the reservation fails, which costs eager commit but nothing else.
    bool Reserve (size_t bytes);

    // NullOffset when the arena is not reserved or is exhausted.
    CellOffset Allocate (size_t bytes);

    // nullptr for NullOffset and for any offset outside the allocated range.
    const void* At (CellOffset offset) const;
    void*       At (CellOffset offset);

    void Reset ();    // discard every payload, keep the reservation for the next fetch
    void Free ();     // release the region

    // A bump watermark, and the only undo this arena has. Take Mark() immediately before a store
    // whose usefulness is not yet known; if it turns out to be a duplicate, Retract(mark) hands the
    // bytes back. Correct only while nothing allocated after `mark` is still referenced, which is
    // why it exists for the fetch path's intern probe - store, probe, retract on a hit, all inside
    // one cell, with no other allocation in between - and not as a general-purpose facility.
    //
    // Retract does not decommit: Committed() never shrinks, so the block stays mapped and the next
    // allocation reuses it without another VirtualAlloc. That is the point - a duplicate costs no
    // arena at all rather than costing one and getting it back later.
    size_t Mark () const { return m_used; }
    void   Retract (size_t mark);

    size_t Reserved ()  const { return m_reserved; }
    size_t Committed () const { return m_committed; }
    size_t Used ()      const { return m_used; }
    bool   IsReserved () const { return 0 != m_base; }
    bool   IsFallback () const { return m_fallback; }

private:
    bool Grow (size_t needed);

    SqlArena (const SqlArena&);                 // not supported
    SqlArena& operator = (const SqlArena&);

    char*  m_base;
    size_t m_reserved;
    size_t m_committed;
    size_t m_used;
    bool   m_fallback;
};

}

#endif // __SqlArena_h__
