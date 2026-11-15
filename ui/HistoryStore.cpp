#include "HistoryStore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

HistoryStore::HistoryStore (const QString& path, int maxEntries) : m_path(path), m_max(maxEntries) { load(); }

void HistoryStore::load ()
{
    QFile f(m_path); if (!f.open(QIODevice::ReadOnly)) return;
    for (const auto& v : QJsonDocument::fromJson(f.readAll()).array())
    {
        const QJsonObject o = v.toObject(); HistoryEntry e;
        e.when = QDateTime::fromString(o["when"].toString(), Qt::ISODate); e.sql = o["sql"].toString(); e.seconds = o["seconds"].toDouble();
        e.rows = o["rows"].toVariant().toLongLong(); e.error = o["error"].toString(); e.connection = o["connection"].toString();
        if (!e.sql.isEmpty()) m_list << e;
    }
}

void HistoryStore::save () const
{
    QJsonArray arr;
    for (const auto& e : m_list) arr.append(QJsonObject{ {"when", e.when.toString(Qt::ISODate)}, {"sql", e.sql}, {"seconds", e.seconds}, {"rows", e.rows}, {"error", e.error}, {"connection", e.connection} });
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path); if (f.open(QIODevice::WriteOnly)) { f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact)); f.commit(); }
}

void HistoryStore::add (const HistoryEntry& e)
{
    m_list << e;
    while (m_list.size() > m_max) m_list.removeFirst();
    if (m_persistent) save();
}

void HistoryStore::removeSql (const QString& sql) { for (int i = m_list.size() - 1; i >= 0; --i) if (m_list[i].sql == sql) m_list.removeAt(i); }

void HistoryStore::clear () { m_list.clear(); save(); }
