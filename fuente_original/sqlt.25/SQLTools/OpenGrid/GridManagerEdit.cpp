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

/***************************************************************************/
/*      Purpose: Grid manager implementation for grid component            */
/*      ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~            */
/*      (c) 1996-1999,2003 Aleksey Kochetov                                */
/***************************************************************************/

// 2017-11-28 implemented range selection

#include "stdafx.h"
#include "GridManager.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace OG2
{
    class EditNotValid : public std::exception
    {
    public:
        EditNotValid () : std::exception("Invalid input") {}
    };

EditGridManager::EditGridManager (CWnd* client , AGridSource* source, bool shouldDeleteSource)
: PaintGridManager(client, source, shouldDeleteSource),
  m_pEditor(0),
  m_State(esClear),
  m_leaveRefused(false),
  m_pendingRowWarned(false)
{
}

void EditGridManager::EvClose ()
{
    EndEditMode(false/*save*/, false/*focusToClient*/);
    PaintGridManager::EvClose();
}

void EditGridManager::Clear ()
{
    EndEditMode(false/*save*/, false/*focusToClient*/);
    PaintGridManager::Clear();
}

void EditGridManager::SetEditMode ()
{
    _ASSERTE(!IsEditMode());

    if (m_Options.m_Editing
    //&& m_pSource->CanModify()
    && !m_pSource->IsEmpty()
    && m_pSource->GetCount(edHorz)
    && m_pSource->GetCount(edVert))
    {
        SetFlag(esEditMode);
        OpenEditor();
    } else
        MessageBeep(MB_ICONHAND);
}

void EditGridManager::EndEditMode (bool save, bool focusToClient)
{
    if (IsEditMode())
    {
        CloseEditor(save, focusToClient);
        ClearFlag(esEditMode);
        m_leaveRefused = false;
    }
}

int EditGridManager::AppendRow ()
{
    endScrollMode();
    int row = m_pSource->Append();
    m_Rulers[edVert].SetCurrent(row);
    return row;
}

int EditGridManager::InsertRow ()
{
    endScrollMode();
    int row = m_Rulers[edVert].GetCurrent();
    m_pSource->Insert(row);
    m_Rulers[edVert].SetCurrent(row);
    return row;
}

int EditGridManager::DeleteRow ()
{
    endScrollMode();
    int row = m_Rulers[edVert].GetCurrent();
    m_pSource->Delete(row);
    //m_Rulers[edVert].SetCurrent(row);
    return row;
}

// Ctrl+Del. The confirmation comes before anything is cancelled, so abandoning the dialog leaves an
// open editor exactly as the user typed it. The row itself goes later, when the task publishes.
void EditGridManager::DeleteCurrentRow ()
{
    ASSERT_EXCEPTION_FRAME;

    endScrollMode();

    const int row = m_Rulers[edVert].GetCurrent();

    if (row < 0 || row >= m_pSource->GetCount(edVert))
    {
        MessageBeep(MB_ICONHAND);
        return;
    }

    if (AfxMessageBox(L"Delete this row from the database?\n\n"
                      L"It still needs a COMMIT unless the connection autocommits.",
                      MB_YESNO|MB_ICONEXCLAMATION|MB_DEFBUTTON2) != IDYES)
        return;

    // Discarded, not saved: a saved cell would take the one in-flight write slot the delete needs.
    EndEditMode(false/*save*/);

    m_pSource->CommitRowDelete(row);
}

// A staged row holds the cursor on itself; a changed cell anywhere else is offered the choice, and
// Cancel holds the cursor until Enter or Esc ends the session.
bool EditGridManager::BeforeMove (eDirection dir)
{
    // Ahead of the edit-mode test, because the row is held with no editor open too - most of the time.
    if (dir == edVert && m_pSource && m_pSource->IsPendingRow(m_Rulers[edVert].GetCurrent()))
    {
        // Once per staged row, so a held arrow key or Tab's wraparound cannot stack dialogs.
        if (!m_pendingRowWarned)
        {
            m_pendingRowWarned = true;
            AfxMessageBox(L"The new row is not finished yet.\n\n"
                          L"Ctrl+Enter inserts it and Esc throws it away.",
                          MB_OK|MB_ICONASTERISK);
        }

        // Focus returned as the Cancel below returns it: the box took it, and an editor still has text.
        if (m_pEditor)
            m_pEditor->SetFocus();

        return false;
    }

    if (!IsEditMode() || !m_pEditor)
        return true;

    if (m_leaveRefused)
        return false;

    // Unchanged behaves as it always did: CloseEditor(save) posts nothing PostEdit finds different.
    if (!m_pSource->IsEditDirty())
    {
        CloseEditor();
        return true;
    }

    // Crossing columns of the staged row is filling it in rather than leaving it, and CommitCellEdit
    // only records the text locally - so it is saved without being asked about.
    if (dir == edHorz && m_pSource->IsEditingPendingRow())
    {
        CloseEditor(true/*save*/);
        return true;
    }

    switch (AfxMessageBox(L"The cell has been changed.\n\nSave the change before moving away?",
                          MB_YESNOCANCEL|MB_ICONQUESTION|MB_DEFBUTTON1))
    {
    case IDYES:
        CloseEditor(true/*save*/);
        return true;

    case IDNO:
        CloseEditor(false/*save*/);
        return true;

    // Latched, so the wraparound this refusal triggers asks once rather than once per direction.
    default:
        m_leaveRefused = true;
        m_pEditor->SetFocus();
        return false;
    }
}

void EditGridManager::AfterMove (eDirection dir, int oldVal)
{
    __super::AfterMove(dir, oldVal);
}

// F2 from the menu rather than the keyboard. Guarded on IsEditMode: SetEditMode asserts it is not
// already on, and the update handler greys the item while an editor is open in any case.
void EditGridManager::EditCurrentCell ()
{
    if (!IsEditMode())
        SetEditMode();
}

// Ctrl+F2. The source appends the staged row and answers its index; the ruler follows, so the user
// is left on the row they asked for rather than the one they happened to be standing on.
void EditGridManager::BeginPendingRow ()
{
    beginPendingRow(-1);
}

// Ctrl+Shift+F2. Read before the staging runs: the row under the cursor is the one to copy, and
// nothing below moves it.
void EditGridManager::DuplicateCurrentRow ()
{
    beginPendingRow(m_pSource ? m_Rulers[edVert].GetCurrent() : -1);
}

void EditGridManager::beginPendingRow (int copyFrom)
{
    if (!m_pSource || !m_pSource->CanInsertRow())
        return;

    endScrollMode();

    // The source drops the sort before appending, permuting rows under an open editor.
    EndEditMode();

    const int row = m_pSource->BeginPendingRow(copyFrom);
    if (row < 0)
        return;

    // A fresh row gets a fresh warning: the last one explained a row that is no longer there.
    m_pendingRowWarned = false;

    // Reaching the ruler through SetCurrent runs AfterMove, which reads the raw Shift state to
    // decide whether to extend a range. Ctrl+Shift+F2 has Shift down, but this move is the code's
    // and not the user's - staging a row must not select the rows it stepped across to get there.
    m_SuppressRangeSelect = true;

    m_Rulers[edVert].SetCurrent(row);

    // SetCurrent moves the focus without scrolling, and the staged row is the last one - so an
    // insert into a grid scrolled away from the bottom would otherwise stage a row out of sight.
    if (!m_Rulers[edVert].IsCurrentVisible())
        ScrollToEnd(edVert);

    m_SuppressRangeSelect = false;
}

// Ctrl+Enter. Reached only with no editor open: ClaimKey hands the key to CellEditor while one is,
// where it commits a multiline edit, so the cell is already staged by the time this runs.
void EditGridManager::PostPendingRow ()
{
    if (m_pSource && m_pSource->HasPendingRow())
        m_pSource->CommitRowInsert();
}

void EditGridManager::DiscardPendingRow ()
{
    if (m_pSource && m_pSource->HasPendingRow())
        m_pSource->DiscardPendingRow();
}

void EditGridManager::BeforeScroll (eDirection)
{
    if (IsEditMode() && m_pEditor)
    {
        m_pEditor->SetWindowPos(0, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOZORDER | SWP_NOREDRAW);
        SetFlag(esHiddenEdit);
    }
}

void EditGridManager::AfterScroll (eDirection)
{
}

void EditGridManager::OpenEditor ()
{
    if (IsEditMode() && !m_pEditor && IsCurrentVisible())
    {
        m_leaveRefused = false;

        CRect rc;
        GetCurRect(rc);
        rc.InflateRect(-1, -1);
        m_pEditor = m_pSource->BeginEdit(m_Rulers[edVert].GetCurrent(), m_Rulers[edHorz].GetCurrent(), m_pClientWnd, rc);

        if (m_pEditor)
        {
            m_pClientWnd->InvalidateRect(rc, FALSE);
            m_pEditor->SetFont(m_pClientWnd->GetFont());
            m_pEditor->SetFocus();
        }
    }
}

void EditGridManager::CloseEditor (bool save, bool focusToClient)
{
    ASSERT_EXCEPTION_FRAME;

    if (IsEditMode() && m_pEditor)
    {
        if (save)
        {
            if (m_pEditor->UpdateData(TRUE))
                m_pSource->PostEdit();
            else
                _RAISE(EditNotValid());
        }
        else
            m_pSource->CancelEdit();

        m_pEditor = 0;

        if (focusToClient)
            m_pClientWnd->SetFocus();
    }
}

bool EditGridManager::IdleAction ()
{
    if (CheckFlag(esHiddenEdit) && IsEditMode()
        && m_pEditor && IsCurrentVisible())
    {
        ClearFlag(esHiddenEdit);
        CRect rc;
        GetCurRect(rc);
        rc.InflateRect(-1,-1);
        m_pEditor->SetWindowPos(&CWnd::wndTop, rc.left, rc.top, rc.Width(), rc.Height(), SWP_SHOWWINDOW);
    }

    if (CheckFlag(esRefreshEdit) && IsEditMode())
    {
        ClearFlag(esRefreshEdit);
        CloseEditor(false/*save*/);
    }

    if (!m_pEditor && IsEditMode())
        OpenEditor();

    return false;
}

void EditGridManager::EvSetFocus (bool focus)
{
    PaintGridManager::EvSetFocus(focus);

    if (focus && m_pEditor) m_pEditor->SetFocus();
}

bool EditGridManager::EvKeyDown (UINT key, UINT repeatCount, UINT flags)
{
    bool retVal = true;
    try {
        if (!PaintGridManager::EvKeyDown(key, repeatCount, flags))
            switch (key)
            {
            // Ctrl+F2 stages a row to INSERT, Ctrl+Shift+F2 stages a copy of the row under the
            // cursor, and bare F2 stays the cell-edit toggle it has always been. ClaimKey answers
            // for all three, so the modifiers are read here rather than inferred from it.
            case VK_F2:
                if (0xFF00 & GetKeyState(VK_CONTROL))
                {
                    // Not claimed, not consumed: a Ctrl+F2 this grid has no use for belongs to the
                    // host, exactly as a bare F2 on a read-only grid always has.
                    if (!ClaimKey(key))
                        retVal = false;
                    else if (0xFF00 & GetKeyState(VK_SHIFT))
                        DuplicateCurrentRow();
                    else
                        BeginPendingRow();
                }
                else if (!IsEditMode()) SetEditMode();
                else                    EndEditMode();
                break;
            // Enter commits. With m_MoveOnEnter set EvKeyDown already took the key, so this is the
            // non-moving case; with no editor open, Ctrl+Enter posts a staged row (ClaimKey answered).
            case VK_RETURN:
                if (IsEditMode())   EndEditMode(true/*save*/);
                else if (ClaimKey(key)) PostPendingRow();
                break;
            case VK_ESCAPE:
            {
                // Captured before either runs, so the third claim below can tell an Esc that had
                // something to do from one that did not.
                const bool hadEditor    = IsEditMode();
                const bool hadSelection = !m_Rulers[edHorz].IsSelectionEmpty()
                                       || !m_Rulers[edVert].IsSelectionEmpty();

                if (hadEditor)
                    EndEditMode(false/*save*/);

                if (hadSelection)
                {
                    m_Rulers[edVert].ResetSelection();
                    m_Rulers[edHorz].ResetSelection();
                    RepaintAll();
                }

                // A third claim, taken only for an Esc the two above had no use for, so an Esc that
                // closes an editor or clears a selection behaves exactly as it did before.
                if (!hadEditor && !hadSelection)
                    DiscardPendingRow();

                break;
            }
            // ClaimKey is the one predicate, consulted again here because EvKeyDown is also reached
            // from OnKeyDown, where a bare Del arrives unclaimed and must fall through untouched.
            case VK_DELETE:
                if (ClaimKey(key))  DeleteCurrentRow();
                else                retVal = false;
                break;
            default:
                retVal = false;
            }
    }
    catch (const EditNotValid&) {}
    return retVal;
}

bool EditGridManager::ClaimKey (UINT key) const
{
    const bool shift = (0xFF00 & GetKeyState(VK_SHIFT))   != 0;
    const bool ctrl  = (0xFF00 & GetKeyState(VK_CONTROL)) != 0;
    const bool alt   = (0xFF00 & GetKeyState(VK_MENU))    != 0;

    // Bare F2 only, and only where this grid can actually edit: a read-only grid leaves the key to
    // whatever its host has bound to it, and so do the modified forms - except Ctrl+F2 below.
    if (key == VK_F2)
    {
        if (!m_Options.m_Editing || !m_pSource)
            return false;

        if (!shift && !ctrl && !alt)
            return m_pSource->CanModify();

        // Ctrl+F2 shadows a bookmark command, the same trade Ctrl+Del makes: a view's
        // PreTranslateMessage runs first, so only the focused grid loses the key. An editor keeps it.
        // Shift is claimed on the same ground, for the copy-of-the-current-row variant.
        if (ctrl && !alt && !IsEditMode())
            return m_pSource->CanInsertRow();

        return false;
    }

    // Ctrl+Del deletes the row. A bare Del and the other modified forms stay with the host, and an
    // open editor keeps the key, where Del is a character edit rather than a grid command.
    if (key == VK_DELETE)
        return ctrl && !shift && !alt && !IsEditMode()
            && m_Options.m_Editing && m_pSource && m_pSource->CanDeleteRow();

    // Ctrl+Enter is how CellEditor commits a multiline edit instead of inserting a newline, so an
    // open editor owns the key; with none open it posts a staged row, and with neither it stays put.
    if (key == VK_RETURN)
        return ctrl && !shift && !alt
            && (IsEditMode() || (m_pSource && m_pSource->HasPendingRow()));

    return false;
}

void EditGridManager::EvLButtonDblClk (UINT /*modKeys*/, const CPoint& point)
{
    int lim[4];
    m_Rulers[edHorz].GetDataLoc(lim);
    m_Rulers[edVert].GetDataLoc(lim+2);

    if (lim[0] <= point.x && point.x < lim[1]
    && lim[2] <= point.y && point.y < lim[3]
    && !IsEditMode())
    {
        SetEditMode();
    }
}

void EditGridManager::InvalidateEdit ()
{
    SetFlag(esRefreshEdit);
}

}//namespace OG2
