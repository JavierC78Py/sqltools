#include "ObjectListModel.h"
#include "Theme.h"
#include <QHash>

ObjectListModel::ObjectListModel (const QStringList& headers, const QString& kinds, int statusCol, const QIcon& icon, QObject* parent)
    : QAbstractTableModel(parent), m_headers(headers), m_kinds(kinds), m_status(statusCol), m_icon(icon) {}

QVariant ObjectListModel::data (const QModelIndex& i, int role) const
{
    if (!i.isValid() || i.row() >= m_rows.size()) return QVariant();
    const QString& v = m_rows[i.row()].value(i.column());
    switch (role)
    {
    case Qt::DisplayRole: return v;
    case Qt::ToolTipRole: return v;
    case Qt::DecorationRole: return i.column() == 0 ? QVariant(m_icon) : QVariant();
    case Qt::TextAlignmentRole: return int((kind(i.column()) == 'n' ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
    case Qt::ForegroundRole: if (isInvalid(i.row())) return QVariant(Theme::error()); return QVariant();
    case Qt::UserRole + 1: return kind(i.column()) == 'n' ? QVariant(v.toDouble()) : QVariant(v);        // valor para ordenar
    }
    return QVariant();
}

bool ObjectListProxy::filterAcceptsRow (int row, const QModelIndex& parent) const
{
    auto* m = static_cast<ObjectListModel*>(sourceModel());
    if (m_state == 1 && m->isInvalid(row)) return false;
    if (m_state == 2 && !m->isInvalid(row)) return false;
    for (auto it = m_filters.constBegin(); it != m_filters.constEnd(); ++it)
        if (!m->index(row, it.key(), parent).data().toString().contains(it.value(), Qt::CaseInsensitive)) return false;
    return true;
}

bool ObjectListProxy::lessThan (const QModelIndex& a, const QModelIndex& b) const
{
    const QVariant x = a.data(Qt::UserRole + 1), y = b.data(Qt::UserRole + 1);
    if (x.typeId() == QMetaType::Double && y.typeId() == QMetaType::Double) return x.toDouble() < y.toDouble();
    return QString::compare(x.toString(), y.toString(), Qt::CaseInsensitive) < 0;
}
