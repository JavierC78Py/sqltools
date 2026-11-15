#include "FindInFilesDialog.h"
#include <QCheckBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

// Devuelve filas {archivo, línea, texto}. Los archivos se leen como UTF-8 y, si no lo son, como Latin-1 (scripts de Windows).
QList<QStringList> FindInFilesDialog::search (const QString& folder, const QStringList& masks, const QString& text, bool cs, bool regex, bool recursive, int maxHits, bool* truncated)
{
    QList<QStringList> out; if (truncated) *truncated = false;
    if (text.isEmpty() || !QDir(folder).exists()) return out;
    QRegularExpression re(regex ? text : QRegularExpression::escape(text), cs ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption);
    if (!re.isValid()) return out;
    QDirIterator it(folder, masks, QDir::Files | QDir::Readable, recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
    while (it.hasNext())
    {
        const QString path = it.next(); QFile f(path); if (!f.open(QIODevice::ReadOnly)) continue;
        const QByteArray data = f.readAll(); if (data.contains('\0')) continue;                          // binarios fuera
        QString txt = QString::fromUtf8(data); if (txt.contains(QChar::ReplacementCharacter)) txt = QString::fromLatin1(data);
        int line = 0;
        for (const QString& l : txt.split('\n')) { ++line; if (re.match(l).hasMatch()) { out.push_back({path, QString::number(line), l.trimmed().left(300)}); if (out.size() >= maxHits) { if (truncated) *truncated = true; return out; } } }
    }
    return out;
}

FindInFilesDialog::FindInFilesDialog (std::function<void(const QString&, int)> openAt, QWidget* parent) : QDialog(parent), m_open(std::move(openAt))
{
    setWindowTitle("Find in Files"); setWindowFlag(Qt::Tool); resize(900, 520);
    QSettings st;
    m_text = new QLineEdit(st.value("fif/text").toString()); m_folder = new QLineEdit(st.value("fif/folder", QDir::homePath()).toString());
    m_masks = new QLineEdit(st.value("fif/masks", "*.sql;*.pls;*.pks;*.pkb;*.plsql;*.txt").toString());
    m_case = new QCheckBox("Distinguir mayúsculas"); m_regex = new QCheckBox("Expresión regular"); m_sub = new QCheckBox("Incluir subcarpetas"); m_sub->setChecked(true);
    auto* pick = new QPushButton("Elegir…"); auto* go = new QPushButton("Buscar"); go->setDefault(true);
    auto* fr = new QHBoxLayout(); fr->addWidget(m_folder); fr->addWidget(pick);
    auto* form = new QFormLayout(); form->addRow("Buscar", m_text); form->addRow("Carpeta", fr); form->addRow("Archivos", m_masks);
    auto* opts = new QHBoxLayout(); opts->addWidget(m_case); opts->addWidget(m_regex); opts->addWidget(m_sub); opts->addStretch(1); opts->addWidget(go);
    m_table = new QTableWidget(0, 3); m_table->setHorizontalHeaderLabels({"Archivo", "Línea", "Texto"});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers); m_table->setSelectionBehavior(QAbstractItemView::SelectRows); m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(true); m_table->setColumnWidth(0, 360); m_table->setColumnWidth(1, 60);
    m_info = new QLabel(); auto* lay = new QVBoxLayout(this); lay->addLayout(form); lay->addLayout(opts); lay->addWidget(m_table, 1); lay->addWidget(m_info);
    connect(go, &QPushButton::clicked, this, &FindInFilesDialog::run); connect(m_text, &QLineEdit::returnPressed, this, &FindInFilesDialog::run);
    connect(pick, &QPushButton::clicked, this, [this]{ const QString d = QFileDialog::getExistingDirectory(this, "Carpeta donde buscar", m_folder->text()); if (!d.isEmpty()) m_folder->setText(d); });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int){ m_open(m_table->item(row, 0)->data(Qt::UserRole).toString(), m_table->item(row, 1)->text().toInt()); });
}

void FindInFilesDialog::openWith (const QString& seed) { if (!seed.isEmpty() && !seed.contains(QChar(0x2029))) m_text->setText(seed); show(); raise(); activateWindow(); m_text->setFocus(); m_text->selectAll(); }

void FindInFilesDialog::run ()
{
    QSettings st; st.setValue("fif/text", m_text->text()); st.setValue("fif/folder", m_folder->text()); st.setValue("fif/masks", m_masks->text());
    bool trunc = false; const QString folder = m_folder->text().trimmed();
    const auto hits = search(folder, m_masks->text().split(QRegularExpression("[;,\\s]+"), Qt::SkipEmptyParts), m_text->text(), m_case->isChecked(), m_regex->isChecked(), m_sub->isChecked(), 5000, &trunc);
    m_table->setRowCount(0);
    for (const auto& h : hits)
    {
        const int r = m_table->rowCount(); m_table->insertRow(r);
        auto* f = new QTableWidgetItem(QDir(folder).relativeFilePath(h[0])); f->setData(Qt::UserRole, h[0]); f->setToolTip(h[0]);
        auto* l = new QTableWidgetItem(h[1]); l->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(r, 0, f); m_table->setItem(r, 1, l); m_table->setItem(r, 2, new QTableWidgetItem(h[2]));
    }
    m_info->setText(hits.isEmpty() ? "Sin resultados." : QString("%1 coincidencia(s)%2. Doble clic para abrir.").arg(hits.size()).arg(trunc ? " (se muestran solo las primeras 5000)" : ""));
}
