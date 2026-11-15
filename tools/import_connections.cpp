// Importa un connections.xml de SQLTools (sin contraseñas) a las preferencias REALES de la app. Uso: import_connections <connections.xml>
// Equivale al botón «Importar…» del gestor, sin pedir confirmación (se usa cuando el usuario ya la dio).
#include "../ui/ConnectionImporter.h"
#include <QCoreApplication>
#include <cstdio>

int main (int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("SqlToolsMac"); QCoreApplication::setApplicationName("SQLTools Mac");      // mismos nombres que la app
    if (argc < 2) return 2;
    QList<ConnectionProfile> list; QString err;
    if (!ConnectionImporter::parseSqlToolsXml(argv[1], &list, &err)) { std::fprintf(stderr, "ERROR: %s\n", qPrintable(err)); return 1; }
    QSettings settings; ConnectionStore store(&settings);
    const int before = store.all().size(); int added = 0, dup = 0;
    for (auto p : list)
    {
        bool isDup = false; for (const auto& e : store.all()) isDup = isDup || ConnectionImporter::sameConnection(e, p);
        if (isDup) { ++dup; continue; }
        p.tnsAdmin.clear();                                   // usarán la carpeta TNS global
        store.save(p); ++added;
    }
    std::printf("Antes: %d conexiones. Leídas: %lld. Añadidas: %d. Ya existían: %d. Ahora: %lld.\n", before, (long long)list.size(), added, dup, (long long)store.all().size());
}
