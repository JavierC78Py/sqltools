#include "CommandMenu.h"
#include <QAction>
#include <QFile>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequence>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QToolBar>

CommandMenu::CommandMenu (QMainWindow* w) : QObject(w), m_win(w) {}

static QString cleanLabel (QString s) { s.remove('&'); return s.simplified(); }

// ---- Atajos de Windows -> Mac -------------------------------------------------------------------------------------------
// Reglas: Ctrl -> Cmd (en Parallels ya era así), Alt -> Opción. Los que chocan con atajos del sistema (Cmd+Q salir, Cmd+H ocultar,
// Cmd+M minimizar, Cmd+W cerrar ventana, Cmd+Espacio Spotlight) usan la tecla Control física (Meta en Qt).
QList<QKeySequence> CommandMenu::macSequences (const QStringList& winKeys, QStringList* notes)
{
    QList<QKeySequence> out;
    for (const QString& w : winKeys)
    {
        QStringList chords = w.split(',', Qt::SkipEmptyParts);
        QStringList qt;
        bool skip = false;
        for (int i = 0; i < chords.size() && !skip; ++i)
        {
            QString c = chords[i].trimmed();
            if (c.contains("Num5")) { skip = true; if (notes) *notes << (w + ": no hay teclado numérico"); break; }
            c.replace("Enter", "Return").replace("PgUp", "PgUp").replace("Backspace", "Backspace");
            QStringList mods; QString key;
            for (const QString& t : c.split('+', Qt::KeepEmptyParts)) { if (t == "Ctrl" || t == "Shift" || t == "Alt") mods << t; else key = t.isEmpty() ? QStringLiteral("+") : t; }
            if (i == 0 && mods == QStringList{"Ctrl"} && (key == "Q" || key == "H" || key == "M" || key == "W" || key == "Space" || key == "Tab"))
            {   // choca con un atajo del sistema: se usa la tecla Control física
                mods = QStringList{"Meta"}; if (notes) *notes << (w + ": usa Control en lugar de Cmd (Cmd+" + key + " es del sistema)");
            }
            if (i > 0 && qt[0].startsWith("Meta+")) { /* la segunda tecla de un acorde va sin modificadores */ }
            qt << ((mods.isEmpty() ? QString() : mods.join('+') + "+") + key);
        }
        if (skip) continue;
        const QKeySequence seq = QKeySequence::fromString(qt.join(", "), QKeySequence::PortableText);
        if (seq.isEmpty()) { if (notes) *notes << (w + ": Qt no entiende esta combinación"); continue; }
        out << seq;
    }
    return out;
}

bool CommandMenu::load (QString* error)
{
    QFile f(":/spec/menu_spec.json");
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = "No se encontró el inventario de menús (recurso)."; return false; }
    QJsonParseError pe; const auto doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (doc.isNull()) { if (error) *error = "Inventario de menús inválido: " + pe.errorString(); return false; }
    m_spec = doc.object();
    createActions(m_spec["menu"].toArray(), QString());
    // los comandos que están solo en las barras
    const QJsonObject cmds = m_spec["commands"].toObject();
    for (auto it = cmds.begin(); it != cmds.end(); ++it)
        if (!m_actions.contains(it.key()))
        {
            auto* a = new QAction(it.key(), this);
            const QString help = it.value().toObject()["help"].toString();
            a->setText(help.section('\n', 1, 1).isEmpty() ? help.section('\n', 0, 0) : help.section('\n', 1, 1));
            a->setToolTip(a->text()); a->setEnabled(false);
            if (QFile::exists(":/icons/" + it.key() + ".png")) a->setIcon(QIcon(":/icons/" + it.key() + ".png"));
            m_actions.insert(it.key(), a);
        }
    return true;
}

void CommandMenu::createActions (const QJsonArray& items, const QString& path)
{
    const QJsonObject cmds = m_spec["commands"].toObject();
    for (const auto& v : items)
    {
        const QJsonObject it = v.toObject();
        if (it["type"] == "popup") { createActions(it["children"].toArray(), path.isEmpty() ? cleanLabel(it["label"].toString()) : path + " > " + cleanLabel(it["label"].toString())); continue; }
        if (it["type"] != "item") continue;
        const QString id = it["id"].toString();
        if (m_actions.contains(id)) continue;                      // un mismo comando puede estar en varios menús (p.ej. Copy)
        auto* a = new QAction(cleanLabel(it["label"].toString()), this);
        a->setEnabled(false); a->setObjectName(id);
        const QJsonObject c = cmds[id].toObject();
        const QString help = c["help"].toString();
        a->setToolTip(help.section('\n', 1, 1).isEmpty() ? a->text() : help.section('\n', 1, 1));
        a->setStatusTip(help.section('\n', 0, 0));
        if (QFile::exists(":/icons/" + id + ".png")) a->setIcon(QIcon(":/icons/" + id + ".png"));
        a->setIconVisibleInMenu(false);                             // en los menús, como en Mac, sin iconos
        m_actions.insert(id, a); m_menuPath.insert(id, path);
    }
}

QAction* CommandMenu::bind (const QString& id, std::function<void()> fn, int req, bool checkable, bool checked)
{
    QAction* a = m_actions.value(id);
    if (!a) { m_conflicts << ("comando desconocido: " + id); return nullptr; }
    m_bound.insert(id, req);
    a->setEnabled(true);
    if (checkable) { a->setCheckable(true); a->setChecked(checked); }
    connect(a, &QAction::triggered, this, [fn](bool){ fn(); });
    // atajos: solo los comandos implementados los activan (así un atajo sin función no roba teclas del editor)
    QStringList notes;
    const QStringList winKeys = m_spec["commands"].toObject()[id].toObject()["keys"].toVariant().toStringList();
    QList<QKeySequence> seqs;
    for (const QKeySequence& s : macSequences(winKeys, &notes))
    {
        const QString key = s.toString(QKeySequence::PortableText);
        if (m_usedSequences.contains(key)) { m_conflicts << (id + ": el atajo " + key + " ya lo usa otro comando"); continue; }
        m_usedSequences.insert(key); seqs << s;
    }
    if (!seqs.isEmpty()) a->setShortcuts(seqs);
    for (const QString& n : notes) m_conflicts << (id + ": " + n);
    return a;
}

QMenu* CommandMenu::buildMenu (const QJsonArray& items, QMenu* menu)
{
    for (const auto& v : items)
    {
        const QJsonObject it = v.toObject();
        if (it["type"] == "separator") { menu->addSeparator(); continue; }
        if (it["type"] == "popup") { QMenu* sub = menu->addMenu(cleanLabel(it["label"].toString())); buildMenu(it["children"].toArray(), sub); continue; }
        if (auto* a = m_actions.value(it["id"].toString())) menu->addAction(a);
    }
    return menu;
}

void CommandMenu::buildMenuBar ()
{
    QMenuBar* bar = m_win->menuBar();
    for (const auto& v : m_spec["menu"].toArray())
    {
        const QJsonObject p = v.toObject();
        if (p["type"] != "popup") continue;
        QMenu* m = bar->addMenu(cleanLabel(p["label"].toString()));
        buildMenu(p["children"].toArray(), m);
    }
}

void CommandMenu::fillToolBar (QToolBar* bar, const QString& specName)
{
    const QJsonObject tb = m_spec["toolbars"].toObject()[specName].toObject();
    const int s = tb["size"].toInt(19);
    bar->setObjectName(specName);
    bar->setIconSize(QSize(s, s)); bar->setMovable(false); bar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    for (const auto& b : tb["buttons"].toArray())
    {
        if (b.isNull()) { bar->addSeparator(); continue; }
        if (auto* a = m_actions.value(b.toString())) bar->addAction(a);
    }
}

QToolBar* CommandMenu::buildToolBar (const QString& specName, const QString& title)
{
    auto* bar = m_win->addToolBar(title);
    fillToolBar(bar, specName);
    return bar;
}

void CommandMenu::update (bool connected, bool busy)
{
    for (auto it = m_bound.begin(); it != m_bound.end(); ++it)
    {
        const int req = it.value(); bool on = true;
        if ((req & NeedConnection) && !connected) on = false;
        if ((req & NeedIdle) && busy) on = false;
        if ((req & NeedBusy) && !busy) on = false;
        if (auto* a = m_actions.value(it.key())) a->setEnabled(on);
    }
}

QStringList CommandMenu::dump () const
{
    QStringList out; const QJsonObject cmds = m_spec["commands"].toObject();
    for (auto it = m_actions.begin(); it != m_actions.end(); ++it)
    {
        const QString id = it.key(); const QJsonObject c = cmds[id].toObject();
        QStringList win; for (const auto& k : c["keys"].toArray()) win << k.toString();
        QStringList mac; for (const auto& s : macSequences(win)) mac << s.toString(QKeySequence::NativeText);
        out << QString("%1\t%2\t%3\t%4\t%5\t%6").arg(id, it.value()->text(), m_menuPath.value(id, "(solo barra)"), win.join(" | "), mac.join(" | "), m_bound.contains(id) ? "si" : "no");
    }
    out.sort();
    return out;
}
