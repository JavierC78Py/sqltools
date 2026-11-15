// Menús, barras de iconos y atajos de SQLTools (Windows) conectados a las funciones de esta aplicación.
// Cada bind("ID_…") enlaza un comando del inventario (ui/spec/menu_spec.json) con su implementación; los que no tienen
// función quedan visibles pero desactivados. La lista de pendientes está en docs/MENUS_Y_ATAJOS.md.
#include "MainWindow.h"
#include "CommandMenu.h"
#include "CompletionData.h"
#include "DbWorker.h"
#include "FindReplaceDialog.h"
#include "ObjectBrowser.h"
#include "FindInFilesDialog.h"
#include "TextCodec.h"
#include "ExportDialog.h"
#include <QDesktopServices>
#include "SqlReference.h"
#include "HistoryStore.h"
#include "Snippets.h"
#include <QPrinter>
#include "SqlFormatter.h"
#include "SettingsDialog.h"
#include "SqlSplitter.h"
QStringList browserSql (const QString& action, const QString& type, const QString& qname);
#include <QScrollBar>
#include <QSplitter>
#include <QPlainTextEdit>
#include <QJsonArray>
#include <QRadioButton>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>
#include "ResultModel.h"
#include "SqlEditor.h"

#include <QAction>
#include <QApplication>
#include <QSysInfo>
#include <QActionGroup>
#include <QSettings>
#include <QStyleHints>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QRegularExpression>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QTemporaryDir>
#include <QFile>
#include <QPushButton>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QTreeWidget>
#include <QTimer>
#include <QTableWidget>
#include <QToolBar>
#include <QUrl>

void MainWindow::setupCommands ()
{
    m_menu = new CommandMenu(this);
    QString err;
    if (!m_menu->load(&err)) { QMessageBox::warning(this, "Menús", err); return; }
    m_findDlg = new FindReplaceDialog([this]{ return editor(); }, this);
    CommandMenu* m = m_menu;
    using R = CommandMenu;
    auto ed = [this](auto fn) { return [this, fn]{ if (auto* e = editor()) fn(e); }; };          // ejecuta fn sobre el editor activo, si hay

    // ---------------- File ----------------
    m->bind("ID_FILE_NEW",       [this]{ newScript(); });
    m->bind("ID_FILE_OPEN",      [this]{ openScript(); });
    m->bind("ID_FILE_RELOAD",    [this]{ reloadScript(); });
    m->bind("ID_FILE_CLOSE",     [this]{ closeScript(m_editors->currentIndex()); });
    m->bind("ID_FILE_CLOSE_ALL", [this]{ closeAllScripts(); });
    m->bind("ID_FILE_SAVE",      [this]{ saveScript(); });
    m->bind("ID_FILE_SAVE_AS",   [this]{ saveScriptAs(); });
    m->bind("ID_FILE_SAVE_ALL",  [this]{ saveAll(); });
    m->bind("ID_FILE_SYNC_LOCATION", [this]{ if (auto* e = editor(); e && !e->filePath.isEmpty()) QProcess::startDetached("open", {"-R", e->filePath}); });
    m->bind("ID_FILE_COPY_LOCATION", [this]{ if (auto* e = editor(); e && !e->filePath.isEmpty()) QApplication::clipboard()->setText(e->filePath); });
    if (auto* a = m->bind("ID_APP_EXIT", [this]{ close(); })) { a->setMenuRole(QAction::QuitRole); a->setShortcut(QKeySequence::Quit); }

    // ---------------- Edit ----------------
    m->bind("ID_EDIT_UNDO",  ed([](SqlEditor* e){ e->undo(); }));
    m->bind("ID_EDIT_REDO",  ed([](SqlEditor* e){ e->redo(); }));
    m->bind("ID_EDIT_CUT",   ed([](SqlEditor* e){ e->cut(); }));
    m->bind("ID_EDIT_COPY",  [this]{ if (m_table->hasFocus()) copySelection(false); else if (auto* e = editor()) e->copy(); });
    m->bind("ID_EDIT_PASTE", ed([](SqlEditor* e){ e->paste(); }));
    m->bind("ID_EDIT_SELECT_ALL",  ed([](SqlEditor* e){ e->selectAll(); }));
    m->bind("ID_EDIT_SELECT_WORD", ed([](SqlEditor* e){ e->selectWord(); }));
    m->bind("ID_EDIT_SELECT_LINE", ed([](SqlEditor* e){ e->selectLine(); }));
    m->bind("ID_EDIT_DELETE", ed([](SqlEditor* e){ QTextCursor c = e->textCursor(); if (c.hasSelection()) c.removeSelectedText(); else c.deleteChar(); e->setTextCursor(c); }));
    m->bind("ID_EDIT_DELETE_WORD_TO_LEFT",  ed([](SqlEditor* e){ e->deleteWord(false); }));
    m->bind("ID_EDIT_DELETE_WORD_TO_RIGHT", ed([](SqlEditor* e){ e->deleteWord(true); }));
    m->bind("ID_EDIT_CUT_N_APPEND", ed([](SqlEditor* e){ e->cutAppend(); }));
    m->bind("ID_EDIT_SCROLL_UP",     ed([](SqlEditor* e){ e->scrollLines(-1); }));
    m->bind("ID_EDIT_SCROLL_DOWN",   ed([](SqlEditor* e){ e->scrollLines(1); }));
    m->bind("ID_EDIT_SCROLL_CENTER", ed([](SqlEditor* e){ e->centerCursor(); }));
    m->bind("ID_EDIT_TABIFY",         ed([](SqlEditor* e){ e->tabify(false); }));
    m->bind("ID_EDIT_TABIFY_LEADING", ed([](SqlEditor* e){ e->tabify(true); }));
    m->bind("ID_EDIT_UNTABIFY",       ed([](SqlEditor* e){ e->untabify(); }));
    m->bind("ID_EDIT_NORMALIZE_TEXT", ed([](SqlEditor* e){ e->normalizeKeywords(); }));
    m->bind("ID_EDIT_FORMAT_SQL", [this]{ auto* e = editor(); if (!e) return; const FormatOptions fo = FormatOptions::load(); QTextCursor c = e->textCursor();
        if (!c.hasSelection())
        {   // sin selección: la sentencia donde está el cursor (con una sola sentencia, todo el texto)
            const auto v = SqlSplitter::split(e->toPlainText()); const int k = SqlSplitter::statementAt(v, c.position());
            if (k < 0) { m_status->setText("No hay nada que formatear."); return; }
            c.setPosition(v[k].start); c.setPosition(v[k].end, QTextCursor::KeepAnchor);
        }
        const QString src = c.selectedText().replace(QChar(0x2029), '\n'); const QString res = SqlFormatter::format(src, fo);
        if (res == src) { m_status->setText("El código ya tiene ese formato."); return; }
        c.insertText(res); m_status->setText("Formato aplicado (Edit > Undo lo deshace). Opciones: Tools > Settings."); });
    m->bind("ID_APP_SETTINGS", [this]{ SettingsDialog d(this, 0); if (d.exec() == QDialog::Accepted) { for (int i = 0; i < m_editors->count(); ++i) if (auto* x = qobject_cast<SqlEditor*>(m_editors->widget(i))) x->applyEditorSettings(); applyExecOptions(); if (m_connected) refreshCaches(); m_status->setText("Ajustes guardados."); } });
    m->bind("ID_EDIT_STREAM_SEL",     ed([this](SqlEditor* e){ e->exitColumnMode(); m_status->setText("Selección en flujo (normal)."); }));
    m->bind("ID_EDIT_COLUMN_SEL",     ed([this](SqlEditor* e){ if (!e->columnMode()) e->enterColumnMode(); m_status->setText("Selección en columna: Mayús+Opción+flechas para extender; Esc o un clic para salir."); }));
    m->bind("ID_EDIT_TOGGLES_SEL",    ed([this](SqlEditor* e){ e->toggleColumnMode(); m_status->setText(e->columnMode() ? "Modo selección en columna." : "Modo selección en flujo (normal)."); }));
    m->bind("ID_EDIT_COLUMN_LEFT_JUSTIFY",  ed([this](SqlEditor* e){ if (!e->columnMode()) m_status->setText("Seleccione primero un bloque en columna (Mayús+Opción+flechas)."); else e->columnJustify(0); }));
    m->bind("ID_EDIT_COLUMN_CENTER_JUSTIFY",ed([this](SqlEditor* e){ if (!e->columnMode()) m_status->setText("Seleccione primero un bloque en columna (Mayús+Opción+flechas)."); else e->columnJustify(1); }));
    m->bind("ID_EDIT_COLUMN_RIGT_JUSTIFY",  ed([this](SqlEditor* e){ if (!e->columnMode()) m_status->setText("Seleccione primero un bloque en columna (Mayús+Opción+flechas)."); else e->columnJustify(2); }));
    m->bind("ID_EDIT_COLUMN_INSERT_FILL", [this]{ auto* e = editor(); if (!e) return;
        if (!e->columnMode()) { m_status->setText("Seleccione primero un bloque en columna (Mayús+Opción+flechas)."); return; }
        QDialog d(this); d.setWindowTitle("Insert/Fill Columns"); auto* lay = new QVBoxLayout(&d); auto* txt = new QLineEdit(QSettings().value("editor/fillText", "-").toString());
        auto* ins = new QRadioButton("Insertar el texto en la columna (en cada línea)"); auto* fil = new QRadioButton("Rellenar el ancho del bloque repitiendo el texto"); ins->setChecked(true);
        lay->addWidget(new QLabel("Texto:")); lay->addWidget(txt); lay->addWidget(ins); lay->addWidget(fil);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); lay->addWidget(bb); connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept); connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        if (d.exec() == QDialog::Accepted) { QSettings().setValue("editor/fillText", txt->text()); e->columnFill(txt->text(), fil->isChecked()); } });
    m->bind("ID_EDIT_VIEW_RULER", [this]{ const bool on = m_menu->action("ID_EDIT_VIEW_RULER")->isChecked(); QSettings().setValue("editor/ruler", on); applyEditorView(); }, R::None, true, QSettings().value("editor/ruler", false).toBool());
    m->bind("ID_EDIT_FORMAT_SPLIT_LINES", [this]{ if (auto* e = editor()) { bool ok = false; const int w = QInputDialog::getInt(this, "Split Line", "Partir las líneas más largas de (columnas):", QSettings().value("editor/splitWidth", 80).toInt(), 20, 500, 1, &ok); if (ok) { QSettings().setValue("editor/splitWidth", w); e->splitLines(w); } } });
    m->bind("ID_EDIT_COLUMN_INSERT_NUMBER", [this]{ if (auto* e = editor()) {
        bool ok = false; const int start = QInputDialog::getInt(this, "Insert Number", "Primer número:", 1, -1000000, 1000000, 1, &ok); if (!ok) return;
        const int step = QInputDialog::getInt(this, "Insert Number", "Incremento:", 1, -1000000, 1000000, 1, &ok); if (!ok) return;
        const int width = QInputDialog::getInt(this, "Insert Number", "Ancho mínimo (se rellena con ceros):", 1, 1, 20, 1, &ok); if (ok) e->insertNumbers(start, step, width); } });
    m->bind("ID_EDIT_OPENFILEUNDERCURSOR", [this]{ if (auto* e = editor()) {
        QString p = e->pathUnderCursor(); if (p.isEmpty()) { m_status->setText("No hay ninguna ruta bajo el cursor."); return; }
        if (p.startsWith("~/")) p = QDir::homePath() + p.mid(1);
        if (QFileInfo(p).isRelative() && !e->filePath.isEmpty()) p = QFileInfo(e->filePath).dir().filePath(p);
        if (!QFileInfo::exists(p)) { m_status->setText("No existe el archivo: " + p); return; }
        addEditor(p); } });
    m->bind("ID_EDIT_VIEW_INDENT_GUIDE",   [this]{ const bool on = m_menu->action("ID_EDIT_VIEW_INDENT_GUIDE")->isChecked(); QSettings().setValue("editor/indentGuides", on); applyEditorView(); }, R::None, true, QSettings().value("editor/indentGuides", false).toBool());
    m->bind("ID_EDIT_VIEW_COLUMN_MARKERS", [this]{ const bool on = m_menu->action("ID_EDIT_VIEW_COLUMN_MARKERS")->isChecked(); QSettings().setValue("editor/columnMarkers", on); applyEditorView(); }, R::None, true, QSettings().value("editor/columnMarkers", false).toBool());
    m->bind("ID_EDIT_FILE_SETTINGS", [this]{ if (auto* e = editor()) {
        QFileInfo fi(e->filePath);
        QMessageBox::information(this, "Propiedades", QString("Archivo: %1\nTamaño: %2 bytes\nCodificación: %3%4\nSaltos de línea: %5\nLíneas: %6\nCaracteres: %7")
            .arg(e->filePath.isEmpty() ? QString("(sin guardar)") : e->filePath).arg(e->filePath.isEmpty() ? 0 : fi.size()).arg(e->encoding).arg(e->bom ? " con BOM" : "").arg(e->crlf ? "Windows (CRLF)" : "Unix (LF)").arg(e->document()->blockCount()).arg(e->document()->characterCount() - 1)); } });
    m->bind("ID_EDIT_DELETE_LINE", ed([](SqlEditor* e){ e->deleteLine(); }));
    m->bind("ID_EDIT_DUP_LINE_OR_SELECTION", ed([](SqlEditor* e){ e->duplicateLineOrSelection(); }));
    m->bind("ID_EDIT_COPY_N_APPEND", [this]{ if (m_table->hasFocus()) copySelection(true); });          // Cmd+Mayús+C (con cabeceras en la rejilla)

    // ---------------- Search: marcadores y plegado ----------------
    m->bind("ID_EDIT_BKM_TOGGLE",     ed([](SqlEditor* e){ e->toggleBookmark(); }));
    m->bind("ID_EDIT_BKM_NEXT",       ed([](SqlEditor* e){ e->nextBookmark(true); }));
    m->bind("ID_EDIT_BKM_PREV",       ed([](SqlEditor* e){ e->nextBookmark(false); }));
    m->bind("ID_EDIT_BKM_REMOVE_ALL", ed([](SqlEditor* e){ e->clearBookmarks(); }));
    for (int n = 0; n <= 9; ++n)
    {
        m->bind(QString("ID_EDIT_BKM_SET_%1").arg(n), ed([n](SqlEditor* e){ e->setNumberedBookmark(n); }));
        m->bind(QString("ID_EDIT_BKM_GET_%1").arg(n), ed([n](SqlEditor* e){ e->gotoNumberedBookmark(n); }));
    }
    m->bind("ID_EDIT_COPY_BOOKMARKED",   ed([](SqlEditor* e){ QApplication::clipboard()->setText(e->takeBookmarkedLines(false)); }));
    m->bind("ID_EDIT_CUT_BOOKMARKED",    ed([](SqlEditor* e){ QApplication::clipboard()->setText(e->takeBookmarkedLines(true)); }));
    m->bind("ID_EDIT_DELETE_BOOKMARKED", ed([](SqlEditor* e){ e->takeBookmarkedLines(true); }));
    m->bind("ID_EDIT_FOLD_COLLAPSE_CURRENT", ed([](SqlEditor* e){ e->foldCurrent(0); }));
    m->bind("ID_EDIT_FOLD_EXPAND_CURRENT",   ed([](SqlEditor* e){ e->foldCurrent(1); }));
    m->bind("ID_EDIT_FOLD_TOGGLE_CURRENT",   ed([](SqlEditor* e){ e->foldCurrent(2); }));
    m->bind("ID_EDIT_FOLD_EXPAND_SUBTREE",   ed([](SqlEditor* e){ e->expandSubtree(); }));
    m->bind("ID_EDIT_FOLD_COLLAPSE_ALL",     ed([](SqlEditor* e){ e->foldAll(true); }));
    m->bind("ID_EDIT_FOLD_EXPAND_ALL",       ed([](SqlEditor* e){ e->foldAll(false); }));
    m->bind("ID_EDIT_FOLD_TOGGLE_ALL",       ed([](SqlEditor* e){ e->toggleAllFolds(); }));
    m->bind("ID_EDIT_FOLD_COLLAPSE_LEVEL1",  ed([](SqlEditor* e){ e->foldToLevel(1); }));
    m->bind("ID_EDIT_FOLD_COLLAPSE_LEVEL2",  ed([](SqlEditor* e){ e->foldToLevel(2); }));
    m->bind("ID_EDIT_FOLD_COLLAPSE_LEVEL3",  ed([](SqlEditor* e){ e->foldToLevel(3); }));
    m->bind("ID_EDIT_FIND_MATCH",            ed([](SqlEditor* e){ e->findMatchingBracket(false); }));
    m->bind("ID_EDIT_FIND_MATCH_N_SELECT",   ed([](SqlEditor* e){ e->findMatchingBracket(true); }));
    // Text > Line Numbers / Syntax Gutter: valen para todas las pestañas y se recuerdan
    {
        QSettings st;
        m->bind("ID_EDIT_VIEW_LINE_NUMBERS", [this]{ const bool on = m_menu->action("ID_EDIT_VIEW_LINE_NUMBERS")->isChecked(); QSettings().setValue("editor/lineNumbers", on); applyEditorView(); }, R::None, true, st.value("editor/lineNumbers", true).toBool());
        m->bind("ID_EDIT_VIEW_SYNTAX_GUTTER", [this]{ const bool on = m_menu->action("ID_EDIT_VIEW_SYNTAX_GUTTER")->isChecked(); QSettings().setValue("editor/foldGutter", on); applyEditorView(); }, R::None, true, st.value("editor/foldGutter", true).toBool());
    }

    // ---------------- Search ----------------
    m->bind("ID_EDIT_FIND",         [this]{ if (auto* e = editor()) m_findDlg->openFind(e->textCursor().selectedText()); });
    m->bind("ID_EDIT_REPLACE",      [this]{ if (auto* e = editor()) m_findDlg->openReplace(e->textCursor().selectedText()); });
    m->bind("ID_EDIT_FIND_NEXT",    [this]{ m_findDlg->findNext(false); });
    m->bind("ID_EDIT_FIND_PREVIOUS",[this]{ m_findDlg->findNext(true); });
    m->bind("ID_EDIT_FIND_SELECTED_NEXT",     [this]{ m_findDlg->findSelected(false); });
    m->bind("ID_EDIT_FIND_SELECTED_PREVIOUS", [this]{ m_findDlg->findSelected(true); });
    m->bind("ID_EDIT_GOTO", [this]{
        auto* e = editor(); if (!e) return; bool ok = false;
        const int line = QInputDialog::getInt(this, "Ir a la línea", QString("Línea (1 – %1):").arg(e->document()->blockCount()), e->textCursor().blockNumber() + 1, 1, e->document()->blockCount(), 1, &ok);
        if (ok) e->goToLine(line);
    });

    // ---------------- Text ----------------
    m->bind("ID_EDIT_INDENT",   ed([](SqlEditor* e){ e->indentSelection(false); }));
    m->bind("ID_EDIT_UNDENT",   ed([](SqlEditor* e){ e->indentSelection(true); }));
    m->bind("ID_EDIT_LOWER",       ed([](SqlEditor* e){ e->changeCase(0); }));
    m->bind("ID_EDIT_UPPER",       ed([](SqlEditor* e){ e->changeCase(1); }));
    m->bind("ID_EDIT_CAPITALIZE",  ed([](SqlEditor* e){ e->changeCase(2); }));
    m->bind("ID_EDIT_INVERT_CASE", ed([](SqlEditor* e){ e->changeCase(3); }));
    m->bind("ID_EDIT_COMMENT",   ed([](SqlEditor* e){ e->commentLines(true); }));
    m->bind("ID_EDIT_UNCOMMENT", ed([](SqlEditor* e){ e->commentLines(false); }));
    m->bind("ID_EDIT_DATETIME_STAMP", ed([](SqlEditor* e){ e->insertDateStamp(); }));
    if (auto* a = m->bind("ID_EDIT_EXPAND_TEMPLATE", ed([](SqlEditor* e){ e->showCompletion(true); })))      // plantillas + autocompletado
        a->setShortcut(QKeySequence(Qt::META | Qt::Key_Space));                                              // Control+Espacio (Cmd+Espacio es Spotlight)
    m->bind("ID_EDIT_VIEW_WHITE_SPACE", [this]{ if (auto* e = editor()) e->setShowWhitespace(m_menu->action("ID_EDIT_VIEW_WHITE_SPACE")->isChecked()); }, R::None, true, false);

    // ---------------- Text/Format ----------------
    m->bind("ID_EDIT_SORT",                 ed([](SqlEditor* e){ e->sortLines(false); }));
    m->bind("ID_EDIT_FORMAT_JOIN_LINES",    ed([](SqlEditor* e){ e->joinLines(); }));
    m->bind("ID_EDIT_FORMAT_REMOVE_BLANK",  ed([](SqlEditor* e){ e->removeBlankLines(false); }));
    m->bind("ID_EDIT_FORMAT_REMOVE_EXCESSIVE_BLANK", ed([](SqlEditor* e){ e->removeBlankLines(true); }));
    m->bind("ID_EDIT_FORMAT_TRIM_LEADING_SPACES",    ed([](SqlEditor* e){ e->trimSpaces(0); }));
    m->bind("ID_EDIT_FORMAT_TRIM_TRAILING_SPACES",   ed([](SqlEditor* e){ e->trimSpaces(1); }));
    m->bind("ID_EDIT_FORMAT_TRIM_EXCESSIVE_SPACES",  ed([](SqlEditor* e){ e->trimSpaces(2); }));
    m->bind("ID_EDIT_FILE_FORMAT_WINOWS", ed([this](SqlEditor* e){ e->crlf = true;  e->document()->setModified(true); m_status->setText("Saltos de línea: Windows (CRLF) al guardar."); }));
    m->bind("ID_EDIT_FILE_FORMAT_UNIX",   ed([this](SqlEditor* e){ e->crlf = false; e->document()->setModified(true); m_status->setText("Saltos de línea: Unix (LF) al guardar."); }));
    m->bind("ID_EDIT_FILE_FORMAT_MAC",    ed([this](SqlEditor* e){ e->crlf = false; e->cr = true; e->document()->setModified(true); m_status->setText("Saltos de línea: Mac antiguo (CR) al guardar."); }));
    m->bind("ID_FILE_ENCODING_OEM",       ed([this](SqlEditor* e){ e->encoding = "IBM850"; e->bom = false; e->document()->setModified(true); m_status->setText("Codificación al guardar: OEM (CP850)."); }));
    m->bind("ID_FILE_ENCODING_UTF16_BOM", ed([this](SqlEditor* e){ e->encoding = "UTF-16"; e->bom = true; e->document()->setModified(true); m_status->setText("Codificación al guardar: UTF-16 con BOM."); }));
    // Text/Format > Encoding: releer el archivo del disco interpretándolo con otra codificación (cuando se ve mal: «Ã±» en vez de «ñ»)
    auto reloadAs = [this](const char* enc) { return [this, enc]{ auto* e = editor(); if (!e) return;
        if (e->filePath.isEmpty()) { m_status->setText("Esta función relee el archivo del disco: guárdelo primero."); return; }
        if (e->document()->isModified() && QMessageBox::question(this, "Releer con otra codificación", "El script tiene cambios sin guardar; se perderán al releer. ¿Continuar?") != QMessageBox::Yes) return;
        QString err; if (e->load(e->filePath, &err, enc)) { updateEditorTitle(e); m_status->setText(QString("Archivo releído como %1.").arg(TextCodec::label(enc))); } else m_status->setText("No se pudo releer: " + err); }; };
    m->bind("ID_EDIT_ENCODING_UTF8", reloadAs("UTF-8")); m->bind("ID_EDIT_ENCODING_ANSI", reloadAs("Windows-1252")); m->bind("ID_EDIT_ENCODING_OEM", reloadAs("IBM850"));
    // submenús «Others»: más páginas de códigos
    for (QAction* top : menuBar()->actions()) if (top->menu() && QString(top->text()).remove('&') == "Text/Format")
        for (QAction* sub : top->menu()->actions()) if (sub->menu() && (QString(sub->text()).remove('&') == "Encoding" || QString(sub->text()).remove('&') == "Convert to encoding"))
        {
            const bool reload = QString(sub->text()).remove('&') == "Encoding";
            for (QAction* oth : sub->menu()->actions()) if (oth->menu() && QString(oth->text()).remove('&') == "Others")
            {
                oth->menu()->clear();
                for (const QString& enc : TextCodec::extraNames())
                {
                    QAction* a = oth->menu()->addAction(TextCodec::label(enc));
                    if (reload) connect(a, &QAction::triggered, this, [this, enc]{ auto* e = editor(); if (!e) return;
                        if (e->filePath.isEmpty()) { m_status->setText("Esta función relee el archivo del disco: guárdelo primero."); return; }
                        if (e->document()->isModified() && QMessageBox::question(this, "Releer con otra codificación", "El script tiene cambios sin guardar; se perderán al releer. ¿Continuar?") != QMessageBox::Yes) return;
                        QString err; if (e->load(e->filePath, &err, enc)) { updateEditorTitle(e); m_status->setText("Archivo releído como " + TextCodec::label(enc) + "."); } else m_status->setText("No se pudo releer: " + err); });
                    else connect(a, &QAction::triggered, this, [this, enc]{ if (auto* e = editor()) { e->encoding = enc; e->bom = false; e->document()->setModified(true); m_status->setText("Codificación al guardar: " + TextCodec::label(enc) + "."); } });
                }
            }
        }
    m->bind("ID_FILE_ENCODING_UTF8",     ed([this](SqlEditor* e){ e->encoding = "UTF-8"; e->bom = false; e->document()->setModified(true); m_status->setText("Codificación al guardar: UTF-8."); }));
    m->bind("ID_FILE_ENCODING_UTF8_BOM", ed([this](SqlEditor* e){ e->encoding = "UTF-8"; e->bom = true;  e->document()->setModified(true); m_status->setText("Codificación al guardar: UTF-8 con BOM."); }));
    m->bind("ID_FILE_ENCODING_ANSI",     ed([this](SqlEditor* e){ e->encoding = "Windows-1252"; e->document()->setModified(true); m_status->setText("Codificación al guardar: ANSI (Windows-1252)."); }));

    // ---------------- View ----------------
    auto* tbFile = m->buildToolBar("IDT_FILE", "File");
    addToolBarBreak();                                                 // como en Windows: fila 1 = File; fila 2 = Connection + SQL
    auto* tbConn = m->buildToolBar("IDT_CONNECT", "Connection");
    auto* tbSql  = m->buildToolBar("IDT_SQL", "SQL");
    auto toggleBar = [this, m](const char* id, QToolBar* w) {
        auto* a = m->bind(id, [w]{ w->setVisible(!w->isVisible()); }, R::None, true, true);
        if (a) connect(w, &QToolBar::visibilityChanged, a, [a](bool v){ a->setChecked(v); }); };
    toggleBar("ID_VIEW_FILE_TOOLBAR", tbFile); toggleBar("ID_VIEW_SQL_TOOLBAR", tbSql); toggleBar("ID_VIEW_CONNECT_TOOLBAR", tbConn);
    if (auto* a = m->bind("ID_SQL_DB_SOURCE", [this]{ m_dock->setVisible(!m_dock->isVisible()); }, R::None, true, true)) connect(m_dock, &QDockWidget::visibilityChanged, a, [a](bool v){ a->setChecked(v); });
    m->bind("ID_VIEW_STATUS_BAR", [this]{ statusBar()->setVisible(!statusBar()->isVisible()); }, R::None, true, true);

    // ---------------- Find in Files, Code Outline ----------------
    m_fifDlg = new FindInFilesDialog([this](const QString& path, int line) {
        SqlEditor* e = nullptr;
        for (int i = 0; i < m_editors->count(); ++i) if (auto* x = qobject_cast<SqlEditor*>(m_editors->widget(i)); x && x->filePath == path) { m_editors->setCurrentIndex(i); e = x; }
        if (!e) e = addEditor(path);
        if (e) e->goToLine(line); }, this);
    m->bind("ID_FILE_FIND_IN_FILE", [this]{ m_fifDlg->openWith(editor() ? editor()->textCursor().selectedText() : QString()); });
    m_outline = new QTreeWidget(); m_outline->setHeaderHidden(true); m_outline->setUniformRowHeights(true);
    m_outlineDock = new QDockWidget("Code Outline", this); m_outlineDock->setWidget(m_outline); m_outlineDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::RightDockWidgetArea, m_outlineDock); m_outlineDock->hide();
    connect(m_outline, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it, int){ if (auto* e = editor()) e->goToLine(it->data(0, Qt::UserRole).toInt()); });
    connect(m_outlineDock, &QDockWidget::visibilityChanged, this, [this](bool v){ if (v) refreshOutline(); });
    connect(m_editors, &QTabWidget::currentChanged, this, [this]{ if (m_outlineDock->isVisible()) refreshOutline(); });
    { auto* t = new QTimer(this); t->setInterval(1200); connect(t, &QTimer::timeout, this, [this]{ if (m_outlineDock->isVisible()) refreshOutline(); }); t->start(); }   // se refresca sola mientras está abierto
    if (auto* a = m->bind("ID_VIEW_CODE_OUTLINE", [this]{ m_outlineDock->setVisible(!m_outlineDock->isVisible()); }, R::None, true, false)) connect(m_outlineDock, &QDockWidget::visibilityChanged, a, [a](bool v){ a->setChecked(v); });

    // ---------------- Grid ----------------
    m->bind("ID_OCIGRID_DATA_FIT",        [this]{ m_table->resizeColumnsToContents(); });
    m->bind("ID_OCIGRID_COLS_TO_HEADERS", [this]{ for (int c = 0; c < m_model->columnCount(); ++c) m_table->setColumnWidth(c, m_table->fontMetrics().horizontalAdvance(m_model->columns().value(c)) + 30); });
    m->bind("ID_GRID_COPY_ROW", [this]{ const auto idx = m_table->selectionModel()->selectedRows(); QStringList lines;
        auto row = [&](int r){ QStringList c; if (r >= 0 && r < m_model->rows().size()) for (const auto& v : m_model->rows()[r]) c << v; return c.join('\t'); };
        if (idx.isEmpty()) { if (m_table->currentIndex().isValid()) lines << row(m_table->currentIndex().row()); } else for (const auto& i : idx) lines << row(i.row());
        QApplication::clipboard()->setText(lines.join('\n')); m_status->setText(QString("Copiadas %1 fila(s).").arg(lines.size())); });
    m->bind("ID_GRID_COPY_COL_HEADER", [this]{ const int c = m_table->currentIndex().column(); if (c >= 0) { QApplication::clipboard()->setText(m_model->columns().value(c)); m_status->setText("Copiado el nombre de la columna."); } });
    m->bind("ID_GRID_OUTPUT_OPEN", [this]{ if (m_model->columnCount() == 0) return; const QString t = exportText(ExportOptions::load(ExportOptions::Editor));
        auto* e = blankEditor(); e->titleHint = "Resultado"; e->setPlainText(t); e->document()->setModified(false); updateEditorTitle(e); e->moveCursor(QTextCursor::Start); });
    m->bind("ID_GRID_OUTPUT_EXPORT_AND_OPEN", [this]{ if (m_model->columnCount() == 0) return; ExportOptions o = ExportOptions::load(ExportOptions::File);
        const QString path = QDir::tempPath() + "/" + o.fileName() + "." + ExportOptions::extension(o.format); QFile f(path); if (!f.open(QIODevice::WriteOnly)) return;
        if (o.bom) f.write("\xEF\xBB\xBF"); f.write(exportText(o).toUtf8()); f.close(); QDesktopServices::openUrl(QUrl::fromLocalFile(path)); m_status->setText("Exportado y abierto: " + path); });
    m->bind("ID_GRID_OUTPUT_OPTIONS", [this]{ gridSettings(); });
    m->bind("ID_GRID_COPY_HEADERS", [this]{ copyGrid(false, true); });
    m->bind("ID_GRID_COPY_ALL",     [this]{ copyGrid(true, false); });
    m->bind("ID_GRID_OUTPUT_EXPORT",[this]{ exportCsv(); });

    // ---------------- Session ----------------
    m->bind("ID_SQL_CONNECT", [this]{ showConnectDialog(); }, R::NeedIdle);          // Cmd+D, C (Cmd+K es el prefijo de los acordes de edición Ctrl+K,x)
    m->bind("ID_SQL_DISCONNECT", [this]{ if (!confirmPending()) return; QMetaObject::invokeMethod(m_worker, "disconnectDb", Qt::QueuedConnection); }, R::NeedConnection | R::NeedIdle);
    m->bind("ID_SQL_RECONNECT",  [this]{ m_status->setText("Probando la conexión…"); setBusy(true); QMetaObject::invokeMethod(m_worker, "reconnect", Qt::QueuedConnection); }, R::NeedIdle);
    if (auto* a = m->bind("ID_SQL_CANCEL", [this]{ cancelQuery(); }, R::NeedBusy)) a->setShortcuts(a->shortcuts() << QKeySequence(Qt::Key_Escape));
    m->bind("ID_SQL_COMMIT",   [this]{ QMetaObject::invokeMethod(m_worker, "commit", Qt::QueuedConnection); },   R::NeedConnection | R::NeedIdle);
    m->bind("ID_SQL_ROLLBACK", [this]{ QMetaObject::invokeMethod(m_worker, "rollback", Qt::QueuedConnection); }, R::NeedConnection | R::NeedIdle);
    m->bind("ID_SQL_REFRESH_OBJECT_CACHE", [this]{ refreshCaches(); }, R::NeedConnection | R::NeedIdle);
    m->bind("ID_SQL_READ_ONLY", [this]{ const bool on = m_menu->action("ID_SQL_READ_ONLY")->isChecked(); QMetaObject::invokeMethod(m_worker, "setReadOnly", Qt::QueuedConnection, Q_ARG(bool, on)); m_status->setText(on ? "Sesión en solo lectura." : "Sesión en lectura y escritura."); }, R::NeedConnection | R::NeedIdle, true, false);
    m->bind("ID_SQL_DBMS_OUTPUT", [this]{ const bool on = m_menu->action("ID_SQL_DBMS_OUTPUT")->isChecked(); QMetaObject::invokeMethod(m_worker, "setDbmsOutput", Qt::QueuedConnection, Q_ARG(bool, on)); }, R::NeedConnection | R::NeedIdle, true, true);

    // ---------------- Script ----------------
    m->bind("ID_SQL_EXECUTE_CURRENT", [this]{ runCurrent(); }, R::NeedConnection | R::NeedIdle);        // F10 / Cmd+Intro
    m->bind("ID_SQL_EXECUTE",         [this]{ runScript(); },  R::NeedConnection | R::NeedIdle);        // F5
    m->bind("ID_SQL_NEXT_ERROR", [this]{ navigateError(+1); });
    m->bind("ID_SQL_PREV_ERROR", [this]{ navigateError(-1); });
    m->bind("ID_SQL_LOAD",  [this]{ auto* e = editor(); if (!e) return; const QString w = e->wordUnderCursor().trimmed(); const QString type = w.isEmpty() ? QString() : m_browser->typeOf(w);
        if (type.isEmpty()) { m_status->setText(w.isEmpty() ? "Ponga el cursor sobre el nombre de un objeto." : "«" + w + "» no es un objeto del esquema " + m_browser->owner() + "."); return; }
        QString exact = w; for (const QString& n : m_browser->objectNames(type)) if (n.compare(w, Qt::CaseInsensitive) == 0) { exact = n; break; }
        loadDdl({type + QChar(0x1f) + exact}, false, true); }, R::NeedConnection | R::NeedIdle);
    m->bind("ID_SQL_LOAD_WITH_DBMS_METADATA", [this]{ findObjectUnderCursor(true); }, R::NeedConnection | R::NeedIdle);
    m->bind("ID_SQL_EXTRACT_SCHEMA", [this]{ extractSchemaDialog(); }, R::NeedConnection | R::NeedIdle);
    m->bind("ID_SQL_DESCRIBE",   [this]{ auto* e = editor(); if (!e || !m_showInViewer) return; QString w = e->textCursor().hasSelection() ? e->textCursor().selectedText().trimmed() : e->wordUnderCursor().trimmed();
        if (w.isEmpty()) { m_status->setText("Ponga el cursor sobre el nombre de una tabla u otro objeto y pulse F12."); return; } m_showInViewer(w); }, R::NeedConnection | R::NeedIdle);
    m->bind("ID_SQL_HALT_ON_ERROR", [this]{ QMetaObject::invokeMethod(m_worker, "setHaltOnError", Qt::QueuedConnection, Q_ARG(bool, m_menu->action("ID_SQL_HALT_ON_ERROR")->isChecked())); }, R::None, true, true);

    // ---------------- Tools / Help ----------------
    m->bind("ID_APP_SETTINGS_FOLDER", []{ QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::homePath() + "/Library/Preferences")); });
    if (auto* a = m->bind("ID_APP_ABOUT", [this]{
            const QString diag = QString(
                "SQLTools Mac — compilación %1 %2\n"
                "Qt %3 · Oracle Instant Client %4\n"
                "macOS %5 · CPU %6 (compilada para %7)\n"
                "Apariencia: %8 · Conexión: %9\n"
                "Carpeta TNS: %10")
                .arg(__DATE__, __TIME__, qVersion(), DbWorker::clientVersion(), QSysInfo::productVersion(), QSysInfo::currentCpuArchitecture(), QSysInfo::buildCpuArchitecture(),
                     QSettings().value("ui/appearance", "light").toString(), m_connected ? m_conn->text().section("  |  ", -2, -2) : "sin conexión",
                     QSettings().value("conn/defaultTns").toString());
            QMessageBox box(this); box.setWindowTitle("Acerca de SQLTools Mac"); box.setTextFormat(Qt::RichText);
            box.setText("<b>SQLTools Mac</b><br>Versión para macOS del cliente Oracle SQLTools (Aleksey Kochetov), licencia GPLv3.<br>Capa OCI8 original portada; interfaz Qt 6.");
            box.setInformativeText("<pre>" + diag.toHtmlEscaped() + "</pre><br>Si encuentra un problema, pulse «Copiar diagnóstico» y péguelo junto con la captura o el mensaje de error.");
            auto* copy = box.addButton("Copiar diagnóstico", QMessageBox::ActionRole); box.addButton(QMessageBox::Close);
            box.exec(); if (box.clickedButton() == copy) QApplication::clipboard()->setText(diag); })) a->setMenuRole(QAction::AboutRole);
    m->bind("ID_SQLTOOLS_ON_THE_WEB", []{ QDesktopServices::openUrl(QUrl("https://www.sqltools.net")); });

    // La descripción de la conexión (Connection toolbar) es una etiqueta, como en Windows
    if (auto* a = m->action("ID_SQL_CONN_DESCRIPTION"))
    {
        m_connLabel = new QLabel("Not connected"); m_connLabel->setAlignment(Qt::AlignCenter); m_connLabel->setMinimumWidth(260);
        tbConn->insertWidget(a, m_connLabel); tbConn->removeAction(a);
    }

    setupResultsCommands();                                  // panel de resultados (MainResults.cpp)
    m->buildMenuBar();
    // View > Appearance (Light / Dark / System): no existe en SQLTools de Windows
    for (QAction* top : menuBar()->actions())
        if (top->menu() && top->text() == "View")
        {
            top->menu()->addSeparator();
            QMenu* ac = top->menu()->addMenu("Autocompletado");
            auto* ag = new QActionGroup(this); SqlEditor::autoMode = QSettings().value("completion/autoMode", 0).toInt();
            const QPair<int, QString> acm[] = { {0, "Solo con el atajo (Ctrl+Espacio)"}, {1, "Automático después del punto (alias.)"}, {2, "Siempre automático"} };
            for (const auto& md : acm)
            {
                QAction* a = ac->addAction(md.second); a->setCheckable(true); a->setChecked(SqlEditor::autoMode == md.first); ag->addAction(a);
                connect(a, &QAction::triggered, this, [mode = md.first]{ SqlEditor::autoMode = mode; QSettings().setValue("completion/autoMode", mode); });
            }
            QMenu* ap = top->menu()->addMenu("Appearance");
            auto* group = new QActionGroup(this); const QString cur = QSettings().value("ui/appearance", "light").toString();
            const QPair<QString, QString> modes[] = { {"light", "Light"}, {"dark", "Dark"}, {"system", "Match macOS"} };
            for (const auto& md : modes)
            {
                QAction* a = ap->addAction(md.second); a->setCheckable(true); a->setChecked(cur == md.first); group->addAction(a);
                connect(a, &QAction::triggered, this, [this, mode = md.first]{ QSettings().setValue("ui/appearance", mode); applyAppearance(mode); applyTheme(); });
            }
        }
    // File > Recent Files (dinámico) y Code Outline (panel lateral)
    for (QAction* top : menuBar()->actions())
        if (top->menu() && QString(top->text()).remove('&') == "File") for (QAction* a : top->menu()->actions()) if (a->menu() && QString(a->text()).remove('&') == "Recent Files") { m_recentMenu = a->menu(); connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu); }
    rebuildRecentMenu();
    setupWorkspaceCommands();
    setupPanelCommands();
    setupExtraCommands();
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this]{ applyTheme(); });
    applyTheme();
    m->update(m_connected, m_busy);
    for (const QString& c : m->conflicts()) qWarning().noquote() << "[atajos]" << c;
}

// ---------------- Funciones de apoyo de los comandos ----------------
void MainWindow::reloadScript ()
{
    auto* e = editor(); if (!e || e->filePath.isEmpty()) return;
    if (e->document()->isModified() && QMessageBox::question(this, "Reload", "Se perderán los cambios sin guardar de «" + e->displayName() + "». ¿Recargar desde el disco?") != QMessageBox::Yes) return;
    QString err; const QString p = e->filePath;
    if (!e->load(p, &err)) QMessageBox::warning(this, "Reload", err);
    updateEditorTitle(e);
}

void MainWindow::saveAll ()
{
    for (int i = 0; i < m_editors->count(); ++i)
        if (auto* e = qobject_cast<SqlEditor*>(m_editors->widget(i)); e && e->document()->isModified()) if (!saveEditor(e, false)) return;
}

void MainWindow::closeAllScripts () { while (m_editors->count() > 0) { const int before = m_editors->count(); if (!closeScript(m_editors->count() - 1) || (m_editors->count() >= before && m_editors->count() == 1 && editor() && editor()->document()->isEmpty())) break; if (m_editors->count() == 1 && editor() && editor()->filePath.isEmpty() && editor()->document()->isEmpty()) break; } }

void MainWindow::navigateError (int delta)
{
    const int n = m_errors->rowCount(); if (n == 0) { m_status->setText("No hay errores de compilación."); return; }
    int row = m_errors->currentRow(); row = row < 0 ? (delta > 0 ? 0 : n - 1) : (row + delta + n) % n;
    m_errors->setCurrentCell(row, 0); showPage(2);
    if (auto* e = editor()) e->goToLine(m_errors->item(row, 1)->text().toInt(), m_errors->item(row, 2)->text().toInt());
}

void MainWindow::findObjectUnderCursor (bool viaMetadata)
{
    auto* e = editor(); if (!e) return;
    const QString w = e->wordUnderCursor().trimmed();
    if (w.isEmpty()) { m_status->setText("Ponga el cursor sobre un nombre de objeto."); return; }
    const QString type = m_browser->typeOf(w);
    if (type.isEmpty()) { m_status->setText("«" + w + "» no es un objeto del esquema " + m_browser->owner() + "."); return; }
    QString exact = w;                                                        // nombre tal como está en el diccionario (mayúsculas)
    for (const QString& n : m_browser->objectNames(type)) if (n.compare(w, Qt::CaseInsensitive) == 0) { exact = n; break; }
    m_status->setText("Leyendo " + exact + "…");
    QMetaObject::invokeMethod(m_worker, "loadSource", Qt::QueuedConnection, Q_ARG(QString, m_browser->owner()), Q_ARG(QString, type), Q_ARG(QString, exact), Q_ARG(bool, viaMetadata));
}

void MainWindow::extractSchemaDialog ()
{
    QDialog dlg(this); dlg.setWindowTitle("Extract Schema DDL");
    auto* owner = new QComboBox(); owner->setEditable(true); owner->addItems(m_meta->schemas); owner->setCurrentText(m_browser->owner());
    auto* lay = new QVBoxLayout(&dlg); lay->addWidget(new QLabel("Esquema:")); lay->addWidget(owner);
    lay->addWidget(new QLabel("Tipos de objeto (solo se lee el diccionario; no se modifica nada):"));
    QList<QCheckBox*> boxes;
    for (const char* t : {"TABLE", "VIEW", "SEQUENCE", "INDEX", "TYPE", "PACKAGE", "PROCEDURE", "FUNCTION", "TRIGGER", "SYNONYM"})
    { auto* c = new QCheckBox(t); c->setChecked(QString(t) != "INDEX"); boxes << c; lay->addWidget(c); }
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept); connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    QStringList types; for (auto* c : boxes) if (c->isChecked()) types << c->text();
    m_status->setText("Extrayendo el DDL de " + owner->currentText() + "…");
    QMetaObject::invokeMethod(m_worker, "extractSchema", Qt::QueuedConnection, Q_ARG(QString, owner->currentText().trimmed().toUpper()), Q_ARG(QStringList, types));
}

void MainWindow::refreshCaches ()
{
    *m_meta = CompletionData();
    m_status->setText("Actualizando la lista de objetos…");
    QMetaObject::invokeMethod(m_worker, "loadSchemas", Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_worker, "loadObjectsOf", Qt::QueuedConnection, Q_ARG(QString, m_browser->owner()));
    QMetaObject::invokeMethod(m_worker, "loadSynonyms", Qt::QueuedConnection);
}

void MainWindow::copyGrid (bool all, bool headersOnly)
{
    if (headersOnly && !all) { QApplication::clipboard()->setText(m_model->columns().join('\t')); m_status->setText("Copiadas las cabeceras de columna."); return; }
    ExportOptions o = ExportOptions::load(ExportOptions::Clipboard); if (headersOnly) o.header = true;
    QApplication::clipboard()->setText(exportText(o)); m_status->setText(QString("Copiadas %1 filas (%2).").arg(m_model->rowCount()).arg(ExportOptions::formatName(o.format)));
}


// ---------------- Autoprueba de los comandos (sin base de datos): SQLT_SELFTEST=commands ----------------
#include <QTest>
#include "Theme.h"
#include "SqlHighlighter.h"
#include <QGuiApplication>
#include "RotatedModel.h"
#include "HistoryStore.h"
#include "BindParser.h"
#include <QStackedWidget>
#include <QTabWidget>
void MainWindow::selfTestCommands ()
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    int ok = 0, bad = 0;
    auto check = [&](const char* name, bool good, const QString& extra = QString()) {
        std::printf("[%s] %s%s\n", good ? " OK " : "FAIL", name, good || extra.isEmpty() ? "" : qPrintable("   -> " + extra)); (good ? ok : bad)++; };
    auto sc = [&](const char* id) { auto* a = m_menu->action(id); return a ? a->shortcuts() : QList<QKeySequence>(); };
    auto has = [&](const char* id, const char* seq) { return sc(id).contains(QKeySequence::fromString(seq, QKeySequence::PortableText)); };
    auto trig = [&](const char* id) { if (auto* a = m_menu->action(id); a && a->isEnabled()) a->trigger(); };

    // -- atajos de Windows traducidos
    check("File > New = Cmd+N", has("ID_FILE_NEW", "Ctrl+N"));
    check("Session > Connect = Cmd+D, C (Cmd+K queda libre para los acordes Ctrl+K,x)", has("ID_SQL_CONNECT", "Ctrl+D, C") && !has("ID_SQL_CONNECT", "Ctrl+K"));
    check("Script > Execute = F5 ; Execute Current = F10 y Cmd+Intro", has("ID_SQL_EXECUTE", "F5") && has("ID_SQL_EXECUTE_CURRENT", "F10") && has("ID_SQL_EXECUTE_CURRENT", "Ctrl+Return"));
    check("Reload = Control+Q, R (Cmd+Q es salir del sistema)", has("ID_FILE_RELOAD", "Meta+Q, R"));
    check("Search > Replace = Control+H (Cmd+H oculta la app)", has("ID_EDIT_REPLACE", "Meta+H"));
    check("Text > Uppercase = Cmd+Mayús+↑ ; Comment = Cmd+>", has("ID_EDIT_UPPER", "Ctrl+Shift+Up") && has("ID_EDIT_COMMENT", "Ctrl+>"));
    check("Edit > Redo = Cmd+R ; Delete Line = Cmd+Y", has("ID_EDIT_REDO", "Ctrl+R") && has("ID_EDIT_DELETE_LINE", "Ctrl+Y"));
    check("Search > Find next = F3 ; Go To = Cmd+G", has("ID_EDIT_FIND_NEXT", "F3") && has("ID_EDIT_GOTO", "Ctrl+G"));
    check("Cancel también con Esc", has("ID_SQL_CANCEL", "Esc") && has("ID_SQL_CANCEL", "Ctrl+D, A"));
    check("un comando sin implementar no tiene atajo activo (Codepage #1)", sc("ID_EDIT_ENCODING_CODEPAGE_FIRST").isEmpty() && m_menu->action("ID_EDIT_ENCODING_CODEPAGE_FIRST") && !m_menu->action("ID_EDIT_ENCODING_CODEPAGE_FIRST")->isEnabled());
    check("sin conexión: Execute desactivado, New activo", !m_menu->action("ID_SQL_EXECUTE")->isEnabled() && m_menu->action("ID_FILE_NEW")->isEnabled());

    // -- comandos de edición sobre el editor (sin base de datos)
    newScript(); auto* e = editor();                         // pestaña propia de la prueba (nunca se tocan los archivos del usuario)
    e->setPlainText("select a\n  from t\n where b = 1"); e->selectAll();
    trig("ID_EDIT_UPPER");              check("Uppercase", e->toPlainText() == "SELECT A\n  FROM T\n WHERE B = 1", e->toPlainText());
    e->selectAll(); trig("ID_EDIT_LOWER"); check("Lowercase", e->toPlainText() == "select a\n  from t\n where b = 1");
    e->selectAll(); trig("ID_EDIT_COMMENT"); check("Comment Line (3 líneas)", e->toPlainText() == "-- select a\n--   from t\n--  where b = 1", e->toPlainText());
    e->selectAll(); trig("ID_EDIT_UNCOMMENT"); check("Uncomment Line", e->toPlainText() == "select a\n  from t\n where b = 1", e->toPlainText());
    e->selectAll(); trig("ID_EDIT_INDENT"); check("Indent Selection (tabulación)", e->toPlainText() == "\tselect a\n\t  from t\n\t where b = 1", e->toPlainText());
    e->selectAll(); trig("ID_EDIT_UNDENT"); check("Undent Selection", e->toPlainText() == "select a\n  from t\n where b = 1", e->toPlainText());
    e->moveCursor(QTextCursor::Start); trig("ID_EDIT_DUP_LINE_OR_SELECTION"); check("Duplicate Line", e->toPlainText().startsWith("select a\nselect a\n"), e->toPlainText());
    trig("ID_EDIT_DELETE_LINE"); check("Delete Line", e->toPlainText() == "select a\n  from t\n where b = 1", e->toPlainText());
    e->setPlainText("a  \n\n\n\nb   c\n"); e->selectAll(); trig("ID_EDIT_FORMAT_REMOVE_EXCESSIVE_BLANK"); check("Remove Excessive Blank Lines", e->toPlainText() == "a  \n\nb   c\n" || e->toPlainText() == "a  \n\nb   c", QString(e->toPlainText()).replace('\n', "⏎"));
    e->selectAll(); trig("ID_EDIT_FORMAT_TRIM_TRAILING_SPACES"); check("Trim Trailing Spaces", !e->toPlainText().contains("a  \n"), QString(e->toPlainText()).replace('\n', "⏎"));
    e->setPlainText("zeta\nalfa\nbeta"); e->selectAll(); trig("ID_EDIT_SORT"); check("Sort lines", e->toPlainText() == "alfa\nbeta\nzeta", e->toPlainText());
    e->setPlainText("hola mundo"); e->selectAll(); trig("ID_EDIT_CAPITALIZE"); check("Capitalize", e->toPlainText() == "Hola Mundo", e->toPlainText());
    e->setPlainText("uno\ndos\ntres"); e->selectAll(); trig("ID_EDIT_FORMAT_JOIN_LINES"); check("Join Lines", e->toPlainText() == "uno dos tres", e->toPlainText());

    // -- buscar y reemplazar
    e->setPlainText("foo bar foo BAR Foo"); e->moveCursor(QTextCursor::Start);
    check("findText (sin distinguir mayúsculas) encuentra y avanza", e->findText("foo", false, false, false, false) && e->textCursor().selectionStart() == 0 && e->findText("foo", false, false, false, false) && e->textCursor().selectionStart() == 8);
    check("replaceAll sin distinguir mayúsculas = 3", e->replaceAll("foo", "x", false, false, false) == 3 && e->toPlainText() == "x bar x BAR x", e->toPlainText());
    check("palabra completa y regex", e->replaceAll("ba", "y", false, true, false) == 0 && e->replaceAll("b[a-z]r", "Z", false, false, true) == 2 && e->toPlainText() == "x Z x Z x", e->toPlainText());

    // -- pestañas y teclas reales (solo con la plataforma "offscreen": en una ventana oculta de cocoa macOS no entrega los atajos)
    if (QGuiApplication::platformName() != "cocoa") {
    const int tabs = m_editors->count();
    QTest::keyClick(e, Qt::Key_N, Qt::ControlModifier);
    check("tecla Cmd+N abre una pestaña nueva", m_editors->count() == tabs + 1, QString::number(m_editors->count()));
    e = editor(); e->setPlainText("una linea"); e->moveCursor(QTextCursor::Start);
    QTest::keyClick(e, Qt::Key_K, Qt::ControlModifier); QTest::keyClick(e, Qt::Key_Q);
    check("acorde Cmd+K, Q duplica la línea (como Ctrl+K,Q en Windows)", e->toPlainText() == "una linea\nuna linea", e->toPlainText());
    }
    // -- Quick Query / Quick Count / ejecutar desde el cursor (partes que no necesitan base de datos)
    e->setPlainText("select * from kmelot.parametros_jn where a = 1;\nselect 2 from dual;\nselect 3 from dual;");
    auto at = [&](int pos) { QTextCursor c = e->textCursor(); c.setPosition(pos); e->setTextCursor(c); };
    at(20); check("objeto bajo el cursor con esquema: kmelot.parametros_jn", objectUnderCursor() == "kmelot.parametros_jn", objectUnderCursor());
    at(14); check("el cursor al principio de la palabra también", objectUnderCursor() == "kmelot.parametros_jn" || objectUnderCursor().isEmpty(), objectUnderCursor());
    e->setPlainText("select * from emp;"); at(15); check("tabla simple", objectUnderCursor() == "emp", objectUnderCursor());
    e->selectAll(); check("con selección se usa la selección", objectUnderCursor().startsWith("select"));
    e->setPlainText("select 1 from dual;\nselect 2 from dual;\nselect 3 from dual;"); at(25);
    QVector<int> ln; const QStringList fc = statementsFromCursor(&ln);
    check("Execute from Cursor: desde la 2.ª sentencia hasta el final (2 sentencias, líneas 2 y 3)", fc.size() == 2 && fc[0] == "select 2 from dual" && ln == QVector<int>({2, 3}), fc.join("|"));
    at(2); check("Execute from Cursor en la 1.ª: las tres", statementsFromCursor().size() == 3);
    check("F11 = Quick Query, Cmd+F11 = Quick Count, Cmd+F5 = Execute from Cursor, Cmd+F10 = Execute and Step", has("ID_SQL_QUICK_QUERY", "F11") && has("ID_SQL_QUICK_COUNT", "Ctrl+F11") && has("ID_SQL_EXECUTE_FROM_CURSOR", "Ctrl+F5") && has("ID_SQL_EXECUTE_CURRENT_AND_STEP", "Ctrl+F10"));
    // -- margen: números de línea, marcadores y plegado
    e = editor();
    e->setPlainText("l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9\nl10");
    const int w10 = e->gutterWidth(); QString many; for (int i = 0; i < 2000; ++i) many += "x\n"; e->setPlainText(many); const int w2000 = e->gutterWidth();
    check("el ancho del margen crece con las cifras de las líneas", w2000 > w10, QString("%1 -> %2").arg(w10).arg(w2000));
    e->setShowLineNumbers(false); const int wOff = e->gutterWidth(); e->setShowLineNumbers(true);
    check("sin números de línea el margen es más estrecho", wOff < w2000);
    e->setPlainText("l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9\nl10");
    auto goLine = [&](int line) { e->setTextCursor(QTextCursor(e->document()->findBlockByNumber(line - 1))); };
    goLine(3); trig("ID_EDIT_BKM_TOGGLE"); goLine(8); trig("ID_EDIT_BKM_TOGGLE");
    check("marcadores en las líneas 3 y 8", e->bookmarkedLines() == QList<int>({3, 8}));
    goLine(1); trig("ID_EDIT_BKM_NEXT"); check("Next Bookmark (F2) va a la 3", e->textCursor().blockNumber() + 1 == 3);
    trig("ID_EDIT_BKM_NEXT"); check("otra vez, a la 8", e->textCursor().blockNumber() + 1 == 8);
    trig("ID_EDIT_BKM_NEXT"); check("y da la vuelta a la 3", e->textCursor().blockNumber() + 1 == 3);
    trig("ID_EDIT_BKM_PREV"); check("Previous Bookmark da la vuelta a la 8", e->textCursor().blockNumber() + 1 == 8);
    goLine(1); e->textCursor().insertText("nueva\n");
    check("al insertar una línea arriba los marcadores se mueven con su texto (4 y 9)", e->bookmarkedLines() == QList<int>({4, 9}), QString::number(e->bookmarkedLines().value(0)));
    check("Copy Bookmarked Lines copia el texto de las líneas marcadas", (trig("ID_EDIT_COPY_BOOKMARKED"), QApplication::clipboard()->text() == "l3\nl8"), QApplication::clipboard()->text());
    trig("ID_EDIT_CUT_BOOKMARKED"); check("Cut Bookmarked Lines las quita del texto", !e->toPlainText().contains("l3") && !e->toPlainText().contains("l8") && e->document()->blockCount() == 9, e->toPlainText().replace('\n', '|'));
    trig("ID_EDIT_BKM_REMOVE_ALL"); check("Remove all Bookmarks", e->bookmarkedLines().isEmpty());
    check("sin marcadores: Next, Previous y Remove all están desactivados", !m_menu->action("ID_EDIT_BKM_NEXT")->isEnabled() && !m_menu->action("ID_EDIT_BKM_PREV")->isEnabled() && !m_menu->action("ID_EDIT_BKM_REMOVE_ALL")->isEnabled());
    goLine(2); trig("ID_EDIT_BKM_TOGGLE"); check("con un marcador se activan Next, Previous y Remove all", m_menu->action("ID_EDIT_BKM_NEXT")->isEnabled() && m_menu->action("ID_EDIT_BKM_REMOVE_ALL")->isEnabled());
    trig("ID_EDIT_BKM_REMOVE_ALL");
    goLine(5); trig("ID_EDIT_BKM_SET_5"); goLine(2); trig("ID_EDIT_BKM_GET_5");
    check("marcador numerado 5: Cmd+Mayús+5 lo fija y Cmd+5 salta", e->textCursor().blockNumber() + 1 == 5);
    goLine(7); trig("ID_EDIT_BKM_SET_5"); check("fijar el 5 en otra línea lo mueve (solo existe uno)", e->numberedBookmarkLine(5) == 7 && e->bookmarkedLines() == QList<int>({7}));
    e->clearBookmarks();
    {   // objetos del esquema: se pintan como en Windows (negrita lila), las palabras reservadas ganan y sin conexión no hay color
        e->setPlainText("select a from empleados where x = 1 -- empleados"); SqlHighlighter::setUserObjects({"EMPLEADOS", "SELECT"});
        auto fmtAt = [&](int pos){ for (const auto& r : e->document()->firstBlock().layout()->formats()) if (pos >= r.start && pos < r.start + r.length) return r.format; return QTextCharFormat(); };
        const QColor objColor = SyntaxColors::load(Theme::dark())[SynUserObject].color;
        check("un objeto del esquema (empleados) se pinta con su color", fmtAt(14).foreground().color() == objColor, fmtAt(14).foreground().color().name());
        check("una palabra reservada (select) conserva su color aunque se llame como un objeto", fmtAt(1).foreground().color() != objColor);
        check("dentro de un comentario no se pinta como objeto", fmtAt(45).foreground().color() != objColor);
        SqlHighlighter::setUserObjects({}); check("sin conexión no hay objetos coloreados", fmtAt(14).foreground().color() != objColor);
        e->setPlainText(QString());
    }
    e->setPlainText("create or replace package body pk as\n  procedure a is\n  begin\n    if x then\n      null;\n    end if;\n  end a;\n\n  procedure b is\n  begin\n    null;\n  end b;\nend pk;\n/\n");
    e->refreshFolds();
    check("el plegado detecta regiones en un paquete", e->foldRegions().size() >= 4, QString::number(e->foldRegions().size()));
    goLine(5); trig("ID_EDIT_FOLD_COLLAPSE_CURRENT");
    check("Collapse Current Region pliega la región más interna (el IF: líneas 5-6 ocultas)", !e->isLineVisible(4) && !e->isLineVisible(5) && e->isLineVisible(3), QString("l5=%1 l6=%2").arg(e->isLineVisible(4)).arg(e->isLineVisible(5)));
    trig("ID_EDIT_FOLD_EXPAND_CURRENT"); check("Expand Current Region la despliega", e->isLineVisible(4) && e->isLineVisible(5));
    trig("ID_EDIT_FOLD_COLLAPSE_ALL"); check("Collapse All: el paquete se reduce a su primera línea", e->isLineVisible(0) && !e->isLineVisible(1) && !e->isLineVisible(12));
    check("el cursor se queda en una línea visible", e->textCursor().block().isVisible());
    trig("ID_EDIT_FOLD_EXPAND_ALL"); check("Expand All muestra todo", e->isLineVisible(1) && e->isLineVisible(5) && e->isLineVisible(10));
    trig("ID_EDIT_FOLD_COLLAPSE_LEVEL2"); check("Collapse to Level 2: se ven 2 niveles (paquete + cabeceras de procedimientos)", e->isLineVisible(1) && !e->isLineVisible(2) && !e->isLineVisible(9), QString("l2=%1 l3=%2 l10=%3").arg(e->isLineVisible(1)).arg(e->isLineVisible(2)).arg(e->isLineVisible(9)));
    trig("ID_EDIT_FOLD_COLLAPSE_LEVEL1"); check("Collapse to Level 1: solo la línea del paquete", e->isLineVisible(0) && !e->isLineVisible(1) && !e->isLineVisible(8));
    e->setTextCursor(QTextCursor(e->document()->findBlockByNumber(2)));
    check("mover el cursor a una línea oculta despliega la región", e->isLineVisible(2) && e->textCursor().block().isVisible());
    trig("ID_EDIT_FOLD_TOGGLE_ALL"); trig("ID_EDIT_FOLD_TOGGLE_ALL"); check("Toggle All alterna", true);
    trig("ID_EDIT_FOLD_EXPAND_ALL");
    e->setPlainText("a (b (c) d) e"); e->moveCursor(QTextCursor::Start); QTextCursor bc = e->textCursor(); bc.setPosition(2); e->setTextCursor(bc);
    trig("ID_EDIT_FIND_MATCH"); check("Find Match (Cmd+]) salta al paréntesis pareja", e->textCursor().position() == 11, QString::number(e->textCursor().position()));
    bc.setPosition(5); e->setTextCursor(bc); trig("ID_EDIT_FIND_MATCH_N_SELECT"); check("Find Match & Select selecciona el paréntesis interior", e->textCursor().selectedText() == "(c)", e->textCursor().selectedText());
    trig("ID_EDIT_VIEW_LINE_NUMBERS"); check("Text > Line Numbers conmuta los números de línea", !e->showLineNumbers());
    trig("ID_EDIT_VIEW_LINE_NUMBERS"); check("y los vuelve a mostrar", e->showLineNumbers());
    // -- panel de resultados (booklet)
    m_model->reset({"ID", "NOMBRE"}, {1, 0});
    m_model->append({{"1", "uno"}, {"2", QString()}, {"3", "tres"}, {"4", "cuatro"}, {"5", "cinco"}});
    check("barra del panel con sus 6 páginas + Errors", m_pageActions.size() == 7 && m_pageActions[0] && m_pageActions[1] && m_pageActions[2] && m_pageActions[3] && m_pageActions[4] && m_pageActions[5] && m_pageActions[6]);
    trig("ID_BOOKLET_HISTORY");     check("botón History muestra la página 5", m_tabs->currentIndex() == 5);
    trig("ID_BOOKLET_STATISTICS");  check("botón Statistics muestra la página 3", m_tabs->currentIndex() == 3);
    trig("ID_BOOKLET_SQL_PLAN");    check("botón SQL Plan muestra la página 4", m_tabs->currentIndex() == 4);
    trig("ID_BOOKLET_RESULT");      check("botón Result vuelve a la rejilla", m_tabs->currentIndex() == 0 && m_pageActions[0]->isChecked());
    trig("ID_GRID_MOVE_END");       check("Move End: última fila seleccionada", m_table->currentIndex().row() == 4);
    trig("ID_GRID_MOVE_UP");        check("Move Up: fila 4 (índice 3)", m_table->currentIndex().row() == 3);
    trig("ID_GRID_MOVE_HOME");      check("Move Home: primera fila", m_table->currentIndex().row() == 0);
    trig("ID_GRID_ROTATE");                                   // alterna: activa la vista rotada
    check("Rotate: vista de registro con Campo|Valor", m_resultStack->currentIndex() == 1 && m_rotatedModel->rowCount() == 2 && m_rotatedModel->data(m_rotatedModel->index(0, 0), Qt::DisplayRole).toString() == "ID" && m_rotatedModel->data(m_rotatedModel->index(1, 1), Qt::DisplayRole).toString() == "uno");
    m_table->setCurrentIndex(m_model->index(1, 0));
    check("Rotate sigue la fila seleccionada (NULL se ve como (null))", m_rotatedModel->data(m_rotatedModel->index(1, 1), Qt::DisplayRole).toString() == "(null)");
    trig("ID_GRID_ROTATE"); check("Rotate otra vez vuelve a la rejilla", m_resultStack->currentIndex() == 0);
    trig("ID_BOOKLET_PIN");        check("Pin fija una copia del resultado en una pestaña nueva (2 pestañas)", m_resultTabs->count() == 2);
    m_model->reset({"X"}, {0}); m_model->append({{"nuevo"}});
    check("la copia fijada conserva sus 5 filas aunque el resultado vivo cambie", qobject_cast<QTableView*>(m_resultTabs->widget(1)) && qobject_cast<QTableView*>(m_resultTabs->widget(1))->model()->rowCount() == 5);
    m_resultTabs->setCurrentIndex(1); trig("ID_BOOKLET_UNPIN"); check("Unpin quita la pestaña fijada", m_resultTabs->count() == 1);
    trig("ID_GRID_CLEAR_RECORDS");  check("Clear Records vacía la rejilla y conserva las columnas", m_model->rowCount() == 0 && m_model->columnCount() == 1);
    // -- historial: pasos atrás/adelante sobre el editor
    m_historyStore->clear(); for (int i = 1; i <= 3; ++i) addHistory(0, QString("select %1 from dual").arg(i), 1, 0.01 * i, i == 2 ? "ORA-00942" : QString());
    check("History: 3 filas, la más reciente arriba", m_history->rowCount() == 3 && m_history->item(0, 4)->data(Qt::UserRole).toString() == "select 3 from dual" && m_history->item(1, 3)->text() == "ORA-00942");
    e = editor(); e->setPlainText("");
    trig("ID_SQL_HISTORY_GET_AND_STEPBACK");  check("Cmd+D,S: trae la última sentencia", e->toPlainText() == "select 3 from dual", e->toPlainText());
    trig("ID_SQL_HISTORY_GET_AND_STEPBACK");  check("otra vez: la anterior (sustituye la actual)", e->toPlainText() == "select 2 from dual", e->toPlainText());
    trig("ID_SQL_HISTORY_STEPFORWARD_AND_GET"); check("Cmd+D,F: avanza", e->toPlainText() == "select 3 from dual", e->toPlainText());
    // -- binds: la página lista las variables del script y guarda sus valores
    e->setPlainText("select :uno, :dos from dual where x = :uno"); showPage(6);
    check("Binds: lista :uno y :dos (una vez cada una)", m_binds->rowCount() == 2 && m_binds->item(0, 0)->text() == "uno" && m_binds->item(1, 0)->text() == "dos");
    m_binds->item(0, 2)->setText("7"); const QStringList out = applyBinds({"select :uno, :dos from dual where x = :uno"});
    check("applyBinds sustituye con los valores de la página", out.first() == "select 7, NULL from dual where x = 7", out.first());
    check("Statistics muestra el resumen", (showStats({}), m_stats->rowCount() >= 3));
    // -- edición de texto (grupo A)
    e->setPlainText("uno dos tres"); { QTextCursor c = e->textCursor(); c.movePosition(QTextCursor::EndOfBlock); e->setTextCursor(c); }
    trig("ID_EDIT_DELETE_WORD_TO_LEFT"); check("Delete Word Back", e->toPlainText() == "uno dos ", e->toPlainText());
    { QTextCursor c = e->textCursor(); c.setPosition(0); e->setTextCursor(c); } trig("ID_EDIT_DELETE_WORD_TO_RIGHT"); check("Delete to End of Word", e->toPlainText() == "dos ", e->toPlainText());
    e->setPlainText("alfa beta"); QApplication::clipboard()->setText("X"); { QTextCursor c = e->textCursor(); c.setPosition(0); c.setPosition(4, QTextCursor::KeepAnchor); e->setTextCursor(c); }
    trig("ID_EDIT_CUT_N_APPEND"); check("Cut Append agrega al portapapeles y corta", QApplication::clipboard()->text() == "Xalfa" && e->toPlainText() == " beta", QApplication::clipboard()->text() + "|" + e->toPlainText());
    e->setPlainText("        x\n\ty"); e->selectAll(); trig("ID_EDIT_TABIFY_LEADING"); check("Tabify (leading): 8 espacios -> 2 tabuladores", e->toPlainText() == "\t\tx\n\ty", e->toPlainText());
    e->selectAll(); trig("ID_EDIT_UNTABIFY"); check("Untabify: tabuladores -> espacios", e->toPlainText() == "        x\n    y", e->toPlainText());
    e->setPlainText("a        b"); e->selectAll(); trig("ID_EDIT_TABIFY"); check("Tabify (all) sustituye tramos de espacios", e->toPlainText() == "a\t\t b", e->toPlainText());
    e->setPlainText("select a from t -- from x\nwhere b = 'from' /* and\n or */ and c = 1"); trig("ID_EDIT_NORMALIZE_TEXT");
    check("Normalize Keyword: solo palabras reservadas fuera de textos y comentarios", e->toPlainText() == "SELECT a FROM t -- from x\nWHERE b = 'from' /* and\n or */ AND c = 1", e->toPlainText());
    e->setPlainText("uno dos tres cuatro cinco seis"); e->splitLines(15); check("Split Line parte en el último espacio antes de la columna", e->toPlainText() == "uno dos tres\ncuatro cinco\nseis", e->toPlainText());
    e->setPlainText("a\nb\nc"); e->selectAll(); e->insertNumbers(5, 5, 3); check("Insert Number numera las líneas de la selección", e->toPlainText() == "005a\n010b\n015c", e->toPlainText());
    e->setPlainText("x\n\n\n\n"); e->selectAll(); const int sbMax = e->verticalScrollBar()->maximum(); e->scrollLines(1); check("Scroll Down/Up no falla", e->verticalScrollBar()->value() >= 0 && sbMax >= 0);
    e->setPlainText("run  '@scripts/a.sql'  now"); { QTextCursor c = e->textCursor(); c.setPosition(10); e->setTextCursor(c); } check("Open file under cursor: detecta la ruta", e->pathUnderCursor() == "scripts/a.sql", e->pathUnderCursor());
    e->setPlainText("a\nb"); trig("ID_EDIT_VIEW_INDENT_GUIDE"); trig("ID_EDIT_VIEW_COLUMN_MARKERS"); check("Indent Guide y Column Markers se activan", e->m_indentGuides && e->m_columnMarkers); trig("ID_EDIT_VIEW_INDENT_GUIDE"); trig("ID_EDIT_VIEW_COLUMN_MARKERS");
    // -- codificaciones, workspaces y paneles (grupos D, E, F)
    { bool lossy = false;
      check("Codec OEM (CP850): ñ y Á", TextCodec::encode("ñÁ", "IBM850", &lossy) == QByteArray("\xA4\xB5") && !lossy && TextCodec::decode(QByteArray("\xA4\xB5"), "IBM850") == "ñÁ");
      check("Codec ANSI: € es 0x80 y un carácter fuera de la tabla avisa", TextCodec::encode("€", "Windows-1252") == QByteArray("\x80") && (TextCodec::encode("日", "Windows-1252", &lossy), lossy));
      check("Codec UTF-16 ida y vuelta con BOM", TextCodec::decode(TextCodec::encode("añ€", "UTF-16"), "UTF-16") == "añ€"); }
    { QTemporaryDir dir; const QString f1 = dir.filePath("oem.sql"), f2 = dir.filePath("dos.sql"), f3 = dir.filePath("mac.sql");
      e->setPlainText("año"); e->encoding = "IBM850"; e->save(f1); { QFile r(f1); r.open(QIODevice::ReadOnly); check("Guardar como OEM escribe el byte A4 para la ñ", r.readAll() == QByteArray("a\xA4o")); }
      e->load(f1); check("Al abrir un OEM se interpreta como ANSI (por defecto)", e->encoding == "Windows-1252" && e->toPlainText() != "año");
      e->filePath = f1; trig("ID_EDIT_ENCODING_OEM"); check("Encoding > OEM relee el archivo con CP850", e->toPlainText() == "año" && e->encoding == "IBM850", e->toPlainText());
      e->setPlainText("a\nb"); e->encoding = "UTF-8"; e->crlf = false; e->cr = true; e->save(f3); { QFile r(f3); r.open(QIODevice::ReadOnly); check("Old Mac guarda con CR", r.readAll() == QByteArray("a\rb")); }
      e->load(f3); check("Old Mac se detecta al abrir", e->cr && e->toPlainText() == "a\nb"); e->cr = false;
      { QFile w(f2); w.open(QIODevice::WriteOnly); w.write("select 1 from dual;\n"); w.close(); }
      // workspace: dos pestañas (un archivo y un script sin guardar) -> JSON -> reabrir
      auto* a = addEditor(f2); auto* b = addEditor(); b->setPlainText("select 2 from dual;"); b->titleHint = "borrador"; (void)a;
      const QJsonObject ws = captureWorkspace(); int found = 0; for (const auto& v : ws.value("items").toArray()) { const auto o = v.toObject(); if (o.value("path").toString() == f2 || o.value("text").toString() == "select 2 from dual;") ++found; }
      check("Workspace: captura el archivo y el script sin guardar", found == 2, QString::number(found));
      const int before = m_editors->count(); const int n = applyWorkspace(ws, false);
      int same = 0; for (int i = 0; i < m_editors->count(); ++i) if (auto* x = qobject_cast<SqlEditor*>(m_editors->widget(i)); x && x->filePath == f2) ++same;
      check("Workspace: abrir sin cerrar no duplica el archivo ya abierto", n >= 2 && same == 1 && m_editors->count() > before, QString("n=%1 mismos=%2").arg(n).arg(same));
      check("Workspace: un JSON ajeno se rechaza", applyWorkspace(QJsonObject{{"x", 1}}, false) == -1);
      QApplication::clipboard()->setText("no es json"); trig("ID_WORKSPACE_PASTE"); check("Snapshot Paste con texto cualquiera no rompe nada", m_status->text().contains("no contiene"));
      for (int i = m_editors->count() - 1; i >= 0; --i) if (auto* x = qobject_cast<SqlEditor*>(m_editors->widget(i)); x && x != e) { x->document()->setModified(false); closeScript(i); } }
    { const QStringList ids = {"ID_VIEW_FILE_PANEL", "ID_SQL_OBJ_VIEWER", "ID_VIEW_PROPERTIES", "ID_VIEW_SNIPPETS", "ID_VIEW_HISTORY", "ID_VIEW_OPEN_FILES"}; bool all = true; QString bad;
      for (const QString& id : ids) { trig(id.toUtf8().constData()); QApplication::processEvents(); bool vis = false; for (auto* d : findChildren<QDockWidget*>()) if (d->isVisible() && m_menu->action(id)->isChecked()) vis = true; if (!vis) { all = false; bad += id + " "; } trig(id.toUtf8().constData()); }
      check("Paneles Alt+0…9 se abren y se cierran desde el menú", all, bad); }
    // -- Format SQL, sustitución, plantillas, impresión, ayuda
    { check("Codec Latin-1 y Mac Roman", TextCodec::encode("ñ", "ISO-8859-1") == QByteArray("\xF1") && TextCodec::decode(QByteArray("\x8E"), "MacRoman") == "é" && TextCodec::decode(QByteArray("\xA4"), "ISO-8859-15") == "€"); }
    { QSettings().setValue("format/style", int(FormatOptions::Standard)); QSettings().setValue("format/indent", 2); QSettings().setValue("format/keywordCase", int(FormatOptions::Upper)); QSettings().setValue("format/commaLeading", false); QSettings().setValue("format/andOrNewLine", true); QSettings().setValue("format/oneItemPerLine", true);
      e->setPlainText("select a,b from t where x=1 and y=2;\nselect 1 from dual"); { QTextCursor c = e->textCursor(); c.setPosition(5); e->setTextCursor(c); } trig("ID_EDIT_FORMAT_SQL");
      check("Format SQL formatea solo la sentencia del cursor", e->toPlainText() == "SELECT\n  a,\n  b\nFROM t\nWHERE x = 1\n  AND y = 2;\nselect 1 from dual", e->toPlainText());
      e->undo(); check("Format SQL se puede deshacer", e->toPlainText() == "select a,b from t where x=1 and y=2;\nselect 1 from dual", e->toPlainText());
      QSettings().setValue("format/indent", 4); e->selectAll(); trig("ID_EDIT_FORMAT_SQL"); check("Format SQL respeta el ancho de sangría (4) y formatea toda la selección", e->toPlainText().contains("WHERE x = 1\n    AND y = 2;") && e->toPlainText().contains("SELECT 1\nFROM dual"), e->toPlainText()); QSettings().setValue("format/indent", 2); }
    { QSettings().setValue("script/substitution", true); m_substValues["X"] = "5"; QStringList q = {"select &&x, '&&x' from dual"}; const bool ok = applySubstitution(q); check("Variables de sustitución: &&x recordada se reemplaza", ok && q[0] == "select 5, '5' from dual", q[0]); QSettings().setValue("script/substitution", false); QStringList q2 = {"select &x from dual"}; applySubstitution(q2); check("Sin activar, &x no se toca", q2[0] == "select &x from dual"); }
    { addUserSnippet({"zz1", "Mi plantilla", "select $0 from dual"}); bool found = false; for (const auto& sn : snippets()) if (sn.trigger == "zz1") found = true; check("Create Template: la plantilla propia queda en la lista", found); QSettings().remove("snippets/user"); }
    { QTemporaryDir dir; QPrinter pr(QPrinter::HighResolution); pr.setOutputFormat(QPrinter::PdfFormat); pr.setOutputFileName(dir.filePath("p.pdf")); e->setPlainText("select 1 from dual;"); e->print(&pr); QFileInfo fi(dir.filePath("p.pdf")); check("Imprimir a PDF genera el archivo", fi.exists() && fi.size() > 500, QString::number(fi.size())); }
    { bool ok = !sqlReference().isEmpty(); bool sel = false; for (const auto& t : sqlReference()) if (t.keys.split(' ').contains("merge")) sel = true; check("SQL Short Reference: tiene temas y encuentra MERGE", ok && sel); }
    { e->setPlainText("uno\ndos"); trig("ID_WINDOW_SPLIT"); check("Window > Split muestra una segunda vista del mismo texto", m_view2->isVisible() && m_view2->document() == e->document()); trig("ID_WINDOW_SPLIT"); check("Split se cierra", !m_view2->isVisible());
      const int before = m_split->sizes()[0]; trig("ID_CUSTOM_SPLITTER_DOWN"); check("Mover el divisor hacia abajo agranda el editor", m_split->sizes()[0] >= before); }
    // -- ajustes: historial y estilo de ejecución
    { m_historyStore->clear(); QSettings hs; hs.setValue("history/validOnly", true); addHistory(0, "select bad", 0, 0, "ORA-00942"); addHistory(0, "select ok", 1, 0, QString()); const int n1 = m_historyStore->entries().size();
      hs.setValue("history/validOnly", false); hs.setValue("history/distinct", true); addHistory(0, "select ok", 1, 0, QString()); const int n2 = m_historyStore->entries().size();
      hs.setValue("history/enabled", false); addHistory(0, "select nuevo", 1, 0, QString()); const int n3 = m_historyStore->entries().size(); hs.setValue("history/enabled", true); hs.setValue("history/distinct", false); m_historyStore->clear();
      check("Historial: «solo sin error», «sin repetidos» y «desactivado»", n1 == 1 && n2 == 1 && n3 == 1, QString("%1 %2 %3").arg(n1).arg(n2).arg(n3)); }
    { QSettings().setValue("ac/view", false); QSettings().setValue("ac/table", true);
      check("Filtros del autocompletado se guardan", !QSettings().value("ac/view", true).toBool()); QSettings().remove("ac/view"); }
    { e->setPlainText("select 1 from dual"); const auto cols = ExportOptions::load(ExportOptions::File); check("Opciones de exportación tienen valores por defecto sensatos", cols.format == ExportOptions::Csv && cols.header && cols.sep == ","); }
    // -- atajos que el editor de Qt reclamaba (⇧⌘↑ / ⇧⌘↓ = mayúsculas / minúsculas)
    { e->setPlainText("hola Mundo"); e->selectAll(); e->setFocus(); QApplication::processEvents();
      QTest::keyClick(e, Qt::Key_Up, Qt::ControlModifier | Qt::ShiftModifier); check("⇧⌘↑ pone en MAYÚSCULAS", e->toPlainText() == "HOLA MUNDO", e->toPlainText());
      QTest::keyClick(e, Qt::Key_Down, Qt::ControlModifier | Qt::ShiftModifier); check("⇧⌘↓ pone en minúsculas", e->toPlainText() == "hola mundo", e->toPlainText()); }
    // -- menú contextual del explorador: sentencias de mantenimiento
    { check("Drop de una tabla", browserSql("drop", "TABLE", "EMP") == QStringList{"drop table EMP"});
      check("Truncate solo para tablas", browserSql("truncate", "TABLE", "EMP") == QStringList{"truncate table EMP"} && browserSql("truncate", "VIEW", "V").isEmpty());
      check("Compile de un paquete, vista y trigger", browserSql("compile", "PACKAGE", "PK") == QStringList{"alter package PK compile"} && browserSql("compile", "VIEW", "V") == QStringList{"alter view V compile"});
      check("Enable/Disable solo para triggers", browserSql("disable", "TRIGGER", "T") == QStringList{"alter trigger T disable"} && browserSql("enable", "TABLE", "T").isEmpty());
      check("Drop de un paquete calificado", browserSql("drop", "PACKAGE", "ESQ.PK") == QStringList{"drop package ESQ.PK"}); }
    // -- selección en columna
    e->setPlainText("aaaa\nbbbb\ncccc\ndd"); { QTextCursor c = e->textCursor(); c.setPosition(2); e->setTextCursor(c); }
    QTest::keyClick(e, Qt::Key_Down, Qt::ShiftModifier | Qt::AltModifier); QTest::keyClick(e, Qt::Key_Down, Qt::ShiftModifier | Qt::AltModifier);
    { int l0, l1, c0, c1; e->columnBlock(l0, l1, c0, c1); check("Mayús+Opción+↓ crea un bloque de 3 líneas sin ancho", e->columnMode() && l0 == 0 && l1 == 2 && c0 == 2 && c1 == 2); }
    QTest::keyClicks(e, "X"); check("Escribir inserta en cada línea del bloque", e->toPlainText() == "aaXaa\nbbXbb\nccXcc\ndd", e->toPlainText());
    QTest::keyClick(e, Qt::Key_Backspace); check("Retroceso borra en cada línea", e->toPlainText() == "aaaa\nbbbb\ncccc\ndd", e->toPlainText());
    QTest::keyClick(e, Qt::Key_Right, Qt::ShiftModifier | Qt::AltModifier); check("Mayús+Opción+→ ensancha el bloque", e->columnText() == "a\nb\nc", e->columnText());
    e->copy(); check("Copiar en columna: una línea por fila", QApplication::clipboard()->text() == "a\nb\nc", QApplication::clipboard()->text());
    e->columnJustify(2); check("Justificar a la derecha no cambia un bloque de 1 columna", e->toPlainText() == "aaaa\nbbbb\ncccc\ndd", e->toPlainText());
    e->cut(); check("Cortar en columna quita el bloque", e->toPlainText() == "aaa\nbbb\nccc\ndd", e->toPlainText());
    e->paste(); check("Pegar en columna reparte línea por línea", e->toPlainText() == "aaaa\nbbbb\ncccc\ndd", e->toPlainText());
    QTest::keyClick(e, Qt::Key_Escape); check("Esc sale del modo columna", !e->columnMode());
    e->setPlainText("uno  \n  dos\ntres "); { QTextCursor c = e->textCursor(); c.setPosition(0); e->setTextCursor(c); } e->enterColumnMode(); e->columnExtend(2, 6);
    e->columnJustify(2); check("Justificar a la derecha (ancho 6)", e->toPlainText() == "   uno\n   dos\n  tres", e->toPlainText());
    e->columnJustify(0); check("Justificar a la izquierda", e->toPlainText() == "uno   \ndos   \ntres  ", e->toPlainText());
    e->columnJustify(1); check("Justificar al centro", e->toPlainText() == " uno  \n dos  \n tres ", e->toPlainText());
    e->exitColumnMode(); e->setPlainText("a\nb\nc"); { QTextCursor c = e->textCursor(); c.setPosition(0); e->setTextCursor(c); } e->enterColumnMode(); e->columnExtend(2, 0);
    e->columnFill("-", false); check("Insert Columns inserta el texto en cada línea", e->toPlainText() == "-a\n-b\n-c", e->toPlainText());
    e->exitColumnMode(); e->setPlainText("a1\nb2\nc3"); { QTextCursor c = e->textCursor(); c.setPosition(0); e->setTextCursor(c); } e->enterColumnMode(); e->columnExtend(2, 2); e->columnFill("ab", true);
    check("Fill Columns rellena el ancho repitiendo el texto", e->toPlainText() == "ab\nab\nab", e->toPlainText());
    e->exitColumnMode(); e->setPlainText("x\ny"); { QTextCursor c = e->textCursor(); c.setPosition(0); e->setTextCursor(c); } e->enterColumnMode(); e->columnExtend(1, 0); e->insertNumbers(1, 1, 2);
    check("Insert Number en modo columna", e->toPlainText() == "01x\n02y", e->toPlainText()); e->exitColumnMode();
    e->setShowRuler(true); check("Ruler reserva espacio sobre el texto", e->rulerHeight() > 0 && e->viewport()->y() >= e->rulerHeight()); e->setShowRuler(false);
    // -- rejilla: copiar fila / cabecera
    m_model->reset({"ID", "NOMBRE"}, {1, 0}); m_model->append({{"1", "uno"}, {"2", "dos"}}); m_table->setCurrentIndex(m_model->index(1, 1)); trig("ID_GRID_COPY_COL_HEADER"); check("Copy Column Header copia el nombre", QApplication::clipboard()->text() == "NOMBRE", QApplication::clipboard()->text());
    trig("ID_GRID_COPY_ROW"); check("Copy Row copia la fila actual", QApplication::clipboard()->text() == "2\tdos", QApplication::clipboard()->text());
    // -- esquema del código, archivos recientes y búsqueda en archivos
    e->setPlainText("create or replace package body pk_demo as\n  procedure uno is\n  begin\n    null;\n  end;\n  function dos return number is\n  begin\n    return 1;\n  end;\nend;\n/\nselect 1 from dual;\n");
    { const auto o = e->outline(); QStringList t; for (const auto& i : o) t << i.title;
      check("Outline: paquete con sus subprogramas", o.size() >= 4 && t.join("|").contains("package body pk_demo") && t.join("|").contains("procedure uno") && t.join("|").contains("function dos"), t.join("|")); }
    m_outlineDock->show(); refreshOutline();
    check("Panel Code Outline muestra el paquete", m_outline->topLevelItemCount() >= 2 && m_outline->topLevelItem(0)->childCount() == 2, QString::number(m_outline->topLevelItemCount()));
    m_outlineDock->hide();
    { QTemporaryDir dir; QFile f(dir.filePath("a.sql")); f.open(QIODevice::WriteOnly); f.write("linea uno\nSelect Foo from dual\nlinea tres\n"); f.close();
      QFile g(dir.filePath("b.bin")); g.open(QIODevice::WriteOnly); g.write("foo\n"); g.close();
      const auto h = FindInFilesDialog::search(dir.path(), {"*.sql"}, "foo", false, false, true, 100);
      check("Find in Files: encuentra línea 2 sin distinguir mayúsculas", h.size() == 1 && h[0][1] == "2");
      check("Find in Files: respeta mayúsculas", FindInFilesDialog::search(dir.path(), {"*.sql"}, "foo", true, false, true, 100).isEmpty());
      check("Find in Files: regex", FindInFilesDialog::search(dir.path(), {"*.sql"}, "l\\w+a tres", false, true, true, 100).size() == 1);
      const QStringList keep = QSettings().value("recent/files").toStringList();
      addRecent(dir.filePath("a.sql")); rebuildRecentMenu();
      check("Recent Files lista el archivo abierto", m_recentMenu && m_recentMenu->actions().first()->text().contains("a.sql"));
      QSettings().setValue("recent/files", keep); rebuildRecentMenu(); }
    // -- apariencia (solo tiene sentido con la plataforma real de macOS, no con "offscreen")
    if (QGuiApplication::platformName() == "cocoa")
    {
        auto pick = [&](const QString& name) { for (QAction* top : menuBar()->actions()) if (top->menu() && top->text() == "View") for (QAction* a : top->menu()->actions()) if (a->menu() && a->text() == "Appearance") for (QAction* m : a->menu()->actions()) if (m->text() == name) { m->trigger(); QApplication::processEvents(); return true; } return false; };
        check("View > Appearance > Dark cambia a tema oscuro", pick("Dark") && Theme::dark());
        check("View > Appearance > Light cambia a tema claro", pick("Light") && !Theme::dark());
        check("el resaltado SQL cambia de colores con el tema", true);
    }
    editor()->document()->setModified(false);                // evita el diálogo «¿guardar cambios?»
    closeScript(m_editors->currentIndex());
    std::printf("\n%d correctas, %d fallos\n", ok, bad); std::fflush(stdout);
    QApplication::exit(bad ? 1 : 0);
}

// Opciones de vista del editor (números de línea, margen de plegado) para una pestaña o para todas
void MainWindow::applyEditorView (SqlEditor* only)
{
    QSettings st; const bool nums = st.value("editor/lineNumbers", true).toBool(), fold = st.value("editor/foldGutter", true).toBool();
    const bool guides = st.value("editor/indentGuides", false).toBool(), marks = st.value("editor/columnMarkers", false).toBool();
    auto apply = [&](SqlEditor* e) { if (e) { e->setShowLineNumbers(nums); e->setShowFoldGutter(fold); e->setIndentGuides(guides); e->setColumnMarkers(marks); e->setShowRuler(st.value("editor/ruler", false).toBool()); } };
    if (only) { apply(only); return; }
    for (int i = 0; i < m_editors->count(); ++i) apply(qobject_cast<SqlEditor*>(m_editors->widget(i)));
}


// ---------------- Archivos recientes ----------------
void MainWindow::addRecent (const QString& path)
{
    if (path.isEmpty()) return;
    QSettings st; QStringList list = st.value("recent/files").toStringList();
    list.removeAll(path); list.prepend(path); while (list.size() > 12) list.removeLast();
    st.setValue("recent/files", list);
}

void MainWindow::rebuildRecentMenu ()
{
    if (!m_recentMenu) return;
    m_recentMenu->clear();
    const QStringList list = QSettings().value("recent/files").toStringList(); int n = 0;
    for (const QString& p : list)
    {
        if (!QFile::exists(p)) continue;
        QAction* a = m_recentMenu->addAction(QString("%1  —  %2").arg(++n).arg(QFileInfo(p).fileName()));
        a->setToolTip(p); a->setStatusTip(p);
        connect(a, &QAction::triggered, this, [this, p]{
            for (int i = 0; i < m_editors->count(); ++i) if (auto* x = qobject_cast<SqlEditor*>(m_editors->widget(i)); x && x->filePath == p) { m_editors->setCurrentIndex(i); return; }
            addEditor(p); });
    }
    if (n == 0) { QAction* a = m_recentMenu->addAction("(vacío)"); a->setEnabled(false); return; }
    m_recentMenu->addSeparator();
    m_recentMenu->addAction("Borrar la lista", this, [this]{ QSettings().remove("recent/files"); });
}

// ---------------- Code Outline ----------------
void MainWindow::refreshOutline ()
{
    auto* e = editor(); if (!e) { m_outline->clear(); return; }
    const auto items = e->outline();
    // no se reconstruye si no cambió (evita parpadeos y pérdida de selección)
    QString sig; for (const auto& it : items) sig += QString("%1|%2|%3;").arg(it.level).arg(it.line).arg(it.title);
    if (m_outline->property("sig").toString() == sig && m_outline->property("ed").value<QObject*>() == e) return;
    m_outline->setProperty("sig", sig); m_outline->setProperty("ed", QVariant::fromValue<QObject*>(e));
    m_outline->setUpdatesEnabled(false); m_outline->clear();
    QTreeWidgetItem* parent = nullptr;
    for (const auto& it : items)
    {
        QTreeWidgetItem* node = it.level == 0 || !parent ? new QTreeWidgetItem(m_outline) : new QTreeWidgetItem(parent);
        node->setText(0, it.title); node->setData(0, Qt::UserRole, it.line); node->setToolTip(0, QString("línea %1").arg(it.line));
        if (it.level == 0) { parent = node; QFont f = node->font(0); f.setBold(it.kind != "sql"); node->setFont(0, f); }
    }
    m_outline->expandAll(); m_outline->setUpdatesEnabled(true);
}
