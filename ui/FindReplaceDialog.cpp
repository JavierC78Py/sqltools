#include "FindReplaceDialog.h"
#include "Theme.h"
#include "SqlEditor.h"
#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

FindReplaceDialog::FindReplaceDialog (std::function<SqlEditor*()> cur, QWidget* parent) : QDialog(parent), m_cur(std::move(cur))
{
    setWindowTitle("Buscar y reemplazar"); setModal(false);
    setWindowFlag(Qt::Tool);
    QSettings st;
    m_find = new QLineEdit(st.value("find/text").toString()); m_repl = new QLineEdit(st.value("find/with").toString());
    m_case = new QCheckBox("Distinguir mayúsculas"); m_word = new QCheckBox("Palabra completa"); m_regex = new QCheckBox("Expresión regular");
    m_case->setChecked(st.value("find/case").toBool()); m_word->setChecked(st.value("find/word").toBool()); m_regex->setChecked(st.value("find/regex").toBool());
    m_info = new QLabel(); m_info->setStyleSheet(Theme::css(Theme::muted()));
    auto* next = new QPushButton("Siguiente"); auto* prev = new QPushButton("Anterior"); auto* rep = new QPushButton("Reemplazar"); auto* all = new QPushButton("Reemplazar todo");
    next->setDefault(true);
    auto* form = new QFormLayout(); form->addRow("Buscar", m_find);
    m_replLabel = new QLabel("Reemplazar con"); form->addRow(m_replLabel, m_repl);
    auto* opts = new QHBoxLayout(); opts->addWidget(m_case); opts->addWidget(m_word); opts->addWidget(m_regex);
    auto* b1 = new QHBoxLayout(); b1->addWidget(prev); b1->addWidget(next);
    m_replRow = new QWidget(); auto* b2 = new QHBoxLayout(m_replRow); b2->setContentsMargins(0, 0, 0, 0); b2->addWidget(rep); b2->addWidget(all);
    auto* lay = new QVBoxLayout(this); lay->addLayout(form); lay->addLayout(opts); lay->addLayout(b1); lay->addWidget(m_replRow); lay->addWidget(m_info);
    connect(next, &QPushButton::clicked, this, [this]{ run(0); });
    connect(prev, &QPushButton::clicked, this, [this]{ run(1); });
    connect(rep, &QPushButton::clicked, this, [this]{ run(2); });
    connect(all, &QPushButton::clicked, this, [this]{ run(3); });
    connect(m_find, &QLineEdit::returnPressed, this, [this]{ run(0); });
    setReplaceVisible(false); resize(460, 170);
}

QString FindReplaceDialog::text () const { return m_find->text(); }

void FindReplaceDialog::setReplaceVisible (bool on)
{
    m_repl->setVisible(on); m_replLabel->setVisible(on); m_replRow->setVisible(on);
    setWindowTitle(on ? "Reemplazar" : "Buscar"); adjustSize();
}

void FindReplaceDialog::openFind (const QString& seed)
{
    if (!seed.isEmpty() && !seed.contains(QChar(0x2029))) m_find->setText(seed);
    setReplaceVisible(false); show(); raise(); activateWindow(); m_find->setFocus(); m_find->selectAll();
}
void FindReplaceDialog::openReplace (const QString& seed)
{
    if (!seed.isEmpty() && !seed.contains(QChar(0x2029))) m_find->setText(seed);
    setReplaceVisible(true); show(); raise(); activateWindow(); m_find->setFocus(); m_find->selectAll();
}

bool FindReplaceDialog::run (int action)
{
    SqlEditor* e = m_cur(); if (!e) return false;
    QSettings st; st.setValue("find/text", m_find->text()); st.setValue("find/with", m_repl->text());
    st.setValue("find/case", m_case->isChecked()); st.setValue("find/word", m_word->isChecked()); st.setValue("find/regex", m_regex->isChecked());
    const QString t = m_find->text(); bool ok = true;
    if (action == 3) { const int n = e->replaceAll(t, m_repl->text(), m_case->isChecked(), m_word->isChecked(), m_regex->isChecked()); m_info->setText(QString("%1 reemplazo(s).").arg(n)); return n > 0; }
    if (action == 2) ok = e->replaceCurrent(t, m_repl->text(), m_case->isChecked(), m_word->isChecked(), m_regex->isChecked());
    else ok = e->findText(t, action == 1, m_case->isChecked(), m_word->isChecked(), m_regex->isChecked());
    m_info->setText(ok ? QString() : "No se encontró el texto.");
    return ok;
}

void FindReplaceDialog::findNext (bool backward)
{
    if (m_find->text().isEmpty()) { openFind(QString()); return; }
    run(backward ? 1 : 0);
}

void FindReplaceDialog::findSelected (bool backward)
{
    SqlEditor* e = m_cur(); if (!e) return;
    const QString w = e->wordUnderCursor(); if (w.isEmpty()) return;
    m_find->setText(w); run(backward ? 1 : 0);
}
