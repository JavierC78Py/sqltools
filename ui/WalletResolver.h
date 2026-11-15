#pragma once
// Resuelve qué wallet usar para un alias, como hace el SQLTools de Windows: la wallet se toma de la propia entrada de tnsnames.ora
// (my_wallet_directory) o, si no la trae, de sqlnet.ora. En Mac esas rutas suelen no servir tal cual:
//   - tnsnames.ora de Windows trae rutas "C:\..." (se traducen: "C:\Mac\Home\..." es la carpeta personal compartida con Parallels);
//   - sqlnet.ora de las wallets de Autonomous DB trae DIRECTORY="?/network/admin", que con Instant Client no apunta a ninguna parte.
// Para no tocar los archivos del usuario, cuando hace falta se prepara una carpeta TNS temporal con la configuración corregida.
#include <QList>
#include <QString>
#include <QStringList>

struct WalletPlan
{
    QString tnsAdmin;        // carpeta que se usará como TNS_ADMIN
    QString walletDir;       // wallet encontrada (vacía si el alias no usa TLS)
    bool overlay = false;    // true si se creó una carpeta temporal con la configuración corregida
    QString note;            // explicación breve de lo que se hizo
    QString error;           // si no vacío: no se puede conectar (mensaje para el usuario)
};

// Resultado de revisar un alias del tnsnames.ora
struct AliasCheck
{
    QString alias;
    bool tls = false;          // usa tcps (necesita wallet)
    QString walletPath;        // wallet que se usaría (vacío si no necesita o no se encontró)
    bool ok = true;
    QString message;           // explicación (o el problema)
};

namespace WalletResolver
{
    // Revisa todos los alias de la carpeta TNS: ¿tienen la wallet donde dice el TNS (o en sqlnet.ora / la propia carpeta)?
    QList<AliasCheck> inspect (const QString& tnsFolder, const QString& homeDir);
    QStringList aliasNames (const QString& tnsFolder);
    WalletPlan prepare (const QString& tnsFolder, const QString& alias, const QString& overlayRoot, const QString& homeDir);
    QString mapWindowsPath (const QString& path, const QString& homeDir);          // "C:\Mac\Home\x" -> "<home>/x"; otras rutas de Windows quedan igual
    QString aliasBlock (const QString& tnsnamesText, const QString& alias);        // texto de la entrada del alias (con paréntesis balanceados)
}
