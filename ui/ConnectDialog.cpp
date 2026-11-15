#include "ConnectDialog.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QDir>

// Oracle no expande "~": se convierte aquí a la ruta completa de la carpeta personal.
static QString expandPath (QString p)
{
    p = p.trimmed();
    if (p == "~" || p.startsWith("~/")) p = QDir::homePath() + p.mid(1);
    return QDir::cleanPath(p);
}

ConnectDialog::ConnectDialog (QWidget* parent) : QDialog(parent)
{
    setWindowTitle("Conectar a Oracle");
    QSettings st;                               // se recuerdan usuario, alias y carpeta; NUNCA la contraseña
    m_user = new QLineEdit(st.value("conn/user").toString());
    m_pass = new QLineEdit(); m_pass->setEchoMode(QLineEdit::Password);
    m_alias = new QComboBox(); m_alias->setEditable(true);
    m_tns = new QLineEdit(st.value("conn/tns").toString());
    m_tns->setPlaceholderText("Carpeta con tnsnames.ora y el wallet (TNS_ADMIN)");
    auto* browseBtn = new QPushButton("Elegir…");
    auto* tnsRow = new QHBoxLayout(); tnsRow->addWidget(m_tns); tnsRow->addWidget(browseBtn);

    auto* form = new QFormLayout(this);
    form->addRow("Usuario", m_user);
    form->addRow("Contraseña", m_pass);
    form->addRow("Alias TNS", m_alias);
    form->addRow("Carpeta TNS / wallet", tnsRow);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText("Conectar"); bb->button(QDialogButtonBox::Cancel)->setText("Cancelar");
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(browseBtn, &QPushButton::clicked, this, &ConnectDialog::browse);
    connect(m_tns, &QLineEdit::editingFinished, this, &ConnectDialog::reloadAliases);
    reloadAliases();
    m_alias->setCurrentText(st.value("conn/alias").toString());
    (m_user->text().isEmpty() ? m_user : m_pass)->setFocus();
    setMinimumWidth(520);
}

QString ConnectDialog::user () const { return m_user->text().trimmed(); }
QString ConnectDialog::password () const { return m_pass->text(); }
QString ConnectDialog::alias () const { return m_alias->currentText().trimmed(); }
QString ConnectDialog::tnsAdmin () const { return m_tns->text().trimmed().isEmpty() ? QString() : expandPath(m_tns->text()); }

void ConnectDialog::browse ()
{
    QString d = QFileDialog::getExistingDirectory(this, "Carpeta con tnsnames.ora / wallet", tnsAdmin());
    if (!d.isEmpty()) { m_tns->setText(d); reloadAliases(); }
}

void ConnectDialog::reloadAliases ()
{
    const QString cur = m_alias->currentText();
    m_alias->clear();
    QFile f(tnsAdmin() + "/tnsnames.ora");
    if (f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        static const QRegularExpression re(R"(^([A-Za-z][A-Za-z0-9_.\-]*)\s*=)");
        while (!f.atEnd()) { auto m = re.match(QString::fromUtf8(f.readLine())); if (m.hasMatch()) m_alias->addItem(m.captured(1)); }
    }
    m_alias->setCurrentText(cur);
}
