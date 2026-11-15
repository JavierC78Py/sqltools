#include "MainWindow.h"
#include "ObjectListDefs.h"
#include "DdlOptions.h"
#include "HistoryStore.h"
#include <QPushButton>
#include "ExportDialog.h"
#include "SettingsDialog.h"
#include "Theme.h"
#include "ConnectionManager.h"
#include "CommandMenu.h"
#include "FindReplaceDialog.h"
#include "ConnectionStore.h"
#include "DbWorker.h"
#include "ResultModel.h"
#include "SqlHighlighter.h"
#include "SqlSplitter.h"
#include "SqlEditor.h"
#include "ObjectBrowser.h"
#include "CompletionData.h"
#include "Snippets.h"
#include <QDockWidget>
#include <QCloseEvent>
#include <QMenuBar>
#include <QTableWidget>
#include <QFileInfo>
#include <QTabWidget>
#include <QElapsedTimer>
#include <functional>
#include <memory>
#include <QTextEdit>
#include <QTextBlock>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QStyleHints>
#include <QGuiApplication>
#include <QTextStream>

MainWindow::MainWindow ()
{
    setWindowTitle("SQLTools Mac");
    resize(1100, 720);

    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont); mono.setPointSize(13);
    m_editors = new QTabWidget();
    m_editors->setTabsClosable(true); m_editors->setMovable(true); m_editors->setDocumentMode(true);
    connect(m_editors, &QTabWidget::tabCloseRequested, this, [this](int i){ closeScript(i); });

    m_model = new ResultModel(this);
    m_table = new QTableView(); m_table->setStyleSheet(Theme::modernCss(true));
    m_table->setModel(m_model);
    m_table->setFont(mono);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setWordWrap(false);
    m_table->setTextElideMode(Qt::ElideRight);
    m_table->verticalHeader()->setDefaultSectionSize(QFontMetrics(mono).height() + 6);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);   // filas de alto fijo: mucho más rápido con miles de filas
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionsMovable(true);

    m_output = new QPlainTextEdit();
    m_output->setReadOnly(true); m_output->setFont(mono); m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_output->setPlaceholderText("Aquí aparece la salida de DBMS_OUTPUT.PUT_LINE y los avisos de los scripts.");
    m_errors = new QTableWidget(0, 4);
    m_errors->setHorizontalHeaderLabels({"Tipo", "Línea", "Col.", "Descripción"});
    m_errors->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_errors->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_errors->setFont(mono);
    m_errors->verticalHeader()->setVisible(false);
    m_errors->horizontalHeader()->setStretchLastSection(true);
    m_errors->setColumnWidth(0, 90); m_errors->setColumnWidth(1, 70); m_errors->setColumnWidth(2, 60);
    connect(m_errors, &QTableWidget::cellDoubleClicked, this, [this](int row, int){
        if (auto* e = editor()) e->goToLine(m_errors->item(row, 1)->text().toInt(), m_errors->item(row, 2)->text().toInt()); });
    setupResultsPanel();                                     // m_tabs (páginas) + barra de iconos del panel de resultados
    m_split = new QSplitter(Qt::Vertical);
    m_editorSplit = new QSplitter(Qt::Vertical);                 // editor + (opcional) segunda vista del mismo script (Window > Split)
    m_view2 = new QPlainTextEdit(); m_view2->setVisible(false); m_view2->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_editorSplit->addWidget(m_editors); m_editorSplit->addWidget(m_view2); m_editorSplit->setStretchFactor(0, 3); m_editorSplit->setStretchFactor(1, 2);
    connect(m_editors, &QTabWidget::currentChanged, this, [this]{ if (m_view2->isVisible()) syncSplitView(); updateBookmarkActions(); });
    m_split->addWidget(m_editorSplit); m_split->addWidget(m_resultsHost);
    m_split->setSizes({800, 200});                                  // al abrir, el panel de resultados ocupa ~1/5 del alto (como en Windows)
    m_split->setStretchFactor(0, 4); m_split->setStretchFactor(1, 1);
    setCentralWidget(m_split);

    m_meta = new CompletionData();
    m_browser = new ObjectBrowser();
    auto* dock = new QDockWidget("Objetos", this);
    dock->setWidget(m_browser); dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
    dock->setMinimumWidth(240);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    m_dock = dock;

    m_status = new QLabel("Sin conexión"); m_conn = new QLabel();
    statusBar()->addWidget(m_status, 1); statusBar()->addPermanentWidget(m_conn);

    // Hilo de base de datos
    m_worker = new DbWorker();
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    qRegisterMetaType<QVector<QStringList>>("QVector<QStringList>");
    qRegisterMetaType<QVector<int>>("QVector<int>");
    m_bgWorker = new DbWorker(); m_bgWorker->moveToThread(&m_bgThread); connect(&m_bgThread, &QThread::finished, m_bgWorker, &QObject::deleteLater); m_bgThread.start();
    connect(m_bgWorker, &DbWorker::connected, this, [this]{ m_bgReady = true; });
    connect(m_bgWorker, &DbWorker::failed, this, [this]{ m_bgReady = false; });           // sin segunda conexión se usa la principal, sin avisos
    connect(m_worker, &DbWorker::connected,    this, &MainWindow::onConnected);
    connect(m_worker, &DbWorker::disconnected, this, &MainWindow::onDisconnected);
    connect(m_worker, &DbWorker::columnsReady, this, &MainWindow::onColumns);
    connect(m_worker, &DbWorker::rowsReady,    this, &MainWindow::onRows);
    connect(m_worker, &DbWorker::outputReady,  this, &MainWindow::onOutput);
    connect(m_worker, &DbWorker::messageReady, this, &MainWindow::onMessage);
    connect(m_worker, &DbWorker::finished,     this, &MainWindow::onFinished);
    connect(m_worker, &DbWorker::failed,       this, &MainWindow::onFailed);
    connect(m_worker, &DbWorker::compileErrors, this, &MainWindow::onCompileErrors);
    // Explorador de objetos
    connect(m_worker, &DbWorker::schemasLoaded, this, [this](const QStringList& names, const QString& current){
        m_browser->setSchemas(names, current); m_meta->schemas = names; m_meta->currentSchema = current; });
    for (DbWorker* w : {m_worker, m_bgWorker}) connect(w, &DbWorker::userObjectsLoaded, this, [](const QStringList& n){ SqlHighlighter::setUserObjects(QSet<QString>(n.begin(), n.end())); });
    connect(m_worker, &DbWorker::objectsLoaded, this, [this](const QString& owner, const QVector<QStringList>& rows){
        if (owner == m_browser->owner() || m_browser->owner().isEmpty()) m_browser->setObjects(rows);
        QStringList tables, routines, others; QSet<QString> packages;
        QSettings acs; auto allow = [&](const char* k) { return acs.value(QString("ac/") + k, true).toBool(); };      // Settings > Autocompletado: tipos de objeto que se ofrecen
        for (const auto& r : rows)
        {
            if (r[0] == "TABLE") { if (allow("table")) tables << r[1]; } else if (r[0] == "VIEW" || r[0] == "MATERIALIZED VIEW") { if (allow("view")) tables << r[1]; } else if (r[0] == "SYNONYM") { if (allow("synonym")) tables << r[1]; }
            else if (r[0] == "PROCEDURE" || r[0] == "FUNCTION" || r[0] == "PACKAGE") { if (allow(r[0] == "PACKAGE" ? "package" : r[0] == "FUNCTION" ? "function" : "procedure")) routines << r[1]; if (r[0] == "PACKAGE") packages.insert(owner == m_meta->currentSchema ? r[1].toUpper() : owner + "." + r[1].toUpper()); }
            else if (r[0] == "SEQUENCE") { if (allow("sequence")) others << r[1]; } else if (r[0] == "TYPE") { if (allow("type")) others << r[1]; }
        }
        if (owner == m_meta->currentSchema || m_meta->currentSchema.isEmpty())
        {
            m_meta->tables = tables + (QSettings().value("ac/synonym", true).toBool() ? m_synonymNames : QStringList()); m_meta->routines = routines; m_meta->others = others;
            m_meta->packages.unite(packages);
        }
        else { m_meta->schemaTables[owner] = tables; m_meta->schemaRoutines[owner] = routines; m_meta->packages.unite(packages); }
        if (auto* e = editor()) e->refreshCompletion();          // quizá el usuario esperaba los objetos de este esquema
    });
    connect(m_worker, &DbWorker::synonymsLoaded, this, [this](const QVector<QStringList>& rows){
        m_synonymNames.clear();
        for (const auto& r : rows) if (!m_synonymNames.contains(r[0])) m_synonymNames << r[0];
        if (QSettings().value("ac/synonym", true).toBool()) for (const QString& n : m_synonymNames) if (!m_meta->tables.contains(n)) m_meta->tables << n;
    });
    connect(m_worker, &DbWorker::columnsLoaded, this, [this](const QString& owner, const QString& name, const QVector<QStringList>& rows){
        QStringList names; for (const auto& r : rows) names << r[0];
        const QString key = (owner == m_meta->currentSchema ? name : owner + "." + name).toUpper();
        m_meta->members.insert(key, names);
        if (owner == m_browser->owner()) m_browser->setColumns(name, rows);
        if (auto* e = editor()) e->refreshCompletion();          // si el usuario estaba esperando esta lista, se abre ahora
    });
    connect(m_worker, &DbWorker::metadataFailed, this, [this](const QString& what, const QString& msg){ m_status->setText("No se pudo leer " + what + ": " + msg.section('\n', 0, 0)); });
    connect(m_worker, &DbWorker::sourceTiming, this, [this](int ms){ m_srcDbMs = ms; });
    connect(m_worker, &DbWorker::sourceReady, this, [this](const QString& type, const QString& name, const QString& text){
        QElapsedTimer tm; tm.start();
        auto* e = blankEditor(); e->titleHint = name; e->setPlainText(text); e->document()->setModified(false); updateEditorTitle(e);
        e->moveCursor(QTextCursor::Start);
        const auto sec = [](qint64 ms){ return QLocale().toString(ms / 1000.0, 'f', 2) + " s"; };
        m_status->setText(type + " " + name + " abierto en una pestaña nueva" + (m_srcDbMs >= 0 ? QString(" (base de datos: %1 · editor: %2)").arg(sec(m_srcDbMs), sec(tm.elapsed())) : QString()) + "."); m_srcDbMs = -1; });
    connect(m_worker, &DbWorker::schemaDdlProgress, this, [this](int d, int t){ m_status->setText(QString("Extrayendo DDL… %1 de %2").arg(d).arg(t)); });
    connect(m_worker, &DbWorker::schemaDdlReady, this, [this](const QString& owner, const QString& text, int ok, int bad){
        auto* e = blankEditor(); e->titleHint = "DDL_" + owner; e->setPlainText(text); e->document()->setModified(false); updateEditorTitle(e);
        e->moveCursor(QTextCursor::Start); m_status->setText(QString("DDL de %1: %2 objetos extraídos%3.").arg(owner).arg(ok).arg(bad ? QString(", %1 sin DDL (ver comentarios)").arg(bad) : QString())); });
    connect(m_browser, &ObjectBrowser::actionRequested, this, &MainWindow::onBrowserAction);
    connect(m_browser, &ObjectBrowser::listRequested, this, [this](const QString& owner, const QString& key, const QString& sql){
        int cols = 1; for (const auto& d : objectListDefs()) if (d.key == key) cols = int(d.headers.size());
        QMetaObject::invokeMethod(listWorker(), "loadObjectList", Qt::QueuedConnection, Q_ARG(QString, owner), Q_ARG(QString, key), Q_ARG(QString, sql), Q_ARG(int, cols)); });
    for (DbWorker* w : {m_worker, m_bgWorker}) connect(w, &DbWorker::objectListLoaded, this, [this](const QString& owner, const QString& key, const QVector<QStringList>& rows, const QString& error){ if (owner == m_browser->owner()) m_browser->setListRows(key, rows, error); });
    connect(m_browser, &ObjectBrowser::refreshRequested, this, [this]{ if (m_connected) QMetaObject::invokeMethod(m_worker, "loadObjectsOf", Qt::QueuedConnection, Q_ARG(QString, m_browser->owner())); });
    connect(m_browser, &ObjectBrowser::schemaChanged, this, [this](const QString& o){ if (m_connected) QMetaObject::invokeMethod(m_worker, "loadObjectsOf", Qt::QueuedConnection, Q_ARG(QString, o)); });
    connect(m_browser, &ObjectBrowser::columnsRequested, this, [this](const QString& t){ QMetaObject::invokeMethod(m_worker, "loadColumns", Qt::QueuedConnection, Q_ARG(QString, m_browser->owner()), Q_ARG(QString, t)); });
    connect(m_browser, &ObjectBrowser::openSourceRequested, this, [this](const QString& type, const QString& name){ loadDdl({type + QChar(0x1f) + name}, false, false); });      // doble clic: Load DDL con las opciones guardadas, sin preguntar
    connect(m_browser, &ObjectBrowser::selectAllRequested, this, [this](const QString& name){
        if (!m_connected || m_busy) return;
        auto* e = blankEditor(); e->titleHint = name; e->setPlainText("select * from " + (m_browser->owner() == m_meta->currentSchema ? QString() : m_browser->owner() + ".") + name + " fetch first 200 rows only;"); e->document()->setModified(false); updateEditorTitle(e);
        e->moveCursor(QTextCursor::Start); runCurrent(); });
    connect(m_worker, &DbWorker::transactionState, this, [this](bool pending){ m_dirty = pending; if (m_status && !pending) return; if (pending) m_status->setText("Hay cambios sin confirmar (Commit o Rollback)."); });
    setupCommands();
    applyExecOptions();
    m_thread.start();
    restoreSession();
    setBusy(false);
}

MainWindow::~MainWindow ()
{
    m_worker->cancel();
    QMetaObject::invokeMethod(m_worker, "disconnectDb", Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(m_bgWorker, "disconnectDb", Qt::BlockingQueuedConnection);
    m_thread.quit(); m_thread.wait(5000); m_bgThread.quit(); m_bgThread.wait(5000);
}

void MainWindow::setBusy (bool busy)
{
    m_busy = busy;
    if (m_menu) { m_menu->update(m_connected, busy); updateBookmarkActions(); if (auto* a = m_menu->action("ID_GRID_OUTPUT_EXPORT")) a->setEnabled(m_model->rowCount() > 0 && !busy); }
}

// Como en Windows: Siguiente/Anterior marcador, Quitar todos y las órdenes de líneas marcadas solo están activos si hay marcadores
void MainWindow::updateBookmarkActions ()
{
    if (!m_menu) return;
    auto* e = editor(); const bool any = e && !e->bookmarkedLines().isEmpty();
    for (const char* id : {"ID_EDIT_BKM_NEXT", "ID_EDIT_BKM_PREV", "ID_EDIT_BKM_REMOVE_ALL", "ID_EDIT_COPY_BOOKMARKED", "ID_EDIT_CUT_BOOKMARKED", "ID_EDIT_DELETE_BOOKMARKED"})
        if (auto* a = m_menu->action(id)) a->setEnabled(any);
    for (int n = 0; n <= 9; ++n) if (auto* a = m_menu->action(QString("ID_EDIT_BKM_GET_%1").arg(n).toUtf8().constData())) a->setEnabled(e && e->numberedBookmarkLine(n) > 0);
}

void MainWindow::startConnect (const QVariantMap& params)
{
    m_connParams = params;
    QMetaObject::invokeMethod(m_worker, "connectDb", Qt::QueuedConnection, Q_ARG(QVariantMap, params));
}

void MainWindow::showConnectDialog ()
{
    QSettings settings; ConnectionStore store(&settings);
    // Primera vez: si había una conexión de la versión anterior (usuario/alias/carpeta), se convierte en la primera guardada.
    if (store.all().isEmpty() && !settings.value("conn/user").toString().isEmpty())
    {
        ConnectionProfile p; p.id = ConnectionStore::newId(); p.tag = "Principal"; p.user = settings.value("conn/user").toString();
        p.alias = settings.value("conn/alias").toString(); p.tnsAdmin = settings.value("conn/tns").toString(); store.save(p);
    }
    if (m_connected && !confirmPending()) return;
    ConnectionManager dlg(&store, this);
    // "Probar": la prueba corre en el hilo de la base de datos y el resultado vuelve al diálogo
    connect(&dlg, &ConnectionManager::testRequested, this, [this](const QVariantMap& p){ QMetaObject::invokeMethod(m_worker, "testConnection", Qt::QueuedConnection, Q_ARG(QVariantMap, p)); });
    auto link = connect(m_worker, &DbWorker::testResult, &dlg, &ConnectionManager::showTestResult);
    const int result = dlg.exec();
    disconnect(link);
    if (result != QDialog::Accepted) return;

    m_pendingProfileId = dlg.profileId(); m_pendingTag = dlg.tag();
    m_status->setText("Conectando…"); setBusy(true);
    { const QVariantMap cp = dlg.connectParams(); m_lastUser = cp.value("user").toString(); m_lastAlias = cp.value("alias").toString(); }
    startConnect(dlg.connectParams());
}

// Solo para pruebas sin pantalla: parámetros de conexión desde variables SQLT_*
QVariantMap MainWindow::paramsFromEnvironment () const
{
    return { {"user", qEnvironmentVariable("SQLT_USER")}, {"password", qEnvironmentVariable("SQLT_PASS")}, {"alias", qEnvironmentVariable("SQLT_ALIAS")},
             {"tnsAdmin", qEnvironmentVariable("SQLT_TNS_ADMIN")}, {"mode", qEnvironmentVariable("SQLT_MODE", "Normal")}, {"readOnly", qEnvironmentVariableIntValue("SQLT_READONLY") != 0},
             {"bypass", false} };
}

void MainWindow::onConnected (const QString& version, const QString& who)
{
    m_connected = true; m_browser->setConnected(true);
    if (!m_pendingProfileId.isEmpty()) { QSettings settings; ConnectionStore(&settings).touch(m_pendingProfileId); }      // +1 al contador de uso
    m_conn->setText((m_pendingTag.isEmpty() ? QString() : m_pendingTag + "  |  ") + who + "  |  Oracle " + version);
    if (m_connLabel) m_connLabel->setText((m_pendingTag.isEmpty() ? QString() : m_pendingTag + "  ") + who);
    setWindowTitle("SQLTools Mac — " + (m_pendingTag.isEmpty() ? who : m_pendingTag + " " + who));
    m_pendingProfileId.clear(); m_pendingTag.clear();
    m_status->setText("Conectado."); setBusy(false);
    applyGridOptions();
    QMetaObject::invokeMethod(m_worker, "loadSchemas", Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_worker, "loadUserObjects", Qt::QueuedConnection);
    m_bgReady = false; QMetaObject::invokeMethod(m_bgWorker, "connectDb", Qt::QueuedConnection, Q_ARG(QVariantMap, m_connParams));      // segunda sesión para las listas
    QMetaObject::invokeMethod(m_worker, "loadObjectsOf", Qt::QueuedConnection, Q_ARG(QString, QString()));
    QMetaObject::invokeMethod(m_worker, "loadSynonyms", Qt::QueuedConnection);
    if (auto* e = editor()) e->setFocus();
}
void MainWindow::onDisconnected () { m_bgReady = false; if (m_bgWorker) QMetaObject::invokeMethod(m_bgWorker, "disconnectDb", Qt::QueuedConnection); SqlHighlighter::setUserObjects({}); m_browser->setConnected(false); if (m_connLabel) m_connLabel->setText("Not connected"); setWindowTitle("SQLTools Mac"); m_browser->clear(); m_connected = false; m_conn->clear(); m_status->setText("Sin conexión"); setBusy(false); }

void MainWindow::startRun (const QStringList& stmtsIn, const QVector<int>& lines)
{
    QStringList stmts = applyBinds(stmtsIn);
    if (!applySubstitution(stmts)) return;
    m_lastStatements = stmts; clearRunResults();
    m_model->reset(); m_firstBatch = true;
    m_output->clear(); setPageBadge(1, QString()); showPage(0);
    m_errors->setRowCount(0); setPageBadge(2, QString());
    m_status->setText("Ejecutando…"); setBusy(true);
    QMetaObject::invokeMethod(m_worker, "executeScript", Qt::QueuedConnection, Q_ARG(QStringList, stmts), Q_ARG(QVector<int>, lines));
}

// Sentencias de una selección o del texto completo, con la línea donde empieza cada una (para ubicar errores).
static void statementsOf (const QString& text, int firstLine, QStringList& stmts, QVector<int>& lines)
{
    for (const SqlStatement& st : SqlSplitter::split(text)) { stmts << st.text; lines << (st.startLine + firstLine - 1); }
}

void MainWindow::runCurrent () { runBlock(QSettings().value("exec/style", 0).toInt() == 1); }

// Execute Current: la sentencia (delimitada por ; y /) o el bloque entre líneas en blanco (estilo TOAD)
void MainWindow::runBlock (bool byBlankLines)
{
    if (!m_connected || m_busy) return;
    auto* e = editor(); if (!e) return;
    QTextCursor c = e->textCursor(); QStringList stmts; QVector<int> lines;
    if (c.hasSelection())
    {
        const QString sel = c.selectedText().replace(QChar(0x2029), '\n');
        stmts << sel; lines << (e->document()->findBlock(c.selectionStart()).blockNumber() + 1);
    }
    else if (byBlankLines)
    {
        QTextBlock b = c.block(); if (b.text().trimmed().isEmpty()) { m_status->setText("El cursor está en una línea en blanco."); return; }
        QTextBlock first = b, last = b;
        while (first.previous().isValid() && !first.previous().text().trimmed().isEmpty()) first = first.previous();
        while (last.next().isValid() && !last.next().text().trimmed().isEmpty()) last = last.next();
        QTextCursor r(first); r.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
        QString t = r.selectedText().replace(QChar(0x2029), '\n').trimmed();
        if (t.endsWith(';') && !t.contains(QRegularExpression("\\bend\\s*;\\s*$", QRegularExpression::CaseInsensitiveOption))) t.chop(1);
        stmts << t; lines << first.blockNumber() + 1;
    }
    else
    {
        const auto v = SqlSplitter::split(e->toPlainText());
        const int k = SqlSplitter::statementAt(v, c.position());
        if (k < 0) { m_status->setText("No hay ninguna sentencia en la posición del cursor."); return; }
        stmts << v[k].text; lines << v[k].startLine;
    }
    if (stmts.first().trimmed().isEmpty()) return;
    startRun(stmts, lines);
}

void MainWindow::runScript ()
{
    if (!m_connected || m_busy) return;
    QTextCursor c = editor()->textCursor();
    QStringList stmts; QVector<int> lines;
    if (c.hasSelection()) statementsOf(c.selectedText().replace(QChar(0x2029), '\n'), editor()->document()->findBlock(c.selectionStart()).blockNumber() + 1, stmts, lines);
    else statementsOf(editor()->toPlainText(), 1, stmts, lines);
    if (stmts.isEmpty()) { m_status->setText("No hay nada que ejecutar."); return; }
    startRun(stmts, lines);
}

void MainWindow::onOutput (const QString& text)
{
    m_output->moveCursor(QTextCursor::End);
    if (!m_output->document()->isEmpty() && !m_output->toPlainText().endsWith('\n')) m_output->insertPlainText("\n");
    m_output->insertPlainText(text);
    setPageBadge(1, "●");
    if (m_model->rowCount() == 0) showPage(1);        // sin filas que mostrar: lo útil es la salida
}
void MainWindow::onMessage (const QString& line) { m_output->appendPlainText("-- " + line); }

void MainWindow::cancelQuery () { if (m_busy) { m_status->setText("Cancelando…"); m_worker->cancel(); } }

void MainWindow::onColumns (const QStringList& names, const QVector<int>& right)
{
    m_model->reset(names, right);
    for (int i = 0; i < names.size(); ++i) m_table->setColumnWidth(i, qMax(90, int(m_table->fontMetrics().horizontalAdvance(names[i])) + 30));
}
void MainWindow::onRows (const QVector<QStringList>& rows)
{
    m_model->append(rows);
    if (m_firstBatch)                       // ajustar anchos con la primera tanda, con tope
    {
        m_firstBatch = false;
        const int n = m_model->columnCount();
        for (int c = 0; c < n; ++c)
        {
            int w = m_table->fontMetrics().horizontalAdvance(m_model->columns()[c]) + 30;
            for (int r = 0; r < qMin(rows.size(), 200); ++r) w = qMax(w, int(m_table->fontMetrics().horizontalAdvance(rows[r].at(c).left(80))) + 24);
            m_table->setColumnWidth(c, qMin(w, 420));
        }
    }
    m_status->setText(QString("Recibiendo… %1 filas").arg(m_model->rowCount()));
}
void MainWindow::onFinished (qint64 rows, double seconds, const QString& message)
{
    setBusy(false);
    m_lastRows = rows; m_lastSeconds = seconds; showStats(m_statsExtra.isEmpty() ? QVector<QStringList>() : m_statsExtra);
    if (!m_shotPath.isEmpty()) qInfo().noquote() << "hilo BD:" << rows << "filas en" << seconds << "s | filas ya en la rejilla:" << m_model->rowCount();
    m_status->setText(message.isEmpty() ? QString("%1 fila(s) en %2 s").arg(rows).arg(seconds, 0, 'f', 2) : message + QString("  (%1 s)").arg(seconds, 0, 'f', 2));
    if (!m_shotPath.isEmpty()) QTimer::singleShot(300, this, &MainWindow::saveScreenshotAndQuit);
}
void MainWindow::onFailed (const QString& message)
{
    const bool wasConnecting = !m_connected;
    setBusy(false);
    const QString firstLine = message.section('\n', 0, 0).left(200);
    const QString lastLine = message.section('\n', -1).trimmed();
    m_status->setText("Error: " + firstLine + (lastLine.startsWith("(sentencia") ? "   " + lastLine : QString()));
    m_output->appendPlainText("ERROR: " + message.trimmed());
    setPageBadge(1, "●");
    if (m_model->rowCount() == 0) showPage(1);
    if (!m_shotPath.isEmpty()) { QTimer::singleShot(300, this, &MainWindow::saveScreenshotAndQuit); return; }
    QMessageBox::warning(this, wasConnecting ? "No se pudo conectar" : "Error de Oracle", message);
}

void MainWindow::copySelection (bool withHeaders)
{
    auto sel = m_table->selectionModel()->selectedIndexes();
    if (sel.isEmpty()) return;
    std::sort(sel.begin(), sel.end(), [](const QModelIndex& a, const QModelIndex& b){ return a.row() != b.row() ? a.row() < b.row() : a.column() < b.column(); });
    QString out; int lastRow = sel.first().row(), firstCol = sel.first().column();
    if (withHeaders) { for (const auto& i : sel) { if (i.row() != sel.first().row()) break; out += m_model->columns().at(i.column()) + '\t'; } out.chop(1); out += '\n'; }
    for (const auto& i : sel)
    {
        if (i.row() != lastRow) { out += '\n'; lastRow = i.row(); } else if (i.column() != firstCol || &i != &sel.first()) { if (&i != &sel.first() && i.row() == lastRow) out += '\t'; }
        out += m_model->rows()[i.row()].at(i.column());
    }
    QApplication::clipboard()->setText(out);
}

// Exportar a archivo: diálogo con formato y opciones (parte de los ajustes de Settings > Exportación)
void MainWindow::exportCsv ()
{
    if (m_model->columnCount() == 0) return;
    ExportDialog dlg(ExportOptions::load(ExportOptions::File), this); if (dlg.exec() != QDialog::Accepted) return;
    ExportOptions o = dlg.options(); o.save(ExportOptions::File);
    const QString path = QFileDialog::getSaveFileName(this, "Exportar resultados", QDir::homePath() + "/" + o.fileName() + "." + ExportOptions::extension(o.format), ExportOptions::formatName(o.format) + " (*." + ExportOptions::extension(o.format) + ")");
    if (path.isEmpty()) return;
    QFile f(path); if (!f.open(QIODevice::WriteOnly)) { QMessageBox::warning(this, "Exportar", "No se pudo escribir el archivo."); return; }
    if (o.bom || o.format == ExportOptions::Csv && o.bom) f.write("\xEF\xBB\xBF");
    f.write(exportText(o).toUtf8()); f.close();
    m_status->setText(QString("Exportadas %1 filas a %2 (%3).").arg(m_model->rowCount()).arg(path, ExportOptions::formatName(o.format)));
}

QString MainWindow::exportText (const ExportOptions& o) const { return ResultExporter::format(m_model->columns(), m_model->rightAligned(), m_model->rows(), o); }

// ---- Solo para pruebas sin pantalla ----------------------------------------------------------
void MainWindow::applyAppearance (const QString& mode)
{
    auto* h = QGuiApplication::styleHints();
    if (mode == "dark") h->setColorScheme(Qt::ColorScheme::Dark);
    else if (mode == "system") h->unsetColorScheme();
    else h->setColorScheme(Qt::ColorScheme::Light);
}

// Los iconos originales de SQLTools se diseñaron sobre gris claro: en el tema oscuro las barras de iconos conservan un fondo gris claro para que se vean bien.
void MainWindow::applyTheme ()
{
    setStyleSheet("QMainWindow::separator { width: 7px; height: 7px; background: transparent; } QMainWindow::separator:hover { background: rgba(60,120,255,90); }");   // borde de los paneles fácil de agarrar para cambiar su ancho
    const bool dark = Theme::dark();
    const QString checked = "QToolButton:checked { background: rgba(60,120,255,70); border: 1px solid rgba(60,120,255,170); border-radius: 3px; } ";
    const QString bar = dark ? "QToolBar { background: #d2d2d2; spacing: 2px; border: none; } " + checked
                             : "QToolBar { spacing: 2px; } " + checked;
    for (QToolBar* tb : findChildren<QToolBar*>()) tb->setStyleSheet(bar);
    if (m_connLabel) m_connLabel->setStyleSheet("QLabel { color: #202020; background: #ffffff; border: 1px solid #909090; padding: 2px 8px; }");
}

QStringList MainWindow::dumpCommands () const { return m_menu ? m_menu->dump() : QStringList(); }

void MainWindow::autoStartFromEnvironment ()
{
    if (qEnvironmentVariable("SQLT_SELFTEST") == "commands") { QTimer::singleShot(200, this, &MainWindow::selfTestCommands); return; }
    if (qEnvironmentVariableIsSet("SQLT_MANUAL_SHOTS")) { QTimer::singleShot(300, this, [this]{ generateManualShots(qEnvironmentVariable("SQLT_MANUAL_SHOTS")); }); return; }
    if (qEnvironmentVariableIsSet("SQLT_OPEN") && qEnvironmentVariableIsEmpty("SQLT_USER"))   // solo abrir archivos y capturar (sin base de datos)
    {
        for (const QString& f : qEnvironmentVariable("SQLT_OPEN").split(',', Qt::SkipEmptyParts)) addEditor(f);
        if (qEnvironmentVariableIsSet("SQLT_FOLD_DEMO") && editor())                         // demostración del margen: marcadores y un bloque plegado
            QTimer::singleShot(100, this, [this]{ auto* e = editor(); e->refreshFolds();
                e->setTextCursor(QTextCursor(e->document()->findBlockByNumber(8))); e->toggleBookmark();
                e->setTextCursor(QTextCursor(e->document()->findBlockByNumber(21))); e->setNumberedBookmark(3);
                e->setTextCursor(QTextCursor(e->document()->findBlockByNumber(13))); e->foldCurrent(0); });
        if (qEnvironmentVariableIsSet("SQLT_COLUMN_DEMO") && editor())                       // demostración de la selección en columna, regla, guías y marcas
            QTimer::singleShot(100, this, [this]{ auto* e = editor(); e->setShowRuler(true); e->setIndentGuides(true); e->setColumnMarkers(true);
                QTextBlock b = e->document()->findBlockByNumber(13); QTextCursor c(b); c.setPosition(b.position() + 6); e->setTextCursor(c); e->enterColumnMode(); e->columnExtend(6, 9); });
        if (qEnvironmentVariableIsSet("SQLT_PANELS_DEMO")) QTimer::singleShot(100, this, [this]{ for (const char* id : {"ID_VIEW_FILE_PANEL", "ID_VIEW_OPEN_FILES", "ID_VIEW_PROPERTIES"}) m_menu->action(id)->trigger(); });
        if (qEnvironmentVariableIsSet("SQLT_SETTINGS_DEMO")) QTimer::singleShot(100, this, [this]{ SettingsDialog d(this, qEnvironmentVariableIntValue("SQLT_SETTINGS_TAB")); d.show(); d.grab().save(qEnvironmentVariable("SQLT_SETTINGS_DEMO")); });
        m_shotPath = qEnvironmentVariable("SQLT_SCREENSHOT");
        if (!m_shotPath.isEmpty()) QTimer::singleShot(500, this, &MainWindow::saveScreenshotAndQuit);
        return;
    }
    const QByteArray u = qgetenv("SQLT_USER"), p = qgetenv("SQLT_PASS"), a = qgetenv("SQLT_ALIAS"), t = qgetenv("SQLT_TNS_ADMIN");
    if (u.isEmpty() || p.isEmpty() || a.isEmpty()) return;
    m_shotPath = qEnvironmentVariable("SQLT_SCREENSHOT");
    if (qEnvironmentVariableIsSet("SQLT_TEST_CONN"))             // prueba del botón "Probar" (conexión aparte en el hilo de BD)
    {
        connect(m_worker, &DbWorker::testResult, this, [this](bool ok, const QString& msg){
            { QString one = msg; qInfo().noquote() << (ok ? "[OK]" : "[FALLO]") << one.replace('\n', " | "); } QApplication::quit(); });
        QVariantMap p = paramsFromEnvironment();
        if (qEnvironmentVariableIsSet("SQLT_BAD_ALIAS")) p["alias"] = qEnvironmentVariable("SQLT_BAD_ALIAS");
        QMetaObject::invokeMethod(m_worker, "testConnection", Qt::QueuedConnection, Q_ARG(QVariantMap, p));
        return;
    }
    if (qEnvironmentVariableIsSet("SQLT_COMPLETE_TEST"))         // prueba del autocompletado contra el esquema real
    {
        struct Case { QString text; QString what; };
        auto* cases = new QVector<Case>{
            { "select e.| from PARAMETROS_JN e", "columnas por alias (tabla en el FROM, alias tras el cursor)" },
            { "select * from PARAM|", "tablas tras FROM con prefijo PARAM" },
            { "begin\n  PKG_WA_API.|\nend;", "miembros de un paquete" },
            { "select sysd|", "palabra clave y funciones" },
            { "select * from PARAMETROS_JN p join PAIS d on p.ID_PARAMETRO = d.|", "columnas del segundo alias de un JOIN" },
            { "select * from RMAN$CATALOG.|", "objetos de OTRO esquema (se cargan a demanda)" },
            { "begin\n  RMAN$CATALOG.DBMS_RCVMAN.|\nend;", "miembros de un paquete de otro esquema" },
            { "select * from |", "tras FROM: tablas, sinónimos y esquemas" },
            { "select s.| from AWS$ARN s", "sinónimo público sin privilegios sobre su destino (debe quedar vacío)" },
            { "select d.| from dual d", "DUAL: sinónimo público hacia SYS.DUAL" },
            { "select u.| from user_tables u", "vista del diccionario USER_TABLES (sinónimo público, no está en la lista propia)" },
            { "select x.| from (select p.id_parametro, p.nombre from PARAMETROS_JN p) x", "subconsulta con columnas calificadas" },
        };
        auto* idx = new int(0);
        auto step = std::make_shared<std::function<void()>>();
        *step = [this, cases, idx, step]{
            if (*idx >= cases->size()) { QTimer::singleShot(100, this, &MainWindow::saveScreenshotAndQuit); return; }
            const Case c = (*cases)[*idx]; QString t = c.text; t.replace("\\n", "\n"); const int p = t.indexOf('|'); t.remove(p, 1);
            auto* e = blankEditor(); e->setPlainText(t); QTextCursor tc = e->textCursor(); tc.setPosition(p); e->setTextCursor(tc);
            auto* clock = new QElapsedTimer(); clock->start();
            e->showCompletion(true);                                     // primera vez: puede pedir columnas al servidor
            QTimer::singleShot(2500, this, [this, e, c, idx, step, clock]{
                e->showCompletion(true);                                 // segunda vez: ya con la caché cargada
                const QStringList k = e->candidates();
                qInfo().noquote() << QString("[%1] %2 -> %3 candidatos: %4%5").arg(*idx + 1).arg(c.what).arg(k.size()).arg(k.mid(0, 8).join(", ")).arg(k.size() > 8 ? ", …" : "");
                ++*idx; (*step)(); });
        };
        connect(m_worker, &DbWorker::objectsLoaded, this, [step]{ (*step)(); }, Qt::SingleShotConnection);
        startConnect(paramsFromEnvironment());
        return;
    }
    if (qEnvironmentVariableIsSet("SQLT_BROWSER_TEST"))          // prueba del explorador: lista objetos, expande uno y abre su código
    {
        connect(m_worker, &DbWorker::objectsLoaded, this, [this](const QString&, const QVector<QStringList>&){
            qInfo().noquote() << "objetos:" << m_browser->summary();
            const QString spec = qEnvironmentVariable("SQLT_BROWSER_TEST");               // "TIPO:NOMBRE"
            const QString type = spec.section(':', 0, 0), name = spec.section(':', 1);
            if (!name.isEmpty()) {
                m_browser->expandObject(name);
                if (type == "TABLE" || type == "VIEW") QMetaObject::invokeMethod(m_worker, "loadColumns", Qt::QueuedConnection, Q_ARG(QString, QString()), Q_ARG(QString, name));
                QMetaObject::invokeMethod(m_worker, "loadSource", Qt::QueuedConnection, Q_ARG(QString, QString()), Q_ARG(QString, type), Q_ARG(QString, name));
            } else QTimer::singleShot(300, this, &MainWindow::saveScreenshotAndQuit);
        }, Qt::SingleShotConnection);
        connect(m_worker, &DbWorker::sourceReady, this, [this](const QString&, const QString&, const QString& text){
            qInfo().noquote() << "código (" << text.size() << "caracteres):" << text.left(160).replace('\n', "⏎");
            QTimer::singleShot(600, this, &MainWindow::saveScreenshotAndQuit); }, Qt::SingleShotConnection);
        connect(m_worker, &DbWorker::failed, this, [this](const QString& m){ qInfo().noquote() << "falló:" << m; QTimer::singleShot(100, this, &MainWindow::saveScreenshotAndQuit); });
        startConnect(paramsFromEnvironment());
        return;
    }
    const QString sql = qEnvironmentVariable("SQLT_AUTORUN");
    const QString action = qEnvironmentVariable("SQLT_ACTION");          // + quickq | quickc | fromcursor | step | script                    // plan | stats | binds | rotate | pin  (pruebas del panel de resultados)
    if (action == "plan") connect(m_worker, &DbWorker::planReady, this, [this](const QString& t){ qInfo().noquote() << "plan:" << t.section('\n', 0, 4).replace('\n', "⏎"); QTimer::singleShot(300, this, &MainWindow::saveScreenshotAndQuit); });
    if (action == "stats") connect(m_worker, &DbWorker::statsReady, this, [this](const QVector<QStringList>& r){ qInfo().noquote() << "estadísticas de sesión:" << r.size() << "contadores; ejemplo:" << (r.isEmpty() ? QString() : r.first().join("=")) << "| filas en la página:" << m_stats->rowCount(); showPage(3); QTimer::singleShot(400, this, &MainWindow::saveScreenshotAndQuit); });
    if (action == "binds") for (const QString& kv : qEnvironmentVariable("SQLT_BINDS").split(';', Qt::SkipEmptyParts)) { BindValue v; v.value = kv.section('=', 1); const QString t = kv.section('=', 0, 0); m_bindValues[t.section(':', 0, 0).toUpper()] = v; if (t.contains(':')) m_bindValues[t.section(':', 0, 0).toUpper()].type = t.section(':', 1); }
    const int cancelMs = qEnvironmentVariableIntValue("SQLT_CANCEL_AFTER_MS");
    connect(m_worker, &DbWorker::connected, this, [this, sql, cancelMs, action]{
        if (!sql.isEmpty()) { editor()->setPlainText(sql); { QTextCursor tc = editor()->textCursor(); tc.setPosition(qEnvironmentVariableIntValue("SQLT_AUTORUN_CURSOR")); editor()->setTextCursor(tc); } if (action == "quickq") quickQuery(false); else if (action == "quickc") quickQuery(true); else if (action == "fromcursor") { QVector<int> l; startRun(statementsFromCursor(&l), l); } else if (action == "step") executeAndStep(); else if (action == "plan") explainPlan(); else if (action == "stats") { m_menu->action("ID_SQL_SESSION_STATISTICS")->trigger(); QTimer::singleShot(600, this, [this]{ runScript(); }); } else if (qEnvironmentVariableIntValue("SQLT_AUTORUN_SCRIPT")) runScript(); else runCurrent(); if (cancelMs > 0) QTimer::singleShot(cancelMs, this, &MainWindow::cancelQuery); }
    }, Qt::SingleShotConnection);
    startConnect(paramsFromEnvironment());
}
void MainWindow::saveScreenshotAndQuit ()
{
    grab().save(m_shotPath);
    if (m_model->rowCount() > 0) qInfo().noquote() << "primera fila:" << m_model->rows().first().join(" | ");
    { QStringList tabs; for (int i = 0; i < m_resultTabs->count(); ++i) tabs << m_resultTabs->tabText(i); qInfo().noquote() << "pestañas de resultados:" << tabs.join(" ‖ "); }
    if (editor()) qInfo().noquote() << "cursor en la línea:" << editor()->textCursor().blockNumber() + 1 << "| historial:" << m_history->rowCount() << "sentencias";
    qInfo().noquote() << "captura:" << m_shotPath << "| filas en la rejilla:" << m_model->rowCount() << "| salida:" << m_output->toPlainText().left(300).replace('\n', "⏎");
    QApplication::quit();
}


// ---- Errores de compilación ------------------------------------------------------------------
void MainWindow::onCompileErrors (const QVector<QStringList>& rows, const QString& objectName)
{
    m_errors->setRowCount(0);
    int nErr = 0;
    for (const auto& r : rows)
    {
        const int row = m_errors->rowCount(); m_errors->insertRow(row);
        const bool warning = r[0].compare("WARNING", Qt::CaseInsensitive) == 0;
        if (!warning) ++nErr;
        const QStringList cells = { warning ? "Aviso" : "Error", r[1], r[2], r[3] };
        for (int c = 0; c < 4; ++c)
        {
            auto* it = new QTableWidgetItem(cells[c]);
            if (!warning) it->setForeground(Theme::error());
            m_errors->setItem(row, c, it);
        }
    }
    if (rows.isEmpty()) { setPageBadge(2, QString()); m_output->appendPlainText("Objeto " + objectName + " compilado correctamente."); return; }
    setPageBadge(2, QString::number(rows.size()));
    m_output->appendPlainText(QString("Objeto %1 creado con %2 error(es) y %3 aviso(s) de compilación. Doble clic en la pestaña Errores para ir a la línea.")
                              .arg(objectName).arg(nErr).arg(rows.size() - nErr));
    showPage(2);
}

// ---- Pestañas de script ----------------------------------------------------------------------
SqlEditor* MainWindow::editor () const { return qobject_cast<SqlEditor*>(m_editors->currentWidget()); }

void MainWindow::updateEditorTitle (SqlEditor* e)
{
    const int i = m_editors->indexOf(e); if (i < 0) return;
    m_editors->setTabText(i, e->displayName() + (e->document()->isModified() ? " •" : ""));
    m_editors->setTabToolTip(i, e->filePath);
}

SqlEditor* MainWindow::addEditor (const QString& path)
{
    auto* e = new SqlEditor();
    applyEditorView(e);
    e->setCompletionData(m_meta);
    if (!m_bkmTimer) { m_bkmTimer = new QTimer(this); m_bkmTimer->setSingleShot(true); m_bkmTimer->setInterval(120); connect(m_bkmTimer, &QTimer::timeout, this, &MainWindow::updateBookmarkActions); }
    connect(e, &SqlEditor::bookmarksChanged, this, &MainWindow::updateBookmarkActions);
    connect(e->document(), &QTextDocument::contentsChanged, m_bkmTimer, qOverload<>(&QTimer::start));
    // las claves del editor son "TABLA" (esquema actual) o "ESQUEMA.TABLA"
    connect(e, &SqlEditor::columnsNeeded, this, [this](const QString& k){
        QMetaObject::invokeMethod(m_worker, "loadColumns", Qt::QueuedConnection, Q_ARG(QString, k.contains('.') ? k.section('.', 0, 0) : m_meta->currentSchema), Q_ARG(QString, k.section('.', -1))); });
    connect(e, &SqlEditor::membersNeeded, this, [this](const QString& k){
        QMetaObject::invokeMethod(m_worker, "loadMembers", Qt::QueuedConnection, Q_ARG(QString, k.contains('.') ? k.section('.', 0, 0) : m_meta->currentSchema), Q_ARG(QString, k.section('.', -1))); });
    connect(e, &SqlEditor::schemaObjectsNeeded, this, [this](const QString& o){ QMetaObject::invokeMethod(m_worker, "loadObjectsOf", Qt::QueuedConnection, Q_ARG(QString, o)); });
    if (!path.isEmpty())
    {
        QString err;
        if (!e->load(path, &err)) { QMessageBox::warning(this, "Abrir", "No se pudo abrir " + path + ":\n" + err); delete e; return nullptr; }
    }
    // Al abrir un archivo se reutiliza la pestaña "Sin título" vacía si es la única.
    if (!path.isEmpty() && m_editors->count() == 1)
        if (auto* only = qobject_cast<SqlEditor*>(m_editors->widget(0)); only && only->filePath.isEmpty() && only->document()->isEmpty() && !only->document()->isModified())
        { m_editors->removeTab(0); only->deleteLater(); }
    m_editors->setCurrentIndex(m_editors->addTab(e, e->displayName()));
    if (!path.isEmpty()) addRecent(path);
    connect(e->document(), &QTextDocument::modificationChanged, this, [this, e]{ updateEditorTitle(e); });
    updateEditorTitle(e);
    e->setFocus();
    return e;
}

void MainWindow::newScript () { addEditor(); }

// Pestaña lista para recibir texto: la actual si está vacía y sin guardar, o una nueva.
SqlEditor* MainWindow::blankEditor ()
{
    if (auto* e = editor(); e && e->filePath.isEmpty() && e->titleHint.isEmpty() && e->document()->isEmpty() && !e->document()->isModified()) return e;
    return addEditor();
}

void MainWindow::openScript ()
{
    QSettings st;
    const QStringList paths = QFileDialog::getOpenFileNames(this, "Abrir script", st.value("files/lastDir").toString(), "Scripts SQL (*.sql *.pls *.pks *.pkb *.plsql *.txt);;Todos (*)");
    for (const QString& p : paths)
    {
        st.setValue("files/lastDir", QFileInfo(p).absolutePath());
        bool already = false;                                        // si ya está abierto, solo se activa
        for (int i = 0; i < m_editors->count(); ++i)
            if (auto* e = qobject_cast<SqlEditor*>(m_editors->widget(i)); e && e->filePath == p) { m_editors->setCurrentIndex(i); already = true; }
        if (!already) addEditor(p);
    }
}

bool MainWindow::saveEditor (SqlEditor* e, bool saveAs)
{
    QString path = e->filePath;
    if (path.isEmpty() || saveAs)
    {
        QSettings st;
        path = QFileDialog::getSaveFileName(this, "Guardar script", st.value("files/lastDir").toString() + "/" + (e->filePath.isEmpty() ? "script.sql" : QFileInfo(e->filePath).fileName()), "Scripts SQL (*.sql);;Todos (*)");
        if (path.isEmpty()) return false;
        if (QFileInfo(path).suffix().isEmpty()) path += ".sql";
        st.setValue("files/lastDir", QFileInfo(path).absolutePath());
    }
    QString err;
    if (!e->save(path, &err)) { QMessageBox::warning(this, "Guardar", "No se pudo guardar:\n" + err); return false; }
    updateEditorTitle(e); addRecent(path);
    m_status->setText("Guardado: " + path + (e->encoding != "UTF-8" ? "  (" + e->encoding + ")" : QString()));
    return true;
}
bool MainWindow::saveScript ()   { auto* e = editor(); return e ? saveEditor(e, false) : false; }
bool MainWindow::saveScriptAs () { auto* e = editor(); return e ? saveEditor(e, true) : false; }

bool MainWindow::closeScript (int index)
{
    auto* e = qobject_cast<SqlEditor*>(m_editors->widget(index));
    if (!e) return true;
    if (e->document()->isModified())
    {
        m_editors->setCurrentIndex(index);
        auto r = QMessageBox::question(this, "Guardar cambios", "¿Guardar los cambios de «" + e->displayName() + "»?",
                                       QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
        if (r == QMessageBox::Cancel) return false;
        if (r == QMessageBox::Save && !saveEditor(e, false)) return false;
    }
    m_editors->removeTab(index);
    e->deleteLater();
    if (m_editors->count() == 0) addEditor();                         // siempre queda al menos una pestaña
    return true;
}

// Se recuerdan solo los archivos abiertos (rutas), no el contenido de scripts sin guardar.
void MainWindow::saveSession ()
{
    QStringList files; int current = 0;
    for (int i = 0; i < m_editors->count(); ++i)
        if (auto* e = qobject_cast<SqlEditor*>(m_editors->widget(i)); e && !e->filePath.isEmpty()) { if (i == m_editors->currentIndex()) current = files.size(); files << e->filePath; }
    QSettings st; st.setValue("session/files", files); st.setValue("session/current", current);
}

void MainWindow::restoreSession ()
{
    QSettings st;
    const QStringList files = st.value("session/files").toStringList();
    for (const QString& f : files) if (QFile::exists(f)) addEditor(f);
    if (m_editors->count() == 0) addEditor();
    else m_editors->setCurrentIndex(qBound(0, st.value("session/current").toInt(), m_editors->count() - 1));
}

void MainWindow::closeEvent (QCloseEvent* ev)
{
    if (m_connected && !confirmPending()) { ev->ignore(); return; }
    saveSession();
    saveSnapshot(true);                                               // instantánea automática (se puede recuperar en File > Snapshot > Open AS/QS)
    for (int i = m_editors->count() - 1; i >= 0; --i)
    {
        auto* e = qobject_cast<SqlEditor*>(m_editors->widget(i));
        if (e && e->document()->isModified())
        {
            m_editors->setCurrentIndex(i);
            auto r = QMessageBox::question(this, "Guardar cambios", "¿Guardar los cambios de «" + e->displayName() + "»?",
                                           QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
            if (r == QMessageBox::Cancel || (r == QMessageBox::Save && !saveEditor(e, false))) { ev->ignore(); return; }
        }
    }
    ev->accept();
}

// Segunda vista del mismo texto (se edita en las dos a la vez; comparte deshacer y colores)
void MainWindow::syncSplitView ()
{
    auto* e = editor(); if (!e) return;
    if (m_view2->document() != e->document()) { m_view2->setDocument(e->document()); m_view2->setFont(e->font()); m_view2->setTabStopDistance(e->tabStopDistance()); }
}

// Opciones de Settings que afectan a la sesión y al historial
void MainWindow::applyExecOptions ()
{
    QSettings st; const int cod = st.value("exec/commitOnDisconnect", 0).toInt();
    QMetaObject::invokeMethod(m_worker, "setTransactionOptions", Qt::QueuedConnection, Q_ARG(bool, st.value("exec/autocommit", false).toBool()), Q_ARG(int, cod == 1 ? 1 : 0));
    { const DdlOptions dd = DdlOptions::load(); QMetaObject::invokeMethod(m_worker, "setDdlOptions", Qt::QueuedConnection, Q_ARG(bool, dd.storage > 0), Q_ARG(bool, true), Q_ARG(bool, dd.specWithBody)); }
    if (m_historyStore) { m_historyStore->setMax(st.value("history/maxCount", 2000).toInt()); m_historyStore->setPersistent(st.value("history/persistent", true).toBool()); }
}

// Cambios sin confirmar al desconectar o cerrar: según la opción, se deshacen, se confirman o se pregunta
bool MainWindow::confirmPending ()
{
    if (!m_dirty) return true;
    const int opt = QSettings().value("exec/commitOnDisconnect", 0).toInt();
    if (opt == 1) { QMetaObject::invokeMethod(m_worker, "commit", Qt::BlockingQueuedConnection); return true; }
    if (opt == 0) return true;                                      // Oracle deshace al cerrar la sesión
    QMessageBox box(this); box.setWindowTitle("Cambios sin confirmar"); box.setText("Hay cambios sin confirmar en la sesión.");
    box.setInformativeText("¿Confirmarlos (Commit), deshacerlos (Rollback) o cancelar?");
    auto* c = box.addButton("Commit", QMessageBox::AcceptRole); auto* r = box.addButton("Rollback", QMessageBox::DestructiveRole); box.addButton("Cancelar", QMessageBox::RejectRole); box.exec();
    if (box.clickedButton() == c) { QMetaObject::invokeMethod(m_worker, "commit", Qt::BlockingQueuedConnection); return true; }
    if (box.clickedButton() == r) { QMetaObject::invokeMethod(m_worker, "rollback", Qt::BlockingQueuedConnection); return true; }
    return false;
}
