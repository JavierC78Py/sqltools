#pragma once
#include <QDialog>
#include <functional>
class QCheckBox; class QLabel; class QLineEdit; class QTableWidget;

// Buscar texto en los archivos de una carpeta (Search > Find In Files): resultados con archivo, línea y texto; doble clic abre el archivo en esa línea.
class FindInFilesDialog : public QDialog
{
    Q_OBJECT
public:
    FindInFilesDialog (std::function<void(const QString&, int)> openAt, QWidget* parent = nullptr);
    void openWith (const QString& seed);
    static QList<QStringList> search (const QString& folder, const QStringList& masks, const QString& text, bool cs, bool regex, bool recursive, int maxHits, bool* truncated = nullptr);
private:
    void run ();
    std::function<void(const QString&, int)> m_open;
    QLineEdit *m_text, *m_folder, *m_masks; QCheckBox *m_case, *m_regex, *m_sub; QTableWidget* m_table; QLabel* m_info;
};
