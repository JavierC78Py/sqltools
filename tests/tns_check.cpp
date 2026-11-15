// Revisa una carpeta TNS e imprime el estado de cada alias (equivale a «Comprobar TNS…» de Ajustes). Uso: tns_check <carpeta>
#include "../ui/WalletResolver.h"
#include <QCoreApplication>
#include <QDir>
#include <cstdio>
int main (int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) return 2;
    const auto list = WalletResolver::inspect(argv[1], QDir::homePath());
    int bad = 0;
    for (const auto& c : list) { bad += !c.ok; std::printf("%-8s %-24s %-12s %s\n", c.ok ? "OK" : "REVISAR", qPrintable(c.alias), c.tls ? "TLS(wallet)" : "TCP", qPrintable(c.message.left(150))); }
    std::printf("\n%lld alias, %d para revisar\n", (long long)list.size(), bad);
}
