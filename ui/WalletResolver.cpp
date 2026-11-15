#include "WalletResolver.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace {

QString readAll (const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(f.readAll()) : QString();
}

QString canonical (const QString& p) { return QFileInfo(p).canonicalFilePath(); }
bool hasWallet (const QString& dir) { return !dir.isEmpty() && QFile::exists(dir + "/cwallet.sso"); }

QString tnsnamesPath (const QString& folder)
{
    for (const char* n : { "tnsnames.ora", "TNSNAMES.ORA" }) if (QFile::exists(folder + "/" + n)) return folder + "/" + n;
    return QString();
}

// DIRECTORY de WALLET_LOCATION en sqlnet.ora (vacío si no hay)
QString sqlnetWalletDir (const QString& sqlnetText)
{
    static const QRegularExpression re(R"re(WALLET_LOCATION[^\n]*?DIRECTORY\s*=\s*"?([^")\n]+)"?)re", QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(sqlnetText);
    return m.hasMatch() ? m.captured(1).trimmed() : QString();
}

QString myWalletDir (const QString& block)
{
    static const QRegularExpression re(R"(my_wallet_directory\s*=\s*([^)\n]+))", QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(block);
    return m.hasMatch() ? m.captured(1).trimmed().remove('"') : QString();
}

// Busca una wallet por el nombre que le da el TNS ("C:\\Wallet_KMBR01" -> carpeta "Wallet_KMBR01", o la carpeta que contiene "Wallet_KMBR01.zip")
// bajo las carpetas indicadas (hasta 2 niveles).
QString findWalletByName (const QString& leaf, const QStringList& roots)
{
    if (leaf.isEmpty()) return QString();
    for (const QString& root : roots)
    {
        if (!QDir(root).exists()) continue;
        QStringList dirs; dirs << root;
        for (const QString& d1 : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        { dirs << root + "/" + d1; for (const QString& d2 : QDir(root + "/" + d1).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) dirs << root + "/" + d1 + "/" + d2; }
        for (const QString& d : dirs)
        {
            if (QFileInfo(d).fileName().compare(leaf, Qt::CaseInsensitive) == 0 && hasWallet(d)) return d;
            if (hasWallet(d) && QFile::exists(d + "/" + leaf + ".zip")) return d;
        }
    }
    return QString();
}

} // namespace

QString WalletResolver::mapWindowsPath (const QString& path, const QString& home)
{
    QString p = path.trimmed();
    static const QRegularExpression drive(R"(^[A-Za-z]:[\\/])");
    if (!drive.match(p).hasMatch()) return p;                                    // no es una ruta de Windows
    QString rest = p.mid(3).replace('\\', '/');
    static const QRegularExpression macHome(R"(^Mac/Home(/|$))", QRegularExpression::CaseInsensitiveOption);   // carpeta personal del Mac vista desde Parallels
    if (auto m = macHome.match(rest); m.hasMatch()) return QDir::cleanPath(home + "/" + rest.mid(m.capturedLength()));
    return p;                                                                    // otra unidad de Windows (C:\Wallet_X): no existe en el Mac
}

QString WalletResolver::aliasBlock (const QString& text, const QString& alias)
{
    const QRegularExpression head("(?:^|\\n)\\s*" + QRegularExpression::escape(alias) + "\\s*=\\s*", QRegularExpression::CaseInsensitiveOption);
    auto m = head.match(text);
    if (!m.hasMatch()) return QString();
    int i = m.capturedEnd(), depth = 0, start = -1;
    for (; i < text.size(); ++i)
    {
        if (text[i] == '(') { if (depth == 0) start = i; ++depth; }
        else if (text[i] == ')') { if (--depth == 0 && start >= 0) return text.mid(start, i - start + 1); }
    }
    return text.mid(start >= 0 ? start : m.capturedEnd());
}

WalletPlan WalletResolver::prepare (const QString& folder, const QString& alias, const QString& overlayRoot, const QString& home)
{
    WalletPlan plan; plan.tnsAdmin = folder;
    if (folder.isEmpty()) return plan;                                           // sin carpeta: se usa el TNS_ADMIN del entorno tal cual
    const QString tnsFile = tnsnamesPath(folder);
    const QString tnsText = readAll(tnsFile);
    const QString block = aliasBlock(tnsText, alias);
    if (block.isEmpty()) return plan;                                            // alias desconocido: que Oracle informe (ORA-12154)

    static const QRegularExpression tcps(R"(\(\s*protocol\s*=\s*tcps\s*\))", QRegularExpression::CaseInsensitiveOption);
    const bool usesTls = tcps.match(block).hasMatch();
    const QString myWallet = myWalletDir(block);                                 // lo que dice la propia entrada (estilo Windows)
    const QString myWalletMac = myWallet.isEmpty() ? QString() : mapWindowsPath(myWallet, home);
    const QString sqlnetText = readAll(folder + "/sqlnet.ora");
    const QString sqlnetDir = sqlnetWalletDir(sqlnetText);

    // wallet: 1) la de la entrada del TNS, 2) la de sqlnet.ora, 3) la propia carpeta
    QString wallet;
    if (hasWallet(myWalletMac)) wallet = myWalletMac;
    else if (!sqlnetDir.isEmpty() && sqlnetDir.startsWith('/') && hasWallet(sqlnetDir)) wallet = sqlnetDir;
    else if (hasWallet(folder)) wallet = folder;
    else if (!myWallet.isEmpty())                                                // por el nombre que usa el TNS (p.ej. C:\\Wallet_KMBR01)
    {
        const QString leaf = myWallet.section(QRegularExpression("[\\\\/]"), -1);
        wallet = findWalletByName(leaf, { folder, QFileInfo(folder).absolutePath() });
    }
    plan.walletDir = wallet;

    if (!usesTls && myWallet.isEmpty()) return plan;                             // alias sin TLS ni wallet: nada que preparar
    if (wallet.isEmpty())
    {
        if (usesTls)
            plan.error = "El alias «" + alias + "» usa TLS y no se encontró su wallet.\n"
                         + (myWallet.isEmpty() ? QString() : "El TNS indica «" + myWallet + "»" + (myWalletMac != myWallet ? " (en este Mac: «" + myWalletMac + "»)" : QString()) + ", que no existe aquí.\n")
                         + "Indique la carpeta de la wallet (la que contiene cwallet.sso) como «Carpeta TNS / wallet» de la conexión.";
        return plan;
    }

    // ¿hace falta corregir la configuración?
    // la entrada del TNS es utilizable tal cual solo si su texto ya es una ruta válida de este Mac (una ruta "C:\\..." hay que reescribirla aunque se haya traducido)
    const bool tnsOk = myWallet.isEmpty() || (myWallet == myWalletMac && canonical(myWallet) == canonical(wallet));
    const bool sqlnetOk = !sqlnetDir.isEmpty() && sqlnetDir.startsWith('/') && canonical(sqlnetDir) == canonical(wallet);
    if (tnsOk && (sqlnetOk || !myWallet.isEmpty())) return plan;                 // ya está bien configurada

    // carpeta temporal: tnsnames.ora (con la ruta de wallet corregida) + sqlnet.ora (con WALLET_LOCATION correcto); el resto, enlaces a la original
    const QString key = QString::fromLatin1(QCryptographicHash::hash((folder + "|" + alias).toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
    const QString dir = overlayRoot + "/" + key;
    QDir(dir).removeRecursively(); QDir().mkpath(dir);
    for (const QString& n : QDir(folder).entryList(QDir::Files | QDir::NoDotAndDotDot))
    {
        if (n.compare("tnsnames.ora", Qt::CaseInsensitive) == 0 || n.compare("sqlnet.ora", Qt::CaseInsensitive) == 0) continue;
        QFile::link(folder + "/" + n, dir + "/" + n);
    }
    QString newTns = tnsText;
    if (!myWallet.isEmpty()) newTns.replace(myWallet, wallet);                   // la entrada pasa a apuntar a la wallet real
    QFile t(dir + "/tnsnames.ora"); if (!t.open(QIODevice::WriteOnly)) { plan.error = "No se pudo preparar la configuración temporal: " + t.errorString(); return plan; }
    t.write(newTns.toUtf8()); t.close();
    QStringList keep; for (const QString& line : sqlnetText.split('\n')) if (!line.contains("WALLET_LOCATION", Qt::CaseInsensitive)) keep << line;
    QFile s(dir + "/sqlnet.ora"); if (!s.open(QIODevice::WriteOnly)) { plan.error = "No se pudo preparar sqlnet.ora temporal: " + s.errorString(); return plan; }
    s.write((keep.join('\n').trimmed() + "\nWALLET_LOCATION = (SOURCE = (METHOD = file) (METHOD_DATA = (DIRECTORY=\"" + wallet + "\")))\n").toUtf8()); s.close();

    plan.tnsAdmin = dir; plan.overlay = true;
    plan.note = "Wallet: " + wallet + " (configuración TNS temporal; sus archivos no se modifican)";
    return plan;
}

QStringList WalletResolver::aliasNames (const QString& folder)
{
    QStringList out;
    static const QRegularExpression re(R"(^([A-Za-z][A-Za-z0-9_.\-]*)\s*=)", QRegularExpression::MultilineOption);
    for (auto it = re.globalMatch(readAll(tnsnamesPath(folder))); it.hasNext();) out << it.next().captured(1);
    return out;
}

QList<AliasCheck> WalletResolver::inspect (const QString& folder, const QString& home)
{
    QList<AliasCheck> out;
    const QString text = readAll(tnsnamesPath(folder));
    const QString sqlnetDir = sqlnetWalletDir(readAll(folder + "/sqlnet.ora"));
    static const QRegularExpression tcps(R"(\(\s*protocol\s*=\s*tcps\s*\))", QRegularExpression::CaseInsensitiveOption);
    for (const QString& alias : aliasNames(folder))
    {
        AliasCheck c; c.alias = alias;
        const QString block = aliasBlock(text, alias);
        c.tls = tcps.match(block).hasMatch();
        const QString my = myWalletDir(block), myMac = my.isEmpty() ? QString() : mapWindowsPath(my, home);
        if (!c.tls && my.isEmpty()) { c.message = "Sin TLS: no necesita wallet."; out << c; continue; }
        if (!my.isEmpty())
        {
            if (my == myMac && hasWallet(my)) { c.walletPath = my; c.message = "Wallet del TNS: " + my; }
            else if (my != myMac && hasWallet(myMac)) { c.ok = false; c.walletPath = myMac; c.message = "El TNS usa la ruta de Windows «" + my + "». En el Mac está en «" + myMac + "»: cambie la ruta en el tnsnames.ora (la app lo corrige al conectar, pero conviene dejarlo bien)."; }
            else
            {
                c.ok = false;
                const QString guess = findWalletByName(myMac.section(QRegularExpression("[\\\\/]"), -1), { folder, QFileInfo(folder).absolutePath() });
                c.message = "El TNS indica «" + my + "», que no existe en este Mac." + (guess.isEmpty() ? QString(" No se encontró una wallet con ese nombre cerca: indique su carpeta (la que tiene cwallet.sso).")
                                                                                                         : " La wallet parece estar en «" + guess + "»: cambie la ruta del TNS por esa.");
                c.walletPath = guess;
            }
        }
        else if (!sqlnetDir.isEmpty() && sqlnetDir.startsWith('/') && hasWallet(sqlnetDir)) { c.walletPath = sqlnetDir; c.message = "Wallet de sqlnet.ora: " + sqlnetDir; }
        else if (hasWallet(folder)) { c.walletPath = folder; c.message = "Wallet en la propia carpeta TNS (sqlnet.ora no la indica con una ruta válida)."; }
        else { c.ok = false; c.message = "Usa TLS y no se encontró la wallet: añada (my_wallet_directory= /ruta/de/la/wallet) a la entrada, dentro de (security=…)."; }
        out << c;
    }
    return out;
}
