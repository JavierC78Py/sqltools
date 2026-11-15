// Pruebas de la resolución de wallet (sin Oracle): carpetas temporales que imitan wallets de Autonomous DB y TNS de Windows.
#include "../ui/WalletResolver.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

static int fails = 0;
static void check (const char* name, bool ok, const QString& extra = QString()) { std::printf("[%s] %s%s\n", ok ? " OK " : "FAIL", name, ok || extra.isEmpty() ? "" : qPrintable("   -> " + extra)); if (!ok) ++fails; }
static void put (const QString& path, const QString& text) { QDir().mkpath(QFileInfo(path).absolutePath()); QFile f(path); f.open(QIODevice::WriteOnly); f.write(text.toUtf8()); }
static QString read (const QString& path) { QFile f(path); f.open(QIODevice::ReadOnly); return QString::fromUtf8(f.readAll()); }

static QString tlsEntry (const QString& alias, const QString& extra = QString())
{ return alias + " = (description= (retry_count=20)(retry_delay=3)(address=(protocol=tcps)(port=1522)(host=h.example.com))(connect_data=(service_name=svc))(security=(ssl_server_dn_match=yes)" + extra + "))\n"; }

int main (int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp; const QString T = tmp.path(), home = T + "/home", overlay = T + "/overlay";

    // 1) wallet estándar de ADB con sqlnet.ora "?/network/admin": hace falta carpeta temporal
    put(T + "/w1/tnsnames.ora", tlsEntry("db_high")); put(T + "/w1/cwallet.sso", "x");
    put(T + "/w1/sqlnet.ora", "WALLET_LOCATION = (SOURCE = (METHOD = file) (METHOD_DATA = (DIRECTORY=\"?/network/admin\")))\nSSL_SERVER_DN_MATCH=yes\n");
    auto p1 = WalletResolver::prepare(T + "/w1", "DB_HIGH", overlay, home);
    check("?/network/admin -> carpeta temporal con la wallet real", p1.overlay && p1.error.isEmpty() && p1.walletDir == T + "/w1" && p1.tnsAdmin.startsWith(overlay), p1.error + " " + p1.tnsAdmin);
    check("sqlnet.ora temporal apunta a la wallet y conserva SSL_SERVER_DN_MATCH", read(p1.tnsAdmin + "/sqlnet.ora").contains("DIRECTORY=\"" + T + "/w1\"") && read(p1.tnsAdmin + "/sqlnet.ora").contains("SSL_SERVER_DN_MATCH") && !read(p1.tnsAdmin + "/sqlnet.ora").contains("?/network"));
    check("los archivos de la wallet se enlazan (no se copian) y los originales no cambian", QFileInfo(p1.tnsAdmin + "/cwallet.sso").isSymLink() && read(T + "/w1/sqlnet.ora").contains("?/network/admin"));

    // 2) sqlnet.ora ya correcto: se usa tal cual
    put(T + "/w2/tnsnames.ora", tlsEntry("db_high")); put(T + "/w2/cwallet.sso", "x"); put(T + "/w2/sqlnet.ora", "WALLET_LOCATION = (SOURCE = (METHOD = file) (METHOD_DATA = (DIRECTORY=\"" + T + "/w2\")))\n");
    auto p2 = WalletResolver::prepare(T + "/w2", "db_high", overlay, home);
    check("sqlnet.ora ya correcto: sin carpeta temporal", !p2.overlay && p2.tnsAdmin == T + "/w2" && p2.error.isEmpty());

    // 3) TNS estilo Windows: my_wallet_directory con ruta de otra unidad, wallet en la propia carpeta
    put(T + "/w3/tnsnames.ora", tlsEntry("db_high", "(my_wallet_directory= C:\\Wallet_DbKmelot)")); put(T + "/w3/cwallet.sso", "x");
    auto p3 = WalletResolver::prepare(T + "/w3", "db_high", overlay, home);
    check("my_wallet_directory=C:\\... inexistente: usa la wallet de la carpeta y reescribe la ruta", p3.overlay && read(p3.tnsAdmin + "/tnsnames.ora").contains("my_wallet_directory= " + T + "/w3") && !read(p3.tnsAdmin + "/tnsnames.ora").contains("C:\\Wallet"), read(p3.tnsAdmin + "/tnsnames.ora"));
    check("el tnsnames.ora original no se modifica", read(T + "/w3/tnsnames.ora").contains("C:\\Wallet_DbKmelot"));

    // 4) my_wallet_directory = C:\Mac\Home\... se traduce a la carpeta personal
    put(home + "/Desktop/CBC/Wallet_x/cwallet.sso", "x");
    put(T + "/w4/tnsnames.ora", tlsEntry("cbc_high", "(my_wallet_directory= C:\\Mac\\Home\\Desktop\\CBC\\Wallet_x)"));
    auto p4 = WalletResolver::prepare(T + "/w4", "cbc_high", overlay, home);
    check("C:\\Mac\\Home\\... se traduce a la carpeta personal y se encuentra la wallet", p4.walletDir == home + "/Desktop/CBC/Wallet_x" && p4.error.isEmpty() && p4.overlay, p4.error + " | " + p4.walletDir);
    check("mapWindowsPath: C:\\Mac\\Home\\a\\b", WalletResolver::mapWindowsPath("C:\\Mac\\Home\\a\\b", "/Users/u") == "/Users/u/a/b");
    check("mapWindowsPath: C:\\Wallet_X queda igual", WalletResolver::mapWindowsPath("C:\\Wallet_X", "/Users/u") == "C:\\Wallet_X");

    // 5) TLS sin ninguna wallet localizable: error claro
    put(T + "/w5/tnsnames.ora", tlsEntry("db_high", "(my_wallet_directory= C:\\Wallet_Perdida)"));
    auto p5 = WalletResolver::prepare(T + "/w5", "db_high", overlay, home);
    check("TLS sin wallet: error que nombra la ruta del TNS", !p5.error.isEmpty() && p5.error.contains("C:\\Wallet_Perdida") && p5.error.contains("cwallet.sso"), p5.error);

    // 6) alias sin TLS (red interna): nada que preparar
    put(T + "/w6/tnsnames.ora", "INV = (DESCRIPTION=(ADDRESS=(PROTOCOL=TCP)(HOST=10.0.0.5)(PORT=1521))(CONNECT_DATA=(SERVICE_NAME=inv)))\n");
    auto p6 = WalletResolver::prepare(T + "/w6", "inv", overlay, home);
    check("alias TCP sin wallet: se usa la carpeta tal cual", !p6.overlay && p6.error.isEmpty() && p6.tnsAdmin == T + "/w6" && p6.walletDir.isEmpty());

    // 7) alias desconocido y carpeta vacía
    auto p7 = WalletResolver::prepare(T + "/w1", "no_existe", overlay, home);
    check("alias desconocido: sin error propio (Oracle dirá ORA-12154)", p7.error.isEmpty() && !p7.overlay && p7.tnsAdmin == T + "/w1");
    auto p8 = WalletResolver::prepare(QString(), "x", overlay, home);
    check("sin carpeta: no hace nada", p8.tnsAdmin.isEmpty() && p8.error.isEmpty());

    // 8) varios alias en el mismo archivo: solo se mira el bloque del alias pedido
    put(T + "/w9/tnsnames.ora", tlsEntry("a_high", "(my_wallet_directory= C:\\Wallet_A)") + tlsEntry("b_high", "(my_wallet_directory= C:\\Wallet_B)")); put(T + "/w9/cwallet.sso", "x");
    auto p9 = WalletResolver::prepare(T + "/w9", "b_high", overlay, home);
    check("solo se reescribe la ruta del alias usado", read(p9.tnsAdmin + "/tnsnames.ora").contains("C:\\Wallet_A") && !read(p9.tnsAdmin + "/tnsnames.ora").contains("C:\\Wallet_B"), read(p9.tnsAdmin + "/tnsnames.ora"));
    // 9) el TNS raíz (de Windows) con rutas C:\Wallet_X: se encuentran las wallets por nombre de carpeta o de .zip
    put(T + "/raiz/tnsnames.ora", tlsEntry("br_high", "(my_wallet_directory= C:\\Wallet_KMBR01)") + tlsEntry("km_high", "(my_wallet_directory= C:\\Wallet_DbKmelot)") + tlsEntry("otra_high", "(my_wallet_directory= C:\\Wallet_Otra)"));
    put(T + "/raiz/br01/Wallet_KMBR01/cwallet.sso", "x");
    put(T + "/raiz/principal/cwallet.sso", "x"); put(T + "/raiz/principal/Wallet_DbKmelot.zip", "z");
    auto pa = WalletResolver::prepare(T + "/raiz", "br_high", overlay, home);
    check("Wallet_KMBR01 se encuentra por el nombre de su carpeta", pa.walletDir == T + "/raiz/br01/Wallet_KMBR01" && pa.overlay && pa.error.isEmpty(), pa.error + " | " + pa.walletDir);
    auto pb = WalletResolver::prepare(T + "/raiz", "km_high", overlay, home);
    check("Wallet_DbKmelot se encuentra por el nombre de su .zip", pb.walletDir == T + "/raiz/principal" && pb.error.isEmpty(), pb.error + " | " + pb.walletDir);
    auto pc = WalletResolver::prepare(T + "/raiz", "otra_high", overlay, home);
    check("una wallet que no está en ninguna parte da error claro", !pc.error.isEmpty() && pc.error.contains("Wallet_Otra"), pc.error);
    check("los mensajes de error usan saltos de línea reales", !pc.error.contains("\\n") && pc.error.contains('\n'));
    // 10) inspect(): un TNS ya preparado para el Mac (my_wallet_directory con ruta del Mac) y otro con rutas de Windows
    put(T + "/mac/wallets/uno/cwallet.sso", "x"); put(T + "/mac/wallets/dos/cwallet.sso", "x");
    put(T + "/mac/tnsnames.ora", tlsEntry("uno_high", "(my_wallet_directory=" + T + "/mac/wallets/uno)") + tlsEntry("dos_high", "(my_wallet_directory= " + T + "/mac/wallets/dos)")
        + tlsEntry("windows_high", "(my_wallet_directory= C:\\Wallet_Uno)") + tlsEntry("falta_high", "(my_wallet_directory= " + T + "/mac/wallets/no_existe)")
        + "INV = (DESCRIPTION=(ADDRESS=(PROTOCOL=TCP)(HOST=10.0.0.5)(PORT=1521))(CONNECT_DATA=(SERVICE_NAME=inv)))\n");
    auto ins = WalletResolver::inspect(T + "/mac", home);
    auto by = [&](const QString& a) { for (const auto& c : ins) if (c.alias == a) return c; return AliasCheck(); };
    check("inspect: 5 alias leídos", ins.size() == 5, QString::number(ins.size()));
    check("inspect: ruta de Mac válida = correcto (con y sin espacio tras el =)", by("uno_high").ok && by("uno_high").walletPath == T + "/mac/wallets/uno" && by("dos_high").ok && by("dos_high").walletPath == T + "/mac/wallets/dos");
    check("inspect: ruta C:\\ = revisar", !by("windows_high").ok && by("windows_high").message.contains("C:\\Wallet_Uno"));
    put(T + "/mac/wallets/Wallet_Uno/cwallet.sso", "x");
    auto ins2 = WalletResolver::inspect(T + "/mac", home); QString wmsg; for (const auto& c : ins2) if (c.alias == "windows_high") wmsg = c.message;
    check("inspect: sugiere dónde está la wallet de una ruta C:\\Wallet_Uno", wmsg.contains(T + "/mac/wallets/Wallet_Uno"), wmsg);
    check("inspect: ruta de Mac inexistente = revisar", !by("falta_high").ok && by("falta_high").message.contains("no_existe"));
    check("inspect: alias TCP sin wallet = correcto", by("INV").ok && !by("INV").tls);
    // y con rutas de Mac en el TNS, la app conecta directo (sin carpeta temporal)
    auto pm = WalletResolver::prepare(T + "/mac", "uno_high", overlay, home);
    check("TNS ya preparado para Mac: se usa tal cual (sin carpeta temporal)", !pm.overlay && pm.tnsAdmin == T + "/mac" && pm.walletDir == T + "/mac/wallets/uno" && pm.error.isEmpty(), pm.error + pm.tnsAdmin);
    std::printf("\n%s\n", fails ? "HAY FALLOS" : "Todas las pruebas de wallet pasaron");
    return fails ? 1 : 0;
}
