/* 
    Copyright (C) 2002 Aleksey Kochetov

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

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __OEFindReplaceDlg_h__
#define __OEFindReplaceDlg_h__

#include "resource.h"
#include "OpenEditor/OEHelpers.h"
#include <COMMON/EditWithSelPos.h>


    class COEditorView;

// GlobalSettings::FindDisableLivePreview / FindCloseOnDeactivate ("Editor::General",
// default false) are read where they act, so a change applies to an open dialog.

class CFindReplaceDlg : public CDialog
{
    static CPoint m_ptTopLeft;

    // One Find/Replace dialog for the whole app, heap-allocated by ShowModeless,
    // owned by the main frame and deleted in PostNcDestroy.
    static CFindReplaceDlg* m_pInstance;

public:
    CString  m_SearchText, m_ReplaceText;

    BOOL m_MatchCase;
    BOOL m_MatchWholeWord;
    BOOL m_AllWindows;
    BOOL m_RegExp;
    BOOL m_BackslashExpressions;

    int  m_WhereReplace; // scope trio: 0 - selection, 1 - current file, 2 - all windows

    // Singleton lifecycle: ShowModeless creates, reuses or (on a Find <-> Replace
    // switch) recreates; NotifyViewDestroying keeps the target from dangling.
    static void ShowModeless (COEditorView* pView, BOOL replace);
    static void NotifyViewDestroying (COEditorView* pView);

    // From COEditorView::OnActivateView(TRUE): retarget the open dialog to the
    // editor the user moved to. "All Windows" is exempt - it follows its hits.
    static void NotifyViewActivated (COEditorView* pView);

    // Idle relay from COEditorView::OnIdleUpdateCmdUI: an owned popup is not a
    // child, so the WM_IDLEUPDATECMDUI broadcast never reaches it. Idempotent.
    static void NotifyIdle ();

    // From the view's OnEditFindNext/Previous: F3 with the dialog open on pView
    // searches the combo's (possibly uncommitted) text. True = handled.
    static bool RouteViewFindCommand (COEditorView* pView, bool backward);

    // True while the singleton dialog is open on pView; the view's find-command
    // update handlers use it so MFC never drops an accelerator F3 as disabled.
    static bool IsOpenFor (COEditorView* pView);

private:
    void toPrintableStr (const wchar_t* from, std::wstring& _to);
    void toUnprintableStr (const wchar_t* from, std::wstring& _to, bool skipEscDgt);

    COEditorView* m_pEditorView;

    CComboBox m_wndSearchText;
    CComboBox m_wndReplaceText;
    CEditWithSelPos m_edtSearchText;
    CEditWithSelPos m_edtReplaceText;

    BOOL m_ReplaceMode;
    BOOL m_Modified;

    // Find-family subset copied from the frame's (possibly remapped) table, so
    // F3/Ctrl+F keep working here without hijacking ordinary edit keys.
    HACCEL m_hFindAccel;

    void AdjustPosition ();
    void SaveOption ();
    void SetupButtons ();
    void ShowPopupMenu (UINT btnId, UINT menuId);

    void RetargetFull (COEditorView* pView);   // Ctrl+F/Ctrl+H: refresh text/state, take the foreground
    // The target died or the user switched editors: no foreground, no focus
    // steal, no combo refill; reposition=false also leaves the dialog in place.
    void RetargetQuiet (COEditorView* pView, bool reposition = true);
    // Guarded quiet retarget on an editor activation - see NotifyViewActivated.
    void RetargetToActivatedView (COEditorView* pView);
    // The MDI-active child's editor view, or NULL when it shows no editor pane.
    static COEditorView* ActiveEditorView ();
    BOOL RefreshFromView ();                   // recompute m_WhereReplace; returns the Replace-in-selection enable flag
    void CloseDialog (bool restoreFocus = true);
    void RestoreFocusToSearchText ();
    void BuildFindAccelTable ();

    // Find-as-you-type: previews always search from m_previewAnchor (captured at
    // show/retarget and after every commit), so keystrokes never walk matches.
    void CapturePreviewAnchor ();
    // countOnly arms a tick that only counts ("n of m") and leaves selection,
    // scroll and folds untouched; All Windows ticks are always count-only.
    void RestartPreviewTimer (bool countOnly = false);
    void RunFindPreview ();

    // Re-resolve the anchor through its line's LineId after a document edit;
    // a deleted anchor line falls back to a re-capture from the selection.
    void RefreshStalePreviewAnchor ();

    // Cursor-relativity: a live selection differing from the one the dialog last
    // knew means the user moved the caret - re-capture the anchor there.
    void RefreshAnchorForMovedSelection ();
    // Record the target view's selection as the one the dialog last knew, so its
    // own navigation is never misread as user caret movement.
    void RememberViewSelection ();

    // Clear the matching-text tint this dialog injected (never a selection-derived
    // one); committed operations hand the highlight to the view's maintenance.
    void ClearInjectedFindHighlight ();

    // Shared body of Enter / Find Next / routed F3: commit the combo and search,
    // inside the armed region when there is one. True = a search ran.
    bool RunCommittedSearch (bool backward, bool restoreFocus = true);

    // Scoped committed step: advance to the next match fully inside the armed
    // region, wrapping at its edges (fresh start while the selection covers it).
    bool RunScopedSearch (bool backward, CString& counterText);

    // Caption mirror of the "n of m" counter ("Find - 3 of 12") - the floating
    // dialog sits away from the main frame's status line.
    void UpdateTitleCounter (const CString& counterText);

    CString m_baseCaption;   // template caption ("Find"/"Replace"), captured in OnInitDialog
    CString m_titleCounter;  // counter currently appended to the caption, "" = none

    OpenEditor::Position m_previewAnchor;
    bool m_previewAnchorValid;
    bool m_countOnlyPreview;   // pending tick counts without navigating
    bool m_findHighlightInjected;   // the view's tint is this dialog's injection

    // Stable identity of the preview anchor: the line's LineId re-resolves to its
    // current index, the action stamp detects edits since the capture.
    OpenEditor::LineId     m_previewAnchorLineId;
    OpenEditor::SequenceId m_previewAnchorActionId;

    // The target view's selection as the dialog last knew it; invalid until the
    // first capture, which keeps the moved-selection diff from firing early.
    OpenEditor::Square m_lastKnownSelection;
    bool m_lastKnownSelectionValid;

    // Idle-driven scope tracking: the classification the last pass saw
    // (-1 = not examined, 0 = no usable selection, 1 = single-line, 2 = multi-line).
    int m_lastScopeSelState;
    void RefreshScopeIdle ();

    // The captured selection-scope region (position coords) that scoped preview,
    // counter and Replace All stay inside; visualized as a held line band.
    OpenEditor::Square     m_scopeRegion;
    bool                   m_scopeRegionValid;
    OpenEditor::LineId     m_scopeRegionStartId;
    OpenEditor::LineId     m_scopeRegionEndId;
    OpenEditor::SequenceId m_scopeRegionActionId;

    void CaptureScopeRegion ();          // snapshot the live selection as the region + arm the band
    void ReleaseScopeRegion ();          // drop region + band (no-op when none; never touches radio)
    void RefreshScopeRegionStaleness (); // LineId re-resolution after edits; may release + fall back
    void ApplyScopeRadio (int scope);    // radio trio + SetupButtons; no-op when the scope stands

// Construction
public:
    CFindReplaceDlg (BOOL replace, COEditorView*);
    void SearchBatch (OpenEditor::ESearchBatch mode);

    // Dialog Data
    //{{AFX_DATA(CFindReplaceDlg)
    enum { IDD = IDD_OE_EDIT_REPLACE };
    //}}AFX_DATA
    //{{AFX_VIRTUAL(CFindReplaceDlg)
    protected:
    virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
    //}}AFX_VIRTUAL

// Implementation
protected:
    //{{AFX_MSG(CFindReplaceDlg)
    afx_msg void OnFindNext();
    afx_msg void OnFindPrevious();
    virtual BOOL OnInitDialog();
    afx_msg void OnChangeSettings();
    afx_msg void OnReplace();
    afx_msg void OnReplaceAll();
    afx_msg void OnScopeRadio();
    afx_msg void OnSearchText();
    afx_msg void OnCount();
    afx_msg void OnMarkAll();
    //}}AFX_MSG
    DECLARE_MESSAGE_MAP()
public:
    afx_msg void OnBnClicked_RegexpFind();
    afx_msg void OnBnClicked_RegexpReplace();
    afx_msg void OnInsertRegexpFind (UINT nID);
    afx_msg void OnInsertRegexpReplace (UINT nID);
protected:
    // Modeless close paths: Esc, the Enter fallback and the caption X all funnel
    // into CloseDialog; PostNcDestroy clears the singleton and deletes this.
    virtual void OnCancel();
    virtual void OnOK();
    afx_msg void OnClose();
    afx_msg void OnTimer(UINT_PTR nIDEvent);   // find-as-you-type debounce
    // Opt-in close-on-deactivate: OnActivate/OnActivateApp only arm the check,
    // decided in the posted handler once the activation has settled.
    afx_msg void OnActivate(UINT nState, CWnd* pWndOther, BOOL bMinimized);
    afx_msg void OnActivateApp(BOOL bActive, DWORD dwThreadID);
    afx_msg LRESULT OnCheckCloseOnDeactivate(WPARAM, LPARAM);
    virtual void PostNcDestroy();
    virtual BOOL PreTranslateMessage(MSG* pMsg);
    virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
};

//{{AFX_INSERT_LOCATION}}

#endif//__OEFindReplaceDlg_h__
