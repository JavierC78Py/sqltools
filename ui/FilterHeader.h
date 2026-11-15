#pragma once
#include <QHeaderView>
#include <QList>
class QLineEdit;

// Cabecera de tabla con una caja de filtro bajo cada título (como la lista de objetos de SQLTools)
class FilterHeader : public QHeaderView
{
    Q_OBJECT
public:
    explicit FilterHeader (QWidget* parent = nullptr);
    void setFilterCount (int n);
    QString filterText (int col) const;
    void clearFilters ();
    QSize sizeHint () const override;
signals:
    void filterChanged (int col, const QString& text);
protected:
    void updateGeometries () override;
private:
    void placeEdits ();
    QList<QLineEdit*> m_edits;
};
