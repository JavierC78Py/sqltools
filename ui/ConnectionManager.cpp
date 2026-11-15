#include "ConnectionManager.h"
#include "Theme.h"
#include "FilterHeader.h"
#include "ConnectionImporter.h"
#include "WalletResolver.h"
#include <QTableWidget>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTableView>

// Filtro por columna (como la fila de filtros del SQLTools de Windows): cada columna con su texto; coincide si contiene el texto.
class ColumnFilterProxy : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    QStringList filters;
protected:
    bool filterAcceptsRow (int row, const QModelIndex& parent) const override
    {
        for (int c = 0; c < filters.size(); ++c)
            if (!filters[c].isEmpty() && !sourceModel()->index(row, c, parent).data().toString().contains(filters[c], Qt::CaseInsensitive)) return false;
        return true;
    }
    bool lessThan (const QModelIndex& a, const QModelIndex& b) const override
    {
        if (a.column() == 3) return a.data(Qt::UserRole).toInt() < b.data(Qt::UserRole).toInt();          // "Veces": numérico
        if (a.column() == 4) return a.data(Qt::UserRole).toLongLong() < b.data(Qt::UserRole).toLongLong(); // "Último uso": por fecha
        return QString::compare(a.data().toString(), b.data().toString(), Qt::CaseInsensitive) < 0;
    }
};

ConnectionManager::ConnectionManager (ConnectionStore* store, QWidget* parent) : QDialog(parent), m_store(store)
{
    setWindowTitle("Conectar a Oracle");
    resize(1040, 540);

    // ---- lista con filtros ----
    m_model = new QStandardItemModel(0, 5, this);
    m_model->setHorizontalHeaderLabels({"Etiqueta", "Usuario", "Alias", "Veces", "Último uso"});
    m_proxy = new ColumnFilterProxy(this); m_proxy->setSourceModel(m_model); m_proxy->filters = QStringList(5, QString());
    m_table = new QTableView(); auto* fh = new FilterHeader(m_table); m_table->setHorizontalHeader(fh);
    m_table->setModel(m_proxy); m_table->setSortingEnabled(true); m_table->setSelectionBehavior(QAbstractItemView::SelectRows); m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers); m_table->verticalHeader()->hide(); m_table->verticalHeader()->setDefaultSectionSize(24); m_table->setAlternatingRowColors(true); m_table->setShowGrid(false);
    m_table->horizontalHeader()->setStretchLastSection(true); m_table->setWordWrap(false); fh->setFilterCount(5);
    const int widths[] = {110, 110, 140, 56, 140};
    for (int c = 0; c < 5; ++c) m_table->setColumnWidth(c, widths[c]);
    connect(fh, &FilterHeader::filterChanged, this, [this](int c, const QString& t){ if (c < m_proxy->filters.size()) { m_proxy->filters[c] = t; m_proxy->invalidate(); } });
    auto syncFilters = []{};
    auto* left = new QVBoxLayout(); left->setContentsMargins(0, 0, 0, 0); left->addWidget(m_table);

    // ---- formulario ----
    m_tag = new QLineEdit(); m_user = new QLineEdit(); m_pass = new QLineEdit(); m_pass->setEchoMode(QLineEdit::Password);
    m_alias = new QComboBox(); m_alias->setEditable(true);
    m_tns = new QLineEdit(); m_tns->setPlaceholderText("Vacío = usar la carpeta TNS global (Ajustes…)");
    auto* browse = new QPushButton("Elegir…");
    m_bypass = new QCheckBox("Sin tnsnames.ora (host, puerto y SID/servicio)");
    m_host = new QLineEdit(); m_port = new QLineEdit("1521"); m_sid = new QLineEdit();
    m_service = new QCheckBox("Usar servicio en lugar de SID"); m_service->setChecked(true);
    m_mode = new QComboBox(); m_mode->addItems({"Normal", "SYSDBA", "SYSOPER"});
    m_readOnly = new QCheckBox("Solo lectura (solo permite SELECT)"); m_slow = new QCheckBox("Red lenta");
    m_savePw = new QCheckBox("Guardar contraseña en el Llavero de macOS"); m_savePw->setChecked(true);
    m_result = new QLabel(); m_result->setWordWrap(true);

    auto* tnsRow = new QHBoxLayout(); tnsRow->setContentsMargins(0, 0, 0, 0); tnsRow->addWidget(m_tns, 1); tnsRow->addWidget(browse);
    auto* form = new QFormLayout(); form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter); form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow); form->setHorizontalSpacing(12); form->setVerticalSpacing(8);
    form->addRow("Etiqueta", m_tag); form->addRow("Usuario", m_user); form->addRow("Contraseña", m_pass);
    form->addRow("Alias TNS", m_alias); form->addRow("Carpeta TNS", tnsRow);
    m_tnsInfo = new QLabel(); m_tnsInfo->setWordWrap(true); m_tnsInfo->setContentsMargins(0, 0, 0, 0); form->addRow(m_tnsInfo);
    form->addRow(m_bypass);
    m_bypassBox = new QWidget(); auto* bf = new QFormLayout(m_bypassBox); bf->setContentsMargins(0, 0, 0, 0); bf->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter); bf->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow); bf->setHorizontalSpacing(12); bf->setVerticalSpacing(8);
    bf->addRow("Host", m_host); bf->addRow("Puerto", m_port); bf->addRow("SID / servicio", m_sid); bf->addRow(m_service); form->addRow(m_bypassBox);
    form->addRow("Modo", m_mode);
    auto* opts = new QVBoxLayout(); opts->setSpacing(4); opts->addWidget(m_readOnly); opts->addWidget(m_slow); opts->addWidget(m_savePw);

    m_btnConnect = new QPushButton("Conectar"); m_btnConnect->setDefault(true); m_btnConnect->setObjectName("primary");
    m_btnSave = new QPushButton("Guardar"); auto* btnNew = new QPushButton("Nueva"); auto* btnClose = new QPushButton("Cerrar");
    m_btnTest = new QPushButton("Probar"); m_btnDelete = new QPushButton("Eliminar"); auto* btnSettings = new QPushButton("Ajustes…"); auto* btnImport = new QPushButton("Importar…");
    auto* bg = new QGridLayout(); bg->setSpacing(8);
    bg->addWidget(m_btnConnect, 0, 0, 1, 2); bg->addWidget(btnClose, 0, 2);
    bg->addWidget(m_btnTest, 1, 0); bg->addWidget(m_btnSave, 1, 1); bg->addWidget(btnNew, 1, 2);
    bg->addWidget(m_btnDelete, 2, 0); bg->addWidget(btnImport, 2, 1); bg->addWidget(btnSettings, 2, 2);
    auto* right = new QVBoxLayout(); right->setSpacing(10); right->addLayout(form); right->addLayout(opts); right->addWidget(m_result); right->addStretch(1); right->addLayout(bg);

    auto* root = new QHBoxLayout(this); root->setContentsMargins(14, 14, 14, 14); root->setSpacing(18);
    root->addLayout(left, 3); root->addLayout(right, 2);
    {   // aspecto moderno (bordes suaves, botón principal destacado)
        const bool dk = Theme::dark(); const QString line = dk ? "#3d3d40" : "#d0d4dc", bgc = dk ? "#2e2f33" : "#ffffff", hov = dk ? "#3a3d43" : "#eef1f6", acc = QGuiApplication::palette().color(QPalette::Highlight).name();
        setStyleSheet(Theme::modernCss() + QString(R"(
            QPushButton { border: 1px solid %1; border-radius: 7px; padding: 5px 12px; background: %2; }
            QPushButton:hover { background: %3; } QPushButton:pressed { background: %1; }
            QPushButton:disabled { color: #9aa0ab; }
            QPushButton#primary { background: %4; color: white; border-color: %4; font-weight: 600; }
            QPushButton#primary:hover { background: %4; }
            QLineEdit, QComboBox { min-height: 22px; }
            QCheckBox { spacing: 8px; }
        )").arg(line, bgc, hov, acc));
        m_table->setFrameShape(QFrame::NoFrame);
    }

    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &ConnectionManager::onRowChanged);
    connect(m_table, &QTableView::doubleClicked, this, &ConnectionManager::connectNow);
    connect(browse, &QPushButton::clicked, this, &ConnectionManager::browseTns);
    connect(m_tns, &QLineEdit::editingFinished, this, &ConnectionManager::reloadAliases);
    connect(m_bypass, &QCheckBox::toggled, this, &ConnectionManager::updateBypassState);
    connect(m_btnConnect, &QPushButton::clicked, this, &ConnectionManager::connectNow);
    connect(m_btnSave, &QPushButton::clicked, this, &ConnectionManager::saveConnection);
    connect(m_btnTest, &QPushButton::clicked, this, &ConnectionManager::testNow);
    connect(m_btnDelete, &QPushButton::clicked, this, &ConnectionManager::deleteConnection);
    connect(btnNew, &QPushButton::clicked, this, &ConnectionManager::newConnection);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnSettings, &QPushButton::clicked, this, &ConnectionManager::openSettings);
    connect(btnImport, &QPushButton::clicked, this, &ConnectionManager::importFromSqlTools);

    updateTnsInfo();
    reloadList();
    if (m_model->rowCount() == 0) newConnection();
    else { QSettings st; selectById(st.value("conn/lastId").toString()); if (!m_table->currentIndex().isValid()) m_table->selectRow(0); }
    updateBypassState();
    syncFilters();
}

void ConnectionManager::reloadList (const QString& selectId)
{
    m_model->removeRows(0, m_model->rowCount());
    auto list = m_store->all();
    for (const auto& p : list)
    {
        QList<QStandardItem*> row;
        row << new QStandardItem(p.tag) << new QStandardItem(p.user) << new QStandardItem(p.bypass ? p.host + ":" + p.port + "/" + p.sid : p.alias)
            << new QStandardItem(QString::number(p.count)) << new QStandardItem(p.lastUsed.isValid() ? p.lastUsed.toString("yyyy-MM-dd HH:mm") : QString());
        row[0]->setData(p.id, Qt::UserRole + 10);
        row[3]->setData(p.count, Qt::UserRole); row[3]->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row[4]->setData(p.lastUsed.isValid() ? p.lastUsed.toSecsSinceEpoch() : 0, Qt::UserRole);
        m_model->appendRow(row);
    }
    m_table->sortByColumn(0, Qt::AscendingOrder);
    if (!selectId.isEmpty()) selectById(selectId);
}

void ConnectionManager::selectById (const QString& id)
{
    for (int r = 0; r < m_proxy->rowCount(); ++r)
        if (m_proxy->index(r, 0).data(Qt::UserRole + 10).toString() == id) { m_table->selectRow(r); m_table->scrollTo(m_proxy->index(r, 0)); return; }
}

void ConnectionManager::onRowChanged ()
{
    const auto idx = m_table->currentIndex(); if (!idx.isValid()) return;
    fillForm(m_store->find(m_proxy->index(idx.row(), 0).data(Qt::UserRole + 10).toString()));
}

void ConnectionManager::fillForm (const ConnectionProfile& p)
{
    m_currentId = p.id; m_count = p.count; m_last = p.lastUsed;
    m_tag->setText(p.tag); m_user->setText(p.user); m_pass->clear();
    m_pass->setPlaceholderText(p.savePassword ? "(guardada en el Llavero)" : "");
    m_tns->setText(p.tnsAdmin); reloadAliases(); m_alias->setCurrentText(p.alias);
    m_bypass->setChecked(p.bypass); m_host->setText(p.host); m_port->setText(p.port); m_sid->setText(p.sid); m_service->setChecked(p.useService);
    m_mode->setCurrentText(p.mode); m_readOnly->setChecked(p.readOnly); m_slow->setChecked(p.slow); m_savePw->setChecked(p.savePassword);
    m_result->clear(); updateBypassState();
}

void ConnectionManager::newConnection ()
{
    ConnectionProfile p; p.id = ConnectionStore::newId();
    m_table->clearSelection(); fillForm(p); m_tag->setFocus();
}

QString ConnectionManager::globalTns () const
{
    QString d = QSettings().value("conn/defaultTns").toString().trimmed();
    if (d == "~" || d.startsWith("~/")) d = QDir::homePath() + d.mid(1);
    return d.isEmpty() ? QString() : QDir::cleanPath(d);
}

QString ConnectionManager::effectiveTns () const
{
    QString d = m_tns->text().trimmed();
    if (d == "~" || d.startsWith("~/")) d = QDir::homePath() + d.mid(1);
    return d.isEmpty() ? globalTns() : QDir::cleanPath(d);
}

void ConnectionManager::updateTnsInfo ()
{
    const QString g = globalTns();
    if (g.isEmpty()) { m_tnsInfo->setStyleSheet(Theme::css(Theme::error())); m_tnsInfo->setText("No hay carpeta TNS global. Defínala en «Ajustes…» (la carpeta donde está su tnsnames.ora)."); }
    else if (!QFile::exists(g + "/tnsnames.ora") && !QFile::exists(g + "/TNSNAMES.ORA")) { m_tnsInfo->setStyleSheet(Theme::css(Theme::error())); m_tnsInfo->setText("La carpeta TNS global no tiene tnsnames.ora: " + g); }
    else { m_tnsInfo->setStyleSheet(Theme::css(Theme::muted())); m_tnsInfo->setText("Carpeta TNS global: " + g); }
}

ConnectionProfile ConnectionManager::currentProfile () const
{
    ConnectionProfile p;
    p.id = m_currentId.isEmpty() ? ConnectionStore::newId() : m_currentId;
    p.tag = m_tag->text().trimmed(); p.user = m_user->text().trimmed(); p.alias = m_alias->currentText().trimmed(); p.tnsAdmin = m_tns->text().trimmed();
    if (p.tnsAdmin == "~" || p.tnsAdmin.startsWith("~/")) p.tnsAdmin = QDir::homePath() + p.tnsAdmin.mid(1);       // Oracle no expande "~"
    p.bypass = m_bypass->isChecked(); p.host = m_host->text().trimmed(); p.port = m_port->text().trimmed(); p.sid = m_sid->text().trimmed();
    p.useService = m_service->isChecked(); p.mode = m_mode->currentText(); p.readOnly = m_readOnly->isChecked(); p.slow = m_slow->isChecked();
    p.savePassword = m_savePw->isChecked(); p.count = m_count; p.lastUsed = m_last;
    return p;
}

bool ConnectionManager::validate (QString* why) const
{
    if (m_user->text().trimmed().isEmpty()) { *why = "Falta el usuario."; return false; }
    if (m_bypass->isChecked()) { if (m_host->text().trimmed().isEmpty() || m_sid->text().trimmed().isEmpty()) { *why = "Falta el host o el SID/servicio."; return false; } }
    else if (m_alias->currentText().trimmed().isEmpty()) { *why = "Falta el alias TNS."; return false; }
    return true;
}

// La contraseña escrita manda; si el campo está vacío y la conexión la guardó, se lee del Llavero en este momento (no antes).
bool ConnectionManager::resolvePassword (QString* pw)
{
    *pw = m_pass->text();
    if (!pw->isEmpty()) return true;
    const auto p = currentProfile();
    if (p.savePassword && m_store->password(p.id, pw)) return true;
    QMessageBox::information(this, "Contraseña", "Escriba la contraseña (no hay ninguna guardada para esta conexión).");
    m_pass->setFocus();
    return false;
}

QVariantMap ConnectionManager::paramsFor (const ConnectionProfile& p, const QString& password) const
{
    return { {"tag", p.tag}, {"user", p.user}, {"password", password}, {"alias", p.alias}, {"tnsAdmin", p.tnsAdmin.isEmpty() ? globalTns() : p.tnsAdmin}, {"bypass", p.bypass}, {"host", p.host},
             {"port", p.port}, {"sid", p.sid}, {"useService", p.useService}, {"mode", p.mode}, {"readOnly", p.readOnly}, {"slow", p.slow} };
}

void ConnectionManager::saveConnection ()
{
    QString why; if (!validate(&why)) { QMessageBox::warning(this, "Guardar", why); return; }
    auto p = currentProfile();
    m_store->save(p); m_currentId = p.id;
    if (p.savePassword) { if (!m_pass->text().isEmpty()) m_store->setPassword(p.id, m_pass->text()); }
    else m_store->removePassword(p.id);                                          // ya no se quiere guardar: se borra del Llavero
    reloadList(p.id);
    m_pass->clear(); m_pass->setPlaceholderText(p.savePassword ? "(guardada en el Llavero)" : "");
    m_result->setText("Conexión guardada.");
}

void ConnectionManager::deleteConnection ()
{
    if (m_currentId.isEmpty() || m_store->find(m_currentId).id.isEmpty()) return;
    if (QMessageBox::question(this, "Eliminar", "¿Eliminar la conexión «" + m_tag->text() + " " + m_user->text() + "@" + m_alias->currentText() + "» y su contraseña guardada?") != QMessageBox::Yes) return;
    m_store->remove(m_currentId);
    reloadList();
    if (m_model->rowCount() == 0) newConnection(); else m_table->selectRow(0);
}

void ConnectionManager::connectNow ()
{
    QString why; if (!validate(&why)) { QMessageBox::warning(this, "Conectar", why); return; }
    QString pw; if (!resolvePassword(&pw)) return;
    auto p = currentProfile();
    m_store->save(p);                                                             // se guarda/actualiza la conexión al usarla
    if (p.savePassword) m_store->setPassword(p.id, pw);
    QSettings().setValue("conn/lastId", p.id);
    m_currentId = p.id; m_params = paramsFor(p, pw);
    accept();
}

void ConnectionManager::testNow ()
{
    QString why; if (!validate(&why)) { QMessageBox::warning(this, "Probar", why); return; }
    QString pw; if (!resolvePassword(&pw)) return;
    m_result->setStyleSheet(QString()); m_result->setText("Probando…");
    m_btnTest->setEnabled(false);
    emit testRequested(paramsFor(currentProfile(), pw));
}

void ConnectionManager::showTestResult (bool ok, const QString& message)
{
    m_btnTest->setEnabled(true);
    m_result->setStyleSheet(Theme::css(ok ? Theme::ok() : Theme::error()));
    m_result->setText(message.section('\n', 0, 2));
}

void ConnectionManager::browseTns ()
{
    const QString d = QFileDialog::getExistingDirectory(this, "Carpeta con tnsnames.ora / wallet", m_tns->text());
    if (!d.isEmpty()) { m_tns->setText(d); reloadAliases(); }
}

void ConnectionManager::reloadAliases ()
{
    const QString cur = m_alias->currentText();
    m_alias->clear();
    const QString dir = effectiveTns();
    QFile f(dir + (QFile::exists(dir + "/tnsnames.ora") ? "/tnsnames.ora" : "/TNSNAMES.ORA"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        static const QRegularExpression re(R"(^([A-Za-z][A-Za-z0-9_.\-]*)\s*=)");
        while (!f.atEnd()) { auto m = re.match(QString::fromUtf8(f.readLine())); if (m.hasMatch()) m_alias->addItem(m.captured(1)); }
    }
    m_alias->setCurrentText(cur);
}

void ConnectionManager::updateBypassState ()
{
    const bool b = m_bypass->isChecked();
    m_host->setEnabled(b); m_port->setEnabled(b); m_sid->setEnabled(b); m_service->setEnabled(b);
    m_alias->setEnabled(!b); m_bypassBox->setVisible(b);
}

void ConnectionManager::openSettings ()
{
    QDialog d(this); d.setWindowTitle("Ajustes de conexión"); d.resize(560, 160);
    QSettings st; auto* tns = new QLineEdit(st.value("conn/defaultTns").toString());
    tns->setPlaceholderText("Carpeta donde está su tnsnames.ora (con las wallets apuntando a este Mac)");
    auto* pick = new QPushButton("Elegir…"); auto* check = new QPushButton("Comprobar TNS…");
    connect(pick, &QPushButton::clicked, &d, [&]{ const QString f = QFileDialog::getExistingDirectory(&d, "Carpeta TNS", tns->text()); if (!f.isEmpty()) tns->setText(f); });
    connect(check, &QPushButton::clicked, &d, [&]{
        QString dir = tns->text().trimmed(); if (dir == "~" || dir.startsWith("~/")) dir = QDir::homePath() + dir.mid(1);
        const auto list = WalletResolver::inspect(dir, QDir::homePath());
        if (list.isEmpty()) { QMessageBox::warning(&d, "Comprobar TNS", "No se encontró ningún alias: ¿hay un tnsnames.ora en «" + dir + "»?"); return; }
        QDialog r(&d); r.setWindowTitle("Comprobación del TNS: " + dir); r.resize(900, 480);
        auto* t = new QTableWidget(list.size(), 4); t->setHorizontalHeaderLabels({"Alias", "Tipo", "Estado", "Detalle"});
        t->setEditTriggers(QAbstractItemView::NoEditTriggers); t->verticalHeader()->hide(); t->horizontalHeader()->setStretchLastSection(true);
        int bad = 0, tls = 0;
        for (int i = 0; i < list.size(); ++i)
        {
            const auto& c = list[i]; bad += !c.ok; tls += c.tls;
            const QStringList cells = { c.alias, c.tls ? "TLS (wallet)" : "TCP", c.ok ? "Correcto" : "Revisar", c.message };
            for (int k = 0; k < 4; ++k) { auto* it = new QTableWidgetItem(cells[k]); if (!c.ok) it->setForeground(Theme::error()); t->setItem(i, k, it); }
        }
        t->setColumnWidth(0, 200); t->setColumnWidth(1, 100); t->setColumnWidth(2, 80);
        auto* sum = new QLabel(QString("%1 alias: %2 con TLS, %3 para revisar.").arg(list.size()).arg(tls).arg(bad));
        auto* lay = new QVBoxLayout(&r); lay->addWidget(sum); lay->addWidget(t);
        auto* close = new QPushButton("Cerrar"); connect(close, &QPushButton::clicked, &r, &QDialog::accept); lay->addWidget(close, 0, Qt::AlignRight);
        r.exec(); });
    auto* row = new QHBoxLayout(); row->addWidget(tns); row->addWidget(pick);
    auto* help = new QLabel("Como en el SQLTools de Windows, hay una sola carpeta TNS para todas las conexiones. "
                            "Cada entrada de su tnsnames.ora debe indicar su wallet con una ruta de este Mac, por ejemplo:\n"
                            "(security=(my_wallet_directory=/Users/usted/Documents/wallet/principal_chile)(ssl_server_dn_match=yes))");
    help->setWordWrap(true); help->setStyleSheet(Theme::css(Theme::muted()));
    auto* lay = new QFormLayout(&d); lay->addRow("Carpeta TNS", row); lay->addRow(help); lay->addRow(check);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); lay->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept); connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() == QDialog::Accepted) { st.setValue("conn/defaultTns", tns->text().trimmed()); updateTnsInfo(); reloadAliases(); }
}

// Importa la lista del SQLTools de Windows (connections.xml). Muestra un resumen y pide confirmación antes de guardar nada.
void ConnectionManager::importFromSqlTools ()
{
    const QString start = QDir(QDir::homePath() + "/Sqltools_mac/importar").exists() ? QDir::homePath() + "/Sqltools_mac/importar" : QDir::homePath();
    const QString path = QFileDialog::getOpenFileName(this, "Importar conexiones de SQLTools (connections.xml)", start, "XML (*.xml);;Todos (*)");
    if (path.isEmpty()) return;
    QList<ConnectionProfile> list; QString err;
    if (!ConnectionImporter::parseSqlToolsXml(path, &list, &err)) { QMessageBox::warning(this, "Importar", err); return; }

    const auto existing = m_store->all();
    QList<ConnectionProfile> fresh; int dup = 0;
    for (auto p : list)
    {
        bool isDup = false; for (const auto& e : existing) isDup = isDup || ConnectionImporter::sameConnection(e, p);
        if (isDup) { ++dup; continue; }
        p.tnsAdmin.clear();                                       // usarán la carpeta TNS global (como en Windows, un solo TNS)
        fresh << p;
    }
    if (fresh.isEmpty()) { QMessageBox::information(this, "Importar", QString("No hay conexiones nuevas: las %1 del archivo ya existen.").arg(dup)); return; }

    QStringList sample; for (int i = 0; i < qMin(12, int(fresh.size())); ++i) sample << "  " + fresh[i].display();
    QMessageBox box(this); box.setWindowTitle("Importar conexiones"); box.setIcon(QMessageBox::Question);
    box.setText(QString("Se importarán %1 conexiones nuevas (%2 ya existían).").arg(fresh.size()).arg(dup));
    box.setInformativeText("Las contraseñas NO se importan (en el SQLTools de Windows están cifradas con su contraseña maestra). "
                           "Se pedirán la primera vez que conecte con cada una y quedarán en el Llavero de macOS."
                           + (globalTns().isEmpty() ? QString("\n\nAún no hay una carpeta TNS global: defínala con «Ajustes…» (la carpeta donde está su tnsnames.ora).")
                                                    : QString("\n\nLas conexiones usarán la carpeta TNS global: %1").arg(globalTns())));
    box.setDetailedText("Primeras conexiones:\n" + sample.join("\n") + (fresh.size() > 12 ? "\n  …" : ""));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel); box.button(QMessageBox::Yes)->setText("Importar"); box.button(QMessageBox::Cancel)->setText("Cancelar");
    if (box.exec() != QMessageBox::Yes) return;

    for (const auto& p : fresh) m_store->save(p);
    reloadList();
    m_result->setStyleSheet(Theme::css(Theme::ok())); m_result->setText(QString("Importadas %1 conexiones. Seleccione una y escriba su contraseña para conectar.").arg(fresh.size()));
}
