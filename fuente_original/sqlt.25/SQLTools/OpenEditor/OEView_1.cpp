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
    13.05.2002 bug fix, processing for control chars has been added (esc,...)
    10.07.2002 bug fix, double/triple click on spaces does not select line
    08.09.2002 bug fix, the searched text has been found but may be still invisible
    08.09.2002 improvement, new copy commands
    28.09.2002 improvement, splitter panes/views synhronization
    28.09.2002 bug fix, scrolling on mouse weel support is twice faster then defined
    19.11.2002 improvement, smart vertical scrolling for goto and bookmark jumps
    08.01.2003 improvement, dropdown list control for autocomplete/templates
    16.03.2003 improvement, triple click for line selection has been discontinued
    16.03.2003 bug fix, mouse selection fails sometimes on left button click with pressed <shift>
    17.03.2003 bug fix, cursor position become wrong after document type was changed
    22.03.2003 bug fix, mouse click on gutter does not select line (since 16.03.2003)
    23.03.2003 improvement, added a new mouse word selectiton on left butten click with pressed ctrl
    24.03.2003 improvement, MouseSelectionDelimiters has been added (hidden) which influences on double click selection behavior
    31.03.2003 bug fix, editor context menu position is wrong on Shift+F10
    29.06.2003 improvement, status line indicator for character under curstor 
    16.02.2004 bug fix, exception on PgDn if "Cursor beyond end of file" is unchecked
    30.06.2004 improvement/bug fix, text search/replace interface has been changed
    11.10.2004 improvement, Ruler for columns has been added
    11.10.2004 improvement, Columnar markers has been added (can be defined in class property page)
    11.10.2004 improvement, "Datetime stamp" command has been implemented
    11.10.2004 improvement, columnar edit mode has been added
    24.10.2004 bug fix, "Next search" is not working after the previous fix (replace all fails on 1 line selection)
    28.03.2005 (ak) some changes in OpenFileUnderCursor 
    2011.09.13 improved cursor position / selection indicator has been borrowed from sqltools++
    2014.05.09 bug fix, template changes are not available in already open files
    2016.05.10 improvement, implemented Read-Only mode
    2016.06.14 improvement, Alternative Columnar selection mode 
    2016.06.15 improvement, mouse selectin by words
    2017.11.28 bug fix, indentation does not work with a single line columnar selection
    2024.02.01 bug fix, flickering cursor over text selection in virtual desktop
*/

#include "stdafx.h"
#include <fstream>
#include <direct.h>
#include <io.h>
#include <COMMON/AppGlobal.h>
#include <Common/AppUtilities.h>
#include <COMMON/ExceptionHelper.h>
#include "COMMON/GUICommandDictionary.h"
#include "OpenEditor/OEDocument.h"
#include "OpenEditor/OEView.h"
#include "OpenEditor/OEFindReplaceDlg.h"
#include "OpenEditor/OEGoToDialog.h"
#include "OpenEditor/OEHighlighter.h" // for a destructor
#include "OpenEditor/OESortDlg.h"
#include "OpenEditor/OEInsertNumberDlg.h"
#include "COMMON/StrHelpers.h"
#include "COMMON/InputDlg.h"
#include "COMMON/CustomShellContextMenu.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

using namespace OpenEditor;

static const WCHAR g_szClassName[] = L"OpenEditorWindow";
static HCURSOR g_hCurIBeam = ::LoadCursor(NULL, IDC_IBEAM);
static HCURSOR g_hCurArrow = ::LoadCursor(NULL, IDC_ARROW);
// Over a fold glyph or a collapsed row's [...] a click changes fold state rather
// than placing the caret, so it gets the pointing hand - the affordance the rest
// of Windows uses for "this is clickable".
static HCURSOR g_hCurHand  = ::LoadCursor(NULL, IDC_HAND);
BOOL COEditorView::m_isOverWriteMode = FALSE;
UINT COEditorView::m_uWheelScrollLines = 0;
COEAutocompleteCtrl COEditorView::m_autocompleteList;
UINT COEditorView::m_AltColumnarTextFormat = 0;

int g_cyPixelsPerInch = CWindowDC(CWnd::GetDesktopWindow()).GetDeviceCaps(LOGPIXELSY);
int PixelToPoint (int pixels) { return MulDiv(pixels, 72, g_cyPixelsPerInch); }
int PointToPixel (int points) { return MulDiv(points, g_cyPixelsPerInch, 72); }

// Auto-close / auto-surround pair table. Hard-coded and file-local on purpose:
// LanguageSupport::FindMatch is a *search* routine over buffer content, not a pair table.
//   isQuote       - selects the quote vs bracket surround setting and enables in-string suppression;
//   bAutoClosable - false for the backtick, which is auto-surround only, never auto-closed.
// Language-specific quotes (HighlighterBase::m_stringPair / m_charPair) are a future extension.
namespace
{
    struct AutoPairEntry
    {
        wchar_t opener;
        wchar_t closer;
        bool    isQuote;
        bool    bAutoClosable;
    };

    const AutoPairEntry g_autoPairTable[] =
    {
        { L'(',  L')',  false, true  },
        { L'[',  L']',  false, true  },
        { L'{',  L'}',  false, true  },
        { L'"',  L'"',  true,  true  },
        { L'\'', L'\'', true,  true  },
        { L'`',  L'`',  true,  false }
    };

    const size_t g_autoPairCount = sizeof(g_autoPairTable) / sizeof(g_autoPairTable[0]);

    // The table entry an opening character belongs to, or 0 when it opens no pair.
    const AutoPairEntry* FindAutoPairEntry (wchar_t ch)
    {
        for (size_t i = 0; i < g_autoPairCount; i++)
            if (g_autoPairTable[i].opener == ch)
                return &g_autoPairTable[i];

        return 0;
    }

    // Cheap filter for the skip-over path (§3.10 rule 5.2): only a character the table can
    // produce as a closer is worth looking up in the tracking map at all.
    bool IsAutoPairCloser (wchar_t ch)
    {
        for (size_t i = 0; i < g_autoPairCount; i++)
            if (g_autoPairTable[i].closer == ch)
                return true;

        return false;
    }

    // The opener a typed character surrounds with (D8): typing a *closing* bracket over a
    // selection wraps it in the pair exactly like typing the matching opener does - Visual
    // Studio behavior, where the VS Code default fires on openers only. Quotes are their own
    // closers, so the lookup returns them unchanged, and a character that closes no pair
    // returns unchanged too - FindAutoPairEntry then rejects it exactly as before. Used by
    // both surround entry points; the skip-over path (§3.10 rule 5.2) never sees it because
    // that path runs only with an empty selection, while surround runs only with a selection.
    wchar_t SurroundOpenerFor (wchar_t ch)
    {
        for (size_t i = 0; i < g_autoPairCount; i++)
            if (g_autoPairTable[i].closer == ch)
                return g_autoPairTable[i].opener;

        return ch;
    }
}

// check for existence of file only
#define ACCESS_MIN_PRIVILEGES 0

#define RETURN_IF_LOCKED { if (!m_bAttached) return; if (IsLocked()) { Global::SetStatusText("The content is locked and cannot be modified!"); MessageBeep((UINT)-1); return; } } 

// can we use PathFileExists? According MSDN, it does not support UNC path
// but actually it does for WinXP - test PathFileExists on Win98 and NT4
inline bool FileExists (LPCSTR filename) 
{
    // has read privileges?
    return (access(filename, ACCESS_MIN_PRIVILEGES) == 0); 
}

/////////////////////////////////////////////////////////////////////////////
// COEditorView

IMPLEMENT_DYNCREATE(COEditorView, CView)

BEGIN_MESSAGE_MAP(COEditorView, CView)
    //{{AFX_MSG_MAP(COEditorView)
    ON_WM_PAINT()
    ON_WM_CREATE()
    ON_WM_DESTROY()
    ON_WM_SIZE()
    ON_WM_HSCROLL()
    ON_WM_VSCROLL()
    ON_WM_SETFOCUS()
    ON_WM_KILLFOCUS()
    ON_WM_KEYDOWN()
    ON_WM_SYSKEYDOWN()
    ON_WM_MOUSEMOVE()
    ON_WM_LBUTTONDOWN()
    ON_WM_LBUTTONUP()
    //ON_WM_LBUTTONDBLCLK()
    ON_WM_TIMER()
    ON_COMMAND(ID_EDIT_COLUMN_SEL, OnEditToggleColumnarSelection)
    ON_COMMAND(ID_EDIT_STREAM_SEL, OnEditToggleStreamSelection)
    ON_COMMAND(ID_EDIT_TOGGLES_SEL, OnEditToggleSelectionMode)
    ON_UPDATE_COMMAND_UI_RANGE(ID_EDIT_STREAM_SEL, ID_EDIT_COLUMN_SEL, OnUpdate_SelectionType)
    ON_WM_CHAR()
    ON_COMMAND(ID_EDIT_COPY, OnEditCopy)
    ON_COMMAND(ID_EDIT_CUT, OnEditCut)
    ON_COMMAND(ID_EDIT_PASTE, OnEditPaste)
    ON_UPDATE_COMMAND_UI(ID_EDIT_COPY, OnUpdate_EditCopy)
    ON_UPDATE_COMMAND_UI(ID_EDIT_PASTE, OnUpdate_EditPaste)
    ON_COMMAND(ID_TEST, OnTest)
    ON_COMMAND(ID_EDIT_UNDO, OnEditUndo)
    ON_COMMAND(ID_EDIT_REDO, OnEditRedo)
    ON_UPDATE_COMMAND_UI(ID_EDIT_UNDO, OnUpdate_EditUndo)
    ON_UPDATE_COMMAND_UI(ID_EDIT_REDO, OnUpdate_EditRedo)
    ON_COMMAND(ID_EDIT_SELECT_ALL, OnEditSelectAll)
    ON_COMMAND(ID_EDIT_DELETE_LINE, OnEditDeleteLine)
    ON_COMMAND(ID_EDIT_DELETE_WORD_TO_LEFT, OnEditDeleteWordToLeft)
    ON_COMMAND(ID_EDIT_DELETE_WORD_TO_RIGHT, OnEditDeleteWordToRight)
    ON_COMMAND(ID_EDIT_FIND, OnEditFind)
    ON_COMMAND(ID_EDIT_REPLACE, OnEditReplace)
    ON_COMMAND(ID_EDIT_FIND_NEXT, OnEditFindNext)
    ON_COMMAND(ID_EDIT_FIND_PREVIOUS, OnEditFindPrevious)
    ON_COMMAND(ID_EDIT_FIND_SELECTED_NEXT, OnEditFindSeletedNext)
    ON_COMMAND(ID_EDIT_FIND_SELECTED_PREVIOUS, OnEditFindSeletedPrevious)
    ON_COMMAND(ID_EDIT_BKM_TOGGLE, OnEditBookmarkToggle)
    ON_COMMAND(ID_EDIT_BKM_NEXT, OnEditBookmarkNext)
    ON_COMMAND(ID_EDIT_BKM_PREV, OnEditBookmarkPrev)
    ON_COMMAND(ID_EDIT_BKM_REMOVE_ALL, OnEditBookmarkRemoveAll)
    ON_WM_CONTEXTMENU()
    ON_WM_INITMENUPOPUP()
    ON_UPDATE_COMMAND_UI(ID_EDIT_FIND_NEXT, OnUpdate_EditFindNext)
    ON_COMMAND(ID_EDIT_LOWER, OnEditLower)
    ON_COMMAND(ID_EDIT_UPPER, OnEditUpper)
    ON_COMMAND(ID_EDIT_CAPITALIZE, OnEditCapitalize)
    ON_COMMAND(ID_EDIT_INVERT_CASE, OnEditInvertCase)
    ON_UPDATE_COMMAND_UI(ID_EDIT_TABIFY, OnUpdate_SelectionOperation)
    ON_UPDATE_COMMAND_UI(ID_EDIT_TABIFY_LEADING, OnUpdate_SelectionOperation)
    ON_COMMAND(ID_EDIT_TABIFY, OnBlockTabify)
    ON_COMMAND(ID_EDIT_TABIFY_LEADING, OnBlockTabifyLeading)
    ON_COMMAND(ID_EDIT_UNTABIFY, OnBlockUntabify)
    ON_COMMAND(ID_EDIT_INDENT, OnEditIndent)
    ON_COMMAND(ID_EDIT_UNDENT, OnEditUndent)
    ON_COMMAND(ID_EDIT_SORT, OnEditSort)
    ON_COMMAND(ID_EDIT_FIND_MATCH, OnEditFindMatch)
    ON_COMMAND(ID_EDIT_FIND_MATCH_N_SELECT, OnEditFindMatchAndSelect)
    ON_COMMAND(ID_EDIT_COMMENT, OnEditComment)
    ON_COMMAND(ID_EDIT_UNCOMMENT, OnEditUncomment)
    ON_UPDATE_COMMAND_UI(ID_EDIT_COMMENT, OnUpdate_CommentUncomment)
    ON_UPDATE_COMMAND_UI(ID_EDIT_UNCOMMENT, OnUpdate_CommentUncomment)
    ON_UPDATE_COMMAND_UI(ID_EDIT_CUT, OnUpdate_SelectionOperation)
    ON_UPDATE_COMMAND_UI(ID_EDIT_FIND_PREVIOUS, OnUpdate_EditFindNext)
    ON_UPDATE_COMMAND_UI(ID_EDIT_UNTABIFY, OnUpdate_SelectionOperation)
    ON_UPDATE_COMMAND_UI(ID_EDIT_UNDENT, OnUpdate_SelectionOperation)
    ON_UPDATE_COMMAND_UI(ID_EDIT_INDENT, OnUpdate_SelectionOperation)
    ON_COMMAND(ID_EDIT_GOTO, OnEditGoto)
    //}}AFX_MSG_MAP
    ON_UPDATE_COMMAND_UI(ID_INDICATOR_POS,        OnUpdate_Pos)
    ON_UPDATE_COMMAND_UI(ID_INDICATOR_SCROLL_POS, OnUpdate_ScrollPos)
    ON_UPDATE_COMMAND_UI(ID_INDICATOR_OVR,        OnUpdate_Mode)
    ON_UPDATE_COMMAND_UI(ID_INDICATOR_BLOCK_TYPE, OnUpdate_BlockType)
    ON_UPDATE_COMMAND_UI(ID_INDICATOR_CUR_CHAR,   OnUpdate_CurChar)
    // Random bookmark commands
    ON_COMMAND_RANGE(ID_EDIT_BKM_SET_0, ID_EDIT_BKM_SET_9, OnSetRandomBookmark)
    ON_COMMAND_RANGE(ID_EDIT_BKM_GET_0, ID_EDIT_BKM_GET_9, OnGetRandomBookmark)
    /*
    it is commented because it does not work dynamically and commands are not accesible
    ON_UPDATE_COMMAND_UI_RANGE(ID_EDIT_BKM_GET_0, ID_EDIT_BKM_GET_9, OnUpdate_GetRandomBookmark)
    */
    ON_UPDATE_COMMAND_UI_RANGE(ID_EDIT_BKM_NEXT, ID_EDIT_BKM_REMOVE_ALL, OnUpdate_BookmarkGroupRO)
    // Outlining commands (md/CodeFolding.md stage 7). Two contiguous ID ranges,
    // two enablement questions: caret-addressed vs whole-document.
    ON_COMMAND(ID_EDIT_FOLD_COLLAPSE_CURRENT, OnEditFoldCollapseCurrentRegion)
    ON_COMMAND(ID_EDIT_FOLD_EXPAND_CURRENT,   OnEditFoldExpandCurrentRegion)
    ON_COMMAND(ID_EDIT_FOLD_TOGGLE_CURRENT,   OnEditFoldToggleCurrentRegion)
    ON_COMMAND(ID_EDIT_FOLD_EXPAND_SUBTREE,   OnEditFoldExpandCurrentSubtree)
    ON_COMMAND(ID_EDIT_FOLD_COLLAPSE_ALL,     OnEditFoldCollapseAllRegions)
    ON_COMMAND(ID_EDIT_FOLD_EXPAND_ALL,       OnEditFoldExpandAllRegions)
    ON_COMMAND(ID_EDIT_FOLD_TOGGLE_ALL,       OnEditFoldToggleAllRegions)
    ON_COMMAND(ID_EDIT_FOLD_COLLAPSE_LEVEL1,  OnEditFoldCollapseToLevel1)
    ON_COMMAND(ID_EDIT_FOLD_COLLAPSE_LEVEL2,  OnEditFoldCollapseToLevel2)
    ON_COMMAND(ID_EDIT_FOLD_COLLAPSE_LEVEL3,  OnEditFoldCollapseToLevel3)
    ON_UPDATE_COMMAND_UI_RANGE(ID_EDIT_FOLD_COLLAPSE_CURRENT, ID_EDIT_FOLD_EXPAND_SUBTREE, OnUpdate_FoldCurrentRegion)
    ON_UPDATE_COMMAND_UI_RANGE(ID_EDIT_FOLD_COLLAPSE_ALL, ID_EDIT_FOLD_COLLAPSE_LEVEL3, OnUpdate_FoldGroup)
    // The gutter context menu's PlSqlOutlineFoldingEnabled flip (50440) - outside
    // both ranges above, always enabled, check state instead of gray-out.
    ON_COMMAND(ID_EDIT_FOLD_TOGGLE_ENABLED, OnEditFoldToggleEnabled)
    ON_UPDATE_COMMAND_UI(ID_EDIT_FOLD_TOGGLE_ENABLED, OnUpdate_FoldToggleEnabled)
    // Standard printing commands
    ON_COMMAND(ID_FILE_PRINT, CView::OnFilePrint)
    ON_COMMAND(ID_FILE_PRINT_DIRECT, CView::OnFilePrint)
    ON_COMMAND(ID_FILE_PRINT_PREVIEW, OnFilePrintPreview) // FRM
    ON_COMMAND(ID_EDIT_SCROLL_UP,     OnEditScrollUp    )
    ON_COMMAND(ID_EDIT_SCROLL_DOWN,   OnEditScrollDown  )
    ON_COMMAND(ID_EDIT_SCROLL_CENTER, OnEditScrollCenter)
    ON_COMMAND(ID_EDIT_EXPAND_TEMPLATE, OnEditExpandTemplate)
    ON_COMMAND(ID_EDIT_CREATE_TEMPLATE, OnEditCreateTemplate)
    ON_COMMAND(ID_EDIT_OPENFILEUNDERCURSOR, OnOpenFileUnderCursor)
    
    ON_COMMAND(ID_EDIT_CUT_N_APPEND, OnEditCutAndAppend)
    ON_UPDATE_COMMAND_UI(ID_EDIT_CUT_N_APPEND,  OnUpdate_SelectionOperation)
    ON_COMMAND(ID_EDIT_CUT_BOOKMARKED, OnEditCutBookmarked)
    ON_UPDATE_COMMAND_UI(ID_EDIT_CUT_BOOKMARKED, OnUpdate_BookmarkGroup)
    ON_COMMAND(ID_EDIT_COPY_N_APPEND, OnEditCopyAndAppend)
    ON_UPDATE_COMMAND_UI(ID_EDIT_COPY_N_APPEND, OnUpdate_EditCopy)
    ON_COMMAND(ID_EDIT_COPY_BOOKMARKED, OnEditCopyBookmarked)
    ON_UPDATE_COMMAND_UI(ID_EDIT_COPY_BOOKMARKED, OnUpdate_BookmarkGroupRO)
    ON_COMMAND(ID_EDIT_DELETE_BOOKMARKED, OnEditDeleteBookmarked)
    ON_UPDATE_COMMAND_UI(ID_EDIT_DELETE_BOOKMARKED, OnUpdate_BookmarkGroup)
    ON_COMMAND(ID_EDIT_SELECT_WORD, OnEditSelectWord)
    ON_COMMAND(ID_EDIT_SELECT_LINE, OnEditSelectLine)
    ON_COMMAND(ID_EDIT_DELETE, OnEditDelete)
    ON_WM_MOUSEWHEEL()
    ON_WM_SETTINGCHANGE()
    ON_COMMAND(ID_EDIT_NORMALIZE_TEXT, OnEditNormalizeText)
    ON_COMMAND(ID_EDIT_DATETIME_STAMP, OnEditDatetimeStamp)

    ON_COMMAND(ID_EDIT_FORMAT_JOIN_LINES    ,  OnEditJoinLines          )        
    ON_COMMAND(ID_EDIT_FORMAT_SPLIT_LINES   ,  OnEditSplitLine          )        
    ON_COMMAND(ID_EDIT_FORMAT_REMOVE_BLANK  ,  OnEditRemoveBlankLines   )        
    ON_COMMAND(ID_EDIT_FORMAT_REMOVE_EXCESSIVE_BLANK, OnRemoveExcessiveBlankLines)

    ON_COMMAND(ID_EDIT_FORMAT_TRIM_LEADING_SPACES  , OnTrimLeadingSpaces   )
    ON_COMMAND(ID_EDIT_FORMAT_TRIM_EXCESSIVE_SPACES, OnTrimExcessiveSpaces )
    ON_COMMAND(ID_EDIT_FORMAT_TRIM_TRAILING_SPACES , OnTrimTrailingSpaces  )
    
    ON_COMMAND(ID_EDIT_FORMAT_SQL, OnEditFormatSql)
    ON_COMMAND(ID_EDIT_FORMAT_SQL_WITH_SETTINGS, OnEditFormatSqlWithSettings)

    ON_COMMAND(ID_EDIT_COLUMN_INSERT_FILL   ,  OnEditColumnInsert       )            
    ON_COMMAND(ID_EDIT_COLUMN_INSERT_NUMBER ,  OnEditColumnInsertNumber )        
    ON_COMMAND(ID_EDIT_COLUMN_LEFT_JUSTIFY  ,  OnEditColumnLeftJustify  )        
    ON_COMMAND(ID_EDIT_COLUMN_CENTER_JUSTIFY,  OnEditColumnCenterJustify)        
    ON_COMMAND(ID_EDIT_COLUMN_RIGT_JUSTIFY  ,  OnEditColumnRightJustify )        
    ON_COMMAND(ID_EDIT_DUP_LINE_OR_SELECTION,  OnEditDupLineOrSelection )

    ON_UPDATE_COMMAND_UI(ID_EDIT_FORMAT_JOIN_LINES    ,  OnUpdate_SelectionOperation)        
    ON_UPDATE_COMMAND_UI(ID_EDIT_FORMAT_SPLIT_LINES   ,  OnUpdate_SelectionOperation)        
    ON_UPDATE_COMMAND_UI(ID_EDIT_FORMAT_REMOVE_BLANK  ,  OnUpdate_SelectionOperation)        
    ON_UPDATE_COMMAND_UI(ID_EDIT_FORMAT_REMOVE_EXCESSIVE_BLANK,  OnUpdate_SelectionOperation)        

    ON_UPDATE_COMMAND_UI(ID_EDIT_FORMAT_TRIM_LEADING_SPACES  , OnUpdate_SelectionOperation)
    ON_UPDATE_COMMAND_UI(ID_EDIT_FORMAT_TRIM_EXCESSIVE_SPACES, OnUpdate_SelectionOperation)
    ON_UPDATE_COMMAND_UI(ID_EDIT_FORMAT_TRIM_TRAILING_SPACES , OnUpdate_SelectionOperation)

    ON_UPDATE_COMMAND_UI(ID_EDIT_FORMAT_SQL, OnUpdate_StreamSelectionOperation)
    ON_UPDATE_COMMAND_UI(ID_EDIT_FORMAT_SQL_WITH_SETTINGS, OnUpdate_StreamSelectionOperation)

    ON_UPDATE_COMMAND_UI(ID_EDIT_COLUMN_INSERT_FILL   ,  OnUpdate_ColumnOperation)            
    ON_UPDATE_COMMAND_UI(ID_EDIT_COLUMN_INSERT_NUMBER ,  OnUpdate_ColumnOperation)        
    ON_UPDATE_COMMAND_UI(ID_EDIT_COLUMN_LEFT_JUSTIFY  ,  OnUpdate_ColumnOperation)        
    ON_UPDATE_COMMAND_UI(ID_EDIT_COLUMN_CENTER_JUSTIFY,  OnUpdate_ColumnOperation)        
    ON_UPDATE_COMMAND_UI(ID_EDIT_COLUMN_RIGT_JUSTIFY  ,  OnUpdate_ColumnOperation)        

    ON_UPDATE_COMMAND_UI(ID_EDIT_REPLACE,               OnUpdate_NotInReadOnly)
    ON_UPDATE_COMMAND_UI(ID_EDIT_EXPAND_TEMPLATE,       OnUpdate_NotInReadOnly)
    ON_UPDATE_COMMAND_UI(ID_EDIT_DELETE,                OnUpdate_NotInReadOnly)
    ON_UPDATE_COMMAND_UI(ID_EDIT_DELETE_LINE,           OnUpdate_NotInReadOnly)
    ON_UPDATE_COMMAND_UI(ID_EDIT_DELETE_WORD_TO_LEFT,   OnUpdate_NotInReadOnly)
    ON_UPDATE_COMMAND_UI(ID_EDIT_DELETE_WORD_TO_RIGHT,  OnUpdate_NotInReadOnly)
    ON_UPDATE_COMMAND_UI(ID_EDIT_DUP_LINE_OR_SELECTION, OnUpdate_NotInReadOnly)
    ON_UPDATE_COMMAND_UI(ID_EDIT_LOWER,                 OnUpdate_NotInReadOnly)
    ON_UPDATE_COMMAND_UI(ID_EDIT_UPPER,                 OnUpdate_NotInReadOnly)
    ON_UPDATE_COMMAND_UI(ID_EDIT_CAPITALIZE,            OnUpdate_NotInReadOnly)
    ON_UPDATE_COMMAND_UI(ID_EDIT_INVERT_CASE,           OnUpdate_NotInReadOnly)
    ON_UPDATE_COMMAND_UI(ID_EDIT_NORMALIZE_TEXT,        OnUpdate_NotInReadOnly)
    ON_UPDATE_COMMAND_UI(ID_EDIT_DATETIME_STAMP,        OnUpdate_NotInReadOnly)
    ON_UPDATE_COMMAND_UI(ID_EDIT_SORT,                  OnUpdate_NotInReadOnly)

    ON_MESSAGE_VOID(WM_IDLEUPDATECMDUI, OnIdleUpdateCmdUI)
END_MESSAGE_MAP()

// Out of line because a static data member of array type has to be defined
// exactly once; the header only declares it (md/CodeFolding.md stage 4).
const wchar_t COEditorView::FOLD_MARKER_TEXT[] = L"...";

/////////////////////////////////////////////////////////////////////////////
// COEditorView construction/destruction

COEditorView::COEditorView ()
{
    memset(m_Rulers, 0, sizeof(m_Rulers));
    m_bAttached            =
    m_bMouseSelection      =
    m_bMouseSelectionStart = FALSE;
    m_bMouseSelectionByWord = FALSE;
    m_nMouseSelStartLine   = -1;
    m_nClicks = 0;
    m_tmLastClick = 0;
    m_bSyntaxHighlight     = TRUE;
    m_nDigitsInLatsLineNumber = 1;
    m_nMaxLineLength       = 1;
    m_bDragScrolling       = FALSE;
    m_nFistHighlightedLine = -1;
    m_nLastHighlightedLine = -1;
    m_nFistHighlightedColumn = -1;   // -1/-1: the band is the legacy full-row form
    m_nLastHighlightedColumn = -1;
    m_autoscrollOnHighlighting = false;
    m_suppressMatchingHighlight = false;   // Step 3: scope-region tint suppression off by default
    m_scopeRegion.clear();
    m_scopeRegionValid     = false;
    m_matchOverlayValid    = false;   // Step 4 "highlight all occurrences" overlay
    m_highlightingPosCache.line = -1;
    m_highlightingPosCache.column = -1;
    m_bPaleIfLocked = FALSE;
    m_nExecutionLine = -1;
    m_nDelayedScrollLine = -1;
    m_syntaxGutter = true;
    m_unsupportedAsianChars = false;
    m_foldingGutter = false;
    m_currentFoldRegionStart = -1;
    m_currentFoldRegionEnd   = -1;
    m_autoPairsLine = -1;
    m_autoPairEditing = false;

    if (!m_AltColumnarTextFormat)
        m_AltColumnarTextFormat = RegisterClipboardFormat(L"OpenEditor.ColumnarText");
}

COEditorView::~COEditorView ()
{
    try { EXCEPTION_FRAME;

        for (int i(0); i < 2; i++)
            DetachSibling(i);
    }
    _DESTRUCTOR_HANDLER_;
}

void COEditorView::OnDestroy ()
{
    // Step 2 of the modeless conversion (md/editor_search_modeless_conversion_plan.md):
    // the app-level Find/Replace dialog singleton holds a raw pointer to its
    // target view. WM_DESTROY fires while the CWnd object and the split-sibling
    // links are still intact (the destructor's DetachSibling runs later), so
    // the dialog can safely retarget to a sibling pane or close itself.
    // Without this, closing the targeted view would leave the modeless dialog
    // driving a dangling pointer.
    CFindReplaceDlg::NotifyViewDestroying(this);

    CView::OnDestroy();
}

void COEditorView::AttachSibling (COEditorView* sibling, UINT ruler)
{
    DetachSibling(ruler);
    sibling->DetachSibling(ruler);
    m_Rulers[ruler].m_sibling = sibling;
    sibling->m_Rulers[ruler].m_sibling = this;
}

void COEditorView::DetachSibling (UINT ruler)
{
    if (m_Rulers[ruler].m_sibling) 
    {
        _ASSERTE(m_Rulers[ruler].m_sibling->m_Rulers[ruler].m_sibling == this);
        m_Rulers[ruler].m_sibling->m_Rulers[ruler].m_sibling = 0;
        m_Rulers[ruler].m_sibling = 0;
    }
}

int COEditorView::CalculateDigitsInLatsLineNumber ()
{
    int digits(1);
    for (int lines(GetLineCount()); lines; lines /= 10, digits++)
        ;
    return digits;
}

int COEditorView::CalculateLeftMargin (int digits)
{
    // 17.03.2003 bug fix, cursor position become wrong after document type was changed
    if (!digits)
        digits = CalculateDigitsInLatsLineNumber();

    int leftMargin = (GetSettings().GetLineNumbers() && digits > 1) 
        ? m_Rulers[0].m_CharSize * (digits + 1) 
            : m_Rulers[0].m_CharSize * 25 / 10;

    leftMargin += LINE_STATUS_WIDTH; // extra for line status

    LanguageSupportPtr ls = GetLanguageSupport();
    if (m_syntaxGutter && ls.get() && ls->HasExtendedSupport())
        leftMargin += EXT_SYN_GUTTER_WIDTH;
    else if (m_foldingGutter)
        // Folding borrows the syntax gutter rather than adding a margin of its
        // own (decision D6, plan §7 Q2), so the two never stack: when the
        // extended syntax gutter is already there the fold glyphs are drawn
        // into it, and only when it is absent does folding have to reserve the
        // width itself. Without this branch a document with folding on and the
        // syntax gutter off would paint its [-]/[+] boxes over the line numbers.
        leftMargin += EXT_SYN_GUTTER_WIDTH;

    return leftMargin;
}

    static 
    ULONG getSplitterPaneID (ULONG row, ULONG col)
    {
        return AFX_IDW_PANE_FIRST + row * 16 + col;
    }
    static 
    void getSplitterRowCol (ULONG id, ULONG& row, ULONG& col)
    {
        id -= AFX_IDW_PANE_FIRST;
        col = 0xf & id;
        row = id >> 4;
    }
    
COEditorView* COEditorView::GetSibling (ULONG row, ULONG col)
{
    CWnd* parent = GetParent();
    if (parent)
    {
        CWnd* sibling = parent->GetDlgItem(getSplitterPaneID(row, col));
        if (sibling && sibling->IsKindOf(RUNTIME_CLASS(COEditorView)))
            return static_cast<COEditorView*>(sibling);
    }
    _ASSERTE(0);
    return 0;
}

COEditorView::Ruler* COEditorView::GetSiblingRuler (ULONG row, ULONG col, UINT ruler)
{
    _ASSERTE(ruler < 2);
    if (ruler < 2)
    {
        CWnd* parent = GetParent();
        if (parent)
        {
            COEditorView* sibling = GetSibling(row, col);
            if (sibling)
                return sibling->m_Rulers + ruler;
        }
    }
    _ASSERTE(0);
    return 0;
}

void COEditorView::OnInitialUpdate ()
{
    SetStorage(&GetDocument()->GetStorage());
    m_bAttached = TRUE;
    m_nDigitsInLatsLineNumber = CalculateDigitsInLatsLineNumber();

    OnSettingsChanged();

    // on splitting
    ULONG id = ::GetWindowLong(m_hWnd, GWL_ID);
    
    _ASSERTE(getSplitterPaneID(0,0) <= id && id <= getSplitterPaneID(1,1));
    
    if (getSplitterPaneID(0,0) < id && id <= getSplitterPaneID(1,1))
    {
        ULONG row, col;
        getSplitterRowCol(id, row, col);
        
        // initialize initial scrolling and cursor positions
        if (row == 1 && col == 0 || row == 0 && col == 1) // 2 views
        {
            Ruler* siblingRuler;
            siblingRuler = GetSiblingRuler(0, 0, 1);
            if (siblingRuler) m_Rulers[1].m_Topmost = siblingRuler->m_Topmost;
            siblingRuler = GetSiblingRuler(0, 0, 0);
            if (siblingRuler) m_Rulers[0].m_Topmost = siblingRuler->m_Topmost;
            
            // link siblings for synchronized scrolling
            COEditorView* sibling;
            if (col) // for vertical scrolling
            { 
                sibling = GetSibling(0, row);
                if (sibling) AttachSibling(sibling, 1);
            }
            if (row) // for horizontal scrolling
            {
                sibling = GetSibling(col, 0);
                if (sibling) AttachSibling(sibling, 0);
            }
        }
        else // 4 views
        {
            Ruler* siblingRuler;
            siblingRuler = GetSiblingRuler(1, 0, 1);
            if (siblingRuler) m_Rulers[1].m_Topmost = siblingRuler->m_Topmost;
            siblingRuler = GetSiblingRuler(0, 1, 0);
            if (siblingRuler) m_Rulers[0].m_Topmost = siblingRuler->m_Topmost;

            COEditorView* sibling;
            sibling = GetSibling(1, 0);
            if (sibling) AttachSibling(sibling, 1);
            sibling = GetSibling(0, 1);
            if (sibling) AttachSibling(sibling, 0);
        }

        Position pos;
        pos.line = m_Rulers[1].m_Topmost;
        pos.column = m_Rulers[0].m_Topmost;
        MoveTo(pos);
    }

    RECT rect;
    GetClientRect(&rect);
    OnSize(SIZE_RESTORED, rect.right, rect.bottom);

    AdjustCaretPosition();
    Invalidate(FALSE);
}

BOOL COEditorView::PreCreateWindow (CREATESTRUCT& cs)
{
    WNDCLASS wndClass;
    BOOL bRes = CView::PreCreateWindow(cs);
    HINSTANCE hInstance = AfxGetInstanceHandle();

    // see if the class already exists
    if (!::GetClassInfo(hInstance, g_szClassName, &wndClass))
    {
        // get default stuff
        ::GetClassInfo(hInstance, cs.lpszClass, &wndClass);
        wndClass.lpszClassName = g_szClassName;
        wndClass.style &= ~(CS_OWNDC | CS_CLASSDC | CS_PARENTDC);
        wndClass.style &= ~(CS_HREDRAW | CS_VREDRAW);
        //2016.06.15 improvement, mouse selectin by words
        //wndClass.style |= CS_DBLCLKS /*| CS_OWNDC*/;
        wndClass.style &= ~CS_DBLCLKS;
        wndClass.hbrBackground = 0;
        wndClass.hCursor = 0;
        // register a new class
        if (!AfxRegisterClass(&wndClass))
            AfxThrowResourceException();
    }
    cs.lpszClass = g_szClassName;

    return bRes;
}

LRESULT COEditorView::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    try { EXCEPTION_FRAME;

        return CView::WindowProc(message, wParam, lParam);
    }
    _OE_DEFAULT_HANDLER_;

    return 0;
}

BOOL COEditorView::OnCmdMsg(UINT nID, int nCode, void* pExtra, AFX_CMDHANDLERINFO* pHandlerInfo)
{
    try { EXCEPTION_FRAME;

        if (!pExtra // only for commands
        && nCode != 0
        && nCode != ID_EDIT_DELETE // one exception
        && m_autocompleteList.IsActive()) 
            m_autocompleteList.HideControl();

        return CView::OnCmdMsg(nID, nCode, pExtra, pHandlerInfo);
    }
    _OE_DEFAULT_HANDLER_;

    return TRUE;
}

/////////////////////////////////////////////////////////////////////////////
// COEditorView message handlers

int COEditorView::OnCreate (LPCREATESTRUCT lpCreateStruct)
{
    if (CView::OnCreate(lpCreateStruct) == -1)
        return -1;

    m_DropTaget.Register(this);// for Drag & Drop

    return 0;
}

void COEditorView::OnSize (UINT nType, int cx, int cy)
{
    CView::OnSize(nType, cx, cy);
    DoSize(nType, cx, cy);
//    char buff[80];
//    _snprintf(buff, sizeof(buff)/sizeof(buff[0]), "editor size x=%d,y=%d (chars)", m_Rulers[0].m_Count, m_Rulers[1].m_Count);
//    Global::SetStatusText(buff, TRUE);
}

void COEditorView::DoHScroll (UINT nSBCode, BOOL keyboard)
{
    SCROLLINFO scrollInfo;
    scrollInfo.cbSize = sizeof(scrollInfo);
    scrollInfo.fMask = SIF_TRACKPOS;

    GetScrollInfo(SB_HORZ, &scrollInfo);

    int oldTopmost = m_Rulers[0].m_Topmost;

    if(m_bAttached 
    && m_Rulers[0].OnScroll(nSBCode, scrollInfo.nTrackPos, m_nMaxLineLength - 1, GetSettings().GetCursorBeyondEOL()))
    {
        // Deferred-tasks note: reuse the pixels that only need to slide over
        // instead of repainting the full client rect on every scroll click.
        // The gutter is excluded (rc.left starts at m_Rulers[0].m_Indent) -
        // columns never move it - while the horizontal ruler at the top DOES
        // shift with the text, so it stays inside the scrolled band. A jump of
        // a whole screen or more (thumb drag across the document, etc.) has no
        // reusable pixels left, so it keeps the old full-repaint path.
        int deltaCols = oldTopmost - m_Rulers[0].m_Topmost;

        if (deltaCols != 0 && abs(deltaCols) < m_Rulers[0].m_Count)
        {
            CRect rc;
            rc.left   = m_Rulers[0].m_Indent;
            rc.right  = m_Rulers[0].m_Indent + m_Rulers[0].m_ClientSize;
            rc.top    = 0;
            rc.bottom = m_Rulers[1].m_Indent + m_Rulers[1].m_ClientSize;

            ScrollWindow(deltaCols * m_Rulers[0].m_CharSize, 0, &rc, &rc);
        }
        else
            Invalidate(FALSE);

        AdjustHorzScroller(keyboard ? 0 : scrollInfo.nMax);
        SetCaretPosition();
    }

    // refresh status bar
    if (nSBCode == SB_THUMBTRACK)
        AfxGetApp()->OnIdle(0);
}

void COEditorView::DoVScroll (UINT nSBCode, BOOL keyboard)
{
    SCROLLINFO scrollInfo;
    scrollInfo.cbSize = sizeof(scrollInfo);
    scrollInfo.fMask = SIF_TRACKPOS;

    GetScrollInfo(SB_VERT, &scrollInfo);

    // Row space: the position AdjustVertScroller published is a visual row, so the arithmetic runs
    // over rows and the outcome is resolved back to a buffer line for m_Rulers[1].m_Topmost, which
    // stays a line for every other reader. RowToLine never returns a hidden line - a thumb track
    // that stops inside a collapsed run lands on that run's anchor, the row the user can see. With
    // folding off the mapper is an identity function and this is the original computation.
    // The m_bAttached test is evaluated first, so nothing reaches the document while uninitialised.
    int  topRow = 0;
    bool moved  = false;
    int  oldTopRow = 0;

    if (m_bAttached)
    {
        topRow = oldTopRow = GetTopmostRow();
        moved  = ScrollTopmost(topRow, nSBCode, scrollInfo.nTrackPos, m_Rulers[1].m_Count,
                               GetVisibleLineCount() - 1, GetSettings().GetCursorBeyondEOF());
    }

    if (moved)
    {
        m_Rulers[1].m_Topmost = RowToLine(topRow);

        // Deferred-tasks note: same reasoning as DoHScroll, over rows instead of
        // columns. The gutter (line numbers/fold glyphs) DOES move with rows, so
        // it stays inside the scrolled band (rc.left = 0); the horizontal ruler
        // at the top does not move vertically, so it is excluded (rc.top starts
        // at m_Rulers[1].m_Indent). A jump of a whole screen or more falls back
        // to the old full-repaint path, since nothing on screen survives it.
        int deltaRows = oldTopRow - topRow;

        if (deltaRows != 0 && abs(deltaRows) < m_Rulers[1].m_Count)
        {
            CRect rc;
            rc.left   = 0;
            rc.right  = m_Rulers[0].m_Indent + m_Rulers[0].m_ClientSize;
            rc.top    = m_Rulers[1].m_Indent;
            rc.bottom = m_Rulers[1].m_Indent + m_Rulers[1].m_ClientSize;

            ScrollWindow(0, deltaRows * m_Rulers[1].m_CharSize, &rc, &rc);
        }
        else
            Invalidate(FALSE);

        AdjustVertScroller(keyboard ? 0 : scrollInfo.nMax);
        SetCaretPosition();
    }

    // refresh status bar
    if (nSBCode == SB_THUMBTRACK)
        AfxGetApp()->OnIdle(0);
}

void COEditorView::OnHScroll (UINT nSBCode, UINT /*nPos*/, CScrollBar*)
{
    DoHScroll(nSBCode, FALSE/*keyboard*/);
}

void COEditorView::OnVScroll (UINT nSBCode, UINT /*nPos*/, CScrollBar*)
{
    DoVScroll(nSBCode, FALSE/*keyboard*/);
}

void COEditorView::DoPageDown ()
{
    DoVScroll(SB_PAGEDOWN, TRUE);
    AdjustVertSibling();
}

void COEditorView::DoPageUp ()
{
    DoVScroll(SB_PAGEUP, TRUE);
    AdjustVertSibling();
}

void COEditorView::ShowCaret (bool show, bool dont_destroy_caret)
{
    if (show)
    {
        if (m_bAttached)
        {
            ::CreateCaret(
                    *this, (HBITMAP)NULL,
                    !m_isOverWriteMode
                        ? 2 * GetSystemMetrics(SM_CXBORDER) : m_Rulers[0].m_CharSize/*nWidth*/,
                    m_Rulers[1].m_CharSize/*nHeight*/
                );

            SetCaretPosition();

            ::ShowCaret(*this);
        }
    }
    else
    {
        if (!dont_destroy_caret)
        {
            ::HideCaret(*this);
            ::DestroyCaret();
        }
    }
}

void COEditorView::OnSetFocus (CWnd* pOldWnd)
{
    CView::OnSetFocus(pOldWnd);

    ShowCaret(TRUE);
}

void COEditorView::OnKillFocus (CWnd* pNewWnd)
{
    CView::OnKillFocus(pNewWnd);

    ShowCaret(FALSE);
}

void COEditorView::OnKeyDown (UINT nChar, UINT nRepCnt, UINT nFlags)
{
    if (!m_bAttached) return;

    bool clearSelection = true;

    Position prevPos = GetPosition();

    bool _shift = (0xFF00 & GetKeyState(VK_SHIFT))   ? true : false;
    bool _cntrl = (0xFF00 & GetKeyState(VK_CONTROL)) ? true : false;
    bool _force = GetBlockMode() == ebtColumn && _shift ? true : false;

    switch (nChar)
    {
    default:
        CView::OnKeyUp(nChar, nRepCnt, nFlags);
        return;

    case VK_INSERT:
        if (!_shift && !_cntrl && !(0xFF00 & GetKeyState(VK_MENU)))
        {
            m_isOverWriteMode = !m_isOverWriteMode;
            ShowCaret(TRUE);
            return;
        }
        break;
    case VK_DELETE:
        RETURN_IF_LOCKED

        if (!IsSelectionEmpty() && GetBlockDelAndBSDelete())
        {
            if (GetBlockMode() == ebtColumn
            && GetColBlockEditMode())
            {
                ColumnarDelete();
                clearSelection = false;
            }
            else
                DeleteBlock();
        }
        else
            Delete();
        break;
    case VK_BACK:
        RETURN_IF_LOCKED

        if (!IsSelectionEmpty() && GetBlockDelAndBSDelete())
        {
            if (GetBlockMode() == ebtColumn
            && GetColBlockEditMode())
            {
                ColumnarBackspace();
                clearSelection = false;
                break;
            }
            else
                DeleteBlock();
        }
        else
            Backspace();
        break;
    case VK_UP:
        GoToUp(_force);
        break;
    case VK_DOWN:
        GoToDown(_force);
        break;
    case VK_LEFT:
        if (_cntrl)
            WordLeft();
        else
            if (!_shift && !IsSelectionEmpty())
            {
                Square blkPos;
                GetSelection(blkPos);
                blkPos.normalize();

                if (prevPos != blkPos.start)
                    MoveTo(blkPos.start);
                else
                    ClearSelection(true);
            }
            else
                GoToLeft(_force);
        break;
    case VK_RIGHT:
        if (_cntrl)
            WordRight();
        else
            if (!_shift && !IsSelectionEmpty())
            {
                Square blkPos;
                GetSelection(blkPos);
                blkPos.normalize();

                if (prevPos != blkPos.end)
                    MoveTo(blkPos.end);
                else
                    ClearSelection(true);
            }
            else
                GoToRight(_force);
        break;
    case VK_HOME:
        if (_cntrl)
            GoToTop();
        else
            SmartGoToStart();
        break;
    case VK_END:
        if (_cntrl)
            GoToBottom();
        else
            SmartGoToEnd();
        break;
    case VK_PRIOR:
        {
            AdjustCaretPosition();
            Position pos = GetPosition();

            // Choke point 4 (page motion): what has to survive the page scroll is
            // the ROW the caret sits on, not its buffer-line distance from the
            // top line. The two are the same number only while nothing is
            // folded, so the offset is taken in rows and resolved back through
            // the mapper - a page that ends with the caret inside a collapsed
            // run puts it on that run's anchor (D5).
            //
            // RowToLineUnclamped, not RowToLine, because PgDn may legitimately
            // land below the last row when "cursor beyond end of file" is on.
            const int screenRow = LineToRow(pos.line) - GetTopmostRow();
            const int topRow    = GetTopmostRow();

            DoPageUp();

            if (GetTopmostRow() != topRow)
                pos.line = RowToLineUnclamped(GetTopmostRow() + screenRow);
            else
                pos.line = 0;

            MoveTo(pos, _force);
        }
        break;
    case VK_NEXT:
        {
            AdjustCaretPosition();
            Position pos = GetPosition();

            // As VK_PRIOR above: the caret's row offset is what survives the
            // scroll, and the line it resolves to comes from the mapper.
            const int screenRow = LineToRow(pos.line) - GetTopmostRow();
            const int topRow    = GetTopmostRow();

            DoPageDown();

            pos.line = RowToLineUnclamped(GetTopmostRow() + screenRow);

            // 16.02.2004 bug fix, exception on PgDn if "Cursor beyond end of file" is unchecked
            if (!GetSettings().GetCursorBeyondEOF())
            {
                if (GetTopmostRow() != topRow)
                    pos.line = min(pos.line, max(0, GetLineCount() - 1));
                else
                    pos.line = GetLineCount() - 1;
            }

            MoveTo(pos, _force);
        }
        break;
    case VK_ESCAPE:
        if (!IsSelectionEmpty())
            ClearSelection(true);
        break;
    };

    if (_shift)
    {
        bool _alt = (0xFF00 & GetKeyState(VK_MENU)) ? true : false;
        if (_alt && GetBlockMode() != ebtColumn) // convert to columnar mode
            SetBlockMode(ebtColumn, true);

        SelectByCursor(prevPos);
    }
    else
    {
        if (clearSelection && !IsSelectionEmpty())
            ClearSelection(true);
    }

    AdjustCaretPosition();

    // for better visual response
    if (nRepCnt) UpdateWindow();
}

void COEditorView::OnSysKeyDown (UINT nChar, UINT nRepCnt, UINT nFlags)
{
    switch (nChar)
    {
    case VK_UP:
    case VK_DOWN:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
        OnKeyDown(nChar, nRepCnt, nFlags);
        break;
    default:
        __super::OnSysKeyDown(nChar, nRepCnt, nFlags);
        break;
    }
}

/////////////////////////////////////////////////////////////////////////////
// Auto-close (spec "Editor Auto-closing and Auto-surround", §1, §3.10 rules 5.1-5.4).
// The §3.5 tracking mechanics live next to the other notification handlers in OEView_6.cpp;
// what follows is the keystroke-side policy, kept beside OnChar together with the §3.11
// pair table it is driven by.

// The closing character to auto-insert for `ch`, or 0 when auto-close must not fire - the whole
// rule 5.4 conjunction: the pair table (auto-closable entries only, so the backtick is
// surround-only); the AutoCloseBrackets master switch, covering brackets and quotes alike; the
// whitespace rule when AutoCloseBeforeWhitespaceOnly is on; and in-string suppression, for quote
// characters only - inside a comment both features behave as on plain text.
// `pos` is the caret position *before* `ch` is inserted, and both tests are defined on that
// pre-insert state. Evaluating in-string afterwards would be wrong: the quote just typed would
// itself put the tokenizer into string state and suppress every subsequent quote.
wchar_t COEditorView::GetAutoCloser (wchar_t ch, Position pos) const
{
    if (!GetAutoCloseBrackets())
        return 0;

    const AutoPairEntry* entry = FindAutoPairEntry(ch);

    if (!entry || !entry->bAutoClosable)
        return 0;

    if (pos.line < 0 || pos.line >= GetLineCount())
        return 0;

    // §3.8: "whitespace" means space, tab or end-of-line - nothing else. Other characters
    // (e.g. a closing bracket) do not qualify.
    if (GetAutoCloseBeforeWhitespaceOnly())
    {
        OEStringW lineBuff;
        GetLineW(pos.line, lineBuff);

        const wchar_t* str = lineBuff.data();
        const int inx = PosToInx(pos.line, pos.column);

        if (inx >= 0 && inx < static_cast<int>(lineBuff.length())
        && str[inx] != L' ' && str[inx] != L'\t')
            return 0;
    }

    // Not cheap - it rescans and retokenizes the line - hence last, and only for quotes.
    if (entry->isQuote && IsInStringAt(pos))
        return 0;

    return entry->closer;
}

// §3.10 rule 5.2 / §1.2: the user typed the very closing character this view auto-inserted
// and the caret sits on it, so the keystroke steps over it instead of inserting a duplicate.
// FindAutoCloser consumes the record, which is what makes the skip happen exactly once
// (§3.5) and what keeps §3.7 true - a pre-existing, not session-inserted closer has no
// record and is simply typed over.
// The skip is a caret move only: GoToRight(true) touches neither the document nor the undo
// stack (§1.2, §3.6).
bool COEditorView::SkipOverAutoCloser (wchar_t ch)
{
    // Cheap filters first. The map is empty for almost every keystroke, and only a character
    // the pair table can produce as a closer is worth a lookup at all; neither test
    // allocates.
    if (m_autoPairs.empty() || !IsAutoPairCloser(ch))
        return false;

    const Position pos = GetPosition();

    if (pos.line < 0 || pos.line >= GetLineCount())
        return false;

    OEStringW lineBuff;
    GetLineW(pos.line, lineBuff);

    const int inx = PosToInx(pos.line, pos.column);
    const int len = static_cast<int>(lineBuff.length());

    // Verify the character under the caret *before* consulting the map: FindAutoCloser
    // consumes its record, and typing some other character there must not lose it.
    if (inx < 0 || inx >= len || lineBuff.data()[inx] != ch)
        return false;

    wchar_t closer = 0;

    if (!FindAutoCloser(pos.line, inx, closer))
        return false;

    GoToRight(true);
    return true;
}

// Inserts the character the user typed and, when auto-close applies, its closing character in a
// separate undo group, leaving the caret between the pair.
// `bAutoClose` is false on the type-over-selection path: auto-close requires an empty selection and
// auto-surround is a separate step, so that path has to keep inserting exactly one character.
// Both inserts run under AutoPairEditGuard, which makes the OnChangedLines / OnChangedLinesQuantity
// hooks stand down so the pair records are SHIFTED instead of dropped. That is what lets skip-over
// survive typing between an opener and its closer.
void COEditorView::InsertTypedChar (wchar_t ch, bool bAutoClose)
{
    const Position before = GetPosition();

    // decided on the pre-insert state - see GetAutoCloser
    const wchar_t closer = bAutoClose ? GetAutoCloser(ch, before) : 0;

    {
        AutoPairEditGuard guard(*this);
        Insert(ch);
    }

    const Position after = GetPosition();

    if (after.line != before.line)
    {
        // Not a plain single-character insert on the caret line (a CR or an indent reached
        // EditContext::Insert): the recorded columns cannot be trusted any more (§3.5).
        ResetAutoPairs();
        return;
    }

    // EditContext::Insert (OEContext_3.cpp:236-238) writes the character at
    // pos2inx(pre-insert caret) and only then calls GoToRight(true), so `after` already sits
    // one column past the character that was inserted: the opener's own index is caret - 1.
    // Deriving it from `before` instead would be wrong whenever the caret was in virtual
    // space, since expandVirtualSpace pads the line during the insert.
    const int openerInx = PosToInx(after.line, after.column) - 1;
    const int closerInx = openerInx + 1;

    // The character landed at openerInx, so every recorded column at or after it moves one
    // to the right.
    ShiftAutoPairs(after.line, openerInx, 1);

    if (!closer)
        return;

    // §3.6: UndoCursorPosition is not appendable (OEUndo.h:148), so pushing one here stops
    // the undo stack from coalescing the closer into the opener's UndoInsert
    // (OEUndo.cpp:397-408) - group ids alone do not, because AppendOnInsert never looks at
    // them. One Ctrl+Z then removes only the auto-inserted character and leaves the opener
    // in place (§1.3). Safe to call here: OnChar is never reached during undo/redo.
    PushInUndoStack(after);

    {
        AutoPairEditGuard guard(*this);
        Insert(closer);
    }

    // The closer is an insert like any other and shifts whatever follows it - e.g. the ')'
    // of an enclosing pair when a nested one is opened between '(' and ')'.
    ShiftAutoPairs(after.line, closerInx, 1);

    // Insert() leaves the caret after the closer; §1.1 wants it between the pair. Pure
    // navigation, so this adds no record to the undo group built above.
    GoToLeft(true);

    // §3.5: remember the closer so typing it steps over instead of duplicating (§1.2).
    TrackAutoPair(after.line, openerInx, closerInx, closer);
}
// Smart-indent Stage 3(a). Whether this keystroke can complete a dedent token - also the ONLY thing
// licensing the caller's undo group, so character and rewrite undo together. Not quotes (§3.6).
bool COEditorView::IsSmartDedentCandidate (wchar_t ch) const
{
    return GetIndentType() == eiSmart
        && (iswalpha(ch) || ch == L')' || ch == L']' || ch == L'}');
}

// Performs the Stage 3(a) rewrite when the engine agrees the caret line is a dedent line; a no-op
// otherwise. Called only from OnChar's insert path, which already holds the group it must join.
void COEditorView::ApplySmartDedent ()
{
    const Position caret = GetPosition();

    // Query first. Constructing the group before knowing that something will change would bump
    // the undo group id on every ordinary keystroke and fragment the stack.
    const int wantColumn = smartDedentColumn(caret);

    if (wantColumn < 0)
        return;

    // Nested inside the group OnChar opened, so a no-op: only the outermost UndoGroup registers and
    // only its dtor advances the id (OEStorage.cpp:68-89). Kept for a caller with no group open.
    EditContext::UndoGroup group(*this);

    int delta(0);

    {
        // The rewrite goes through DeleteLinePart/InsertLinePart, which fire OnChangedLines; without
        // the guard its coarse reset drops the pair records before ShiftAutoPairs moves them (§3.5).
        AutoPairEditGuard guard(*this);
        delta = setLineIndent(caret.line, wantColumn, caret);
    }

    // Every pair column recorded on the line moved by the whitespace's change in length.
    if (delta)
        ShiftAutoPairs(caret.line, 0, delta);
}

// Shift+Tab with no selection: outdent the caret's line one stop as one undo group, shaped exactly
// like ApplySmartDedent. Never consults the engine - Editor_Features.md §6 has the routing table.
void COEditorView::ApplyManualUndent ()
{
    const Position caret = GetPosition();

    // Query first, for the reason ApplySmartDedent gives: a keystroke that changes nothing must not
    // create an undo group, or Shift+Tab on a flush-left line adds a dead Ctrl+Z step.
    const int wantColumn = undentTargetColumn(caret.line);

    if (wantColumn < 0)
        return;

    EditContext::UndoGroup group(*this);

    int delta(0);

    {
        // setLineIndent rewrites through DeleteLinePart/InsertLinePart, which fire OnChangedLines;
        // without the guard its coarse reset drops the pair records before ShiftAutoPairs (§3.5).
        AutoPairEditGuard guard(*this);
        delta = setLineIndent(caret.line, wantColumn, caret);
    }

    // setLineIndent has already moved the caret by the same delta, with force=true so the sticky
    // column follows the text rather than staying where it was.
    if (delta)
        ShiftAutoPairs(caret.line, 0, delta);
}

// Wrap the current selection in an opener/closer pair instead of replacing it, as a single undo
// group. Returns false - document untouched - when the character belongs to no pair (a typed closer
// counts as its opener), the matching AutoSurround* setting is off, or a quote is typed inside a
// string; the caller then falls through to the legacy type-over-selection behavior. The caller
// establishes the preconditions in OnChar's `default:` branch: non-empty selection, overtype off,
// and not a column selection - that belongs to AutoSurroundColumnSelection, which surrounds by
// different mechanics.
//
// Deliberately not EditContext::InsertBlock: in stream mode it inserts at the caret and redefines
// the selection to the inserted text, and it never deletes the pre-existing selection. Explicit
// navigation plus single-character Insert() leaves m_blkPos alone, which is what "the wrapped text
// remains selected" requires. (The PL/SQL escaped-quote feature is the one exception: it *replaces*
// the selected text, so it uses those two block primitives - see AutoSurroundEscapedQuote.)
//
// When the PL/SQL quoting gate is active the ' / { / [ keys divert into the escaped-quote or
// q-quote features below, before any edit, and fall back into this path when they do not apply.
// Every other key, and every other behavior here, is byte-identical legacy.
bool COEditorView::AutoSurroundSelection (wchar_t ch)
{
    // D8: a typed closer surrounds exactly like its opener. Normalizing before the table
    // lookup means `)` / `]` / `}` take every path `(` / `[` / `{` take - the §2.4/D7 gate
    // and its q-quote diversion included, so in PL/SQL a typed `}` produces q'{...}' just
    // like a typed `{` does.
    ch = SurroundOpenerFor(ch);

    const AutoPairEntry* entry = FindAutoPairEntry(ch);

    if (!entry)
        return false;

    // §2.4 (D7): PL/SQL-aware stream surround. When the gate is active it *replaces* rule
    // 4.3's normal gate for exactly three keys - the language's string quote (Feature A) and
    // { / [ (Feature B) - so those keys surround even with AutoSurroundQuotes /
    // AutoSurroundBrackets off, and only AutoSurroundPlsqlQuoting=0 switches them back to
    // legacy. Every other key (including ( < " and the backtick) keeps the normal gate and
    // the legacy path byte-identically (§3.12, case 30).
    const wchar_t plsqlQuote = PlsqlQuotingChar();

    const bool bPlsqlKey = plsqlQuote != 0
        && (ch == plsqlQuote || ch == L'{' || ch == L'[');

    // Rule 4.3's gate is the AutoSurround* pair of settings, not AutoCloseBrackets, and the
    // §3.8 whitespace rule does not apply here: what follows the caret is irrelevant when the
    // opener goes in front of the selection. The backtick is surround-only
    // (bAutoClosable == false) and is intentionally *not* filtered out - §2.1 lists it among
    // the supported pairs even though §1.1 does not list it for auto-close.
    if (!bPlsqlKey
    && (entry->isQuote ? !GetAutoSurroundQuotes() : !GetAutoSurroundBrackets()))
        return false;

    Square sel;
    GetSelection(sel);
    sel.normalize();   // house style: OEContext_3.cpp:490, OEView_1.cpp:2453

    if (sel.is_empty())
        return false;

    // §3.2, evaluated at the position the opener is about to occupy - the same pre-insert
    // state auto-close uses, for the same reason. As a side effect this also stops a second
    // quote typed at an already-wrapped selection from double-wrapping it, because the
    // selection it would surround now starts inside the string the first quote opened.
    if (entry->isQuote && IsInStringAt(sel.start))
        return false;

    // Feature B (§2.4/D7): { or [ produce the Oracle alternative-quoting form q'{...}' /
    // q'[...]' instead of the plain pair, using the typed bracket as the q delimiter and
    // Common::q_closer_for as the closer - the single bracket table the splitter, highlighter
    // and scanner share. Both fallbacks are decided *before* any edit (§3.12): a q-literal
    // cannot be opened inside a string, nor when the selection itself contains the closing
    // sequence }' / ]' - the construct could never be closed. Either fallback continues down
    // the regular bracket path below, which still honors AutoSurroundBrackets - the gate
    // bypass above was for the q-form only - so the fallback can still end in rule 4.4.
    if (bPlsqlKey && ch != plsqlQuote)
    {
        const wchar_t qCloser = Common::q_closer_for(ch);

        std::wstring text;
        GetBlock(text, &sel);

        const std::wstring terminator = std::wstring(1, qCloser) + plsqlQuote;

        if (!IsInStringAt(sel.start)
        && text.find(terminator) == std::wstring::npos)
            return AutoSurroundQQuote(ch, qCloser, plsqlQuote, sel);

        if (!GetAutoSurroundBrackets())
            return false;
    }

    // Feature A (§2.4/D7): the language's string quote doubles every embedded quote in the
    // selection - Oracle's escape for ' inside a literal - and surrounds the escaped text.
    // A selection with no embedded quotes runs the existing path below *verbatim*, including
    // §3.5's TrackAutoPair: nothing was rewritten, so nothing invalidates the record (§3.12,
    // case 24). The §3.2 check above already ran, so Feature A inherits the in-string
    // suppression.
    if (bPlsqlKey && ch == plsqlQuote)
    {
        std::wstring text;
        GetBlock(text, &sel);

        std::wstring escaped;
        escaped.reserve(text.size());

        for (size_t i = 0; i < text.size(); ++i)
        {
            escaped += text[i];

            if (text[i] == plsqlQuote)
                escaped += text[i];
        }

        if (escaped != text)
            return AutoSurroundEscapedQuote(ch, sel, escaped);
    }

    const Position caretBefore = GetPosition();

    // The opener shifts only its own line, so the closer's position moves by one column only
    // when the selection does not span lines.
    Position closerPos = sel.end;

    if (closerPos.line == sel.start.line)
        closerPos.column += 1;

    {
        // D1: one undo group for the whole action. EditContext::Insert opens an UndoGroup of
        // its own (OEContext_3.cpp:234), but the ctor no-ops while a group is already active
        // and its dtor then does not call NextUndoGroup(), so both inserts - and the two
        // records pushed here - all land in this one group and a single Ctrl+Z reverts the
        // surround. Unlike auto-close (§3.6) no UndoCursorPosition separator is needed to
        // stop coalescing: UndoInsert::Append requires the inserts to be contiguous
        // (OEUndo.cpp:397-408) and the selected text always lies between them.
        UndoGroup undoGroup(*this);
        PushInUndoStack(caretBefore);
        PushInUndoStack(GetBlockMode(), IsAltColumnarMode(), sel);

        // Both inserts inside one guard, so the coarse tier stands down for the pair of them
        // and the recorded columns are shifted rather than dropped (§3.5).
        AutoPairEditGuard guard(*this);

        MoveTo(sel.start, true);
        Insert(ch);

        MoveTo(closerPos, true);
        Insert(entry->closer);
    }

    // Re-select the wrapped text: the original selection, shifted past the opener. The caret
    // goes to its end, the convention used by ConvertSelectionToLines (OEContext_3.cpp:509-510)
    // and OnDrop (OEView_4.cpp:455-456) - SetSelection assigns m_blkPos verbatim and never
    // touches m_curPos (OEContext_3.cpp:515-528). Leaving the caret on the closer is also what
    // makes §2.2's "session-inserted for overtype-skip purposes" reachable.
    Square keep = sel;
    keep.start.column += 1;

    if (keep.end.line == sel.start.line)
        keep.end.column += 1;

    SetSelection(keep);
    MoveTo(keep.end, true);

    // §2.2 / §3.7: the closer this action inserted is session-inserted, so record it for
    // skip-over. The map is keyed by the opener's line and stores both columns on that line,
    // which can only describe a pair whose two ends share a line - so a multi-line surround is
    // left untracked rather than recorded against the wrong line, where FindAutoCloser would
    // test the closer against the opener line's text. Skip-over then simply does not fire for
    // it: the same conservative outcome §3.5 prescribes for positions it cannot track.
    if (sel.start.line == sel.end.line)
    {
        // pos-space coordinates identify characters stably, so these are the two characters
        // just written: the opener at sel.start, the closer at closerPos.
        const int openerInx = PosToInx(sel.start.line, sel.start.column);
        const int closerInx = PosToInx(closerPos.line, closerPos.column);

        // Two inserts, so two shifts, in insert order - closerPos is already expressed in
        // post-opener coordinates, which is exactly what the second shift expects.
        ShiftAutoPairs(sel.start.line, openerInx, 1);
        ShiftAutoPairs(sel.start.line, closerInx, 1);

        TrackAutoPair(sel.start.line, openerInx, closerInx, entry->closer);
    }

    return true;
}

// The quoting gate's language leg: returns the language's string-quote character when the PL/SQL
// quoting surround is active - AutoSurroundPlsqlQuoting on AND the language declares
// AlternativeQuotes - else 0, meaning "run the legacy path". Deliberately a language *attribute*,
// not the name "PL/SQL": any language opting into AlternativeQuotes gets the behavior, the same
// data-driven choice the q-quote highlighter and storage scanner make. The returned character is
// GetStringPair().first[0] - what CommonHighlighter::qQuoteChar() recognizes - so the generator and
// the recognizer cannot disagree. Read per keystroke with no caching; Find throws on an unknown
// language name, and the guard turns that into a silent legacy fallback.
wchar_t COEditorView::PlsqlQuotingChar ()
{
    if (!GetAutoSurroundPlsqlQuoting())
        return 0;

    if (GetSettings().GetLanguage() == "NONE")   // guard precedent: OnUpdate_CommentUncomment
        return 0;

    try
    {
        LanguagePtr lang = LanguagesCollection::Find(GetSettings().GetLanguage());

        if (!lang || !lang->HasAlternativeQuotes())
            return 0;

        const std::wstring quote = Common::wstr(lang->GetStringPair().first);
        return quote.empty() ? wchar_t(0) : quote[0];
    }
    catch (const std::logic_error&)   // language not found
    {
        return 0;
    }
}

// Wrap a stream selection as the Oracle alternative-quoting literal q'<ch>sel<qCloser>' - the
// regular surround skeleton with a three-character prefix and a two-character suffix. One undo
// group, the same two undo records and the same "wrapped text remains selected, caret at its end"
// post-state; keep shifts by 3 instead of 1, and the prefix shifts sel.start.line only.
// Deliberately no pair tracking: the closer is the two-character sequence }' / ]' while
// TrackAutoPair records a single character, and skipping just the bracket would leave the caret on
// the trailing quote - where the in-string rule suppresses the next ' the user types, producing
// q'{...}'' - so every record is dropped instead.
bool COEditorView::AutoSurroundQQuote (wchar_t ch, wchar_t qCloser, wchar_t quote, const Square& sel)
{
    const Position caretBefore = GetPosition();

    // The three-character prefix q'<ch> shifts its own line only, so the closer moves right by
    // three columns only when the selection does not span lines - the +1 condition of §2.1 with
    // the prefix width.
    Position closerPos = sel.end;

    if (closerPos.line == sel.start.line)
        closerPos.column += 3;

    {
        // D1: one undo group; EditContext::Insert's own group no-ops while ours is active.
        UndoGroup undoGroup(*this);
        PushInUndoStack(caretBefore);
        PushInUndoStack(GetBlockMode(), IsAltColumnarMode(), sel);

        AutoPairEditGuard guard(*this);

        MoveTo(sel.start, true);
        Insert(L'q');
        Insert(quote);
        Insert(ch);

        MoveTo(closerPos, true);
        Insert(qCloser);
        Insert(quote);
    }

    Square keep = sel;
    keep.start.column += 3;

    if (keep.end.line == sel.start.line)
        keep.end.column += 3;

    SetSelection(keep);
    MoveTo(keep.end, true);

    ResetAutoPairs();
    return true;
}

// Wrap a stream selection in the language's string quote, doubling every embedded quote first.
// `escaped` is the selection text with the doubling already applied, built by
// AutoSurroundSelection - which runs the legacy path instead when the text needs no escaping, so
// pair tracking stays intact for that case.
// The replacement reuses the two block primitives: DeleteBlock leaves the caret at sel.start, and
// stream-mode InsertBlock inserts there and redefines the selection to the inserted text - exactly
// the state the regular surround geometry needs. Their inner undo groups are inert while ours is
// active, so the replacement plus the two quote inserts undo as one item.
// No pair tracking: the doubling shifts tracked columns by a text-dependent amount, so no recorded
// pair can be relocated reliably - every record is dropped instead. The closing quote typed here is
// therefore *not* recorded for skip-over.
bool COEditorView::AutoSurroundEscapedQuote (wchar_t ch, const Square& sel, const std::wstring& escaped)
{
    const Position caretBefore = GetPosition();

    Square inner;   // the escaped text as it sits in the document after the replacement

    {
        // D1: one undo group for replacement + both quote inserts.
        UndoGroup undoGroup(*this);
        PushInUndoStack(caretBefore);
        PushInUndoStack(GetBlockMode(), IsAltColumnarMode(), sel);

        AutoPairEditGuard guard(*this);

        DeleteBlock();
        InsertBlock(escaped.c_str(), false /*hideSelection*/, false /*putSelInUndo*/);

        // The selection is now the inserted (escaped) text; the inner quote geometry works on
        // it, not on the pre-escape sel.
        GetSelection(inner);
        inner.normalize();

        Position closerPos = inner.end;

        if (closerPos.line == inner.start.line)
            closerPos.column += 1;

        MoveTo(inner.start, true);
        Insert(ch);

        MoveTo(closerPos, true);
        Insert(ch);
    }

    // Same post-state as the regular surround: the wrapped text remains selected, caret at end.
    Square keep = inner;
    keep.start.column += 1;

    if (keep.end.line == inner.start.line)
        keep.end.column += 1;

    SetSelection(keep);
    MoveTo(keep.end, true);

    ResetAutoPairs();
    return true;
}

// Wrap every line of a column selection in its own opener/closer pair. Policy lives here, mechanics
// in EditContext::ColumnarSurround - which is where they have to be, because InsertLinePart is
// private to EditContext.
// Returns false - with the document untouched - when the character opens no pair, the matching
// AutoSurround* setting is off, a quote is suppressed inside a string, or the block is zero-width;
// the caller then falls through to the pre-existing column behavior. The caller establishes the
// preconditions in OnChar's `default:` branch: non-empty selection, overtype off, ebtColumn.
// Deliberately no pair tracking and no AutoPairEditGuard - see the ResetAutoPairs comment below.
bool COEditorView::AutoSurroundColumnSelection (wchar_t ch)
{
    // D8: a typed closer surrounds exactly like its opener (see AutoSurroundSelection).
    // ColumnarSurround below writes entry->opener / entry->closer, so the normalization only
    // has to reach the table lookup - the block gets the same pair either way.
    ch = SurroundOpenerFor(ch);

    const AutoPairEntry* entry = FindAutoPairEntry(ch);

    if (!entry)
        return false;

    // Same gate as AutoSurroundSelection: the AutoSurround* pair of settings, not
    // AutoCloseBrackets, and §3.8's whitespace rule does not apply - what follows the caret is
    // irrelevant when the opener goes in front of the selection. The backtick stays available
    // (bAutoClosable == false, §2.1).
    if (entry->isQuote ? !GetAutoSurroundQuotes() : !GetAutoSurroundBrackets())
        return false;

    Square sel;
    GetSelection(sel);
    sel.normalize();
    sel.normalize_columns();  // both, as ColumnarInsert / ColumnarIndent / DeleteBlock all do

    // §2.2: a zero-width column block is a column *caret* spanning lines, and typing into it
    // means "put this character on every line" - ColumnarInsert's !width branch
    // (OEContext_3.cpp:988-994), reached via rule 4.4 below. There is no text to surround, so
    // that behavior is left exactly as it is. A *single-line* zero-width block never arrives
    // here at all: start == end makes Square::is_empty() true (OEHelpers.h:93), so OnChar took
    // the rules 5.x branch instead. This test is why rule 4.2's zero-width case still works.
    if (sel.end.column <= sel.start.column)
        return false;

    // §3.2, evaluated once at the block's first line - the position the opener is about to
    // occupy, as in AutoSurroundSelection. Testing every line instead would wrap only those
    // lines that happen to sit outside a string literal, and a ragged block is worse than a
    // suppressed one; testing none would let a quote typed inside a literal widen it. One call
    // also keeps IsInStringAt's cost - it retokenizes a line (§3.2) - at one line rather than N.
    if (entry->isQuote && IsInStringAt(sel.start))
        return false;

    // Stage 6 B8/B9: ColumnarSurround writes an opener and a closer on EVERY line
    // of the block via InsertLinePart, which is not one of the virtual block
    // primitives, so nothing else on this path reveals for it. Placed after all
    // the early returns so a suppressed surround does not expand a fold.
    RevealColumnarEditRows();

    ColumnarSurround(entry->opener, entry->closer);

    // A columnar surround records nothing and clears what was there. Both halves follow from the
    // data structure, not from preference: recording is IMPOSSIBLE - AutoPairRec keys on the
    // opener's line and stores both columns as indexes into that one line, so it cannot describe N
    // closers on N lines - and it would be UNREACHABLE anyway, because SkipOverAutoCloser is gated
    // on an empty selection while what is left here is a non-empty column block, so the next ')'
    // keystroke is itself a columnar surround and wraps the block again.
    //
    // The reset is what the spec already prescribes for a block/columnar insert or delete, and the
    // coarse tier would perform it anyway via OnChangedLines - but correctness here should not
    // depend on a notification side effect in another file, so it is stated explicitly. Line
    // indexes are unchanged by the edit, which adds no lines.
    ResetAutoPairs(sel.start.line, sel.end.line);

    return true;
}

void COEditorView::OnChar (UINT nChar, UINT nRepCnt, UINT /*nFlags*/)
{
    if (!m_bAttached) return;

    RETURN_IF_LOCKED

    NormalizeOnCharCxt cxt;
    PreNormalizeOnChar(cxt, static_cast<char>(nChar));

    // Rules 4.x (a selection is present) and 5.x (none) are mutually exclusive and are decided on
    // the state as it is *before* this keystroke modifies it, so the flag is captured up front - and
    // hoisted out of the switch, because a declaration inside `default:` would be jumped over by the
    // case labels that follow it.
    const bool bEmptySel = IsSelectionEmpty();

    switch (nChar)
    {
    default:
        // Auto-surround is tried first because it takes precedence over BlockTypingOverwrite: with a
        // selection present, an opening character wraps the selection rather than replacing it.
        // Overtype is the second condition, so overtype mode never surrounds.
        //
        // Column and stream selections wrap by different mechanics, so they get their own entry
        // points. Both return false for "not a pair character - opener or its closer / setting off /
        // suppressed inside a string / nothing to surround" and fall through to the legacy
        // type-over-selection with the document untouched.
        //
        // The zero-width rejection lives inside AutoSurroundColumnSelection rather than as a guard
        // out here: a zero-width column block is *not* Square::is_empty(), so that case still
        // reaches ColumnarInsert below unchanged while a non-zero-width one is surrounded.
        if (!bEmptySel && !m_isOverWriteMode)
        {
            const bool bSurrounded = GetBlockMode() == ebtColumn
                ? AutoSurroundColumnSelection(static_cast<wchar_t>(nChar))
                : AutoSurroundSelection(static_cast<wchar_t>(nChar));

            if (bSurrounded)
                break;
        }

        if (!IsSelectionEmpty())
        {
            if (GetBlockTypingOverwrite()) 
            {
                if (GetBlockMode() == ebtColumn
                && GetColBlockEditMode())
                {
                    // Stage 6 B8/B9: ColumnarInsert now reveals its own rows through
                    // the EditContext::RevealLine hook, gated on ColInsertRevealLine
                    // (OEContext_3.cpp:962+), so there is no view-level pre-flight
                    // here - putting one here would make that setting unobservable.
                    ColumnarInsert(static_cast<char>(nChar));
                    break;
                }
                else
                    DeleteBlock();
            }
            else 
                ClearSelection(true);
        }

        if (m_isOverWriteMode)
        {
            // §3.10 rules 4.1 / 5.1: overtype mode keeps today's behavior and never
            // auto-closes or auto-surrounds (§3.3).
            Overwrite(static_cast<wchar_t>(nChar));
        }
        else if (bEmptySel && SkipOverAutoCloser(static_cast<wchar_t>(nChar)))
        {
            // §3.10 rule 5.2: a closer this view auto-inserted, stepped over - caret move only,
            // nothing written or pushed (§1.2, §3.6). No dedent here: unreachable, plan §6 record.
        }
        else
        {
            // §3.10 rules 4.4 / 5.3 / 5.4. Auto-close requires an empty selection, hence
            // bEmptySel: with a selection present only auto-surround may fire (rule 4.3).
            const wchar_t ch = static_cast<wchar_t>(nChar);

            if (bEmptySel && IsSmartDedentCandidate(ch))
            {
                // Stage 3(a): ONE undo group for character and rewrite, so one Ctrl+Z undoes both -
                // the opposite of the auto-closer at :1196-1201. bAutoClose: ch opens no pair here.
                EditContext::UndoGroup group(*this);

                // The group id alone does NOT hold them together: AppendOnInsert coalesces adjacent
                // inserts and ignores group ids (OEUndo.cpp:326-343) - this record is the barrier.
                if (completesDedentToken(ch, GetPosition()))
                    PushInUndoStack(GetPosition());

                InsertTypedChar(ch, false);
                ApplySmartDedent();
            }
            else
                InsertTypedChar(ch, bEmptySel);
        }
        break;

    case '\t':
        {
            // Stage 6 B8/B9: the columnar paths below write the block's whole row range with no
            // virtual block primitive, so hidden rows must be visible first. ebtColumn-gated inside.
            RevealColumnarEditRows();

            Square block;
            GetSelection(block);

            const bool shift = (0xFF00 & GetKeyState(VK_SHIFT)) != 0;

            // A single-line STREAM selection is not a block operation - there is no set of lines to
            // walk - so it belongs to the caret path; a COLUMN one is, even spanning a single line.
            const bool singleStreamLine = block.start.line == block.end.line
                                       && GetBlockMode() != ebtColumn;

            if (shift)
            {
                // Shift+Tab NEVER types over a selection: it outdents it, so destroying the
                // selection belongs to the Tab branch alone. Here it was DATA LOSS, not cosmetics.
                CWaitCursor wait;

                if (block.is_empty())
                    ApplyManualUndent();
                else
                    if (GetBlockMode() == ebtColumn
                    && GetColBlockEditMode())
                        ColumnarUndent();
                    else
                        UndentBlock();
            }
            else
            {
                // Decide the route BEFORE mutating the selection: DeleteBlock()/ClearSelection()
                // empty the LIVE one, so later tests see nothing. One flag serves delete and route.
                const bool deleteSelection = (!block.is_empty() && !GetBlockTabIndent())
                                          || singleStreamLine;

                if (deleteSelection)
                {
                    if (GetBlockTypingOverwrite()) DeleteBlock();
                    else ClearSelection(true);
                }

                CWaitCursor wait;

                if (block.is_empty() || deleteSelection)
                    DoIndent(m_isOverWriteMode ? true : false);
                else
                    if (GetBlockMode() == ebtColumn
                    && GetColBlockEditMode())
                        ColumnarIndent();
                    else
                        IndentBlock();
            }
        }
        break;

     // it's processed already.
    case VK_ESCAPE:
    case VK_BACK:
        break;

     // control characters are not supported
    case 0x00: // (NUL)
    case 0x01: // (SOH)
    case 0x02: // (STX)
    case 0x03: // (ETX)
    case 0x04: // (EOT)
    case 0x05: // (ENQ)
    case 0x06: // (ACK)
    case 0x07: // (BEL)
    case 0x0A: // (LF)
    case 0x0B: // (VT)
    case 0x0C: // (FF)
    case 0x0E: // (SI)
    case 0x0F: // (SO)
    case 0x10: // (DLE)
    case 0x11: // (DC1)
    case 0x12: // (DC2)
    case 0x13: // (DC3)
    case 0x14: // (DC4)
    case 0x15: // (NAK)
    case 0x16: // (SYN)
    case 0x17: // (ETB)
    case 0x18: // (CAN)
    case 0x19: // (EM)
    case 0x1A: // (SUB)
    case 0x1C: // (FS)
    case 0x1D: // (GS)
    case 0x1E: // (RS)
    case 0x1F: // (US)
        MessageBeep((UINT)-1);
    }

    if (cxt.matched)
        NormalizeOnChar(cxt);

    AdjustCaretPosition();
    // for better visual response
    if (nRepCnt) UpdateWindow();
}

void COEditorView::OnMouseMove (UINT nFlags, CPoint point)
{
    if (!m_bAttached) return;

    HCURSOR hCursor = g_hCurIBeam;

    if ((point.x < m_Rulers[0].m_Indent || point.y < m_Rulers[1].m_Indent)
    && !(m_bMouseSelection && m_nMouseSelStartLine == -1))
        hCursor = g_hCurArrow;
    else
    {
        m_nMouseSelStartLine = -1;
        //hCursor = g_hCurIBeam;
    }

    // Captured HERE, before the drag branch below shifts `point` into text-area
    // coordinates, so the hit test always sees client coordinates. Suppressed
    // while a selection is being dragged: the user is selecting then, not
    // folding, and the cursor should say which.
    const bool overFold = !(m_bMouseSelection && (nFlags & MK_LBUTTON))
                       && FoldHoverAtPoint(point);

    if (m_bMouseSelection && (nFlags & MK_LBUTTON))
    {
        SelectByMouse(point);

        point.x -= m_Rulers[0].m_Indent;
        point.y -= m_Rulers[1].m_Indent;

        if (point.x < m_Rulers[0].m_CharSize / 2
         || point.x > m_Rulers[0].m_ClientSize - m_Rulers[0].m_CharSize / 2
         || point.y < m_Rulers[1].m_CharSize / 2
         || point.y > m_Rulers[1].m_ClientSize - m_Rulers[1].m_CharSize / 2)
        {
            SetTimer(SEL_MOUSE_TIMER, SEL_MOUSE_TIMER_ELAPSE, NULL);
        }
    }
    else if (point.x > m_Rulers[0].m_Indent && point.y > m_Rulers[1].m_Indent
    && !(MK_LBUTTON & nFlags) && CursorOnSelection(point)) // cursor above selection
        hCursor = g_hCurArrow;

    // Applied last, so it wins over both the gutter arrow and the I-beam: over a
    // [+]/[-] glyph or a collapsed row's [...] the click toggles a fold instead of
    // placing the caret or starting a selection, and nothing else on screen says
    // so. Without this the band is a plain arrow like the rest of the gutter and
    // the glyphs do not look clickable.
    if (overFold)
        hCursor = g_hCurHand;

    SetCursor(hCursor);
}

// 2016.06.15 improvement, mouse selectin by words
void COEditorView::OnLButtonDown (UINT nFlags, CPoint point)
{
    DWORD tmClick = GetMessageTime();

    if (!m_rcClick.PtInRect(point) 
    || tmClick - m_tmLastClick > GetDoubleClickTime())
        m_nClicks = 0;

    m_nClicks++;

    m_tmLastClick = tmClick;
    m_rcClick.SetRect(point, point);
    m_rcClick.InflateRect(GetSystemMetrics(SM_CXDOUBLECLK) / 2, GetSystemMetrics(SM_CYDOUBLECLK) / 2);

    switch (m_nClicks)
    {
    case 1:
        OnLButtonDownImpl(nFlags, point);
        break;
    case 2: 
        // A double-click on a collapsed fold's [...] expands it and is NOT a word
        // selection (md/CodeFolding.md stage 5). Handled here rather than inside
        // OnLButtonDblClkImpl because the word-selection state below is set
        // unconditionally once that call returns, and leaving it set would start a
        // word-select drag anchored on a row that has just grown underneath the
        // mouse. The marker is chrome, not document text: it is not selectable and
        // there is no word there to select. Word selection on the anchor's OWN text
        // is untouched - the test is the marker's rect, not the whole row.
        if (ExpandFoldMarkerAtPoint(point))
            break;

        OnLButtonDblClkImpl(nFlags, point);
        GetSelection(m_startWord);
        m_startWord.normalize();
        m_bMouseSelection = TRUE;
        m_bMouseSelectionByWord = TRUE;
        m_bMouseSelectionStart = FALSE;
        break;
    }
}

void COEditorView::OnLButtonDownImpl (UINT nFlags, CPoint point)
{
    if (!m_bAttached) return;

    // A press on a fold glyph toggles that fold and is NOT a text selection
    // (md/CodeFolding.md stage 5). Tested before everything else below: the
    // gutter branch would otherwise turn it into a whole-line selection, and
    // CursorOnSelection can never be true inside the margin, so nothing is taken
    // away from either path. Returns false for any press outside the fold band,
    // or inside it but on a row with no glyph, and those fall through unchanged.
    if (ToggleFoldAtPoint(point))
        return;

    // 16.03.2003 improvement, triple click for line selection has been discontinued

    if (CursorOnSelection(point))
    {
       StartDrag();
    }
    else
    {
        Position pos = MouseToPos(point);

        if (point.y < m_Rulers[1].m_Indent)
        {
            // probably columnar selection would be nice here?

            if (!IsSelectionEmpty())
                ClearSelection(true);

            pos.line = GetCursorLine();
            MoveTo(pos);
        }
        else if (point.x < m_Rulers[0].m_Indent) // 22.03.2003 bug fix, mouse click on gutter does not select line (since 16.03.2003)
        {
            SetCapture();
            m_bMouseSelection = TRUE;
            m_bMouseSelectionStart = TRUE;

            if (point.x < m_Rulers[0].m_Indent)
                m_nMouseSelStartLine = pos.line;

            SelectLine(pos.line);
        }
        else
        {
            if (MK_SHIFT & nFlags)
            {
                // 16.03.2003 bug fix, mouse selection fails sometimes on left button click with pressed <shift>
                m_bMouseSelection = 
                m_bMouseSelectionStart = FALSE;
                m_bMouseSelectionByWord = FALSE;

                SelectByMouse(point);
            }
            else if (MK_CONTROL & nFlags) // process it as dblclick
            {
                // 23.03.2003 improvement, added a new mouse word selectiton on left butten click with pressed ctrl
                OnLButtonDblClkImpl(nFlags, point);
                GetSelection(m_startWord);
                m_startWord.normalize();
                m_bMouseSelection = TRUE;
                m_bMouseSelectionByWord = TRUE;
                m_bMouseSelectionStart = FALSE;
            }
            else
            {
                SetCapture();
                m_bMouseSelection = TRUE;
                m_bMouseSelectionStart = TRUE;
                m_nMouseSelStartLine = -1;

                if (point.x < m_Rulers[0].m_Indent)
                    m_nMouseSelStartLine = pos.line;

                if (!IsSelectionEmpty())
                    ClearSelection(true);

                MoveTo(pos);
            }
        }
    }
}

void COEditorView::OnLButtonUp (UINT /*nFlags*/, CPoint point)
{
    if (!m_bAttached) return;

    if (m_bMouseSelection)
    {
        ::ReleaseCapture();

        if (m_bMouseSelection && !IsSelectionEmpty())
        {
            Square selection;
            GetSelection(selection);
            Position pos = GetPosition();

            if (selection.start.line <= selection.end.line
            && pos.line < selection.end.line)
            {
                pos.column = 0;
                pos.line = selection.end.line;
                MoveTo(pos);
            }
        }

        m_bMouseSelection = FALSE;
        m_bMouseSelectionByWord = FALSE;
        m_nMouseSelStartLine = -1;
    }

    // cursor above selection
    if (point.x > m_Rulers[0].m_Indent && point.y > m_Rulers[1].m_Indent
    && CursorOnSelection(point))
        SetCursor(g_hCurArrow);
}

void COEditorView::OnLButtonDblClkImpl (UINT /*nFlags*/, CPoint point)
{
    if (!m_bAttached) return;

    if (point.y >= m_Rulers[1].m_Indent && point.x >= m_Rulers[0].m_Indent)
    {
        ClearSelection(true);

        Square selection;
        Position pos = MouseToPos(point);

        // 24.03.2003 improvement, MouseSelectionDelimiters has been added (hidden) which influences on double click selection behavior
        const char* delims = !GetMouseSelectionDelimiters().empty() ? GetMouseSelectionDelimiters().c_str() : 0;

        if (WordOrSpaceFromPoint(pos, selection, delims))
        {
            MoveTo(selection.end);
            SetSelection(selection);
            InvalidateSquare(selection);
        }
        else
            MoveTo(pos);
    }
}

void COEditorView::OnTimer (UINT_PTR nIDEvent)
{
    if (m_bAttached)
    {
        switch (nIDEvent)
        {
        case SEL_MOUSE_TIMER:
            if (GetKeyState(VK_LBUTTON) & 0xFF00)
            {
                bool scroll = false;

                POINT point;
                GetCursorPos(&point);
                ScreenToClient(&point);

                if (point.x - m_Rulers[0].m_Indent < m_Rulers[0].m_CharSize / 2)
                {
                    scroll = true;
                    SendMessage(WM_HSCROLL, SB_LINELEFT);
                }
                if (point.x - m_Rulers[0].m_Indent
                > m_Rulers[0].m_ClientSize - m_Rulers[0].m_CharSize / 2)
                {
                    scroll = true;
                    SendMessage(WM_HSCROLL, SB_LINERIGHT);
                }
                if (point.y - m_Rulers[1].m_Indent < m_Rulers[1].m_CharSize / 2)
                {
                    scroll = true;
                    SendMessage(WM_VSCROLL, SB_LINELEFT);
                }
                if (point.y - m_Rulers[1].m_Indent 
                > m_Rulers[1].m_ClientSize - m_Rulers[1].m_CharSize / 2)
                {
                    scroll = true;
                    SendMessage(WM_VSCROLL, SB_LINERIGHT);
                }
                if (scroll)
                {
                    SelectByMouse(point);
                    UpdateWindow();
                }
            }
            else
            {
                KillTimer(nIDEvent);
            }
            break;

        case DRAG_MOUSE_TIMER:
            m_bDragScrolling = TRUE;            
            KillTimer(nIDEvent);
            break;
        case DELAYED_SCROLL_TIMER:
            KillTimer(nIDEvent);
            if (m_nDelayedScrollLine != -1)
                ScrollTo(m_nDelayedScrollLine);
            m_nDelayedScrollLine = -1;
            break;
        case DOC_TIMER_1:
        case DOC_TIMER_2:
        case DOC_TIMER_3:
            GetDocument()->OnTimer(nIDEvent);
            break;
        }
    }            
    else
        CView::OnTimer(nIDEvent);
}

void COEditorView::OnUpdate_Mode (CCmdUI* pCmdUI)
{
    pCmdUI->Enable(m_isOverWriteMode);
}

void COEditorView::OnUpdate_Pos (CCmdUI* pCmdUI)
{
    const Position pos = GetPosition();
    Square blk;
    GetSelection(blk);
    bool curPosChanged = (m_posCache != pos);
    bool curSelChanged = (m_selCache != blk);

    if (curPosChanged || curSelChanged)
    {
        m_posCache = pos;
        m_selCache = blk;

        WCHAR buff[80]; 
        const int buff_size = sizeof(buff)/sizeof(buff[0]);
        buff[buff_size-1] = 0;
        //_snprintf(buff, sizeof(buff)-1, " Ln: %d, Col: %d", pos.line + 1, pos.column + 1);

        // 2011.09.13 improved cursor position / selection indicator has been borrowed from sqltools++
        //Square blk;
        //GetSelection(blk);

        if (!blk.is_empty())
        {
            if (GetBlockMode() == ebtColumn)
            {
                blk.normalize();
                blk.normalize_columns();

                int nChars = 0;

                for (int i = blk.start.line; i <= blk.end.line; i++)
                {
                    if (i >= GetLineCount())
                        nChars += blk.end.column - blk.start.column;
                    else
                    {
                        int nStartColumn = pos2inx(i, blk.start.column);
                        int nEndColumn = pos2inx(i, blk.end.column);
                        nChars += nEndColumn - nStartColumn;
                    }
                }

                _snwprintf(buff, buff_size-1, L" Ln: %d, Col: %d [RxC: %dx%d, Sz: %d]", 
                        pos.line + 1, 
                        pos.column + 1,
                        blk.end.line - blk.start.line + 1,
                        blk.end.column - blk.start.column,
                        nChars);
            }
            else
            {
                bool bLastCharStartLine = false;
                if ((blk.start.line == blk.end.line) && (blk.start.column > blk.end.column))
                    std::swap(blk.start.column, blk.end.column);
                if (blk.start.line > blk.end.line)
                {
                    std::swap(blk.start.column, blk.end.column);
                    std::swap(blk.start.line, blk.end.line);
                }
                const char* currentLine; 
                int currentLineLength;
                int nStartColumn = (blk.start.line < GetLineCount()) ? pos2inx(blk.start.line, blk.start.column) : blk.start.column;
                int nEndColumn = (blk.end.line < GetLineCount()) ? pos2inx(blk.end.line, blk.end.column) : blk.end.column;
                int nChars = 0;
                for (int i = blk.start.line; i <= blk.end.line; i++)
                {
                    if (i >= GetLineCount())
                        break;
                    GetLineA(i, currentLine, currentLineLength);
                    if ((i == blk.start.line) && (i == blk.end.line))
                    {
                        bLastCharStartLine = false;
                        nChars += nEndColumn - nStartColumn;
                    }
                    else if (i == blk.start.line)
                    {
                        nChars += currentLineLength - nStartColumn;
                        if (currentLineLength == nStartColumn)
                            bLastCharStartLine = true;
                        else
                            bLastCharStartLine = false;
                    }
                    else if (i == blk.end.line)
                        nChars += nEndColumn;
                    else
                        nChars += currentLineLength;
                }

                _snwprintf(buff, buff_size-1, L" Ln: %d, Col: %d [R: %d, Sz: %d]", 
                        pos.line + 1, 
                        pos.column + 1,
                        blk.end.line - blk.start.line + ((blk.end.column > 0 && ! bLastCharStartLine) ? 1 : 0),
                        nChars);
            }
        }
        else
            _snwprintf(buff, buff_size-1, L" Ln: %d, Col: %d", pos.line + 1, pos.column + 1);

        pCmdUI->SetText(buff);
    }
    pCmdUI->Enable(TRUE);

    bool curSequenceIdChanged = m_highlightingActionSeqCache != GetActionId();
    // probably it is not the best place but maybe will change that later
    if (m_highlightingPosCache != pos || curSequenceIdChanged)
    {
        m_highlightingPosCache = pos;
        m_highlightingActionSeqCache = GetActionId();
        HideHighlighting();
    }

    if (curPosChanged || curSequenceIdChanged)
    {
        HighlightBraces();
        UpdateCurrentFoldHighlight();
    }

    /*
    call HighligtMatchingText here
    */
    if (curPosChanged || curSelChanged || curSequenceIdChanged)
    {
        // Step 3 VS-implicit region: while the modeless Replace dialog holds
        // a scope region, no tint is derived from the selection - its scoped
        // preview ticks select the first match INSIDE the region on purpose
        // and the tint is document-wide (it would mark occurrences outside
        // the region). Drop a tint that is still showing from before the
        // suppression; see SuppressMatchingTextHighlight.
        if (m_suppressMatchingHighlight)
        {
            if (!m_highlightedText.empty())
            {
                m_highlightedText.clear();
                Invalidate(FALSE);
            }
        }
        else if (!IsSelectionEmpty())
        {
            Square selection;
            GetSelection(selection);
            if (selection.start.line == selection.end.line)
            {
                std::wstring text;
                GetBlock(text);

                if (m_highlightedText != text 
                && !Common::is_blank_line(text.c_str(), text.size()))
                {
                    m_highlightedText = text;
                    Invalidate(FALSE);
                }
            }
        }
        else if (!m_highlightedText.empty())
        {
            m_highlightedText.clear();
            Invalidate(FALSE);
        }
    }
}

void COEditorView::HighlightBraces ()
{
    Position pos = GetPosition();
    if (pos.line < GetLineCount()
    && pos.column <= GetLineLength(pos.line))
    {
        LanguageSupport::Match match;
        if (GetMatchInfo(GetPosition()/*pos*/, match))
            SetBraceHighlighting(match);
    }
}

void COEditorView::OnUpdate_ScrollPos (CCmdUI* pCmdUI)
{
    WCHAR buff[80]; 
    const int buff_size = sizeof(buff)/sizeof(buff[0]);
    buff[buff_size-1] = 0;
    _snwprintf(buff, buff_size-1, L" Top: %d, Left: %d", m_Rulers[1].m_Topmost + 1, m_Rulers[0].m_Topmost + 1);
    pCmdUI->SetText(buff);
    pCmdUI->Enable(TRUE);
}

void COEditorView::OnUpdate_BlockType (CCmdUI* pCmdUI)
{
    pCmdUI->SetText(GetBlockMode() == ebtColumn 
        ? (IsAltColumnarMode() ? L" Sel: Alt-Column " : L" Sel: Columnar ") : L" Sel: Stream ");
    pCmdUI->Enable(TRUE);
}

// 29.06.2003 improvement, status line indicator for character under curstor 
void COEditorView::OnUpdate_CurChar (CCmdUI* pCmdUI)
{
    Position pos = GetPosition();

    if (m_curCharCache.pos != pos)
    {
        m_curCharCache.pos = pos;
        m_curCharCache.message = L" Chr: ";

        if (pos.line < GetLineCount()
        && pos.column < GetLineLength(pos.line))
        {
            std::wstring message = L" Chr: ";
            wchar_t buff[80];
            
            OEStringW lineBuff;
            GetLineW(pos.line, lineBuff);
            int len = lineBuff.length();
            const wchar_t* str = lineBuff.data();

            int inx = pos2inx(str, len, pos.column);
            if (iswprint(str[inx])) // because cannot convert signle byte to utf16
            {
                message += '\'';
                message += str[inx];
            }
            else
            {
                message += L"\'?";
            }
            

            if (GetDocument()->GetSaveEncodingCodepage() == CP_UTF16)
            {
                int iChr = str[inx];
                message += L"'=x";
                message += _itow(iChr, buff, 16);
                message += L"=";
                message += _itow(iChr, buff, 10);
            }
            else
            {
                int codepage = GetDocument()->GetEncodingCodepage();

                char chrBuff[10];
                int chrSize = WideCharToMultiByte(codepage, 0, str + inx, 1, chrBuff, sizeof(chrBuff)/sizeof(chrBuff[0]), 0, 0);
                if (chrSize > 0)
                {
                    message += L"'=x";
                    for (int i = 0; i < chrSize; ++i)
                        message += _itow((unsigned char)chrBuff[i], buff, 16);

                    if (chrSize == 1)
                    {
                        message += L"=";
                        message += _itow((unsigned char)chrBuff[0], buff, 10);
                    }
                }
            }

            m_curCharCache.message = message;
        }
    }

    pCmdUI->SetText(m_curCharCache.message.c_str());
    pCmdUI->Enable(TRUE);
}

void COEditorView::OnEditToggleColumnarSelection()
{
    SetBlockMode(ebtColumn);
}

void COEditorView::OnEditToggleStreamSelection()
{
    SetBlockMode(ebtStream);
}

void COEditorView::OnEditToggleSelectionMode()
{
    SetBlockMode(GetBlockMode() == ebtStream ? ebtColumn : ebtStream);
}

void COEditorView::OnUpdate_SelectionType (CCmdUI* pCmdUI)
{
    EBlockMode blkMode = GetBlockMode();

    if (pCmdUI->m_pMenu)
    {
        pCmdUI->m_pMenu->CheckMenuRadioItem(ID_EDIT_STREAM_SEL, ID_EDIT_COLUMN_SEL,
            blkMode == ebtStream ? ID_EDIT_STREAM_SEL : ID_EDIT_COLUMN_SEL, MF_BYCOMMAND);
        }
    else
    {
        BOOL enable = FALSE;
        switch (pCmdUI->m_nID)
        {
        case ID_EDIT_COLUMN_SEL: enable = blkMode == ebtColumn; break;
        case ID_EDIT_STREAM_SEL: enable = blkMode == ebtStream; break;
        }
        pCmdUI->SetRadio(enable);
    }
}

// OpenClipboard fails whenever another window already holds the clipboard open - a clipboard
// manager, Windows clipboard history, an RDP redirector, another application mid-copy. That is a
// normal, transient state rather than an error, and the documented response is to ask again: the
// lock is only ever held for the handful of instructions another process needs to publish or read
// one format.
// Copy and paste used to test it exactly once and give up without a word, which is the worst of the
// available outcomes: the user pressed Ctrl+C, nothing landed on the clipboard, nothing said so, and
// the next Ctrl+V produced whatever had been there BEFORE - which reads as data corruption rather
// than as a keystroke that never happened. Retry briefly instead: 500ms in the pathological case,
// nothing at all in the normal one, and the caller's "it failed" branch stays reachable for a
// clipboard that really is stuck.
static bool OpenClipboardWithRetry (HWND owner, int attempts = 25, DWORD sleepMs = 20)
{
    for (int i = 0; i < attempts; i++)
    {
        if (::OpenClipboard(owner))
            return true;

        ::Sleep(sleepMs);
    }

    return false;
}

void COEditorView::DoEditCopy (const std::wstring& buff, bool append)
{
    if (OpenClipboardWithRetry(NULL))
    {
        CWaitCursor wait;

        if (!append) ::EmptyClipboard();

        if (buff.size())
        {
            HGLOBAL hDataSrc = ::GetClipboardData(CF_UNICODETEXT);
            const wchar_t* src = hDataSrc ? (const wchar_t*)::GlobalLock(hDataSrc) : NULL;
            size_t length = src ? wcslen(src) : 0; 
            
            HGLOBAL hDataDest = ::GlobalAlloc(GMEM_MOVEABLE|GMEM_DDESHARE, (length + buff.size() + 1) * sizeof(wchar_t));
            if (hDataDest)
            {
                wchar_t* dest = (wchar_t*)::GlobalLock(hDataDest);
                if (dest) 
                {
                    if (src) memcpy(dest, src, length * sizeof(wchar_t));
                    memcpy(dest + length, buff.c_str(), (buff.size() + 1) * sizeof(wchar_t));
                }
                if (hDataSrc) ::GlobalUnlock(hDataSrc);
                ::GlobalUnlock(hDataDest);
                ::EmptyClipboard();
                ::SetClipboardData(CF_UNICODETEXT, hDataDest);
            }
        }

        if (IsAltColumnarMode()) // it is just a tag
        {
            string dummy("dummy");
            Common::AppSetClipboardText(dummy.c_str(), dummy.size(), m_AltColumnarTextFormat);
        }

        ::CloseClipboard();
    }
}

void COEditorView::OnEditCopy()
{
    if (!m_bAttached) return;

    if (!IsSelectionEmpty())
    {
        CWaitCursor wait;

        std::wstring buff;
        GetBlock(buff);
        DoEditCopy(buff);
    }
}

void COEditorView::OnEditCut()
{
    if (!m_bAttached) return;

    RETURN_IF_LOCKED

    if (!IsSelectionEmpty())
    {
        OnEditCopy();
        DeleteBlock();
    }
}

static bool endsWithNewline (const wchar_t* s)
{
    size_t n = s ? wcslen(s) : 0;
    return n > 0 && (s[n - 1] == L'\n' || s[n - 1] == L'\r');
}

// Aligns a {code} payload, restoring the trailing newline AlignCodeFragment drops; one impl for paste and drop.
int COEditorView::ApplyCodeFragment (const wchar_t* raw, std::wstring& out)
{
    const bool trailingNewline = endsWithNewline(raw);
    const int indent_size = AlignCodeFragment(raw, out);
    if (trailingNewline)
        out += L'\n';
    return indent_size;
}

void COEditorView::InsertCodeFragment (const wchar_t* raw)
{
    if (!m_bAttached || !raw || !*raw)
        return;

    const EBlockMode orgBlockMode = GetBlockMode();

    if (orgBlockMode != ebtStream)
        SetBlockMode(ebtStream);

    std::wstring text;
    const int indent_size = ApplyCodeFragment(raw, text);

    {
        UndoGroup undoGroup(*this);

        InsertBlock(text.c_str());

        Position caret = GetPosition();

        if (GetLineLength(caret.line) == 0)
        {
            caret.column = indent_size;
            insertAutoIndent(caret);
        }
    }

    if (orgBlockMode != ebtStream)
        SetBlockMode(orgBlockMode);
}

void COEditorView::OnEditPaste()
{
    if (!m_bAttached) return;

    RETURN_IF_LOCKED

    if (OpenClipboardWithRetry(NULL))
    {
        HGLOBAL hData = ::GetClipboardData(CF_UNICODETEXT);

        if (hData != NULL)
        {
            wchar_t* src = (wchar_t*)::GlobalLock(hData);
            if (src)
            {
                if (!IsSelectionEmpty())
                {
                    Square blkPos;
                    GetSelection(blkPos);

                    if (GetBlockMode() == ebtColumn)
                    {
                        if (blkPos.start.column != blkPos.end.column)
                        {
                            Position curPos;
                            curPos.line   = min(blkPos.start.line,   blkPos.end.line);
                            curPos.column = min(blkPos.start.column, blkPos.end.column);
                            DeleteBlock();
                            MoveTo(curPos, true);
                        }
                    }
                    else
                        DeleteBlock();
                }

                bool isAltColumnarText = ::IsClipboardFormatAvailable(m_AltColumnarTextFormat) ?  true : false;

                if (isAltColumnarText && GetBlockMode() != ebtColumn)
                    SetBlockMode(ebtColumn, true);

                std::string info;
                Common::AppGetClipboardText(info, CF_PRIVATEFIRST);
                if (info == "{code}")
                {
                    std::wstring text;
                    ApplyCodeFragment(src, text);
                    InsertBlock(text.c_str());
                }
                else
                    InsertBlock(src);
            }
            ::GlobalUnlock(hData); // handle passible exceptions from block above!!!
        }
        ::CloseClipboard();
    }
}

void COEditorView::OnUpdate_EditCopy (CCmdUI* pCmdUI)
{
    pCmdUI->Enable(!IsSelectionEmpty());
}

void COEditorView::OnUpdate_EditPaste (CCmdUI* pCmdUI)
{
    pCmdUI->Enable(!IsLocked() && IsClipboardFormatAvailable(CF_UNICODETEXT));
}

void COEditorView::OnEditUndo ()
{
    RETURN_IF_LOCKED

    CWaitCursor wait;

    if (m_bAttached) Undo();
}

void COEditorView::OnEditRedo ()
{
    RETURN_IF_LOCKED

    CWaitCursor wait;

    if (m_bAttached) Redo();
}

void COEditorView::OnUpdate_EditUndo (CCmdUI* pCmdUI)
{
    if (m_bAttached && !IsLocked())
        pCmdUI->Enable(CanUndo() ? TRUE : FALSE);
    else
        pCmdUI->Enable(FALSE);
}

void COEditorView::OnUpdate_EditRedo (CCmdUI* pCmdUI)
{
    if (m_bAttached && !IsLocked())
        pCmdUI->Enable(CanRedo() ? TRUE : FALSE);
    else
        pCmdUI->Enable(FALSE);
}

void COEditorView::OnEditSelectAll()
{
    SelectAll();
}

void COEditorView::OnEditDeleteLine()
{
    RETURN_IF_LOCKED

    DeleteLine();
}

void COEditorView::OnEditDeleteWordToLeft()
{
    RETURN_IF_LOCKED

    DeleteWordToLeft();
}

void COEditorView::OnEditDeleteWordToRight()
{
    RETURN_IF_LOCKED

    DeleteWordToRight();
}

void COEditorView::OnEditFind()
{
    std::wstring buff;
    Square sqr;

    GetBlockOrWordUnderCursor(buff, sqr, true/*onlyOneLine*/);
    SetSearchText(buff.c_str());

    // Step 2 of the modeless conversion (md/editor_search_modeless_conversion_plan.md):
    // show/reuse the app-level singleton dialog instead of blocking in
    // DoModal. The word/block just stored via SetSearchText is picked into the
    // combo by the dialog (constructor on first create, RetargetFull on reuse).
    CFindReplaceDlg::ShowModeless(this, FALSE);
}

void COEditorView::OnEditReplace()
{
    RETURN_IF_LOCKED

    std::wstring buff;
    Square sqr;

    if (GetBlockOrWordUnderCursor(buff, sqr, true/*onlyOneLine*/))
        SetSearchText(buff.c_str());

    // Step 2 of the modeless conversion: singleton; when the Find dialog is
    // already open it is recreated with the Replace template (ShowModeless).
    CFindReplaceDlg::ShowModeless(this, TRUE);
}

void COEditorView::OnEditFindNext()
{
    // Step 3.1 bugfix (find-as-you-type): when the modeless Find/Replace dialog
    // is open on THIS view, F3 must search the combo's (possibly uncommitted)
    // text - not the shared searcher's last committed search. Route through the
    // dialog so it commits the combo first (like Enter); falls through to a
    // plain repeat search when no dialog targets this view.
    if (CFindReplaceDlg::RouteViewFindCommand(this, false /*backward*/))
        return;

    COEditorView* pView;
    RepeadSearch(false /*backward*/, pView);
}

void COEditorView::OnEditFindPrevious()
{
    // Step 3.1 bugfix (find-as-you-type): see OnEditFindNext - Shift+F3 routes
    // through the open dialog so it continues the combo's text backwards.
    if (CFindReplaceDlg::RouteViewFindCommand(this, true /*backward*/))
        return;

    COEditorView* pView;
    RepeadSearch(true /*backward*/, pView);
}

void COEditorView::OnEditFindSeletedNext()
{
    Square sqr;
    GetSelection(sqr);
    if (sqr.start.line == sqr.end.line)
    {
        std::wstring buff;
        GetBlockOrWordUnderCursor(buff, sqr, true/*onlyOneLine*/);
        if (!buff.empty())
        {
            SetSearchText(buff.c_str());
            COEditorView* pView;
            RepeadSearch(false /*backward*/, pView);
        }
    }
}

void COEditorView::OnEditFindSeletedPrevious()
{
    Square sqr;
    GetSelection(sqr);
    if (sqr.start.line == sqr.end.line)
    {
        std::wstring buff;
        GetBlockOrWordUnderCursor(buff, sqr, true/*onlyOneLine*/);
        if (!buff.empty())
        {
            SetSearchText(buff.c_str());
            COEditorView* pView;
            RepeadSearch(true /*backward*/, pView);
        }
    }
}

bool COEditorView::Find (const COEditorView*& pView, FindCtx& ctx) const
{
    const OpenEditor::EditContext* pEditContext;

    if (EditContext::Find(pEditContext, ctx))
    {
        pView = static_cast<const COEditorView*>(pEditContext);

        CWnd* pParent = pView->GetParent();
        while (pParent && !pParent->IsKindOf(RUNTIME_CLASS(CMDIChildWnd)))
            pParent = pParent->GetParent();

        if (pParent)
            ((CMDIChildWnd*)(pParent))->MDIActivate();
        else
            _ASSERTE(0);

        return true;
    }
    return false;
}

// Step 3.2 "n of m" match counter (md/editor_search_modeless_conversion_plan.md).
//
// Display cap for the counter. A scan that passes it stops early and the total
// reads "999+", bounding the worst-case work of counting on match-rich documents
// (at most cap+1 hits are enumerated). A document with FEW matches is still
// scanned to EOF - counting is synchronous, the same cost class as the Count
// button (Searcher::DoBatch). That is the acknowledged CP2 limitation; caching
// or an async scan belongs to the Step 4 in-editor widget.
static const int cnMatchCounterCap = 999;

// Wall-clock bound for one counting pass, checked after every hit. The cap above
// bounds MATCH-RICH documents; this bounds the sparse-match huge document, where
// the scan is a full regex pass over every line on each F3/preview tick. When the
// bound trips, the counter is simply not reported (status stays as before Step
// 3.2) - a missing counter is cheap, a stalled editor is not. The FIRST Find call
// is not preemptible, so the worst case remains one full scan per invocation -
// the same cost class as the Count button.
static const DWORD cnMatchCounterTimeLimitMs = 100;

// The one capped forward scan both counter callers share: CountSearchMatches (a TRANSIENT searcher
// holding the dialog's live text/options - a mid-typing count must not touch the committed one) and
// countCommittedMatches (the shared committed searcher). Everything is in RAW INDEX space, the space
// Searcher::Find works in; callers convert the target square with pos2inx first. The loop mirrors
// DoBatch/esbCount so totals agree with the Count button: start at the storage top, advance m_start
// past each hit, no empty-match skip on the very first call (an empty pattern matches at 0:0 and
// that match counts), completion is the wrap (m_eofPassed) rather than Find failing.
// Returns false when the scan cannot produce a counter at all: the time bound tripped. matchIndex is
// 0 when `targetRaw` was not hit within the cap; capped=true means the scan stopped at cap+1 hits
// and the total displays as "cnMatchCounterCap+".
// With "Search All Windows" a hit from ANOTHER storage is not an abandon but the COMPLETION of this
// document's count - Find always starts at `storage` and only moves on after its bottom, so every
// match was enumerated before the crossing. The caller marks that count " in this file".
static bool countMatchesIn (Storage& storage, const Searcher& searcher, const Square& targetRaw,
                            int& matchIndex, int& matchTotal, bool& capped,
                            const Square* regionRaw = 0,
                            std::vector<Square>* collect = 0)
{
    matchIndex = 0;
    matchTotal = 0;
    capped     = false;

    FindCtx ctx;
    ctx.m_storage       = &storage;
    // Step 3 VS-implicit selection region: with a region (RAW INDEX space, the
    // caller converted it with pos2inx), the scan both STARTS and ENDS inside
    // it - matches count only while fully contained, the same containment rule
    // the replace-in-selection batch loop applies, so a scoped "n of m"
    // describes exactly what a scoped Replace All would touch. Without one the
    // scan starts at the top of the storage, as before.
    ctx.m_line          = regionRaw ? regionRaw->start.line : 0;
    ctx.m_start         = ctx.m_end = regionRaw ? regionRaw->start.column : 0;
    ctx.m_backward      = false;
    ctx.m_skipFirstNull = false;  // DoBatch: the first call must not skip an empty match at the document start

    DWORD tickStart = GetTickCount();

    while (searcher.Find(ctx))
    {
        if (ctx.m_storage != &storage)
            break;                // "Search All Windows": the searcher moved on to the next
                                  // document - this one's count is complete (see the header
                                  // comment), not abandoned.

        if (ctx.m_eofPassed)
            break;                // wrapped to the top: scan complete (the wrapped hit is a recount)

        if (regionRaw
        && (ctx.m_line > regionRaw->end.line
        || (ctx.m_line == regionRaw->end.line && ctx.m_end > regionRaw->end.column)))
            break;                // past the region bottom: the scoped count is complete

        ++matchTotal;

        // Step 4 "highlight all occurrences" collector: the scan enumerates in
        // raw index space and the searcher is line-based, so a hit is a
        // single-line raw-index square here; SetMatchOverlay converts it to the
        // display (tab-expanded) columns OnPaint's overlay pass works in.
        // Capped with the counter itself; a timed-out scan leaves a
        // PARTIAL collection behind (still true squares, just not all).
        if (collect && matchTotal <= cnMatchCounterCap)
        {
            Square hit;
            hit.start.line   = hit.end.line = ctx.m_line;
            hit.start.column = ctx.m_start;
            hit.end.column   = ctx.m_end;
            collect->push_back(hit);
        }

        if (!matchIndex
        &&  ctx.m_line  == targetRaw.start.line
        &&  ctx.m_start == targetRaw.start.column
        &&  ctx.m_end   == targetRaw.end.column)
            matchIndex = matchTotal;

        if (matchTotal > cnMatchCounterCap)
        {
            capped = true;
            break;                // bounded work; the index is only knowable within the cap
        }

        if (GetTickCount() - tickStart > cnMatchCounterTimeLimitMs)
            return false;         // huge document: abandon, no counter. Checked every hit, not
                                  // every 256th: the expensive work is the line scan BETWEEN
                                  // hits, and a sparse-match document may never reach 256.

        ctx.m_start = ctx.m_end;  // advance past this match (the esbCount pattern)
        ctx.m_skipFirstNull = true;
    }

    return true;
}

// Shared display policy for the counter, so the dialog-driven and committed
// paths cannot drift apart: "n of m", or "n of 999+" when the scan capped.
// Returns false when there is nothing meaningful to show - the target was never
// identified (matchIndex 0), or it is itself beyond the cap (the 1000th hit
// cannot be rendered as "n of 999+"); callers then leave the status text alone.
static bool formatMatchCounter (int matchIndex, int matchTotal, bool capped, CString& counterText)
{
    if (matchIndex <= 0)
        return false;

    if (capped)
    {
        if (matchIndex > cnMatchCounterCap)
            return false;
        counterText.Format(_T("%d of %d+"), matchIndex, cnMatchCounterCap);
    }
    else
        counterText.Format(_T("%d of %d"), matchIndex, matchTotal);

    return true;
}

bool COEditorView::RepeadSearch (bool backward, COEditorView*& pView)
{
    CWaitCursor wait;

    Square blk;
    GetSelection(blk);
    Position pos = GetPosition();

    if (!blk.is_empty()
    && blk.start.line == pos.line && blk.end.line == pos.line
    && (blk.start.column == pos.column || blk.end.column == pos.column))
    {
        blk.normalize();
        pos = blk.end;
    }
    else
    {
        blk.start = blk.end = pos;
    }

    // 24.10.2004 bug fix, "Next search" is not working after the previous fix (replace all fails on 1 line selection)
    // Step 1 decoupling (md/editor_search_modeless_conversion_plan.md): the
    // direction travels per-call in FindCtx. Step 2.5 completed it: the caller
    // supplies the direction (Find Next forward, Find Previous backward) and no
    // persistent searcher flag exists to read from or write to.
    FindCtx ctx;
    ctx.m_line = blk.end.line;
    ctx.m_start = ctx.m_end = (!backward) ? blk.end.column : blk.start.column;
    ctx.m_backward = backward;

    if (Find((const COEditorView*&)pView, ctx))
    {
        blk.start.line = blk.end.line = ctx.m_line;
        blk.start.column = ctx.m_start;
        blk.end.column = ctx.m_end;

        // "n of m" counter, counted on the view that DISPLAYS the hit (pView): with "Search All
        // Windows" that may be another document, and the count then covers that document only,
        // suffixed " in this file". Cached in m_searchCounterText for the dialog's caption mirror
        // (UpdateTitleCounter reads it after RunCommittedSearch), so the caption shows the counter
        // even when the status line below shows the wrap notice instead.
        //
        // A search that wrapped past the beginning/end ALWAYS continues from the other end and says
        // so with non-blocking status text; the old modal "Move to the beginning/end?" question is
        // gone, because a notification that never takes the keyboard needs no opt-out. That wrap
        // notice WINS over the counter in the status line - it is transient feedback about THIS
        // search, while the counter is standing information the caption and the next F3 repeat.
        CString counterText;
        bool haveCounter = pView && pView->countCommittedMatches(blk, counterText);
        m_searchCounterText = haveCounter ? counterText : CString();

        if (pView == this && ctx.m_eofPassed)
        {
            if (backward)
                Global::SetStatusText("Passed the beginning of the file.");
            else
                Global::SetStatusText("Passed the end of the file.");
        }
        else if (haveCounter)
            Global::SetStatusText(counterText);
        else
            Global::SetStatusText("");

        {
            // A search hit is a DELIBERATE jump, so any fold hiding it opens first. This is the
            // single funnel every find entry point shares - OnEditFindNext/Previous,
            // OnEditFindSeletedNext/Previous and CFindReplaceDlg::OnFindNext, which OnReplace also
            // ends in - so without it the match is selected and the caret parked on a row the user
            // cannot see.
            //
            // Before the vcenter test, not after: that test is a viewport question and revealing
            // changes which rows exist. Not MoveToAndCenter itself, because its ClearSelection(true)
            // would discard the found-text selection the next two lines establish. On pView, which
            // is the view actually displaying the hit and is not always `this` ("Search All
            // Windows" moves it). No-op with folding off.
            //
            // NOTE (pre-existing, deliberately not changed here): vcenter reads `this->m_Rulers[1]`
            // and OnEditScrollCenter() runs on `this`, while the selection and caret move to pView.
            // They differ only across split siblings, where the test is answered by the wrong pane.
            pView->RevealLine(blk.start.line);

            bool vcenter = blk.end.line < m_Rulers[1].m_Topmost || blk.end.line > m_Rulers[1].m_Topmost + m_Rulers[1].m_Count;

            pView->SetSelection(blk);
            pView->MoveTo(blk.start);  // 08/09/2002 bug fix, the searched text has been found but may be still invisible
            if (vcenter) OnEditScrollCenter();
            pView->MoveTo(blk.end);
        }

        // Step 1 decoupling: the shared searcher was not mutated for this
        // call, so there is nothing to restore on the way out.
        return true;
    }

    // Step 3.2 follow-up: a miss leaves no counter behind - the modeless dialog
    // reads this cache for its caption mirror and must not keep showing the
    // previous hit's count.
    m_searchCounterText.Empty();

    CString message("Cannot find the string \'");
    message += GetSearchText();
    message += "\'.";
    Global::SetStatusText(message);
    // Step 2 of the modeless conversion: repeated Find Next from the modeless
    // dialog must not be blocked by a modal popup on every failed step; the
    // status text plus a beep carries the same information without taking the
    // keyboard away (same non-blocking idiom as RETURN_IF_LOCKED above).
    MessageBeep((UINT)-1);

    // Step 2.5 cleanup: nothing to restore on failure - the searcher keeps no
    // direction state, so the old flip-on-failure quirk (a failed Find Previous
    // silently reversed the direction the dialog's next search would use) is
    // gone with it.
    return false;
}

// Step 3.1 find-as-you-type preview (md/editor_search_modeless_conversion_plan.md).
// Called from CFindReplaceDlg's debounce timer (forward, uncommitted dialog text
// from the dialog's stable anchor) and from its scoped committed step
// (RunScopedSearch: committed text, either direction, stepping off the current
// selection inside the region); selects and reveals the hit in THIS view.
// Differences from RepeadSearch are the point: a stack-temporary Searcher never AddRef'ed into any
// storage (the app-wide singleton keeps the committed text/options, so F3 never drifts while
// typing); the search starts at the dialog's anchor, not the live selection; searchAll is forced off;
// no MDIActivate, focus change, status text or beep - the dialog keeps the keyboard and reports the
// outcome itself. An invalid regex THROWS from compileExpression, deliberately not caught here: the
// dialog swallows it as a normal mid-typing state ("(" typed but not yet closed).
bool COEditorView::PreviewSearch (const wchar_t* text, bool wholeWords, bool matchCase,
                                  bool regExpr, const Position& anchor, Square* foundRaw,
                                  bool selectHit, const Square* regionPos, bool backward)
{
    if (!text || !*text)
        return false;

    Searcher searcher;
    searcher.SetText(text);
    searcher.SetOption(wholeWords, matchCase, regExpr, false /*searchAll*/);

    FindCtx ctx;
    ctx.m_storage  = GetStorage();
    ctx.m_line     = anchor.line;
    // The anchor arrives in POSITION coordinates (captured from GetSelection) while Searcher::Find
    // works in RAW INDEX space, so convert - the same pos2inx/inx2pos pair EditContext::Find uses
    // for committed searches. Without it every tab before the anchor shifted the preview start, and
    // the selection it set, right by the tab expansion.
    ctx.m_start    = ctx.m_end = pos2inx(anchor.line, anchor.column, GetCursorBeyondEOL());
    ctx.m_backward = backward;   // typing preview: forward; scoped committed step: per-call

    if (!searcher.Find(ctx))
        return false;

    // A hit must be fully inside the scope region (POSITION coordinates; the bound edge converted
    // with the same pos2inx pair as the anchor above). A forward scan can only escape through the
    // region BOTTOM, a backward scan only through its TOP; a match past that edge is a MISS even
    // though the whole file has occurrences - the dialog then wraps to the other region end
    // (committed step) or reports "cannot find ... in selection" (preview). The opposite edge needs
    // no check: the scan runs away from it.
    if (regionPos)
    {
        if (backward)
        {
            const int startInx = pos2inx(regionPos->start.line, regionPos->start.column, GetCursorBeyondEOL());
            if (ctx.m_line < regionPos->start.line
            || (ctx.m_line == regionPos->start.line && ctx.m_start < startInx))
                return false;
        }
        else
        {
            const int endInx = pos2inx(regionPos->end.line, regionPos->end.column, GetCursorBeyondEOL());
            if (ctx.m_line > regionPos->end.line
            || (ctx.m_line == regionPos->end.line && ctx.m_end > endInx))
                return false;
        }
    }

    // Hand the hit back in the scan's own RAW INDEX space - the CountSearchMatches pass that
    // follows compares every match it enumerates against these coordinates.
    if (foundRaw)
    {
        foundRaw->start.line   = foundRaw->end.line = ctx.m_line;
        foundRaw->start.column = ctx.m_start;
        foundRaw->end.column   = ctx.m_end;
    }

    // Step 3.2 follow-up 2: count-only probe - report where the hit WOULD be
    // (foundRaw) and change nothing else. Opening the Find dialog counts the
    // text it opened with but must not navigate to the first occurrence, and
    // live counting under "Search All Windows" must not move the selection.
    if (!selectHit)
        return true;

    Square blk;
    blk.start.line   = blk.end.line = ctx.m_line;
    blk.start.column = inx2pos(ctx.m_line, ctx.m_start);   // raw -> position, the EditContext::Find pair
    blk.end.column   = inx2pos(ctx.m_line, ctx.m_end);

    // The same reveal/selection tail as RepeadSearch (D5: a search hit is a
    // deliberate jump, so a fold hiding it opens first; vcenter only when the
    // hit is off the viewport), on `this` only - a preview never retargets.
    RevealLine(blk.start.line);

    bool vcenter = blk.end.line < m_Rulers[1].m_Topmost || blk.end.line > m_Rulers[1].m_Topmost + m_Rulers[1].m_Count;

    SetSelection(blk);
    MoveTo(blk.start);
    if (vcenter) OnEditScrollCenter();
    MoveTo(blk.end);
    return true;
}

// Step 3.2 follow-up 3: injection into the built-in "highlight text matching
// the selection" mechanism - the same m_highlightedText member OnUpdate_CurPos
// feeds from the current selection and OnPaint's "Highligting MatchingText"
// block consumes (whole word tokens equal to the string, case-insensitive; see
// the declaration comment for the limitations). Deliberately no navigation:
// only the member and a repaint, selection/scroll/folds stay untouched. The
// value is transient by design - the next pos/sel/action-seq change re-derives
// the member from the selection, exactly as if the user had selected the text.
void COEditorView::HighlightMatchingText (const wchar_t* text)
{
    const std::wstring newText(text ? text : L"");

    // Step 3 VS-implicit region: an explicit injection is gated by the same
    // suppression as the selection-derived maintenance - while the Replace
    // dialog holds a scope region the tint stays off (the band is the
    // region's visual). Clearing (NULL/empty) always passes: the dialog's
    // ClearInjectedFindHighlight must work in every state.
    if (m_suppressMatchingHighlight && !newText.empty())
        return;

    if (m_highlightedText == newText)
        return;

    m_highlightedText = newText;
    Invalidate(FALSE);
}

// Step 3 VS-implicit region: suppress/restore the "highlight text matching
// the selection" mechanism for the lifetime of the modeless Replace dialog's
// scope region - see the declaration comment (OEView.h) for why the scoped
// preview ticks must not feed it. Enabling drops a tint already showing (the
// tick that armed the region may have selected a match first); disabling
// deliberately does NOT re-derive one - the next caret/selection/action
// change feeds the maintenance in OnUpdate_CurPos from the live selection.
void COEditorView::SuppressMatchingTextHighlight (bool suppress)
{
    if (m_suppressMatchingHighlight == suppress)
        return;

    m_suppressMatchingHighlight = suppress;

    if (suppress && !m_highlightedText.empty())
    {
        m_highlightedText.clear();
        Invalidate(FALSE);
    }
}

// Step 4 "highlight all occurrences" overlay - see the declaration comment
// (OEView.h) for the ownership and lifecycle rules. Transient by design: the
// action stamp captured here invalidates the squares at paint time as soon as
// the document is edited; the dialog's next preview tick re-collects them.
// Repainting is unconditional on a content change (the previous overlay may
// have covered squares the new one does not).
void COEditorView::SetMatchOverlay (const std::vector<OpenEditor::Square>& matches)
{
    if (matches.empty())
    {
        ClearMatchOverlay();
        return;
    }

    m_matchOverlay = matches;

    // The collector enumerates in the searcher's raw-index space, but OnPaint's
    // row loop consumes display (tab-expanded) columns - the space
    // Position::column lives in (see the IsInStringAt unit note in OEView_6.cpp),
    // where InxToPix(col - Topmost) places a rect. Convert once, here at the
    // intake: raw columns painted as-is would shift every rect left by the
    // expanded tab cells on any line holding a literal tab before the match.
    // Bounded by the collector's own cap; InxToPos is the identity past EOF.
    for (size_t k = 0; k < m_matchOverlay.size(); ++k)
    {
        OpenEditor::Square& sq = m_matchOverlay[k];
        sq.start.column = InxToPos(sq.start.line, sq.start.column);
        sq.end.column   = InxToPos(sq.end.line,   sq.end.column);
    }

    m_matchOverlayActionId = GetActionId();
    m_matchOverlayValid    = true;
    Invalidate(FALSE);
}

void COEditorView::ClearMatchOverlay ()
{
    const bool wasValid = m_matchOverlayValid;

    m_matchOverlay.clear();
    m_matchOverlayValid = false;

    if (wasValid)
        Invalidate(FALSE);
}

// Step 3.2 "n of m" match counter - dialog/preview side entry point, called by
// CFindReplaceDlg::RunFindPreview right after PreviewSearch selected a hit. Same
// transient-searcher discipline as PreviewSearch itself: the shared committed
// searcher keeps its text/options, so a mid-typing count can never drift the
// F3/Shift+F3 semantics. `targetRaw` is the hit PreviewSearch reported in raw
// index space (its foundRaw out-param) - the space the count scan compares in.
bool COEditorView::CountSearchMatches (const wchar_t* text, bool wholeWords, bool matchCase,
                                       bool regExpr, const Square& targetRaw, CString& counterText,
                                       const Square* regionPos, std::vector<Square>* collect)
{
    counterText.Empty();

    if (collect)
        collect->clear();   // Step 4: a timed-out scan must not append to the caller's stale squares

    if (!text || !*text)
        return false;

    Searcher searcher;
    searcher.SetText(text);
    searcher.SetOption(wholeWords, matchCase, regExpr, false /*searchAll*/);

    int  matchIndex = 0, matchTotal = 0;
    bool capped = false;

    // Step 3 VS-implicit selection region: convert the region (POSITION
    // coordinates - the dialog's capture space) into the RAW INDEX space the
    // scan compares in, the same pos2inx pair countCommittedMatches uses for
    // its target square.
    Square regionRaw;
    if (regionPos)
    {
        regionRaw.start.line   = regionPos->start.line;
        regionRaw.end.line     = regionPos->end.line;
        regionRaw.start.column = pos2inx(regionPos->start.line, regionPos->start.column, GetCursorBeyondEOL());
        regionRaw.end.column   = pos2inx(regionPos->end.line,   regionPos->end.column,   GetCursorBeyondEOL());
    }

    if (!countMatchesIn(*GetStorage(), searcher, targetRaw, matchIndex, matchTotal, capped,
                        regionPos ? &regionRaw : 0, collect))
        return false;

    return formatMatchCounter(matchIndex, matchTotal, capped, counterText);
}

// Step 3.2 "n of m" match counter - committed-search side (RepeadSearch). Counts
// with the SHARED searcher the hit was just found with, so text/options match by
// construction; the scan itself is read-only (FindCtx is per-call, the Searcher
// keeps no scan state - Step 1/2.5 decoupling). `blkPos` is the match square in
// POSITION coordinates (EditContext::Find's output); it is converted back to raw
// index space with the same pos2inx call EditContext::Find used on the way in.
bool COEditorView::countCommittedMatches (const Square& blkPos, CString& counterText)
{
    counterText.Empty();

    Storage* storage = GetStorage();
    if (!storage)
        return false;

    Square targetRaw;
    targetRaw.start.line   = targetRaw.end.line = blkPos.start.line;
    targetRaw.start.column = pos2inx(blkPos.start.line, blkPos.start.column, GetCursorBeyondEOL());
    targetRaw.end.column   = pos2inx(blkPos.end.line,   blkPos.end.column,   GetCursorBeyondEOL());

    int  matchIndex = 0, matchTotal = 0;
    bool capped = false;

    if (!countMatchesIn(*storage, storage->GetSearcher(), targetRaw, matchIndex, matchTotal, capped))
        return false;

    if (!formatMatchCounter(matchIndex, matchTotal, capped, counterText))
        return false;

    // Step 3.2 follow-up: "Search All Windows" no longer suppresses the counter.
    // The shared searcher spans every open document, but the scan completed at
    // the point where it would have crossed into the next one, so the count
    // covers THIS document only - say so instead of hiding it.
    if (storage->GetSearcher().IsSearchAll())
        counterText += _T(" in this file");

    return true;
}

void COEditorView::SetQueueBookmark (int line, bool on)
{
    try
    {
        SetBookmark(line, eBmkGroup1, on);
    }
    catch (const std::out_of_range&)
    {
        MessageBeep((UINT)-1);
        Global::SetStatusText("Cannot toggle a bookmark below EOF!");
    }
}

void COEditorView::OnEditBookmarkToggle ()
{
    try
    {
        Position pos = GetPosition();
        SetBookmark(pos.line, eBmkGroup1, !IsBookmarked(pos.line, eBmkGroup1));
    }
    catch (const std::out_of_range&)
    {
        MessageBeep((UINT)-1);
        Global::SetStatusText("Cannot toggle a bookmark below EOF!");
    }
}

void COEditorView::OnEditBookmarkNext ()
{
    Position pos = GetPosition();

    if (EditContext::NextBookmark(pos.line, eBmkGroup1))
    {
        pos.column = 0;
        MoveToAndCenter(pos);
    }
}

void COEditorView::OnEditBookmarkPrev ()
{
    Position pos = GetPosition();

    if (EditContext::PreviousBookmark(pos.line, eBmkGroup1))
    {
        pos.column = 0;
        MoveToAndCenter(pos);
    }
}

void COEditorView::OnEditBookmarkRemoveAll ()
{
    EditContext::RemoveAllBookmark(eBmkGroup1);
}

void COEditorView::OnSetRandomBookmark (UINT nBookmarkCmd)
{
    RandomBookmark bookmark(static_cast<unsigned char>(nBookmarkCmd - ID_EDIT_BKM_SET_0));

    _CHECK_AND_THROW_(bookmark.Valid(), "Invalid id for random bookmark!");

    try
    {
        const RandomBookmarkArray& bookmarks = GetRandomBookmarks();

        int line = bookmarks.GetLine(bookmark);

        // turn off a bookmark with the same number
        if (line != -1)
            SetRandomBookmark(bookmark, line, false);

        Position pos = GetPosition();
        if (line != pos.line)
        {
            // turn of another bookmark on the line
            // i must do it because of a implementation limitation
            for (RandomBookmark i(RandomBookmark::FIRST); i.Valid(); i++)
                if (bookmarks.GetLine(i) == pos.line)
                    SetRandomBookmark(i, pos.line, false);

            // turn on
            SetRandomBookmark(bookmark, pos.line, true);
        }
    }
    catch (const std::out_of_range&)
    {
        MessageBeep((UINT)-1);
        Global::SetStatusText("Cannot toggle a bookmark below EOF!");
    }
}

void COEditorView::OnGetRandomBookmark (UINT nBookmarkCmd)
{
    RandomBookmark bookmark(static_cast<unsigned char>(nBookmarkCmd - ID_EDIT_BKM_GET_0));

    _CHECK_AND_THROW_(bookmark.Valid(), "Invalid id for random bookmark!");

    Position pos;
    pos.column = 0;
    pos.line   = 0;

    if (GetRandomBookmark(bookmark, pos.line))
        MoveToAndCenter(pos);
}

void COEditorView::OnUpdate_GetRandomBookmark (CCmdUI* pCmdUI)
{
    int line;
    RandomBookmark bookmark(static_cast<unsigned char>(pCmdUI->m_nID - ID_EDIT_BKM_GET_0));
    pCmdUI->Enable((bookmark.Valid() && GetRandomBookmark(bookmark, line)) ? TRUE : FALSE);
}

void COEditorView::OnUpdate_BookmarkGroup (CCmdUI* pCmdUI)
{
    pCmdUI->Enable(!IsLocked() && GetBookmarkNumber(eBmkGroup1) > 0);
}

void COEditorView::OnUpdate_BookmarkGroupRO (CCmdUI* pCmdUI)
{
    pCmdUI->Enable(GetBookmarkNumber(eBmkGroup1) > 0);
}

void COEditorView::OnContextMenu (CWnd*, CPoint point)
{
    if (point.x == -1 && point.y == -1)
    {
        Position pos = GetPosition();

        // 31.03.2003 bug fix, editor context menu position is wrong on Shift+F10
        point.x = m_Rulers[0].PosToPix(pos.column);
        point.y = LineToPixY(pos.line) + m_Rulers[1].m_CharSize/2;

        CRect rc;
        GetClientRect(rc);
        if (!rc.PtInRect(point))
            point.x = point.y = 4;

        ClientToScreen(&point);
    }
    else
    {
        CPoint pt = point;
        ScreenToClient(&pt);

        // A right-click inside the left margin - line numbers, bookmarks, the
        // fold band - gets the outlining menu (ToDoNotes CodeFolding item 2)
        // instead of the text-area one. The whole margin and not only
        // InFoldBand's strip: the band exists only while folding is on, and the
        // menu's last item is what switches folding back on, so the menu has to
        // stay reachable with the band gone. Shift+F10 (point == -1,-1, handled
        // above) keeps the ordinary menu - there is no gutter point to speak of.
        CRect rcMargin;
        GetClientRect(rcMargin);

        if (rcMargin.PtInRect(pt) && pt.x < m_Rulers[0].m_Indent)
        {
            TrackFoldGutterMenu(point);
            return;
        }

        if (!CursorOnSelection(pt)
        && (pt.x >= m_Rulers[0].m_Indent) && (pt.y >= m_Rulers[1].m_Indent))
        {
            CRect rc;
            GetClientRect(rc);
            if (rc.PtInRect(pt))
            {
                Position pos = MouseToPos(pt);
        
                if (!IsSelectionEmpty())
                    ClearSelection(true);

                MoveTo(pos);
            }
        }
    }

    CMenu menu;
    int resid = (0xFF00 & GetKeyState(VK_SHIFT)) ? IDR_OE_WORKBOOK_POPUP : IDR_OE_EDIT_POPUP;
    VERIFY(menu.LoadMenu(resid));
    CMenu* pPopup = menu.GetSubMenu(0);

    if (resid == IDR_OE_EDIT_POPUP)
        GetDocument()->OnContextMenuInit(pPopup);

    ASSERT(pPopup != NULL);
    ASSERT_KINDOF(CFrameWnd, AfxGetMainWnd());
    Common::GUICommandDictionary::AddAccelDescriptionToMenu(pPopup->m_hMenu);

    CString path = GetDocument()->GetPathName();

    bool prop = GetSettings().GetFileManagerShellContexMenuProperties();
    bool git  = GetSettings().GetFileManagerShellContexMenuTortoiseGIT();
    bool svn  = GetSettings().GetFileManagerShellContexMenuTortoiseSVN();

    if ((prop || git || svn) && (0xFF00 & GetKeyState(VK_SHIFT)) && ::PathFileExists(path))
    {
        if (UINT idCommand = CustomShellContextMenu(this, point, path, pPopup, prop, git, svn).ShowContextMenu())
            SendMessage(WM_COMMAND, idCommand, 0);
    }
    else
        pPopup->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
}

// The outlining context menu for a right-click inside the left margin
// (md/CodeFolding.md, ToDoNotes CodeFolding item 2). Owner is THIS view, which
// means WM_INITMENUPOPUP lands in COEditorView::OnInitMenuPopup above - and
// that handler enables a fixed list of edit commands by hand and never
// dispatches ON_UPDATE_COMMAND_UI. So the state pass CFrameWnd would run for a
// frame-owned menu is run here, against this view rather than whichever pane
// happens to be active (a split's inactive pane has the same gutter): the ten
// fold commands gray on their two enablement questions, and the Folding
// Enabled item picks up its check. DoUpdate handles separators (ID -1) itself.
void COEditorView::TrackFoldGutterMenu (const CPoint& screenPoint)
{
    CMenu menu;
    VERIFY(menu.LoadMenu(IDR_OE_FOLD_POPUP));
    CMenu* pPopup = menu.GetSubMenu(0);
    ASSERT(pPopup != NULL);

    CCmdUI cmdUI;
    cmdUI.m_pOther     = NULL;
    cmdUI.m_pMenu      = pPopup;
    cmdUI.m_nIndexMax  = pPopup->GetMenuItemCount();

    for (UINT i = 0; i < cmdUI.m_nIndexMax; i++)
    {
        cmdUI.m_nIndex = i;
        cmdUI.m_nID    = pPopup->GetMenuItemID(i);
        cmdUI.DoUpdate(this, FALSE);
    }

    Common::GUICommandDictionary::AddAccelDescriptionToMenu(pPopup->m_hMenu);

    pPopup->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y, this);
}

void COEditorView::OnInitMenuPopup(CMenu* pPopupMenu, UINT nIndex, BOOL bSysMenu)
{
    if (!bSysMenu && pPopupMenu)
    {
        UINT flag = (IsSelectionEmpty()) ? MF_BYCOMMAND|MF_GRAYED : MF_BYCOMMAND|MF_ENABLED;
        pPopupMenu->EnableMenuItem(ID_EDIT_COPY, flag);
        
        flag = (IsLocked() || IsSelectionEmpty()) ? MF_BYCOMMAND|MF_GRAYED : MF_BYCOMMAND|MF_ENABLED;
        pPopupMenu->EnableMenuItem(ID_EDIT_CUT, flag);

        flag = (IsLocked() || !IsClipboardFormatAvailable(CF_UNICODETEXT)) ? MF_BYCOMMAND|MF_GRAYED : MF_BYCOMMAND|MF_ENABLED;
        pPopupMenu->EnableMenuItem(ID_EDIT_PASTE, flag);

        flag = (!CanUndo()) ? MF_BYCOMMAND|MF_GRAYED : MF_BYCOMMAND|MF_ENABLED;
        pPopupMenu->EnableMenuItem(ID_EDIT_UNDO, flag);

        flag = (!CanRedo()) ? MF_BYCOMMAND|MF_GRAYED : MF_BYCOMMAND|MF_ENABLED;
        pPopupMenu->EnableMenuItem(ID_EDIT_REDO, flag);

        flag = (IsSearchTextEmpty() && !CFindReplaceDlg::IsOpenFor(this)) ? MF_BYCOMMAND|MF_GRAYED : MF_BYCOMMAND|MF_ENABLED;
        pPopupMenu->EnableMenuItem(ID_EDIT_FIND_NEXT, flag);
        pPopupMenu->EnableMenuItem(ID_EDIT_FIND_PREVIOUS, flag);

        flag = (IsLocked()) ? MF_BYCOMMAND|MF_GRAYED : MF_BYCOMMAND|MF_ENABLED;
        pPopupMenu->EnableMenuItem(ID_EDIT_REPLACE, flag);
    }
    else
        CView::OnInitMenuPopup(pPopupMenu, nIndex, bSysMenu);
}

void COEditorView::OnUpdate_EditFindNext(CCmdUI* pCmdUI)
{
    // Also enable when a modeless Find/Replace dialog targeting this view is open:
    // OnEditFindNext/OnEditFindPrevious then run the dialog's committed search
    // (via RouteViewFindCommand) even when the view itself has no committed search
    // text yet - e.g. when the dialog was opened over a multi-line selection. Without
    // this, MFC drops accelerator-generated WM_COMMANDs (F3 / Shift+F3) as "disabled
    // command" before the handler ever runs, so the first F3 appears dead until a
    // Find Next click commits the search text into the view.
    pCmdUI->Enable(!IsSearchTextEmpty() || CFindReplaceDlg::IsOpenFor(this));
}

void COEditorView::OnUpdate_SelectionOperation (CCmdUI* pCmdUI)
{
    pCmdUI->Enable(!IsLocked() && !IsSelectionEmpty());
}

void COEditorView::OnUpdate_StreamSelectionOperation (CCmdUI* pCmdUI)
{
    pCmdUI->Enable(!IsLocked() && !IsSelectionEmpty() && (GetBlockMode() == EBlockMode::ebtStream));
}

void COEditorView::OnEditLower ()
{
    RETURN_IF_LOCKED

    CWaitCursor wait;
    ScanAndReplaceText(LowerText, IsSelectionEmpty());
}

void COEditorView::OnEditUpper ()
{
    RETURN_IF_LOCKED

    CWaitCursor wait;
    ScanAndReplaceText(UpperText, IsSelectionEmpty());
}

void COEditorView::OnEditCapitalize()
{
    RETURN_IF_LOCKED

    CWaitCursor wait;
    ScanAndReplaceText(CapitalizeText, IsSelectionEmpty());
}

void COEditorView::OnEditInvertCase()
{
    RETURN_IF_LOCKED

    CWaitCursor wait;
    ScanAndReplaceText(InvertCaseText, IsSelectionEmpty());
}

void COEditorView::OnBlockTabify()
{
    RETURN_IF_LOCKED

    CWaitCursor wait;

    if (GetBlockMode() == ebtColumn)
    {
        UndoGroup undoGroup(*this);
        PushInUndoStack(GetPosition());
        Square orgBlkPos;
        GetSelection(orgBlkPos);
        PushInUndoStack(GetBlockMode(), IsAltColumnarMode(), orgBlkPos);

        Square blk = orgBlkPos;
        blk.normalize();
        blk.normalize_columns();
        int tabSpacing = GetTabSpacing();
        blk.start.column = (blk.start.column / tabSpacing) * tabSpacing;
        blk.end.column = (blk.end.column / tabSpacing) * tabSpacing + tabSpacing;
        SetSelection(blk);

        ScanAndReplaceText(TabifyText, false);
        SetSelection(orgBlkPos);
    }
    else
        ScanAndReplaceText(TabifyText, false);
}

void COEditorView::OnBlockUntabify()
{
    RETURN_IF_LOCKED

    CWaitCursor wait;

    if (GetBlockMode() == ebtColumn)
    {
        UndoGroup undoGroup(*this);
        PushInUndoStack(GetPosition());
        Square orgBlkPos;
        GetSelection(orgBlkPos);
        PushInUndoStack(GetBlockMode(), IsAltColumnarMode(), orgBlkPos);

        Square blk = orgBlkPos;
        blk.normalize();
        blk.normalize_columns();
        int tabSpacing = GetTabSpacing();
        blk.start.column = (blk.start.column / tabSpacing) * tabSpacing;
        blk.end.column = (blk.end.column / tabSpacing) * tabSpacing + tabSpacing;
        SetSelection(blk);

        ScanAndReplaceText(UntabifyText, false);
        SetSelection(orgBlkPos);
    }
    else
        ScanAndReplaceText(UntabifyText, false);
}


void COEditorView::OnBlockTabifyLeading()
{
    RETURN_IF_LOCKED

    CWaitCursor wait;

    if (GetBlockMode() == ebtColumn)
    {
        UndoGroup undoGroup(*this);
        PushInUndoStack(GetPosition());
        Square orgBlkPos;
        GetSelection(orgBlkPos);
        PushInUndoStack(GetBlockMode(), IsAltColumnarMode(), orgBlkPos);

        Square blk = orgBlkPos;
        blk.normalize();
        blk.normalize_columns();
        int tabSpacing = GetTabSpacing();
        blk.start.column = (blk.start.column / tabSpacing) * tabSpacing;
        blk.end.column = (blk.end.column / tabSpacing) * tabSpacing + tabSpacing;
        SetSelection(blk);

        ScanAndReplaceText(TabifyLeadingSpaces, false);
        SetSelection(orgBlkPos);
    }
    else
        ScanAndReplaceText(TabifyLeadingSpaces, false);
}

void COEditorView::OnEditIndent()
{
    RETURN_IF_LOCKED

    CWaitCursor wait;

    IndentBlock();
}

void COEditorView::OnEditUndent()
{
    RETURN_IF_LOCKED

    CWaitCursor wait;

    UndentBlock();
}

void COEditorView::OnEditSort()
{
    RETURN_IF_LOCKED

    COESortDlg dlg;

    if (dlg.DoModal() == IDOK)
    {
        CWaitCursor wait;

        SortCtx ctx;

        ctx.mKeyOrder          = dlg.mKeyOrder        ;
        ctx.mKeyStartColumn    = dlg.mKeyStartColumn  ;
        ctx.mKeyLength         = dlg.mKeyLength       ;
        ctx.mRemoveDuplicates  = dlg.mRemoveDuplicates;
        ctx.mIgnoreCase        = dlg.mIgnoreCase      ;

        Sort(ctx);
    }
}

// One wall-clock budget for the whole bracket-match command, shared by both handlers below. A NAMED
// CONSTANT rather than a literal so the two handlers cannot drift apart; it is also the number that
// decides how long the UI is unresponsive on a very large cold document, and needs measurement on a
// large file before being treated as settled.
//
// Established ONCE, here, and not per FindMatch call: one EditContext::FindMatch(true) reaches
// PlSqlSupport::FindMatch up to three times and the forced parse itself walks the document in N
// batches, so a per-call timeout would compound into N x 3 x T instead of bounding the command.
// Deriving the deadline here also discharges "must be a no-op when already parsed" by construction:
// by the time the first inner FindMatch runs all three calls read the published tree without waiting.
static const ULONGLONG kFindMatchParseBudgetMs = 3000;

void COEditorView::OnEditFindMatch()
{
    // The wait cursor already spans the whole command, and it is the right place for it:
    // one cursor over three inner FindMatch calls rather than three constructed and
    // destroyed inside EditContext. Do NOT add a second, nested one below.
    CWaitCursor wait;

    // Join the background parse before matching - a bounded join on the worker, which keeps the wait
    // BATCH-bounded (m_batchLimit lines at a time, deadline re-checked between batches) instead of
    // document-bounded.
    //
    // The return value is deliberately ignored. On timeout nothing is torn down and nothing is
    // cancelled: the abandoned batch keeps running and the next idle tick commits it through the
    // single publication site, so the document is never left inconsistent. EditContext::FindMatch
    // then reads whatever was published in time and falls back to the text scan on a miss - the same
    // degradation it already applies, with no modal message. So the first Ctrl+[ on a very large
    // cold document may return the cruder match, the parse finishes a moment later on its own, and a
    // second Ctrl+[ is instant and correct.
    if (COEDocument* pDoc = GetDocument())
        pDoc->WaitForParseQuiescent(::GetTickCount64() + kFindMatchParseBudgetMs);

    EditContext::FindMatch(false);
}

void COEditorView::OnEditFindMatchAndSelect()
{
    CWaitCursor wait;

    // Same join, same silent degradation - see OnEditFindMatch. This is the variant that
    // reaches PlSqlSupport::FindMatch three times, which is exactly why the deadline is
    // computed once here rather than inside EditContext.
    if (COEDocument* pDoc = GetDocument())
        pDoc->WaitForParseQuiescent(::GetTickCount64() + kFindMatchParseBudgetMs);

    EditContext::FindMatch(true);
}

void COEditorView::OnUpdate_CommentUncomment (CCmdUI* pCmdUI)
{
    pCmdUI->Enable(!IsLocked() && GetSettings().GetLanguage() != "NONE");
}

void COEditorView::OnEditComment ()
{
    RETURN_IF_LOCKED

    DoCommentText(true);
}

void COEditorView::OnEditUncomment ()
{
    RETURN_IF_LOCKED

    DoCommentText(false);
}

void COEditorView::DoCommentText (bool comment)
{
    RETURN_IF_LOCKED

    CWaitCursor wait;

    Position pos = GetPosition();
    EBlockMode blkMode = GetBlockMode();
    bool selected = !IsSelectionEmpty();

    if (selected && blkMode == ebtColumn)
    {
        AfxMessageBox(L"The comment/uncomment operation currently supports only stream selection.", MB_OK|MB_ICONSTOP);
        return;
    }

    try
    {
        LanguagePtr lang = LanguagesCollection::Find(GetSettings().GetLanguage());
        
        if (lang->GetEndLineComment().empty())
        {
            AfxMessageBox(Common::wstr(std::string("Line comment string has not been defined for language \"") 
                + lang->GetName() + "\".").c_str(), MB_OK|MB_ICONSTOP);
            return;
        }

        if (selected)
            ConvertSelectionToLines();
        else
            SelectLine(pos.line);

        if (comment)
            ScanAndReplaceText(CommentText, false);
        else
            ScanAndReplaceText(UncommentText, false);

        if (!selected)
        {
            ClearSelection(true);
            SetBlockMode(blkMode);
        }
    }
    catch (const std::logic_error& x) // language not found
    {
        AfxMessageBox(Common::wstr(std::string("Cannot comment/uncomment the selection.\n") + x.what()).c_str(), MB_OK|MB_ICONSTOP);
        return;
    }
}

bool COEditorView::CommentText (const OpenEditor::EditContext& context, std::wstring& str)
{
    std::wstring lineComment = Common::wstr(
        LanguagesCollection::Find(context.GetSettings().GetLanguage())
            ->GetEndLineComment());

    // skip spaces and tabs
    int i = 0, len = str.size();
    for (; iswspace(str[i]) && i < len; i++)
        ;

    // comment line
    if (!str.empty() && i < len)
        str = lineComment + str;

    return true;
}

bool COEditorView::UncommentText (const OpenEditor::EditContext& context, std::wstring& str)
{
    std::wstring lineComment = Common::wstr(
        LanguagesCollection::Find(context.GetSettings().GetLanguage())
            ->GetEndLineComment());

    // skip spaces and tabs
    int i = 0, len = str.size();
    for (; iswspace(str[i]) && i < len; i++)
        ;


    if (!str.empty() && i < len 
    && !wcsncmp(str.c_str() + i, lineComment.c_str(), lineComment.length()))
        str.erase(i, lineComment.length());

    return true;
}

void COEditorView::OnEditGoto()
{
    COEGoToDialog dlg(this);

    Position pos = GetPosition();

    static int whereFrom = 0;

    dlg.m_WhereFrom = whereFrom;
    dlg.m_WhereTo = (dlg.m_WhereFrom == 0) ? pos.line + 1 : 0;
    dlg.m_InfoString.Format(
        L"There are %u lines in the file. The current line is %u.",
        GetLineCount(), pos.line + 1);

    if (dlg.DoModal() == IDOK)
    {
        switch (dlg.m_WhereFrom)
        {
        case 0: // from top of the file
            pos.line = dlg.m_WhereTo - 1;
            break;
        case 1: // from bottom of the file
            pos.line = GetLineCount() - dlg.m_WhereTo;
            break;
        case 2: // from current position
            pos.line += dlg.m_WhereTo;
            break;
        }

        MoveToAndCenter(pos);

        whereFrom = dlg.m_WhereFrom;
    }
}

void COEditorView::OnEditScrollUp ()
{
    AdjustCaretPosition();

    DoVScroll(SB_LINEUP, TRUE);

    if (GetPosition().line >= m_Rulers[1].m_Topmost + m_Rulers[1].m_Count)
        GoToUp();

    AdjustCaretPosition();
    AdjustVertSibling();
}

void COEditorView::OnEditScrollDown ()
{
    AdjustCaretPosition();

    DoVScroll(SB_LINEDOWN, TRUE);

    if (GetPosition().line < m_Rulers[1].m_Topmost)
        GoToDown();

    AdjustCaretPosition();
    AdjustVertSibling();
}

void COEditorView::OnEditScrollCenter ()
{
    // Centering is a viewport calculation, so it runs in rows and then stores
    // the visible LINE the target row shows. Going through RowToLine also
    // guarantees m_Topmost is never a line hidden inside a collapsed run, which
    // a raw `pos.line - m_Count/2` could produce. With folding off the row is
    // the line and the result is the old one.
    int topRow = LineToRow(GetPosition().line) - m_Rulers[1].m_Count/2;

    if (topRow < 0) topRow = 0;

    const int topmost = RowToLine(topRow);

    if (topmost != m_Rulers[1].m_Topmost)
    {
        m_Rulers[1].m_Topmost = topmost;
        Invalidate(FALSE);
        AdjustHorzScroller();
        AdjustVertSibling();
        SetCaretPosition();
    }

}

void COEditorView::DoEditExpandTemplate (TemplatePtr templ, bool canModifyTemplate)
{
    RETURN_IF_LOCKED

    if (!ExpandTemplate(templ)) 
    {
        if (!m_autocompleteList.m_hWnd) 
            m_autocompleteList.Create();

        if (m_autocompleteList.m_hWnd) 
        {
            m_autocompleteList.ShowControl(this, templ, canModifyTemplate);
        }
    }
}

bool COEditorView::DoInsertTemplateEntry (TemplatePtr templ, int index)
{
    // RETURN_IF_LOCKED returns void, so the same two tests are spelled out here: a
    // detached view has no buffer to insert into, and a locked one has to say so rather
    // than quietly do nothing and leave the caller reporting success.
    if (!m_bAttached)
        return false;

    if (IsLocked())
    {
        Global::SetStatusText("The content is locked and cannot be modified!");
        MessageBeep((UINT)-1);
        return false;
    }

    if (!templ || index < 0)
        return false;

    // The index overload of ExpandKeyword, which is what makes this insert the entry the
    // caller named. The keyword overload matches against the word under the caret, and
    // there is no such word when the request came from a pane.
    return ExpandTemplate(templ, index);
}


void COEditorView::OnEditExpandTemplate ()
{
    RETURN_IF_LOCKED

    DoEditExpandTemplate(
        COEDocument::GetSettingsManager().GetTemplateByName(
            GetSettings().GetName()
        ),
        true // canModifyTemplate
    );
}

void COEditorView::OnEditCreateTemplate ()
{
    RETURN_IF_LOCKED

    std::wstring text;
    {
        Square blk;
        GetSelection(blk);
        blk.normalize();
        // doubt that more 100 lines text can be template
        if (!blk.is_empty() && (blk.start.line - blk.start.line) < 100)
            GetBlock(text);
    }

    COEDocument::ShowTemplateDialog( 
        COEDocument::GetSettingsManager().GetTemplateByName(GetSettings().GetName()),
          -2,  // add new one
          text // use selection
        );
}

void COEditorView::OnOpenFileUnderCursor ()
{
    Square tmp_sqr;
    std::wstring cursor_filename;
    CString curr_path;
    bool file_exists=false;

    // TODO: use doc/class specific delimiters and handle quoted strings
    // TODO: parse whole line like "#include <stdio.h>" or "@catalog.sql"
    // TODO: add default ext for "@catalog"
    // TODO: add doc/class specific "PATH"

    // get word & do simple checks
    if(!GetBlockOrWordUnderCursor(cursor_filename, tmp_sqr, false, " \t<>\"@"))
        return;
    
    if(cursor_filename.length() > MAX_PATH) 
        THROW_APP_EXCEPTION("File name is too long: \"" + Common::str(cursor_filename) + "\"");

    // search in the current path directory
    if (GetSettings().GetWorkDirFollowsDocument())
    {
        curr_path = GetDocument()->GetPathName();
        if (LPTSTR file = ::PathFindFileName(curr_path.GetBuffer()))
            *file = 0;
        curr_path.ReleaseBuffer();
        curr_path += cursor_filename.c_str();
        file_exists = PathFileExists(curr_path);
    }
    else
    {
        // do we really need a full path?
        //getcwd(curr_path.GetBuffer(MAX_PATH+1), MAX_PATH);
        if (GetCurrentDirectory(MAX_PATH, curr_path.GetBuffer(MAX_PATH+1)) > 0)
        {
            curr_path.ReleaseBuffer();
            curr_path += "\\";
            curr_path += cursor_filename.c_str();
            file_exists = PathFileExists(curr_path);
        }
    }

    // TODO: this behaviour has to be documented because it's not usual (actually I'm not sure)
    // search through all templates
    POSITION pos_templ=AfxGetApp()->GetFirstDocTemplatePosition();
    while(file_exists == false && pos_templ != NULL) {
        CDocTemplate *doc_templ = AfxGetApp()->GetNextDocTemplate(pos_templ);
        POSITION doc_pos = doc_templ->GetFirstDocPosition();
        // through all documents in one template
        while (doc_pos != NULL)
        {
            if(CDocument* pDoc = doc_templ->GetNextDoc(doc_pos)) 
            {
                curr_path = pDoc->GetPathName();
                if (LPTSTR file = ::PathFindFileName(curr_path.GetBuffer()))
                    *file = 0;
                curr_path.ReleaseBuffer();
                curr_path += cursor_filename.c_str();
                if (PathFileExists(curr_path))
                {
                    file_exists = true;
                    break;
                }
            }
        }
    }

    // search through the PATH directories
    if (!file_exists)
    {
        CString env_path;
        env_path.GetEnvironmentVariable(L"PATH");
        int curPos = 0;
        curr_path = env_path.Tokenize(L";", curPos);
        while (!curr_path.IsEmpty()) 
        {
            if (curr_path.GetAt(curr_path.GetLength()-1) != '\\')
                curr_path += "\\";

            curr_path += cursor_filename.c_str();

            if (PathFileExists(curr_path))
            {
                file_exists = true;
                break;
            }
            curr_path = env_path.Tokenize(L";", curPos);
        }
    }

    if(file_exists == true) 
        AfxGetApp()->OpenDocumentFile(curr_path);
    else 
        THROW_APP_EXCEPTION("Cannot find file: \"" + Common::str(cursor_filename) + "\"");
}    

void COEditorView::MoveToBottom ()
{
    ClearSelection(true);
    GoToBottom();
    AdjustCaretPosition();
}

void COEditorView::MoveToAndCenter (Position pos)
{
    // Open any fold hiding the target BEFORE moving, and before the `center` test
    // below: that test is a viewport question, and revealing changes which rows
    // exist. This is the choke point the deliberate jumps already share - Go To
    // line, bookmark next/prev, random-bookmark get, and the caret restore after
    // Undo - so one call here covers all of them (md/CodeFolding.md D5).
    //
    // Expanding can never move the caret: it only makes lines visible, so a line
    // that was visible stays visible and ApplyFoldChange's caret snap is a no-op.
    RevealLine(pos.line);

    bool center 
        = pos.line < m_Rulers[1].m_Topmost 
            || pos.line > m_Rulers[1].m_Topmost + m_Rulers[1].m_Count;

    if (!IsSelectionEmpty())
        ClearSelection(true);

    MoveTo(pos);

    if (center) 
        OnEditScrollCenter();
}

void COEditorView::GetBlock (std::wstring& str, const Square* sqr) const
{
    CWaitCursor wait;

    EditContext::GetBlock(str, sqr);
}

void COEditorView::InsertBlock (const wchar_t* str, bool hideSelection, bool putSelInUndo)
{
    CWaitCursor wait;

    EditContext::InsertBlock(str, hideSelection, putSelInUndo);
}

void COEditorView::DeleteBlock (bool putSelInUndo)
{
    CWaitCursor wait;

    // Stage 6 B8/B9: this is the single funnel for every columnar delete -
    // EditContext::ColumnarDelete (:1221, :1227) and ColumnarBackspace (:1195,
    // :1202) both reach it by virtual dispatch, as does ColumnarInsert's
    // delete-the-block-first half (:984) and the `else DeleteBlock()` fallback the
    // key handlers take when ColBlockEditMode is off. The column branch
    // (OEContext_3.cpp:890-910) writes the Square's whole row range, so any hidden
    // row inside it has to be visible first. Gated on ebtColumn inside, so a stream
    // delete never expands anything and B6's resolution stands.
    RevealColumnarEditRows();

    EditContext::DeleteBlock(putSelInUndo);
}

void COEditorView::OnEditDelete ()
{
    if (!m_bAttached) return;

    RETURN_IF_LOCKED

    if (!IsSelectionEmpty() && GetBlockDelAndBSDelete())
    {
        if (GetBlockMode() == ebtColumn
        && GetColBlockEditMode())
            ColumnarDelete();
        else
            DeleteBlock();
    }
    else
        Delete();
}

void COEditorView::OnEditCutAndAppend ()
{
    if (!m_bAttached) return;

    RETURN_IF_LOCKED

    if (!IsSelectionEmpty())
    {
        OnEditCopyAndAppend();
        DeleteBlock();
    }
}

void COEditorView::OnEditCopyAndAppend ()
{
    if (!m_bAttached) return;

    if (!IsSelectionEmpty())
    {
        CWaitCursor wait;

        std::wstring buff;
        GetBlock(buff);
        DoEditCopy(buff, true);
    }
}

void COEditorView::OnEditCutBookmarked ()
{
    RETURN_IF_LOCKED

    OnEditCopyBookmarked();
    OnEditDeleteBookmarked();
}

void COEditorView::OnEditCopyBookmarked ()
{
    CWaitCursor wait;

    std::wstring buff;
    CopyBookmarkedLines(buff);
    DoEditCopy(buff);
}

void COEditorView::OnEditDeleteBookmarked ()
{
    RETURN_IF_LOCKED

    CWaitCursor wait;

    DeleteBookmarkedLines();
}

void COEditorView::OnEditSelectWord ()
{
    Square selection;
    if (WordFromPoint(GetPosition(), selection))
    {
        MoveTo(selection.end);
        SetSelection(selection);
        InvalidateSquare(selection);
    }
}

void COEditorView::OnEditSelectLine ()
{
    SelectLine(GetPosition().line);
}

BOOL COEditorView::OnMouseWheel(UINT /*nFlags*/, short zDelta, CPoint /*pt*/)
{
    SCROLLINFO scrollInfo;
    scrollInfo.cbSize = sizeof(scrollInfo);
    scrollInfo.fMask = SIF_TRACKPOS;

    GetScrollInfo(SB_VERT, &scrollInfo);

    if (scrollInfo.nMax >= (int)scrollInfo.nPage)
    {
        if (!m_uWheelScrollLines)
            ::SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &m_uWheelScrollLines, 0);

        if (m_uWheelScrollLines == WHEEL_PAGESCROLL)
        {
            DoVScroll(zDelta > 0 ? SB_PAGEDOWN : SB_PAGEUP, TRUE);
        }
        else
        {
            int nToScroll = ::MulDiv(zDelta, m_uWheelScrollLines, WHEEL_DELTA);

            if (zDelta > 0)
                while (nToScroll--)
                    DoVScroll(SB_LINELEFT, TRUE);
            else
                while (nToScroll++)
                    DoVScroll(SB_LINERIGHT, TRUE);
        }
    }

    return FALSE;
    //return CView::OnMouseWheel(nFlags, zDelta, pt);
}

void COEditorView::OnSettingChange (UINT, LPCTSTR)
{
    m_uWheelScrollLines = 0;
}

void COEditorView::OnActivateView(BOOL bActivate, CView* pActivateView, CView* pDeactiveView)
{
    if (!bActivate)
    {
        if (m_autocompleteList.IsActive()) 
            m_autocompleteList.HideControl();
    }
    else
    {
        // The modeless Find/Replace dialog follows the editor the user works in
        // (md/editor_search_modeless_conversion_plan.md): activating another
        // document - or another split pane of this one - retargets the app-wide
        // singleton, so Find Next no longer keeps searching the file the dialog
        // was opened over. No-op unless the dialog is open and visible and
        // targets a different view; the "All Windows" scope opts out inside.
        // CFrameWnd::SetActiveView stores m_pViewActive BEFORE this notification
        // (winfrm.cpp:1336-1340), so the dialog's own active-view lookups
        // already report `this`.
        CFindReplaceDlg::NotifyViewActivated(this);
    }

    CView::OnActivateView(bActivate, pActivateView, pDeactiveView);
}

void COEditorView::OnEditNormalizeText()
{
    RETURN_IF_LOCKED

    CWaitCursor wait;
    ScanAndReplaceText(NormalizeText, IsSelectionEmpty());
}

void COEditorView::OnEditDatetimeStamp()
{
    string format = GetSettings().GetTimestampFormat();
    Common::date_oracle_to_c(format.c_str(), format);
    
    if (!format.empty())
    {
        time_t t = time(0);
        if (const tm* lt = localtime(&t)) 
        {
            wchar_t buff[80];
            wcsftime(buff, sizeof buff, Common::wstr(format).c_str(), lt);
            buff[sizeof(buff)/sizeof(buff[0])-1] = 0;

            if (wcslen(buff) > 0)
            {
                UndoGroup undGroup(*this);

                if (!IsSelectionEmpty())
                    if (GetBlockTypingOverwrite()) DeleteBlock();
                    else ClearSelection(true);

                InsertBlock(buff);
            }
        }
        else
            AfxMessageBox(L"Datetime stamp failed. Unknown reason.", MB_OK|MB_ICONSTOP);
    }
    else
        AfxMessageBox(L"Datetime stamp failed. Empty format string.", MB_OK|MB_ICONSTOP);
}

void COEditorView::OnEditJoinLines ()
{
    RETURN_IF_LOCKED

    if (!IsSelectionEmpty())
    {
        CWaitCursor wait;

        Square blk;
        GetSelection(blk);
        blk.normalize();

        std::wstring text, buff;
        GetBlock(text);
        const wchar_t* textPtr = text.c_str();

        std::wostringstream out;
        DelimitersMap space(" \t\n\r");
        
        bool spaceNeeded = false;
        while (*textPtr)
        {
            while (*textPtr && space[*textPtr]) 
                ++textPtr;

            while (*textPtr && !space[*textPtr]) 
                buff += *(textPtr++);

            if (!buff.empty())
            {
                if (spaceNeeded)
                    out << ' ';
                else
                    spaceNeeded = true;

                out << buff;

                buff.clear();
            }
        }
        if (!blk.end.column)
            out << std::endl;

        UndoGroup undoGroup(*this);
        PushInUndoStack(GetPosition());
        DeleteBlock(true);
        InsertBlock(out.str().c_str(), false, true);
    }
}

/*
1) left and right margins
2) insert new line after: ,
3) ignore ^ between: ()
4*) do not split text between: ''""  
*/
#include "OESplitLinesDlg.h"

struct SplitLineContext
{
    DelimitersMap m_newLineAfterDelim;
    DelimitersMap m_spaceChar;
    FastmapW<bool> m_controlChar;
    int m_dontChangeBetween_Index;
    FastmapW<int> m_dontChangeBetween_OpenBraces;  // contains brace index for initialized and -1 for empty entries
    FastmapW<int> m_dontChangeBetween_ClosingBraces; // contains brace index for initialized and -1 for empty entries
    FastmapW<int> m_ignoreNewLineAfterBetween_OpenBraces;  // contains brace index for initialized and -1 for empty entries
    FastmapW<int> m_ignoreNewLineAfterBetween_ClosingBraces;  // contains brace index for initialized and -1 for empty entries
    std::map<int, int> m_ignoreNewLineAfterBetween_IndexMap;
    bool m_firstLine, m_skippingNewLineAfter;

    SplitLineContext (const OESplitLinesDlg& dlg)
        : m_spaceChar(" \t\n\r"),
        m_controlChar(false),
        m_dontChangeBetween_OpenBraces(-1),
        m_dontChangeBetween_ClosingBraces(-1),
        m_ignoreNewLineAfterBetween_OpenBraces(-1),
        m_ignoreNewLineAfterBetween_ClosingBraces(-1)
    {
        m_dontChangeBetween_Index = -1;
        m_firstLine = true;
        m_skippingNewLineAfter = false;

        if (dlg.m_bAdvancedOptions)
        {
            m_newLineAfterDelim.Set(Common::str(dlg.m_sInsertNewLineAfter).c_str());

            std::vector<std::pair<wchar_t,wchar_t> > dontChaneBeetwen;
            dlg.GetDontChaneBeetwen(dontChaneBeetwen);
            for (unsigned char i = 0; i < min<unsigned int>(UCHAR_MAX, dontChaneBeetwen.size()); ++i)
            {
                m_controlChar.assign(dontChaneBeetwen[i].first,  true);
                m_controlChar.assign(dontChaneBeetwen[i].second, true);

                m_dontChangeBetween_OpenBraces   .assign(dontChaneBeetwen[i].first,  i);
                m_dontChangeBetween_ClosingBraces.assign(dontChaneBeetwen[i].second, i);
            }

            std::vector<std::pair<wchar_t,wchar_t> > ignoreForceNewLine;
            dlg.GetIgnoreForceNewLine(ignoreForceNewLine);
            for (unsigned char i = 0; i < min<unsigned int>(UCHAR_MAX, ignoreForceNewLine.size()); ++i)
            {
                m_controlChar.assign(ignoreForceNewLine[i].first , true);
                m_controlChar.assign(ignoreForceNewLine[i].second, true);

                m_ignoreNewLineAfterBetween_OpenBraces   .assign(ignoreForceNewLine[i].first , i);
                m_ignoreNewLineAfterBetween_ClosingBraces.assign(ignoreForceNewLine[i].second, i);
            }
        }
    }

    bool IsLiteral () const { return (m_dontChangeBetween_Index != -1) ? true : false; }
    bool IsSkippingNewLineAfter () const { return m_skippingNewLineAfter; }

    void scanChar (wchar_t ch)
    {
        if (m_controlChar[ch])
        {
            if (m_dontChangeBetween_Index != -1)
            {
                if (m_dontChangeBetween_ClosingBraces[ch] == m_dontChangeBetween_Index)
                    m_dontChangeBetween_Index = -1;
            }
            else
            {
                if (m_dontChangeBetween_OpenBraces[ch] != -1)
                    m_dontChangeBetween_Index = m_dontChangeBetween_OpenBraces[ch];
            }

            if (m_dontChangeBetween_Index == -1)
            {
                int index = m_ignoreNewLineAfterBetween_ClosingBraces[ch];

                if (index != -1) 
                {
                    auto it = m_ignoreNewLineAfterBetween_IndexMap.find(index);
                    if (it != m_ignoreNewLineAfterBetween_IndexMap.end())
                    {
                        if (--it->second <= 0)
                        {
                            m_ignoreNewLineAfterBetween_IndexMap.erase(it);
                            if (m_ignoreNewLineAfterBetween_IndexMap.empty())
                                m_skippingNewLineAfter = false;
                        }
                    }
                }
                else
                {
                    index = m_ignoreNewLineAfterBetween_OpenBraces[ch];
                    if (index != -1)
                    {
                        auto it = m_ignoreNewLineAfterBetween_IndexMap.find(index);
                        if (it != m_ignoreNewLineAfterBetween_IndexMap.end())
                            ++it->second;
                        else
                            m_ignoreNewLineAfterBetween_IndexMap[index] = 1;

                        m_skippingNewLineAfter = true;
                    }
                }
            }
        }
    }

    bool GetWord (const wchar_t*& textPtr, std::wstring& buff, bool& newLineAfter)
    {
        buff.clear();
        newLineAfter = false;

        //skip spaces
        int newLines = 0;
        while (*textPtr && m_spaceChar[*textPtr]) 
        {
            if (*textPtr == '\n') ++newLines;
            ++textPtr;
        }

        if (newLines > 1 || (newLines == 1 && m_firstLine))
        {
            m_firstLine = false;
            buff.assign(newLines, '\n');
            return !buff.empty();
        }

        while (*textPtr && (IsLiteral() || !m_spaceChar[*textPtr]))
        {
            scanChar(*textPtr);

            buff += *textPtr;

            if (!IsLiteral() && !IsSkippingNewLineAfter() && m_newLineAfterDelim[*textPtr])
            {
                newLineAfter = true;
                ++textPtr;
                break;
            }

            ++textPtr;
        }

        m_firstLine = false;
        return !buff.empty();
    }
};

void COEditorView::OnEditSplitLine ()
{
    RETURN_IF_LOCKED

    if (!IsSelectionEmpty())
    {
        Square blk;
        GetSelection(blk);
        blk.normalize();

        OESplitLinesDlg dlg;
        
        if (dlg.DoModal() != IDOK) return;
        
        SplitLineContext splitLineContext(dlg);

        int leftMarging  = dlg.m_nLeftMargin;
        int rightMarging = dlg.m_nRightMargin;

        CWaitCursor wait;

        std::wstring text, buff;
        GetBlock(text);
        const wchar_t* textPtr = text.c_str();
        
        std::wostringstream out;

        std::wstring indent(leftMarging - 1, ' ');

        bool newLineAfter  = false;
        bool spaceNeeded   = false;
        bool newLineNeeded = false;
        int lineSize = blk.start.column; // count the selection

        while (splitLineContext.GetWord(textPtr, buff, newLineAfter))
        {
            if (!buff.empty() && buff.at(0) == '\n')
            {
                out << buff;
                lineSize = 0;
                newLineNeeded = false;
                continue;
            }

            if (newLineNeeded)
            {
                out << std::endl;
                lineSize = 0;
                newLineNeeded = false;
            }

            if (!lineSize)
            {
                out << indent;
                lineSize = indent.size();
                spaceNeeded = false;
            }

            if (lineSize + (int)buff.size() + 1 <= rightMarging)
            {
                if (spaceNeeded) 
                {
                    out << ' ';
                    lineSize++;
                }
            }
            else
            {
                if (lineSize && lineSize > (int)indent.size()) 
                {
                    out << std::endl << indent;
                    lineSize = indent.size();
                }
            }
            out << buff;
            lineSize += buff.size();

            newLineNeeded = newLineAfter;
            spaceNeeded = true;
        }

        if (lineSize && !blk.end.column && !splitLineContext.IsLiteral())
            out << std::endl;

        UndoGroup undoGroup(*this);
        PushInUndoStack(GetPosition());
        DeleteBlock(true);
        InsertBlock(out.str().c_str(), false, true);

        if (splitLineContext.IsLiteral())
            AfxMessageBox(L"Incomplete string literal appears in the selected text.\t", MB_OK|MB_ICONEXCLAMATION);
    }
}

void COEditorView::OnEditRemoveBlankLines ()
{
    RETURN_IF_LOCKED

    CWaitCursor wait;

    RemoveBlankLines(false/*excessiveOnly*/);
}

void COEditorView::OnRemoveExcessiveBlankLines ()
{
    RETURN_IF_LOCKED

    CWaitCursor wait;

    RemoveBlankLines(true/*excessiveOnly*/);
}

void COEditorView::OnTrimLeadingSpaces ()
{
    RETURN_IF_LOCKED

    CWaitCursor wait;
    ScanAndReplaceText(TrimLeadingSpaces, IsSelectionEmpty());
}

void COEditorView::OnTrimExcessiveSpaces ()
{
    RETURN_IF_LOCKED

    CWaitCursor wait;
    ScanAndReplaceText(TrimExcessiveSpaces, IsSelectionEmpty());
}

void COEditorView::OnTrimTrailingSpaces ()
{
    RETURN_IF_LOCKED

    CWaitCursor wait;
    ScanAndReplaceText(TrimTrailingSpaces, IsSelectionEmpty());
}

void COEditorView::OnEditColumnInsert ()
{
    RETURN_IF_LOCKED

    Common::CInputDlg dlg;
    dlg.m_title = L"Insert/Fill Columns";
    dlg.m_prompt = L"Value to insert into every selected line:";

    if (dlg.DoModal() == IDOK)
    {
        CWaitCursor wait;

        Square blk;
        GetSelection(blk);
        Position pos = GetPosition();

        UndoGroup undoGroup(*this);
        PushInUndoStack(GetPosition());
        PushInUndoStack(GetBlockMode(), IsAltColumnarMode(), blk);

        if (blk.start.column != blk.end.column) 
            DeleteBlock(true);

        InsertBlock(dlg.m_value.c_str());

        // restore 0-width selection
        if (blk.start.column == blk.end.column)
        {
            GetSelection(blk);
            blk.start.column = blk.end.column;
            SetSelection(blk);
            pos.column = blk.end.column;
            MoveTo(pos);
        }
    }
}

#include <sstream>
#include <iomanip>
void COEditorView::OnEditColumnInsertNumber ()
{
    RETURN_IF_LOCKED

    OEInsertNumberDlg dlg;

    Square blk;
    GetSelection(blk);
    Position pos = GetPosition();
    //bool isAltColumnarMode = IsAltColumnarMode();

    if (!blk.is_empty() && dlg.DoModal() == IDOK)
    {
        CWaitCursor wait;

        UndoGroup undoGroup(*this);
        PushInUndoStack(GetPosition());
        PushInUndoStack(GetBlockMode(), IsAltColumnarMode(), blk);

        blk.normalize();
        blk.normalize_columns();

        std::wostringstream out;  
        out << std::setbase(!dlg.m_nNumberFormat ? 10 : 16);

        int number = dlg.m_nFirstNumber;
        int width  = 0;
        int lines = GetLineCount();

        for (int line = 0; line <= blk.end.line - blk.start.line; ++line)
        {
            if (dlg.m_bSkipEmptyLines)
            {
                int len = 0;
                const char* str = 0;
                if (line < lines) GetLineA(line, str, len);
                if (!str || !len || Common::is_blank_line(str, len))
                    continue;
            }

            out << number;
            width = max<int>(width, out.str().size());
            number += dlg.m_nIncrement;
            out.str(std::wstring());
        }

        number = dlg.m_nFirstNumber;
        wchar_t filler = dlg.m_bLeadingZeros ? '0' : ' ';
        for (int line = 0; line <= blk.end.line - blk.start.line; ++line)
        {
            if (dlg.m_bSkipEmptyLines)
            {
                int len = 0;
                const char* str = 0;
                if (line < lines) GetLineA(line, str, len);
                if (!str || !len || Common::is_blank_line(str, len))
                {
                    out << std::endl;
                    continue;
                }
            }

            out << std::setfill(filler) << std::setw(width) << number << std::endl;
            number += dlg.m_nIncrement;
        }

        DeleteBlock(true);
        MoveTo(blk.start, true);
        InsertBlock(out.str().c_str(), false);

        // restore 0-width selection
        if (blk.start.column == blk.end.column)
        {
            GetSelection(blk);
            blk.start.column = blk.end.column;
            SetSelection(blk);
            pos.column = blk.end.column;
            MoveTo(pos);
        }
    }
}

void COEditorView::OnEditColumnLeftJustify () 
{
    RETURN_IF_LOCKED

    CWaitCursor wait;

    // Stage 6 B8/B9: justify untabifies and then rewrites every line of the block
    // through ScanAndReplaceText - neither is a virtual block primitive, so nothing
    // else on this path reveals for it. Done before orgBlkPos is captured so the
    // undo record below holds the geometry the edit actually runs on.
    RevealColumnarEditRows();

    Square orgBlkPos;
    GetSelection(orgBlkPos);
    if (GetBlockMode() == ebtColumn 
    && orgBlkPos.start.column != orgBlkPos.end.column)
    {
        UndoGroup undoGroup(*this);
        PushInUndoStack(GetPosition());
        PushInUndoStack(GetBlockMode(), IsAltColumnarMode(), orgBlkPos);

        UntabifyForColumnarOperation(orgBlkPos);
        ScanAndReplaceText(ColumnLeftJustify, false);
    }
}

void COEditorView::OnEditColumnCenterJustify ()
{
    RETURN_IF_LOCKED

    CWaitCursor wait;

    // Stage 6 B8/B9: as OnEditColumnLeftJustify - justify rewrites every line of the
    // block, so hidden rows inside it must be visible first.
    RevealColumnarEditRows();

    Square orgBlkPos;
    GetSelection(orgBlkPos);
    if (GetBlockMode() == ebtColumn 
    && orgBlkPos.start.column != orgBlkPos.end.column)
    {
        UndoGroup undoGroup(*this);
        PushInUndoStack(GetPosition());
        PushInUndoStack(GetBlockMode(), IsAltColumnarMode(), orgBlkPos);

        UntabifyForColumnarOperation(orgBlkPos);
        ScanAndReplaceText(ColumnCenterJustify, false);
    }
}

void COEditorView::OnEditColumnRightJustify ()
{
    RETURN_IF_LOCKED

    CWaitCursor wait;

    // Stage 6 B8/B9: as OnEditColumnLeftJustify - justify rewrites every line of the
    // block, so hidden rows inside it must be visible first.
    RevealColumnarEditRows();

    Square orgBlkPos;
    GetSelection(orgBlkPos);
    if (GetBlockMode() == ebtColumn 
    && orgBlkPos.start.column != orgBlkPos.end.column)
    {
        UndoGroup undoGroup(*this);
        PushInUndoStack(GetPosition());
        PushInUndoStack(GetBlockMode(), IsAltColumnarMode(), orgBlkPos);

        UntabifyForColumnarOperation(orgBlkPos);
        ScanAndReplaceText(ColumnRightJustify, false);
    }
}

void COEditorView::OnEditDupLineOrSelection ()
{
    RETURN_IF_LOCKED

    CWaitCursor wait;

    Square orgBlkPos;
    GetSelection(orgBlkPos);
    Position pos = GetPosition();

    if (orgBlkPos.is_empty())
    {
        if (pos.line < GetLineCount())
        {
            OEStringW lineBuff;
            GetLineW(pos.line, lineBuff);
            lineBuff.append('\n');
            lineBuff.append(0);

            UndoGroup undoGroup(*this);
            PushInUndoStack(pos);
            PushInUndoStack(GetBlockMode(), IsAltColumnarMode(), orgBlkPos);

            pos.column = 0;
            ++pos.line;
            MoveTo(pos, true);
            InsertBlock(lineBuff.data(), false, true);
        }
    } 
    else if (GetBlockMode() == ebtStream) 
    {
        UndoGroup undoGroup(*this);
        PushInUndoStack(pos);
        PushInUndoStack(GetBlockMode(), IsAltColumnarMode(), orgBlkPos);

        std::wstring buffer;
        GetBlock(buffer, &orgBlkPos);
        InsertBlock(buffer.c_str(), false, true);
        if (!(orgBlkPos.start < orgBlkPos.end))
        {
            SetSelection(orgBlkPos);
            MoveTo(orgBlkPos.end, true);
        }
    }
    else
    {
        MessageBeep((UINT)-1);
        Global::SetStatusText("Duplicate selection is not supported in Columnar mode.", TRUE);
    }
}

void COEditorView::OnUpdate_ColumnOperation (CCmdUI* pCmdUI)
{
    pCmdUI->Enable(!IsLocked() && !IsSelectionEmpty() && GetBlockMode() == ebtColumn);
}

void COEditorView::OnUpdate_NotInReadOnly (CCmdUI* pCmdUI)
{
    pCmdUI->Enable(!IsLocked());
}

//#include "OpenEditor/OEWorkspaceManager.h"

void COEditorView::OnTest ()
{
    //AfxGetMainWnd()->SendMessage(WM_QUERYENDSESSION, 0, ENDSESSION_LOGOFF);
    //AfxGetMainWnd()->SendMessage(WM_ENDSESSION, TRUE, ENDSESSION_LOGOFF);
    //OEWorkspaceManager::Get().InstantAutosave();
}

void COEditorView::OnIdleUpdateCmdUI ()
{
    // Step 3 idle relay: the modeless Find/Replace dialog is an owned popup,
    // invisible to the WM_IDLEUPDATECMDUI descendant broadcast this handler
    // rides on - hand the idle pass to its scope tracking. No-op unless the
    // dialog is open and visible; split siblings relaying twice is idempotent.
    CFindReplaceDlg::NotifyIdle();

    if (COEDocument* pDoc = GetDocument())
    {
        pDoc->UpdateTitle();

        // Pick up whatever the background parse has published. Idempotent and near-free - it returns
        // at once unless a parse completed since the last tick - and it is what builds the fold
        // regions while the outline pane is hidden, that pane gating the outline walk on its own
        // visibility. Both split siblings call it; the cached dual signal makes the second a no-op.
        //
        // "Background" is literal: the tree this reads is built on ParseQueue's worker and COMMITTED
        // on the UI thread. This tick neither waits for nor drives that parse - it only consumes
        // what an earlier tick already published - so a fold region appears one idle tick after the
        // batch that produced it. Callers that need it NOW block through WaitForParseQuiescent.
        pDoc->UpdateFoldsFromOutline();
    }
}

void COEditorView::OnFilePrintPreview () // FRM
{
    AFXPrintPreview (this);
}
