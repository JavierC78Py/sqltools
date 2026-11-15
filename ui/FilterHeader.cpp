#include "FilterHeader.h"
#include <QLineEdit>

FilterHeader::FilterHeader (QWidget* parent) : QHeaderView(Qt::Horizontal, parent)
{
    setSectionsMovable(true); setSectionsClickable(true); setSortIndicatorShown(true); setStretchLastSection(true);
    connect(this, &QHeaderView::sectionResized, this, [this]{ placeEdits(); });
    connect(this, &QHeaderView::sectionMoved, this, [this]{ placeEdits(); });
    connect(this, &QHeaderView::geometriesChanged, this, [this]{ placeEdits(); });
}

void FilterHeader::setFilterCount (int n)
{
    qDeleteAll(m_edits); m_edits.clear();
    for (int i = 0; i < n; ++i)
    {
        auto* e = new QLineEdit(this); e->setPlaceholderText("Escribir texto aquí"); e->setClearButtonEnabled(true); e->show();
        connect(e, &QLineEdit::textChanged, this, [this, i](const QString& t){ emit filterChanged(i, t); });
        m_edits << e;
    }
    updateGeometries(); placeEdits();
}

QString FilterHeader::filterText (int col) const { return col >= 0 && col < m_edits.size() ? m_edits[col]->text() : QString(); }
void FilterHeader::clearFilters () { for (auto* e : m_edits) e->clear(); }

QSize FilterHeader::sizeHint () const
{
    QSize s = QHeaderView::sizeHint();
    if (!m_edits.isEmpty()) s.setHeight(s.height() + m_edits.first()->sizeHint().height() + 4);
    return s;
}

void FilterHeader::updateGeometries ()
{
    if (!m_edits.isEmpty()) setViewportMargins(0, 0, 0, m_edits.first()->sizeHint().height() + 4); else setViewportMargins(0, 0, 0, 0);
    QHeaderView::updateGeometries(); placeEdits();
}

void FilterHeader::placeEdits ()
{
    for (int i = 0; i < m_edits.size(); ++i)
    {
        const int h = m_edits[i]->sizeHint().height();
        m_edits[i]->setGeometry(sectionViewportPosition(i) + 1, height() - h - 2, qMax(20, sectionSize(i) - 2), h);
        m_edits[i]->setVisible(!isSectionHidden(i));
    }
}
