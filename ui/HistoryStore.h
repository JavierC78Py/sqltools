#pragma once
// Historial de sentencias ejecutadas (se guarda en un archivo JSON y sobrevive a los reinicios).
#include <QDateTime>
#include <QList>
#include <QString>

struct HistoryEntry { QDateTime when; QString sql; double seconds = 0; qint64 rows = 0; QString error; QString connection; };

class HistoryStore
{
public:
    explicit HistoryStore (const QString& path, int maxEntries = 2000);
    const QList<HistoryEntry>& entries () const { return m_list; }      // la más antigua primero
    void add (const HistoryEntry& e);
    void clear ();
    void setMax (int n) { m_max = qMax(10, n); while (m_list.size() > m_max) m_list.removeFirst(); }
    void setPersistent (bool on) { m_persistent = on; }
    void removeSql (const QString& sql);                  // para «sin repetidos»
private:
    void load ();
    void save () const;
    bool m_persistent = true;
    QString m_path; int m_max; QList<HistoryEntry> m_list;
};
