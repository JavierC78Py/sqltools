#pragma once
#include <QAbstractTableModel>
#include <QIcon>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QVector>

// Filas de una pestaña de la lista de objetos
class ObjectListModel : public QAbstractTableModel
{
public:
    ObjectListModel (const QStringList& headers, const QString& kinds, int statusCol, const QIcon& icon, QObject* parent = nullptr);
    int rowCount (const QModelIndex& p = QModelIndex()) const override { return p.isValid() ? 0 : m_rows.size(); }
    int columnCount (const QModelIndex& p = QModelIndex()) const override { return p.isValid() ? 0 : m_headers.size(); }
    QVariant data (const QModelIndex& i, int role) const override;
    QVariant headerData (int s, Qt::Orientation o, int role) const override { return o == Qt::Horizontal && role == Qt::DisplayRole ? QVariant(m_headers.value(s)) : QVariant(); }
    void setRows (const QVector<QStringList>& rows) { beginResetModel(); m_rows = rows; endResetModel(); }
    const QVector<QStringList>& rows () const { return m_rows; }
    QChar kind (int col) const { return col >= 0 && col < m_kinds.size() ? m_kinds[col] : QChar('s'); }
    bool isInvalid (int row) const { return m_status >= 0 && m_rows.value(row).value(m_status) != "VALID" && m_rows.value(row).value(m_status) != "ENABLED" && !m_rows.value(row).value(m_status).isEmpty(); }
    int statusCol () const { return m_status; }
private:
    QStringList m_headers; QString m_kinds; int m_status; QIcon m_icon; QVector<QStringList> m_rows;
};

// Filtro por columna + válidos/inválidos + orden según el tipo de la columna
class ObjectListProxy : public QSortFilterProxyModel
{
public:
    explicit ObjectListProxy (QObject* parent = nullptr) : QSortFilterProxyModel(parent) { setDynamicSortFilter(false); }
    void setColumnFilter (int col, const QString& t) { if (t.isEmpty()) m_filters.remove(col); else m_filters[col] = t; invalidateFilter(); }
    void clearFilters () { m_filters.clear(); invalidateFilter(); }
    void setStateFilter (int mode) { m_state = mode; invalidateFilter(); }          // 0 todos · 1 solo válidos · 2 solo inválidos
protected:
    bool filterAcceptsRow (int row, const QModelIndex& parent) const override;
    bool lessThan (const QModelIndex& a, const QModelIndex& b) const override;
private:
    QHash<int, QString> m_filters; int m_state = 0;
};
