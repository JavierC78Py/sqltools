#include "DdlPreferencesDialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

DdlOptionsWidget::DdlOptionsWidget (QWidget* parent) : QWidget(parent)
{
    auto* lay = new QHBoxLayout(this); lay->setContentsMargins(0, 0, 0, 0);
    auto chk = [](const QString& t) { return new QCheckBox(t); };
    auto* g1 = new QGroupBox("Common DDL"); auto* l1 = new QVBoxLayout(g1);
    m_sqlPlus = chk("SQL Plus compatibility"); m_schema = chk("Prefix with Schema Name"); m_lower = chk("Lowercase Object's Names"); m_prompts = chk("Generate PROMPT(s)"); m_slash = chk("Always use / as end of statement");
    m_grants = chk("Include Grants"); m_comments = chk("Include Comments"); m_specBody = chk("Package Specification with Body"); m_bodySpec = chk("Package Body with Specification");
    m_viewTrig = chk("View with Instead Triggers"); m_viewForce = chk("View with FORCE"); m_seqStart = chk("Sequences with START WITH");
    for (auto* c : {m_sqlPlus, m_schema, m_lower, m_prompts, m_slash, m_grants, m_comments, m_specBody, m_bodySpec, m_viewTrig, m_viewForce, m_seqStart}) l1->addWidget(c); l1->addStretch(1);
    auto* g2 = new QGroupBox("Table Specific DDL"); auto* l2 = new QVBoxLayout(g2);
    m_tableDef = chk("Table definition"); m_constraints = chk("Constraints"); m_indexes = chk("Indexes"); m_triggers = chk("Triggers");
    m_storage = new QComboBox(); m_storage->addItems({"Sin cláusulas físicas", "STORAGE y TABLESPACE", "Siempre con TABLESPACE"});
    m_noStorageCons = chk("Sin STORAGE en claves primarias y únicas"); m_commentsAfter = chk("Comentarios de columna al final de la línea"); m_commentsPos = new QSpinBox(); m_commentsPos->setRange(10, 200);
    for (auto* c : {m_tableDef, m_constraints, m_indexes, m_triggers}) l2->addWidget(c);
    l2->addWidget(new QLabel("Cláusulas físicas:")); l2->addWidget(m_storage); l2->addWidget(m_noStorageCons); l2->addWidget(m_commentsAfter);
    auto* pos = new QHBoxLayout(); pos->addWidget(new QLabel("   en la columna")); pos->addWidget(m_commentsPos); pos->addStretch(1); l2->addLayout(pos); l2->addStretch(1);
    lay->addWidget(g1); lay->addWidget(g2);
}

void DdlOptionsWidget::setValue (const DdlOptions& o)
{
    m_sqlPlus->setChecked(o.sqlPlus); m_schema->setChecked(o.schemaPrefix); m_lower->setChecked(o.lowerNames); m_prompts->setChecked(o.prompts); m_slash->setChecked(o.slashAlways); m_grants->setChecked(o.grants); m_comments->setChecked(o.comments);
    m_specBody->setChecked(o.specWithBody); m_bodySpec->setChecked(o.bodyWithSpec); m_viewTrig->setChecked(o.viewTriggers); m_viewForce->setChecked(o.viewForce); m_seqStart->setChecked(o.sequenceStart);
    m_tableDef->setChecked(o.tableDefinition); m_constraints->setChecked(o.constraints); m_indexes->setChecked(o.indexes); m_triggers->setChecked(o.triggers); m_storage->setCurrentIndex(qBound(0, o.storage, 2));
    m_noStorageCons->setChecked(o.noStorageForConstraint); m_commentsAfter->setChecked(o.commentsAfterColumn); m_commentsPos->setValue(o.commentsPos);
}

DdlOptions DdlOptionsWidget::value () const
{
    DdlOptions o; o.sqlPlus = m_sqlPlus->isChecked(); o.schemaPrefix = m_schema->isChecked(); o.lowerNames = m_lower->isChecked(); o.prompts = m_prompts->isChecked(); o.slashAlways = m_slash->isChecked(); o.grants = m_grants->isChecked(); o.comments = m_comments->isChecked();
    o.specWithBody = m_specBody->isChecked(); o.bodyWithSpec = m_bodySpec->isChecked(); o.viewTriggers = m_viewTrig->isChecked(); o.viewForce = m_viewForce->isChecked(); o.sequenceStart = m_seqStart->isChecked();
    o.tableDefinition = m_tableDef->isChecked(); o.constraints = m_constraints->isChecked(); o.indexes = m_indexes->isChecked(); o.triggers = m_triggers->isChecked(); o.storage = m_storage->currentIndex();
    o.noStorageForConstraint = m_noStorageCons->isChecked(); o.commentsAfterColumn = m_commentsAfter->isChecked(); o.commentsPos = m_commentsPos->value();
    return o;
}

DdlPreferencesDialog::DdlPreferencesDialog (const DdlOptions& start, QWidget* parent) : QDialog(parent)
{
    setWindowTitle("Local DDL Preferences"); auto* lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel("Estas opciones valen solo para esta carga. Los valores predeterminados se cambian en Settings > DDL."));
    m_w = new DdlOptionsWidget(); m_w->setValue(start); lay->addWidget(m_w);
    m_shift = new QCheckBox("Only show this Dialog when the Shift key is down"); m_shift->setChecked(start.onlyWithShift); lay->addWidget(m_shift);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); lay->addWidget(bb); connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept); connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

DdlOptions DdlPreferencesDialog::options () const { DdlOptions o = m_w->value(); o.onlyWithShift = m_shift->isChecked(); return o; }
