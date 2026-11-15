#pragma once
#include <QSet>
#include <QWidget>
#include <QHash>
#include <QStringList>
#include <QVector>
class QComboBox; class QLabel; class QMenu; class QTableView; class QToolButton; class QButtonGroup;
class FilterHeader; class ObjectListModel; class ObjectListProxy; struct ObjectListDef;

// Lista de objetos del esquema como en SQLTools para Windows: una pestaña por tipo de objeto (tablas, claves, índices, triggers, código,
// tipos, secuencias, vistas, sinónimos…), cada una con sus columnas, filtro por columna, orden, y menú contextual según el tipo.
class ObjectBrowser : public QWidget
{
    Q_OBJECT
public:
    explicit ObjectBrowser (QWidget* parent = nullptr);
    void clear ();
    void setSchemas (const QStringList& names, const QString& current);
    QString owner () const;                                                   // esquema que se está mostrando
    void setObjects (const QVector<QStringList>& rows);                       // {tipo, nombre, estado}: base del autocompletado y de typeOf
    void setColumns (const QString&, const QVector<QStringList>&) {}          // (la lista de columnas ahora está en el Object Viewer)
    void expandObject (const QString&) {}
    QString summary () const;                                                 // "Tables 3, Packages 2…" (diagnóstico)
    QString typeOf (const QString& name) const;                               // tipo del objeto con ese nombre (vacío si no está)
    QStringList objectNames (const QString& type = QString()) const;          // para el autocompletado
    QMenu* contextMenuFor (const QString& type, const QString& name);         // menú contextual de ese objeto, sin mostrarlo (capturas del manual; quien llama lo borra)
    void setListRows (const QString& key, const QVector<QStringList>& rows, const QString& error);   // respuesta del servidor a listRequested
    void setConnected (bool on) { m_connected = on; }
    void showTab (int index);
signals:
    void schemaChanged (const QString& owner);
    void refreshRequested ();
    void listRequested (const QString& owner, const QString& key, const QString& sql);
    void columnsRequested (const QString& table);
    void openSourceRequested (const QString& type, const QString& name);
    void selectAllRequested (const QString& name);
    void describeRequested (const QString& name);
    // Acciones del menú contextual. objects = "TIPO\x1fNOMBRE" de cada objeto seleccionado
    void actionRequested (const QString& action, const QStringList& objects);
private:
    void requestList (int index, bool background = false);
    void prefetchNext ();
public:
    void holdPrefetch (int ms);                      // pausa la precarga (p. ej. mientras se lee un objeto con F12)
private:
    qint64 m_holdUntil = 0;
    void updateStatus ();
    void applyView ();
    QMenu* buildMenu (const QStringList& objs, bool anyInvalid, const QString& rowsText);
    QStringList selectedObjects (bool* anyInvalid = nullptr, QString* text = nullptr) const;
    void showMenuAt (const QPoint& pos);
    void applyModernStyle ();
    void rebuildTabButtons ();
    QComboBox* m_schema; QTableView* m_view; FilterHeader* m_header; QLabel* m_status;
    QToolButton *m_refresh, *m_listBtn, *m_reportBtn, *m_validBtn, *m_invalidBtn, *m_filterBtn, *m_settingsBtn;
    QWidget* m_tabHost; QVector<QToolButton*> m_tabButtons; QButtonGroup* m_tabGroup;
    QVector<ObjectListModel*> m_models; QVector<ObjectListProxy*> m_proxies; QVector<bool> m_loaded; int m_inFlight = -1; QSet<int> m_failed;
    int m_tab = 0; bool m_detail = true, m_titles = true, m_connected = false, m_sized = false;
    QVector<QStringList> m_rows;
};
