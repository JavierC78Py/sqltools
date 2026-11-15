#pragma once
#include <QDialog>
#include <QWidget>
#include "DdlOptions.h"
class QCheckBox; class QComboBox; class QSpinBox;

// Las opciones del DDL (páginas «Common DDL» y «Table Specific DDL»). Se usa en el diálogo local y en Settings.
class DdlOptionsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit DdlOptionsWidget (QWidget* parent = nullptr);
    void setValue (const DdlOptions& o);
    DdlOptions value () const;
private:
    QCheckBox *m_sqlPlus, *m_schema, *m_lower, *m_prompts, *m_slash, *m_grants, *m_comments, *m_specBody, *m_bodySpec, *m_viewTrig, *m_viewForce, *m_seqStart,
              *m_tableDef, *m_constraints, *m_indexes, *m_triggers, *m_noStorageCons, *m_commentsAfter;
    QComboBox* m_storage; QSpinBox* m_commentsPos;
};

// «Local DDL Preferences»: aparece al pedir Load DDL; los cambios valen solo para esa carga (los predeterminados están en Settings > DDL)
class DdlPreferencesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DdlPreferencesDialog (const DdlOptions& start, QWidget* parent = nullptr);
    DdlOptions options () const;
private:
    DdlOptionsWidget* m_w; QCheckBox* m_shift;
};
