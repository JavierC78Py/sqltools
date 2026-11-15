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
#ifndef __GridManager_h__
#define __GridManager_h__

/***************************************************************************/
/*      Purpose: Grid manager implementation for grid component            */
/*      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~            */
/*      (c) 1996-1999,2003 Aleksey Kochetov                                */
/***************************************************************************/

#include <map>
#include <COMMON/NVector.h>
#include "AGridManager.h"
#include "AGridSource.h"

namespace OG2 /* OpenGrid v2 */
{
    struct CalcGridManager;

#define DECLARE_GRID_RULER_PROPERTY(type, name) \
    protected: type m_##name; \
    public: type Get##name () const    { return m_##name; } \
    public: void Set##name (type name) { m_##name = name; }

// Changing answers false to refuse, and Set/Inc/Dec pass that on, so a move can be vetoed.
#define DECLARE_STRICT_GRID_RULER_PROPERTY(type, name) \
    private: type m_##name; \
    public: type Get##name () const    { return m_##name; } \
    public: bool Set##name (type name) { if (m_##name != name) { if (!On##name##Changing()) return false; type old = m_##name; m_##name = name; On##name##Changed(old); } return true; } \
    protected: void set##name##Silently (type name) { m_##name = name; } \
    public: bool Inc##name ()          { if (!On##name##Changing()) return false; type old = m_##name++; On##name##Changed(old); return true; } \
    public: bool Dec##name ()          { if (!On##name##Changing()) return false; type old = m_##name--; On##name##Changed(old); return true; } \
    public: virtual bool On##name##Changing () = 0; \
    public: virtual void On##name##Changed (type) = 0;

    typedef Common::nvector<int> IntVector;

    class GridRulerBase
    {
        DECLARE_STRICT_GRID_RULER_PROPERTY(int, Current);
        DECLARE_STRICT_GRID_RULER_PROPERTY(int, Topmost);
        DECLARE_GRID_RULER_PROPERTY(int, SavedTopmost);
        DECLARE_GRID_RULER_PROPERTY(int, CellMargin);
        DECLARE_GRID_RULER_PROPERTY(int, Count);
        DECLARE_GRID_RULER_PROPERTY(int, FirstFixCount);
        DECLARE_GRID_RULER_PROPERTY(int, NoFixCount);
        DECLARE_GRID_RULER_PROPERTY(int, UnusedCount);
        DECLARE_GRID_RULER_PROPERTY(int, LastFixCount);
        DECLARE_GRID_RULER_PROPERTY(int, FirstFixSize);
        DECLARE_GRID_RULER_PROPERTY(int, LastFixSize);
        DECLARE_GRID_RULER_PROPERTY(int, LineSize);
        DECLARE_GRID_RULER_PROPERTY(int, CharSize);
        DECLARE_GRID_RULER_PROPERTY(int, ClientSize);
        DECLARE_GRID_RULER_PROPERTY(int, ScrollSize);
        DECLARE_GRID_RULER_PROPERTY(int, SizeAsOne);

        DECLARE_GRID_RULER_PROPERTY(int, FirstSelected);
        DECLARE_GRID_RULER_PROPERTY(int, LastSelected);

        DECLARE_GRID_RULER_PROPERTY(eDirection, Dir);
        DECLARE_GRID_RULER_PROPERTY(HWND, Scroller);
        DECLARE_GRID_RULER_PROPERTY(CalcGridManager*, Manager);

    public:
        GridRulerBase ()            { memset(this, 0, sizeof(*this)); m_SizeAsOne = -1; ResetSelection(); }
        virtual void Clear ()       { memset(&m_CellMargin, 0, (const char*)&m_Dir - (const char*)&m_CellMargin); m_SizeAsOne = -1; ResetSelection(); }
        virtual void Reset ()       { m_Current = m_Topmost = m_SavedTopmost = 0; ResetSelection(); }

        bool IsSelectionEmpty () const  { return ((m_FirstSelected == -1) && (m_LastSelected == -1)); }
        void ResetSelection ()          { m_FirstSelected =  -1; m_LastSelected = -1; }
        
        bool GetNormalizedSelection (int& first, int& last) const
        {
            if (!IsSelectionEmpty())
            {
                if (m_FirstSelected < m_LastSelected)
                {
                    first = m_FirstSelected;
                    last  = m_LastSelected;
                }
                else
                {
                    first = m_LastSelected;
                    last  = m_FirstSelected;
                }
                if (first == -1) first = last;
                if (last == -1)  last  = first;

                return true;
            }
            first = last = -1;

            return false;
        }
        friend void rotate (GridRulerBase& left, GridRulerBase& right);
   };

    class GridRuler : public GridRulerBase
    {
    protected:
        static const int CACHE_ITEMS_CAP = 1024;
        IntVector m_Items;
        std::map<int, int> m_resizedItems;
        mutable IntVector m_cacheFirstFixItems;
        mutable IntVector m_cacheLastFixItems;
    public:
        AGridSource* GetSource ();
        const AGridSource* GetSource () const;
        int GetSourceCount () const;

        GridRuler ();
        virtual void Clear ();
        virtual void Reset ()   { ClearCacheItems(); GridRulerBase::Reset(); }

        virtual bool OnCurrentChanging ();
        virtual void OnCurrentChanged  (int);
        virtual bool OnTopmostChanging ();
        virtual void OnTopmostChanged  (int);

        void calcRuler   (int cx);
        void calcScroller ();
        void adjustTopmostByPos (int pos, bool full);

        int  PointToItem (int x) const;

        int  GetDataItemCount () const;
        int  GetFullVisibleDataItemCount () const;
        int  GetLastDataItem () const;

        bool IsLastDataItemFullVisible() const;
        bool IsCurrentVisible () const;
        bool IsCurrentFullVisible () const;

        int  IsDataItem (int i) const;
        int  ToAbsolute (int) const;
        void GetLoc (int, int[2]) const;
        void GetInnerLoc (int, int[2]) const;
        void GetInnerLoc (int, int&, int&) const;
        void GetInnerLoc (int, long&, long&, EFixType&) const;
        void GetCurrentLoc (int[2]) const;
        void GetCurrentInnerLoc (int[2]) const;
        void GetDataLoc (int[2]) const;
        void GetDataInnerLoc (int[2]) const;
        void GetDataArea (int[2]) const;
        void GetLeftFixLoc (int [2]) const;
        void GetLeftFixDataLoc (int [2]) const;
        void GetRightFixLoc (int [2]) const;
        void GetClientArea (int [2]) const;

        void GetFirstDataLoc (int [2]) const;

        EFixType GetItemType (int) const;

        // Cursor navigation
        bool MoveToLeft  ();
        bool MoveToRight ();
        bool MoveToHome  ();
        bool MoveToEnd   ();

        // new methods, pos - abs, inx - screen
        int  GetLastVisiblePos () const;
        int  GetLastFullVisiblePos () const;
        void CalcFirstFixSize ();
        void CalcLastFixSize ();

        bool IsEmpty ()             { return m_Items.empty(); }
        int  GetWidth (int i) const { return m_Items.at(i); }
        CWnd* GetClientWnd ();

        int  GetPixelSize         (int pos) const;
        int  GetFirstFixPixelSize (int pos) const;
        int  GetLastFixPixelSize  (int pos) const;
        void SetPixelSize         (int pos, int size, bool by_user = false, bool force = false);
        void ResetPixelSize       (int pos);

        const std::map<int,int>& GetResizedItems () const               { return m_resizedItems; }
        void SetResizedItems (const std::map<int,int>& resizedItems)    { m_resizedItems = resizedItems;  }
        void ClearCacheItems ();

        void OnChangedQuantity (int pos, int quantity);
};

inline
int GridRuler::GetPixelSize (int pos) const
{
    if (m_SizeAsOne != -1)
        return m_SizeAsOne;

    std::map<int, int>::const_iterator it = m_resizedItems.find(pos);

    if (it == m_resizedItems.end())
        return GetSource()->GetSize(pos, m_Dir) * GetCharSize()  + 2 * GetCellMargin();

    return LOWORD(it->second);
}

inline
int GridRuler::GetFirstFixPixelSize (int pos) const
{
    if (pos >= static_cast<int>(m_cacheFirstFixItems.size()))
        m_cacheFirstFixItems.resize(pos+1, -1);

    if (m_cacheFirstFixItems[pos] == -1)
        m_cacheFirstFixItems[pos] = GetSource()->GetFirstFixSize(pos, m_Dir) * GetCharSize()  + 2 * GetCellMargin();

    return m_cacheFirstFixItems[pos];
}

inline
int GridRuler::GetLastFixPixelSize (int pos) const
{
    if (pos >= static_cast<int>(m_cacheLastFixItems.size()))
        m_cacheLastFixItems.resize(pos+1, -1);

    if (m_cacheLastFixItems[pos] == -1)
        m_cacheLastFixItems[pos] = GetSource()->GetLastFixSize(pos, m_Dir) * GetCharSize()  + 2 * GetCellMargin();

    return m_cacheLastFixItems[pos];
}

inline
void GridRuler::SetPixelSize (int pos, int size, bool by_user /*= false*/, bool force /*= false*/)
{
    if (by_user || force)
        m_resizedItems[pos] = MAKELONG(size, by_user);
    else
    {
        std::map<int, int>::iterator it = m_resizedItems.find(pos);
        if (m_resizedItems.find(pos) == m_resizedItems.end()
        || HIWORD(it->second) == 0)
            m_resizedItems[pos] = MAKELONG(size, by_user);
    }
}

inline
void GridRuler::ResetPixelSize (int pos)
{
    // Drop a user-resized item; GetPixelSize then recomputes the source-derived default size.
    m_resizedItems.erase(pos);
}

/////////////////////////////////////////////////////////////////////////////

struct CalcGridManager : public AGridManager
{
    CWnd* m_pClientWnd;
    GridRuler m_Rulers[2];
    AGridSource* m_pSource;
    bool m_ShouldDeleteSource;

    CalcGridManager (CWnd*, AGridSource*, bool shouldDeleteSource);
    ~CalcGridManager ()                 { if (m_ShouldDeleteSource) delete m_pSource; }

    AGridSource* SetSource (AGridSource*, bool shouldDeleteSource);
    AGridSource* GetSource() const      { return m_pSource; }

    virtual void EvOpen (CRect& rect, CSize& chSize);
    virtual void EvFontChanged (CSize& chSize);
    virtual void EvClose ();
    virtual void Clear ();
    virtual void Refresh ();
    virtual void Rotate  ();

    virtual void InvalidateEdit () = 0;
    virtual void RepaintAll     () const = 0;
    virtual void RepaintCurCol  () const = 0;
    virtual void RepaintCurRow  () const = 0;
    virtual void RepaintCurrent  () const = 0;

    void DoEvSize (const CSize& size, bool adjust = true, bool force = false);

    virtual bool BeforeMove   (eDirection) = 0;   // false refuses the move
    virtual void AfterMove    (eDirection, int) = 0;
    virtual void BeforeScroll (eDirection) = 0;
    virtual void AfterScroll  (eDirection) = 0;

    virtual void CalcRuler (eDirection dir, int cx, bool adjust, bool invalidate);
    virtual void RecalcRuler (eDirection dir, bool adjust, bool invalidate);
    virtual void CalcScroller (eDirection dir);

    bool IsCurrentVisible () const;
    bool IsCurrentFullVisible () const;
    void GetCurRect (CRect&) const;

    void SetupRuler (eDirection, int charSize, bool resetPos);

    const int GetCurrentPos (eDirection dir) const                          { return m_Rulers[dir].GetCurrent(); }
    const std::map<int, int>& GetResizedItems (eDirection dir) const        { return m_Rulers[dir].GetResizedItems(); }
    void SetResizedItems (eDirection dir, const std::map<int, int>& items)  { m_Rulers[dir].SetResizedItems(items);  }
    void ClearCacheItems (eDirection dir)                                   { m_Rulers[dir].ClearCacheItems();  }

    // new style selection
    bool IsRangeSelectionEmpty () const                                     { return m_Rulers[edHorz].IsSelectionEmpty() && m_Rulers[edVert].IsSelectionEmpty();  }
    bool IsPointOverRangeSelection (const CPoint&) const;

    // Companion to IsPointOverRangeSelection, which answers false for the fixed strips: this reports
    // whether the click is on a header that already is the whole selection, so it can be preserved.
    bool IsPointOverSelectedHeader (const CPoint&) const;

    // Absolute data position on one axis under the point, or -1 for the fixed strip and for clicks
    // past the last item. The same arithmetic EvLButtonDown and the hit tests all need.
    int PointToPosition (eDirection, const CPoint&) const;

    // Hit tests live here rather than on GridManager because EvLButtonDown needs IsFixedCorner to
    // tell the corner from the empty region past the data - both report selPos -1/-1.
    bool IsFixedCorner (const CPoint& point) const;
    bool IsFixedCol (int x) const;
    bool IsFixedRow (int y) const;
    bool IsDataCell (const CPoint& point) const;
};

/////////////////////////////////////////////////////////////////////////////

struct NavGridManager : public CalcGridManager
{
    NavGridManager (CWnd*, AGridSource*, bool shouldDeleteSource);

    virtual void EvClose ();

    virtual void EvSize        (const CSize& size, bool force = false);
    virtual bool EvKeyDown     (unsigned key, unsigned repeatCount, unsigned flags);
    virtual void EvLButtonDown (unsigned modKeys, const CPoint& point);
    virtual void EvScroll      (eDirection, unsigned scrollCode, unsigned thumbPos);

    void SelectAll ();

    virtual void Refresh ();

    virtual bool MoveToLeft     (eDirection dir);
    virtual bool MoveToRight    (eDirection dir);
    virtual bool MoveToHome     (eDirection dir);
    virtual bool MoveToEnd      (eDirection dir);
    virtual bool MoveToPageUp   (eDirection dir);
    virtual bool MoveToPageDown (eDirection dir);

    void MoveTo (eDirection dir, int pos)                   { m_Rulers[dir].SetCurrent(pos); }

    virtual bool ScrollToLeft  (eDirection);
    virtual bool ScrollToRight (eDirection);
    virtual bool ScrollToHome  (eDirection);
    virtual bool ScrollToEnd   (eDirection);

    virtual void AfterMove     (eDirection, int);

protected:
    bool m_ScrollMode;

    // Set while a SetCurrent is the code's rather than the user's, so AfterMove leaves the selection
    // alone. AfterMove reads the raw Shift state to decide whether to extend a range, which is right
    // for an arrow key and wrong for a programmatic move: Ctrl+Shift+F2 has Shift down but means no
    // range. The mouse-click path sets it for the same reason.
    bool m_SuppressRangeSelect;

    enum eScrollDirection { esLeft, esRight };

    void setScrollMode (bool scroll = true);
    void endScrollMode () { if (m_ScrollMode) setScrollMode(false); }

    class ScrollController
    {
        eDirection m_Dir;
        NavGridManager& m_Manager;

    public:
        ScrollController (NavGridManager& manager, eDirection dir)
            : m_Dir(dir), m_Manager(manager)
            {
                if (!m_Manager.m_ScrollMode)
                    m_Manager.setScrollMode(true);
            }

        ~ScrollController ()
            {
                if (m_Manager.m_ScrollMode
                && m_Manager.IsCurrentFullVisible())
                    m_Manager.setScrollMode(false);
            }
    };
};

/////////////////////////////////////////////////////////////////////////////

struct PaintGridManager : public NavGridManager
{
    static DrawCellContexts m_Dcc;

    PaintGridManager (CWnd*, AGridSource*, bool shouldDeleteSource);
    void InitColors ();

    void Paint (CDC& dc, bool erase, const CRect& rect);
private:
    void erase      (CDC&, const CRect&) const;
    void drawUnused (CDC&, const CRect&) const;
    void drawLines  (eDirection dir, CDC& dc, int lim[4], COLORREF color) const;
public:
    void DrawFocus (CDC&) const;

    // overrided AGridManager abstract method's
    virtual void EvSetFocus    (bool);
    virtual void RepaintAll    () const;
    virtual void RepaintCurCol () const;
    virtual void RepaintCurRow () const;
    virtual void RepaintCurrent () const;

    void InvalidateRows (int start, int end);
    void InvalidateCols (int start, int end);

    virtual void OnChangedRows (int startRow, int endRow);
    virtual void OnChangedCols (int startCol, int endCol);
    virtual void OnChangedRowsQuantity (int row, int quantity);
    virtual void OnChangedColsQuantity (int col, int quantity);
    virtual void OnRowsReordered ();
};

/////////////////////////////////////////////////////////////////////////////

struct EditGridManager : public PaintGridManager
{
    CWnd* m_pEditor;
    enum eState {
        esClear       = 0x00,
        esEditMode    = 0x01,
        esHiddenEdit  = 0x02,
        esRefreshEdit = 0x04
    };
    BYTE m_State;

    // Set by a Cancel to leaving a changed cell, cleared when the next edit session opens.
    bool m_leaveRefused;

    // Set once a staged row has said why it holds the cursor, cleared when the next one begins.
    bool m_pendingRowWarned;

    void SetFlag   (eState flag)            { m_State |=  flag; }
    void ClearFlag (eState flag)            { m_State &= ~flag; }
    bool CheckFlag (eState flag) const      { return (m_State & flag) ? true : false; }

    EditGridManager (CWnd*, AGridSource*, bool shouldDeleteSource);

    virtual void EvClose ();
    virtual void Clear ();
    virtual bool IsEditMode () const        { return (esEditMode & m_State); }

    virtual void SetEditMode ();
    virtual void EndEditMode (bool save = true, bool focusToClient = true);

    virtual int AppendRow ();
    virtual int InsertRow ();
    virtual int DeleteRow ();

    // Ctrl+Del. Confirms, then queues the server-side DELETE; the row goes when the task publishes.
    virtual void DeleteCurrentRow ();

    // F2. Opens the cell editor on the focused cell - the menu's route to what the key does.
    virtual void EditCurrentCell ();

    // Ctrl+F2 stages a row to INSERT and leaves the user on it. The source owns the row but the
    // ruler is owned here, which is why the gesture is a manager method and not a source one.
    virtual void BeginPendingRow ();

    // Ctrl+Shift+F2. The same staged row, seeded from the one under the cursor, so a near-copy needs
    // only its differences typed. A NULL is left unfilled and posts as the column's own DEFAULT.
    virtual void DuplicateCurrentRow ();

    // The two above: copyFrom names the row to seed from, or -1 to stage a blank one.
    void beginPendingRow (int copyFrom);

    // Ctrl+Enter with no editor open posts the staged row. With one open the key belongs to
    // CellEditor, whose Ctrl+Enter commits a multiline edit instead of taking a newline.
    virtual void PostPendingRow ();

    // Esc, and only once an open editor and a live selection have both declined it.
    virtual void DiscardPendingRow ();

    CWnd* GetEditor  () const               { return m_pEditor; }
    void OpenEditor  ();
    void CloseEditor (bool save = true, bool focusToClient = true);

    virtual bool BeforeMove   (eDirection);
    virtual void AfterMove    (eDirection, int);
    virtual void BeforeScroll (eDirection);
    virtual void AfterScroll  (eDirection);

    virtual bool IdleAction ();
    //static  void IdleActionForAll ();

    virtual void EvSetFocus (bool);
    virtual bool EvKeyDown (unsigned key, unsigned repeatCount, unsigned flags);
    virtual bool ClaimKey (UINT key) const;
    virtual void EvLButtonDblClk (unsigned modKeys, const CPoint& point);

    virtual void InvalidateEdit ();
};

/////////////////////////////////////////////////////////////////////////////

class ResizeGridManager : public EditGridManager
{
protected:
    bool       m_Capture;
    eDirection m_CaptureType;
    CPoint     m_oldPos;
    int        m_ResizingItem;

public:
    ResizeGridManager (CWnd*, AGridSource*, bool shouldDeleteSource);

    bool ResizingProcess () const { return m_Capture; }

    virtual void EvLButtonDown (unsigned modKeys, const CPoint& point);
    virtual void EvLButtonUp   (unsigned modKeys, const CPoint& point);
    virtual void EvMouseMove   (unsigned modKeys, const CPoint& point);

    void LightRefresh (eDirection dir);

	bool IsMouseInColResizingPos (eDirection, const CPoint&, int&) const;

private:
    bool checkMousePos (eDirection, const CPoint&, int&) const;
    void drawResizeLine (const CPoint&);
};

inline
bool ResizeGridManager::IsMouseInColResizingPos (eDirection dir, const CPoint& point, int& item) const
{
	return checkMousePos(dir, point, item);
}


/////////////////////////////////////////////////////////////////////////////

class GridManager : public ResizeGridManager
{
public:
    GridManager (CWnd* client , AGridSource* source, bool shouldDeleteSource = false)
        : ResizeGridManager(client, source, shouldDeleteSource) {}
};


/////////////////////////////////////////////////////////////////////////////

    inline
    void CalcGridManager::RecalcRuler (eDirection dir, bool adjust, bool invalidate)
        { CalcRuler(dir, m_Rulers[dir].GetClientSize(), adjust, invalidate); }
    inline
    void CalcGridManager::CalcScroller (eDirection dir)
        { m_Rulers[dir].calcScroller(); }
    inline
    bool CalcGridManager::IsCurrentVisible () const
        { return m_Rulers[edHorz].IsCurrentVisible() && m_Rulers[edVert].IsCurrentVisible(); }
    inline
    bool CalcGridManager::IsCurrentFullVisible () const
        { return m_Rulers[edHorz].IsCurrentFullVisible() && m_Rulers[edVert].IsCurrentFullVisible(); }

    inline
    AGridSource* GridRuler::GetSource ()
        { return GetManager()->m_pSource; }
    inline
    const AGridSource* GridRuler::GetSource () const
        { return GetManager()->m_pSource; }
    inline
    int GridRuler::GetSourceCount () const
        { return GetSource()->GetCount(m_Dir); }
    inline
    bool GridRuler::IsCurrentVisible () const
        { return (GetTopmost() <= GetCurrent() && GetCurrent() < GetTopmost() + m_NoFixCount); }
    inline
    int GridRuler::IsDataItem (int i) const
        { return (i >= m_FirstFixCount) && i < (m_FirstFixCount + m_NoFixCount - m_UnusedCount); }
    inline
    int GridRuler::GetLastDataItem () const
        { return m_FirstFixCount + m_NoFixCount - m_UnusedCount - 1; }
    inline
    int GridRuler::GetDataItemCount () const
        { return m_NoFixCount - m_UnusedCount; }
    inline
    int GridRuler::GetFullVisibleDataItemCount () const
        { return GetDataItemCount() - (IsLastDataItemFullVisible() ? 0 : 1); }
    inline
    int GridRuler::ToAbsolute (int i) const
        { return i + GetTopmost() - m_FirstFixCount; }
    inline
    void GridRuler::GetInnerLoc (int i, int loc[2]) const
        {
            loc[0] = m_Items.at(i);
            loc[1] = m_Items.at(i + 1) - m_LineSize;
        }
    inline
    void GridRuler::GetLoc (int i, int loc[2]) const
        {
            loc[0] = m_Items.at(i);
            loc[1] = m_Items.at(i + 1);
        }
    inline
    void GridRuler::GetInnerLoc (int i, int& beg, int& end) const
        {
            beg = m_Items.at(i);
            end = m_Items.at(i + 1) - m_LineSize;
        }
    inline
    void GridRuler::GetInnerLoc (int i, long& beg, long& end, EFixType& type) const
        {
            beg = m_Items.at(i);
            end = m_Items.at(i + 1) - m_LineSize;
            type = GetItemType(i);
        }
    inline
    void GridRuler::GetCurrentLoc (int loc[2]) const
        {
            _ASSERTE(IsCurrentVisible());
            if (IsCurrentVisible())
            {
                loc[0] = m_Items.at(m_FirstFixCount + GetCurrent() - GetTopmost());
                loc[1] = m_Items.at(m_FirstFixCount + GetCurrent() - GetTopmost() + 1);
            }
        }
    inline
    void GridRuler::GetCurrentInnerLoc (int loc[2]) const
        {
            _ASSERTE(IsCurrentVisible());
            if (IsCurrentVisible())
            {
                loc[0] = m_Items.at(m_FirstFixCount + GetCurrent() - GetTopmost());
                loc[1] = m_Items.at(m_FirstFixCount + GetCurrent() - GetTopmost() + 1) - m_LineSize;
            }
        }
    inline
    void GridRuler::GetDataArea (int loc[2]) const
        {
            loc[0] = m_FirstFixSize;
            loc[1] = !m_LastFixCount ? GetClientSize() : m_FirstFixSize + m_ScrollSize - m_LineSize;
        }
    inline
    void GridRuler::GetDataLoc (int loc[2]) const
        {
            loc[0] = m_Items.at(m_FirstFixCount);
            loc[1] = m_Items.at(GetLastDataItem() + 1);
        }
    inline
    void GridRuler::GetDataInnerLoc (int loc[2]) const
        {
            loc[0] = m_Items.at(m_FirstFixCount);
            loc[1] = m_Items.at(GetLastDataItem() + 1) - m_LineSize;
        }
    inline
    void GridRuler::GetLeftFixLoc (int loc[2]) const
        {
            loc[0] = m_Items.at(0);
            loc[1] = m_Items.at(m_FirstFixCount);
        }
    inline
    void GridRuler::GetLeftFixDataLoc (int loc[2]) const
        {
            loc[0] = m_Items.at(0);
            loc[1] = m_Items.at(GetLastDataItem() + 1);
        }
    inline
    void GridRuler::GetRightFixLoc (int loc[2]) const
        {
            loc[0] = m_Items.at(m_FirstFixCount + m_NoFixCount);
            loc[1] = m_Items.at(m_Count);
        }
    inline
    void GridRuler::GetClientArea (int loc[2]) const
        {
            loc[0] = 0;
            loc[1] = GetClientSize();
        }
    inline
    void GridRuler::GetFirstDataLoc (int loc[2]) const
        {
            loc[0] = m_Items.at(m_FirstFixCount);
            loc[1] = m_Items.at(m_FirstFixCount+1);
        }
    inline
    int GridRuler::GetLastVisiblePos () const
        {
            return GetTopmost() + GetDataItemCount() - 1;
        }
    inline
    int GridRuler::GetLastFullVisiblePos () const
        {
            return GetTopmost() + GetDataItemCount() - 1 - (IsLastDataItemFullVisible() ? 0 : 1);
        }
    inline
    void GridRuler::CalcFirstFixSize ()
        {
            for (int i = 0; i < m_FirstFixCount; i++)
                m_FirstFixSize += GetFirstFixPixelSize(i) + m_LineSize;
        }
    inline
    void GridRuler::CalcLastFixSize ()
        {
            for (int i = 0; i < m_LastFixCount; i++)
                m_LastFixSize += GetLastFixPixelSize(i) + m_LineSize;
        }
    inline
    CWnd* GridRuler::GetClientWnd ()
        { return m_Manager->m_pClientWnd; }

    inline
    void rotate (GridRulerBase& left, GridRulerBase& right)
    {
        std::swap(left.m_Current,      right.m_Current);
        std::swap(left.m_Topmost,      right.m_Topmost);
        std::swap(left.m_SavedTopmost, right.m_SavedTopmost);
    }


}//namespace OG2

#endif//__GridManager_h__
