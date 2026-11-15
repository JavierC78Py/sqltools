#pragma once
#include <QDialog>
#include <QWidget>
#include "ResultExporter.h"
class QCheckBox; class QComboBox; class QLineEdit;

// Opciones de exportación (formato, CSV, XML, SQL…). Se usa en el diálogo de exportar y en Settings.
class ExportOptionsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ExportOptionsWidget (bool withFormat, QWidget* parent = nullptr);
    void setValue (const ExportOptions& o);
    ExportOptions value () const;
private:
    QComboBox *m_format, *m_sep, *m_xmlCase; QCheckBox *m_header, *m_json, *m_always, *m_crlf, *m_bom, *m_ansi; QLineEdit *m_quote, *m_escape, *m_null, *m_prefix, *m_root, *m_record, *m_table, *m_dateFmt, *m_fname;
    bool m_withFormat;
};

class ExportDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ExportDialog (const ExportOptions& start, QWidget* parent = nullptr);
    ExportOptions options () const { return m_w->value(); }
private:
    ExportOptionsWidget* m_w;
};
