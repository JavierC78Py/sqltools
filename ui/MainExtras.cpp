// Comandos varios: Execute Current Alternative, variables de sustitución, SQL*Plus, Table Transformation Helper,
// Create Template, ayuda (SQL Short Reference y guía) e impresión.
#include "MainWindow.h"
#include "CommandMenu.h"
#include "CompletionData.h"
#include "DbWorker.h"
#include "ObjectBrowser.h"
#include "Snippets.h"
#include "SettingsDialog.h"
#include "DdlPreferencesDialog.h"
#include "SqlEditor.h"
#include "SqlReference.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPageSetupDialog>
#include <QPlainTextEdit>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryFile>
#include <QTextBrowser>
#include <QVBoxLayout>

// ---- variables de sustitución: &nombre y &&nombre (como SQL*Plus) ----
bool MainWindow::applySubstitution (QStringList& stmts)
{
    if (!QSettings().value("script/substitution", false).toBool()) return true;
    static const QRegularExpression re(R"(&&?([A-Za-z_][\w$#]*)\.?)");
    QStringList names; QSet<QString> seen;
    for (const QString& s : stmts) { auto it = re.globalMatch(s); while (it.hasNext()) { const QString n = it.next().captured(1).toUpper(); if (!seen.contains(n) && !m_substValues.contains(n)) { seen.insert(n); names << n; } } }
    // las que usan && ya recordadas no se vuelven a pedir; el resto sí
    QStringList ask; QSet<QString> sticky; for (const QString& s : stmts) { auto it = re.globalMatch(s); while (it.hasNext()) { auto m = it.next(); if (m.captured(0).startsWith("&&")) sticky.insert(m.captured(1).toUpper()); } }
    { QSet<QString> sa; for (const QString& s : stmts) { auto it = re.globalMatch(s); while (it.hasNext()) { const QString n = it.next().captured(1).toUpper(); if (!sa.contains(n) && !(sticky.contains(n) && m_substValues.contains(n))) { sa.insert(n); ask << n; } } } }
    QHash<QString, QString> vals;
    if (!ask.isEmpty())
    {
        QDialog d(this); d.setWindowTitle("Variables de sustitución"); auto* form = new QFormLayout(&d); QHash<QString, QLineEdit*> edits;
        form->addRow(new QLabel("El script usa variables &nombre. Escriba su valor (se pegará tal cual, sin comillas):"));
        for (const QString& n : ask) { auto* e = new QLineEdit(m_substValues.value(n)); edits[n] = e; form->addRow("&" + n.toLower(), e); }
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); form->addRow(bb); connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept); connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        if (!ask.isEmpty()) edits[ask.first()]->setFocus();
        if (d.exec() != QDialog::Accepted) return false;
        for (const QString& n : ask) vals[n] = edits[n]->text();
    }
    for (const QString& n : sticky) if (!vals.contains(n)) vals[n] = m_substValues.value(n);
    for (auto it = vals.constBegin(); it != vals.constEnd(); ++it) if (sticky.contains(it.key())) m_substValues[it.key()] = it.value();      // && se recuerda en la sesión
    for (QString& s : stmts)
    {
        QString out; int last = 0; auto it = re.globalMatch(s);
        while (it.hasNext()) { auto m = it.next(); out += s.mid(last, m.capturedStart() - last); out += vals.contains(m.captured(1).toUpper()) ? vals.value(m.captured(1).toUpper()) : m_substValues.value(m.captured(1).toUpper()); last = m.capturedEnd(); }
        out += s.mid(last); s = out;
    }
    return true;
}

void MainWindow::setupExtraCommands ()
{
    using R = CommandMenu;
    CommandMenu* m = m_menu;

    // Execute Current Alternative (⌥F10): el otro estilo de delimitación (si F10 usa ; y /, este usa líneas en blanco, y al revés)
    m->bind("ID_SQL_EXECUTE_CURRENT_ALT", [this]{ runBlock(QSettings().value("exec/style", 0).toInt() == 0); }, R::NeedConnection | R::NeedIdle);

    // variables de sustitución
    m->bind("ID_SQL_SUBSTITUTION_VARIABLES", [this]{ QSettings().setValue("script/substitution", m_menu->action("ID_SQL_SUBSTITUTION_VARIABLES")->isChecked()); m_status->setText(QSettings().value("script/substitution").toBool() ? "Variables de sustitución activadas: &nombre se pide antes de ejecutar." : "Variables de sustitución desactivadas."); },
            R::None, true, QSettings().value("script/substitution", false).toBool());

    // SQL*Plus: abre Terminal con sqlplus ejecutando el script (la contraseña se escribe en Terminal, nunca pasa por aquí)
    m->bind("ID_SQL_EXECUTE_IN_SQLPLUS", [this]{
        auto* e = editor(); if (!e) return;
        QString sqlplus = QStandardPaths::findExecutable("sqlplus");
        for (const QString& d : {QCoreApplication::applicationDirPath() + "/../Frameworks", QDir::homePath() + "/instantclient", QString("/opt/oracle/instantclient"), QString("/usr/local/bin"), QString("/opt/homebrew/bin")}) if (sqlplus.isEmpty() && QFile::exists(d + "/sqlplus")) sqlplus = d + "/sqlplus";
        if (sqlplus.isEmpty()) { QMessageBox::information(this, "SQL*Plus", "No se encontró el programa «sqlplus» en este Mac.\n\nEl Instant Client básico no lo incluye: descargue el paquete «SQL*Plus» del Instant Client de Oracle para macOS y déjelo en el PATH o en ~/instantclient."); return; }
        QString text = e->textCursor().hasSelection() ? e->textCursor().selectedText().replace(QChar(0x2029), '\n') : e->toPlainText();
        QTemporaryFile* f = new QTemporaryFile(QDir::tempPath() + "/sqltools_XXXXXX.sql"); f->setAutoRemove(false); if (!f->open()) { delete f; return; }
        f->write(text.toUtf8()); if (!text.endsWith('\n')) f->write("\n"); f->write("exit\n"); f->close();
        const QString conn = m_connected ? m_conn->text().section("  |  ", -2, -2).trimmed() : QString();   // usuario@servidor tal como se muestra
        const QString who = m_lastUser.isEmpty() ? QString() : m_lastUser + (m_lastAlias.isEmpty() ? QString() : "@" + m_lastAlias);
        const QString cmd = QString("cd %1; %2 %3 @%4").arg("'" + QDir::tempPath() + "'", "'" + sqlplus + "'", who.isEmpty() ? QString("/nolog") : who, "'" + f->fileName() + "'");
        QProcess::startDetached("osascript", {"-e", "tell application \"Terminal\" to activate", "-e", "tell application \"Terminal\" to do script \"" + QString(cmd).replace("\\", "\\\\").replace("\"", "\\\"") + "\""});
        m_status->setText("sqlplus abierto en Terminal; escriba la contraseña allí."); (void)conn; });

    // Table Transformation Helper
    m->bind("ID_SQL_TABLE_TRANSFORMER", [this]{
        QDialog d(this); d.setWindowTitle("Table Transformation Helper"); auto* form = new QFormLayout(&d);
        form->addRow(new QLabel("Genera, SIN ejecutar nada, el script para reconstruir una tabla\n(bloquear, renombrar, recrear con la nueva definición, copiar los datos y restablecer índices,\nrestricciones, disparadores, comentarios y permisos). Solo lee el diccionario."));
        auto* schema = new QComboBox(); schema->setEditable(true); schema->addItems(m_meta->schemas); schema->setCurrentText(m_browser->owner());
        auto* table = new QComboBox(); table->setEditable(true); table->addItems(m_browser->objectNames("TABLE")); table->setCurrentText(editor() ? editor()->wordUnderCursor().toUpper() : QString());
        form->addRow("Esquema", schema); form->addRow("Tabla", table);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); form->addRow(bb); connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept); connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        if (d.exec() != QDialog::Accepted || table->currentText().trimmed().isEmpty()) return;
        m_status->setText("Generando el script de transformación…");
        QMetaObject::invokeMethod(m_worker, "transformTable", Qt::QueuedConnection, Q_ARG(QString, schema->currentText().trimmed().toUpper()), Q_ARG(QString, table->currentText().trimmed().toUpper())); }, R::NeedConnection | R::NeedIdle);

    // Create Template: la selección pasa a ser una plantilla propia (se expande con su abreviatura)
    m->bind("ID_EDIT_CREATE_TEMPLATE", [this]{
        auto* e = editor(); if (!e) return; if (!e->textCursor().hasSelection()) { m_status->setText("Seleccione primero el texto que será la plantilla."); return; }
        QString body = e->textCursor().selectedText().replace(QChar(0x2029), '\n');
        QDialog d(this); d.setWindowTitle("Create Template"); auto* form = new QFormLayout(&d); auto* trig = new QLineEdit(); auto* title = new QLineEdit();
        form->addRow(new QLabel("La selección se guardará como plantilla. Escriba la abreviatura que la expandirá\n(la misma que ya exista se reemplaza). Use $0 donde debe quedar el cursor.")); form->addRow("Abreviatura", trig); form->addRow("Nombre", title);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); form->addRow(bb); connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept); connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
        if (d.exec() != QDialog::Accepted) return; const QString t = trig->text().trimmed();
        if (t.isEmpty() || t.contains(QRegularExpression("\\s"))) { QMessageBox::warning(this, "Create Template", "La abreviatura no puede estar vacía ni tener espacios."); return; }
        addUserSnippet({t, title->text().trimmed().isEmpty() ? t : title->text().trimmed(), body}); m_status->setText("Plantilla «" + t + "» guardada. Escríbala y pulse Ctrl+Espacio para usarla."); });

    // Ayuda
    auto showHelp = [this](bool reference) {
        QDialog* d = new QDialog(this); d->setAttribute(Qt::WA_DeleteOnClose); d->setWindowTitle(reference ? "SQL Short Reference" : "Guía de herramientas"); d->resize(980, 680);
        auto* lay = new QHBoxLayout(d); auto* browser = new QTextBrowser(); browser->setOpenExternalLinks(true);
        QFont f = browser->font(); f.setPointSize(f.pointSize() + 1); browser->setFont(f);
        if (reference)
        {
            auto* left = new QWidget(); auto* ll = new QVBoxLayout(left); ll->setContentsMargins(0, 0, 0, 0); auto* filter = new QLineEdit(); filter->setPlaceholderText("Buscar tema o palabra…"); auto* list = new QListWidget(); ll->addWidget(filter); ll->addWidget(list);
            for (const RefTopic& t : sqlReference()) { auto* it = new QListWidgetItem(t.title); it->setData(Qt::UserRole, t.keys); it->setData(Qt::UserRole + 1, "## " + t.title + "\n\n" + t.md); list->addItem(it); }
            connect(list, &QListWidget::currentItemChanged, d, [browser](QListWidgetItem* it){ if (it) browser->setMarkdown(it->data(Qt::UserRole + 1).toString()); });
            connect(filter, &QLineEdit::textChanged, d, [list](const QString& q){ int first = -1; for (int i = 0; i < list->count(); ++i) { auto* it = list->item(i); const bool hit = q.isEmpty() || it->text().contains(q, Qt::CaseInsensitive) || it->data(Qt::UserRole).toString().contains(q, Qt::CaseInsensitive) || it->data(Qt::UserRole + 1).toString().contains(q, Qt::CaseInsensitive); it->setHidden(!hit); if (hit && first < 0) first = i; } if (first >= 0) list->setCurrentRow(first); });
            auto* split = new QSplitter(); split->addWidget(left); split->addWidget(browser); split->setSizes({260, 720}); lay->addWidget(split);
            const QString w = editor() ? editor()->wordUnderCursor().toLower() : QString(); int pick = 0;
            if (!w.isEmpty()) for (int i = 0; i < list->count(); ++i) if (list->item(i)->data(Qt::UserRole).toString().split(' ').contains(w)) { pick = i; break; }
            list->setCurrentRow(pick);
        }
        else
        {
            QFile g(":/docs/GUIA_HERRAMIENTAS.md"); if (g.open(QIODevice::ReadOnly)) browser->setMarkdown(QString::fromUtf8(g.readAll())); else browser->setPlainText("No se encontró la guía integrada.");
            lay->addWidget(browser);
        }
        d->show(); };
    m->bind("ID_SQL_HELP", [showHelp]{ showHelp(true); });
    m->bind("ID_HELP_FINDER", [showHelp]{ showHelp(false); });

    // Window > Split: segunda vista del mismo script (para ver dos partes de un paquete largo a la vez)
    m->bind("ID_WINDOW_SPLIT", [this]{ const bool on = m_menu->action("ID_WINDOW_SPLIT")->isChecked(); m_view2->setVisible(on); if (on) { syncSplitView(); m_editorSplit->setSizes({300, 200}); } }, R::None, true, false);
    // Mover el divisor entre el editor y los resultados
    auto moveSplitter = [this](int d) { auto sz = m_split->sizes(); if (sz.size() == 2) { sz[0] = qMax(40, sz[0] + d); sz[1] = qMax(40, sz[1] - d); m_split->setSizes(sz); } };
    m->bind("ID_CUSTOM_SPLITTER_UP", [moveSplitter]{ moveSplitter(-60); });
    m->bind("ID_CUSTOM_SPLITTER_DOWN", [moveSplitter]{ moveSplitter(60); });

    // Impresión
    auto printer = [this]() -> QPrinter* { if (!m_printer) m_printer = new QPrinter(QPrinter::HighResolution); return m_printer; };
    m->bind("ID_FILE_PRINT", [this, printer]{ auto* e = editor(); if (!e) return; QPrintDialog d(printer(), this); d.setWindowTitle("Imprimir " + e->displayName()); if (d.exec() == QDialog::Accepted) e->print(printer()); });
    m->bind("ID_FILE_PRINT_PREVIEW", [this, printer]{ auto* e = editor(); if (!e) return; QPrintPreviewDialog d(printer(), this); d.setWindowTitle("Vista previa — " + e->displayName()); connect(&d, &QPrintPreviewDialog::paintRequested, &d, [e](QPrinter* p){ e->print(p); }); d.resize(900, 700); d.exec(); });
    m->bind("ID_FILE_PRINT_SETUP", [this, printer]{ QPageSetupDialog d(printer(), this); d.exec(); });
    m->bind("ID_EDIT_PRINT_PAGE_SETUP", [this, printer]{ QPageSetupDialog d(printer(), this); d.exec(); });
}


// ---- Load DDL (como SQLTools de Windows: pregunta las opciones y genera el DDL completo) ----
void MainWindow::loadDdl (const QStringList& objects, bool oneFile, bool askDialog, const QString& ownerIn)
{
    if (!m_connected || objects.isEmpty()) return;
    DdlOptions o = DdlOptions::load();
    if (askDialog && (!o.onlyWithShift || (QGuiApplication::queryKeyboardModifiers() & Qt::ShiftModifier)))
    {
        DdlPreferencesDialog d(o, this); if (d.exec() != QDialog::Accepted) return;
        const bool shiftFlag = d.options().onlyWithShift; if (shiftFlag != o.onlyWithShift) { o.onlyWithShift = shiftFlag; o.save(); }     // este ajuste sí es permanente
        const bool keep = o.onlyWithShift; o = d.options(); o.onlyWithShift = keep;
    }
    m_status->setText(QString("Leyendo el DDL de %1 objeto(s)…").arg(objects.size()));
    QMetaObject::invokeMethod(m_worker, "loadObjectDdl", Qt::QueuedConnection, Q_ARG(QString, ownerIn.isEmpty() ? m_browser->owner() : ownerIn), Q_ARG(QStringList, objects), Q_ARG(QVariantMap, o.toMap()), Q_ARG(bool, oneFile));
}

// ---- Menú contextual del explorador de objetos ----
// Sentencia de una acción de mantenimiento sobre un objeto (qname = nombre ya calificado/entrecomillado)
QStringList browserSql (const QString& action, const QString& type, const QString& qname)
{
    const QString t = type.toLower();
    if (action == "drop") return { "drop " + t + " " + qname };
    if (action == "truncate" && type == "TABLE") return { "truncate table " + qname };
    if (action == "compile") return { "alter " + t + " " + qname + " compile" };
    if ((action == "enable" || action == "disable") && type == "TRIGGER") return { "alter trigger " + qname + " " + action };
    return {};
}

static QString qualifiedName (const QString& owner, const QString& current, const QString& name)
{
    static const QRegularExpression plain("^[A-Z][A-Z0-9_$#]*$");
    const QString n = plain.match(name).hasMatch() ? name : "\"" + name + "\"";
    return owner.isEmpty() || owner == current ? n : owner + "." + n;
}

void MainWindow::onBrowserAction (const QString& action, const QStringList& objs)
{
    const QString owner = m_browser->owner();
    auto type = [](const QString& o) { return o.section(QChar(0x1f), 0, 0); }; auto name = [](const QString& o) { return o.section(QChar(0x1f), 1); };
    if (action == "refresh") { if (m_connected) QMetaObject::invokeMethod(m_worker, "loadObjectsOf", Qt::QueuedConnection, Q_ARG(QString, owner)); return; }
    if (action == "settings") { SettingsDialog d(this, 6); if (d.exec() == QDialog::Accepted) { for (int i = 0; i < m_editors->count(); ++i) if (auto* x = qobject_cast<SqlEditor*>(m_editors->widget(i))) x->applyEditorSettings(); applyExecOptions(); m_status->setText("Ajustes guardados."); } return; }
    if (objs.isEmpty() || !m_connected) return;
    if (action == "query") { quickQueryOn(qualifiedName(owner, m_meta->currentSchema, name(objs.first())), false); return; }
    if (action == "viewer") { if (m_showInViewer) m_showInViewer(name(objs.first())); return; }
    if (action == "ddl") { loadDdl(objs, false, true); return; }
    if (action == "ddl-one") { loadDdl(objs, true, true); return; }
    if (action == "ddl-meta") { for (const QString& o : objs) QMetaObject::invokeMethod(m_worker, "loadSource", Qt::QueuedConnection, Q_ARG(QString, owner), Q_ARG(QString, type(o)), Q_ARG(QString, name(o)), Q_ARG(bool, action == "ddl-meta")); m_status->setText(QString("Leyendo el DDL de %1 objeto(s)…").arg(objs.size())); return; }
    if (action == "transform") { QMetaObject::invokeMethod(m_worker, "transformTable", Qt::QueuedConnection, Q_ARG(QString, owner), Q_ARG(QString, name(objs.first()))); m_status->setText("Generando el script de transformación…"); return; }
    // acciones que MODIFICAN la base: se muestran las sentencias y se pide confirmación; se ejecutan como cualquier script (respetan «Read Only» y quedan en el historial)
    QStringList stmts;
    if (action == "compile-all") stmts << QString("begin dbms_utility.compile_schema(schema => '%1', compile_all => false); end;").arg(owner);
    else for (const QString& o : objs) stmts << browserSql(action, type(o), qualifiedName(owner, m_meta->currentSchema, name(o)));
    if (stmts.isEmpty()) return;
    if (m_busy) { m_status->setText("Hay una ejecución en curso."); return; }
    const bool destructive = action == "drop" || action == "truncate";
    QMessageBox box(this); box.setIcon(destructive ? QMessageBox::Warning : QMessageBox::Question); box.setWindowTitle(destructive ? "Confirmar: modifica la base de datos" : "Confirmar");
    box.setText(QString("Se ejecutará en %1:").arg(owner)); QString list; for (const QString& st : stmts) list += st + ";\n";
    box.setInformativeText(list + (destructive ? "\nEsta acción no se puede deshacer." : QString())); box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel); box.setDefaultButton(QMessageBox::Cancel);
    if (box.exec() != QMessageBox::Ok) return;
    QVector<int> lines(stmts.size(), 1); startRun(stmts, lines);
}
