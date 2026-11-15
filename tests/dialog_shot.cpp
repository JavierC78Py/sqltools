// Captura del gestor de conexiones con datos de ejemplo (ficticios). Usa un archivo de configuración temporal y NO toca las preferencias reales.
#include "../ui/ConnectionManager.h"
#include <QApplication>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QTimer>
#include <QDateTime>

int main (int argc, char** argv)
{
    QApplication app(argc, argv); QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
    QApplication::setOrganizationName("SqlToolsMacPRUEBA"); QApplication::setApplicationName("dialog-shot");   // QSettings por defecto del diálogo -> dominio de prueba
    QTemporaryDir dir; QSettings s(dir.path() + "/t.ini", QSettings::IniFormat);
    ConnectionStore st(&s, "SQLToolsMac-PRUEBA");
    struct E { const char *tag, *user, *alias; int count; int daysAgo; };
    const E ej[] = { {"Producción","app_ventas","prod_high",357,0}, {"Producción","reportes","prod_high",9,3}, {"Producción","admin_bd","prod_high",140,0}, {"Desarrollo","dev_ana","dev_high",185,1},
                     {"Desarrollo","dev_luis","dev_high",12,40}, {"Pruebas","qa_usuario","qa_high",7,2}, {"Pruebas","qa_admin","qa_high",14,9} };
    for (const auto& e : ej) { ConnectionProfile p; p.id = ConnectionStore::newId(); p.tag = e.tag; p.user = e.user; p.alias = e.alias; p.count = e.count;
        p.lastUsed = QDateTime::currentDateTime().addDays(-e.daysAgo); p.tnsAdmin = "/ruta/ejemplo/wallet"; st.save(p); }
    ConnectionManager dlg(&st);
    dlg.show();
    QTimer::singleShot(400, [&]{ dlg.grab().save(argv[1]); QApplication::quit(); });
    return app.exec();
}
