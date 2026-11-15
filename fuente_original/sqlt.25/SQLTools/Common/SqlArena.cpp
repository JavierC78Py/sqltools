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

#include "SqlArena.h"
#include <cstdlib>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace Common
{

// Offsets are 32-bit, so the region has to fit below 4 GB.
static const size_t MaxReserve = 0xFFFFFF00u;

SqlArena::SqlArena ()
    : m_base(0), m_reserved(0), m_committed(0), m_used(0), m_fallback(false)
{
}

SqlArena::~SqlArena ()
{
    Free();
}

bool SqlArena::Reserve (size_t bytes)
{
    Free();

    if (Align >= bytes || MaxReserve < bytes)
        return false;

    m_base = static_cast<char*>(VirtualAlloc(0, bytes, MEM_RESERVE, PAGE_READWRITE));
    if (0 != m_base)
    {
        m_reserved = bytes;
        m_committed = 0;
        m_fallback = false;
    }
    else
    {
        // No address space for the reservation: stay contiguous with one malloc instead.
        m_base = static_cast<char*>(malloc(bytes));
        if (0 == m_base)
            return false;

        m_reserved = bytes;
        m_committed = bytes;
        m_fallback = true;
    }

    // Offset 0 is NullOffset, so the first payload starts one alignment unit in.
    m_used = Align;
    return true;
}

SqlArena::CellOffset SqlArena::Allocate (size_t bytes)
{
    if (0 == m_base)
        return NullOffset;

    size_t size = (bytes + (Align - 1)) & ~static_cast<size_t>(Align - 1);
    if (0 == size)
        size = Align;

    const CellOffset offset = static_cast<CellOffset>(m_used);
    const size_t end = m_used + size;
    if (end < m_used || m_reserved < end)
        return NullOffset;

    if (m_committed < end && !Grow(end))
        return NullOffset;

    m_used = end;
    return offset;
}

bool SqlArena::Grow (size_t needed)
{
    if (m_fallback)
    {
        m_committed = m_reserved;
        return true;
    }

    size_t commit = (needed + (BlockSize - 1)) & ~static_cast<size_t>(BlockSize - 1);
    if (m_reserved < commit)
        commit = m_reserved;

    if (0 == VirtualAlloc(m_base, commit, MEM_COMMIT, PAGE_READWRITE))
        return false;

    m_committed = commit;
    return true;
}

const void* SqlArena::At (CellOffset offset) const
{
    if (0 == m_base || NullOffset == offset || m_used <= offset)
        return 0;

    return m_base + offset;
}

void* SqlArena::At (CellOffset offset)
{
    if (0 == m_base || NullOffset == offset || m_used <= offset)
        return 0;

    return m_base + offset;
}

void SqlArena::Reset ()
{
    if (0 != m_base)
        m_used = Align;
}

void SqlArena::Retract (size_t mark)
{
    // A mark at or above m_used means the arena was Reset() or Free()d since it was taken, or that
    // the caller passed a mark belonging to a different arena. Doing nothing is the only safe
    // answer in either case: raising m_used would expose bytes nobody has written, and the caller's
    // payload below the mark stays valid whichever way this goes.
    if (0 != m_base && mark < m_used)
        m_used = mark;
}

void SqlArena::Free ()
{
    if (0 != m_base)
    {
        if (m_fallback)
            free(m_base);
        else
            VirtualFree(m_base, 0, MEM_RELEASE);
    }

    m_base = 0;
    m_reserved = 0;
    m_committed = 0;
    m_used = 0;
    m_fallback = false;
}

}
