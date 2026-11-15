#include "ConnectionStore.h"
#include "Keychain.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

ConnectionStore::ConnectionStore (QSettings* s, const QString& service) : m_s(s), m_service(service) {}

QString ConnectionStore::newId () { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QList<ConnectionProfile> ConnectionStore::all () const
{
    QList<ConnectionProfile> out;
    const auto arr = QJsonDocument::fromJson(m_s->value("connections/list").toString().toUtf8()).array();
    for (const auto& v : arr)
    {
        const QJsonObject o = v.toObject(); ConnectionProfile p;
        p.id = o["id"].toString(); p.tag = o["tag"].toString(); p.user = o["user"].toString(); p.alias = o["alias"].toString();
        p.tnsAdmin = o["tnsAdmin"].toString(); p.bypass = o["bypass"].toBool(); p.host = o["host"].toString(); p.port = o["port"].toString("1521");
        p.sid = o["sid"].toString(); p.useService = o["useService"].toBool(true); p.mode = o["mode"].toString("Normal");
        p.readOnly = o["readOnly"].toBool(); p.slow = o["slow"].toBool(); p.savePassword = o["savePassword"].toBool(true);
        p.count = o["count"].toInt(); p.lastUsed = QDateTime::fromString(o["lastUsed"].toString(), Qt::ISODate);
        if (!p.id.isEmpty()) out << p;
    }
    return out;
}

void ConnectionStore::writeAll (const QList<ConnectionProfile>& list)
{
    QJsonArray arr;
    for (const auto& p : list)
        arr.append(QJsonObject{ {"id", p.id}, {"tag", p.tag}, {"user", p.user}, {"alias", p.alias}, {"tnsAdmin", p.tnsAdmin}, {"bypass", p.bypass},
            {"host", p.host}, {"port", p.port}, {"sid", p.sid}, {"useService", p.useService}, {"mode", p.mode}, {"readOnly", p.readOnly}, {"slow", p.slow},
            {"savePassword", p.savePassword}, {"count", p.count}, {"lastUsed", p.lastUsed.isValid() ? p.lastUsed.toString(Qt::ISODate) : QString()} });
    m_s->setValue("connections/list", QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    m_s->sync();
}

void ConnectionStore::save (const ConnectionProfile& p)
{
    auto list = all(); bool found = false;
    for (auto& e : list) if (e.id == p.id) { e = p; found = true; }
    if (!found) list << p;
    writeAll(list);
}

void ConnectionStore::remove (const QString& id)
{
    auto list = all(); list.erase(std::remove_if(list.begin(), list.end(), [&](const ConnectionProfile& p){ return p.id == id; }), list.end());
    writeAll(list); Keychain::remove(m_service, id);
}

ConnectionProfile ConnectionStore::find (const QString& id) const { for (const auto& p : all()) if (p.id == id) return p; return {}; }

void ConnectionStore::touch (const QString& id)
{
    auto list = all();
    for (auto& e : list) if (e.id == id) { ++e.count; e.lastUsed = QDateTime::currentDateTime(); }
    writeAll(list);
}

bool ConnectionStore::setPassword (const QString& id, const QString& pw) { return Keychain::set(m_service, id, pw); }
bool ConnectionStore::password (const QString& id, QString* out) const { return Keychain::get(m_service, id, out); }
void ConnectionStore::removePassword (const QString& id) { Keychain::remove(m_service, id); }
