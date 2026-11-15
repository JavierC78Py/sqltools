#pragma once
#include <QDialog>
#include <QVector>
#include "SqlFormatter.h"
#include "SyntaxColors.h"
class QCheckBox; class QComboBox; class QFontComboBox; class QPlainTextEdit; class QPushButton; class QSpinBox; class QTabWidget; class QTableWidget; class SqlHighlighter; class ExportOptionsWidget;

// Settings: Format SQL (con vista previa), Editor, Colores, Ejecución e historial, Autocompletado y Exportación.
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog (QWidget* parent = nullptr, int tab = 0);
    void accept () override;
private:
    void updatePreview ();
    FormatOptions collectFormat () const;
    void applyFormat (const FormatOptions& o);
    void refreshProfiles (const QString& select = QString());
    void buildColors (QTabWidget* tabs);
    void loadColorTable ();
    void storeColorTable ();
    void refreshColorPreview ();
    QTabWidget* m_tabs;
    QCheckBox *m_wrap = nullptr, *m_viewDbl = nullptr, *m_viewPkg = nullptr; QSpinBox* m_viewMax = nullptr; QCheckBox* m_hlObjects = nullptr; QSpinBox *m_indent, *m_maxLine, *m_fontSize, *m_tabWidth, *m_histMax, *m_contInd, *m_andOrInd, *m_joinInd; QCheckBox *m_tabs2, *m_comma, *m_andOr, *m_oneItem, *m_kwAlone, *m_onNew, *m_spComma, *m_spParens, *m_alignDecl, *m_alignAsg, *m_blankExc, *m_blankBegin;
    QComboBox *m_fnCase, *m_idCase, *m_profile; QPlainTextEdit* m_source; bool m_loading = false;
    QComboBox *m_case, *m_style, *m_auto, *m_execStyle, *m_commitDisc;
    QFontComboBox* m_fontFamily;
    QPlainTextEdit* m_preview;
    // colores
    QComboBox* m_colorTheme; QTableWidget* m_colorTable; QPlainTextEdit* m_colorPreview; SqlHighlighter* m_colorHl; QVector<SynStyle> m_light, m_dark; int m_shownTheme = 0;
    // ejecución / historial / autocompletado
    class DdlOptionsWidget* m_ddlW; QCheckBox* m_ddlShift;
    QCheckBox *m_autocommit, *m_quickLower, *m_histOn, *m_histValid, *m_histDistinct, *m_histPersist; QVector<QCheckBox*> m_acTypes;
    // exportación
    QComboBox *m_expFmt[3]; QCheckBox* m_expHdr[3]; ExportOptionsWidget* m_expCommon;
};
