#include "ObjectBrowser.h"
#include "FilterHeader.h"
#include "ObjectListDefs.h"
#include "ObjectListModel.h"
#include "Theme.h"
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QSettings>
#include <QTableView>
#include <QDateTime>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
// Distribución «en renglones»: los botones de las pestañas pasan a la fila siguiente cuando no caben (como las pestañas de varias filas de Windows)
class FlowLayout : public QLayout
{
public:
    explicit FlowLayout (QWidget* parent) : QLayout(parent) { setContentsMargins(0, 0, 0, 0); }
    ~FlowLayout () override { qDeleteAll(m_items); }
    void addItem (QLayoutItem* i) override { m_items << i; }
    int count () const override { return m_items.size(); }
    QLayoutItem* itemAt (int i) const override { return m_items.value(i); }
    QLayoutItem* takeAt (int i) override { return i >= 0 && i < m_items.size() ? m_items.takeAt(i) : nullptr; }
    bool hasHeightForWidth () const override { return true; }
    int heightForWidth (int w) const override { return doLayout(QRect(0, 0, w, 0), true); }
    void setGeometry (const QRect& r) override { QLayout::setGeometry(r); doLayout(r, false); }
    QSize sizeHint () const override { return minimumSize(); }
    QSize minimumSize () const override { QSize s; for (auto* i : m_items) s = s.expandedTo(i->minimumSize()); return s; }
private:
    int doLayout (const QRect& r, bool test) const
    {
        int x = r.x(), y = r.y(), rowH = 0;
        for (auto* i : m_items)
        {
            const QSize sz = i->sizeHint();
            if (x + sz.width() > r.right() + 1 && rowH > 0) { x = r.x(); y += rowH; rowH = 0; }
            if (!test) i->setGeometry(QRect(QPoint(x, y), sz));
            x += sz.width(); rowH = qMax(rowH, sz.height());
        }
        return y + rowH - r.y();
    }
    QList<QLayoutItem*> m_items;
};
QIcon objIcon (const QString& name) { return QIcon(":/objicons/icons_obj/" + name + ".png"); }
}

ObjectBrowser::ObjectBrowser (QWidget* parent) : QWidget(parent)
{
    const auto& defs = objectListDefs(); QSettings st;
    m_titles = st.value("objlist/titles", true).toBool(); m_detail = st.value("objlist/detail", true).toBool();
    auto btn = [](const QString& icon, const QString& tip, bool checkable = false) { auto* b = new QToolButton(); b->setIcon(objIcon(icon)); b->setIconSize(QSize(20, 20)); b->setFixedSize(30, 30); b->setToolTip(tip); b->setAutoRaise(true); b->setCheckable(checkable); return b; };
    m_refresh = btn("ico_refresh", "Recargar (F5 en el panel)"); m_listBtn = btn("ico_as_list_btn", "Lista corta (solo nombres)", true); m_reportBtn = btn("ico_as_report_btn", "Lista detallada (con columnas)", true);
    m_validBtn = btn("ico_valid_btn", "Mostrar solo los objetos válidos", true); m_invalidBtn = btn("ico_invalid_btn", "Mostrar solo los objetos inválidos", true);
    m_filterBtn = btn("ico_filter", "Borrar los filtros de las columnas"); m_settingsBtn = btn("ico_settings", "Ajustes del DDL…");
    m_schema = new QComboBox(); m_schema->setToolTip("Esquema cuyos objetos se muestran");
    m_status = new QLabel(); m_status->setStyleSheet(Theme::css(Theme::muted()));
    auto* top = new QHBoxLayout(); top->setSpacing(4); top->setContentsMargins(0, 0, 0, 0);
    top->addWidget(m_refresh); top->addWidget(m_listBtn); top->addWidget(m_reportBtn); top->addWidget(m_schema, 1); top->addWidget(m_validBtn); top->addWidget(m_invalidBtn); top->addWidget(m_filterBtn); top->addWidget(m_settingsBtn);
    m_tabHost = new QWidget(); new FlowLayout(m_tabHost); m_tabGroup = new QButtonGroup(this); m_tabGroup->setExclusive(true);
    m_view = new QTableView(); m_header = new FilterHeader(m_view); m_view->setHorizontalHeader(m_header);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows); m_view->setSelectionMode(QAbstractItemView::ExtendedSelection); m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->verticalHeader()->hide(); m_view->verticalHeader()->setDefaultSectionSize(20); m_view->setShowGrid(false); m_view->setAlternatingRowColors(true); m_view->setSortingEnabled(true);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu); m_view->setWordWrap(false); m_view->setIconSize(QSize(16, 16)); m_view->setTextElideMode(Qt::ElideRight);
    auto* lay = new QVBoxLayout(this); lay->setContentsMargins(4, 4, 4, 4); lay->setSpacing(3); lay->addLayout(top); lay->addWidget(m_tabHost); lay->addWidget(m_view, 1); lay->addWidget(m_status);
    for (const auto& d : defs)
    {
        auto* m = new ObjectListModel(d.headers, d.kinds, d.statusCol, objIcon("list_" + d.icon), this); auto* p = new ObjectListProxy(this); p->setSourceModel(m);
        m_models << m; m_proxies << p; m_loaded << false;
    }
    rebuildTabButtons();
    applyModernStyle();
    (m_detail ? m_reportBtn : m_listBtn)->setChecked(true);
    connect(m_listBtn, &QToolButton::clicked, this, [this]{ m_detail = false; QSettings().setValue("objlist/detail", false); m_listBtn->setChecked(true); m_reportBtn->setChecked(false); applyView(); });
    connect(m_reportBtn, &QToolButton::clicked, this, [this]{ m_detail = true; QSettings().setValue("objlist/detail", true); m_reportBtn->setChecked(true); m_listBtn->setChecked(false); applyView(); });
    connect(m_validBtn, &QToolButton::clicked, this, [this](bool on){ if (on) m_invalidBtn->setChecked(false); for (auto* p : m_proxies) p->setStateFilter(on ? 1 : 0); updateStatus(); });
    connect(m_invalidBtn, &QToolButton::clicked, this, [this](bool on){ if (on) m_validBtn->setChecked(false); for (auto* p : m_proxies) p->setStateFilter(on ? 2 : 0); updateStatus(); });
    connect(m_filterBtn, &QToolButton::clicked, this, [this]{ m_header->clearFilters(); for (auto* p : m_proxies) p->clearFilters(); updateStatus(); });
    connect(m_settingsBtn, &QToolButton::clicked, this, [this]{ emit actionRequested("settings", QStringList()); });
    connect(m_refresh, &QToolButton::clicked, this, [this]{ m_inFlight = -1; m_failed.clear(); for (int i = 0; i < m_loaded.size(); ++i) m_loaded[i] = false; emit refreshRequested(); requestList(m_tab); });
    connect(m_schema, &QComboBox::activated, this, [this]{ m_inFlight = -1; m_failed.clear(); for (int i = 0; i < m_loaded.size(); ++i) { m_loaded[i] = false; m_models[i]->setRows({}); } m_rows.clear(); emit schemaChanged(owner()); requestList(m_tab); });
    connect(m_header, &FilterHeader::filterChanged, this, [this](int col, const QString& t){ m_proxies[m_tab]->setColumnFilter(col, t); updateStatus(); });
    connect(m_view, &QTableView::doubleClicked, this, [this](const QModelIndex& i){ const auto& d = objectListDefs()[m_tab]; if (d.ddlType.isEmpty()) return; emit openSourceRequested(d.ddlType, m_proxies[m_tab]->index(i.row(), 0).data().toString()); });
    connect(m_view, &QTableView::customContextMenuRequested, this, &ObjectBrowser::showMenuAt);
    showTab(qBound(0, st.value("objlist/tab", 0).toInt(), int(defs.size()) - 1));
}

// Aspecto moderno: bordes finos y redondeados, botones "píldora", cabecera plana y colores que siguen el tema claro/oscuro.
void ObjectBrowser::applyModernStyle ()
{
    const QString css = Theme::modernCss();
    setStyleSheet(css);
    m_schema->setMinimumWidth(110); m_schema->setMinimumHeight(30); m_schema->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); m_view->setFrameShape(QFrame::NoFrame); m_view->setAlternatingRowColors(true); m_view->verticalHeader()->setDefaultSectionSize(22);
}

void ObjectBrowser::rebuildTabButtons ()
{
    const auto& defs = objectListDefs(); auto* fl = m_tabHost->layout();
    for (auto* b : m_tabButtons) { m_tabGroup->removeButton(b); fl->removeWidget(b); b->deleteLater(); } m_tabButtons.clear();
    for (int i = 0; i < defs.size(); ++i)
    {
        auto* b = new QToolButton(m_tabHost); b->setCheckable(true); b->setAutoRaise(true); b->setIcon(objIcon("list_" + defs[i].icon)); b->setIconSize(QSize(16, 16)); b->setToolTip(defs[i].title);
        b->setToolButtonStyle(m_titles ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly); if (m_titles) b->setText(defs[i].title);
        m_tabGroup->addButton(b, i); fl->addWidget(b); m_tabButtons << b; connect(b, &QToolButton::clicked, this, [this, i]{ showTab(i); });
    }
    if (m_tab < m_tabButtons.size()) m_tabButtons[m_tab]->setChecked(true);
    m_tabHost->updateGeometry();
}

void ObjectBrowser::showTab (int index)
{
    const auto& defs = objectListDefs(); if (index < 0 || index >= defs.size()) return;
    m_tab = index; QSettings().setValue("objlist/tab", index); if (index < m_tabButtons.size()) m_tabButtons[index]->setChecked(true);
    m_view->setModel(m_proxies[index]); m_header->setFilterCount(defs[index].headers.size());
    m_view->horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder);
    // restablece los textos de filtro visibles a los del modelo actual (cada pestaña conserva los suyos en su proxy; aquí empiezan vacíos)
    m_proxies[index]->clearFilters();
    for (int c = 0; c < defs[index].headers.size(); ++c) m_view->setColumnWidth(c, c == 0 ? 220 : (c < defs[index].kinds.size() && defs[index].kinds[c] == 'd') ? 150 : 110);
    applyView(); updateStatus();
    if (!m_loaded[index]) requestList(index);
}

void ObjectBrowser::applyView ()
{
    const int n = m_models[m_tab]->columnCount();
    for (int c = 1; c < n; ++c) m_view->setColumnHidden(c, !m_detail);
    m_header->setStretchLastSection(m_detail ? true : false);
    if (!m_detail) m_view->horizontalHeader()->setStretchLastSection(false);
    m_header->updateGeometry();
}

void ObjectBrowser::requestList (int index, bool background)
{
    const auto& d = objectListDefs()[index];
    if (!m_connected) { m_status->setText("Sin conexión."); return; }
    if (owner().isEmpty()) return;
    QString sql = d.sql; sql.replace("%O%", "'" + owner().replace('\'', "''") + "'");
    if (!background || index == m_tab) m_status->setText("Cargando " + d.title + "…");
    m_inFlight = index;
    emit listRequested(owner(), d.key, sql);
}

// Precarga en segundo plano las pestañas que faltan (una a la vez; empieza por las más cercanas a la actual)
void ObjectBrowser::holdPrefetch (int ms) { m_holdUntil = QDateTime::currentMSecsSinceEpoch() + ms; }

void ObjectBrowser::prefetchNext ()
{
    if (!m_connected || m_inFlight >= 0 || owner().isEmpty()) return;
    if (const qint64 left = m_holdUntil - QDateTime::currentMSecsSinceEpoch(); left > 0) { QTimer::singleShot(int(left) + 50, this, &ObjectBrowser::prefetchNext); return; }
    const int n = m_loaded.size();
    for (int k = 1; k < n; ++k) { const int i = (m_tab + k) % n; if (!m_loaded[i] && !m_failed.contains(i)) { requestList(i, true); return; } }
}

void ObjectBrowser::setListRows (const QString& key, const QVector<QStringList>& rows, const QString& error)
{
    const auto& defs = objectListDefs();
    for (int i = 0; i < defs.size(); ++i) if (defs[i].key == key)
    {
        m_models[i]->setRows(rows); m_loaded[i] = error.isEmpty(); if (!error.isEmpty()) m_failed.insert(i); if (m_inFlight == i) m_inFlight = -1;
        QTimer::singleShot(30, this, &ObjectBrowser::prefetchNext);          // precarga el resto de pestañas, de una en una, para que cambiar de pestaña sea instantáneo
        if (i == m_tab)
        {
            m_proxies[i]->sort(m_view->horizontalHeader()->sortIndicatorSection(), m_view->horizontalHeader()->sortIndicatorOrder());
            if (!error.isEmpty()) m_status->setText("⚠ " + error); else updateStatus();
        }
        return;
    }
}

void ObjectBrowser::updateStatus ()
{
    const auto& d = objectListDefs()[m_tab]; const int total = m_models[m_tab]->rowCount(), shown = m_proxies[m_tab]->rowCount();
    m_status->setText(d.ownerOnly && owner() != m_schema->itemText(0) && false ? QString() : (shown == total ? QString("%1 objeto(s)").arg(total) : QString("%1 de %2 objeto(s)").arg(shown).arg(total)));
}

QStringList ObjectBrowser::selectedObjects (bool* anyInvalid, QString* text) const
{
    const auto& d = objectListDefs()[m_tab]; QStringList out; QStringList lines; if (anyInvalid) *anyInvalid = false;
    auto sel = m_view->selectionModel() ? m_view->selectionModel()->selectedRows() : QModelIndexList();
    for (const QModelIndex& i : sel)
    {
        const QString name = m_proxies[m_tab]->index(i.row(), 0).data().toString(); if (!d.ddlType.isEmpty()) out << d.ddlType + QChar(0x1f) + name;
        const QModelIndex src = m_proxies[m_tab]->mapToSource(i); if (anyInvalid && m_models[m_tab]->isInvalid(src.row())) *anyInvalid = true;
        QStringList cells; for (int c = 0; c < m_models[m_tab]->columnCount(); ++c) cells << m_proxies[m_tab]->index(i.row(), c).data().toString(); lines << cells.join('\t');
    }
    if (text) *text = lines.join('\n');
    return out;
}

void ObjectBrowser::showMenuAt (const QPoint& pos)
{
    const QModelIndex i = m_view->indexAt(pos);
    if (i.isValid() && !m_view->selectionModel()->isRowSelected(i.row(), QModelIndex())) { m_view->clearSelection(); m_view->selectRow(i.row()); }
    bool inv = false; QString text; const QStringList objs = selectedObjects(&inv, &text);
    QMenu* m = buildMenu(objs, inv, text); m->exec(m_view->viewport()->mapToGlobal(pos)); delete m;
}

QMenu* ObjectBrowser::contextMenuFor (const QString& type, const QString& name)
{
    return buildMenu({type + QChar(0x1f) + name}, m_rows.size() > 0 && [&]{ for (const auto& r : m_rows) if (r[0] == type && r[1] == name && r[2] != "VALID") return true; return false; }(), name);
}

// Menú contextual: la misma estructura que el del SQLTools original, con las entradas propias de cada tipo de objeto
QMenu* ObjectBrowser::buildMenu (const QStringList& objs, bool anyInvalid, const QString& rowsText)
{
    const bool any = !objs.isEmpty(); const QString type = any ? objs.first().section(QChar(0x1f), 0, 0) : QString();
    bool sameType = true; for (const QString& o : objs) if (o.section(QChar(0x1f), 0, 0) != type) sameType = false;
    const bool isTable = sameType && type == "TABLE", isView = sameType && (type == "VIEW" || type == "MATERIALIZED VIEW");
    const bool compilable = sameType && (type == "PROCEDURE" || type == "FUNCTION" || type == "PACKAGE" || type == "TYPE" || type == "TRIGGER" || type == "VIEW" || type == "MATERIALIZED VIEW" || type == "SYNONYM");
    auto* menuP = new QMenu(this); QMenu& menu = *menuP;
    auto act = [&](const QString& text, const QString& id, bool enabled = true, const QKeySequence& key = QKeySequence()) {
        QAction* a = menu.addAction(text); a->setEnabled(enabled); if (!key.isEmpty()) { a->setShortcut(key); a->setShortcutVisibleInContextMenu(true); }
        connect(a, &QAction::triggered, this, [=]{ emit actionRequested(id, objs); }); return a; };
    act("Refresh", "refresh"); menu.addSeparator();
    if (isTable || isView) { act("Query", "query", any && objs.size() == 1, QKeySequence(Qt::Key_F11)); menu.addSeparator(); }
    if (any)
    {
        act("Show in Object Viewer", "viewer", objs.size() == 1, QKeySequence(Qt::Key_F12));
        QAction* load = act("Load DDL", "ddl", true, QKeySequence(Qt::CTRL | Qt::Key_F12)); { QFont f = load->font(); f.setBold(true); load->setFont(f); menu.setDefaultAction(load); }
        act("Load DDL in one file", "ddl-one");
        act("Load DDL with Dbms_Metadata", "ddl-meta", true, QKeySequence(Qt::ALT | Qt::Key_F12)); menu.addSeparator();
        act("Drop", "drop", true, QKeySequence(Qt::CTRL | Qt::Key_Delete)); menu.addSeparator();
        if (isTable) { act("Truncate", "truncate"); act("Create Transformation Script", "transform", objs.size() == 1); menu.addSeparator(); }
        if (compilable) { act("Compile", "compile"); if (sameType && type == "TRIGGER") { act("Enable", "enable"); act("Disable", "disable"); } menu.addSeparator(); }
        if (anyInvalid) { act("Compile All Invalid with Dbms_Utility", "compile-all"); menu.addSeparator(); }
    }
    QAction* cp = menu.addAction("Copy", this, [=]{ QApplication::clipboard()->setText(rowsText); }); cp->setEnabled(!rowsText.isEmpty()); cp->setShortcut(QKeySequence::Copy); cp->setShortcutVisibleInContextMenu(true);
    QAction* sa = menu.addAction("Select All", this, [this]{ m_view->selectAll(); }); sa->setShortcut(QKeySequence::SelectAll); sa->setShortcutVisibleInContextMenu(true); menu.addSeparator();
    menu.addAction("Filter...", this, [this]{ bool ok = false; const QString t = QInputDialog::getText(this, "Filter", "Mostrar solo los objetos cuyo nombre contiene (vacío = todos):", QLineEdit::Normal, m_header->filterText(0), &ok); if (ok) { m_header->clearFilters(); m_proxies[m_tab]->setColumnFilter(0, t); updateStatus(); } }); menu.addSeparator();
    QMenu* view = menu.addMenu("View"); auto* grp = new QActionGroup(&menu);
    QAction* sl = view->addAction("Short List", this, [this]{ m_listBtn->click(); }); QAction* dl = view->addAction("Detail List", this, [this]{ m_reportBtn->click(); });
    for (QAction* a : {sl, dl}) { a->setCheckable(true); grp->addAction(a); } (m_detail ? dl : sl)->setChecked(true);
    view->addSeparator(); QAction* tt = view->addAction("Tab titles", this, [this](bool on){ m_titles = on; QSettings().setValue("objlist/titles", on); rebuildTabButtons(); }); tt->setCheckable(true); tt->setChecked(m_titles);
    menu.addSeparator(); menu.addAction("DDL Preferences...", this, [this]{ emit actionRequested("settings", QStringList()); });
    return menuP;
}

void ObjectBrowser::clear () { m_inFlight = -1; m_failed.clear(); m_rows.clear(); for (int i = 0; i < m_models.size(); ++i) { m_models[i]->setRows({}); m_loaded[i] = false; } m_status->clear(); }

void ObjectBrowser::setObjects (const QVector<QStringList>& rows) { m_rows = rows; if (m_connected && !m_loaded[m_tab]) requestList(m_tab); }

QString ObjectBrowser::summary () const
{
    QMap<QString, int> n; for (const auto& r : m_rows) ++n[r[0]]; QStringList parts; for (auto it = n.begin(); it != n.end(); ++it) parts << QString("%1 (%2)").arg(it.key()).arg(it.value());
    return parts.join(", ");
}

void ObjectBrowser::setSchemas (const QStringList& names, const QString& current)
{
    QSignalBlocker block(m_schema);
    m_schema->clear(); m_schema->addItems(names);
    if (!names.contains(current)) m_schema->insertItem(0, current);
    m_schema->setCurrentText(current);
}

QString ObjectBrowser::owner () const { return m_schema->currentText(); }

QString ObjectBrowser::typeOf (const QString& name) const
{
    for (const auto& r : m_rows) if (r[1].compare(name, Qt::CaseInsensitive) == 0) return r[0];
    return QString();
}

QStringList ObjectBrowser::objectNames (const QString& type) const
{
    QStringList out; for (const auto& r : m_rows) if (type.isEmpty() || r[0] == type) out << r[1];
    return out;
}
