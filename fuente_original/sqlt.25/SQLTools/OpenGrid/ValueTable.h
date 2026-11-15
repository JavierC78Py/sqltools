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
#ifndef __ValueTable_h__
#define __ValueTable_h__

#include <Common/SqlArena.h>
#include <Common/SqlValue.h>
#include <Common/SqlFormat.h>
#include <Common/ValueDict.h>
#include <Common/NVector.h>
#include <cstddef>
#include <string>
#include <vector>

namespace OG2 /* OG = OpenGrid */
{

// Arena-backed cell storage: a cell is a 32-bit offset into one contiguous arena rather than a
// std::string, and a NULL cell points at one shared payload whose kind is SqlValueKind::Null.
class ValueTable
{
public:
    typedef Common::SqlArena::CellOffset CellOffset;

    ValueTable ();
    ~ValueTable ();

    // Column count is fixed for the life of a result set. nrows < 0 leaves the row count alone,
    // otherwise that many rows are preallocated. Reserves the arena, without which stores fail.
    void Format (int ncols, int nrows = -1);

    // Address space to reserve up front, and so the ceiling on total payload bytes. 2 GB on x64;
    // 256 MB on Win32, where a larger reservation would fail and drag the malloc fallback with it.
    static size_t DefaultReserve ();

    // Override the default before the first Format(). Returns false if neither the reservation nor
    // the malloc fallback could be had, in which case every cell reads back NULL.
    bool Reserve (size_t bytes);

    // Discards every payload but keeps the arena reservation for the next fetch, which is what
    // makes a refetch cheap. Column count survives, as StringTable::Clear() did.
    void Clear ();

    int  Append ();                 // returns the new row's index

    // Logical rows, like every accessor above; each translates before dropping the sort.
    void Insert (int row);

    // Inert, not undefined, for a row outside the table: a grid that has just been refetched can
    // legitimately be asked to delete a row that no longer exists.
    void Delete (int row);
    void DeleteAll ();

    // Reserves cells for nrows more rows.
    void ReserveMore (int nrows);

    int  GetRowCount () const       { return static_cast<int>(m_images.size()); }
    int  GetColumnCount () const    { return m_columns; }
    bool IsEmpty () const           { return !m_columns && !GetRowCount(); }

    // The raw cell offset, and no longer a NULL test: a NULL cell holds the shared payload's
    // non-zero offset. NullOffset here means "no payload at all". Use IsNullCell().
    CellOffset At (int row, int col) const;
    void       SetCell (int row, int col, CellOffset off);

    // The shared Null payload every NULL cell points at, or NullOffset until SetNullText() creates
    // it. Redirecting a cursor's NullOffset onto this is what makes a NULL both render and measure.
    CellOffset NullPayload () const { return m_nullPayload; }

    // True for a cell holding no payload at all and for one holding the shared Null payload. The
    // test is kind-aware, so an empty VARCHAR2 - a Text payload of length 0 - never reads as NULL.
    bool IsNullCell (int row, int col) const    { return GetValue(row, col).IsNull(); }

    // Did the fetch stop at the column's length limit rather than at the end of the value? How much
    // is missing is not recoverable here: the payload is already clamped by the time it exists.
    bool IsTruncatedCell (int row, int col) const { return GetValue(row, col).IsTruncated(); }

    // A non-owning handle onto the cell's payload; IsNull() when the cell is NULL.
    Common::SqlValue GetValue (int row, int col) const;

    // The rendered display text parked on the payload's textOffset: the shared marker for a NULL,
    // empty when nothing has been parked. Filled by the fetch worker unless rendering is lazy.
    void GetText (int row, int col, std::string& out) const;

    // Parks UTF-8 display text and returns false, changing nothing, when the cell is NULL or the
    // arena is exhausted. The NULL refusal is load-bearing: every NULL cell shares one payload.
    bool SetText (int row, int col, const char* utf8, size_t len);
    bool SetText (int row, int col, const std::string& utf8)  { return SetText(row, col, utf8.data(), utf8.size()); }

    // Replaces the cell's payload with a fresh Text one carrying `utf8`, which SetText cannot do: it
    // only parks text on a payload that has none yet, so it is a no-op on a cell already displaying.
    bool OverwriteText (int row, int col, const char* utf8, size_t len);
    bool OverwriteText (int row, int col, const std::string& utf8) { return OverwriteText(row, col, utf8.data(), utf8.size()); }

    // Parks the marker every NULL cell renders, on one shared Null payload this call creates. Call
    // it after Format() and before appending rows, or earlier cells keep offset 0 and measure 0.
    bool SetNullText (const std::string& utf8);

    // The format context lazy rendering reads, by reference so the fetch layer can push every
    // option without a setter each. Defaults render canonical text plus the empty NULL marker.
    Common::SqlFormat&       Format ()       { return m_format; }
    const Common::SqlFormat& Format () const { return m_format; }

    // Lazy rendering's on/off. Off, the fetch worker renders eagerly and GetText only reads the
    // parked textOffset; on, GetText renders an unrendered value through Format().
    void SetLazyRender (bool on) { m_lazyRender = on; }
    bool LazyRender () const      { return m_lazyRender; }

    // Layout metrics, answering the same four questions StringTable answers and with the same
    // numbers, or row heights and exported column widths shift. Out-of-range is inert, not thrown.
    void GetMaxColumnWidthByFirstLine (Common::nvector<size_t>& result, int from, int to) const;
    void GetMaxRowWidthByFirstLine    (Common::nvector<size_t>& result, int from, int to) const;

    int  GetMaxLinesInColumn (int col, int lim) const;
    int  GetMaxLinesInRow    (int row, int lim) const;



    // Per-row image index, as StringRow::m_ImageIndex was. -1 means none.
    int  GetImageIndex (int row) const;
    void SetImageIndex (int row, int image);

    // Row map: identity while m_rowMap is empty, so an unsorted result pays for no indirection. A
    // logical row is what every caller sees; a physical row is a position in m_cells.
    int  ToPhysical (int row) const;

    // Sort() permutes m_rowMap and nothing else, so a sort moves ints and no value data. Only legal
    // once every row is fetched; Append() extends the permutation and Insert()/Delete() drop it.
    bool Sort (int col, bool ascending);
    void ClearSort ();

    // Keeps m_rowMap and withdraws the column claim, so rows stay put and the glyph goes.
    void MarkSortStale ();

    bool IsSorted         () const      { return !m_rowMap.empty(); }
    int  GetSortColumn    () const      { return m_sortColumn;    }
    bool GetSortAscending () const      { return m_sortAscending; }

    // Inverse of ToPhysical, and O(n) - for remapping a selection anchor or the focused cell after
    // a sort, never for per-cell work. Identity while unsorted; -1 for a row the map lacks.
    int  ToLogical (int physicalRow) const;

    // Exact, not the sizeof(std::string) heuristic OciSource.cpp:282 uses today: committed arena
    // bytes plus what the three index vectors actually hold.
    size_t BytesAllocated () const;

    // Arena() is the raw region payloads are stored into, and it belongs to the UI thread: a fetch
    // worker has a scratch arena of its own and hands payloads over through AdoptCell().
    Common::SqlArena&       Arena ()       { return m_arena; }
    const Common::SqlArena& Arena () const { return m_arena; }

    // Per-column deduplication driven from the fetch loop: returns the offset to publish, `stored`
    // on a first sighting and an existing payload's offset on a hit. NullOffset passes through.
    CellOffset InternCell (int col, CellOffset stored, size_t mark);

    // Copies a payload from a fetch worker's scratch arena into this one on a dictionary miss, or
    // returns the payload already here on a hit. UI thread only, so this arena has one writer.
    CellOffset AdoptCell (int col, const Common::SqlArena& scratch, CellOffset stored);

    // The dictionary itself, for reporting. Its lifetime is this table's: it is dropped by Clear()
    // along with the payloads, because every entry is an offset into them.
    const Common::ValueDict& Dict () const { return m_dict; }

    // The per-column decision, so a measurement can say why a column was or was not interned.
    // Undecided while the sample is taken, during which the column is interned optimistically.
    bool ColumnSampleDecided (int col) const;
    bool ColumnInternable    (int col) const;
    void ColumnSample (int col, unsigned& rows, unsigned& distinct) const;

    // How many rows a column is sampled over, and the distinct ratio below which interning pays.
    // Public because a caller has to drive exactly this many cells to see a decision get made.
    enum { InternSampleRows = 100000, InternDistinctPercent = 10 };

private:
    ValueTable (const ValueTable&);               // owns an arena: not copyable
    ValueTable& operator = (const ValueTable&);

    bool   InRange (int row, int col) const;
    size_t CellIndex (int row, int col) const     { return static_cast<size_t>(row) * m_columns + col; }

    // Value read by physical row, bypassing ToPhysical. Only Sort()'s comparator may use this, as
    // it is writing the map; every public read maps, through At().
    Common::SqlValue PhysicalValue (int physicalRow, int col) const;

    // Creates the shared Null payload on first need; false when the arena cannot hold it.
    // Idempotent within one result set. Clear() drops it, because Reset() invalidates every offset.
    bool   EnsureNullPayload ();

    // One column's share of the interning decision. Rows counts the cells sampled, NULLs excluded,
    // and distinct those that kept a payload of their own. decided is sticky, never revisited.
    struct InternSample
    {
        unsigned      rows;
        unsigned      distinct;
        unsigned char decided;
        unsigned char started;      // the optimistic BeginColumn(col, true) has been made

        InternSample () : rows(0), distinct(0), decided(0), started(0) {}
    };

    InternSample& Sample (unsigned col);        // grows m_samples to cover col

    // The per-column policy, factored out so InternCell() and AdoptCell() cannot disagree about it.
    // Both doors bracket their one dictionary call with this pair.
    void BeginSampling (unsigned col);

    // `keptOwnPayload` is the distinctness signal: a first sighting, or a value too long to probe,
    // keeps its own payload. Passed in, as the two doors cannot derive it the same way.
    void CountSample (unsigned col, bool keptOwnPayload);

    Common::SqlArena            m_arena;    // owns every payload (D7)
    Common::nvector<CellOffset> m_cells;    // row-major, contiguous, reserve()-able (D4)
    Common::nvector<int>        m_images;   // one per row
    Common::nvector<int>        m_rowMap;   // empty == identity; Sort() fills it
    CellOffset                  m_nullPayload;  // shared by every NULL cell; NullOffset == none
    int                         m_columns;

    // What m_rowMap currently means, so the column header can draw its glyph after a repaint and a
    // second click can reverse the direction. -1 when m_rowMap is empty or MarkSortStale() ran.
    int                         m_sortColumn;
    bool                        m_sortAscending;
    Common::SqlFormat           m_format;       // Stage 5 (B10): lazy render's format context
    bool                        m_lazyRender;   // Stage 5 (B10): false == eager Stage 3a path

    // Owned here, not by a fetch worker: a fetch runs in portions, so a task-local dictionary would
    // only deduplicate within its own rows, and its entries are offsets into the arena Clear() resets.
    Common::ValueDict           m_dict;
    std::vector<InternSample>   m_samples;
};

}

#endif // __ValueTable_h__
