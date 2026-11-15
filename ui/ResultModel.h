#pragma once
#include <QAbstractTableModel>
#include <QStringList>
#include <QVector>
#include <QColor>

// Modelo de la rejilla: guarda las filas en memoria y las va agregando por lotes mientras llegan,
// para que la ventana muestre resultados antes de terminar la consulta.
class ResultModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit ResultModel (QObject* parent = nullptr) : QAbstractTableModel(parent) {}
    int rowCount (const QModelIndex& p = {}) const override { return p.isValid() ? 0 : m_rows.size(); }
    int columnCount (const QModelIndex& p = {}) const override { return p.isValid() ? 0 : m_names.size(); }
    QVariant data (const QModelIndex& idx, int role) const override;
    QVariant headerData (int section, Qt::Orientation o, int role) const override;
    const QVector<QStringList>& rows () const { return m_rows; }
    const QStringList& columns () const { return m_names; }
    const QVector<int>& rightAligned () const { return m_right; }
    void setNullText (const QString& t) { beginResetModel(); m_null = t; endResetModel(); }

    void reset (const QStringList& names = {}, const QVector<int>& rightAligned = {});
    void append (const QVector<QStringList>& batch);
private:
    QStringList m_names;
    QVector<int> m_right;
    QVector<QStringList> m_rows;
    QString m_null = QStringLiteral("(null)");
};
