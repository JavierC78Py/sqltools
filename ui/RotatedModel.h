#pragma once
// Vista "rotada" del registro actual: una fila por columna (Campo | Valor), como el botón Rotate de la rejilla de SQLTools.
#include "ResultModel.h"
class RotatedModel : public QAbstractTableModel
{
public:
    RotatedModel (ResultModel* src, QObject* parent = nullptr) : QAbstractTableModel(parent), m_src(src) {}
    int rowCount (const QModelIndex& p = {}) const override { return p.isValid() ? 0 : m_src->columnCount(); }
    int columnCount (const QModelIndex& p = {}) const override { return p.isValid() ? 0 : 2; }
    QVariant data (const QModelIndex& i, int role) const override
    {
        if (!i.isValid() || m_row < 0 || m_row >= m_src->rowCount()) return {};
        if (i.column() == 0) return role == Qt::DisplayRole ? QVariant(m_src->columns().value(i.row())) : QVariant();
        return m_src->data(m_src->index(m_row, i.row()), role == Qt::TextAlignmentRole ? int(Qt::AlignLeft | Qt::AlignVCenter) : role);
    }
    QVariant headerData (int s, Qt::Orientation o, int role) const override { return (role == Qt::DisplayRole && o == Qt::Horizontal) ? QVariant(s == 0 ? "Campo" : "Valor") : QVariant(); }
    void setRow (int row) { beginResetModel(); m_row = row; endResetModel(); }
    int row () const { return m_row; }
private:
    ResultModel* m_src; int m_row = -1;
};
