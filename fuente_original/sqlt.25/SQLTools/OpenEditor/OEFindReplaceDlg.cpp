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

/*
    14.08.2002 bug fix, Find/Replace dialog - garbage in combo box history
    25.09.2002 bug fix, Find/Replace dialog - [:xxxx:] exps have been replaced
    16.12.2002 bug fix, regexp replace fails on \1,...
    16.12.2002 bug fix, Find/Replace dialog auto position
    21.03.2003 bug fix, missing entry in fing what/replace with histoty
    26.05.2003 bug fix, Find/Replace dialog: a replace mode depends on a search direction
    26.05.2003 bug fix, Find/Replace dialog: "Transform backslash expressions" has beed added to handle \t\b\ddd...
    01.06.2004 bug fix, Find/Replace dialog: replace skips empty lines, e.g. "^$"
    30.06.2004 improvement/bug fix, text search/replace interface has been changed
    17.01.2005 (ak) changed exception handling for suppressing unnecessary bug report
    30.03.2005 (ak) bug fix, hanging on trying to replace 'a' with 'aa' in the current selection 
    2017-12-03 bug fix, If you want to replace a string in a selected text with a replacement string bigger than the search string, 
                        it will not replace until the end of the original selected text.
*/

#include "stdafx.h"
#include <algorithm>
#include <COMMON/AppGlobal.h>
#include <COMMON/AppUtilities.h>
#include <COMMON/ExceptionHelper.h>
#include <COMMON/StrHelpers.h>
#include "OpenEditor/OEFindReplaceDlg.h"
#include "OpenEditor/OEView.h"
#include "OpenEditor/OEDocument.h"   // GetSettingsManager - the two Find/Replace settings
#include "Common/MyUtf.h"

#include <MultiMon.h>
extern "C" {
    extern BOOL InitMultipleMonitorStubs(void);
    extern HMONITOR (WINAPI* g_pfnMonitorFromWindow)(HWND, DWORD);
    extern BOOL     (WINAPI* g_pfnGetMonitorInfo)(HMONITOR, LPMONITORINFO);
};

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

using namespace OpenEditor;

/////////////////////////////////////////////////////////////////////////////
// CFindReplaceDlg dialog

CPoint CFindReplaceDlg::m_ptTopLeft(-1,-1);
CFindReplaceDlg* CFindReplaceDlg::m_pInstance = nullptr;

namespace {
    // Find-as-you-type debounce: every search-text/option edit restarts it.
    const UINT_PTR IDT_FIND_PREVIEW   = 0x4650;  // 'FP'
    const UINT     cnFindPreviewDelay = 300;     // ms

    // Opt-in close-on-deactivate: decided in a posted handler, once the
    // activation settled and ::GetFocus() reports the new focus.
    const UINT WM_FIND_CHECK_DEACTIVATE = WM_APP + 0x46;

    // "Editor::General" settings read where they act (default false = modern
    // behaviour); fresh reads apply to an already open dialog immediately.
    bool IsLivePreviewDisabled ()
    {
        return COEDocument::GetSettingsManager().GetGlobalSettings()
            ->GetFindDisableLivePreview();
    }

    bool IsCloseOnDeactivateEnabled ()
    {
        return COEDocument::GetSettingsManager().GetGlobalSettings()
            ->GetFindCloseOnDeactivate();
    }
}

CFindReplaceDlg::CFindReplaceDlg(BOOL replace, COEditorView* pView)
: CDialog(!replace ? IDD_OE_EDIT_FIND : IDD_OE_EDIT_REPLACE, NULL)
{
    m_pEditorView    = pView;
    m_ReplaceMode    = replace;
    m_Modified       = TRUE;
    m_hFindAccel     = NULL;
    m_previewAnchor.column = m_previewAnchor.line = 0;
    m_previewAnchorValid   = false;
    m_countOnlyPreview     = false;
    m_findHighlightInjected = false;
    m_lastKnownSelection.clear();
    m_lastKnownSelectionValid = false;
    m_lastScopeSelState = -1;

    // Selection-scope region: nothing captured, invalid sentinels.
    m_scopeRegion.clear();
    m_scopeRegionValid  = false;
	//{{AFX_DATA_INIT(CFindReplaceDlg)
	//}}AFX_DATA_INIT

    if (m_pEditorView)
    {
        // 26.05.2003 bug fix, Find/Replace dialog: "Transform backslash expressions" has beed added to handle \t\b\ddd...
        m_BackslashExpressions = AfxGetApp()->GetProfileInt(L"Editor", L"BackslashExpressions", FALSE);

        std::wstring buff;
        toPrintableStr(m_pEditorView->GetSearchText(), buff);
        m_SearchText = buff.c_str();

        m_MatchCase      = AfxGetApp()->GetProfileInt(L"Editor", L"SearchMatchCase",      FALSE);
        m_MatchWholeWord = AfxGetApp()->GetProfileInt(L"Editor", L"SearchMatchWholeWord", FALSE);
        m_AllWindows     = AfxGetApp()->GetProfileInt(L"Editor", L"SearchAllWindows",     FALSE);
        m_RegExp         = AfxGetApp()->GetProfileInt(L"Editor", L"SearchRegExp",         FALSE);
        // No persistent direction: Find Next is always forward, Find Previous
        // (Shift+F3) backward; the EOF/BOF wrap notice is unconditional.

        m_WhereReplace   = m_AllWindows ? 2 : 1;

        // Live previews search from where the user stands when the dialog opens.
        CapturePreviewAnchor();
    }
    else
        _ASSERTE(0);
}


void CFindReplaceDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CFindReplaceDlg)
	//}}AFX_DATA_MAP
	DDX_CBString(pDX, IDC_EF_SEARCH_TEXT, m_SearchText);
	DDX_Control(pDX, IDC_EF_SEARCH_TEXT, m_wndSearchText);
    DDX_Radio(pDX, IDC_EF_SCOPE_SELECTION, m_WhereReplace);

    if (m_ReplaceMode)
    {
	    DDX_CBString(pDX, IDC_EF_REPLACE_TEXT, m_ReplaceText);
  	    DDX_Control(pDX, IDC_EF_REPLACE_TEXT, m_wndReplaceText);
    }

    DDX_Check(pDX, IDC_EF_MATCH_CASE, m_MatchCase);
	DDX_Check(pDX, IDC_EF_MATCH_WHOLE_WORD, m_MatchWholeWord);
	DDX_Check(pDX, IDC_EF_REGEXP, m_RegExp);
    DDX_Check(pDX, IDC_EF_TRANSFORM_BACKSLASH_EXPR, m_BackslashExpressions);

    // the scope radio is the only source of m_AllWindows
    if (pDX->m_bSaveAndValidate)
        m_AllWindows = (m_WhereReplace == 2);
}


BEGIN_MESSAGE_MAP(CFindReplaceDlg, CDialog)
	//{{AFX_MSG_MAP(CFindReplaceDlg)
	ON_BN_CLICKED(IDC_EF_FIND_NEXT, OnFindNext)
	ON_BN_CLICKED(IDC_EF_FIND_PREVIOUS, OnFindPrevious)
	ON_BN_CLICKED(IDC_EF_REGEXP, OnChangeSettings)
	ON_CBN_EDITCHANGE(IDC_EF_SEARCH_TEXT, OnSearchText)
	ON_CBN_SELCHANGE(IDC_EF_SEARCH_TEXT, OnSearchText)
	ON_CBN_EDITCHANGE(IDC_EF_REPLACE_TEXT, OnChangeSettings)
	ON_CBN_SELCHANGE(IDC_EF_REPLACE_TEXT, OnChangeSettings)
	ON_BN_CLICKED(IDC_EF_REPLACE, OnReplace)
	ON_BN_CLICKED(IDC_EF_REPLACE_ALL, OnReplaceAll)
	ON_BN_CLICKED(IDC_EF_MATCH_CASE, OnChangeSettings)
	ON_BN_CLICKED(IDC_EF_MATCH_WHOLE_WORD, OnChangeSettings)
	ON_BN_CLICKED(IDC_EF_SCOPE_SELECTION, OnScopeRadio)
	ON_BN_CLICKED(IDC_EF_SCOPE_CURRENT_FILE, OnScopeRadio)
	ON_BN_CLICKED(IDC_EF_SCOPE_ALL_WINDOWS, OnScopeRadio)
	ON_BN_CLICKED(IDC_EF_COUNT, OnCount)
	ON_BN_CLICKED(IDC_EF_MARK_ALL, OnMarkAll)
    //}}AFX_MSG_MAP
    ON_BN_CLICKED(IDC_EF_REGEXP_FIND, OnBnClicked_RegexpFind)
    ON_BN_CLICKED(IDC_EF_REGEXP_REPLACE, OnBnClicked_RegexpReplace)
    ON_COMMAND_RANGE(ID_REGEXP_FIND_TAB, ID_REGEXP_FIND_QUOTED, OnInsertRegexpFind)
    ON_COMMAND_RANGE(ID_REGEXP_REPLACE_WHAT_FIND, ID_REGEXP_REPLACE_TAGEXP_9, OnInsertRegexpReplace)
    ON_WM_CLOSE()   // caption X
    ON_WM_TIMER()   // find-as-you-type debounce
    ON_WM_ACTIVATE()     // opt-in close-on-deactivate (Editor::General setting)
    ON_WM_ACTIVATEAPP()  //   ... including whole-app deactivation
    ON_MESSAGE(WM_FIND_CHECK_DEACTIVATE, OnCheckCloseOnDeactivate)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CFindReplaceDlg message handlers

void CFindReplaceDlg::toPrintableStr (const wchar_t* from, std::wstring& _to)
{
    if (m_BackslashExpressions)
    {
        string buff;
        Common::to_printable_str(Common::str(from).c_str(), buff);
        _to = Common::wstr(buff);
    }
    else
        _to = from;
}

void CFindReplaceDlg::toUnprintableStr (const wchar_t* from, std::wstring& _to, bool skipEscDgt)
{
    if (m_BackslashExpressions)
    {
        string buff;
        Common::to_unprintable_str(Common::str(from).c_str(), buff, skipEscDgt);
        _to = Common::wstr(buff);
    }
    else
        _to = from;
}

void CFindReplaceDlg::AdjustPosition ()
{
    CRect rc, newRc, orgRc;
    GetWindowRect(rc);
    newRc = rc;
    orgRc = rc;

    if (m_pEditorView)
    {
        CRect caretRc;
        if (m_pEditorView->GetCaretPosition(caretRc))
        {
            m_pEditorView->ClientToScreen(&caretRc);

            CRect viewRc;
            m_pEditorView->GetClientRect(&viewRc);
            m_pEditorView->ClientToScreen(&viewRc);

            if (rc.top <= caretRc.top && rc.bottom >= caretRc.bottom)
            {
                // so the current line overlapped with the dialog position
                if ((caretRc.top + caretRc.bottom) / 2 < (viewRc.top + viewRc.bottom) / 2) // top window half
                {
                    newRc.top   = (viewRc.top + viewRc.bottom) / 2;
                    newRc.bottom = newRc.top + rc.Height();
                }
                else // bottom window half
                {
                    newRc.bottom = (viewRc.top + viewRc.bottom) / 2;
                    newRc.top    = newRc.bottom - rc.Height();
                }
                rc = newRc;
            }
        }
    }

    {
        // lets make sure that the dialog is visiable on any monitor
        CRect desktopRc;

        if (g_pfnMonitorFromWindow)
        {
            HMONITOR hmonitor = g_pfnMonitorFromWindow(*this, MONITOR_DEFAULTTONEAREST);
            MONITORINFO monitorInfo;
            memset(&monitorInfo, 0, sizeof(monitorInfo));
            monitorInfo.cbSize = sizeof(monitorInfo);

            if (g_pfnGetMonitorInfo(hmonitor, &monitorInfo))
            {
                desktopRc = monitorInfo.rcMonitor;
            }
            else
                GetDesktopWindow()->GetWindowRect(desktopRc);
        }
        else
            GetDesktopWindow()->GetWindowRect(desktopRc);

        newRc = rc;
        // vert position adjustment
        if (rc.top < desktopRc.top)                  
            newRc.MoveToY(desktopRc.top);
        if (rc.bottom > desktopRc.bottom)               
            newRc.MoveToY(desktopRc.bottom - rc.Height());
        // horz position adjustment
        if (rc.left < desktopRc.left)                  
            newRc.MoveToX(desktopRc.left);
        if (rc.right > desktopRc.right)               
            newRc.MoveToX(desktopRc.right - rc.Width());
    }

    if (orgRc != newRc)
        MoveWindow(newRc, TRUE);
}

void CFindReplaceDlg::SaveOption ()
{
    if (m_Modified)
    {

        Common::AppSaveHistory(m_wndSearchText, L"Editor", L"SearchText", 10);
        // 21.03.2003 bug fix, missing entry in fing what/replace with histoty
        Common::AppRestoreHistory(m_wndSearchText, L"Editor", L"SearchText", 10);

        if (m_ReplaceMode)
        {
            Common::AppSaveHistory(m_wndReplaceText, L"Editor", L"ReplaceText", 10);
            // 21.03.2003 bug fix, missing entry in fing what/replace with histoty
            Common::AppRestoreHistory(m_wndReplaceText, L"Editor", L"ReplaceText", 10);
        }

        if (m_pEditorView)
        {
            std::wstring buff;
            toUnprintableStr(m_SearchText, buff, false);

            m_pEditorView->SetSearchText(buff.c_str());
            m_pEditorView->SetSearchOption(m_MatchWholeWord ? true : false,
                                     m_MatchCase      ? true : false,
                                     m_RegExp         ? true : false,
                                     m_AllWindows     ? true : false);

            AfxGetApp()->WriteProfileInt(L"Editor", L"SearchMatchCase",      m_MatchCase     );
            AfxGetApp()->WriteProfileInt(L"Editor", L"SearchMatchWholeWord", m_MatchWholeWord);
            AfxGetApp()->WriteProfileInt(L"Editor", L"SearchAllWindows",     m_AllWindows    );
            AfxGetApp()->WriteProfileInt(L"Editor", L"SearchRegExp",         m_RegExp        );
            // "Editor\MessageOnEOF" and "Editor\SearchDirection" are orphaned:
            // neither the wrap notice nor the direction is a setting any more.

            AfxGetApp()->WriteProfileInt(L"Editor", L"BackslashExpressions", m_BackslashExpressions);
        }

        m_Modified = FALSE;
    }
}

/////////////////////////////////////////////////////////////////////////////
// Modeless singleton lifecycle

BOOL CFindReplaceDlg::RefreshFromView ()
{
    BOOL enableWhereReplace =
        (!m_pEditorView->IsSelectionEmpty()
         && m_pEditorView->GetBlockMode() != ebtColumn
         && !m_AllWindows) ? TRUE : FALSE;

    Square blk;
    m_pEditorView->GetSelection(blk);
    m_WhereReplace = m_AllWindows ? 2
        : (enableWhereReplace && blk.start.line != blk.end.line) ? 0 : 1;

    return enableWhereReplace;
}

void CFindReplaceDlg::RestoreFocusToSearchText ()
{
    // Find() MDIActivates the hit's pane on every search; hand the focus back
    // so Enter stays repeatable.
    if (::IsWindow(m_wndSearchText.GetSafeHwnd())
     && ::IsWindowVisible(m_wndSearchText.GetSafeHwnd()))
        m_wndSearchText.SetFocus();
}

/////////////////////////////////////////////////////////////////////////////
// Find-as-you-type: edits restart one debounce; the tick previews the typed
// text from a stable anchor, without touching focus or committed search state.

void CFindReplaceDlg::CapturePreviewAnchor ()
{
    // Every committed path funnels here: kill an in-flight debounce, which was
    // armed against the pre-commit text and would clobber the committed result.
    if (IsWindow(m_hWnd))
        KillTimer(IDT_FIND_PREVIEW);   // a no-op when the timer is not running

    // Drop the injection flag: a committed search hands the tint to the
    // selection-driven maintenance. Do not clear it - retargets run here too.
    m_findHighlightInjected = false;

    m_previewAnchorValid = false;

    // Invalidate the anchor's stable identity; re-snapshotted below.
    m_previewAnchorLineId   = OpenEditor::LineId();
    m_previewAnchorActionId = OpenEditor::SequenceId();

    // Same for the known-selection snapshot.
    m_lastKnownSelectionValid = false;

    if (m_pEditorView && ::IsWindow(m_pEditorView->GetSafeHwnd()))
    {
        Square blk;
        m_pEditorView->GetSelection(blk);
        blk.normalize();
        m_previewAnchor      = blk.start;
        m_previewAnchorValid = true;

        // Remember the selection the anchor came from: a later difference means
        // the user moved the caret and re-anchors the preview.
        m_lastKnownSelection      = blk;
        m_lastKnownSelectionValid = true;

        // Snapshot the anchor's identity: LineId (survives edits above) + the
        // action stamp. Range-guarded: an invalid LineId degrades to a re-capture.
        m_previewAnchorActionId = m_pEditorView->GetActionId();
        if (m_previewAnchor.line >= 0 && m_previewAnchor.line < m_pEditorView->GetLineCount())
            m_previewAnchorLineId = m_pEditorView->GetLineId(m_previewAnchor.line);
    }
}

// Repairs m_previewAnchor after document edits: re-resolve the anchor line's
// stable LineId to its current index; a deleted line re-captures the anchor.
void CFindReplaceDlg::RefreshStalePreviewAnchor ()
{
    if (!m_previewAnchorValid)
        return;

    if (!m_pEditorView || !::IsWindow(m_pEditorView->GetSafeHwnd()))
        return;

    const OpenEditor::SequenceId actionId = m_pEditorView->GetActionId();
    if (actionId == m_previewAnchorActionId)
        return;   // no data-updating operation since the capture: the anchor stands

    int line = -1;
    if (m_previewAnchorLineId.Valid() && m_pEditorView->FindById(m_previewAnchorLineId, line))
    {
        m_previewAnchor.line    = line;   // same logical line, current index
        m_previewAnchorActionId = actionId;
        return;
    }

    // The anchor line no longer exists: re-anchor at the user's current
    // selection and re-snapshot the identity (CapturePreviewAnchor does both).
    CapturePreviewAnchor();
}

// Cursor-relativity: a selection the dialog did not make means the user moved
// the caret, so re-anchor there and let counter/preview predict Enter.
void CFindReplaceDlg::RefreshAnchorForMovedSelection ()
{
    if (!m_previewAnchorValid || !m_lastKnownSelectionValid)
        return;

    if (!m_pEditorView || !::IsWindow(m_pEditorView->GetSafeHwnd()))
        return;

    Square blk;
    m_pEditorView->GetSelection(blk);
    blk.normalize();

    if (blk.start == m_lastKnownSelection.start && blk.end == m_lastKnownSelection.end)
        return;   // the selection is where the dialog last left it: the anchor stands

    CapturePreviewAnchor();
}

// Snapshot the view's selection as "the one the dialog last knew"; a navigating
// tick calls this after PreviewSearch moved the selection onto its hit.
void CFindReplaceDlg::RememberViewSelection ()
{
    m_lastKnownSelectionValid = false;

    if (m_pEditorView && ::IsWindow(m_pEditorView->GetSafeHwnd()))
    {
        m_pEditorView->GetSelection(m_lastKnownSelection);
        m_lastKnownSelection.normalize();
        m_lastKnownSelectionValid = true;
    }
}

/////////////////////////////////////////////////////////////////////////////
// VS-implicit selection region: the "Selection" scope is a captured square
// (painted as its own view layer) that preview, counter and batch all stay inside.

void CFindReplaceDlg::CaptureScopeRegion ()
{
    ReleaseScopeRegion();   // drop a previous region first (no-op when none)

    if (m_AllWindows || m_WhereReplace != 0)
        return;             // the region exists only in selection scope

    if (!m_pEditorView || !::IsWindow(m_pEditorView->GetSafeHwnd()))
        return;

    if (m_pEditorView->IsSelectionEmpty() || m_pEditorView->GetBlockMode() == ebtColumn)
        return;             // nothing usable to capture - the caller's radio/scope
                            // logic falls back; no band, no scoped ticks

    Square blk;
    m_pEditorView->GetSelection(blk);
    blk.normalize();
    if (blk.is_empty())
        return;

    m_scopeRegion      = blk;
    m_scopeRegionValid = true;

    // Stable identity, as for the preview anchor: endpoint LineIds re-resolve to
    // current indices, the action stamp detects edits (range-guarded).
    m_scopeRegionActionId = m_pEditorView->GetActionId();
    if (blk.start.line >= 0 && blk.start.line < m_pEditorView->GetLineCount())
        m_scopeRegionStartId = m_pEditorView->GetLineId(blk.start.line);
    if (blk.end.line >= 0 && blk.end.line < m_pEditorView->GetLineCount())
        m_scopeRegionEndId = m_pEditorView->GetLineId(blk.end.line);

    // Record the capture as the last known selection, else the next idle pass
    // would misread it as user movement and release/recapture in a loop.
    RememberViewSelection();

    m_pEditorView->SetScopeRegion(blk);
    // Suppress the document-wide matching-text tint while the region is armed;
    // restored in ReleaseScopeRegion.
    m_pEditorView->SuppressMatchingTextHighlight(true);
}

void CFindReplaceDlg::ReleaseScopeRegion ()
{
    if (!m_scopeRegionValid)
        return;

    m_scopeRegionValid    = false;
    m_scopeRegionStartId  = OpenEditor::LineId();
    m_scopeRegionEndId    = OpenEditor::LineId();
    m_scopeRegionActionId = OpenEditor::SequenceId();

    if (m_pEditorView && ::IsWindow(m_pEditorView->GetSafeHwnd()))
    {
        // The tint suppression dies with the region: the view's selection-derived
        // matching-text highlight returns from the next caret change on.
        m_pEditorView->SuppressMatchingTextHighlight(false);
        m_pEditorView->ClearScopeRegion();
    }
}

// Region-side twin of RefreshStalePreviewAnchor: re-resolve both endpoint
// LineIds after an edit; a dead endpoint falls back to whole file visibly.
void CFindReplaceDlg::RefreshScopeRegionStaleness ()
{
    if (!m_scopeRegionValid)
        return;

    if (!m_pEditorView || !::IsWindow(m_pEditorView->GetSafeHwnd()))
        return;

    const OpenEditor::SequenceId actionId = m_pEditorView->GetActionId();
    if (actionId == m_scopeRegionActionId)
        return;             // no data-updating operation since the capture

    int startLine = -1, endLine = -1;
    if (m_scopeRegionStartId.Valid() && m_pEditorView->FindById(m_scopeRegionStartId, startLine)
    &&  m_scopeRegionEndId.Valid()   && m_pEditorView->FindById(m_scopeRegionEndId, endLine))
    {
        m_scopeRegion.start.line = startLine;   // same logical lines, current indices
        m_scopeRegion.end.line   = endLine;
        m_scopeRegionActionId    = actionId;
        m_pEditorView->SetScopeRegion(m_scopeRegion);   // re-arm, column-aware as captured
        return;
    }

    ReleaseScopeRegion();
    m_lastScopeSelState = -1;   // re-classify the live selection on the next idle pass
    ApplyScopeRadio(1);
}

// Shared tail of every scope change: radios + enablement, set directly rather
// than via UpdateData(FALSE), which would rewrite the combos mid-edit.
void CFindReplaceDlg::ApplyScopeRadio (int scope)
{
    if (!::IsWindow(m_hWnd) || scope == m_WhereReplace)
        return;

    m_WhereReplace = scope;

    // BS_AUTORADIOBUTTON keeps the trio exclusive; set all three explicitly
    // anyway so the state never depends on message order.
    ::SendMessage(::GetDlgItem(m_hWnd, IDC_EF_SCOPE_SELECTION),
                  BM_SETCHECK, scope == 0 ? BST_CHECKED : BST_UNCHECKED, 0L);
    ::SendMessage(::GetDlgItem(m_hWnd, IDC_EF_SCOPE_CURRENT_FILE),
                  BM_SETCHECK, scope == 1 ? BST_CHECKED : BST_UNCHECKED, 0L);
    ::SendMessage(::GetDlgItem(m_hWnd, IDC_EF_SCOPE_ALL_WINDOWS),
                  BM_SETCHECK, scope == 2 ? BST_CHECKED : BST_UNCHECKED, 0L);

    SetupButtons();   // Find Next / Replace enablement depends on m_WhereReplace
}

void CFindReplaceDlg::RestartPreviewTimer (bool countOnly)
{
    if (!::IsWindow(m_hWnd))
        return;

    KillTimer(IDT_FIND_PREVIEW);   // a no-op when the timer is not running

    m_countOnlyPreview = countOnly;

    if (IsLivePreviewDisabled())   // live search switched off in settings:
        return;                    // no debounce is armed at all

    SetTimer(IDT_FIND_PREVIEW, cnFindPreviewDelay, NULL);
}

// Undo ONLY the dialog's own tint injection - the flag tells it apart from the
// view's selection-derived highlight, which is never touched.
void CFindReplaceDlg::ClearInjectedFindHighlight ()
{
    // The match overlay shares this helper's lifecycle; it is exclusively the
    // dialog's, so no ownership flag guards it.
    if (m_pEditorView && ::IsWindow(m_pEditorView->GetSafeHwnd()))
        m_pEditorView->ClearMatchOverlay();

    if (!m_findHighlightInjected)
        return;

    m_findHighlightInjected = false;

    if (m_pEditorView && ::IsWindow(m_pEditorView->GetSafeHwnd()))
        m_pEditorView->HighlightMatchingText(NULL);
}

void CFindReplaceDlg::UpdateTitleCounter (const CString& counterText)
{
    // Caption mirror of the "n of m" counter (the dialog floats away from the
    // status line). Empty text restores the plain template caption.
    if (!IsWindow(m_hWnd) || m_baseCaption.IsEmpty())
        return;

    if (counterText == m_titleCounter)
        return;

    m_titleCounter = counterText;

    CString caption(m_baseCaption);
    if (!m_titleCounter.IsEmpty())
    {
        caption += _T(" - ");
        caption += m_titleCounter;
    }
    SetWindowText(caption);
}

void CFindReplaceDlg::RunFindPreview ()
{
    KillTimer(IDT_FIND_PREVIEW);

    if (IsLivePreviewDisabled())   // belt and braces: the setting may have been
        return;                    // flipped while a debounce was in flight

    if (!m_previewAnchorValid)
    {
        UpdateTitleCounter(CString());   // no live counting: no stale caption counter
        return;
    }

    if (!m_pEditorView || !::IsWindow(m_pEditorView->GetSafeHwnd()))
        return;

    // Re-anchor at the live selection first (the user may have moved the caret),
    // so the counter predicts Enter; the caret beats the LineId staleness below.
    RefreshAnchorForMovedSelection();

    // The document may have been edited since the capture: re-resolve the anchor.
    RefreshStalePreviewAnchor();

    CString text;
    m_wndSearchText.GetWindowText(text);

    if (text.IsEmpty())
    {
        UpdateTitleCounter(CString());
        ClearInjectedFindHighlight();   // the combo no longer describes the tint
        return;
    }

    // Count-only ticks: the dialog-open probe must not navigate, and under the
    // All Windows scope counting must not bounce the selection across panes.
    const bool allWindows = (m_AllWindows != FALSE);
    const bool countOnly  = allWindows || m_countOnlyPreview;
    m_countOnlyPreview = false;   // one-shot: the next typing tick navigates again

    // In selection scope with an armed region the tick works inside it: the scan
    // re-bases to the region top, only fully-contained matches count and select.
    if (!allWindows && m_WhereReplace == 0)
        RefreshScopeRegionStaleness();   // the document may have been edited since the last pass
    const bool scoped = !allWindows && m_WhereReplace == 0 && m_scopeRegionValid;
    if (scoped)
        m_previewAnchor = m_scopeRegion.start;

    std::wstring buff;
    toUnprintableStr(text, buff, false);

    bool found = false;
    Square foundRaw;      // the hit in the count scan's raw index space
    CString counterText;  // "n of m" when reportable, empty otherwise

    // "Highlight all occurrences" squares collected by the count scan below (raw
    // index space, capped with the counter; scoped ticks collect region-internal).
    std::vector<Square> overlayMatches;

    try
    {
        found = m_pEditorView->PreviewSearch(buff.c_str(),
                                             m_MatchWholeWord != FALSE,
                                             m_MatchCase      != FALSE,
                                             m_RegExp         != FALSE,
                                             m_previewAnchor,
                                             &foundRaw,
                                             !countOnly /*selectHit*/,
                                             scoped ? &m_scopeRegion : 0 /*regionPos*/);

        // A navigating tick moved the selection itself: record it, else the next
        // tick would misread it as user movement and re-anchor (walking matches).
        if (!countOnly)
            RememberViewSelection();

        // The counter re-compiles the pattern on its own transient searcher;
        // counting inside this try keeps the mid-typing exception policy together.
        if (found)
        {
            m_pEditorView->CountSearchMatches(buff.c_str(),
                                              m_MatchWholeWord != FALSE,
                                              m_MatchCase      != FALSE,
                                              m_RegExp         != FALSE,
                                              foundRaw, counterText,
                                              scoped ? &m_scopeRegion : 0 /*regionPos*/,
                                              &overlayMatches /*collect*/);

            // Hand the collected squares to the view on every found tick, so all
            // other matches stay visible; a timed-out scan leaves a partial set.
            m_pEditorView->SetMatchOverlay(overlayMatches);

            // Same suffixes as the committed path: the counted scope is the
            // target document ("All Windows") or the armed region (scoped).
            if (!counterText.IsEmpty())
            {
                if (allWindows)
                    counterText += _T(" in this file");
                else if (scoped)
                    counterText += _T(" in selection");
            }
        }
    }
    catch (...)
    {
        // An invalid regex mid-typing is the normal transient state: skip the
        // tick silently; committed paths still report a genuine failure.
        UpdateTitleCounter(CString());   // the old count no longer describes the text
        ClearInjectedFindHighlight();    // ... and neither does the tint
        return;
    }

    if (found)
    {
        // A count-only tick gives no visual anchor, so tint the searched text -
        // but only unscoped, and only when the overlay collected no squares.
        if (countOnly && !scoped && overlayMatches.empty())
        {
            m_pEditorView->HighlightMatchingText(buff.c_str());
            m_findHighlightInjected = true;
        }

        // The counter when one is reportable, otherwise an empty status (capped
        // or abandoned scan). The caption mirror shows the same.
        Global::SetStatusText(counterText);
        UpdateTitleCounter(counterText);
    }
    else
    {
        // "Not found": drop any stale tint of an earlier tick.
        ClearInjectedFindHighlight();

        // RepeadSearch's miss wording, without the beep ("not found YET" is
        // routine while typing); the suffix names the narrower scanned scope.
        CString message("Cannot find the string \'");
        message += buff.c_str();
        if (allWindows)
            message += "\' in this file.";
        else if (scoped)
            message += "\' in selection.";
        else
            message += "\'.";
        Global::SetStatusText(message);
        UpdateTitleCounter(CString());
    }
}

void CFindReplaceDlg::OnTimer (UINT_PTR nIDEvent)
{
    if (nIDEvent == IDT_FIND_PREVIEW)
        RunFindPreview();   // one-shot: RunFindPreview kills the timer first

    CDialog::OnTimer(nIDEvent);
}

void CFindReplaceDlg::BuildFindAccelTable ()
{
    if (m_hFindAccel)
    {
        ::DestroyAcceleratorTable(m_hFindAccel);
        m_hFindAccel = NULL;
    }

    CFrameWnd* pFrame = DYNAMIC_DOWNCAST(CFrameWnd, AfxGetMainWnd());
    if (!pFrame || !pFrame->m_hAccelTable)
        return;

    // Copy only the find family from the frame's remapped accel table: the frame
    // sees no keys here, and the whole table would hijack Ctrl+C/V/A in the combos.
    int count = ::CopyAcceleratorTable(pFrame->m_hAccelTable, NULL, 0);
    if (count <= 0)
        return;

    std::vector<ACCEL> src(count);
    if (::CopyAcceleratorTable(pFrame->m_hAccelTable, src.data(), count) <= 0)
        return;

    std::vector<ACCEL> sel;
    for (int i = 0; i < count; i++)
    {
        switch (src[i].cmd)
        {
        case ID_EDIT_FIND:
        case ID_EDIT_REPLACE:
        case ID_EDIT_FIND_NEXT:
        case ID_EDIT_FIND_PREVIOUS:
        case ID_EDIT_FIND_SELECTED_NEXT:
        case ID_EDIT_FIND_SELECTED_PREVIOUS:
            sel.push_back(src[i]);
            break;
        }
    }

    if (!sel.empty())
        m_hFindAccel = ::CreateAcceleratorTable(sel.data(), (int)sel.size());
}

void CFindReplaceDlg::ShowModeless (COEditorView* pView, BOOL replace)
{
    if (!pView || !::IsWindow(pView->GetSafeHwnd()))
        return;

    if (m_pInstance)
    {
        if (m_pInstance->m_ReplaceMode == replace)
        {
            m_pInstance->RetargetFull(pView);
            return;
        }

        // Find and Replace are two templates: destroy and recreate. Reachable
        // from the dialog's own PreTranslateMessage, so only static state after.
        m_pInstance->CloseDialog();
        if (m_pInstance)
            return;   // destroy did not complete; leave the existing dialog alone
    }

    CFindReplaceDlg* pDlg = new CFindReplaceDlg(replace, pView);
    m_pInstance = pDlg;

    // Owned by the main frame: no taskbar button of its own, follows the app's
    // minimize/restore. Create() runs OnInitDialog (position + subclassing).
    if (!pDlg->Create(replace ? IDD_OE_EDIT_REPLACE : IDD_OE_EDIT_FIND, AfxGetMainWnd()))
    {
        m_pInstance = nullptr;
        delete pDlg;   // no WM_NCDESTROY follows a failed Create, so PostNcDestroy will not run
        return;
    }

    pDlg->BuildFindAccelTable();

    pDlg->ShowWindow(SW_SHOW);   // the template no longer carries WS_VISIBLE
    pDlg->SetForegroundWindow();
    pDlg->RestoreFocusToSearchText();
    pDlg->m_edtSearchText.SetSel(0, -1);   // typing immediately replaces the search text
}

void CFindReplaceDlg::NotifyViewDestroying (COEditorView* pView)
{
    if (!m_pInstance || m_pInstance->m_pEditorView != pView)
        return;

    // Called from COEditorView::OnDestroy, while the sibling links are intact:
    // prefer the surviving split pane, close only when nothing is left.
    COEditorView* pSibling = pView->GetSiblingView();
    if (pSibling && ::IsWindow(pSibling->GetSafeHwnd()))
        m_pInstance->RetargetQuiet(pSibling);
    else
        m_pInstance->CloseDialog(false);   // do not SetFocus into a window mid-destruction
}

void CFindReplaceDlg::RetargetFull (COEditorView* pView)
{
    // Persist pending edits that were aimed at the previous target.
    if (UpdateData())
        SaveOption();

    // Clear the injected tint while m_pEditorView still points at the old view.
    ClearInjectedFindHighlight();

    // The region belongs to the old view: drop it before the swap.
    ReleaseScopeRegion();

    m_pEditorView = pView;

    // Previews anchor at the new target's selection from now on.
    CapturePreviewAnchor();

    // The invoker just stored the word/block under the cursor into the view's
    // search text: pick it into the combo, as the constructor does on create.
    std::wstring buff;
    toPrintableStr(pView->GetSearchText(), buff);
    m_SearchText = buff.c_str();
    m_Modified   = TRUE;

    BOOL enableWhereReplace = RefreshFromView();
    m_lastScopeSelState = -1;   // re-classify the new view on first idle

    // Same rule as OnInitDialog: a multi-line selection derives selection scope.
    if (m_WhereReplace == 0)
        CaptureScopeRegion();

    UpdateData(FALSE);   // push m_SearchText and the option state into the controls

    GetDlgItem(IDC_EF_SCOPE_SELECTION)->EnableWindow(enableWhereReplace);

    SetupButtons();
    BuildFindAccelTable();   // the keymap may have been remapped since the last show

    ShowWindow(SW_SHOW);
    SetForegroundWindow();
    AdjustPosition();
    RestoreFocusToSearchText();
    m_edtSearchText.SetSel(0, -1);

    // As in OnInitDialog: the combo was refilled programmatically (no EN_CHANGE),
    // so arm a count-only tick - reopening Find must not move the editor.
    RestartPreviewTimer(true /*countOnly*/);
}

void CFindReplaceDlg::RetargetQuiet (COEditorView* pView, bool reposition)
{
    // The user did not ask for the dialog here: no foreground, no focus, no
    // combo refresh. reposition=false leaves it where the user parked it.

    // Drop any injected tint while m_pEditorView still points at the old view.
    ClearInjectedFindHighlight();

    // The region belonged to the old view; the next idle re-classifies the new one.
    ReleaseScopeRegion();

    m_pEditorView = pView;

    // Re-anchor previews at the new view's selection.
    CapturePreviewAnchor();

    if (reposition && ::IsWindow(m_hWnd) && IsWindowVisible())
        AdjustPosition();
}

// The editor view the user works in: the MDI-active child's active view.
// MDIGetActive tracks the active child independently of the keyboard focus.
COEditorView* CFindReplaceDlg::ActiveEditorView ()
{
    CMDIFrameWnd* pMDIFrame = DYNAMIC_DOWNCAST(CMDIFrameWnd, AfxGetMainWnd());
    if (!pMDIFrame || !::IsWindow(pMDIFrame->GetSafeHwnd()))
        return nullptr;

    CMDIChildWnd* pChild = pMDIFrame->MDIGetActive();
    if (!pChild || !::IsWindow(pChild->GetSafeHwnd()))
        return nullptr;

    // NULL when the active view is not an editor pane (results grid, booklet
    // tab): the dialog then keeps its current target.
    return DYNAMIC_DOWNCAST(COEditorView, pChild->GetActiveView());
}

void CFindReplaceDlg::RetargetToActivatedView (COEditorView* pView)
{
    if (!::IsWindow(m_hWnd) || !IsWindowVisible())
        return;

    // Already the target - the common case (a click into the same pane).
    if (!pView || pView == m_pEditorView || !::IsWindow(pView->GetSafeHwnd()))
        return;

    // "All Windows" opts out: that scope moves the target to the view showing
    // the hit, which an activation-driven retarget would silently undo.
    if (m_AllWindows)
        return;

    // Quiet: no foreground, no focus steal, no move, no combo refill - text and
    // options stay, so the same search continues in the newly activated file.
    RetargetQuiet(pView, false /*reposition*/);

    // The next idle pass re-derives the scope radio from the new selection.
    m_lastScopeSelState = -1;

    // The caption counted the old document: clear it and arm a count-only tick,
    // so the new file's "n of m" appears without moving the caret.
    UpdateTitleCounter(CString());
    RestartPreviewTimer(true /*countOnly*/);
}

void CFindReplaceDlg::NotifyViewActivated (COEditorView* pView)
{
    // Cheap early-out: activating the already targeted view must cost nothing.
    if (!m_pInstance || m_pInstance->m_pEditorView == pView)
        return;

    m_pInstance->RetargetToActivatedView(pView);
}

void CFindReplaceDlg::CloseDialog (bool restoreFocus)
{
    // Remember the position for the next show (m_ptTopLeft is app-lifetime,
    // as it was in the modal days).
    CRect rc;
    GetWindowRect(&rc);
    m_ptTopLeft = rc.TopLeft();

    // Modeless users expect checkbox/option edits to survive a close even
    // without running a search; SaveOption is a no-op when nothing changed.
    if (UpdateData())
        SaveOption();

    // No preview may fire into a dying window (a WM_TIMER may be queued).
    KillTimer(IDT_FIND_PREVIEW);

    COEditorView* pView = m_pEditorView;

    // The dialog's transient tint does not outlive the dialog.
    ClearInjectedFindHighlight();

    // Nor does the scope region - release while the target pointer is still valid.
    ReleaseScopeRegion();

    // Synchronous: WM_NCDESTROY -> PostNcDestroy clears m_pInstance and
    // deletes this object. Past this line only locals may be touched.
    DestroyWindow();

    // Modeless must hand the focus back explicitly; skipped when the close is
    // driven by the target view's own destruction.
    if (restoreFocus && pView && ::IsWindow(pView->GetSafeHwnd()))
    {
        CMDIChildWnd* pChild = DYNAMIC_DOWNCAST(CMDIChildWnd, pView->GetParentFrame());
        if (pChild)
            pChild->MDIActivate();   // bring the document that was searched to the front
        pView->SetFocus();
    }
}

void CFindReplaceDlg::OnFindNext()
{
    RunCommittedSearch(false /*backward*/, true /*restoreFocus*/);
}

void CFindReplaceDlg::OnFindPrevious()
{
    RunCommittedSearch(true /*backward*/, true /*restoreFocus*/);
}

bool CFindReplaceDlg::RunCommittedSearch (bool backward, bool restoreFocus)
{
    CWaitCursor wait;
    bool searched = false;

    // Drop the count-only tint while the pointer still targets the view that
    // carries it (an "All Windows" hit reassigns the pointer below).
    ClearInjectedFindHighlight();

    // A committed search honors the selection scope: with a region armed,
    // Enter/F3/Find Next step inside it (staleness refreshed first).
    RefreshScopeRegionStaleness();
    const bool scoped = m_scopeRegionValid && m_WhereReplace == 0 && !m_AllWindows;

    try { EXCEPTION_FRAME;

        UpdateData();
        SaveOption();

        if (m_pEditorView && !m_SearchText.IsEmpty())
        {
            CString counterText;

            if (scoped)
            {
                RunScopedSearch(backward, counterText);
            }
            else
            {
                // Belt: a region armed under a non-Selection radio must not survive a
                // whole-file search - it would claim a scope the search ignored.
                ReleaseScopeRegion();

                COEditorView* pView;
                // The view that RUNS the search caches the "n of m" counter;
                // remember it - an "All Windows" hit reassigns m_pEditorView below.
                COEditorView* pSearchView = m_pEditorView;
                // Direction travels per-command: Find Next forward, Shift+F3 back.
                if (m_pEditorView->RepeadSearch(backward, pView))
                {
                    m_pEditorView = pView;
                    AdjustPosition();
                }

                // Mirror the committed counter into the caption ("" = plain);
                // shown even when the status line carries the wrap notice.
                counterText = pSearchView->GetSearchCounterText();
            }

            // A committed search moves the anchor: subsequent typing previews
            // from the new match, and Enter keeps advancing.
            CapturePreviewAnchor();

            UpdateTitleCounter(counterText);

            // A hit pulls the focus into the editor pane; hand it back so
            // Enter/F3 repeat - unless F3 came from the pane itself.
            if (restoreFocus)
                RestoreFocusToSearchText();

            searched = true;
        }
    }
    _OE_DEFAULT_HANDLER_;

    return searched;
}

// Scoped committed step ("In Selection" navigation): PreviewSearch's transient
// searcher discipline applies, direction and region ride on its parameters.
bool CFindReplaceDlg::RunScopedSearch (bool backward, CString& counterText)
{
    counterText.Empty();

    std::wstring buff;
    toUnprintableStr(m_SearchText, buff, false);
    if (buff.empty())
        return false;

    const bool wholeWords = m_MatchWholeWord != FALSE;
    const bool matchCase  = m_MatchCase      != FALSE;
    const bool regExpr    = m_RegExp         != FALSE;

    // Step off the current hit: forward from its END, backward from its START -
    // the same basis on which RepeadSearch skips the selection it just made.
    Square blk;
    m_pEditorView->GetSelection(blk);
    blk.normalize();

    // Fresh-start rule: while the selection still COVERS the whole region there
    // is no hit to step off, so start clean at the region top, with no wrap notice.
    const bool coversRegion = !(m_scopeRegion.start < blk.start)   // blk.start <= region.start
                           && !(blk.end < m_scopeRegion.end);      // blk.end   >= region.end

    Position from;
    if (coversRegion)
        from = backward ? m_scopeRegion.end : m_scopeRegion.start;
    else
        from = backward ? blk.start : blk.end;

    Square foundRaw;
    bool wrapped = false;
    bool found = m_pEditorView->PreviewSearch(buff.c_str(), wholeWords, matchCase, regExpr,
                                              from, &foundRaw, true /*selectHit*/,
                                              &m_scopeRegion, backward);
    if (!found && !coversRegion)
    {
        // Ran past the region edge: wrap to its other end, as RepeadSearch does
        // at the file bounds; the status text below reports the wrap.
        from = backward ? m_scopeRegion.end : m_scopeRegion.start;
        found = m_pEditorView->PreviewSearch(buff.c_str(), wholeWords, matchCase, regExpr,
                                             from, &foundRaw, true /*selectHit*/,
                                             &m_scopeRegion, backward);
        wrapped = found;
    }

    if (!found)
    {
        // No match inside the region at all: committed-miss feedback, the scope
        // stays untouched.
        CString message("Cannot find the string \'");
        message += buff.c_str();
        message += "\' in selection.";
        Global::SetStatusText(message);
        MessageBeep((UINT)-1);
        return false;
    }

    // Make the pane visible as the whole-file path does: a scoped step into a
    // background document must not be invisible.
    CWnd* pFrame = m_pEditorView->GetParent();
    while (pFrame && !pFrame->IsKindOf(RUNTIME_CLASS(CMDIChildWnd)))
        pFrame = pFrame->GetParent();
    if (pFrame)
        ((CMDIChildWnd*)pFrame)->MDIActivate();

    AdjustPosition();   // the hit moved: the dialog must not cover it

    // Counter and overlay from the same scoped scan the live tick runs.
    std::vector<Square> overlayMatches;
    m_pEditorView->CountSearchMatches(buff.c_str(), wholeWords, matchCase, regExpr,
                                      foundRaw, counterText, &m_scopeRegion, &overlayMatches);
    m_pEditorView->SetMatchOverlay(overlayMatches);
    if (!counterText.IsEmpty())
        counterText += _T(" in selection");

    // RepeadSearch's precedence: the wrap notice wins in the status line; the
    // caption mirror shows the counter either way.
    if (wrapped)
        Global::SetStatusText(backward ? "Passed the beginning of the selection."
                                       : "Passed the end of the selection.");
    else
        Global::SetStatusText(counterText);

    return true;
}

// static
bool CFindReplaceDlg::RouteViewFindCommand (COEditorView* pView, bool backward)
{
    // With the dialog open on pView, F3/Shift+F3 must continue the combo's
    // (possibly uncommitted) text: commit and search exactly like Enter does.
    CFindReplaceDlg* pDlg = m_pInstance;

    if (!pDlg || pDlg->m_pEditorView != pView || !::IsWindow(pDlg->GetSafeHwnd()))
        return false;   // no dialog, or it targets another view: caller falls through to a plain RepeadSearch

    // Take the focus back only when the dialog had it: an F3 pressed while
    // editing in the pane must not steal focus, yet still continues the text.
    CWnd* pFocus = CWnd::GetFocus();
    HWND hFocus  = pFocus ? pFocus->GetSafeHwnd() : nullptr;
    bool dlgHasFocus = hFocus
        && (hFocus == pDlg->GetSafeHwnd()
            || ::IsChild(pDlg->GetSafeHwnd(), hFocus));

    return pDlg->RunCommittedSearch(backward, dlgHasFocus /*restoreFocus*/);
}

// static
bool CFindReplaceDlg::IsOpenFor (COEditorView* pView)
{
    // The condition RouteViewFindCommand requires, exposed without side effects
    // so command-UI handlers can ask it.
    return pView
        && m_pInstance
        && m_pInstance->m_pEditorView == pView
        && ::IsWindow(m_pInstance->GetSafeHwnd());
}

void CFindReplaceDlg::OnReplace ()
{
    CWaitCursor wait;

    try { EXCEPTION_FRAME;

        UpdateData();
        SaveOption();

        Square blk;

        if (m_pEditorView)
        {
            m_pEditorView->GetSelection(blk);
            blk.normalize();

            // 01.06.2004 bug fix, Find/Replace dialog: replace skips empty lines, e.g. "^$"
            if (/*!blk.is_empty() &&*/ blk.start.line == blk.end.line)
            {
                FindCtx ctx;
                // 16.12.2002 bug fix, regexp replace fails on \1,...
                toUnprintableStr(m_ReplaceText, ctx.m_text, m_RegExp ? true : false /*skipEscDgt*/);
                ctx.m_line  = blk.start.line;
                ctx.m_start = blk.start.column;
                ctx.m_end   = blk.end.column;
                m_pEditorView->Replace(ctx);
            }
        }

        OnFindNext();
    }
    _OE_DEFAULT_HANDLER_;
}

void CFindReplaceDlg::OnReplaceAll ()
{
    SearchBatch(esbReplace);
}

void CFindReplaceDlg::OnCount ()
{
    SearchBatch(esbCount);
}

void CFindReplaceDlg::OnMarkAll ()
{
    SearchBatch(esbMark);
}

void CFindReplaceDlg::SearchBatch (OpenEditor::ESearchBatch mode)
{
    CWaitCursor wait;

    try { EXCEPTION_FRAME;

        int counter = 0;

        // Belt and braces: re-run the idle scope sync so the batch never depends
        // on message-pump timing (transition-only, the user's choice survives).
        RefreshScopeIdle();

        UpdateData();
        SaveOption();

        if (m_pEditorView)
        {
            // != 0: batch through the committed searcher; scope 2's committed
            // m_AllWindows makes DoBatch walk every window (confirm below).
            if (m_WhereReplace != 0 || mode == esbCount || mode == esbMark )
            {
                if (!m_AllWindows || mode == esbCount || mode == esbMark
                || AfxMessageBox(L"All occurances will be replaced for all windows! Do You confirm?", MB_OKCANCEL|MB_ICONWARNING) == IDOK)
                {
                    std::wstring buff;
                    toUnprintableStr(m_ReplaceText, buff, false);
                    counter = m_pEditorView->SearchBatch(buff.c_str(), mode);
                }
            }
            else // replace in selection
            {
                // With a region armed the batch uses the CAPTURED square - the
                // live selection may be a match a preview tick selected inside it.
                Square blk;
                if (m_scopeRegionValid)
                    blk = m_scopeRegion;
                else
                    m_pEditorView->GetSelection(blk);
                bool normBlk = (blk.start < blk.end);
                if (!normBlk) blk.normalize();
                Position orgPos = m_pEditorView->GetPosition();

                //Square blk;
                //m_pEditorView->GetSelection(blk);

                if (!blk.is_empty())
                {
                    blk.normalize();
                    switch (m_pEditorView->GetBlockMode())
                    {
                    default:
                        THROW_APP_EXCEPTION("Unsupported block type for counting or replacing.");
                    case ebtStream:
                        ;
                    }

                    COEditorView* pView = 0;

                    COEditorView::UndoGroup undoGroup(*m_pEditorView);
                    Position pos = m_pEditorView->GetPosition();
                    m_pEditorView->PushInUndoStack(pos);

                    FindCtx ctx;
                    ctx.m_line = blk.start.line;
                    ctx.m_start = blk.start.column;
                    ctx.m_end = blk.end.column;
                    ctx.m_skipFirstNull = false;
                    // Replace-in-selection always scans forward; the direction
                    // travels per-call in the context.
                    ctx.m_backward     = false;
                    // 27.05.2002 bug fix, regexp replace fails on \1,... for ReplaceAll
                    toUnprintableStr(m_ReplaceText, ctx.m_text, m_RegExp ? true : false /*skipEscDgt*/);

                    while (m_pEditorView->Find((const COEditorView *&)pView, ctx)
                    && !ctx.m_eofPassed && pView == m_pEditorView)
                    {
                        if ((ctx.m_line >= blk.start.line && ctx.m_line < blk.end.line)
                        || (ctx.m_line == blk.end.line && ctx.m_end <= blk.end.column))
                        {
                            if (mode == esbReplace)
                            {
                                int orgLen = ctx.m_end - ctx.m_start;
                                m_pEditorView->Replace(ctx);
                                int newLen = ctx.m_end - ctx.m_start;
                                // 30.03.2005 (ak) bug fix, hanging on trying to replace 'a' with 'aa' in the current selection 
                                ctx.m_start = ctx.m_end;
                                // 2017-12-03 bug fix, If you want to replace a string in a selected text with a replacement string bigger than the search string, 
                                //                     it will not replace until the end of the original selected text.
                                if (ctx.m_line == blk.end.line)
                                    blk.end.column += newLen - orgLen;
                            }
                            counter++;
                        }
                        else
                            break;
                    }
                }

                if (!normBlk) 
                    std::swap(blk.start, blk.end);
                else
                    orgPos = blk.end;
                m_pEditorView->MoveTo(orgPos);
                m_pEditorView->SetSelection(blk);
            }

            if (mode == esbReplace && counter)
                AdjustPosition();

            // A batch rewrote the document (and parks the selection): re-anchor
            // the live preview at the resulting position.
            CapturePreviewAnchor();

            // Replace-in-selection consumed the region: re-arm it (and the band)
            // from the block the batch parked the selection on.
            if (mode == esbReplace && m_WhereReplace == 0)
                CaptureScopeRegion();

            // A batch reports its own totals below - drop the caption counter.
            UpdateTitleCounter(CString());
        }

        char buff[100];
        itoa(counter, buff, 10);

        switch (mode)
        {
        case esbReplace:
            Global::SetStatusText(strcat(buff, " occurrence(s) have been replaced!"));
            break;
        case esbMark:
            Global::SetStatusText(strcat(buff, " occurrence(s) have been marked!"));
            break;
        case esbCount:
            Global::SetStatusText(strcat(buff, " occurrence(s) have been found!"));
            MessageBox(Common::wstr(buff).c_str(), L"Find");
            break;
        }
    }
    _OE_DEFAULT_HANDLER_;
}

BOOL CFindReplaceDlg::OnInitDialog ()
{
    // Shared with RetargetFull; must stay before CDialog::OnInitDialog() - it
    // feeds the DDX initial values.
    BOOL enableWhereReplace = RefreshFromView();

    CDialog::OnInitDialog();

    // Base for the caption counter, before any " - n of m" suffix is appended.
    GetWindowText(m_baseCaption);

    Common::AppRestoreHistory(m_wndSearchText, L"Editor", L"SearchText", 10);

    if (!m_SearchText.IsEmpty())
    {
        // 14/08/2002 bug fix, Find/Replace dialog - garbage in combo box history
        m_wndSearchText.SetWindowText(m_SearchText);
    }

    if (m_ReplaceMode)
        Common::AppRestoreHistory(m_wndReplaceText, L"Editor", L"ReplaceText", 10);

    GetDlgItem(IDC_EF_SCOPE_SELECTION)->EnableWindow(enableWhereReplace);

    SetupButtons();

    // Strip the inherited caption icon (since Vista only exstyle + WM_SETICON
    // + frame refresh works); before the positioning, which needs final metrics.
    ModifyStyleEx(0, WS_EX_DLGMODALFRAME);
    SendMessage(WM_SETICON, ICON_SMALL, 0);
    SendMessage(WM_SETICON, ICON_BIG, 0);
    SetWindowPos(NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    if (m_ptTopLeft.x != -1 && m_ptTopLeft.y != -1)
    {
        CRect rc;
        GetWindowRect(&rc);
        rc.MoveToXY(m_ptTopLeft);
        MoveWindow(rc);
    }
    else
    {
        CRect rc, frmRc;
        GetWindowRect(&rc);
        AfxGetMainWnd()->GetWindowRect(&frmRc);
        rc.MoveToX(frmRc.right - rc.Width() - 6);
        MoveWindow(rc);
    }

    AdjustPosition();

    if (!m_edtSearchText.SubclassComboBoxEdit(m_wndSearchText.m_hWnd))
        // hide button for Win95
        ::SetWindowPos(::GetDlgItem(m_hWnd, IDC_EF_REGEXP_FIND), 0, 0, 0, 0, 0, SWP_HIDEWINDOW);

    if (m_wndReplaceText.m_hWnd)
    {
        if (!m_edtReplaceText.SubclassComboBoxEdit(m_wndReplaceText.m_hWnd))
            // hide button for Win95
            ::SetWindowPos(::GetDlgItem(m_hWnd, IDC_EF_REGEXP_REPLACE), 0, 0, 0, 0, 0, SWP_HIDEWINDOW);
    }

    ::EnableWindow(::GetDlgItem(m_hWnd, IDC_EF_REGEXP_FIND), m_RegExp);
    ::EnableWindow(::GetDlgItem(m_hWnd, IDC_EF_REGEXP_REPLACE), m_RegExp);

    // Count the text the dialog opened with (programmatic combo fills generate no
    // EN_CHANGE) without navigating; arm the region first for a scoped count.
    if (m_WhereReplace == 0)
        CaptureScopeRegion();

    RestartPreviewTimer(true /*countOnly*/);

    return TRUE;
}

void CFindReplaceDlg::OnChangeSettings ()
{
    UpdateData();
    m_Modified = TRUE;
    ::EnableWindow(::GetDlgItem(m_hWnd, IDC_EF_REGEXP_FIND), m_RegExp);
    ::EnableWindow(::GetDlgItem(m_hWnd, IDC_EF_REGEXP_REPLACE), m_RegExp);

    // Every search-text keystroke and option toggle restarts the debounce (a
    // replace-text edit too - harmless, the preview re-selects the same hit).
    RestartPreviewTimer();
}

void CFindReplaceDlg::OnScopeRadio ()
{
    OnChangeSettings();   // UpdateData pulls the clicked radio into m_WhereReplace

    // Selection arms the region from the live selection (no-op when it is not
    // usable); any other scope drops it - All Windows escapes selection scope.
    if (m_WhereReplace == 0)
        CaptureScopeRegion();
    else
        ReleaseScopeRegion();

    SetupButtons();
}

void CFindReplaceDlg::NotifyIdle ()
{
    // Idle relay from COEditorView::OnIdleUpdateCmdUI - an owned popup never
    // receives the WM_IDLEUPDATECMDUI broadcast itself.
    if (m_pInstance && ::IsWindow(m_pInstance->m_hWnd) && m_pInstance->IsWindowVisible())
    {
        // Document-switch safety net: MDI activation skips the view notification
        // while this popup holds the keyboard, so retarget from MDIGetActive here.
        m_pInstance->RetargetToActivatedView(ActiveEditorView());

        m_pInstance->RefreshScopeIdle();
    }
}

void CFindReplaceDlg::RefreshScopeIdle ()
{
    if (!::IsWindow(m_hWnd))
        return;

    COEditorView* pView = m_pEditorView;
    if (!pView || !::IsWindow(pView->GetSafeHwnd()))
        return;   // mid-retarget: leave the scope alone, RetargetFull re-derives

    // Classify the live selection as RefreshFromView does: 0 = none usable
    // (empty, column mode, All Windows), 1 = single-line, 2 = multi-line.
    int state;
    Square blk;
    if (m_AllWindows || pView->IsSelectionEmpty() || pView->GetBlockMode() == ebtColumn)
        state = 0;
    else
    {
        pView->GetSelection(blk);
        blk.normalize();
        state = (blk.start.line != blk.end.line) ? 2 : 1;
    }

    // Enablement tracks the classification on every pass (churn-free: an
    // unchanged EnableWindow is a no-op).
    ::EnableWindow(::GetDlgItem(m_hWnd, IDC_EF_SCOPE_SELECTION), state != 0);

    // Region maintenance runs on every pass, before the transition-only radio
    // logic: "moved" = the live selection differs from the one the dialog knew.
    const bool movedByUser = m_lastKnownSelectionValid && state != 0
        && !(blk.start == m_lastKnownSelection.start && blk.end == m_lastKnownSelection.end);

    if (m_scopeRegionValid)
    {
        // Edits since the capture: re-resolve the endpoints; unresolvable ones
        // release the region and fall back to whole file visibly (inside).
        RefreshScopeRegionStaleness();

        if (m_scopeRegionValid && movedByUser)
        {
            if (state == 2)
                CaptureScopeRegion();   // a NEW multi-line selection replaces the region
            else
            {
                // The caret moved off or the selection collapsed onto one line:
                // the region dies and the scope falls back to whole file visibly.
                ReleaseScopeRegion();
                m_lastScopeSelState = -1;   // re-classify below in this same pass
                ApplyScopeRadio(1);
            }
        }
    }
    else if (movedByUser && state == 2 && m_WhereReplace == 0 && !m_AllWindows)
        CaptureScopeRegion();   // scope says Selection but no region is armed:
                                // re-arm on the new selection

    // The radio is re-derived only on classification TRANSITIONS: an explicit
    // click must survive idle passes while the selection situation stands.
    if (state == m_lastScopeSelState)
        return;
    m_lastScopeSelState = state;

    int scope = m_WhereReplace;
    // scope 2 is an explicit user choice; selection transitions never move it.
    if (scope != 2 && state == 0)
        scope = 1;   // selection lost / column mode: fall back to current file
                     // visibly, not to a silent "0 occurrence(s)" no-op
    else if (scope != 2 && state == 2)
        scope = 0;   // a multi-line selection appeared: scope follows it,
                     // same rule RefreshFromView applies at open time.
    // state == 1 (single-line): keep the choice - committed searches end with
    // the hit selected, so auto-switching would fight the user's radio.

    ApplyScopeRadio(scope);   // no-op when the scope stands

    // The region follows the radio: entering selection scope arms it, leaving drops it.
    if (m_WhereReplace == 0 && !m_scopeRegionValid && state != 0)
        CaptureScopeRegion();
    else if (m_WhereReplace == 1 && m_scopeRegionValid)
        ReleaseScopeRegion();
}


void CFindReplaceDlg::OnSearchText ()
{
    OnChangeSettings();
    SetupButtons();
}

void CFindReplaceDlg::SetupButtons ()
{
    // it works not precisely but maybe later...
    BOOL hasSearchText = m_wndSearchText.GetWindowTextLength() > 0
        || m_wndSearchText.GetCurSel() != -1 && m_wndSearchText.GetLBTextLen(m_wndSearchText.GetCurSel());

    if (!m_ReplaceMode)
    {
        GetDlgItem(IDC_EF_MARK_ALL)->EnableWindow(hasSearchText);
        GetDlgItem(IDC_EF_FIND_NEXT)->EnableWindow(hasSearchText);
        GetDlgItem(IDC_EF_FIND_PREVIOUS)->EnableWindow(hasSearchText);
        GetDlgItem(IDC_EF_COUNT)->EnableWindow(hasSearchText);
    }
    else
    {
        // No scope gate: committed searches step inside the armed region
        // (RunScopedSearch), so they stay usable in Selection scope.
        GetDlgItem(IDC_EF_FIND_NEXT)->EnableWindow(hasSearchText);
        GetDlgItem(IDC_EF_FIND_PREVIOUS)->EnableWindow(hasSearchText);
        GetDlgItem(IDC_EF_REPLACE)->EnableWindow(hasSearchText);
        GetDlgItem(IDC_EF_REPLACE_ALL)->EnableWindow(hasSearchText);
    }
}
void CFindReplaceDlg::OnBnClicked_RegexpFind()
{
    ShowPopupMenu(IDC_EF_REGEXP_FIND, IDR_OE_REGEXP_FIND_POPUP);
}

void CFindReplaceDlg::OnBnClicked_RegexpReplace()
{
    ShowPopupMenu(IDC_EF_REGEXP_REPLACE, IDR_OE_REGEXP_REPLACE_POPUP);
}

void CFindReplaceDlg::ShowPopupMenu (UINT btnId, UINT menuId)
{
    HWND hButton = ::GetDlgItem(m_hWnd, btnId);

    _ASSERTE(hButton);

    if (hButton
    && ::SendMessage(hButton, BM_GETCHECK, 0, 0L) == BST_UNCHECKED)
    {
        ::SendMessage(hButton, BM_SETCHECK, BST_CHECKED, 0L);

        CRect rect;
        ::GetWindowRect(hButton, &rect);

        CMenu menu;
        VERIFY(menu.LoadMenu(menuId));

        CMenu* pPopup = menu.GetSubMenu(0);
        _ASSERTE(pPopup != NULL);

        TPMPARAMS param;
        param.cbSize = sizeof param;
        param.rcExclude.left   = 0;
        param.rcExclude.top    = rect.top;
        param.rcExclude.right  = USHRT_MAX;
        param.rcExclude.bottom = rect.bottom;

        if (::TrackPopupMenuEx(*pPopup, TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                               rect.left, rect.bottom + 1, *this, &param))
        {
		      MSG msg;
		      ::PeekMessage(&msg, hButton, WM_LBUTTONDOWN, WM_LBUTTONDOWN, PM_REMOVE);
        }

        ::PostMessage(hButton, BM_SETCHECK, BST_UNCHECKED, 0L);
    }
}


static DWORD start, end;

void CFindReplaceDlg::OnInsertRegexpFind (UINT nID)
{
    if (nID > ID_REGEXP_FIND_TAB)
        ::SendMessage(::GetDlgItem(m_hWnd, IDC_EF_REGEXP), BM_SETCHECK, BST_CHECKED, 0L);

    static
    struct { const wchar_t* text; int offset; }
        expr[] = {
            { L"\\t", -1 },
            { L".",  -1 },
            { L"*",  -1 },
            { L"+",  -1 },
            { L"^",  -1 },
            { L"$",  -1 },
            { L"[]",  1 },
            { L"[^]", 2 },
            { L"|",  -1 },
            { L"\\", -1 },
            { L"([a-zA-Z0-9])",              -1 },
            { L"([a-zA-Z])",                 -1 },
            { L"([0-9])",                    -1 },
            { L"([0-9a-fA-F]+)",             -1 },
            { L"([a-zA-Z_$][a-zA-Z0-9_$]*)", -1 },
            { L"(([0-9]+\\.[0-9]*)|([0-9]*\\.[0-9]+)|([0-9]+))", -1 },
            { L"((\"[^\"]*\")|('[^']*'))",   -1 },
        };

    _ASSERTE((nID - ID_REGEXP_FIND_TAB) < (sizeof(expr)/sizeof(expr[0])));

    if (((nID - ID_REGEXP_FIND_TAB) < (sizeof(expr)/sizeof(expr[0]))))
    {
        m_edtSearchText.InsertAtCurPos(expr[nID - ID_REGEXP_FIND_TAB].text, expr[nID - ID_REGEXP_FIND_TAB].offset);
        OnChangeSettings();
        SetupButtons();
    }
}

void CFindReplaceDlg::OnInsertRegexpReplace (UINT nID)
{
    ::SendMessage(::GetDlgItem(m_hWnd, IDC_EF_REGEXP), BM_SETCHECK, BST_CHECKED, 0L);

    static
    const wchar_t* expr[]
        = { L"\\0", L"\\1", L"\\2", L"\\3", L"\\4", L"\\5", L"\\6", L"\\7", L"\\8", L"\\9" };

    _ASSERTE((nID - ID_REGEXP_REPLACE_WHAT_FIND) < (sizeof(expr)/sizeof(expr[0])));

    if (((nID - ID_REGEXP_REPLACE_WHAT_FIND) < (sizeof(expr)/sizeof(expr[0]))))
    {
        m_edtReplaceText.InsertAtCurPos(expr[nID - ID_REGEXP_REPLACE_WHAT_FIND], -1);
        OnChangeSettings();
        SetupButtons();
    }

}

void CFindReplaceDlg::OnCancel()
{
    // Esc: modeless close; save/destroy/focus-restore live in CloseDialog.
    CloseDialog();
}

// Opt-in close-on-deactivate (GlobalSettings::FindCloseOnDeactivate): the
// handlers only arm the check, decided once the activation has settled.
void CFindReplaceDlg::OnActivate (UINT nState, CWnd* pWndOther, BOOL bMinimized)
{
    CDialog::OnActivate(nState, pWndOther, bMinimized);

    if (nState == WA_INACTIVE && IsCloseOnDeactivateEnabled() && ::IsWindow(m_hWnd))
        PostMessage(WM_FIND_CHECK_DEACTIVATE);
}

void CFindReplaceDlg::OnActivateApp (BOOL bActive, DWORD dwThreadID)
{
    CDialog::OnActivateApp(bActive, dwThreadID);

    // The whole app lost the activation: no window of this thread holds the
    // focus, so the posted check sees a NULL focus and closes.
    if (!bActive && IsCloseOnDeactivateEnabled() && ::IsWindow(m_hWnd))
        PostMessage(WM_FIND_CHECK_DEACTIVATE);
}

LRESULT CFindReplaceDlg::OnCheckCloseOnDeactivate (WPARAM, LPARAM)
{
    if (!::IsWindow(m_hWnd))
        return 0;

    // The setting is re-read, not latched when the check was armed: it may
    // have been switched off (or the dialog re-activated) in between.
    if (!IsCloseOnDeactivateEnabled())
        return 0;

    HWND hFocus = ::GetFocus();

    // Keep the dialog when the focus is inside it or on the editor being
    // searched; anywhere else (another document, toolbars, another app) closes.
    if (hFocus == GetSafeHwnd() || (hFocus && ::IsChild(GetSafeHwnd(), hFocus)))
        return 0;

    if (m_pEditorView && ::IsWindow(m_pEditorView->GetSafeHwnd())
     && hFocus == m_pEditorView->GetSafeHwnd())
        return 0;

    // Close without restoring the focus: the user moved away deliberately and
    // pulling the focus (and the document) back would yank them out of it.
    CloseDialog(false /*restoreFocus*/);
    return 0;
}

void CFindReplaceDlg::OnOK()
{
    // Modeless: Enter is normally consumed by the default button; this fallback
    // must not EndDialog either, so it closes exactly like Esc.
    CloseDialog();
}

void CFindReplaceDlg::OnClose()
{
    // The caption X posts WM_CLOSE; route it through the same
    // save/destroy/focus-restore path as Esc.
    CloseDialog();
}

void CFindReplaceDlg::PostNcDestroy()
{
    // Modeless dialogs are heap-allocated (ShowModeless) and delete themselves here.
    if (m_pInstance == this)
        m_pInstance = nullptr;

    if (m_hFindAccel)
    {
        ::DestroyAcceleratorTable(m_hFindAccel);
        m_hFindAccel = NULL;
    }

    CDialog::PostNcDestroy();
    delete this;
}

BOOL CFindReplaceDlg::PreTranslateMessage(MSG* pMsg)
{
    // While this dialog has the focus the frame's accelerators never fire, so
    // translate the find-family subset at the target view; edit keys stay put.
    if ((pMsg->message == WM_KEYDOWN || pMsg->message == WM_SYSKEYDOWN)
     && m_hFindAccel
     && m_pEditorView && ::IsWindow(m_pEditorView->GetSafeHwnd()))
    {
        // The routed command can re-enter ShowModeless and delete THIS dialog, so
        // capture the HWNDs and touch no member after TranslateAccelerator.
        HWND hSelf = GetSafeHwnd();
        HWND hView = m_pEditorView->GetSafeHwnd();

        if (::TranslateAccelerator(hView, m_hFindAccel, pMsg))
        {
            if (::IsWindow(hSelf))
                RestoreFocusToSearchText();   // Find() -> MDIActivate took the focus away
            return TRUE;
        }
    }

    return CDialog::PreTranslateMessage(pMsg);
}

LRESULT CFindReplaceDlg::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    try { EXCEPTION_FRAME;

        return CDialog::WindowProc(message, wParam, lParam);
    }
    _COMMON_DEFAULT_HANDLER_

    return 0;
}
