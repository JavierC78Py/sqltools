#pragma once
#include <QDateTime>
#include <QList>
#include <QSettings>
#include <QString>

// Una conexión guardada (todo menos la contraseña, que va al Llavero de macOS con la clave "id").
struct ConnectionProfile
{
    QString id;                  // identificador estable (UUID); clave del Llavero
    QString tag;                 // etiqueta para agrupar/identificar ("Km", "KmBr"…)
    QString user;
    QString alias;               // alias de tnsnames.ora
    QString tnsAdmin;            // carpeta con tnsnames.ora / wallet de esta conexión
    bool bypass = false;         // conectar sin tnsnames.ora (host/puerto/SID o servicio)
    QString host, port = "1521", sid;
    bool useService = true;
    QString mode = "Normal";     // Normal | SYSDBA | SYSOPER
    bool readOnly = false;
    bool slow = false;           // red lenta
    bool savePassword = true;
    int count = 0;               // veces que se ha conectado
    QDateTime lastUsed;

    QString display () const { return (tag.isEmpty() ? QString() : tag + "  ") + user + "@" + (bypass ? host + ":" + port + "/" + sid : alias); }
};

class ConnectionStore
{
public:
    explicit ConnectionStore (QSettings* settings, const QString& keychainService = "SQLToolsMac");
    QList<ConnectionProfile> all () const;
    void save (const ConnectionProfile& p);                 // crea o actualiza (por id)
    void remove (const QString& id);                        // también borra su contraseña del Llavero
    ConnectionProfile find (const QString& id) const;
    void touch (const QString& id);                         // +1 al contador y fecha de último uso

    bool setPassword (const QString& id, const QString& password);
    bool password (const QString& id, QString* out) const;
    void removePassword (const QString& id);
    static QString newId ();
private:
    void writeAll (const QList<ConnectionProfile>& list);
    QSettings* m_s; QString m_service;
};
