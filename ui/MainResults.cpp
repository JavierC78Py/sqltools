// Panel de resultados tipo "booklet" de SQLTools: páginas Result, Statistics, SQL Plan, Output, Errors, History y Binds,
// con su barra de iconos (rotar, navegar, ajustar columnas, exportar, fijar resultados, abrir en Excel/HTML…).
#include "MainWindow.h"
#include "Theme.h"
#include "BindParser.h"
#include "CommandMenu.h"
#include "DbWorker.h"
#include "HistoryStore.h"
#include "ResultModel.h"
#include "RotatedModel.h"
#include "SqlEditor.h"
#include "SqlSplitter.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QTextStream>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTableView>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryFile>
#include <QTextStream>
#include <QToolBar>
#include <QVBoxLayout>

static QTableWidget* makeTable (const QStringList& headers, const QFont& font)
{
    auto* t = new QTableWidget(0, headers.size());
    t->setHorizontalHeaderLabels(headers); t->setFont(font);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers); t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->verticalHeader()->setVisible(false); t->horizontalHeader()->setStretchLastSection(true); t->setAlternatingRowColors(true);
    return t;
}

// ======================= Construcción =======================
void MainWindow::setupResultsPanel ()
{
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont); mono.setFamilies({"Menlo", "Monaco", "Courier New"}); mono.setStyleHint(QFont::Monospace); mono.setPointSize(13);

    // Página 0 — Result: rejilla (o vista rotada) + resultados fijados (pin) como pestañas
    m_rotatedModel = new RotatedModel(m_model, this);
    m_rotated = new QTableView(); m_rotated->setModel(m_rotatedModel); m_rotated->setFont(mono); m_rotated->setAlternatingRowColors(true);
    m_rotated->horizontalHeader()->setStretchLastSection(true); m_rotated->verticalHeader()->hide(); m_rotated->setColumnWidth(0, 240);
    m_resultStack = new QStackedWidget(); m_resultStack->addWidget(m_table); m_resultStack->addWidget(m_rotated);
    m_resultTabs = new QTabWidget(); m_resultTabs->setDocumentMode(true); m_resultTabs->setTabBarAutoHide(true); m_resultTabs->setTabsClosable(true);
    m_resultTabs->addTab(m_resultStack, "Result");
    m_resultTabs->tabBar()->setTabButton(0, QTabBar::RightSide, nullptr);          // la pestaña viva no se cierra
    connect(m_resultTabs, &QTabWidget::tabCloseRequested, this, [this](int i){ if (i > 0) { auto* w = m_resultTabs->widget(i); m_resultTabs->removeTab(i); w->deleteLater(); } });
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this](const QModelIndex& cur, const QModelIndex&){ if (cur.isValid()) m_rotatedModel->setRow(cur.row()); });

    // Páginas auxiliares
    m_stats = makeTable({"Estadística", "Valor"}, mono);  m_stats->setColumnWidth(0, 380);
    m_plan = new QPlainTextEdit(); m_plan->setReadOnly(true); m_plan->setFont(mono); m_plan->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_plan->setPlaceholderText("Aquí aparece el plan de ejecución (Script > Explain Plan, F9).");
    m_history = makeTable({"Hora", "Duración", "Filas", "Estado", "SQL"}, mono);
    m_history->setColumnWidth(0, 150); m_history->setColumnWidth(1, 80); m_history->setColumnWidth(2, 70); m_history->setColumnWidth(3, 200);
    connect(m_history, &QTableWidget::cellDoubleClicked, this, [this](int row, int){
        if (auto* e = editor(); e && m_history->item(row, 4)) { e->textCursor().insertText(m_history->item(row, 4)->data(Qt::UserRole).toString()); e->setFocus(); } });
    m_binds = makeTable({"Variable", "Tipo", "Valor"}, mono);
    m_binds->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    m_binds->setColumnWidth(0, 200); m_binds->setColumnWidth(1, 110);
    connect(m_binds, &QTableWidget::cellChanged, this, [this](int row, int col){
        if (col != 2 || !m_binds->item(row, 0)) return;
        m_bindValues[m_binds->item(row, 0)->text().toUpper()].value = m_binds->item(row, 2) ? m_binds->item(row, 2)->text() : QString(); });

    // Contenedor de páginas (la barra de pestañas se oculta: se maneja con los iconos)
    m_tabs = new QTabWidget();
    m_tabs->addTab(m_resultTabs, "Result"); m_tabs->addTab(m_output, "Output"); m_tabs->addTab(m_errors, "Errors");
    m_tabs->addTab(m_stats, "Statistics"); m_tabs->addTab(m_plan, "SQL Plan"); m_tabs->addTab(m_history, "History"); m_tabs->addTab(m_binds, "Binds");
    m_tabs->tabBar()->hide(); m_tabs->setDocumentMode(true);
    m_pageActions.fill(nullptr, 7);

    m_resultsHost = new QWidget();
    auto* lay = new QVBoxLayout(m_resultsHost); lay->setContentsMargins(0, 0, 0, 0); lay->setSpacing(0);
    m_booklet = new QToolBar(); lay->addWidget(m_booklet); lay->addWidget(m_tabs, 1);

    QSettings st;
    const QString histFile = qEnvironmentVariableIsSet("SQLT_HISTORY_FILE") ? qEnvironmentVariable("SQLT_HISTORY_FILE") : QDir::homePath() + "/Library/Application Support/SQLToolsMac/history.json";   // las pruebas usan otro archivo
    m_historyStore = new HistoryStore(histFile);
    m_model->setNullText(st.value("grid/nullText", "(null)").toString());
    reloadHistoryTable();
}

void MainWindow::showPage (int index)
{
    m_tabs->setCurrentIndex(index);
    if (index >= 0 && index < m_pageActions.size() && m_pageActions[index]) m_pageActions[index]->setChecked(true);
    if (index == 6 && editor()) refreshBinds(editor()->toPlainText());
}

// Punto o contador sobre el icono de una página (p.ej. Output con texto nuevo, Errors con N errores)
void MainWindow::setPageBadge (int index, const QString& badge)
{
    if (index < 0 || index >= m_pageActions.size() || !m_pageActions[index]) return;
    QAction* a = m_pageActions[index];
    static const QStringList names = {"Result", "Output", "Errors", "Statistics", "SQL Plan", "History", "Binds"};
    const QIcon base = a->property("baseIcon").value<QIcon>();
    a->setToolTip(names.value(index) + (badge.isEmpty() ? QString() : "  (" + badge + ")"));
    QPixmap pm = base.pixmap(32, 32);
    if (!badge.isEmpty()) { QPainter p(&pm); p.setRenderHint(QPainter::Antialiasing); p.setBrush(QColor(0xd0, 0x30, 0x30)); p.setPen(Qt::NoPen); p.drawEllipse(QRect(pm.width() - 14, 0, 14, 14)); }
    a->setIcon(QIcon(pm));
}

void MainWindow::setupResultsCommands ()
{
    CommandMenu* m = m_menu; using R = CommandMenu;
    m->fillToolBar(m_booklet, "IDT_BOOKLET");
    m_booklet->setIconSize(QSize(16, 16));
    // páginas: exclusivas, como las pestañas de SQLTools
    m_pageGroup = new QActionGroup(this); m_pageGroup->setExclusive(true);
    const struct { const char* id; int page; } pages[] = { {"ID_BOOKLET_RESULT", 0}, {"ID_BOOKLET_OUTPUT", 1}, {"ID_BOOKLET_STATISTICS", 3}, {"ID_BOOKLET_SQL_PLAN", 4}, {"ID_BOOKLET_HISTORY", 5}, {"ID_BOOKLET_BINDS", 6} };
    for (const auto& p : pages)
    {
        const int page = p.page;
        QAction* a = m->bind(p.id, [this, page]{ showPage(page); }, R::None, true, page == 0);
        if (!a) continue;
        m_pageGroup->addAction(a); m_pageActions[page] = a; a->setProperty("baseIcon", QVariant::fromValue(a->icon()));
    }
    // Errors no existe en SQLTools (allí van a Output): página propia con icono de aviso, antes de History
    auto* err = new QAction(style()->standardIcon(QStyle::SP_MessageBoxWarning), "Errors", this);
    err->setCheckable(true); err->setToolTip("Errors"); m_pageGroup->addAction(err); m_pageActions[2] = err; err->setProperty("baseIcon", QVariant::fromValue(err->icon()));
    connect(err, &QAction::triggered, this, [this]{ showPage(2); });
    m_booklet->insertAction(m->action("ID_BOOKLET_HISTORY"), err);
    applyTheme();

    // rejilla
    m->bind("ID_GRID_ROTATE", [this]{ rotateGrid(m_menu->action("ID_GRID_ROTATE")->isChecked()); }, R::None, true, false);
    m->bind("ID_GRID_DATA_FIT",   [this]{ m_table->resizeColumnsToContents(); });
    m->bind("ID_GRID_HEADER_FIT", [this]{ for (int c = 0; c < m_model->columnCount(); ++c) m_table->setColumnWidth(c, m_table->fontMetrics().horizontalAdvance(m_model->columns().value(c)) + 30); });
    const char* moves[] = {"ID_GRID_MOVE_HOME", "ID_GRID_MOVE_PGUP", "ID_GRID_MOVE_UP", "ID_GRID_MOVE_DOWN", "ID_GRID_MOVE_PGDOWN", "ID_GRID_MOVE_END"};
    for (int i = 0; i < 6; ++i) m->bind(moves[i], [this, i]{ moveGridRow(i); });
    m->bind("ID_GRID_EXPORT", [this]{ exportCsv(); });
    m->bind("ID_GRID_SETTINGS", [this]{ gridSettings(); });
    m->bind("ID_BOOKLET_PIN",   [this]{ pinResult(); });
    m->bind("ID_BOOKLET_UNPIN", [this]{ unpinResult(); });
    m->bind("ID_GRID_OPEN_WITH_IE",    [this]{ openResultWith(false); });
    m->bind("ID_GRID_OPEN_WITH_EXCEL", [this]{ openResultWith(true); });
    m->bind("ID_GRID_CLEAR_RECORDS",   [this]{ clearResult(); });

    // Script / Session relacionados
    m->bind("ID_SQL_EXPLAIN_PLAN", [this]{ explainPlan(); }, R::NeedConnection | R::NeedIdle);
    m->bind("ID_SQL_SESSION_STATISTICS", [this]{ const bool on = m_menu->action("ID_SQL_SESSION_STATISTICS")->isChecked();
        QMetaObject::invokeMethod(m_worker, "setSessionStats", Qt::QueuedConnection, Q_ARG(bool, on)); if (on) showPage(3); }, R::NeedConnection | R::NeedIdle, true, false);
    m->bind("ID_SQL_QUICK_QUERY", [this]{ quickQuery(false); }, R::NeedConnection | R::NeedIdle);
    m->bind("ID_SQL_QUICK_COUNT", [this]{ quickQuery(true); },  R::NeedConnection | R::NeedIdle);
    m->bind("ID_SQL_EXECUTE_FROM_CURSOR", [this]{ QVector<int> lines; const QStringList st = statementsFromCursor(&lines); if (st.isEmpty()) m_status->setText("No hay sentencias desde el cursor."); else startRun(st, lines); }, R::NeedConnection | R::NeedIdle);
    m->bind("ID_SQL_EXECUTE_CURRENT_AND_STEP", [this]{ executeAndStep(); }, R::NeedConnection | R::NeedIdle);
    m->bind("ID_SQL_HISTORY_GET_AND_STEPBACK",    [this]{ historyStep(-1); });
    m->bind("ID_SQL_HISTORY_STEPFORWARD_AND_GET", [this]{ historyStep(+1); });

    // señales del hilo de BD hacia las páginas
    connect(m_worker, &DbWorker::statementDone, this, &MainWindow::addHistory);
    connect(m_worker, &DbWorker::resultColumns, this, &MainWindow::onResultColumns);
    connect(m_worker, &DbWorker::resultRows, this, &MainWindow::onResultRows);
    connect(m_worker, &DbWorker::resultDone, this, &MainWindow::onResultDone);
    connect(m_worker, &DbWorker::statsReady, this, &MainWindow::showStats);
    connect(m_worker, &DbWorker::planReady, this, [this](const QString& t){ m_plan->setPlainText(t); showPage(4); });
    applyGridOptions();
}

// ======================= Result =======================
void MainWindow::rotateGrid (bool on)
{
    m_resultStack->setCurrentIndex(on ? 1 : 0);
    if (on) { const auto cur = m_table->currentIndex(); m_rotatedModel->setRow(cur.isValid() ? cur.row() : (m_model->rowCount() ? 0 : -1)); }
}

void MainWindow::moveGridRow (int how)
{
    const int n = m_model->rowCount(); if (n == 0) return;
    int row = m_table->currentIndex().isValid() ? m_table->currentIndex().row() : 0;
    const int page = qMax(1, m_table->viewport()->height() / qMax(1, m_table->verticalHeader()->defaultSectionSize()) - 1);
    switch (how) { case 0: row = 0; break; case 1: row -= page; break; case 2: --row; break; case 3: ++row; break; case 4: row += page; break; default: row = n - 1; }
    row = qBound(0, row, n - 1);
    m_table->setCurrentIndex(m_model->index(row, qMax(0, m_table->currentIndex().column()))); m_table->scrollTo(m_table->currentIndex());
}

void MainWindow::pinResult ()
{
    if (m_model->rowCount() == 0 && m_model->columnCount() == 0) { m_status->setText("No hay resultado que fijar."); return; }
    auto* copy = new ResultModel(this); copy->reset(m_model->columns(), m_model->rightAligned()); copy->append(m_model->rows()); copy->setNullText(QSettings().value("grid/nullText", "(null)").toString());
    auto* view = new QTableView(); view->setModel(copy); view->setFont(m_table->font()); view->setAlternatingRowColors(true); view->setWordWrap(false);
    view->verticalHeader()->setDefaultSectionSize(m_table->verticalHeader()->defaultSectionSize()); view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    for (int c = 0; c < copy->columnCount(); ++c) view->setColumnWidth(c, m_table->columnWidth(c));
    const QString sql = m_lastStatements.isEmpty() ? QString() : m_lastStatements.last().simplified().left(24);
    m_resultTabs->setCurrentIndex(m_resultTabs->addTab(view, QString("📌 %1 · %2 filas").arg(sql.isEmpty() ? QString("#%1").arg(++m_pinCount) : sql).arg(copy->rowCount())));
    m_status->setText("Resultado fijado: no se sobrescribirá con la siguiente ejecución.");
}

void MainWindow::unpinResult ()
{
    const int i = m_resultTabs->currentIndex();
    if (i <= 0) { m_status->setText("Seleccione una pestaña de resultado fijado para soltarla."); return; }
    auto* w = m_resultTabs->widget(i); m_resultTabs->removeTab(i); w->deleteLater();
}

void MainWindow::clearResult () { m_model->reset(m_model->columns(), m_model->rightAligned()); m_rotatedModel->setRow(-1); m_status->setText("Resultados borrados de la rejilla."); }

void MainWindow::openResultWith (bool excel)
{
    if (m_model->columnCount() == 0) return;
    QTemporaryFile* f = new QTemporaryFile(QDir::tempPath() + (excel ? "/sqltools_XXXXXX.csv" : "/sqltools_XXXXXX.html")); f->setAutoRemove(false);
    if (!f->open()) { QMessageBox::warning(this, "Abrir", "No se pudo crear el archivo temporal."); delete f; return; }
    QTextStream o(f); o.setEncoding(QStringConverter::Utf8);
    auto csv = [](const QString& s){ if (s.contains(QRegularExpression("[\",\n\r;]"))) { QString t = s; t.replace('"', "\"\""); return "\"" + t + "\""; } return s; };
    auto html = [](QString s){ return s.toHtmlEscaped(); };
    if (excel)
    {
        o << QChar(0xFEFF);                                                // BOM: Excel reconoce UTF-8
        o << m_model->columns().join(',') << '\n';
        for (const auto& r : m_model->rows()) { QStringList c; for (const auto& v : r) c << csv(v); o << c.join(',') << '\n'; }
    }
    else
    {
        o << "<!doctype html><meta charset=utf-8><style>body{font:13px -apple-system,sans-serif}table{border-collapse:collapse}th,td{border:1px solid #bbb;padding:3px 8px}th{background:#eef}td.n{text-align:right}td.null{color:#999;font-style:italic}</style><table><tr>";
        for (const auto& c : m_model->columns()) o << "<th>" << html(c) << "</th>";
        o << "</tr>\n";
        const auto right = m_model->rightAligned();
        for (const auto& r : m_model->rows()) { o << "<tr>"; for (int i = 0; i < r.size(); ++i) o << (r[i].isNull() ? "<td class=null>(null)</td>" : QString("<td%1>%2</td>").arg(i < right.size() && right[i] == 1 ? " class=n" : "", html(r[i]))); o << "</tr>\n"; }
        o << "</table>";
    }
    o.flush(); const QString path = f->fileName(); f->close(); delete f;
    if (excel) { if (!QProcess::startDetached("open", {"-a", "Microsoft Excel", path})) QDesktopServices::openUrl(QUrl::fromLocalFile(path)); }
    else QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    m_status->setText("Abierto: " + path);
}

void MainWindow::gridSettings ()
{
    QSettings st; QDialog d(this); d.setWindowTitle("Grid settings");
    auto* prefetch = new QSpinBox(); prefetch->setRange(10, 200000); prefetch->setValue(st.value("grid/prefetch", 10000).toInt());
    auto* limit = new QSpinBox(); limit->setRange(100, 1000000); limit->setValue(st.value("grid/stringLimit", 4000).toInt());
    auto* nul = new QLineEdit(st.value("grid/nullText", "(null)").toString());
    auto* quick = new QSpinBox(); quick->setRange(10, 1000000); quick->setValue(st.value("quick/maxRows", 1000).toInt());
    auto* lay = new QFormLayout(&d);
    lay->addRow("Filas por viaje de red (prefetch)", prefetch); lay->addRow("Máximo de caracteres por celda de texto", limit); lay->addRow("Texto para NULL", nul); lay->addRow("Quick Query (F11): máximo de filas", quick);
    auto* hint = new QLabel("Más filas por viaje = mucho más rápido contra bases remotas (cada viaje cuesta ~0,1 s hasta la nube).\nSe aplica a las próximas consultas."); hint->setStyleSheet(Theme::css(Theme::muted())); lay->addRow(hint);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); lay->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept); connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted) return;
    st.setValue("grid/prefetch", prefetch->value()); st.setValue("grid/stringLimit", limit->value()); st.setValue("grid/nullText", nul->text()); st.setValue("quick/maxRows", quick->value());
    m_model->setNullText(nul->text()); applyGridOptions();
}

void MainWindow::applyGridOptions ()
{
    QSettings st;
    QMetaObject::invokeMethod(m_worker, "setFetchOptions", Qt::QueuedConnection, Q_ARG(int, st.value("grid/prefetch", 10000).toInt()), Q_ARG(int, st.value("grid/stringLimit", 4000).toInt()));
}

// ======================= Explain Plan, Statistics =======================
void MainWindow::explainPlan ()
{
    auto* e = editor(); if (!e || !m_connected || m_busy) return;
    QString sql;
    if (e->textCursor().hasSelection()) sql = e->textCursor().selectedText().replace(QChar(0x2029), '\n');
    else { const auto v = SqlSplitter::split(e->toPlainText()); const int k = SqlSplitter::statementAt(v, e->textCursor().position()); if (k >= 0) sql = v[k].text; }
    if (sql.trimmed().isEmpty()) { m_status->setText("Ponga el cursor en la sentencia cuyo plan quiere ver."); return; }
    QStringList one = applyBinds({sql});
    m_status->setText("Calculando el plan de ejecución…"); setBusy(true);
    QMetaObject::invokeMethod(m_worker, "explainPlan", Qt::QueuedConnection, Q_ARG(QString, one.first()));
}

void MainWindow::showStats (const QVector<QStringList>& rows)
{
    m_statsExtra = rows;
    m_stats->setRowCount(0);
    auto add = [this](const QString& k, const QString& v, bool bold = false) { const int r = m_stats->rowCount(); m_stats->insertRow(r);
        auto* a = new QTableWidgetItem(k); auto* b = new QTableWidgetItem(v); b->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        if (bold) { QFont f = a->font(); f.setBold(true); a->setFont(f); b->setFont(f); } m_stats->setItem(r, 0, a); m_stats->setItem(r, 1, b); };
    add("Tiempo de la última ejecución (s)", QString::number(m_lastSeconds, 'f', 3), true);
    add("Filas", QString::number(m_lastRows), true);
    add("Sentencias ejecutadas", QString::number(m_lastStatements.size()), true);
    if (!rows.isEmpty()) { add("— Estadísticas de sesión (diferencia) —", QString(), true); for (const auto& r : rows) add(r[0], r[1]); }
    else add("(Session > Enable Session Statistics muestra aquí los contadores de V$MYSTAT)", QString());
}

// ======================= Historial =======================
void MainWindow::addHistory (int, const QString& sql, qint64 rows, double seconds, const QString& error)
{
    QSettings hs; if (!hs.value("history/enabled", true).toBool()) return;
    if (hs.value("history/validOnly", false).toBool() && !error.isEmpty()) return;
    HistoryEntry e; e.when = QDateTime::currentDateTime(); e.sql = sql; e.rows = rows; e.seconds = seconds; e.error = error; e.connection = m_conn ? m_conn->text().section("  |  ", 0, 0) : QString();
    if (hs.value("history/distinct", false).toBool()) m_historyStore->removeSql(sql);
    m_historyStore->add(e); m_histPos = -1;                      // -1 = el cursor del historial está "después de la última"
    reloadHistoryTable();
}

void MainWindow::reloadHistoryTable ()
{
    const auto& list = m_historyStore->entries();
    m_history->setRowCount(0);
    for (int i = list.size() - 1; i >= 0 && m_history->rowCount() < 500; --i)                  // las 500 más recientes, la última arriba
    {
        const auto& e = list[i]; const int r = m_history->rowCount(); m_history->insertRow(r);
        const QString oneLine = e.sql.simplified();
        auto* when = new QTableWidgetItem(e.when.toString("yyyy-MM-dd HH:mm:ss")); auto* dur = new QTableWidgetItem(QString::number(e.seconds, 'f', 2) + " s");
        auto* rows = new QTableWidgetItem(QString::number(e.rows)); rows->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto* st = new QTableWidgetItem(e.error.isEmpty() ? "OK" : e.error); auto* sql = new QTableWidgetItem(oneLine.left(300)); sql->setData(Qt::UserRole, e.sql); sql->setToolTip(e.sql.left(2000));
        if (!e.error.isEmpty()) { st->setForeground(Theme::error()); }
        m_history->setItem(r, 0, when); m_history->setItem(r, 1, dur); m_history->setItem(r, 2, rows); m_history->setItem(r, 3, st); m_history->setItem(r, 4, sql);
    }
}

// Ctrl+D,S / Ctrl+D,F: trae al editor la sentencia anterior/siguiente del historial (sustituye la actual)
void MainWindow::historyStep (int delta)
{
    auto* e = editor(); const auto& list = m_historyStore->entries();
    if (!e || list.isEmpty()) { m_status->setText("El historial está vacío."); return; }
    // m_histPos = índice de la sentencia que se trajo la última vez (-1 si aún no se trajo ninguna desde la última ejecución)
    const int pos = delta < 0 ? (m_histPos < 0 ? int(list.size()) - 1 : m_histPos - 1) : (m_histPos < 0 ? -1 : m_histPos + 1);
    if (pos < 0 || pos >= list.size()) { m_status->setText(delta < 0 ? "No hay sentencias más antiguas." : "No hay sentencias más recientes."); return; }
    const QString sql = list[pos].sql;
    QTextCursor c = e->textCursor();
    if (!c.hasSelection())
    {
        const auto v = SqlSplitter::split(e->toPlainText()); const int k = SqlSplitter::statementAt(v, c.position());
        if (k >= 0) { c.setPosition(v[k].start); c.setPosition(v[k].end, QTextCursor::KeepAnchor); }
    }
    c.insertText(sql); e->setTextCursor(c);
    m_histPos = pos;
    m_status->setText(QString("Historial %1 de %2").arg(pos + 1).arg(list.size()));
}

// ======================= Binds =======================
void MainWindow::refreshBinds (const QString& text)
{
    const QStringList names = BindParser::names(text);
    m_binds->blockSignals(true); m_binds->setRowCount(0);
    for (const QString& n : names)
    {
        const int r = m_binds->rowCount(); m_binds->insertRow(r);
        const BindValue v = m_bindValues.value(n.toUpper());
        auto* name = new QTableWidgetItem(":" + n); name->setData(Qt::UserRole, n); name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        auto* val = new QTableWidgetItem(v.value); val->setFlags(val->flags() | Qt::ItemIsEditable);
        m_binds->setItem(r, 0, new QTableWidgetItem(n)); m_binds->item(r, 0)->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable); m_binds->item(r, 0)->setText(n);
        auto* type = new QComboBox(); type->addItems({"Auto", "String", "Number", "Date", "NULL"}); type->setCurrentText(v.type);
        connect(type, &QComboBox::currentTextChanged, this, [this, n](const QString& t){ m_bindValues[n.toUpper()].type = t; });
        m_binds->setCellWidget(r, 1, type); m_binds->setItem(r, 2, val);
    }
    m_binds->blockSignals(false);
    if (names.isEmpty()) { m_binds->setRowCount(1); auto* it = new QTableWidgetItem("(no hay variables :nombre en el script)"); it->setFlags(Qt::ItemIsEnabled); m_binds->setItem(0, 0, it); }
}

// Sustituye las :variables por los valores de la página Binds (como literales). Los bloques PL/SQL no se pueden sustituir.
QStringList MainWindow::applyBinds (QStringList stmts)
{
    QStringList all; for (const auto& s : stmts) all << BindParser::names(s);
    if (all.isEmpty()) return stmts;
    refreshBinds(stmts.join("\n;\n"));
    for (const QString& n : all) if (!m_bindValues.contains(n.toUpper())) m_bindValues[n.toUpper()] = BindValue();
    QStringList warnings, out;
    for (const QString& s : stmts)
    {
        const bool plsql = QRegularExpression(R"(^\s*(begin|declare|create\s+(or\s+replace\s+)?(procedure|function|package|trigger|type)))", QRegularExpression::CaseInsensitiveOption).match(s).hasMatch();
        if (plsql && !BindParser::names(s).isEmpty()) { warnings << "En un bloque PL/SQL las variables :nombre no se sustituyen (Oracle pedirá enlazarlas)."; out << s; continue; }
        out << BindParser::substitute(s, m_bindValues, &warnings);
    }
    QStringList used; for (const QString& n : all) used << ":" + n; used.removeDuplicates();
    m_status->setText("Variables sustituidas: " + used.join(", ") + (warnings.isEmpty() ? QString() : "   ⚠ " + warnings.first()));
    return out;
}


// ======================= Varios resultados por script =======================
void MainWindow::clearRunResults ()
{
    for (int i = m_resultTabs->count() - 1; i > 0; --i)
        if (m_resultTabs->widget(i)->property("runResult").toBool()) { auto* w = m_resultTabs->widget(i); m_resultTabs->removeTab(i); w->deleteLater(); }
    m_extraModels.clear(); m_extraTabs.clear();
}

void MainWindow::onResultColumns (int index, const QStringList& names, const QVector<int>& right)
{
    auto* model = new ResultModel(this); model->reset(names, right); model->setNullText(QSettings().value("grid/nullText", "(null)").toString());
    auto* view = new QTableView(); view->setModel(model); view->setFont(m_table->font()); view->setAlternatingRowColors(true); view->setWordWrap(false);
    view->verticalHeader()->setDefaultSectionSize(m_table->verticalHeader()->defaultSectionSize()); view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    view->setProperty("runResult", true); view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (int c = 0; c < names.size(); ++c) view->setColumnWidth(c, qMax(90, view->fontMetrics().horizontalAdvance(names[c]) + 30));
    const QString sql = index < m_lastStatements.size() ? m_lastStatements[index].simplified().left(26) : QString();
    // las pestañas intermedias van antes de las fijadas (📌), en el orden del script
    int pos = 1; while (pos < m_resultTabs->count() && m_resultTabs->widget(pos)->property("runResult").toBool()) ++pos;
    m_resultTabs->insertTab(pos, view, QString("%1: %2").arg(index + 1).arg(sql));
    m_extraModels[index] = model; m_extraTabs[index] = pos;
}

void MainWindow::onResultRows (int index, const QVector<QStringList>& rows)
{
    if (auto* m = m_extraModels.value(index)) m->append(rows);
}

void MainWindow::onResultDone (int index, qint64 rows, bool truncated)
{
    auto* m = m_extraModels.value(index); if (!m) return;
    for (int i = 1; i < m_resultTabs->count(); ++i)
        if (auto* v = qobject_cast<QTableView*>(m_resultTabs->widget(i)); v && v->model() == m)
            m_resultTabs->setTabText(i, m_resultTabs->tabText(i) + QString("  (%1%2)").arg(rows).arg(truncated ? "+, truncado" : ""));
}

// ======================= Quick Query / Quick Count / ejecutar desde el cursor =======================
QString MainWindow::objectUnderCursor () const
{
    auto* e = editor(); if (!e) return QString();
    const QString t = e->toPlainText(); int p = e->textCursor().position();
    if (e->textCursor().hasSelection()) return e->textCursor().selectedText().trimmed();
    auto ok = [](QChar c) { return c.isLetterOrNumber() || c == '_' || c == '$' || c == '#' || c == '.' || c == '"'; };
    int a = p, b = p; while (a > 0 && ok(t[a - 1])) --a; while (b < t.size() && ok(t[b])) ++b;
    QString w = t.mid(a, b - a); while (w.startsWith('.')) w.remove(0, 1); while (w.endsWith('.')) w.chop(1);
    return w;
}

void MainWindow::quickQuery (bool count)
{
    if (!m_connected || m_busy) return;
    const QString obj = objectUnderCursor();
    if (obj.isEmpty()) { m_status->setText("Ponga el cursor sobre el nombre de una tabla o vista (o selecciónelo)."); return; }
    quickQueryOn(obj, count);
}

void MainWindow::quickQueryOn (const QString& obj, bool count)
{
    if (!m_connected || m_busy) return;
    if (!QRegularExpression(R"(^[\w$#."]+$)").match(obj).hasMatch()) { m_status->setText("«" + obj.left(40) + "» no parece un nombre de tabla o vista."); return; }
    const int cap = QSettings().value("quick/maxRows", 1000).toInt();
    const QString sql = count ? (QSettings().value("quick/lower", true).toBool() ? QString("select count(*) as \"COUNT\" from %1") : QString("SELECT COUNT(*) AS \"COUNT\" FROM %1")).arg(obj)
                              : (QSettings().value("quick/lower", true).toBool() ? QString("select * from (select * from %1) where rownum <= %2") : QString("SELECT * FROM (SELECT * FROM %1) WHERE ROWNUM <= %2")).arg(obj).arg(cap);
    startRun({sql}, {1});
    m_status->setText(count ? "Quick Count de " + obj + "…" : QString("Quick Query de %1 (primeras %2 filas)…").arg(obj).arg(cap));
}

// Sentencias desde la que contiene el cursor hasta el final del script (Execute from Cursor)
QStringList MainWindow::statementsFromCursor (QVector<int>* lines) const
{
    QStringList out; auto* e = editor(); if (!e) return out;
    const auto v = SqlSplitter::split(e->toPlainText());
    const int k = SqlSplitter::statementAt(v, e->textCursor().position());
    if (k < 0) return out;
    for (int i = k; i < v.size(); ++i) { out << v[i].text; if (lines) lines->push_back(v[i].startLine); }
    return out;
}

void MainWindow::executeAndStep ()
{
    auto* e = editor(); if (!e || !m_connected || m_busy) return;
    const auto v = SqlSplitter::split(e->toPlainText()); const int k = SqlSplitter::statementAt(v, e->textCursor().position());
    runCurrent();
    if (k >= 0 && k + 1 < v.size()) { QTextCursor c = e->textCursor(); c.setPosition(v[k + 1].start); e->setTextCursor(c); }        // el cursor pasa a la sentencia siguiente
}
