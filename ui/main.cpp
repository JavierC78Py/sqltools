#include <QApplication>
#include <QIcon>
#include <QSettings>
#include <cstdio>
#include "MainWindow.h"

int main (int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("SqlToolsMac");
    // Las ejecuciones de prueba (variables SQLT_*) usan preferencias aparte: nunca tocan las reales del usuario.
    const bool testRun = qEnvironmentVariableIsSet("SQLT_SELFTEST") || qEnvironmentVariableIsSet("SQLT_NOSHOW") || qEnvironmentVariableIsSet("SQLT_SCREENSHOT")
                         || qEnvironmentVariableIsSet("SQLT_USER") || qEnvironmentVariableIsSet("SQLT_OPEN");
    QApplication::setApplicationName(testRun ? "SQLTools Mac SELFTEST" : "SQLTools Mac");
    QApplication::setWindowIcon(QIcon(":/app/icon.png"));
    // Apariencia: por defecto clara (los iconos originales de SQLTools están pensados para fondo claro). View > Appearance la cambia.
    MainWindow::applyAppearance(qEnvironmentVariable("SQLT_APPEARANCE", QSettings().value("ui/appearance", "light").toString()));
    MainWindow w;
    if (qEnvironmentVariableIsSet("SQLT_NOSHOW")) w.setAttribute(Qt::WA_DontShowOnScreen, true);       // pruebas: dibuja sin mostrar la ventana
    if (app.arguments().contains("--dump-commands")) { for (const QString& l : w.dumpCommands()) std::puts(qPrintable(l)); return 0; }
    w.show();
    w.autoStartFromEnvironment();
    return app.exec();
}
