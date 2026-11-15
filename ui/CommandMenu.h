#pragma once
// Barra de menús y barras de iconos generadas a partir del inventario de SQLTools (ui/spec/menu_spec.json):
// los mismos 12 menús, comandos, etiquetas, iconos y atajos que la versión de Windows. Un comando sin función asociada
// aparece desactivado (y su atajo no hace nada), así la lista completa es visible desde el primer día.
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <functional>
class QAction; class QMainWindow; class QMenu; class QToolBar;

class CommandMenu : public QObject
{
    Q_OBJECT
public:
    enum Req { None = 0, NeedConnection = 1, NeedIdle = 2, NeedBusy = 4 };   // cuándo está activo un comando implementado

    explicit CommandMenu (QMainWindow* window);
    bool load (QString* error = nullptr);                                      // lee el JSON de recursos y crea todas las acciones

    // Asocia una función a un comando. checkable: conmutador (View, Read Only…). Devuelve la acción.
    QAction* bind (const QString& id, std::function<void()> fn, int req = None, bool checkable = false, bool checked = false);
    QAction* action (const QString& id) const { return m_actions.value(id); }
    bool isBound (const QString& id) const { return m_bound.contains(id); }

    void buildMenuBar ();
    QToolBar* buildToolBar (const QString& specName, const QString& title);
    void fillToolBar (QToolBar* bar, const QString& specName);                  // llena una barra que ya existe (p.ej. la del panel de resultados)     // "IDT_FILE", "IDT_SQL", "IDT_CONNECT", "IDT_BOOKLET"
    void update (bool connected, bool busy);                                    // activa/desactiva según el estado

    // Windows -> Mac. "Ctrl+Q,R" -> secuencia de Qt con los ajustes para no chocar con atajos del sistema.
    static QList<QKeySequence> macSequences (const QStringList& windowsKeys, QStringList* notes = nullptr);
    QStringList dump () const;                                                  // "ID\tetiqueta\tmenú\tatajos Windows\tatajos Mac\testado" (documentación)
    QStringList conflicts () const { return m_conflicts; }

private:
    void createActions (const QJsonArray& items, const QString& path);
    QMenu* buildMenu (const QJsonArray& items, QMenu* parent);
    QMainWindow* m_win;
    QJsonObject m_spec;
    QHash<QString, QAction*> m_actions;
    QHash<QString, int> m_bound;                 // id -> Req
    QHash<QString, QString> m_menuPath;          // id -> "File > Workspace"
    QSet<QString> m_usedSequences;
    QStringList m_conflicts;
};
