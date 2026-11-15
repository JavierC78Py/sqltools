// Reglas de texto del Object Viewer (copiar / arrastrar al editor), tomadas de SQLTools para Windows.
#include "../ui/ViewerText.h"
#include <QApplication>
#include <QSettings>
#include <QTreeWidgetItem>
#include <cstdio>
static int bad = 0, total = 0;
static void check (const char* n, const QString& got, const QString& want) { ++total; if (got == want) { printf("[ OK ] %s\n", n); return; } ++bad; printf("[FALLO] %s\n   obtenido: %s\n   esperado: %s\n", n, qPrintable(got), qPrintable(want)); }
static QTreeWidgetItem* member (QTreeWidgetItem* parent, const char* raw, const char* kind, QStringList params)
{
    auto* m = new QTreeWidgetItem(parent); m->setText(0, QString(raw).toLower()); m->setData(0, RoleCopy, kind); m->setData(0, RoleRaw, raw);
    for (const QString& p : params) { auto* c = new QTreeWidgetItem(m); c->setText(0, p.toLower()); c->setData(0, RoleCopy, "param"); c->setData(0, RoleRaw, p); }
    return m;
}
int main (int argc, char** argv)
{
    QApplication app(argc, argv); app.setOrganizationName("SQLToolsMacTest"); app.setApplicationName("viewer_text_test"); QSettings().clear();
    QTreeWidget tree; auto* pkg = new QTreeWidgetItem(&tree); pkg->setText(0, "KMELOT.PKG_X"); pkg->setData(0, RoleOwner, "KMELOT"); pkg->setData(0, RoleName, "PKG_X"); pkg->setData(0, RoleType, "PACKAGE");
    auto* anula = member(pkg, "P_ANULA_ORIGEN", "member", {"P_ID_EMPRESA", "P_ORIGEN", "P_ID_ORIGEN"}); auto* sinParams = member(pkg, "P_INICIA", "member", {});
    check("procedimiento de paquete: nombre(p => , …);", viewerText({anula}, {}), "p_anula_origen(p_id_empresa => , p_origen => , p_id_origen => );");
    check("sin parámetros: nombre();", viewerText({sinParams}, {}), "p_inicia();");
    check("⌘ = columnas alineadas", viewerText({anula}, Qt::ControlModifier), "p_anula_origen\n(\n  p_id_empresa => ,\n  p_origen     => ,\n  p_id_origen  => \n);\n");
    check("⌘ sin parámetros", viewerText({sinParams}, Qt::ControlModifier), "p_inicia();\n");
    check("varios elementos = nombres separados por coma", viewerText({anula, sinParams}, {}), "p_anula_origen, p_inicia");
    QSettings().setValue("viewer/copyWithPackage", true); QSettings().setValue("ddl/options", QString());
    auto* col = new QTreeWidgetItem(pkg); col->setText(0, "ID_EMPRESA"); col->setText(1, "number(10,0) not null"); col->setData(0, RoleCopy, "column");
    check("columna", viewerText({col}, {}), "ID_EMPRESA"); check("columna con Mayús lleva el tipo", viewerText({col}, Qt::ShiftModifier), "ID_EMPRESA NUMBER(10,0)");
    check("objeto (paquete/tabla): su nombre", viewerText({pkg}, {}), "pkg_x");
    auto* odd = new QTreeWidgetItem(pkg); odd->setText(0, "Mi Col"); odd->setData(0, RoleCopy, "param"); odd->setData(0, RoleRaw, "Mi Col");
    check("nombres no normales van entre comillas", viewerText({odd}, {}), "\"Mi Col\"");
    printf("%d correctas, %d fallos\n", total - bad, bad); return bad ? 1 : 0;
}
