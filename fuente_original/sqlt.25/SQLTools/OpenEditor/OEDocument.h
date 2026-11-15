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

// 17.01.2005 (ak) simplified settings class hierarchy 

#pragma once

#ifndef __OEDocument_h__
#define __OEDocument_h__

#include "OpenEditor/OEStorage.h"
#include "OpenEditor/OESettings.h"
#include "OpenEditor/OEFolding.h"
#include <COMMON/MemoryMappedFile.h>
#include "FileWatch/FileWatch.h"
#include "OpenEditor/OEDocManager.h"


    class COEditorView;
    namespace Common { class CPropertySheetMem; }
    namespace OpenEditor { struct SectionHit; }
    class RecentFileList;
    class TiXmlElement;

class COEDocument : public CDocument, public CFileWatchClient
{
public:
    struct Encoding
    {
        int codepage, bom;
        Encoding () : codepage(0), bom(0) {}
        Encoding (int cp, int bm) : codepage(cp), bom(bm) {}
    };
private:
    class UndoFileEncoding : public OpenEditor::UndoBase
    {
    public:
        UndoFileEncoding (COEDocument& document, Encoding oldEncoding, Encoding newEncoding);
        virtual void Undo (OpenEditor::UndoContext&) const;
        virtual void Redo (OpenEditor::UndoContext&) const;
        virtual unsigned GetMemUsage() const;
    private:
        COEDocument& m_document;
        Encoding m_oldEncoding, m_newEncoding;
    };
    
    friend UndoFileEncoding;

    // for a modified sign '*' in a title bar
    CString m_orgTitle, m_extension;
    bool m_orgModified, m_extensionInitialized;
    __int64 m_orgFileTime, m_orgFileSize;
    Encoding  m_openEncoding, // set on initial load and on save, used in reload, can be changed if it's not utf16
              m_saveEncoding; // set on load, can be changed by convert
    static bool m_enableOpenUnexisting;

    // for backup processing only
    bool m_newOrSaveAs;
    CString m_lastBackupPath;
    void backupFile (LPCWSTR lpszPathName);

    // settings container
    OpenEditor::Settings m_settings;
    // the main object
    OpenEditor::Storage m_storage;

    ///////////////////////////////////////////////////////////////////////////
    // Code folding (md/CodeFolding.md, stage 3)
    //
    // The model lives with the DOCUMENT (decision D15), not with a view: a
    // split window gives one document two COEditorView siblings linked to the
    // same Storage, and they have to agree on which lines are hidden. Both
    // reach the one LineMapper through GetDocument(). Everything here is inert
    // while IsFoldingEnabled() is false, which is the default - no outline
    // snapshot is pulled and the mapper stays an identity function.
    //
    // Declared after m_storage on purpose: members destruct in reverse
    // declaration order, so the resolver, which holds a reference to the
    // storage, dies first. m_lineMapper follows m_foldModel for the same
    // reason - it points at the model.
    class FoldLineIdResolver : public OpenEditor::ILineIdResolver
    {
    public:
        FoldLineIdResolver (const OpenEditor::Storage& storage) : m_storage(storage) {}

        virtual int GetLineCount () const { return m_storage.GetLineCount(); }

        virtual OpenEditor::LineId GetLineId (int line) const
            { return m_storage.GetLineId(line); }

        virtual bool FindById (const OpenEditor::LineId& id, int& line) const
            { return m_storage.FindById(id, line); }

    private:
        const OpenEditor::Storage& m_storage;
    };

    FoldLineIdResolver     m_foldResolver;
    OpenEditor::FoldModel  m_foldModel;
    OpenEditor::LineMapper m_lineMapper;

    // Triple signal of the last parse reflected in m_foldModel, mirroring
    // CodeOutlineWnd::OnIdleUpdateCmdUI: the storage id snapshotted when the
    // parse STARTED, the parser instance, which changes when the language
    // support is recreated (document type or file format change) and takes the
    // action id back to its initial value with it, and the parse generation,
    // which changes when the SAME object reparses the SAME text - the settings
    // change that moves a parse limit. The first two cannot see that case, so
    // without the third a limit change would finish reparsing and leave the fold
    // model showing the previous generation's regions for the rest of the
    // session. -1 is the "nothing reflected yet" sentinel; generations count
    // from 1, so the two can never be confused.
    OpenEditor::SequenceId m_foldParsedActionId;
    int                    m_foldParserInstanceId;
    int                    m_foldParseGeneration;

    // memory mapped file support
    Common::MemoryMappedFile m_mmfile;
    // allocated memory when file locking is disabled
    LPVOID m_vmdata;
    unsigned long m_vmsize;
    void loadFile (LPCWSTR path, bool reload = false, bool external = false, bool modified = false);

    static OpenEditor::SettingsManager* m_pSettingsManager;
    static OpenEditor::Searcher m_searcher;
    static OpenEditor::SettingsManager& getSettingsManager ();

    virtual void OnWatchedFileChanged ();

protected:
    virtual void GetNewPathName (CString& newName) const { newName = m_strPathName; }

    void SetText (LPVOID, unsigned long);
    LPCVOID GetVMData () const;

    COEDocument();
    virtual ~COEDocument();
    DECLARE_DYNCREATE(COEDocument)

public:
    void SetText (const char*, unsigned long);

    const CString& GetOrgTitle () const { return m_orgTitle; }

    static bool GetEnableOpenUnexisting () { return m_enableOpenUnexisting; }
    static void SetEnableOpenUnexisting (bool enable) { m_enableOpenUnexisting = enable; }
    struct EnableOpenUnexisting
    {
        EnableOpenUnexisting ()  { SetEnableOpenUnexisting(true); }
        ~EnableOpenUnexisting () { SetEnableOpenUnexisting(false); }
    };
    
    struct SettingsDialogCallback
    {
        virtual void BeforePageAdding (Common::CPropertySheetMem&) = 0;
        virtual void AfterPageAdding  (Common::CPropertySheetMem&) = 0;
        virtual void OnOk     () = 0;
        virtual void OnCancel () = 0;
    };

    static void LoadSettingsManager ();
    static void ReloadTemplates ();
    static void SaveSettingsManager ();
    static bool ShowSettingsDialog (SettingsDialogCallback* = 0);
    static void ShowTemplateDialog (OpenEditor::TemplatePtr, int index, const std::wstring& = std::wstring());
    static void ShowFormatSqlSettings (COEditorView*);
    static const OpenEditor::SettingsManager& GetSettingsManager ();
    static OpenEditor::SettingsManager& GetSettingsManagerForUpdate ();
    static void DestroySettingsManager ();

    OpenEditor::Storage& GetStorage ()               { return m_storage; }
    const OpenEditor::Settings& GetSettings () const { return m_settings; }
    OpenEditor::Settings& GetSettings ()             { return m_settings; }

    // Code folding - the members are declared in the private block above.
    OpenEditor::FoldModel&       GetFoldModel ()       { return m_foldModel; }
    const OpenEditor::FoldModel& GetFoldModel () const { return m_foldModel; }
    OpenEditor::LineMapper&      GetLineMapper ()      { return m_lineMapper; }
    // Const overload, mirroring GetFoldModel above. The view's caret and
    // hit-test paths are const members and only ever query the mapper; every
    // LineMapper query is const with a mutable cache, so handing out a const
    // reference here cannot be used to change fold state.
    const OpenEditor::LineMapper& GetLineMapper () const { return m_lineMapper; }

    // Stage 4 (md/CodeFolding.md): how many ROWS the vertical scroller and the
    // paint loop work in - the line count minus every hidden line. The mapper's
    // line count is refreshed first because the view asks on every paint while
    // UpdateFoldsFromOutline only runs from idle; storage stays the truth and
    // the mapper merely subtracts the hidden lines from it. While folding is off
    // the model is empty, so this is exactly m_storage.GetLineCount().
    int GetVisibleLineCount ();

    // The three settings gates. PlSqlKeywordNavigation drives the parser that
    // produces the outline, PlSqlOutlineEnabled the snapshot itself, and
    // PlSqlOutlineFoldingEnabled is the feature; with any one off there are no
    // regions to fold and the mapper must stay an identity function.
    bool IsFoldingEnabled () const;

    // Polls the background parser and rebuilds the regions when a parse has
    // just completed. Idempotent and near-free when nothing changed, so each
    // view's idle path can call it without any coordination.
    void UpdateFoldsFromOutline ();

    // Tells every attached view that the model changed under it, so each can
    // re-run its scrollbar extent and repaint its gutter. Without it the
    // regions exist but nothing dirties the gutter strip, and the glyphs only
    // appear once some unrelated event repaints (document open / paste).
    void NotifyFoldModelChanged ();

    // Storage line-count change, forwarded from
    // COEditorView::OnChangedLinesQuantity. pSender picks exactly one of the
    // split siblings - see the implementation.
    void OnFoldLinesQuantityChanged (CView* pSender, int line, int quantity);

    void SetClassSetting (const char* name)          { if (name) getSettingsManager().SetClassSettingsByLang(name, m_settings); }
    void DefaultFileFormat ()                        { m_storage.SetFileFormat((OpenEditor::EFileFormat)GetSettings().GetFileCreateAs(), false); }

    bool IsContentLocked  () const;
    virtual void LockContent (bool lock);

    bool IsNewDocument () const                     { return m_strPathName.IsEmpty() ? true : false; }

    virtual void OnContextMenuInit (CMenu*)         {}

    // used in by OEWorkspaceManager
    void GetOrgFileTimeAndSize (__int64& fileTime, __int64& fileSize) const { fileTime = m_orgFileTime; fileSize = m_orgFileSize; }
    void SetOrgFileTimeAndSize (__int64 fileTime, __int64 fileSize)         { m_orgFileTime = fileTime; m_orgFileSize = fileSize; }

public:
    virtual BOOL IsModified();
    RecentFileList* GetRecentFileList();
    virtual BOOL OnNewDocument();
    virtual void SetTitle(LPCWSTR lpszTitle);
    virtual BOOL OnSaveDocument(LPCWSTR lpszPathName);
    virtual void SetPathName(LPCWSTR lpszPathName, BOOL bAddToMRU = TRUE);
    virtual BOOL OnOpenDocument(LPCWSTR lpszPathName);
    virtual BOOL SaveModified (); 
    void SetModifiedFlag (BOOL bModified = TRUE);
    void ClearUndo ();
    virtual BOOL DoFileSave();
    virtual BOOL DoSave(LPCWSTR lpszPathName, BOOL bReplace = TRUE);
    virtual void UpdateTitle ();
    virtual BOOL OnIdle(LONG);

    // Block until the background PL/SQL parse has published everything it is going to,
    // or until `deadline` (a GetTickCount64 value, NOT a duration) passes. Returns false
    // on timeout, and every caller degrades rather than failing: FindMatch falls through
    // to the text scan, `.foldsettle` reports loudly because a nondeterministic
    // transcript is the one thing a test harness may not produce.
    //
    // Two other exits, both documented here because they change what "false" means to a
    // caller. It returns TRUE at once when PL/SQL keyword navigation is switched off,
    // re-tested every turn rather than only on entry: COEDocument::OnIdle is gated on the
    // same setting, so with it off nothing can ever publish and quiescent is the honest
    // answer - and the completion event is manual reset, so treating that state as
    // "keep waiting" would be a hot spin to the deadline. It returns FALSE when the pump
    // declines to run while a parse is still pending, which is a dead state by the same
    // argument and is failed closed rather than spun on.
    //
    // It lives here and nowhere else, for two reasons that between them rule out every
    // other home. It has to block on the worker's completion event, and it has to drive
    // COEDocument::OnIdle so the single-sited publication runs - and only the document
    // owns both. Storage does not own the document so it cannot pump it; Threading is
    // DB-shaped and must not learn what a COEDocument is; and PlSqlSupport cannot reach
    // this idle path at all, because an ON_COMMAND handler runs INSIDE DispatchMessage,
    // so a nested message pump never returns to CWinThread::Run's idle point - the only
    // place MFC calls OnIdle. Driving OnIdle(0) directly is what sidesteps that.
    //
    // The parameter is a deadline rather than a timeout on purpose. One FindMatch command
    // reaches PlSqlSupport::FindMatch up to three times and each parse is N batches; a
    // per-call timeout would compound across all of them, and a caller that wants "as
    // long as it takes" passes a deadline far enough out that the distinction is moot.
    bool WaitForParseQuiescent (ULONGLONG deadline);

    // The fact WaitForParseQuiescent loops on, exposed for callers that want to ask
    // without blocking - `.foldsettle` uses it to decide whether another tick is worth
    // taking. Forwarded rather than re-derived: LanguageSupport's base default already
    // answers TRUE for every language with no background parse, so a caller cannot
    // mistake "not PL/SQL" for "still parsing" and spin.
    //
    // It reaches the support object through Storage's language-support ACCESSOR, not
    // through a Storage forwarder. Storage is a text buffer: it owns the
    // LanguageSupportPtr only because it was the one part common to the EditContext
    // family, and owning the object is not a reason to grow the parse protocol on it.
    // The document is the sole consumer of both halves of that protocol, which is the
    // same argument that already put WaitForParseQuiescent here.
    bool IsParseDone () const;

    // Which section of the document (line, offset) sits in, for a selection format.
    // False for a non-PL/SQL document or an unanswerable parse: format as a document.
    bool GetFormatSeed (int line, int offset, OpenEditor::SectionHit& hit) const;
    virtual void OnTimer (UINT /*nIDEvent*/) {};
    virtual void OnCloseDocument ();
    void SaveEncoding (TiXmlElement&) const;
    void RestoreEncoding (const TiXmlElement&);
    COEditorView* GetFirstEditorView ();

protected:
    BOOL doSave  (LPCWSTR lpszPathName, BOOL bReplace, BOOL bMove);
    BOOL doSave2 (LPCWSTR lpszPathName, BOOL bReplace = TRUE, BOOL bMove = FALSE);
    //{{AFX_MSG(COEDocument)
    afx_msg void OnEditPrintPageSetup();
    afx_msg void OnEditFileSettings();
    //}}AFX_MSG
    DECLARE_MESSAGE_MAP()
public:
    afx_msg void OnFileReload();
    afx_msg void OnUpdate_FileReload(CCmdUI *pCmdUI);
    afx_msg void OnUpdate_FileLocation(CCmdUI *pCmdUI);
    afx_msg void OnUpdate_EncodingInd(CCmdUI* pCmdUI);
    afx_msg void OnUpdate_FileTypeInd(CCmdUI* pCmdUI);
    afx_msg void OnUpdate_FileLinesInd(CCmdUI* pCmdUI);

    afx_msg void OnEditViewLineNumbers ();
    afx_msg void OnUpdate_EditViewLineNumbers (CCmdUI *pCmdUI);
    afx_msg void OnEditViewSyntaxGutter ();
    afx_msg void OnUpdate_EditViewSyntaxGutter (CCmdUI *pCmdUI);
    afx_msg void OnEditViewWhiteSpace ();
    afx_msg void OnUpdate_EditViewWhiteSpace (CCmdUI* pCmdUI);
    afx_msg void OnEditViewRuler ();
    afx_msg void OnUpdate_EditViewRuler (CCmdUI* pCmdUI);
    afx_msg void OnEditViewColumnMarkers ();
    afx_msg void OnUpdate_EditViewColumnMarkers (CCmdUI* pCmdUI);
    afx_msg void OnEditViewIndentGuide ();
    afx_msg void OnUpdate_EditViewIndentGuide (CCmdUI* pCmdUI);

    afx_msg void OnFileOpen ();

    afx_msg void OnFileCloseOthers ();
    afx_msg void OnUpdate_FileCloseOthers (CCmdUI* pCmdUI);
    afx_msg void OnFileRename ();
    afx_msg void OnFileCopyLocation();
    afx_msg void OnUpdate_FileCopyLocation (CCmdUI* pCmdUI);
    afx_msg void OnFileCopyName ();

    afx_msg void OneditFileFormatWinows ();
    afx_msg void OneditFileFormatUnix   ();
    afx_msg void OneditFileFormatMac    ();
    afx_msg void OnUpdate_FileFormat (CCmdUI* pCmdUI);

    int  GetSaveEncodingCodepage () const { return m_saveEncoding.codepage; }
    int  GetEncodingCodepage () const;
    void SetEncodingCodepage (int);
    afx_msg void OnEditEncodingAnsi ();
    afx_msg void OnEditEncodingOem ();
    afx_msg void OnEditEncodingUtf8 ();
    afx_msg void OnUpdate_Encoding (CCmdUI* pCmdUI);
    afx_msg void OnUpdate_EncodingCodepage (CCmdUI* pCmdUI);
    afx_msg BOOL OnEncodingCodepage (UINT nID);

    void SetFileEncodingCodepage (int, int);
    afx_msg void OnFileEncodingAnsi      ();
    afx_msg void OnFileEncodingOem       ();
    afx_msg void OnFileEncodingUtf8      ();
    afx_msg void OnFileEncodingUtf8wBom  ();
    afx_msg void OnFileEncodingUtf16wBom ();
    afx_msg void OnUpdate_FileEncoding  (CCmdUI* pCmdUI);
    afx_msg void OnUpdate_FileEncodingCodepage (CCmdUI* pCmdUI);
    afx_msg BOOL OnFileEncodingCodepage (UINT nID);
};

inline
    LPCVOID COEDocument::GetVMData () const { return m_vmdata; }

inline
    OpenEditor::SettingsManager& COEDocument::getSettingsManager ()
    {
        if (!m_pSettingsManager) 
            m_pSettingsManager = new OpenEditor::SettingsManager;
        return *m_pSettingsManager;
    }

inline
    void COEDocument::DestroySettingsManager ()
    {
        if (m_pSettingsManager) 
        {
            delete m_pSettingsManager;
            m_pSettingsManager = 0;
        }
    }

inline
    const OpenEditor::SettingsManager& 
        COEDocument::GetSettingsManager () { return getSettingsManager(); }

inline
    OpenEditor::SettingsManager& 
        COEDocument::GetSettingsManagerForUpdate () { return getSettingsManager(); }

inline
    bool COEDocument::IsContentLocked  () const
    {
        return m_storage.IsLocked();
    }

inline
    void COEDocument::LockContent (bool lock)
    {
        m_storage.Lock(lock);
    }



//{{AFX_INSERT_LOCATION}}

#endif//__OEDocument_h__
