#pragma once
#include <QDialog>
#include <functional>
class QCheckBox; class QLabel; class QLineEdit; class SqlEditor;

// Buscar / Reemplazar sobre el editor de la pestaña activa (ventana no modal, como en SQLTools).
class FindReplaceDialog : public QDialog
{
    Q_OBJECT
public:
    FindReplaceDialog (std::function<SqlEditor*()> currentEditor, QWidget* parent = nullptr);
    void openFind (const QString& seed);
    void openReplace (const QString& seed);
    void findNext (bool backward);                      // F3 / Mayús+F3 (también sin abrir el diálogo)
    void findSelected (bool backward);                  // Cmd+F3: busca la palabra bajo el cursor
    QString text () const;
private:
    void setReplaceVisible (bool on);
    bool run (int action);                              // 0 siguiente, 1 anterior, 2 reemplazar, 3 reemplazar todo
    std::function<SqlEditor*()> m_cur;
    QLineEdit *m_find, *m_repl; QCheckBox *m_case, *m_word, *m_regex; QLabel *m_info, *m_replLabel;
    QWidget* m_replRow;
};
