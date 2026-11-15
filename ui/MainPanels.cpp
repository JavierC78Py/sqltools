// Paneles laterales opcionales (View): File Panel, Object Viewer, Properties, Snippets, Find in Files Output, History, Open Files.
#include "MainWindow.h"
#include "CommandMenu.h"
#include "DbWorker.h"
#include "FindInFilesDialog.h"
#include "HistoryStore.h"
#include "ObjectBrowser.h"
#include "CompletionData.h"
#include "ViewerText.h"
#include "Snippets.h"
#include "SqlEditor.h"
#include "Theme.h"
#include <QDockWidget>
#include <QLocale>
#include <QGuiApplication>
#include <QMimeData>
#include <memory>
#include <QSettings>
#include <QClipboard>
#include <QActionGroup>
#include <QMenu>
#include <QTreeWidget>
#include <functional>
#include <QLineEdit>
#include <QStyle>
#include <QApplication>
#include <QComboBox>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

// Árbol del Object Viewer: se puede arrastrar al editor (el texto lo decide viewerText)
class ViewerTree : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;
    QString textFor (const QList<QTreeWidgetItem*>& items) const { return viewerText(items, QGuiApplication::keyboardModifiers()); }
protected:
    QStringList mimeTypes () const override { return {"text/plain"}; }
    QMimeData* mimeData (const QList<QTreeWidgetItem*>& items) const override { auto* m = new QMimeData; m->setText(textFor(items)); return m; }
    void startDrag (Qt::DropActions) override { QTreeWidget::startDrag(Qt::CopyAction); }      // solo copiar: nunca quita elementos del árbol
};

void MainWindow::setupPanelCommands ()
{
    CommandMenu* m = m_menu; using R = CommandMenu;
    // Cada panel es un dock oculto; la acción del menú lo muestra y se marca sola.
    auto makeDock = [this, m](const QString& title, QWidget* w, const char* id, bool left) {
        auto* d = new QDockWidget(title, this); d->setWidget(w); d->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetClosable);
        addDockWidget(left ? Qt::LeftDockWidgetArea : Qt::RightDockWidgetArea, d); if (m_outlineDock && !left) tabifyDockWidget(m_outlineDock, d); else if (left && m_dock) tabifyDockWidget(m_dock, d);
        d->hide();
        if (auto* a = m->bind(id, [d]{ d->setVisible(!d->isVisible()); if (d->isVisible()) d->raise(); }, R::None, true, false)) connect(d, &QDockWidget::visibilityChanged, a, [a](bool v){ a->setChecked(v); });
        return d; };

    // ---- File Panel (⌥0): árbol de carpetas ----
    auto* fsModel = new QFileSystemModel(this); fsModel->setRootPath(QDir::homePath());
    auto* tree = new QTreeView(); tree->setModel(fsModel); tree->setHeaderHidden(true); for (int c = 1; c < 4; ++c) tree->hideColumn(c);
    tree->setRootIndex(fsModel->index(QDir::homePath()));
    connect(tree, &QTreeView::doubleClicked, this, [this, fsModel](const QModelIndex& i){ if (!fsModel->isDir(i)) addEditor(fsModel->filePath(i)); });
    auto* filesDock = makeDock("File Panel", tree, "ID_VIEW_FILE_PANEL", true);
    auto syncTree = [this, fsModel, tree]{ auto* e = editor(); const QString dir = e && !e->filePath.isEmpty() ? QFileInfo(e->filePath).absolutePath() : QDir::homePath();
        if (tree->property("dir").toString() != dir) { tree->setProperty("dir", dir); tree->setRootIndex(fsModel->index(dir)); } };
    connect(filesDock, &QDockWidget::visibilityChanged, this, [syncTree](bool v){ if (v) syncTree(); });

    // ---- Object Viewer (⌥1 / F12): árbol con las propiedades del objeto, como en SQLTools de Windows ----
    auto* ovBox = new QWidget(); auto* ovLay = new QVBoxLayout(ovBox); ovLay->setContentsMargins(4, 4, 4, 4);
    auto* ovCombo = new QComboBox(); ovCombo->setEditable(true); ovCombo->setToolTip("Objetos vistos (Intro para buscar otro por su nombre)"); ovCombo->setInsertPolicy(QComboBox::NoInsert);
    auto* ovTree = new ViewerTree(); ovTree->setColumnCount(3); ovTree->setHeaderLabels({"Nombre", "Tipo / detalle", "Comentario"}); ovTree->setUniformRowHeights(true); ovTree->setStyleSheet(Theme::modernCss()); ovTree->setAlternatingRowColors(false); ovTree->setIndentation(16); ovTree->setSelectionMode(QAbstractItemView::ExtendedSelection); ovTree->setDragEnabled(true); ovTree->setDragDropMode(QAbstractItemView::DragOnly); ovTree->setDefaultDropAction(Qt::CopyAction);
    ovTree->setHeaderHidden(false); ovTree->header()->setSectionResizeMode(QHeaderView::Interactive); ovTree->header()->setStretchLastSection(true); ovTree->header()->setMinimumSectionSize(40);   // las columnas se ajustan arrastrando el borde de la cabecera
    ovLay->addWidget(ovCombo); ovLay->addWidget(ovTree);
    auto* ovDock = makeDock("Object Viewer", ovBox, "ID_SQL_OBJ_VIEWER", false);
    // Los nodos que llegan del servidor: {kind, text, c1, c2, key, lazy, oowner/oname/otype, children}.
    // Las carpetas «lazy» y los objetos (tabla, trigger, paquete…) se abren al expandirlos y muestran lo suyo, igual que en Windows.
    auto fillNodes = [](QTreeWidgetItem* parent, const QVariantList& list, auto& self) -> void {
        for (const QVariant& v : list)
        {
            const QVariantMap m = v.toMap(); auto* it = new QTreeWidgetItem(parent); const QString kind = m["kind"].toString();
            it->setText(0, m["text"].toString()); if (kind != "folder") { it->setText(1, m["c1"].toString()); it->setText(2, m["c2"].toString()); }
            it->setToolTip(1, it->text(1)); it->setToolTip(2, it->text(2));
            it->setData(0, RoleCopy, m["copy"].toString()); it->setData(0, RoleRaw, m["raw"].toString());
            const QString otype = m["otype"].toString(); bool expandable = false; const QString iconName = m["icon"].toString();
            auto treeIcon = [](const QString& n) { return QIcon(":/objicons/icons_obj/tree_" + n + ".png"); };
            if (kind == "folder")
            {
                it->setIcon(0, treeIcon("folder")); it->setData(0, RoleKey, m["key"].toString()); expandable = m["lazy"].toBool();
            }
            else if (!otype.isEmpty())
            {
                it->setData(0, RoleOwner, m["oowner"].toString()); it->setData(0, RoleName, m["oname"].toString()); it->setData(0, RoleType, otype); expandable = true;
                it->setIcon(0, treeIcon(iconName.isEmpty() ? otype.toLower() : iconName));
                it->setText(1, (m["c1"].toString().isEmpty() ? otype.toLower() : m["c1"].toString()));
            }
            if (expandable) { auto* ph = new QTreeWidgetItem(it); ph->setText(0, "…"); ph->setData(0, RolePlaceholder, true); }          // marcador: permite abrirlo
            if (otype.isEmpty() && kind != "folder" && kind != "error") it->setIcon(0, treeIcon(!iconName.isEmpty() ? iconName : kind == "column" ? "column" : "unknown"));
            if (kind == "column" || kind == "item") it->setForeground(2, Theme::muted());
            if (kind == "error") { it->setForeground(0, Theme::error()); it->setFirstColumnSpanned(true); }
            self(it, m["children"].toList(), self);
        } };
    auto tokens = std::make_shared<QHash<QString, QTreeWidgetItem*>>(); auto counter = std::make_shared<int>(0);
    connect(m_worker, &DbWorker::describeTiming, this, [ovTree](int ms){ ovTree->setProperty("dbMs", ms); });
    connect(m_worker, &DbWorker::objectDescribed, this, [this, ovTree, ovCombo, fillNodes, tokens](const QString& owner, const QString& name, const QString& type, const QVariantList& kids, const QString& token){
        if (!token.isEmpty())        // un objeto abierto dentro del árbol
        {
            QTreeWidgetItem* it = tokens->take(token); if (!it) return;
            qDeleteAll(it->takeChildren()); fillNodes(it, kids, fillNodes); ovTree->resizeColumnToContents(1); return;
        }
        tokens->clear(); ovTree->clear(); const QString title = owner + "." + name;
        ovTree->setProperty("owner", owner); ovTree->setProperty("name", name); ovTree->setProperty("type", type);
        auto* root = new QTreeWidgetItem(ovTree); root->setText(0, title); root->setText(1, type.toLower()); QFont bf = root->font(0); bf.setBold(true); root->setFont(0, bf);
        root->setData(0, RoleOwner, owner); root->setData(0, RoleName, name); root->setData(0, RoleType, type); root->setIcon(0, QIcon(":/objicons/icons_obj/tree_" + (type == "MATERIALIZED VIEW" ? QString("view") : type.toLower()) + ".png"));
        fillNodes(root, kids, fillNodes); root->setExpanded(true);
        if (!ovTree->property("sized").toBool()) { ovTree->resizeColumnToContents(0); if (ovTree->columnWidth(0) > 330) ovTree->setColumnWidth(0, 330); ovTree->resizeColumnToContents(1); ovTree->setProperty("sized", true); }
        if (ovCombo->findText(title) < 0) ovCombo->insertItem(0, title); ovCombo->setCurrentText(title); { const int ms = ovTree->property("dbMs").toInt(); m_status->setText(title + ": " + QString::number(kids.size()) + " elementos" + (ms > 0 ? QString(" (base de datos: %1 s)").arg(QLocale().toString(ms / 1000.0, 'f', 2)) : QString()) + "."); } });
    connect(ovTree, &QTreeWidget::itemCollapsed, this, [](QTreeWidgetItem* it){ if (!it->data(0, RoleKey).toString().isEmpty()) it->setIcon(0, QIcon(":/objicons/icons_obj/tree_folder.png")); });
    connect(ovTree, &QTreeWidget::itemExpanded, this, [this, ovTree, tokens, counter](QTreeWidgetItem* it){
        if (!it->data(0, RoleKey).toString().isEmpty()) it->setIcon(0, QIcon(":/objicons/icons_obj/tree_open_folder.png"));
        if (it->childCount() != 1 || !it->child(0)->data(0, RolePlaceholder).toBool()) return;
        it->child(0)->setText(0, "Leyendo…");
        // el contexto de una carpeta es el objeto que la contiene; el de un objeto, él mismo
        QTreeWidgetItem* obj = it; while (obj && obj->data(0, RoleType).toString().isEmpty()) obj = obj->parent();
        if (!it->data(0, RoleKey).toString().isEmpty() && obj)
            QMetaObject::invokeMethod(m_worker, "describeFolder", Qt::QueuedConnection, Q_ARG(QString, obj->data(0, RoleOwner).toString()), Q_ARG(QString, obj->data(0, RoleName).toString()), Q_ARG(QString, obj->data(0, RoleType).toString()), Q_ARG(QString, it->data(0, RoleKey).toString()));
        else if (!it->data(0, RoleType).toString().isEmpty())
        {
            const QString tk = QString::number(++*counter); (*tokens)[tk] = it;
            QMetaObject::invokeMethod(m_worker, "describeObject", Qt::QueuedConnection, Q_ARG(QString, it->data(0, RoleOwner).toString()), Q_ARG(QString, it->data(0, RoleName).toString()), Q_ARG(QString, it->data(0, RoleType).toString()), Q_ARG(QString, tk));
        } });
    connect(m_worker, &DbWorker::folderDescribed, this, [ovTree, fillNodes](const QString& owner, const QString& name, const QString& key, const QVariantList& kids, const QString& err){
        // se busca la carpeta con esa clave dentro del objeto (owner.name) en cualquier nivel del árbol
        std::function<bool(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* parent) -> bool {
            for (int i = 0; i < parent->childCount(); ++i)
            {
                QTreeWidgetItem* f = parent->child(i);
                if (f->data(0, RoleKey).toString() == key && f->childCount() == 1 && f->child(0)->data(0, RolePlaceholder).toBool() == false && false) {}
                if (f->data(0, RoleKey).toString() == key && f->parent() && f->parent()->data(0, RoleOwner).toString() == owner && f->parent()->data(0, RoleName).toString() == name && f->childCount() >= 1 && f->child(0)->text(0) == "Leyendo…")
                {
                    qDeleteAll(f->takeChildren()); fillNodes(f, kids, fillNodes);
                    f->setText(0, f->text(0).section("  (", 0, 0) + "  (" + QString::number(kids.size()) + ")");
                    if (!err.isEmpty()) { auto* e = new QTreeWidgetItem(f); e->setText(0, "⚠ " + err); e->setForeground(0, Theme::error()); e->setFirstColumnSpanned(true); }
                    ovTree->resizeColumnToContents(1); return true;
                }
                if (walk(f)) return true;
            }
            return false; };
        for (int i = 0; i < ovTree->topLevelItemCount(); ++i) if (walk(ovTree->topLevelItem(i))) break; });
    // Insertar en el editor: doble clic (como «Copy/Paste on double click» de Windows) o «Insert at Cursor»
    auto insertSelected = [this, ovTree]{ auto* e = editor(); if (!e) return; const QString t = viewerText(ovTree->selectedItems(), QGuiApplication::keyboardModifiers()); if (t.isEmpty()) return; e->insertPlainText(t); e->setFocus(); };
    ovTree->setExpandsOnDoubleClick(false);
    connect(ovTree, &QTreeWidget::itemDoubleClicked, this, [ovTree, insertSelected](QTreeWidgetItem* it, int){
        if (!it->data(0, RoleKey).toString().isEmpty() || it->data(0, RolePlaceholder).toBool() || !QSettings().value("viewer/dblInsert", true).toBool()) { it->setExpanded(!it->isExpanded()); return; }
        ovTree->setCurrentItem(it); insertSelected(); });
    // clic derecho: acciones según el tipo del objeto (como en SQLTools de Windows)
    ovTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ovTree, &QWidget::customContextMenuRequested, this, [this, ovTree, ovCombo, insertSelected](const QPoint& pos){
        auto* it = ovTree->itemAt(pos); if (!it) return;
        QTreeWidgetItem* obj = it; while (obj && obj->data(0, RoleType).toString().isEmpty()) obj = obj->parent();
        const QString owner = obj ? obj->data(0, RoleOwner).toString() : QString(), name = obj ? obj->data(0, RoleName).toString() : QString(), type = obj ? obj->data(0, RoleType).toString() : QString();
        const bool isObj = it == obj, tbl = isObj && (type == "TABLE" || type == "VIEW" || type == "MATERIALIZED VIEW");
        QMenu menu;
        if (!it->isSelected()) { ovTree->clearSelection(); it->setSelected(true); }
        QAction* ins = menu.addAction("Insert at Cursor", this, insertSelected); menu.setDefaultAction(ins);
        menu.addAction("Copy", this, [ovTree]{ QApplication::clipboard()->setText(viewerText(ovTree->selectedItems(), QGuiApplication::keyboardModifiers())); })->setShortcut(QKeySequence::Copy);
        if (isObj && type != "CONSTRAINT")
        {
            const QString obj1 = type + QChar(0x1f) + name;
            QAction* ld = menu.addAction("Load DDL", this, [this, obj1, owner]{ loadDdl({obj1}, false, true, owner); }); ld->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F12)); ld->setShortcutVisibleInContextMenu(true);
            QAction* lm = menu.addAction("Load DDL with Dbms_Metadata", this, [this, obj1, owner]{ QMetaObject::invokeMethod(m_worker, "loadSource", Qt::QueuedConnection, Q_ARG(QString, owner), Q_ARG(QString, obj1.section(QChar(0x1f), 0, 0)), Q_ARG(QString, obj1.section(QChar(0x1f), 1)), Q_ARG(bool, true)); }); lm->setShortcut(QKeySequence(Qt::ALT | Qt::Key_F12)); lm->setShortcutVisibleInContextMenu(true);
            if (tbl)
            {
                menu.addSeparator();
                const QString q = (owner == m_browser->owner() ? QString() : owner + ".") + name;
                QAction* qa = menu.addAction("Query", this, [this, q]{ quickQueryOn(q, false); }); qa->setShortcut(QKeySequence(Qt::Key_F11)); qa->setShortcutVisibleInContextMenu(true);
                QAction* qc = menu.addAction("Count query", this, [this, q]{ quickQueryOn(q, true); }); qc->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F11)); qc->setShortcutVisibleInContextMenu(true);
                menu.addSeparator(); QMenu* so = menu.addMenu("Sort order"); auto* grp = new QActionGroup(&menu);
                const bool byName = QSettings().value("viewer/sortByName", false).toBool();
                QAction* a1 = so->addAction("By position", this, [this]{ QSettings().setValue("viewer/sortByName", false); if (m_showInViewer) m_showInViewer(QString()); }); QAction* a2 = so->addAction("By name", this, [this]{ QSettings().setValue("viewer/sortByName", true); if (m_showInViewer) m_showInViewer(QString()); });
                for (QAction* a : {a1, a2}) { a->setCheckable(true); grp->addAction(a); } (byName ? a2 : a1)->setChecked(true);
            }
            menu.addSeparator();
            menu.addAction("Refresh", this, [this, owner, name]{ if (m_showInViewer) m_showInViewer(owner + "." + name); });
            menu.addAction("Purge Cache", this, [this, owner, name]{ QMetaObject::invokeMethod(m_worker, "loadObjectsOf", Qt::QueuedConnection, Q_ARG(QString, m_browser->owner())); if (m_showInViewer) m_showInViewer(owner + "." + name); });
        }
        menu.addSeparator(); menu.addAction("Settings...", this, [this]{ m_menu->action("ID_APP_SETTINGS")->trigger(); });
        Q_UNUSED(ovCombo);
        menu.exec(ovTree->viewport()->mapToGlobal(pos)); });

    // mostrar un objeto (nombre con o sin esquema): lo busca en el esquema actual y pide sus propiedades al servidor
    m_showInViewer = [this, ovDock](const QString& what){
        QString owner = m_browser->owner(), name = what.trimmed();
        if (name.isEmpty()) { auto* tr = ovDock->findChild<QTreeWidget*>(); if (tr && !tr->property("name").toString().isEmpty()) { owner = tr->property("owner").toString(); name = tr->property("name").toString(); } else return; }
        if (name.contains('.')) { owner = name.section('.', 0, 0).toUpper(); name = name.section('.', 1); }
        QString type = owner == m_browser->owner() ? m_browser->typeOf(name) : QString(); QString exact = name;
        if (!type.isEmpty()) for (const QString& n : m_browser->objectNames(type)) if (n.compare(name, Qt::CaseInsensitive) == 0) { exact = n; break; }
        if (type.isEmpty()) { type = "TABLE"; exact = name.toUpper(); }     // otro esquema: se intenta como tabla/vista
        { auto* tr = ovDock->findChild<QTreeWidget*>(); if (tr) { tr->clear(); auto* r = new QTreeWidgetItem(tr); r->setText(0, "Leyendo " + owner + "." + exact + "…"); } }          // respuesta inmediata: el panel nunca queda en blanco
        ovDock->show(); ovDock->raise(); if (ovDock->width() < 520) resizeDocks({ovDock}, {560}, Qt::Horizontal); m_status->setText("Leyendo " + owner + "." + exact + "…");
        m_browser->holdPrefetch(15000);          // la precarga de listas no debe retrasar esta lectura
        QMetaObject::invokeMethod(m_worker, "describeObject", Qt::QueuedConnection, Q_ARG(QString, owner), Q_ARG(QString, exact), Q_ARG(QString, type)); };
    connect(ovCombo, &QComboBox::activated, this, [this, ovCombo]{ if (m_showInViewer) m_showInViewer(ovCombo->currentText()); });
    connect(ovCombo->lineEdit(), &QLineEdit::returnPressed, this, [this, ovCombo]{ if (m_showInViewer) m_showInViewer(ovCombo->currentText()); });
    auto refreshViewer = [](){};

    // ---- Properties (⌥2): propiedades del script actual y de la sesión ----
    auto* props = new QTableWidget(0, 2); props->setHorizontalHeaderLabels({"Propiedad", "Valor"}); props->verticalHeader()->hide(); props->setEditTriggers(QAbstractItemView::NoEditTriggers); props->horizontalHeader()->setStretchLastSection(true);
    auto propsDock = makeDock("Properties", props, "ID_VIEW_PROPERTIES", false);
    auto refreshProps = [this, props]{
        auto* e = editor(); QList<QPair<QString, QString>> kv;
        if (e)
        {
            const QTextCursor c = e->textCursor(); QFileInfo fi(e->filePath);
            kv << qMakePair<QString, QString>("Archivo", e->filePath.isEmpty() ? "(sin guardar)" : e->filePath) << qMakePair<QString, QString>("Tamaño", e->filePath.isEmpty() ? "—" : QString("%1 bytes").arg(fi.size()))
               << qMakePair<QString, QString>("Modificado", e->document()->isModified() ? "sí" : "no") << qMakePair<QString, QString>("Codificación", e->encoding + (e->bom ? " con BOM" : ""))
               << qMakePair<QString, QString>("Saltos de línea", e->crlf ? "Windows (CRLF)" : e->cr ? "Mac antiguo (CR)" : "Unix (LF)")
               << qMakePair<QString, QString>("Líneas", QString::number(e->document()->blockCount())) << qMakePair<QString, QString>("Caracteres", QString::number(e->document()->characterCount() - 1))
               << qMakePair<QString, QString>("Cursor", QString("línea %1, columna %2").arg(c.blockNumber() + 1).arg(c.positionInBlock() + 1))
               << qMakePair<QString, QString>("Selección", e->columnMode() ? "en columna" : QString("%1 caracteres").arg(c.selectionEnd() - c.selectionStart()));
        }
        kv << qMakePair<QString, QString>("Sesión", m_connected ? m_conn->text() : "sin conexión") << qMakePair<QString, QString>("Esquema", m_connected ? m_browser->owner() : "—");
        QString sig; for (auto& p : kv) sig += p.first + p.second; if (props->property("sig").toString() == sig) return; props->setProperty("sig", sig);
        props->setRowCount(0); for (auto& p : kv) { const int i = props->rowCount(); props->insertRow(i); props->setItem(i, 0, new QTableWidgetItem(p.first)); props->setItem(i, 1, new QTableWidgetItem(p.second)); }
        props->resizeColumnToContents(0); };

    // ---- Snippets (⌥5): plantillas; doble clic inserta ----
    auto* snipList = new QListWidget(); for (const Snippet& s : snippets()) { auto* it = new QListWidgetItem(QString("%1   —   %2").arg(s.trigger, s.title)); it->setData(Qt::UserRole, s.body); snipList->addItem(it); }
    snipList->setToolTip("Doble clic: inserta la plantilla en el cursor");
    connect(snipList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it){ if (auto* e = editor()) { e->insertSnippet(it->data(Qt::UserRole).toString()); e->setFocus(); } });
    makeDock("Snippets", snipList, "ID_VIEW_SNIPPETS", false);

    // ---- Find in Files Output (⌥7): el diálogo de búsqueda con el último resultado ----
    m->bind("ID_FILE_SHOW_GREP_OUTPUT", [this]{ m_fifDlg->show(); m_fifDlg->raise(); m_fifDlg->activateWindow(); });

    // ---- History (⌥8): sentencias ejecutadas; doble clic las inserta ----
    auto* histList = new QListWidget(); histList->setToolTip("Doble clic: inserta la sentencia en el editor");
    auto historyDock = makeDock("History", histList, "ID_VIEW_HISTORY", false);
    auto refreshHistory = [this, histList]{
        if (!m_historyStore) return; const auto& en = m_historyStore->entries(); QString sig = QString::number(en.size()) + (en.isEmpty() ? QString() : en.last().when.toString(Qt::ISODate)); if (histList->property("sig").toString() == sig) return; histList->setProperty("sig", sig);
        histList->clear(); int shown = 0;
        for (int i = en.size() - 1; i >= 0 && shown < 300; --i, ++shown) { const auto& h = en[i]; QString one = h.sql.simplified(); if (one.size() > 110) one = one.left(110) + "…";
            auto* it = new QListWidgetItem(QString("%1  %2%3").arg(h.when.toString("dd/MM HH:mm"), h.error.isEmpty() ? QString() : QString("⚠ "), one)); it->setData(Qt::UserRole, h.sql);
            it->setToolTip(QString("%1\n\n%2 filas · %3 s%4").arg(h.sql.left(600)).arg(h.rows).arg(h.seconds, 0, 'f', 2).arg(h.error.isEmpty() ? QString() : "\nError: " + h.error.section('\n', 0, 0))); histList->addItem(it); } };
    connect(histList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it){ if (auto* e = editor()) { e->insertPlainText(it->data(Qt::UserRole).toString()); e->setFocus(); } });

    // ---- Open Files (⌥9): pestañas abiertas ----
    auto* openList = new QListWidget();
    auto openDock = makeDock("Open Files", openList, "ID_VIEW_OPEN_FILES", false);
    auto refreshOpen = [this, openList]{
        QString sig; for (int i = 0; i < m_editors->count(); ++i) if (auto* e = qobject_cast<SqlEditor*>(m_editors->widget(i))) sig += e->displayName() + (e->document()->isModified() ? "*" : "") + "|" + QString::number(i == m_editors->currentIndex()) + ";";
        if (openList->property("sig").toString() == sig) return; openList->setProperty("sig", sig); openList->clear();
        for (int i = 0; i < m_editors->count(); ++i) if (auto* e = qobject_cast<SqlEditor*>(m_editors->widget(i))) { auto* it = new QListWidgetItem((e->document()->isModified() ? "● " : "   ") + e->displayName()); it->setData(Qt::UserRole, i); it->setToolTip(e->filePath); openList->addItem(it); if (i == m_editors->currentIndex()) openList->setCurrentItem(it); } };
    connect(openList, &QListWidget::itemActivated, this, [this](QListWidgetItem* it){ m_editors->setCurrentIndex(it->data(Qt::UserRole).toInt()); if (auto* e = editor()) e->setFocus(); });
    connect(openList, &QListWidget::itemClicked, this, [this](QListWidgetItem* it){ m_editors->setCurrentIndex(it->data(Qt::UserRole).toInt()); });

    // Refresco: solo de los paneles visibles
    auto* t = new QTimer(this); t->setInterval(600);
    connect(t, &QTimer::timeout, this, [=]{ if (propsDock->isVisible()) refreshProps(); if (historyDock->isVisible()) refreshHistory(); if (openDock->isVisible()) refreshOpen(); if (filesDock->isVisible()) syncTree(); });
    t->start();
    for (QDockWidget* d : {propsDock, historyDock, openDock}) connect(d, &QDockWidget::visibilityChanged, this, [=](bool v){ if (!v) return; refreshProps(); refreshHistory(); refreshOpen(); });
}
