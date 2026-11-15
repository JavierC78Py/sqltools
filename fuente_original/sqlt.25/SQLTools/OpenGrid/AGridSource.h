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
#ifndef __AGridSource_h__
#define __AGridSource_h__

/***************************************************************************/
/*      Purpose: Superbase data source class for grid component            */
/*      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~            */
/*      (c) 1996-1999,2003 Aleksey Kochetov                                */
/***************************************************************************/

#include <ostream>
#include <COMMON/NVector.h>
#include "AGridManager.h"

namespace OG2 /* OpenGrid v2 */
{
    enum ESelect   { esNone, esBorder, esRect };
    enum EFixType  { efNone, efFirst, efLast };
    enum ECopyWhat { 
        ecFieldNames,
        ecDragTopLeftCorner,
        ecColumn, 
        ecColumnHeader,
        ecDragColumnHeader,
        ecRow, 
        ecRowHeader,
        ecDragRowHeader,
        ecDataCell, 
        ecEverything,
    };

    struct DrawCellContexts
    {
        int      m_Col, m_Row;
        CRect    m_Rect;
        CSize    m_CellMargin;
        CDC*     m_Dc;
        COLORREF m_TextColor, m_BkColor[2],
                 m_FixTextColor, m_FixBkColor, m_FixShdColor,
                 m_LightColor, m_UnusedColor,
                 m_HighlightColor, m_HighlightTextColor,
                 // Data text whose value the fetch cut short. Not a selection or state colour but a
                 // second text colour for the same cell, so PaintCell can swap to it and back.
                 m_TruncatedTextColor,
                 // A NULL cell's marker text, dimmed so an absent value reads differently from an
                 // empty one. The same swap-and-back, and the same kind of second text colour.
                 m_NullTextColor;
        CBrush   m_BkBrush[2], m_FixBrush, m_HighlightBrush;
        CPen     m_HighlightPen;
        ESelect  m_Select;
        EFixType m_Type[2];
        CRect    m_PaintRect;
        int      m_CharSize[2];

        bool IsDataCell  () const { return (m_Type[0] == efNone && m_Type[1] == efNone); }
        bool IsFixCell   () const { return (m_Type[0] != efNone || m_Type[1] != efNone); }
        bool IsCornerCell() const { return (m_Type[0] != efNone && m_Type[1] != efNone); }
    };

    //struct TooltipCellContexts
    //{
    //    int      m_Col, m_Row;
    //    CRect    m_Rect;
    //    CPoint   m_MousePos;
    //    EFixType m_Type[2];
    //    bool     m_CellRecall;

    //    bool IsDataCell  () const { return (m_Type[0] == efNone && m_Type[1] == efNone); }
    //    bool IsFixCell   () const { return (m_Type[0] != efNone || m_Type[1] != efNone); }
    //    bool IsCornerCell() const { return (m_Type[0] != efNone && m_Type[1] != efNone); }
    //};

class AGridSource
{
public:
    AGridSource ();
    virtual ~AGridSource ()                          {}

    virtual void EvAttach (AGridManager*);
    virtual void EvDetach (AGridManager*);

    bool IsEmpty () const                               { return m_empty;  }
    virtual void SetEmpty (bool empty)                  { m_empty = empty; }
    virtual bool CanModify () const = 0;

    // Row DELETE addressed by ROWID, queued like a cell edit. Both default to "no database here",
    // so every non-Oracle grid stays read-only without having to say so.
    virtual bool CanDeleteRow () const                          { return false; }
    virtual void CommitRowDelete (int /*row*/)                  {}

    // Row INSERT staged in the grid rather than sent per keystroke. Same defaults throughout, so a
    // grid with no database behind it never stages a row it could not post.
    virtual bool CanInsertRow () const                          { return false; }
    virtual bool HasPendingRow () const                         { return false; }

    // Stages a row and answers its index, or -1. copyFrom seeds it from that logical row's values;
    // -1 stages a blank one.
    virtual int  BeginPendingRow (int /*copyFrom*/)             { return -1; }
    virtual void CommitRowInsert ()                             {}
    virtual void DiscardPendingRow ()                           {}

    // True while the open editor sits on the staged row, where a cell edit is local until it posts.
    virtual bool IsEditingPendingRow () const                   { return false; }

    // True while row, taken in the ruler's own grid space, is the one staged for INSERT.
    virtual bool IsPendingRow (int /*row*/) const               { return false; }

    // implementation of the following methods MUST call notification methods
    virtual void Clear ()           = 0;
    // the follow methods operate with rows only
    virtual int  Append ()          = 0;
    virtual int  Insert (int row)   = 0;
    virtual void Delete (int row)   = 0;
    virtual void DeleteAll ()       = 0;

    // Cell management
    virtual int GetCount         (eDirection) const = 0;
    virtual int GetFirstFixCount (eDirection) const = 0;
    virtual int GetLastFixCount  (eDirection) const = 0;

    virtual int GetFirstFixSize  (int pos, eDirection) const = 0;
    virtual int GetLastFixSize   (int pos, eDirection) const = 0;
    virtual int GetSize          (int pos, eDirection) const = 0;

    virtual void PaintCell (DrawCellContexts&) const = 0;
    virtual void CalculateCell (DrawCellContexts&, size_t maxTextLength) const = 0;

    virtual CWnd* BeginEdit  (int, int, CWnd*, const CRect&) = 0; // get editor for grid window
    virtual void  PostEdit   () = 0;  // event for commit edit result &
    virtual void  CancelEdit () = 0;  // end edit session

    // True while an open editor holds text differing from what it was loaded with.
    virtual bool  IsEditDirty () const                        { return false; }

    // clipboard operation support
    virtual void DoEditCopy  (int, int, int, int, ECopyWhat, int format) = 0;
    virtual void DoEditPaste () = 0;
    virtual void DoEditCut   () = 0;

    // drag & drop support
    virtual BOOL       DoDragDrop (int, int, int, int, ECopyWhat) = 0;
    virtual DROPEFFECT DoDragOver (COleDataObject*, DWORD dwKeyState) = 0;
    virtual BOOL       DoDrop     (COleDataObject*, DROPEFFECT)       = 0;

    virtual int ExportText (std::ostream&, int row = -1, int nrows = -1, int col = -1, int ncols = -1, int format = -1) const = 0; // returns actual format

    // tooltips support
    //virtual const char* GetTooltipString (TooltipCellContexts&) const = 0;

protected:
    // Notification for attached grid managers. The first pair take an INCLUSIVE range, the second a
    // row and a COUNT - backwards asserts in PaintGridManager::OnChangedRows, in Release skips it.
    void Notify_ChangedRows (int startRow, int endRow);
    void Notify_ChangedCols (int startCol, int endCol);
    void Notify_ChangedRowsQuantity (int row, int quantity);
    void Notify_ChangedColsQuantity (int col, int quantity);
    // Fired after a sort permuted the rows. Row count is unchanged; see
    // AGridManager::OnRowsReordered for why that is not the same as OnChangedRowsQuantity.
    void Notify_RowsReordered ();

    class NotificationDisabler
    {
    public:
        NotificationDisabler (AGridSource*);
        ~NotificationDisabler ()                { Enable(); }
        void Enable ();
    private:
        AGridSource* m_pOwner;
    };

protected:
    bool m_empty;
    bool m_disableNotifications;
    Common::nvector<AGridManager*> m_GridManagers;
};

}//namespace OG2

#endif//__AGridSource_h__

