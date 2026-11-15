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

// No precompiled header here, deliberately: this file is compiled both with and without one.

// Common/NVector.h reports a bad subscript through _THROW2, a macro the MSVC STL no longer
// provides, so it is defined here for any build that has not already.
#include <algorithm>    // std::sort, for the row-map permutation
#include <stdexcept>
#ifndef _THROW2
#define _THROW2(x, y) do { throw (x(y)); } while (false)
#endif

#include "ValueTable.h"
#include "CalcLines.h"      // calc_lines: the one definition, shared verbatim with StringTable.cpp

namespace OG2
{

using Common::SqlArena;
using Common::SqlValue;
using Common::SqlValueKind;
using Common::SqlValueHeader;

ValueTable::ValueTable ()
    : m_cells ("ValueTable::m_cells")
    , m_images("ValueTable::m_images")
    , m_rowMap("ValueTable::m_rowMap")
    , m_nullPayload(SqlArena::NullOffset)
    , m_columns(0)
    , m_sortColumn(-1)
    , m_sortAscending(true)
    , m_lazyRender(false)
{
}

ValueTable::~ValueTable ()
{
    m_arena.Free();
}

size_t ValueTable::DefaultReserve ()
{
#ifdef _WIN64
    return static_cast<size_t>(2) * 1024 * 1024 * 1024;
#else
    return static_cast<size_t>(256) * 1024 * 1024;
#endif
}

bool ValueTable::Reserve (size_t bytes)
{
    if (m_arena.IsReserved() && m_arena.Reserved() >= bytes)
        return true;                // already big enough; the reservation is never shrunk

    return m_arena.Reserve(bytes);
}

void ValueTable::Format (int ncols, int nrows)
{
    m_columns = ncols < 0 ? 0 : ncols;
    Clear();

    // SqlArena::Allocate hands back NullOffset until the region exists, so reserving here is what
    // makes the stores below work. Failure is not fatal: every cell then reads back NULL.
    if (!m_arena.IsReserved())
        m_arena.Reserve(DefaultReserve());

    // Reserve, not resize - StringTable::Format did the same, so the caller still appends rows
    // one at a time and a preallocated table does not report rows it has not been given.
    if (nrows > 0)
        ReserveMore(nrows);
}

void ValueTable::Clear ()
{
    DeleteAll();
    m_arena.Reset();      // payloads gone, reservation kept for the next fetch

    // Both hold offsets into the arena just reset, so both go with the payloads. An entry left
    // behind would resolve to whatever the next fetch stores at the same offset.
    m_dict.Clear();
    m_samples.clear();
}

int ValueTable::Append ()
{
    // New cells take m_nullPayload: the shared Null payload when SetNullText() has a marker,
    // NullOffset when it does not. A cell costs 4 bytes either way and no row ever allocates.
    const int row = GetRowCount();
    m_cells.resize(m_cells.size() + m_columns, m_nullPayload);
    m_images.push_back(-1);

    // A sort permutes exactly the rows that exist, so appending without extending the map would
    // put the new row on a display position another row already holds. A fetch cannot hit this.
    if (!m_rowMap.empty())
        m_rowMap.push_back(row);

    return row;
}

void ValueTable::Insert (int row)
{
    if (row < 0 || row > GetRowCount())
        return;

    // Translate before ClearSort() drops the map: callers number rows logically, the surgery is physical.
    const int physical = ToPhysical(row);

    // Row surgery moves every physical position at and below it, so a permutation over them means nothing.
    ClearSort();

    const size_t at = static_cast<size_t>(physical) * m_columns;
    m_cells.insert(m_cells.begin() + at, static_cast<size_t>(m_columns), m_nullPayload);
    m_images.insert(m_images.begin() + physical, -1);
}

void ValueTable::Delete (int row)
{
    if (row < 0 || row >= GetRowCount())
        return;

    const int physical = ToPhysical(row);   // as Insert(): before the surgery can move anything

    // Unlike Insert, a delete changes no surviving row's sort key, so the order the sort produced
    // still describes the rows left. Only the deleted row's slot goes; the rest shift up by one.
    if (!m_rowMap.empty())
    {
        m_rowMap.erase(std::remove(m_rowMap.begin(), m_rowMap.end(), physical), m_rowMap.end());

        // Kept rather than ClearSort()'d: the header glyph stays truthful and the grid stays in the
        // order the user put it in, instead of jumping back to fetch order over an untouched sort.
        for (size_t i = 0; i < m_rowMap.size(); ++i)
            if (m_rowMap[i] > physical)
                --m_rowMap[i];
    }

    const size_t at = static_cast<size_t>(physical) * m_columns;
    m_cells.erase(m_cells.begin() + at, m_cells.begin() + at + m_columns);
    m_images.erase(m_images.begin() + physical);

    // The deleted row's payloads are deliberately not reclaimed: the arena has no per-payload
    // free, and every other row's offsets must stay valid until Clear().
}

void ValueTable::DeleteAll ()
{
    m_cells.clear();
    m_images.clear();
    ClearSort();        // and the sort that described them: m_sortColumn must not outlive the map
    m_arena.Reset();

    // Reset() drops every payload including the shared Null one, so the handle goes with them; a
    // stale one would let the next Append point cells at a slot the arena re-hands to a real value.
    m_nullPayload = SqlArena::NullOffset;

    // Resetting here is what stops a delete-all/refetch cycle growing the arena without bound:
    // with no rows left, no offset into it can be live.
}

void ValueTable::ReserveMore (int nrows)
{
    if (nrows <= 0)
        return;

    const size_t moreRows = static_cast<size_t>(nrows);

    if (m_columns > 0)
    {
        const size_t wantCells = m_cells.size() + moreRows * m_columns;
        if (m_cells.capacity() < wantCells)
            m_cells.reserve(wantCells);
    }

    const size_t wantRows = m_images.size() + moreRows;
    if (m_images.capacity() < wantRows)
        m_images.reserve(wantRows);
}

bool ValueTable::InRange (int row, int col) const
{
    return row >= 0 && col >= 0 && col < m_columns && row < GetRowCount();
}

// At() is the single funnel every cell read goes through, so it is also the single place the row
// map is applied. InRange() validates the logical row; ToPhysical() then translates it.
ValueTable::CellOffset ValueTable::At (int row, int col) const
{
    // Two checks, deliberately: InRange supplies the documented non-throwing contract the grid
    // needs while painting, and .at() names the vector a logic error misindexed.
    if (!InRange(row, col))
        return SqlArena::NullOffset;

    return m_cells.at(CellIndex(ToPhysical(row), col));
}

void ValueTable::SetCell (int row, int col, CellOffset off)
{
    if (!InRange(row, col))
        return;     // a dropped publish means the caller mis-sized the table; see ReserveMore

    m_cells.at(CellIndex(ToPhysical(row), col)) = off;
}

SqlValue ValueTable::GetValue (int row, int col) const
{
    // A NULL cell is either offset 0, which maps to kind Null, or the shared Null payload whose
    // header kind is already Null. Both arrive as IsNull(), so no special case is needed here.
    return SqlValue(m_arena.At(At(row, col)));
}

void ValueTable::GetText (int row, int col, std::string& out) const
{
    out.clear();

    const SqlValue value = GetValue(row, col);

    // No IsNull() early return: a NULL cell holds the shared payload, so it falls through to the
    // marker SetNullText() parked there and reads back exactly the text that will be drawn.
    const SqlValueHeader* header = value.Header();
    if (0 == header)
        return;                     // no payload at all: out stays empty

    if (SqlArena::NullOffset != header->textOffset)
    {
        const SqlValue text(m_arena.At(header->textOffset));
        size_t len = 0;
        const char* utf8 = text.Text(len);
        if (0 != utf8)
            out.assign(utf8, len);
        return;
    }

    // textOffset is NullOffset: nothing has been parked. With lazy rendering on the payload is
    // rendered on demand through the format context instead, which covers a NULL cell too.
    if (m_lazyRender)
        out = m_format.Render(value);
}

bool ValueTable::SetText (int row, int col, const char* utf8, size_t len)
{
    const CellOffset off = At(row, col);
    if (SqlArena::NullOffset == off)
        return false;               // NULL by design: nowhere to park the text

    SqlValueHeader* header = static_cast<SqlValueHeader*>(m_arena.At(off));
    if (0 == header)
        return false;

    // Refuse a NULL cell: its payload is the one every NULL cell in the table shares, so parking
    // text here would rewrite the marker for all of them. SetNullText() is the only writer allowed.
    if (SqlValueKind::Null == static_cast<SqlValueKind>(header->kind))
        return false;

    if (SqlArena::NullOffset != header->textOffset)
        return true;                // already rendered, and text is write-once-read-many

    if (0 == utf8)
    {
        utf8 = "";
        len  = 0;
    }

    const CellOffset textOff = SqlValue::StoreText(m_arena, utf8, len);
    if (SqlArena::NullOffset == textOff)
        return false;               // arena exhausted: the value stays readable, its text does not

    header->textOffset = textOff;
    return true;
}

bool ValueTable::OverwriteText (int row, int col, const char* utf8, size_t len)
{
    if (!InRange(row, col))
        return false;

    if (0 == utf8)
    {
        utf8 = "";
        len  = 0;
    }

    // A fresh evfNone payload, so the cell stops sharing whatever it pointed at: an interned
    // neighbour keeps its own text, and a NULL's shared marker is never written through.
    const CellOffset off = SqlValue::StoreText(m_arena, utf8, len);
    if (SqlArena::NullOffset == off)
        return false;               // arena exhausted: the old value stands, nothing half-written

    // Store() leaves textOffset unset. Parking the same bytes as display text keeps the eager path
    // reading the new value back, where the lazy path would have rendered it from the payload.
    const CellOffset textOff = SqlValue::StoreText(m_arena, utf8, len);
    if (SqlArena::NullOffset != textOff)
        static_cast<SqlValueHeader*>(m_arena.At(off))->textOffset = textOff;

    SetCell(row, col, off);
    return true;
}

bool ValueTable::EnsureNullPayload ()
{
    if (SqlArena::NullOffset != m_nullPayload)
        return true;                // already made for this result set

    // Deliberately does not reserve: Format() is the one place that commits address space, and a
    // table that was never formatted must keep failing every store so the misuse is heard about.
    if (!m_arena.IsReserved())
        return false;               // no region: cells stay at offset 0 and read back NULL

    m_nullPayload = SqlValue::StoreNull(m_arena);
    return SqlArena::NullOffset != m_nullPayload;
}

bool ValueTable::SetNullText (const std::string& utf8)
{
    if (!EnsureNullPayload())
        return false;               // no arena: NULLs will render empty, which is degraded not corrupt

    if (SqlValueHeader::MaxLen < utf8.size())
        return false;               // not representable in a 16-bit length; refuse rather than truncate

    SqlValueHeader* header = static_cast<SqlValueHeader*>(m_arena.At(m_nullPayload));
    if (0 == header)
        return false;

    const CellOffset textOff = SqlValue::StoreText(m_arena, utf8.data(), utf8.size());
    if (SqlArena::NullOffset == textOff)
        return false;               // arena exhausted: leave any previous marker in place

    // Write-once-read-many is waived here: this is the marker setter, and it has to work whether
    // it runs before or after the rows that point at the payload. Re-parking leaks bytes.
    header->textOffset = textOff;
    return true;
}

ValueTable::InternSample& ValueTable::Sample (unsigned col)
{
    if (m_samples.size() <= col)
        m_samples.resize(col + 1);  // default-constructed: nothing sampled, nothing decided

    return m_samples[col];
}

ValueTable::CellOffset ValueTable::InternCell (int col, CellOffset stored, size_t mark)
{
    if (SqlArena::NullOffset == stored)
        return stored;              // a NULL: no payload stored, already shared, and not worth sampling

    if (col < 0)
        return stored;              // no ordinal to key a dictionary entry on

    const unsigned c = static_cast<unsigned>(col);
    BeginSampling(c);

    const CellOffset published = m_dict.InternStored(m_arena, c, stored, mark);

    // Comparing the two offsets is valid here and only here: both name m_arena, so equality means
    // the dictionary handed back the payload it was given and the cell kept one of its own.
    CountSample(c, published == stored);

    return published;
}

void ValueTable::BeginSampling (unsigned col)
{
    InternSample& s = Sample(col);

    // Intern optimistically while sampling: distinctness can only be measured by probing, and a
    // miss costs one hash and one memcmp. The sampler is the one place the per-column policy lives.
    if (!s.started)
    {
        s.started = 1;
        m_dict.BeginColumn(col, true);
    }
}

void ValueTable::CountSample (unsigned col, bool keptOwnPayload)
{
    InternSample& s = Sample(col);

    if (s.decided)
        return;

    ++s.rows;
    if (keptOwnPayload)
        ++s.distinct;               // a first sighting, or too long to probe

    if (static_cast<unsigned>(InternSampleRows) <= s.rows)
    {
        // Below InternDistinctPercent distinct the column repeats enough for probing to pay off;
        // at or above it probing is switched off for the fetch. Cross-multiplied to stay exact.
        const bool internable =
            (s.distinct * 100u) < (s.rows * static_cast<unsigned>(InternDistinctPercent));

        m_dict.BeginColumn(col, internable);
        s.decided = 1;
    }
}

ValueTable::CellOffset ValueTable::AdoptCell (int col, const Common::SqlArena& scratch,
                                              CellOffset stored)
{
    if (SqlArena::NullOffset == stored)
        return stored;              // a NULL: the caller redirects this onto the shared Null payload

    const SqlValue src(scratch.At(stored));
    const SqlValueHeader* header = src.Header();

    if (0 == header)
        return SqlArena::NullOffset;    // outside the scratch region: degrade, never copy a wild pointer

    if (col < 0)
    {
        // No ordinal to key a dictionary entry on, so copy without interning. Flags travel with
        // the payload, so one the cursor marked stays marked rather than taking this table's default.
        return SqlValue::Store(m_arena, src.Kind(), src.Body(), header->len, header->flags);
    }

    const unsigned c = static_cast<unsigned>(col);
    BeginSampling(c);

    // Hit or miss comes off the dictionary's own counter, not the offsets: the two name different
    // arenas, so equality between them would be coincidence. Read back to back on this thread.
    const unsigned hits = m_dict.Hits();
    // Flags travel with the value: evfTruncated was set by the cursor, and Intern() keys on the bit
    // so a truncated cell is never handed a complete one's payload. Intern() strips evfInterned.
    const CellOffset published = m_dict.Intern(m_arena, c, src.Body(), header->len, src.Kind(),
                                               header->flags);
    CountSample(c, m_dict.Hits() == hits);

    return published;
}

bool ValueTable::ColumnSampleDecided (int col) const
{
    if (col < 0 || static_cast<size_t>(col) >= m_samples.size())
        return false;               // a column never fetched is a column never decided

    return 0 != m_samples[col].decided;
}

bool ValueTable::ColumnInternable (int col) const
{
    if (col < 0)
        return false;

    // While the sample is still open the column IS being interned, optimistically, so this reports
    // what the dictionary will actually do next rather than only what a finished decision said.
    return m_dict.IsInternable(static_cast<unsigned>(col));
}

void ValueTable::ColumnSample (int col, unsigned& rows, unsigned& distinct) const
{
    rows = 0;
    distinct = 0;

    if (col < 0 || static_cast<size_t>(col) >= m_samples.size())
        return;

    rows     = m_samples[col].rows;
    distinct = m_samples[col].distinct;
}

// The image index travels with its row, so it is keyed by logical row like every other public
// accessor. Reading it physically would leave a sorted grid showing icons on the wrong rows.
int ValueTable::GetImageIndex (int row) const
{
    if (row < 0 || row >= GetRowCount())
        return -1;

    return m_images.at(ToPhysical(row));
}

void ValueTable::SetImageIndex (int row, int image)
{
    if (row < 0 || row >= GetRowCount())
        return;

    m_images.at(ToPhysical(row)) = image;
}

int ValueTable::ToPhysical (int row) const
{
    if (m_rowMap.empty())
        return row;                 // identity: no sort has been asked for

    if (row < 0 || static_cast<size_t>(row) >= m_rowMap.size())
        return row;                 // callers bounds-check first; this keeps a stray one inert

    return m_rowMap.at(row);
}

int ValueTable::ToLogical (int physicalRow) const
{
    if (m_rowMap.empty())
        return physicalRow;         // identity: the two numberings are the same one

    if (physicalRow < 0)
        return -1;

    for (size_t i = 0; i < m_rowMap.size(); ++i)
        if (m_rowMap[i] == physicalRow)
            return static_cast<int>(i);

    return -1;                      // not in the permutation: a row appended after the sort
}

void ValueTable::ClearSort ()
{
    m_rowMap.clear();
    m_sortColumn    = -1;
    m_sortAscending = true;
}

// ClearSort() minus the reorder: m_rowMap survives, so no row moves; only the column claim goes.
void ValueTable::MarkSortStale ()
{
    m_sortColumn    = -1;
    m_sortAscending = true;
}

// Permutes m_rowMap and nothing else: no payload moves and no offset is rewritten, so a sort costs
// one int vector and no value data at all.
bool ValueTable::Sort (int col, bool ascending)
{
    if (col < 0 || col >= m_columns)
        return false;               // nothing changed: a caller cannot be left half-sorted

    // GetRowCount() is the physical count and is independent of m_rowMap, so this reads the same
    // number whether or not a sort is active - which makes a second Sort() rebuild from identity.
    const int rows = GetRowCount();
    if (rows <= 0)
        return false;

    m_rowMap.resize(static_cast<size_t>(rows));
    for (int i = 0; i < rows; ++i)
        m_rowMap[static_cast<size_t>(i)] = i;

    // std::sort, not stable_sort: ordering ties by physical row restores the determinism a grid
    // needs at none of stable_sort's allocation. The tie-break stays ascending in both directions.
    const ValueTable* const self = this;
    std::sort(m_rowMap.begin(), m_rowMap.end(),
        [self, col, ascending] (int lhs, int rhs) -> bool
        {
            const int c = self->PhysicalValue(lhs, col).Compare(self->PhysicalValue(rhs, col));
            if (0 != c)
                return ascending ? (c < 0) : (c > 0);
            return lhs < rhs;
        });

    m_sortColumn    = col;
    m_sortAscending = ascending;
    return true;
}

// The one read that must not go through ToPhysical: Sort()'s comparator is permuting physical row
// numbers, so routing back through the map would compare rows against unassigned positions.
SqlValue ValueTable::PhysicalValue (int physicalRow, int col) const
{
    if (!InRange(physicalRow, col))
        return SqlValue(0);         // maps to kind Null, which Compare sorts first

    return SqlValue(m_arena.At(m_cells.at(CellIndex(physicalRow, col))));
}

size_t ValueTable::BytesAllocated () const
{
    // Committed, not reserved: the arena reserves 2 GB of address space on x64 and commits it a
    // megabyte at a time. Capacity, not size, for the vectors - that is the memory actually held.
    return m_arena.Committed()
         + m_cells.capacity()  * sizeof(CellOffset)
         + m_images.capacity() * sizeof(int)
         + m_rowMap.capacity() * sizeof(int);
}

// The four layout metrics, written to produce the same numbers StringTable produces: drawn row
// height and exported column width both derive from them, so a one-character drift is a regression.
void ValueTable::GetMaxColumnWidthByFirstLine (Common::nvector<size_t>& result, int from, int to) const
{
    result.clear();
    result.resize(m_columns, 0);

    const int rows = GetRowCount();

    // StringTable walked every row and tested from <= row && row <= to. Clamping the lower bound
    // and stopping at the row count selects the same rows; callers pass INT_MAX for "the lot".
    if (from < 0)
        from = 0;

    std::string text;
    for (int row = from; row <= to && row < rows; ++row)
    {
        for (int col = 0; col < m_columns; ++col)
        {
            GetText(row, col, text);

            size_t len = text.size();
            const size_t first_line_len = text.find_first_of("\n\r");

            if (std::string::npos != first_line_len && first_line_len < len)
                len = first_line_len;

            if (result.at(col) < len)
                result.at(col) = len;
        }
    }
}

// Asymmetry kept exactly as StringTable had it: despite the shared ByFirstLine name this measures
// the whole cell and never truncates at a newline. Unifying the two would re-width form exports.
void ValueTable::GetMaxRowWidthByFirstLine (Common::nvector<size_t>& result, int from, int to) const
{
    const int rows = GetRowCount();

    result.clear();
    result.resize(rows, 0);

    if (from < 0)
        from = 0;
    if (to >= m_columns)
        to = m_columns - 1;     // with no columns this leaves to at -1 and the loop empty

    std::string text;
    for (int col = from; col <= to; ++col)
    {
        for (int row = 0; row < rows; ++row)
        {
            GetText(row, col, text);

            const size_t len = text.size();
            if (result.at(row) < len)
                result.at(row) = len;
        }
    }
}

int ValueTable::GetMaxLinesInColumn (int col, int lim) const
{
    if (col < 0 || col >= m_columns)
        return 1;

    int nlines = 1;
    const int rows = GetRowCount();

    std::string text;
    for (int row = 0; row < rows; ++row)
    {
        GetText(row, col, text);

        const int nl = calc_lines(text, lim);
        if (nlines < nl)
            nlines = nl;
    }
    return nlines;
}

int ValueTable::GetMaxLinesInRow (int row, int lim) const
{
    if (row < 0 || row >= GetRowCount())
        return 1;

    int nlines = 1;

    std::string text;
    for (int col = 0; col < m_columns; ++col)
    {
        GetText(row, col, text);

        const int nl = calc_lines(text, lim);
        if (nlines < nl)
            nlines = nl;
    }
    return nlines;
}

}
