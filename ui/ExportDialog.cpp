#include "ExportDialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QVBoxLayout>

ExportOptionsWidget::ExportOptionsWidget (bool withFormat, QWidget* parent) : QWidget(parent), m_withFormat(withFormat)
{
    auto* lay = new QVBoxLayout(this); lay->setContentsMargins(0, 0, 0, 0);
    m_format = new QComboBox(); for (int i = 0; i < ExportOptions::FormatCount; ++i) m_format->addItem(ExportOptions::formatName(i));
    m_header = new QCheckBox("Incluir los nombres de columna"); m_json = new QCheckBox("JSON con sangría (legible)");
    if (withFormat) { auto* f = new QFormLayout(); f->addRow("Formato", m_format); f->addRow(m_header); f->addRow(m_json); lay->addLayout(f); }
    // CSV
    auto* g1 = new QGroupBox("CSV / texto"); auto* f1 = new QFormLayout(g1);
    m_sep = new QComboBox(); m_sep->setEditable(true); m_sep->addItems({",", ";", "|", "TAB"});
    m_quote = new QLineEdit(); m_quote->setMaximumWidth(60); m_quote->setToolTip("Carácter que encierra los campos; vacío = sin comillas");
    m_escape = new QLineEdit(); m_escape->setMaximumWidth(60); m_escape->setToolTip("Carácter que precede a una comilla dentro del campo. Igual a la comilla = se duplica (estándar)");
    m_always = new QCheckBox("Entrecomillar siempre los textos"); m_null = new QLineEdit(); m_null->setPlaceholderText("(vacío)"); m_prefix = new QLineEdit(); m_prefix->setMaximumWidth(60); m_prefix->setToolTip("Se antepone a los textos que empiezan por = + @, para que Excel no los tome por fórmulas");
    m_crlf = new QCheckBox("Saltos de línea de Windows (CRLF)"); m_bom = new QCheckBox("Escribir marca BOM de UTF-8 (para que Excel lea las tildes)");
    f1->addRow("Separador", m_sep); f1->addRow("Comillas", m_quote); f1->addRow("Escape de comillas", m_escape); f1->addRow(m_always); f1->addRow("Texto para NULL", m_null); f1->addRow("Prefijo anti-fórmula", m_prefix); f1->addRow(m_crlf); f1->addRow(m_bom);
    lay->addWidget(g1);
    // XML
    auto* g2 = new QGroupBox("XML"); auto* f2 = new QFormLayout(g2); m_root = new QLineEdit(); m_record = new QLineEdit(); m_xmlCase = new QComboBox(); m_xmlCase->addItems({"MAYÚSCULAS", "minúsculas", "Como está"});
    f2->addRow("Elemento raíz", m_root); f2->addRow("Elemento por fila", m_record); f2->addRow("Nombres de columna", m_xmlCase); lay->addWidget(g2);
    // SQL
    auto* g3 = new QGroupBox("INSERT de SQL"); auto* f3 = new QFormLayout(g3); m_table = new QLineEdit(); m_ansi = new QCheckBox("Fechas como literales ANSI (DATE '…' / TIMESTAMP '…')"); m_dateFmt = new QLineEdit();
    f3->addRow("Nombre de la tabla", m_table); f3->addRow(m_ansi); f3->addRow("Formato de fecha (TO_DATE)", m_dateFmt); lay->addWidget(g3);
    // archivo
    auto* g4 = new QGroupBox("Archivo"); auto* f4 = new QFormLayout(g4); m_fname = new QLineEdit(); m_fname->setToolTip("Puede usar {fecha}, {hora} y {tabla}");
    f4->addRow("Nombre sugerido", m_fname); f4->addRow(new QLabel("Variables: {fecha} {hora} {tabla}")); lay->addWidget(g4);
}

void ExportOptionsWidget::setValue (const ExportOptions& o)
{
    m_format->setCurrentIndex(o.format); m_header->setChecked(o.header); m_json->setChecked(o.jsonPretty);
    m_sep->setCurrentText(o.sep == "\t" ? "TAB" : o.sep); m_quote->setText(o.quote); m_escape->setText(o.escape); m_always->setChecked(o.quoteAlways); m_null->setText(o.nullText); m_prefix->setText(o.prefixChar);
    m_crlf->setChecked(o.crlf); m_bom->setChecked(o.bom); m_root->setText(o.xmlRoot); m_record->setText(o.xmlRecord); m_xmlCase->setCurrentIndex(o.xmlCase);
    m_table->setText(o.sqlTable); m_ansi->setChecked(o.ansiLiterals); m_dateFmt->setText(o.sqlDateFormat); m_fname->setText(o.filenameTemplate);
}

ExportOptions ExportOptionsWidget::value () const
{
    ExportOptions o; o.format = m_format->currentIndex(); o.header = m_header->isChecked(); o.jsonPretty = m_json->isChecked();
    const QString sp = m_sep->currentText(); o.sep = sp == "TAB" ? "\t" : (sp.isEmpty() ? "," : sp); o.quote = m_quote->text(); o.escape = m_escape->text(); o.quoteAlways = m_always->isChecked(); o.nullText = m_null->text(); o.prefixChar = m_prefix->text();
    o.crlf = m_crlf->isChecked(); o.bom = m_bom->isChecked(); o.xmlRoot = m_root->text().isEmpty() ? "RESULT" : m_root->text(); o.xmlRecord = m_record->text().isEmpty() ? "ROW" : m_record->text(); o.xmlCase = m_xmlCase->currentIndex();
    o.sqlTable = m_table->text().trimmed().isEmpty() ? "TABLE_NAME" : m_table->text().trimmed(); o.ansiLiterals = m_ansi->isChecked(); o.sqlDateFormat = m_dateFmt->text().isEmpty() ? "YYYY.MM.DD HH24:MI:SS" : m_dateFmt->text(); o.filenameTemplate = m_fname->text();
    return o;
}

ExportDialog::ExportDialog (const ExportOptions& start, QWidget* parent) : QDialog(parent)
{
    setWindowTitle("Exportar resultados"); auto* lay = new QVBoxLayout(this); m_w = new ExportOptionsWidget(true); m_w->setValue(start); lay->addWidget(m_w);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); bb->button(QDialogButtonBox::Ok)->setText("Exportar…"); lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept); connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    resize(520, 760);
}
