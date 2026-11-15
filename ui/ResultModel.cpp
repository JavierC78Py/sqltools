#include "ResultModel.h"
#include "Theme.h"
#include <QBrush>
#include <QFont>

QVariant ResultModel::data (const QModelIndex& idx, int role) const
{
    if (!idx.isValid() || idx.row() >= m_rows.size()) return {};
    const QString& v = m_rows[idx.row()].at(idx.column());
    switch (role)
    {
    case Qt::DisplayRole:    return v.isNull() ? m_null : v;
    case Qt::ToolTipRole:    return v.isNull() ? QVariant() : QVariant(v.left(2000));
    case Qt::TextAlignmentRole:
        return int((idx.column() < m_right.size() && m_right[idx.column()] == 1 ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
    case Qt::ForegroundRole: if (v.isNull()) return QBrush(Theme::nullCell()); break;
    case Qt::FontRole:       if (v.isNull()) { QFont f; f.setItalic(true); return f; } break;
    }
    return {};
}

QVariant ResultModel::headerData (int s, Qt::Orientation o, int role) const
{
    if (role != Qt::DisplayRole) return {};
    return o == Qt::Horizontal ? QVariant(s < m_names.size() ? m_names[s] : QString()) : QVariant(s + 1);
}

void ResultModel::reset (const QStringList& names, const QVector<int>& right)
{
    beginResetModel(); m_names = names; m_right = right; m_rows.clear(); endResetModel();
}

void ResultModel::append (const QVector<QStringList>& batch)
{
    if (batch.isEmpty()) return;
    beginInsertRows({}, m_rows.size(), m_rows.size() + batch.size() - 1);
    m_rows += batch;
    endInsertRows();
}
