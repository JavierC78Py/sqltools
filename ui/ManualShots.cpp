// Genera las capturas del manual de usuario con datos ficticios (SQLT_MANUAL_SHOTS=<carpeta>). Sin base de datos y con preferencias de prueba.
#include "MainWindow.h"
#include "CommandMenu.h"
#include "CompletionData.h"
#include "DbWorker.h"
#include "ExportDialog.h"
#include "DdlPreferencesDialog.h"
#include "FindInFilesDialog.h"
#include "FindReplaceDialog.h"
#include "HistoryStore.h"
#include "ObjectBrowser.h"
#include "ResultModel.h"
#include "SettingsDialog.h"
#include "SqlEditor.h"
#include "SqlFormatter.h"
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTest>
#include <QTimer>

static void settle (int ms = 120) { for (int i = 0; i < 3; ++i) QApplication::processEvents(); QTest::qWait(ms); QApplication::processEvents(); }

void MainWindow::generateManualShots (const QString& dir)
{
    // Solo se parte de ajustes de fábrica en el dominio de PRUEBA; en el dominio real no se borra nada y se aborta.
    if (!QApplication::applicationName().endsWith("SELFTEST")) { qWarning() << "SQLT_MANUAL_SHOTS solo funciona con SQLT_NOSHOW=1 (preferencias de prueba): se cancela para no tocar sus ajustes."; QApplication::quit(); return; }
    QDir().mkpath(dir); QSettings().clear();
    auto save = [&](QWidget* w, const QString& name) { settle(); const bool ok = w->grab().save(dir + "/" + name + ".png"); qInfo().noquote() << (ok ? "captura" : "FALLO") << name; };
    resize(1440, 900); settle(250);

    // ---- datos ficticios ----
    const QString demo =
        "-- Empleados con su departamento\nselect e.id, e.nombre, d.nombre departamento, e.sueldo\n  from empleados e\n  join departamentos d on d.id = e.id_depto\n where e.sueldo > :minimo\n   and e.activo = 1\n order by e.nombre;\n\n"
        "create or replace procedure p_subir_sueldo (p_id in number, p_pct in number) is\n  v_sueldo empleados.sueldo%type;\nbegin\n  select sueldo into v_sueldo from empleados where id = p_id;\n  if p_pct > 20 then\n    raise_application_error(-20001, 'Aumento demasiado alto');\n  end if;\n  update empleados set sueldo = v_sueldo * (1 + p_pct / 100) where id = p_id;\nexception\n  when no_data_found then\n    null;\nend;\n/\n";
    auto* e = editor(); e->setPlainText(demo); e->titleHint = "ejemplo"; updateEditorTitle(e); e->document()->setModified(false); e->moveCursor(QTextCursor::Start);
    m_model->reset({"ID", "NOMBRE", "DEPARTAMENTO", "SUELDO", "FECHA_INGRESO"}, {1, 0, 0, 1, 2});
    m_model->append({{"101", "Ana Torres", "Ventas", "2450.00", "2019.03.15 00:00:00"}, {"102", "Luis Pérez", "Ventas", "2100.50", "2020.07.01 00:00:00"}, {"103", "María Gómez", "Sistemas", "3200.00", "2018.11.20 00:00:00"},
                     {"104", "Carlos Ruiz", "Sistemas", "2980.75", "2021.01.10 00:00:00"}, {"105", "Elena Díaz", "Compras", "2250.00", "2022.05.02 00:00:00"}, {"106", "Jorge Vega", "Compras", QString(), "2023.09.18 00:00:00"},
                     {"107", "Sofía Luna", "Finanzas", "3500.00", "2017.02.27 00:00:00"}, {"108", "Pablo Mora", "Finanzas", "2800.00", "2020.12.01 00:00:00"}});
    m_status->setText("8 fila(s) en 0,03 s"); m_conn->setText("Sin conexión (datos de ejemplo)");
    m_output->setPlainText("Aumento aplicado al empleado 101\nEmpleados procesados: 8"); m_plan->setPlainText("Plan hash value: 1745902114\n\n-----------------------------------------------------------\n| Id  | Operation          | Name        | Rows  | Cost  |\n-----------------------------------------------------------\n|   0 | SELECT STATEMENT   |             |     8 |     6 |\n|   1 |  HASH JOIN         |             |     8 |     6 |\n|   2 |   TABLE ACCESS FULL| DEPARTAMEN. |     4 |     3 |\n|   3 |   TABLE ACCESS FULL| EMPLEADOS   |     8 |     3 |\n-----------------------------------------------------------");
    m_stats->setRowCount(0); for (const QStringList& r : QVector<QStringList>{{"session logical reads", "412"}, {"physical reads", "0"}, {"parse count (total)", "3"}, {"user calls", "9"}, {"bytes sent via SQL*Net to client", "1824"}}) { const int i = m_stats->rowCount(); m_stats->insertRow(i); for (int c = 0; c < 2; ++c) m_stats->setItem(i, c, new QTableWidgetItem(r[c])); }
    m_historyStore->clear(); addHistory(0, "select * from empleados where id = 101", 1, 0.02, QString()); addHistory(0, "update empleados set sueldo = sueldo * 1.05 where id = 101", 1, 0.01, QString()); addHistory(0, "select * from tabla_que_no_existe", 0, 0.01, "ORA-00942: la tabla o vista no existe"); addHistory(0, "select count(*) from empleados", 1, 0.01, QString());

    // ---- ventana principal ----
    showPage(0); save(this, "01_ventana_principal");
    showPage(1); save(this, "02_resultados_salida");
    showPage(3); save(this, "03_resultados_estadisticas");
    showPage(4); save(this, "04_resultados_plan");
    showPage(5); save(this, "05_resultados_historial");
    showPage(6); save(this, "06_resultados_binds"); showPage(0);

    // ---- menú contextual del explorador de objetos (según el tipo de objeto) ----
    m_browser->setSchemas({"VENTAS"}, "VENTAS");
    m_browser->setObjects({{"TABLE", "DEPARTAMENTOS", "VALID"}, {"TABLE", "EMPLEADOS", "VALID"}, {"VIEW", "V_EMPLEADOS", "VALID"}, {"PACKAGE", "PKG_NOMINA", "VALID"}, {"PROCEDURE", "P_SUBIR_SUELDO", "INVALID"}, {"TRIGGER", "TRG_EMPLEADOS", "VALID"}});
    settle(200);
    auto menuShot = [&](const QString& type, const QString& name, const QString& file) {
        if (QMenu* m = m_browser->contextMenuFor(type, name)) { m->adjustSize(); m->ensurePolished(); m->grab().save(dir + "/" + file + ".png"); qInfo().noquote() << "captura" << file; delete m; } };
    menuShot("TABLE", "EMPLEADOS", "explorador_menu_tabla"); menuShot("PACKAGE", "PKG_NOMINA", "explorador_menu_paquete"); menuShot("TRIGGER", "TRG_EMPLEADOS", "explorador_menu_trigger"); menuShot("PROCEDURE", "P_SUBIR_SUELDO", "explorador_menu_invalido");
    // lista de objetos con pestañas por tipo (datos ficticios)
    m_browser->setConnected(false);
    m_browser->setListRows("tables", {{"DEPARTAMENTOS", ".", "USERS", ".", "2019-03-15 09:10:11", "2024-01-20 10:00:00", "12", "5", "2026-10-01 02:00:00"}, {"EMPLEADOS", ".", "USERS", ".", "2019-03-15 09:12:30", "2026-04-22 08:57:59", "1530", "40", "2026-10-01 02:00:05"}, {"EMPLEADOS_JN", ".", "DATOS", "ENABLED", "2020-07-01 12:00:00", "2020-07-01 12:00:00", "88210", "612", "2026-10-01 02:01:00"}, {"VENTAS", "PARTITIONED", "DATOS", ".", "2018-11-02 15:40:00", "2026-09-10 16:42:15", "2450980", "18400", "2026-10-01 02:03:00"}}, QString());
    m_browser->setListRows("packages", {{"PKG_NOMINA", "2020-02-02 10:00:00", "2026-08-01 09:00:00", "VALID"}, {"PKG_UTIL", "2019-05-05 10:00:00", "2026-09-11 11:00:00", "INVALID"}}, QString());
    m_browser->showTab(0); settle(300); save(this, "lista_objetos");
    m_browser->showTab(9); settle(300); save(this, "lista_objetos_paquetes"); m_browser->showTab(0);
    m_browser->clear();

    // ---- Object Viewer (F12) con datos ficticios ----
    {
        auto nd = [](const QString& k, const QString& t, const QString& c1 = QString(), const QString& c2 = QString(), const QVariantList& kids = QVariantList()) { return QVariant(QVariantMap{{"kind", k}, {"text", t}, {"c1", c1}, {"c2", c2}, {"children", kids}}); };
        QVariantList ch; ch << nd("column", "ID_EMPLEADO", "number(10,0) not null", "/*Código */") << nd("column", "NOMBRE", "varchar2(100) not null", "/*Nombre completo */") << nd("column", "ID_DEPTO", "number(10,0)", "/*Departamento */")
          << nd("column", "SUELDO", "number(18,2) default 0 not null", "/*Moneda */") << nd("column", "ACTIVO", "char(2) default 'SI'", "/*SI/NO */") << nd("column", "FECHA_INGRESO", "date not null", "/*Fecha */")
          << nd("folder", "Constraints", "3", "", QVariantList{nd("item", "empleados_pk", "primary key (id_empleado)"), nd("item", "empleados_depto_fk", "foreign key (id_depto)"), nd("item", "empleados_activo_ck", "check (ACTIVO IN ('SI','NO'))")})
          << nd("folder", "Indexes", "2", "", QVariantList{nd("item", "empleados_pk", "unique (id_empleado)"), nd("item", "empleados_depto_i", "(id_depto)")})
          << nd("folder", "Triggers", "1", "", QVariantList{nd("item", "empleados_trg", "before each row insert or update")})
          << nd("folder", "References", "1", "", QVariantList{nd("item", "empleados_depto_fk", "(id_depto) → departamentos (id_depto)")})
          << nd("folder", "Referenced By", "2", "", QVariantList{nd("item", "ventas", "ventas_emp_fk", "(id_empleado)"), nd("item", "asistencia", "asist_emp_fk", "(id_empleado)")})
          << nd("folder", "Dependent Objects", "2", "", QVariantList{nd("item", "v_empleados", "view"), nd("item", "pkg_nomina", "package body")})
          << nd("folder", "Granted Privileges", "2", "", QVariantList{nd("item", "r_consulta", "select"), nd("item", "r_rrhh", "delete,insert,select,update")});
        m_menu->action("ID_SQL_OBJ_VIEWER")->trigger(); if (m_showInViewer) { /* ancho como al abrir con F12 */ } for (QDockWidget* dk : findChildren<QDockWidget*>()) if (dk->windowTitle() == "Object Viewer") resizeDocks({dk}, {600}, Qt::Horizontal); emit m_worker->objectDescribed("VENTAS", "EMPLEADOS", "TABLE", ch, QString()); settle(300);
        if (auto* dk = findChild<QDockWidget*>()) { Q_UNUSED(dk); }
        save(this, "ver_object_viewer"); m_menu->action("ID_SQL_OBJ_VIEWER")->trigger();
    }

    // ---- paneles laterales ----
    for (const char* id : {"ID_VIEW_FILE_PANEL", "ID_VIEW_OPEN_FILES", "ID_VIEW_PROPERTIES"}) m_menu->action(id)->trigger();
    m_outlineDock->show(); refreshOutline(); settle(300); save(this, "07_paneles_laterales");
    for (const char* id : {"ID_VIEW_FILE_PANEL", "ID_VIEW_OPEN_FILES", "ID_VIEW_PROPERTIES"}) m_menu->action(id)->trigger(); m_outlineDock->hide();

    // ---- editor: columna, regla, guías ----
    e->setShowRuler(true); e->setIndentGuides(true); e->setColumnMarkers(true);
    { QTextBlock b = e->document()->findBlockByNumber(11); QTextCursor c(b); c.setPosition(b.position() + 2); e->setTextCursor(c); e->enterColumnMode(); e->columnExtend(5, 9); }
    save(e, "08_editor_columna_regla");
    e->exitColumnMode(); e->setShowRuler(false); e->setIndentGuides(false); e->setColumnMarkers(false); e->moveCursor(QTextCursor::Start);

    // ---- Format SQL antes / después ----
    { e->setPlainText("select a.id,b.nombre,sum(c.total) total from cab_pedido a join cliente b on a.id_cliente=b.id left join det_pedido c on c.id_pedido=a.id where a.estado='A' and b.activo=1 group by a.id,b.nombre order by 3 desc;");
      save(e, "09_formato_antes"); const FormatOptions o = FormatOptions::load(); e->setPlainText(SqlFormatter::format(e->toPlainText(), o)); save(e, "10_formato_despues"); e->setPlainText(demo); e->document()->setModified(false); }

    // ---- menús (se dibujan como ventanas) ----
    for (QAction* top : menuBar()->actions())
        if (auto* m = top->menu()) { QString n = QString(top->text()).remove('&'); n.replace('/', '_'); m->adjustSize(); m->ensurePolished(); QPixmap px = m->grab(); px.save(dir + "/menu_" + n + ".png"); qInfo().noquote() << "captura menu_" + n; }

    // ---- diálogos ----
    for (int t = 0; t < 7; ++t)
    {
        SettingsDialog d(this, t); d.show(); static const char* names[] = {"ajustes_formato", "ajustes_editor", "ajustes_colores", "ajustes_ejecucion", "ajustes_autocompletado", "ajustes_exportacion", "ajustes_ddl"};
        save(&d, names[t]);
    }
    { DdlPreferencesDialog d(DdlOptions(), this); d.show(); save(&d, "ddl_preferencias_locales"); }
    { ExportDialog d(ExportOptions::load(ExportOptions::File), this); d.show(); save(&d, "exportar_dialogo"); }
    { m_findDlg->openFind("empleados"); save(m_findDlg, "buscar_reemplazar"); m_findDlg->hide(); }
    { m_fifDlg->openWith("select"); save(m_fifDlg, "buscar_en_archivos"); m_fifDlg->hide(); }
    { m_menu->action("ID_SQL_HELP")->trigger(); settle(300); for (QWidget* w : QApplication::topLevelWidgets()) if (w->isVisible() && w->windowTitle() == "SQL Short Reference") { save(w, "ayuda_referencia_sql"); w->close(); } }
    { m_menu->action("ID_HELP_FINDER")->trigger(); settle(300); for (QWidget* w : QApplication::topLevelWidgets()) if (w->isVisible() && w->windowTitle() == "Guía de herramientas") { save(w, "ayuda_guia"); w->close(); } }
    QApplication::quit();
}
