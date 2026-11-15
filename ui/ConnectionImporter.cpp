#include "ConnectionImporter.h"
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QXmlStreamReader>

bool ConnectionImporter::parseSqlToolsXml (const QString& path, QList<ConnectionProfile>* out, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { *error = "No se pudo abrir el archivo: " + f.errorString(); return false; }
    QXmlStreamReader xml(&f);
    bool sawSqlTools = false, sawConnections = false;
    while (!xml.atEnd())
    {
        xml.readNext();
        if (!xml.isStartElement()) continue;
        if (xml.name() == u"SQLTools") sawSqlTools = true;
        if (xml.name() == u"Connections") sawConnections = true;
        if (xml.name() != u"Connection") continue;
        const auto a = xml.attributes();            // OJO: el atributo "Password" existe en el original pero nunca se lee
        ConnectionProfile p; p.id = ConnectionStore::newId();
        p.tag = a.value("Tag").toString(); p.user = a.value("User").toString(); p.alias = a.value("Alias").toString();
        p.host = a.value("Host").toString(); p.port = a.value("Port").toString(); if (p.port.isEmpty()) p.port = "1521";
        p.sid = a.value("Sid").toString(); p.bypass = a.value("Direct").toInt() != 0; p.useService = a.value("UseService").toInt() != 0;
        const int mode = a.value("Mode").toInt(); p.mode = mode == 1 ? "SYSDBA" : mode == 2 ? "SYSOPER" : "Normal";
        p.readOnly = a.value("Safety").toInt() != 0;                    // en SQLTools "Safety" pasó a significar solo lectura
        p.slow = a.value("Slow").toInt() != 0;
        p.count = a.value("UsageCounter").toInt();
        const qint64 t = a.value("LastUsage").toLongLong();
        if (t > 0) p.lastUsed = QDateTime::fromSecsSinceEpoch(t);
        p.savePassword = true;                                          // se pedirá la contraseña la primera vez y quedará en el Llavero
        if (!p.user.isEmpty() && (!p.alias.isEmpty() || !p.host.isEmpty())) *out << p;
    }
    if (xml.hasError()) { *error = "El archivo no es un XML válido: " + xml.errorString(); return false; }
    if (!sawSqlTools || !sawConnections) { *error = "El archivo no parece un connections.xml de SQLTools."; return false; }
    return true;
}

static bool tnsHasAlias (const QString& file, const QString& alias)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    static const QRegularExpression re(R"(^([A-Za-z0-9_.\-]+)\s*=)");
    while (!f.atEnd())
    {
        auto m = re.match(QString::fromUtf8(f.readLine()));
        if (m.hasMatch() && m.captured(1).compare(alias, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

QString ConnectionImporter::findTnsFolder (const QString& alias, const QString& root)
{
    if (alias.isEmpty() || root.isEmpty() || !QDir(root).exists()) return QString();
    QStringList dirs;                                              // subcarpetas hasta 2 niveles
    for (const QString& d1 : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
    {
        dirs << root + "/" + d1;
        for (const QString& d2 : QDir(root + "/" + d1).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) dirs << root + "/" + d1 + "/" + d2;
    }
    for (const QString& d : dirs) if (tnsHasAlias(d + "/tnsnames.ora", alias)) return d;
    return tnsHasAlias(root + "/tnsnames.ora", alias) || tnsHasAlias(root + "/TNSNAMES.ORA", alias) ? root : QString();
}

bool ConnectionImporter::sameConnection (const ConnectionProfile& a, const ConnectionProfile& b)
{
    auto dest = [](const ConnectionProfile& p) { return p.bypass ? (p.host + ":" + p.port + "/" + p.sid).toLower() : p.alias.toLower(); };
    return a.tag.compare(b.tag, Qt::CaseInsensitive) == 0 && a.user.compare(b.user, Qt::CaseInsensitive) == 0 && dest(a) == dest(b);
}
