// Espacios de trabajo (Workspaces), instantáneas (Snapshots), New… desde plantilla, Move/Rename y Send Mail.
#include "MainWindow.h"
#include "CommandMenu.h"
#include "SqlEditor.h"
#include "Snippets.h"
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTabWidget>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

QString MainWindow::snapshotDir () const
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/snapshots"; QDir().mkpath(d); return d;
}

// Fotografía de las pestañas: archivos (con la posición del cursor) y scripts sin guardar (con su texto).
QJsonObject MainWindow::captureWorkspace () const
{
    QJsonArray items; int current = 0;
    for (int i = 0; i < m_editors->count(); ++i)
    {
        auto* e = qobject_cast<SqlEditor*>(m_editors->widget(i)); if (!e) continue;
        QJsonObject o;
        if (!e->filePath.isEmpty()) { o["path"] = e->filePath; o["cursor"] = e->textCursor().position(); }
        else if (!e->document()->isEmpty()) { o["title"] = e->displayName(); o["text"] = e->toPlainText(); o["cursor"] = e->textCursor().position(); }
        else continue;
        if (i == m_editors->currentIndex()) current = items.size();
        items.append(o);
    }
    return { {"app", "SQLToolsMac"}, {"version", 1}, {"saved", QDateTime::currentDateTime().toString(Qt::ISODate)}, {"current", current}, {"items", items} };
}

// Abre lo descrito en el JSON como pestañas (si closeCurrent, antes cierra las actuales). Devuelve cuántas abrió.
int MainWindow::applyWorkspace (const QJsonObject& ws, bool closeCurrent)
{
    if (ws.value("app").toString() != "SQLToolsMac" || !ws.contains("items")) return -1;
    if (closeCurrent && !closeAllTabs()) return -2;
    int opened = 0, firstNew = m_editors->count(); const QJsonArray items = ws.value("items").toArray(); QStringList missing;
    for (const auto& v : items)
    {
        const QJsonObject o = v.toObject(); SqlEditor* e = nullptr;
        if (o.contains("path"))
        {
            const QString p = o["path"].toString(); if (!QFile::exists(p)) { missing << p; continue; }
            for (int i = 0; i < m_editors->count() && !e; ++i) if (auto* x = qobject_cast<SqlEditor*>(m_editors->widget(i)); x && x->filePath == p) e = x;
            if (!e) e = addEditor(p);
        }
        else { e = addEditor(); if (e) { e->titleHint = o["title"].toString(); e->setPlainText(o["text"].toString()); e->document()->setModified(true); updateEditorTitle(e); } }
        if (!e) continue;
        QTextCursor c = e->textCursor(); c.setPosition(qBound(0, o["cursor"].toInt(), e->document()->characterCount() - 1)); e->setTextCursor(c); ++opened;
    }
    const int cur = firstNew + qBound(0, ws.value("current").toInt(), qMax(0, opened - 1));
    if (opened > 0 && cur < m_editors->count()) m_editors->setCurrentIndex(cur);
    if (!missing.isEmpty()) m_status->setText(QString("%1 archivo(s) ya no existen y se omitieron.").arg(missing.size()));
    return opened;
}

bool MainWindow::closeAllTabs ()
{
    while (m_editors->count() > 0)
    {
        auto* e = qobject_cast<SqlEditor*>(m_editors->widget(m_editors->count() - 1));
        if (m_editors->count() == 1 && e && e->filePath.isEmpty() && e->document()->isEmpty() && !e->document()->isModified()) return true;   // ya está en blanco
        m_editors->setCurrentIndex(m_editors->count() - 1);
        if (!closeScript(m_editors->count() - 1)) return false;                                         // el usuario canceló
    }
    return true;
}

static QString wsTitle (const QString& path) { return path.isEmpty() ? QString() : QFileInfo(path).completeBaseName(); }

void MainWindow::saveWorkspaceTo (const QString& path)
{
    QFile f(path); if (!f.open(QIODevice::WriteOnly)) { QMessageBox::warning(this, "Workspace", "No se pudo escribir " + path + ":\n" + f.errorString()); return; }
    f.write(QJsonDocument(captureWorkspace()).toJson(QJsonDocument::Indented)); f.close();
    m_workspacePath = path; setWindowTitle(QString("SQLTools Mac — %1").arg(wsTitle(path)));
    QSettings st; QStringList l = st.value("recent/workspaces").toStringList(); l.removeAll(path); l.prepend(path); while (l.size() > 8) l.removeLast(); st.setValue("recent/workspaces", l);
    m_status->setText("Workspace guardado: " + path);
}

void MainWindow::openWorkspaceFile (const QString& path)
{
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) { QMessageBox::warning(this, "Workspace", "No se pudo abrir " + path + ":\n" + f.errorString()); return; }
    const int n = applyWorkspace(QJsonDocument::fromJson(f.readAll()).object(), true);
    if (n == -1) { QMessageBox::warning(this, "Workspace", "El archivo no es un workspace de SQLTools Mac."); return; }
    if (n < 0) return;
    m_workspacePath = path; setWindowTitle(QString("SQLTools Mac — %1").arg(wsTitle(path)));
    QSettings st; QStringList l = st.value("recent/workspaces").toStringList(); l.removeAll(path); l.prepend(path); while (l.size() > 8) l.removeLast(); st.setValue("recent/workspaces", l);
    m_status->setText(QString("Workspace abierto: %1 pestaña(s).").arg(n));
}

// Instantánea en la carpeta de datos: "quick_" (la pide el usuario) o "auto_" (al cerrar). Se conservan las últimas 20 / 10.
QString MainWindow::saveSnapshot (bool autosave)
{
    const QJsonObject ws = captureWorkspace(); if (ws.value("items").toArray().isEmpty()) return QString();
    const QString name = QString("%1/%2_%3.json").arg(snapshotDir(), autosave ? "auto" : "quick", QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    QFile f(name); if (!f.open(QIODevice::WriteOnly)) return QString(); f.write(QJsonDocument(ws).toJson(QJsonDocument::Indented)); f.close();
    const QStringList all = QDir(snapshotDir()).entryList({QString(autosave ? "auto_*" : "quick_*")}, QDir::Files, QDir::Name | QDir::Reversed);
    for (int i = autosave ? 10 : 20; i < all.size(); ++i) QFile::remove(snapshotDir() + "/" + all[i]);
    return name;
}

static QString snapLabel (const QString& file)
{
    const QString b = QFileInfo(file).completeBaseName(); const QDateTime t = QDateTime::fromString(b.section('_', 1), "yyyyMMdd_HHmmss");
    QFile f(file); int n = 0; if (f.open(QIODevice::ReadOnly)) n = QJsonDocument::fromJson(f.readAll()).object().value("items").toArray().size();
    return QString("%1  —  %2  (%3 pestaña%4)").arg(b.startsWith("auto") ? "Automática" : "Rápida", t.isValid() ? t.toString("dd/MM/yyyy HH:mm:ss") : b, QString::number(n), n == 1 ? "" : "s");
}

void MainWindow::openSnapshotFile (const QString& path)
{
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) { m_status->setText("No se pudo abrir la instantánea."); return; }
    const int n = applyWorkspace(QJsonDocument::fromJson(f.readAll()).object(), false);
    m_status->setText(n >= 0 ? QString("Instantánea abierta: %1 pestaña(s).").arg(n) : "La instantánea no es válida.");
}

void MainWindow::rebuildWorkspaceMenus ()
{
    if (m_wsRecentMenu)
    {
        m_wsRecentMenu->clear(); int n = 0;
        for (const QString& p : QSettings().value("recent/workspaces").toStringList()) { if (!QFile::exists(p)) continue; QAction* a = m_wsRecentMenu->addAction(QString("%1  —  %2").arg(++n).arg(wsTitle(p))); a->setToolTip(p); connect(a, &QAction::triggered, this, [this, p]{ openWorkspaceFile(p); }); }
        if (!n) m_wsRecentMenu->addAction("(vacío)")->setEnabled(false);
    }
    if (m_snapRecentMenu)
    {
        m_snapRecentMenu->clear(); int n = 0;
        for (const QString& fn : QDir(snapshotDir()).entryList({"quick_*"}, QDir::Files, QDir::Name | QDir::Reversed)) { if (++n > 8) break; const QString p = snapshotDir() + "/" + fn; QAction* a = m_snapRecentMenu->addAction(snapLabel(p)); connect(a, &QAction::triggered, this, [this, p]{ openSnapshotFile(p); }); }
        if (!n) m_snapRecentMenu->addAction("(vacío)")->setEnabled(false);
    }
}

void MainWindow::setupWorkspaceCommands ()
{
    using R = CommandMenu;
    CommandMenu* m = m_menu;
    m->bind("ID_WORKSPACE_SAVE", [this]{ if (m_workspacePath.isEmpty()) m_menu->action("ID_WORKSPACE_SAVE_AS")->trigger(); else saveWorkspaceTo(m_workspacePath); });
    m->bind("ID_WORKSPACE_SAVE_AS", [this]{ QString p = QFileDialog::getSaveFileName(this, "Guardar workspace", QDir::homePath() + "/" + (m_workspacePath.isEmpty() ? "trabajo" : wsTitle(m_workspacePath)) + ".sqlws", "Workspace de SQLTools (*.sqlws)"); if (p.isEmpty()) return; if (!p.endsWith(".sqlws")) p += ".sqlws"; saveWorkspaceTo(p); });
    m->bind("ID_WORKSPACE_OPEN", [this]{ const QString p = QFileDialog::getOpenFileName(this, "Abrir workspace", QDir::homePath(), "Workspace de SQLTools (*.sqlws)"); if (!p.isEmpty()) openWorkspaceFile(p); });
    m->bind("ID_WORKSPACE_CLOSE", [this]{ if (closeAllTabs()) { m_workspacePath.clear(); setWindowTitle("SQLTools Mac"); m_status->setText("Workspace cerrado."); } });
    m->bind("ID_WORKSPACE_COPY", [this]{ QApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(captureWorkspace()).toJson(QJsonDocument::Compact))); m_status->setText("Instantánea copiada al portapapeles (pégala en otro SQLTools con Snapshot > Paste)."); });
    m->bind("ID_WORKSPACE_PASTE", [this]{ const int n = applyWorkspace(QJsonDocument::fromJson(QApplication::clipboard()->text().toUtf8()).object(), false); m_status->setText(n >= 0 ? QString("Instantánea pegada: %1 pestaña(s).").arg(n) : "El portapapeles no contiene una instantánea."); });
    m->bind("ID_WORKSPACE_SAVE_QUICK", [this]{ const QString p = saveSnapshot(false); m_status->setText(p.isEmpty() ? "No hay nada que guardar." : "Instantánea rápida guardada: " + QFileInfo(p).fileName()); });
    m->bind("ID_WORKSPACE_OPEN_AUTOSAVED", [this]{
        QDialog d(this); d.setWindowTitle("Instantáneas"); auto* lay = new QVBoxLayout(&d); lay->addWidget(new QLabel("Rápidas (las que guardaste) y automáticas (al cerrar el programa). Se abren sin cerrar tus pestañas."));
        auto* list = new QListWidget(); lay->addWidget(list);
        for (const QString& fn : QDir(snapshotDir()).entryList({"*.json"}, QDir::Files, QDir::Name | QDir::Reversed)) { auto* it = new QListWidgetItem(snapLabel(snapshotDir() + "/" + fn)); it->setData(Qt::UserRole, snapshotDir() + "/" + fn); list->addItem(it); }
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel); lay->addWidget(bb); d.resize(560, 360);
        connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept); connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject); connect(list, &QListWidget::itemDoubleClicked, &d, &QDialog::accept);
        if (d.exec() == QDialog::Accepted && list->currentItem()) openSnapshotFile(list->currentItem()->data(Qt::UserRole).toString()); });
    // menús dinámicos
    for (QAction* top : menuBar()->actions())
        if (top->menu() && QString(top->text()).remove('&') == "File")
            for (QAction* a : top->menu()->actions())
            {
                const QString t = QString(a->text()).remove('&');
                if (a->menu() && t == "Recent Workspaces") { m_wsRecentMenu = a->menu(); connect(m_wsRecentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildWorkspaceMenus); }
                if (a->menu() && t == "Recent Quick Snapshots") { m_snapRecentMenu = a->menu(); connect(m_snapRecentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildWorkspaceMenus); }
            }
    rebuildWorkspaceMenus();

    // New… (desde plantilla)
    m->bind("ID_FILE_NEW_EXT", [this]{
        QDialog d(this); d.setWindowTitle("Nuevo desde plantilla"); auto* lay = new QVBoxLayout(&d); lay->addWidget(new QLabel("Elija una plantilla:")); auto* list = new QListWidget(); lay->addWidget(list);
        list->addItem("Script vacío"); for (const Snippet& s : snippets()) list->addItem(s.title); list->setCurrentRow(0);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); lay->addWidget(bb); d.resize(420, 480);
        connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept); connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject); connect(list, &QListWidget::itemDoubleClicked, &d, &QDialog::accept);
        if (d.exec() != QDialog::Accepted) return; auto* e = addEditor(); if (!e) return;
        const int row = list->currentRow(); if (row > 0) { QString body = snippets().at(row - 1).body; const int at = body.indexOf("$0"); body.remove("$0"); e->setPlainText(body); QTextCursor c = e->textCursor(); c.setPosition(at < 0 ? 0 : at); e->setTextCursor(c); }
        e->setFocus(); });
    // Move / Rename
    m->bind("ID_FILE_RENAME", [this]{ auto* e = editor(); if (!e) return;
        if (e->filePath.isEmpty()) { m_status->setText("El script aún no tiene archivo: use Save As."); return; }
        const QString to = QFileDialog::getSaveFileName(this, "Mover o renombrar el archivo", e->filePath); if (to.isEmpty() || to == e->filePath) return;
        if (QFile::exists(to) && !QFile::remove(to)) { QMessageBox::warning(this, "Mover", "No se pudo reemplazar el archivo existente."); return; }
        if (!QFile::rename(e->filePath, to)) { if (!QFile::copy(e->filePath, to) || !QFile::remove(e->filePath)) { QMessageBox::warning(this, "Mover", "No se pudo mover el archivo."); return; } }
        e->filePath = to; updateEditorTitle(e); addRecent(to); m_status->setText("Archivo movido a " + to); });
    // Send Mail: abre el cliente de correo con el texto (la selección o todo, hasta ~1800 caracteres: límite de los enlaces mailto)
    m->bind("ID_FILE_SEND_MAIL", [this]{ auto* e = editor(); if (!e) return; QString t = e->textCursor().hasSelection() ? e->textCursor().selectedText().replace(QChar(0x2029), '\n') : e->toPlainText();
        const bool cut = t.size() > 1800; if (cut) t = t.left(1800) + "\n…(recortado)"; QUrl u("mailto:"); QUrlQuery q; q.addQueryItem("subject", e->displayName()); q.addQueryItem("body", t); u.setQuery(q);
        QDesktopServices::openUrl(u); m_status->setText(cut ? "Correo preparado (texto recortado a 1800 caracteres; adjunte el archivo si es largo)." : "Correo preparado."); });
}
