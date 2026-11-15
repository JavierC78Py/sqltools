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

// 16.12.2004 (Ken Clubok) Add CSV prefix option
// 2017-11-28 implemented range selection
// 2017-11-28 added 3 new export formats

#pragma once
#ifndef __GridSourceBase_h__
#define __GridSourceBase_h__

/***************************************************************************/
/*      Purpose: Text data source implementation for grid component        */
/*      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~           */
/*      (c) 1996-1999,2003 Aleksey Kochetov                                */
/***************************************************************************/

#include <set>
#include <Common/NVector.h>
#include "AGridSource.h"
#include "GridTextFormat.h"
#include "StringTable.h"
#include "ValueTable.h"


#define BASE_SOURCE_PROPERTY(type, name) \
    private: type m_##name; \
    public:  type Get##name () const { return m_##name; } \
    public:  void Set##name (type val) { m_##name = val; } 

#define BASE_SOURCE_STATIC_PROPERTY(type, name) \
    private: static type m_##name; \
    public:  static type Get##name () { return m_##name; } \
    public:  static void Set##name (type val) { m_##name = val; } 

#define BASE_SOURCE_COL_PROPERTY(type, name) \
    public: type GetCol##name (int col) const { return m_ColumnInfo.at(col).m_##name; } \
    public: void SetCol##name (int col, type val) { m_ColumnInfo.at(col).m_##name = val; } 


namespace OG2 /* OG = OpenGrid */
{
    enum EColumnAlignment  
    { 
        ecaLeft, 
        ecaRight 
    };
    
    // ETextFormat and IsBinaryExportFormat come from GridTextFormat.h, which needs no MFC.

    enum EOrientation
    {
        eoVert = 0,
        eoHorz = 1
    };

class GridSourceBase : public AGridSource
{
public:
    GridSourceBase ();
    ~GridSourceBase ();

    // AGridSource interface implementation
    virtual bool CanModify () const                                     { return false; }
    virtual void Clear ()                                               { m_ColumnInfo.clear(); }

    // Cell management
    virtual int GetFirstFixCount (eDirection) const;
    virtual int GetLastFixCount  (eDirection) const;

    virtual int GetFirstFixSize  (int pos, eDirection) const;
    virtual int GetLastFixSize   (int pos, eDirection) const;
    virtual int GetSize          (int pos, eDirection) const;

    virtual void PaintCell (DrawCellContexts&) const;
    virtual void CalculateCell (DrawCellContexts&, size_t maxTextLength) const;

    // Whether the value PaintCell is about to draw is one the fetch stopped short of, taken in
    // grid-space indices. A rendering question only: it must never reach GetCellStr, which exports.
    virtual bool IsCellTruncated (int /*row*/, int /*col*/) const           { return false; }

    // Whether the cell PaintCell is about to draw holds no value, so its marker can be dimmed.
    // Grid-space indices for the same reason, and a rendering question for the same reason: the
    // answer changes the colour the marker is drawn in and nothing about what GetCellStr returns.
    virtual bool IsCellNull (int /*row*/, int /*col*/) const                { return false; }

    virtual CWnd* BeginEdit  (int row, int col, CWnd*, const CRect&); // get editor for grid window
    virtual void  PostEdit   (); // event for commit edit result &
    virtual void  CancelEdit (); // end edit session

    // True while an open editor holds text differing from what it was loaded with.
    virtual bool  IsEditDirty () const;

    // Whether the cell may be written back. The base answers false, which is what keeps the editor
    // BeginEdit creates read-only for every source that does not say otherwise.
    virtual bool IsCellEditable (int /*row*/, int /*col*/) const            { return false; }

    // PostEdit hands the editor's text here, with BeginEdit's CRLF folding undone, and only for a
    // cell IsCellEditable accepted. The base has nowhere to write it to, so it drops it.
    virtual void CommitCellEdit (int /*row*/, int /*col*/, const std::string& /*utf8*/) {}

    // clipboard operation support
    virtual void DoEditCopy  (int, int, int, int, ECopyWhat, int);
    virtual void DoEditPaste ();
    virtual void DoEditCut   ();

    // drag & drop support
    virtual BOOL       DoDragDrop (int, int, int, int, ECopyWhat);
    virtual DROPEFFECT DoDragOver (COleDataObject*, DWORD dwKeyState);
    virtual BOOL       DoDrop     (COleDataObject*, DROPEFFECT);

    // tooltips support
    //virtual const char* GetTooltipString (TooltipCellContexts&) const;

    // GridSourceBase extended data source interfaces
    virtual void SetCols (int);

    BASE_SOURCE_PROPERTY(bool, ShowFixCol);
    BASE_SOURCE_PROPERTY(bool, ShowFixRow);
    BASE_SOURCE_PROPERTY(bool, ShowRowEnumeration);
    BASE_SOURCE_PROPERTY(bool, ShowTransparentFixCol);
    BASE_SOURCE_PROPERTY(int,  MaxShowLines);

    BASE_SOURCE_PROPERTY(EOrientation, Orientation);
    bool IsTableOrientation () const                                    { return m_Orientation == eoVert; }
    bool IsFormOrientation () const                                     { return m_Orientation == eoHorz; }
    
    BASE_SOURCE_COL_PROPERTY(string,           Header);      // Set/GetColHeader
    BASE_SOURCE_COL_PROPERTY(int,              CharWidth);   // Set/GetColCharWidth
    BASE_SOURCE_COL_PROPERTY(int,              CharHeight);  // Set/GetColCharHeight
    BASE_SOURCE_COL_PROPERTY(EColumnAlignment, Alignment);   // Set/GetColAlignment

    // Oracle type class of the column, as Common::SqlValueKind. The define worker captures it and
    // UI-thread paths read it back, so alignment and export stop asking a live cursor.
    BASE_SOURCE_COL_PROPERTY(Common::SqlValueKind, Kind);    // Set/GetColKind

    // First fix column image support
    virtual int GetImageIndex  (int row) const = 0;
    virtual void SetImageIndex (int row, int image) = 0;
    void SetImageList (CImageList* imageList, BOOL deleteImageList);

    int  GetFixSize (EOrientation rot, eDirection dir) const            { return m_FixSize[rot][dir]; }
    void SetFixSize (EOrientation rot, eDirection dir, int size)        { m_FixSize[rot][dir] = size; }
    int  GetColumnCount () const                                        { return m_ColumnInfo.size(); }
    virtual bool IsTextField (int /*col*/) const                        { return false;}

    // Sort indicator for the column header. The base keeps no rows, so it reports "not sorted" and
    // PaintCell draws no glyph; ValueGridSource overrides both from the table's row map.
    virtual int  GetSortColumn    () const                              { return -1;   }
    virtual bool GetSortAscending () const                              { return true; }

protected:
    void DrawDecFrame (DrawCellContexts&) const;

    // Sort glyph, drawn into the right-hand end of a column header cell. PaintCell and
    // CalculateCell both consult SortGlyphReserve() and must agree, or auto-fit comes out too narrow.
    bool HeaderCarriesSortGlyph (const DrawCellContexts&) const;
    void DrawSortGlyph (DrawCellContexts&) const;
    int  SortGlyphReserve (const DrawCellContexts&) const;
    virtual void CopyColumnNames (std::string&, int, int) const;
    virtual int CopyToString (std::string&, int, int, int, int, ECopyWhat, int); // return actual format
    virtual void GetCellStr (std::string&, int row, int col) const = 0;
    void GetCellStr (std::wstring& buff, int row, int col) const
    {
        std::string utf8buff;
        GetCellStr(utf8buff, row, col);
        buff = Common::wstr(utf8buff);
    }

    struct ColumnInfo
    {
        string               m_Header;
        int                  m_CharWidth;
        int                  m_CharHeight;
        EColumnAlignment     m_Alignment;
        Common::SqlValueKind m_Kind;

        // Text is the neutral default and pairs with ecaLeft: an unclassified column is left-aligned
        // and quoted on export. The define worker overwrites m_Kind for every column it classifies.
        ColumnInfo () : m_CharHeight(1), m_CharWidth(10), m_Alignment(ecaLeft),
                        m_Kind(Common::SqlValueKind::Text) {}
    };

    Common::nvector<ColumnInfo> m_ColumnInfo;

    CEdit* m_Editor;
    int m_editRow, m_editCol;   // the cell m_Editor was opened on, in the grid space BeginEdit takes
    std::string m_editInitial;  // the text BeginEdit loaded into it, CRLF-folded, as the changed test
    CImageList* m_ImageList;
    BOOL m_bDeleteImageList;
    
    int m_FixSize[2][2]; // the first index is Orientation, the second is Dir

    friend class GridStringOleSource;
};

// The text-export half of a grid source, hoisted out of GridStringSource so an arena-backed source
// can reuse all ten Streamer formats without inheriting StringTable. Export settings stay static.
class IGridTextSource : public GridSourceBase
{
public:
    virtual int  ExportText (std::ostream&, int row = -1, int nrows = -1, int col = -1, int ncols = -1, int format = -1) const;

    // The base implementation is literally GetCellStr - no truncation and no escaping - so what a
    // Streamer compares against is exactly the display text the storage produced.
    virtual void GetCellStrForExport (std::string&, int line, int col, int subformat = 0) const;

    virtual bool CanExportSqlLiterals () const                            { return false; }

    // Typed payload access, for formats that must know a value's kind. False when the source holds
    // text only (GridStringSource among them); the handle borrows arena memory for the call only.
    virtual bool GetCellSqlValue (Common::SqlValue&, int row, int col) const { return false; }

    // Public here exactly as it already was in GridStringSource: GridSourceBase declares this
    // overload protected, and the host windows call it through the source pointer.
    virtual void GetCellStr (std::string& str, int row, int col) const = 0;

    // Storage-specific, hence pure: StringTable scans its std::strings, ValueTable scans arena
    // payloads, and neither can do the other's job.
    virtual void GetMaxColumnWidthByFirstLine (Common::nvector<size_t>&, int from, int to) const = 0;

    // Pure for the same reason: StringTable::ReserveMore is a no-op on the ndeque it defaults to,
    // while ValueTable::ReserveMore really reserves.
    virtual void ReserveMore (int nrows) = 0;

    // Data saving settings
    BASE_SOURCE_STATIC_PROPERTY(ETextFormat, OutputFormat);
    BASE_SOURCE_STATIC_PROPERTY(char, FieldDelimiterChar);
    BASE_SOURCE_STATIC_PROPERTY(char, QuoteChar);
    BASE_SOURCE_STATIC_PROPERTY(char, QuoteEscapeChar);
    BASE_SOURCE_STATIC_PROPERTY(char, PrefixChar);
    BASE_SOURCE_STATIC_PROPERTY(bool, OutputWithHeader);
    // Pretty-printed JSON by default. JsonWriter's indent < 0 is the compact spelling.
    BASE_SOURCE_STATIC_PROPERTY(bool, JsonPretty);
    BASE_SOURCE_STATIC_PROPERTY(string, XmlRootElement   );
    BASE_SOURCE_STATIC_PROPERTY(string, XmlRecordElement );
    BASE_SOURCE_STATIC_PROPERTY(int,    XmlFieldElementCase);

    // The worksheet name for the next XLSX export: one export's own file name rather than a setting,
    // so it is per source and setExportSettings() clears it ahead of every export. Empty means Sheet1.
    const std::wstring& GetXlsxSheetName () const                        { return m_xlsxSheetName; }
    void                SetXlsxSheetName (const std::wstring& name)      { m_xlsxSheetName = name; }

private:
    std::wstring m_xlsxSheetName;
};

// Counts the cells an export wrote from a truncated payload, honouring the -1 "all" sentinel. Takes
// AGridSource and narrows to GridSourceBase: a source with no payloads counts as zero.
int countTruncatedCells (const AGridSource* source, int startRow, int rowCount, int startCol, int colCount);

class GridStringSource : public IGridTextSource
{
public:
    GridStringSource ()                                                 { Clear(); }
    // AGridSource interface implementation
    virtual void Clear ();
    virtual int  Append ();
    virtual int  Insert (int row);
    virtual void Delete (int row);
    virtual void DeleteAll ();
    
    virtual int GetCount (eDirection dir) const;
    
    virtual int GetSize (int pos, eDirection) const;

    // GridStringSource extended data source interfaces
    // Data manipulation
    virtual void ReserveMore (int nrows)                                { m_table.ReserveMore(nrows); }

    const string& GetString (int row, int col) const                    { return m_table.GetString(row, col); }
    string&       GetString (int row, int col)                          { return m_table.GetString(row, col); }
    void          SetString (int row, int col, const string& str)       { m_table.SetString(row, col, str); }

    // GridSourceBase interface implementation
    virtual void SetImageIndex (int row, int image)                     { m_table.SetImageIndex(row, image); }
    virtual int  GetImageIndex (int row) const                          { return m_table.GetImageIndex(row); }
    virtual void SetCols (int cols);
    virtual void GetCellStr (std::string& str, int row, int col) const  { if (IsTableOrientation()) str = GetString(row, col); else str = GetString(col, row); }

    virtual void GetMaxColumnWidthByFirstLine (Common::nvector<size_t>&, int from, int to) const;

private:
    StringTable m_table;
};

// ValueTable-backed sibling of GridStringSource: orientation-aware forwarding that differs only in
// the storage member and in how a cell is filled, so the two must be kept in step.
class ValueGridSource : public IGridTextSource
{
public:
    ValueGridSource ()                                                  { Clear(); }

    // AGridSource interface implementation
    virtual void Clear ();
    virtual int  Append ();
    virtual int  Insert (int row);
    virtual void Delete (int row);
    virtual void DeleteAll ();

    virtual int GetCount (eDirection dir) const;

    virtual int GetSize (int pos, eDirection) const;

    // ValueGridSource extended data source interfaces
    // Data manipulation
    virtual void ReserveMore (int nrows)                                { m_table.ReserveMore(nrows); }

    // The fetch-side fill: adopt the payload the worker built in its scratch arena, park the offset
    // that returns, then park the display text. SetText returning false is how a full arena reports.
    void SetCell (int row, int col, ValueTable::CellOffset off)         { m_table.SetCell(row, col, off); }
    bool SetText (int row, int col, const std::string& text)            { return m_table.SetText(row, col, text); }
    void GetText (int row, int col, std::string& out) const             { m_table.GetText(row, col, out); }
    bool IsNullCell (int row, int col) const                            { return m_table.IsNullCell(row, col); }

    // The marker the grid draws for a NULL cell, parked once on the shared Null payload so GetText()
    // returns it and the layout metrics measure it. m_nullText stays as the fallback for a full arena.
    const std::string& GetNullText () const                             { return m_nullText; }
    void               SetNullText (const std::string& text);

    // GridSourceBase interface implementation
    virtual void SetImageIndex (int row, int image)                     { m_table.SetImageIndex(row, image); }
    virtual int  GetImageIndex (int row) const                          { return m_table.GetImageIndex(row); }
    virtual void SetCols (int cols);
    virtual void GetCellStr (std::string& str, int row, int col) const;

    // GridSourceBase's rendering hook, overridden because this source owns payloads carrying the
    // flag. Takes the grid-space pair PaintCell holds, so the orientation swap belongs here.
    virtual bool IsCellTruncated (int row, int col) const;

    // IsCellTruncated's sibling: the same swap, and the NULL test the table already applies to
    // logical rows - so a sorted grid asks about the row it is drawing, not the slot it came from.
    virtual bool IsCellNull (int row, int col) const;

    // The typed seam IGridTextSource declares, answered from m_table.GetValue(). Grid-space indices:
    // the same orientation swap GetCellStr() performs, because the table is always rows-as-records.
    virtual bool GetCellSqlValue (Common::SqlValue& out, int row, int col) const;

    virtual void GetMaxColumnWidthByFirstLine (Common::nvector<size_t>&, int from, int to) const;

    // Exposed because the OCI subclass needs the value-level API - SetCell by arena offset, and
    // GetValue for exact type and NULL tests - which the text-level surface above does not cover.
          ValueTable& Table ()                                          { return m_table; }
    const ValueTable& Table () const                                    { return m_table; }

    // Sorting. Permutes the table's rows by one data column and then tells every attached grid that
    // the row order changed. Returns false, having changed nothing, when it cannot be honoured.
    bool SortByColumn (int col, bool ascending);

    // Inverse of SortByColumn: drops the row-map permutation, restoring fetch order. Reads no
    // payloads, so unlike SortByColumn it needs neither CanSort() nor PrepareSort(). False if unsorted.
    bool ClearSort ();

    // As ClearSort() but the row order survives: the permutation stays, the column claim goes.
    bool MarkSortStale ();

    // Veto hook, modelled on AGridSource::CanModify() rather than folded into SortByColumn so a
    // toolbar or menu can grey the command out for the same reason the sort would be refused.
    virtual bool CanSort () const                                       { return true; }

    // Brings the source to a state where SortByColumn will not refuse it, ahead of the sort being
    // run; the base has nothing left to fetch, so it does nothing. May throw on a cancelled wait.
    virtual void PrepareSort ()                                         {}

    // Sort state, mirrored from the table. GetSortColumn()/GetSortAscending() override the base pair
    // PaintCell reads. GetSortColumn() is -1 when unsorted and when MarkSortStale() kept the map.
    bool         IsSorted         () const                              { return m_table.IsSorted();         }
    virtual int  GetSortColumn    () const                              { return m_table.GetSortColumn();    }
    virtual bool GetSortAscending () const                              { return m_table.GetSortAscending(); }

private:
    ValueGridSource (const ValueGridSource&);              // owns an arena: not copyable
    ValueGridSource& operator = (const ValueGridSource&);

    // Re-parks m_nullText after anything that dropped the table's payloads. SetCols() and Clear() both
    // reset the arena, and with it the shared Null payload the marker is parked on.
    void ApplyNullText ();

    ValueTable  m_table;
    std::string m_nullText;
};

}//namespace OG2

#endif//__GridSourceBase_h__
