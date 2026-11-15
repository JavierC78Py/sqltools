#include "DbWorker.h"
#include <QSettings>
#include <functional>
#include "DdlOptions.h"
#include "MetaDictionary/MetaDictionary.h"
#include "MetaDictionary/Loader.h"
#include "MetaDictionary/TextOutput.h"
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QtGlobal>
#include <cstdlib>

// Los encabezados de OCI8 traen macros estilo Windows (compat): se incluyen DESPUÉS de los de Qt.
#include "WalletResolver.h"
#include "stdafx.h"
#include "OCI8/Connect.h"
#include "OCI8/Statement.h"
#include "OCI8/ACursor.h"

DbWorker::DbWorker (QObject* parent) : QObject(parent) {}

QString DbWorker::clientVersion ()
{
    sword a = 0, b = 0, c = 0, d = 0, e = 0;
    OCIClientVersion(&a, &b, &c, &d, &e);
    return QString("%1.%2.%3.%4.%5").arg(a).arg(b).arg(c).arg(d).arg(e);
}
DbWorker::~DbWorker () = default;

void DbWorker::cancel ()
{
    auto bh = m_break;
    if (bh && m_running) bh->Break();
}

// Abre una conexión OCI con todas las opciones del gestor de conexiones.
std::unique_ptr<OCI8::Connect> DbWorker::openFrom (const QVariantMap& p, QString* error, QString* note)
{
    const QString tnsAdmin = p.value("tnsAdmin").toString();
    const bool bypass = p.value("bypass").toBool();
    if (!bypass && !tnsAdmin.isEmpty())
    {
        if (!QDir(tnsAdmin).exists()) { *error = "La carpeta TNS / wallet no existe: " + tnsAdmin; return nullptr; }
        if (!QFile::exists(tnsAdmin + "/tnsnames.ora")) { *error = "No hay tnsnames.ora en: " + tnsAdmin; return nullptr; }
    }
    QString effectiveTns = tnsAdmin;
    if (!bypass && !tnsAdmin.isEmpty())
    {
        // La wallet se toma de la propia entrada del TNS (my_wallet_directory) o de sqlnet.ora, como en el SQLTools de Windows; si esas rutas
        // no sirven en el Mac, se prepara una carpeta TNS temporal con la configuración corregida (los archivos del usuario no se tocan).
        const WalletPlan plan = WalletResolver::prepare(tnsAdmin, p.value("alias").toString(), QDir::homePath() + "/Library/Application Support/SQLToolsMac/tns", QDir::homePath());
        if (!plan.error.isEmpty()) { *error = plan.error; return nullptr; }
        effectiveTns = plan.tnsAdmin;
        if (note) *note = plan.note;
    }
    if (!effectiveTns.isEmpty()) setenv("TNS_ADMIN", effectiveTns.toUtf8().constData(), 1);
    const QString mode = p.value("mode", "Normal").toString();
    const OCI8::EConnectionMode m = mode == "SYSDBA" ? OCI8::ecmSysDba : mode == "SYSOPER" ? OCI8::ecmSysOper : OCI8::ecmDefault;
    const QByteArray user = p.value("user").toString().toUtf8(), pass = p.value("password").toString().toUtf8();
    const bool ro = p.value("readOnly").toBool(), slow = p.value("slow").toBool();
    try
    {
        std::unique_ptr<OCI8::Connect> cn(new OCI8::Connect());
        if (bypass)
            cn->Open(user.constData(), pass.constData(), p.value("host").toString().toUtf8().constData(), p.value("port", "1521").toString().toUtf8().constData(),
                     p.value("sid").toString().toUtf8().constData(), p.value("useService", true).toBool(), m, ro, slow);
        else
            cn->Open(user.constData(), pass.constData(), p.value("alias").toString().toUtf8().constData(), m, ro, slow);
        return cn;
    }
    catch (const std::exception& e) { *error = QString::fromUtf8(e.what()).trimmed(); return nullptr; }
}

void DbWorker::connectDb (const QVariantMap& params)
{
    m_break.reset(); m_cn.reset();
    QString err;
    auto cn = openFrom(params, &err);
    if (!cn) { emit failed(err); return; }
    try
    {
        m_cn = std::move(cn);
        m_break = m_cn->CreateBreakHandler();
        m_params = params;
        try { m_cn->ExecuteStatement("begin dbms_output.enable(null); end;"); m_outputOn = true; } catch (...) {}   // DBMS_OUTPUT sin límite (en solo lectura Oracle8 lo rechaza: se ignora)
        std::string u, s; m_cn->GetCurrentUserAndSchema(u, s);
        m_owner = QString::fromUtf8(s.c_str());
        const QString who = QString::fromUtf8(u.c_str()) + "@" + (params.value("bypass").toBool() ? params.value("host").toString() : params.value("alias").toString());
        emit connected(QString::fromUtf8(m_cn->GetVersionStr()), who);
    }
    catch (const std::exception& e) { m_cn.reset(); emit failed(QString::fromUtf8(e.what()).trimmed()); }
}

void DbWorker::testConnection (const QVariantMap& params)
{
    QString err, note;
    auto cn = openFrom(params, &err, &note);
    if (!cn) { emit testResult(false, err); return; }
    QString msg;
    try { msg = QString("Conexión correcta: Oracle %1").arg(QString::fromUtf8(cn->GetVersionStr())) + (note.isEmpty() ? QString() : "\n" + note); cn->Close(); }
    catch (const std::exception& e) { emit testResult(false, QString::fromUtf8(e.what()).trimmed()); return; }
    emit testResult(true, msg);
}

void DbWorker::setReadOnly (bool on) { if (m_cn) m_cn->SetReadOnly(on); }

void DbWorker::setDbmsOutput (bool on)
{
    m_outputOn = on;
    if (!m_cn) return;
    const bool ro = m_cn->IsReadOnly(); if (ro) m_cn->SetReadOnly(false);          // la sentencia de control no es del usuario
    try { m_cn->ExecuteStatement(on ? "begin dbms_output.enable(null); end;" : "begin dbms_output.disable; end;"); } catch (...) {}
    if (ro) m_cn->SetReadOnly(true);
}

void DbWorker::setHaltOnError (bool on) { m_haltOnError = on; }
void DbWorker::setDdlOptions (bool storage, bool terminator, bool specWithBody) { m_ddlStorage = storage; m_ddlTerminator = terminator; m_specWithBody = specWithBody; m_ddlConfigured = false; }
void DbWorker::setTransactionOptions (bool autocommit, int commitOnDisconnect) { m_autocommit = autocommit; m_commitOnDisconnect = commitOnDisconnect; }
void DbWorker::setFetchOptions (int prefetch, int stringLimit) { if (prefetch > 0) m_prefetch = prefetch; if (stringLimit > 0) m_stringLimit = stringLimit; }
void DbWorker::setSessionStats (bool on) { m_sessionStats = on; }

// Explain Plan: EXPLAIN PLAN escribe en la tabla temporal PLAN_TABLE (privada de la sesión, no persiste); se lee con DBMS_XPLAN.
void DbWorker::explainPlan (const QString& sqlIn)
{
    if (!m_cn) { emit failed("No hay conexión abierta."); return; }
    QString sql = sqlIn.trimmed(); while (sql.endsWith(';') || sql.endsWith('/')) { sql.chop(1); sql = sql.trimmed(); }
    if (sql.isEmpty()) { emit failed("No hay nada que explicar."); return; }
    const bool ro = m_cn->IsReadOnly(); if (ro) m_cn->SetReadOnly(false);              // EXPLAIN PLAN no es una consulta pero no modifica datos
    QElapsedTimer t; t.start();
    try
    {
        m_cn->ExecuteStatement(("explain plan for " + sql).toUtf8().constData());
        QString text;
        for (const auto& r : queryRows("select plan_table_output from table(dbms_xplan.display('PLAN_TABLE', null, 'ALL'))", 1, 4000)) text += r[0] + "\n";
        if (ro) m_cn->SetReadOnly(true);
        emit planReady(text);
        emit finished(0, t.elapsed() / 1000.0, "Plan de ejecución generado.");
    }
    catch (const std::exception& e) { if (ro) m_cn->SetReadOnly(true); emit failed(QString::fromUtf8(e.what()).trimmed()); }
}

// Instantánea de V$MYSTAT: nombre -> valor
static QMap<QString, double> statSnapshot (const QVector<QStringList>& rows)
{
    QMap<QString, double> m; for (const auto& r : rows) m[r[0]] = r[1].toDouble(); return m;
}

// Prueba la conexión con una consulta trivial; si está caída, la vuelve a abrir con los mismos parámetros.
void DbWorker::reconnect ()
{
    if (m_params.isEmpty()) { emit failed("Aún no hay ninguna conexión que reconectar."); return; }
    if (m_cn)
    {
        try { queryRows("select 1 from dual", 1); emit finished(0, 0, "La conexión está activa."); return; } catch (...) {}
    }
    emit messageReady("La conexión no responde: reconectando…");
    connectDb(m_params);
}

void DbWorker::disconnectDb ()
{
    try { if (m_cn && m_dirty && m_commitOnDisconnect == 1) m_cn->Commit(); } catch (...) {}
    try { if (m_cn) m_cn->Close(); } catch (...) {}
    if (m_dirty) { m_dirty = false; emit transactionState(false); }
    m_break.reset(); m_cn.reset();
    emit disconnected();
}

void DbWorker::commit ()   { try { if (m_cn) { m_cn->Commit(); if (m_dirty) { m_dirty = false; emit transactionState(false); }   emit finished(0, 0, "Commit realizado."); } } catch (const std::exception& e) { emit failed(QString::fromUtf8(e.what()).trimmed()); } }
void DbWorker::rollback () { try { if (m_cn) { m_cn->Rollback(); if (m_dirty) { m_dirty = false; emit transactionState(false); } emit finished(0, 0, "Rollback realizado."); } } catch (const std::exception& e) { emit failed(QString::fromUtf8(e.what()).trimmed()); } }

static bool isPlsqlLike (const QString& sql)
{
    static const QRegularExpression re(R"(^\s*(begin|declare|create\s+(or\s+replace\s+)?(editionable\s+|noneditionable\s+)?(procedure|function|package|trigger|type)))",
                                       QRegularExpression::CaseInsensitiveOption);
    return re.match(sql).hasMatch();
}
static bool isQuery (const QString& sql)
{
    static const QRegularExpression re(R"(^\s*(\(\s*)*(select|with)\b)", QRegularExpression::CaseInsensitiveOption);
    return re.match(sql).hasMatch();
}

// Recoge lo que dejaron dbms_output.put_line/put. Cada viaje a la base trae hasta ~8 KB de texto (no una línea por viaje).
// Las líneas de más de 4000 caracteres se recortan.
void DbWorker::collectOutput ()
{
    if (!m_cn || !m_outputOn) return;
    try
    {
        static const char* plsql =
            "declare l varchar2(32767); st integer := 0; s varchar2(32767) := ''; "
            "begin "
            "  loop "
            "    exit when lengthb(s) > 8000; "
            "    dbms_output.get_line(l, st); "
            "    exit when st <> 0; "
            "    s := s || substr(l, 1, 4000) || chr(10); "
            "  end loop; "
            "  :o := s; :more := case when st = 0 then 1 else 0 end; "
            "end;";
        QString all;
        for (int guard = 0; guard < 2000; ++guard)
        {
            OCI8::Statement st(*m_cn, plsql);
            OCI8::StringVar out(32767);
            OCI8::NumberVar more(*m_cn);
            st.Bind(":o", out); st.Bind(":more", more);
            st.Execute(1);
            std::string chunk; out.GetString(chunk, "");
            all += QString::fromUtf8(chunk.c_str(), (qsizetype)chunk.size());
            if (more.ToInt(0) == 0) break;
        }
        if (!all.isEmpty()) emit outputReady(all);
    }
    catch (...) {}                                   // la salida es informativa: un fallo aquí no debe tapar el resultado
}

// Tras un CREATE [OR REPLACE] PROCEDURE/FUNCTION/PACKAGE/TRIGGER/TYPE, Oracle "tiene éxito con errores de compilación"
// sin lanzar excepción: los errores hay que pedirlos a ALL_ERRORS.
void DbWorker::fetchCompileErrors (const QString& sql)
{
    static const QRegularExpression re(
        R"re(^\s*create\s+(?:or\s+replace\s+)?(?:(?:non)?editionable\s+)?(procedure|function|package\s+body|package|trigger|type\s+body|type)\s+(?:"?([\w$#]+)"?\s*\.\s*)?"?([\w$#]+)"?)re",
        QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(sql);
    if (!m.hasMatch() || !m_cn) return;
    auto q = [](QString v) { return v.replace('\'', "''"); };
    QString type = m.captured(1).toUpper().simplified();
    const QString owner = m.captured(2), name = m.captured(3);
    const bool quoted = sql.contains('"' + name + '"');
    const QString objName = quoted ? name : name.toUpper();
    try
    {
        QString query = QString("select attribute, line, position, text from all_errors where owner = %1 and name = '%2' and type = '%3' order by sequence")
            .arg(owner.isEmpty() ? QString("sys_context('USERENV','CURRENT_SCHEMA')") : "'" + q(owner.toUpper()) + "'")
            .arg(q(objName)).arg(type);
        QByteArray u = query.toUtf8();
        OCI8::AutoCursor cur(*m_cn, u.constData(), 200);
        cur.Execute();
        QVector<QStringList> rows; std::string cell;
        while (cur.Fetch())
        {
            QStringList r;
            for (int i = 0; i < 4; ++i) { cur.GetString(i, cell); r << QString::fromUtf8(cell.c_str(), (qsizetype)cell.size()).trimmed(); }
            // la línea de ALL_ERRORS es relativa al texto del CREATE: se convierte a línea del editor
            r[1] = QString::number(r[1].toInt() + m_stmtStartLine - 1);
            rows << r;
        }
        emit compileErrors(rows, (owner.isEmpty() ? QString() : owner + ".") + objName);
    }
    catch (...) {}                                   // informativo: nunca debe tapar el resultado del CREATE
}

bool DbWorker::runOne (const QString& sqlIn, bool live, int resultIndex, qint64& rows, QString& message)
{
    QString sql = sqlIn.trimmed();
    if (!isPlsqlLike(sql))                           // quitar ';' o '/' finales (Oracle no los admite en SQL suelto)
        while (sql.endsWith(';') || sql.endsWith('/')) { sql.chop(1); sql = sql.trimmed(); }
    if (sql.isEmpty()) return true;

    QByteArray utf8 = sql.toUtf8();
    rows = 0; message.clear();
    if (isQuery(sql))
    {
        // Filas por viaje de red: es la palanca principal de velocidad contra una base remota. Con 200 000 filas hasta Chile,
        // prefetch 500 tardó 40 s y prefetch 20 000 tardó 4,5 s (cada viaje cuesta ~0,1 s de latencia).
        const int prefetch = qEnvironmentVariableIntValue("SQLT_PREFETCH") > 0 ? qEnvironmentVariableIntValue("SQLT_PREFETCH") : m_prefetch;
        // Una consulta intermedia solo se muestra si no se han emitido ya demasiadas pestañas (p.ej. un script con cientos de SELECT)
        static const int kMaxExtraTabs = 12, kExtraRowCap = 10000;
        const bool show = live || m_extraResults < kMaxExtraTabs;
        OCI8::AutoCursor cur(*m_cn, utf8.constData(), show ? (live ? prefetch : qMin(prefetch, 2000)) : 10, m_stringLimit);
        cur.Execute();
        if (!show) return true;                      // consulta intermedia de más: se ejecuta pero no se muestra
        if (!live) ++m_extraResults;
        const int n = cur.GetFieldCount();
        QStringList names; QVector<int> right;
        for (int i = 0; i < n; ++i)
        {
            names << QString::fromUtf8(cur.GetFieldName(i).c_str());
            auto k = cur.GetFieldKind(i);
            using K = Common::SqlValueKind;      // 0 texto, 1 número (alineado a la derecha), 2 fecha, 3 timestamp, 4 intervalo, 5 raw
            right << ((k == K::Number || k == K::BinaryFloat || k == K::BinaryDouble) ? 1 : k == K::Date ? 2 : (k == K::Timestamp || k == K::TimestampTz || k == K::TimestampLtz) ? 3 : (k == K::IntervalYm || k == K::IntervalDs) ? 4 : k == K::Raw ? 5 : 0);
        }
        if (live) emit columnsReady(names, right); else emit resultColumns(resultIndex, names, right);

        QVector<QStringList> batch; bool truncated = false;
        QElapsedTimer flush; flush.start();
        std::string cell;
        auto send = [&]{ if (live) emit rowsReady(batch); else emit resultRows(resultIndex, batch); batch.clear(); flush.restart(); };
        while (cur.Fetch())
        {
            QStringList row; row.reserve(n);
            for (int i = 0; i < n; ++i)
            {
                if (cur.IsNull(i)) row << QString();
                else { cur.GetString(i, cell); row << QString::fromUtf8(cell.c_str(), (qsizetype)cell.size()); }
            }
            batch.push_back(row); ++rows;
            if (batch.size() >= 2000 || flush.elapsed() > 80) send();
            if (!live && rows >= kExtraRowCap) { truncated = true; break; }        // una consulta intermedia no llena la memoria
        }
        if (!batch.isEmpty()) send();
        if (!live) emit resultDone(resultIndex, rows, truncated);
    }
    else
    {
        OCI8::Statement st(*m_cn, utf8.constData());
        st.Execute(1);
        rows = st.GetRowCount();
        const bool dml = !isPlsqlLike(sql) && QRegularExpression(R"(^\s*(insert|update|delete|merge)\b)", QRegularExpression::CaseInsensitiveOption).match(sql).hasMatch();
        message = dml ? QString("%1 fila(s) afectada(s). Sin commit: use Commit o Rollback.").arg(rows) : QString("Sentencia ejecutada.");
        if (isPlsqlLike(sql) && sql.contains(QRegularExpression("^\\s*create", QRegularExpression::CaseInsensitiveOption))) fetchCompileErrors(sql);
        // transacción: DML y bloques PL/SQL anónimos dejan cambios pendientes; el DDL confirma solo
        const bool ddl = QRegularExpression(R"(^\s*(create|alter|drop|truncate|grant|revoke|rename|comment)\b)", QRegularExpression::CaseInsensitiveOption).match(sql).hasMatch();
        if (ddl) { if (m_dirty) { m_dirty = false; emit transactionState(false); } }
        else if (dml || isPlsqlLike(sql) || QRegularExpression(R"(^\s*(call|exec|lock)\b)", QRegularExpression::CaseInsensitiveOption).match(sql).hasMatch())
        {
            if (m_autocommit) { try { m_cn->Commit(); message += " (commit automático)"; if (m_dirty) { m_dirty = false; emit transactionState(false); } } catch (...) {} }
            else if (!m_dirty) { m_dirty = true; emit transactionState(true); }
        }
    }
    return true;
}

// Ejecuta una consulta y devuelve todas sus filas como texto (para metadatos; no para resultados del usuario).
QVector<QStringList> DbWorker::queryRows (const QString& sql, int columns, int stringLimit)
{
    QByteArray u = sql.toUtf8();
    // El cursor reserva (filas por viaje × columnas × largo máximo) bytes: con límites grandes se reducen las filas por viaje para no reservar decenas de MB por consulta
    const int prefetch = qBound(100, (8 * 1024 * 1024) / qMax(1, columns * stringLimit), 2000);
    OCI8::AutoCursor cur(*m_cn, u.constData(), prefetch, stringLimit);
    cur.Execute();
    QVector<QStringList> rows; std::string cell;
    while (cur.Fetch())
    {
        QStringList r;
        for (int i = 0; i < columns; ++i)
        {
            if (cur.IsNull(i)) r << QString();
            else { cur.GetString(i, cell); r << QString::fromUtf8(cell.c_str(), (qsizetype)cell.size()); }
        }
        rows << r;
    }
    return rows;
}

static QString sqlQuote (QString v) { return "'" + v.replace('\'', "''") + "'"; }

void DbWorker::loadSchemas ()
{
    if (!m_cn) return;
    try
    {
        QStringList names;
        for (const auto& r : queryRows("select username from all_users where oracle_maintained = 'N' order by username", 1)) names << r[0];
        emit schemasLoaded(names, m_owner);
    }
    catch (const std::exception& e) { emit metadataFailed("esquemas", QString::fromUtf8(e.what()).trimmed()); }
}

// Misma consulta que SessionCache de SQLTools para Windows: objetos del esquema actual (en MAYÚSCULAS) y, si el usuario no es el dueño del esquema, sus sinónimos
void DbWorker::loadUserObjects ()
{
    if (!m_cn) return;
    try
    {
        const QString o = sqlQuote(m_owner); QStringList names;
        for (const auto& r : queryRows("select distinct object_name from all_objects where owner = " + o +
            " and object_type in ('TABLE','VIEW','PACKAGE','PROCEDURE','FUNCTION','INDEX','SEQUENCE','SYNONYM','TRIGGER','TYPE') and object_name = upper(object_name) and generated = 'N'", 1, 400)) names << r[0].toUpper();
        emit userObjectsLoaded(names);
    }
    catch (const std::exception&) { /* sin colores: no es un error para el usuario */ }
}

void DbWorker::loadObjectsOf (const QString& ownerIn)
{
    if (!m_cn) return;
    const QString owner = ownerIn.isEmpty() ? m_owner : ownerIn;
    try
    {
        // Objetos visibles para el usuario en ese esquema, sin los generados por el sistema (papelera BIN$, índices/LOB automáticos…).
        emit objectsLoaded(owner, queryRows(
            "select object_type, object_name, status from all_objects where owner = " + sqlQuote(owner) + " "
            "and object_type in ('TABLE','VIEW','MATERIALIZED VIEW','SEQUENCE','SYNONYM','PROCEDURE','FUNCTION','PACKAGE','TRIGGER','TYPE') "
            "and object_name not like 'BIN$%' and generated = 'N' and secondary = 'N' "
            "order by object_type, object_name", 3));
    }
    catch (const std::exception& e) { emit metadataFailed("objetos de " + owner, QString::fromUtf8(e.what()).trimmed()); }
}

void DbWorker::loadSynonyms ()
{
    if (!m_cn) return;
    try
    {
        emit synonymsLoaded(queryRows(
            "select synonym_name, owner, table_owner, table_name from all_synonyms "
            "where table_owner is not null and (owner = " + sqlQuote(m_owner) + " or (owner = 'PUBLIC' and table_owner in (select username from all_users where oracle_maintained = 'N'))) "
            "order by synonym_name", 4));
    }
    catch (const std::exception& e) { emit metadataFailed("sinónimos", QString::fromUtf8(e.what()).trimmed()); }
}

void DbWorker::loadColumns (const QString& ownerIn, const QString& table)
{
    if (!m_cn) return;
    const QString owner = ownerIn.isEmpty() ? m_owner : ownerIn;
    auto colsOf = [&](const QString& o, const QString& t) {
        return queryRows(
            "select column_name, data_type || case when data_type in ('VARCHAR2','NVARCHAR2','CHAR','NCHAR','RAW') then '(' || char_length || ')' "
            "when data_type = 'NUMBER' and data_precision is not null then '(' || data_precision || nvl2(data_scale, ',' || data_scale, '') || ')' end, "
            "decode(nullable, 'N', 'NOT NULL', '') "
            "from all_tab_columns where owner = " + sqlQuote(o) + " and table_name = " + sqlQuote(t) + " order by column_id", 3); };
    try
    {
        auto rows = colsOf(owner, table);
        for (int hops = 0; rows.isEmpty() && hops < 3; ++hops)        // ¿es un sinónimo? se sigue hasta su destino (el propio o el público)
        {
            auto syn = queryRows("select table_owner, table_name from all_synonyms where synonym_name = " + sqlQuote(table) + " and owner in (" + sqlQuote(owner) + ", 'PUBLIC') "
                                 "and table_owner is not null order by decode(owner, 'PUBLIC', 2, 1)", 2);
            if (syn.isEmpty()) break;
            rows = colsOf(syn[0][0], syn[0][1]);
        }
        emit columnsLoaded(owner, table, rows);
    }
    catch (const std::exception& e) { emit metadataFailed("columnas de " + table, QString::fromUtf8(e.what()).trimmed()); }
}

void DbWorker::loadMembers (const QString& ownerIn, const QString& package)
{
    if (!m_cn) return;
    try
    {
        const QString owner = ownerIn.isEmpty() ? m_owner : ownerIn;
        auto rows = queryRows("select distinct procedure_name from all_procedures where owner = " + sqlQuote(owner) + " and object_name = " + sqlQuote(package) + " and procedure_name is not null order by 1", 1);
        emit columnsLoaded(owner, package, rows);          // misma señal que las columnas: el llamador las guarda en el mismo mapa
    }
    catch (const std::exception& e) { emit metadataFailed("miembros de " + package, QString::fromUtf8(e.what()).trimmed()); }
}

QString DbWorker::ddlFor (const QString& owner, const QString& type, const QString& name, bool viaMetadata)
{
    const bool plsql = (type == "PROCEDURE" || type == "FUNCTION" || type == "PACKAGE" || type == "TRIGGER" || type == "TYPE");
    QString out;
    if (plsql && !viaMetadata)
    {
        // Especificación y, si existe, cuerpo (paquetes y tipos), tal como están en ALL_SOURCE
        QStringList kinds = { type };
        if (m_specWithBody) { if (type == "PACKAGE") kinds << "PACKAGE BODY"; else if (type == "TYPE") kinds << "TYPE BODY"; }
        // una sola consulta para especificación y cuerpo; ALL_SOURCE.TEXT mide como máximo 4000 bytes (buffers pequeños = menos memoria y menos tiempo)
        QStringList in; for (const QString& k : kinds) in << sqlQuote(k);
        const auto rows = queryRows("select type, text from all_source where owner = " + sqlQuote(owner) + " and name = " + sqlQuote(name) + " and type in (" + in.join(',') + ") order by type, line", 2, 4000);
        for (const QString& k : kinds)
        {
            QString body; bool any = false; for (const auto& r : rows) if (r[0] == k) { body += r[1]; any = true; }
            if (!any) continue;
            while (body.endsWith('\n') || body.endsWith(' ')) body.chop(1);
            out += "create or replace " + body + "\n/\n\n";
        }
        return out;
    }
    if (!m_ddlConfigured)       // DDL legible: sin cláusulas físicas de almacenamiento y con terminador
    {
        for (const char* p : { "SEGMENT_ATTRIBUTES", "STORAGE", "TABLESPACE" })
            try { m_cn->ExecuteStatement((QString("begin dbms_metadata.set_transform_param(dbms_metadata.session_transform, '%1', %2); end;").arg(p, m_ddlStorage ? "true" : "false")).toUtf8().constData()); } catch (...) {}
        try { m_cn->ExecuteStatement(QString("begin dbms_metadata.set_transform_param(dbms_metadata.session_transform, 'SQLTERMINATOR', %1); end;").arg(m_ddlTerminator ? "true" : "false").toUtf8().constData()); } catch (...) {}
        m_ddlConfigured = true;
    }
    QString t = QString(type).replace(' ', '_');
    if (plsql) { /* PACKAGE devuelve especificación; el cuerpo es un tipo aparte en DBMS_METADATA */ }
    auto rows = queryRows("select dbms_metadata.get_ddl(" + sqlQuote(t) + ", " + sqlQuote(name) + ", " + sqlQuote(owner) + ") from dual", 1, 4000000);
    if (!rows.isEmpty()) out = rows[0][0].trimmed() + "\n";
    if (plsql && (type == "PACKAGE" || type == "TYPE"))
        try { auto r2 = queryRows("select dbms_metadata.get_ddl(" + sqlQuote(t + "_BODY") + ", " + sqlQuote(name) + ", " + sqlQuote(owner) + ") from dual", 1, 4000000); if (!r2.isEmpty()) out += "\n" + r2[0][0].trimmed() + "\n"; } catch (...) {}
    return out;
}

void DbWorker::loadSource (const QString& ownerIn, const QString& type, const QString& name, bool viaMetadata)
{
    if (!m_cn) return;
    const QString owner = ownerIn.isEmpty() ? m_owner : ownerIn;
    try
    {
        QElapsedTimer tm; tm.start();
        const QString out = ddlFor(owner, type, name, viaMetadata);
        if (out.isEmpty()) { emit metadataFailed("código de " + name, "No se encontró el código fuente (¿sin permisos?)."); return; }
        emit sourceTiming(int(tm.elapsed())); emit sourceReady(type, name, out);
    }
    catch (const std::exception& e) { emit failed("No se pudo leer " + name + ":\n" + QString::fromUtf8(e.what()).trimmed()); }
}

// DDL de un objeto con la biblioteca de SQLTools (la misma de la versión de Windows): tablas con sus índices, restricciones, triggers,
// comentarios y permisos; vistas, secuencias, sinónimos, etc. Reproduce SqlDocCreater::Put del original.
QString DbWorker::metaDdl (const QString& owner, const QString& type, const QString& name, const QVariantMap& options)
{
    using namespace OraMetaDict;
    const DdlOptions o = DdlOptions::fromMap(options);
    WriteSettings ws;
    ws.m_Comments = o.comments; ws.m_Grants = o.grants; ws.m_LowerNames = o.lowerNames; ws.m_ShemaName = o.schemaPrefix; ws.m_SQLPlusCompatibility = o.sqlPlus; ws.m_GeneratePrompts = o.prompts;
    ws.m_CommentsAfterColumn = o.commentsAfterColumn; ws.m_CommentsPos = o.commentsPos; ws.m_Constraints = o.constraints; ws.m_Indexes = o.indexes; ws.m_NoStorageForConstraint = o.noStorageForConstraint;
    ws.m_StorageClause = o.storage == 0 ? 0 : 1; ws.m_AlwaysPutTablespace = o.storage == 2; ws.m_TableDefinition = o.tableDefinition; ws.m_Triggers = o.triggers;
    ws.m_SequnceWithStart = o.sequenceStart; ws.m_ViewWithTriggers = o.viewTriggers; ws.m_ViewWithForce = o.viewForce; ws.m_EndOfShortStatement = o.slashAlways ? "/" : ";";
    const std::string uo = owner.toUtf8().toStdString(), un = name.toUtf8().toStdString(), ut = type.toUtf8().toStdString();
    Dictionary dict; Loader loader(*m_cn, dict); loader.Init();
    loader.LoadObjects(uo.c_str(), un.c_str(), ut.c_str(), ws, o.specWithBody, o.bodyWithSpec, false);
    const DbObject& obj = dict.LookupObject(uo.c_str(), un.c_str(), ut.c_str());
    TextOutputInMEM out(true, 32 * 1024); out.SetLowerDBName(ws.m_LowerNames);
    obj.Write(out, ws); obj.WriteGrants(out, ws);
    return QString::fromUtf8(out.GetData(), out.GetLength());
}

// Algunos textos de la base traen los bytes 0x80–0x9F de Windows-1252 (comillas «curvas», guiones largos…) guardados como caracteres de control;
// aquí se muestran como lo que son. (SQLTools para Windows los convertía en «?».)
static QString fixWindows1252Controls (QString s)
{
    static const char16_t map[32] = {0x20AC,0x0081,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,0x02C6,0x2030,0x0160,0x2039,0x0152,0x008D,0x017D,0x008F,0x0090,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,0x02DC,0x2122,0x0161,0x203A,0x0153,0x009D,0x017E,0x0178};
    for (QChar& c : s) { const ushort u = c.unicode(); if (u >= 0x80 && u <= 0x9F) c = QChar(map[u - 0x80]); }
    return s;
}

void DbWorker::loadObjectDdl (const QString& ownerIn, const QStringList& objects, const QVariantMap& options, bool oneFile)
{
    if (!m_cn) return;
    const QString owner = ownerIn.isEmpty() ? m_owner : ownerIn;
    QString all; int n = 0;
    for (const QString& obj : objects)
    {
        const QString type = obj.section(QChar(0x1f), 0, 0), name = obj.section(QChar(0x1f), 1);
        QString text; QElapsedTimer tm; tm.start();
        try
        {
            const bool plsql = (type == "PROCEDURE" || type == "FUNCTION" || type == "PACKAGE" || type == "TRIGGER" || type == "TYPE");
            if (plsql) text = ddlFor(owner, type, name, false);                                  // el texto del código (ALL_SOURCE) es el mismo que genera el original
            else text = metaDdl(owner, type, name, options);
        }
        catch (const std::exception& e)
        {
            try { text = ddlFor(owner, type, name, true); } catch (...) {}                       // respaldo: DBMS_METADATA
            if (text.isEmpty()) { emit metadataFailed("DDL de " + name, QString::fromUtf8(e.what()).trimmed()); continue; }
        }
        if (text.isEmpty()) { emit metadataFailed("DDL de " + name, "No se encontró el objeto (¿sin permisos?)."); continue; }
        ++n; text = fixWindows1252Controls(text);
        if (oneFile) { if (!all.isEmpty()) all += "\n"; all += text; }
        else { emit sourceTiming(int(tm.elapsed())); emit sourceReady(type, name, text); }
    }
    if (oneFile && n > 0) emit sourceReady("DDL", QString("DDL (%1 objetos)").arg(n), all);
}

// ---- Object Viewer: árbol de propiedades del objeto (columnas, restricciones, índices, triggers, referencias, dependientes y permisos) ----
static QVariantMap vnode (const QString& kind, const QString& text, const QString& c1 = QString(), const QString& c2 = QString(), const QVariantList& kids = QVariantList())
{
    return { {"kind", kind}, {"text", text}, {"c1", c1}, {"c2", c2}, {"children", kids} };
}

// Un nodo que es un objeto de la base (se puede abrir para ver sus propios componentes y tiene menú contextual)
static QString normType (QString t)
{
    t = t.trimmed().toUpper(); if (t == "PACKAGE BODY") return "PACKAGE"; if (t == "TYPE BODY") return "TYPE";
    static const QStringList ok = {"TABLE", "VIEW", "MATERIALIZED VIEW", "SYNONYM", "PACKAGE", "PROCEDURE", "FUNCTION", "TRIGGER", "TYPE", "SEQUENCE", "INDEX", "CONSTRAINT"};
    return ok.contains(t) ? t : QString();
}
static QVariantMap withId (QVariantMap m, const QString& owner, const QString& name, const QString& type)
{
    const QString t = normType(type); if (t.isEmpty()) return m; m["oowner"] = owner; m["oname"] = name; m["otype"] = t;
    if (!m.contains("icon")) m["icon"] = t == "MATERIALIZED VIEW" ? QString("view") : t.toLower();
    return m;
}

static QString columnSpec (const QStringList& r)        // {tipo, longitud, precisión, escala, anulable, char_used, longitud en caracteres, valor por defecto}
{
    const QString t = r[0].toLower(); QString spec = t;
    if (t == "number") { if (!r[2].isEmpty()) spec += r[3].isEmpty() ? "(" + r[2] + ")" : "(" + r[2] + "," + r[3] + ")"; else if (!r[3].isEmpty()) spec += "(*," + r[3] + ")"; }
    else if (t == "varchar2" || t == "char" || t == "nvarchar2" || t == "nchar") spec += "(" + (r[5] == "C" ? r[6] + " char" : r[1]) + ")";
    else if (t == "raw") spec += "(" + r[1] + ")";
    QString def = r[7].trimmed(); if (!def.isEmpty()) spec += " default " + def;
    if (r[4] == "N") spec += " not null";
    return spec;
}

// Una pestaña de la lista de objetos: se ejecuta la consulta y se devuelven las filas tal cual (texto)
void DbWorker::loadObjectList (const QString& owner, const QString& key, const QString& sql, int columns)
{
    if (!m_cn) return;
    try { emit objectListLoaded(owner, key, queryRows(sql, columns, 400), QString()); }
    catch (const std::exception& e) { emit objectListLoaded(owner, key, {}, QString::fromUtf8(e.what()).trimmed().section('\n', 0, 0)); }
}

// Contenido de una carpeta del Object Viewer (Constraints, Indexes, Triggers, References, Referenced By, Dependent Objects, Granted Privileges)
static QVariantList folderChildren (DbWorker* w, const QString& O, const QString& N, const QString& owner, const QString& key, std::function<QVector<QStringList>(const QString&, int, int)> q)
{
    QVariantList out;
    if (key == "constraints")
    {
        for (const auto& r : q("select c.constraint_name, c.constraint_type, c.status, c.search_condition, nvl(c.r_owner,' '), nvl(c.r_constraint_name,' '), (select listagg(cc.column_name, ', ') within group (order by cc.position) from all_cons_columns cc where cc.owner = c.owner and cc.constraint_name = c.constraint_name) from all_constraints c where c.owner = " + O + " and c.table_name = " + N + " and c.constraint_type in ('P','U','R','C') order by decode(c.constraint_type,'P',1,'U',2,'R',3,4), c.constraint_name", 7, 2000))
        {
            if (r[1] == "C" && QRegularExpression(R"(^\s*"?\w+"?\s+IS\s+NOT\s+NULL\s*$)", QRegularExpression::CaseInsensitiveOption).match(r[3]).hasMatch()) continue;     // los NOT NULL ya se ven en cada columna
            const QString ty = r[1] == "P" ? "primary key" : r[1] == "U" ? "unique" : r[1] == "R" ? "foreign key" : "check";
            QVariantMap cm = vnode("item", r[0].toLower(), ty + (r[1] == "C" ? " [" + r[3].simplified() + "]" : " (" + r[6].toLower() + ")"), r[2] == "ENABLED" ? QString() : "/*" + r[2].toLower() + " */");
            cm["icon"] = r[1] == "P" ? "pk" : r[1] == "U" ? "uk" : r[1] == "R" ? "fk" : "chk";
            out << (r[1] == "C" ? QVariant(cm) : QVariant(withId(cm, owner, r[0], "CONSTRAINT")));
        }
    }
    else if (key == "indexes")
    {
        for (const auto& r : q("select i.index_name, i.uniqueness, i.index_type, (select listagg(c.column_name, ', ') within group (order by c.column_position) from all_ind_columns c where c.index_owner = i.owner and c.index_name = i.index_name) from all_indexes i where i.table_owner = " + O + " and i.table_name = " + N + " order by i.index_name", 4, 2000))
            out << withId(vnode("item", r[0].toLower(), QString(r[1] == "UNIQUE" ? "[unique] " : "") + "(" + r[3].toLower() + ")", r[2] == "NORMAL" ? QString() : "[" + r[2].toLower() + "]"), owner, r[0], "INDEX");
    }
    else if (key == "triggers")
    {
        for (const auto& r : q("select trigger_name, trigger_type, triggering_event, status from all_triggers where table_owner = " + O + " and table_name = " + N + " order by trigger_name", 4, 2000))
            out << withId(vnode("item", r[0].toLower(), (r[1].toLower() + " " + r[2].toLower()).simplified(), r[3] == "ENABLED" ? QString() : "/*" + r[3].toLower() + " */"), owner, r[0], "TRIGGER");
    }
    else if (key == "references")
    {
        for (const auto& r : q("select c.constraint_name, (select listagg(cc.column_name, ', ') within group (order by cc.position) from all_cons_columns cc where cc.owner = c.owner and cc.constraint_name = c.constraint_name), p.table_name, (select listagg(pc.column_name, ', ') within group (order by pc.position) from all_cons_columns pc where pc.owner = p.owner and pc.constraint_name = p.constraint_name), p.owner from all_constraints c join all_constraints p on p.owner = c.r_owner and p.constraint_name = c.r_constraint_name where c.owner = " + O + " and c.table_name = " + N + " and c.constraint_type = 'R' order by c.constraint_name", 5, 2000))
            out << withId(vnode("item", r[2].toLower(), r[0].toLower(), "(" + r[1].toLower() + ") → (" + r[3].toLower() + ")"), r[4], r[2], "TABLE");
    }
    else if (key == "referencedby")
    {
        for (const auto& r : q("select c.table_name, c.constraint_name, (select listagg(cc.column_name, ', ') within group (order by cc.position) from all_cons_columns cc where cc.owner = c.owner and cc.constraint_name = c.constraint_name), c.owner from all_constraints c where c.constraint_type = 'R' and (c.r_owner, c.r_constraint_name) in (select owner, constraint_name from all_constraints where owner = " + O + " and table_name = " + N + " and constraint_type in ('P','U')) order by c.table_name, c.constraint_name", 4, 2000))
            out << withId(vnode("item", r[0].toLower(), r[1].toLower(), "(" + r[2].toLower() + ")"), r[3], r[0], "TABLE");
    }
    else if (key == "dependents")
    {
        for (const auto& r : q("select owner, name, type from all_dependencies where referenced_owner = " + O + " and referenced_name = " + N + " order by type, owner, name", 3, 800))
            out << withId(vnode("item", (r[0] == owner ? QString() : r[0].toLower() + ".") + r[1].toLower(), r[2].toLower()), r[0], r[1], r[2]);
    }
    else if (key == "dependon")
    {
        for (const auto& r : q("select referenced_owner, referenced_name, referenced_type from all_dependencies where owner = " + O + " and name = " + N + " and referenced_type <> 'NON-EXISTENT' order by referenced_type, referenced_owner, referenced_name", 3, 800))
            out << withId(vnode("item", (r[0] == owner ? QString() : r[0].toLower() + ".") + r[1].toLower(), r[2].toLower()), r[0], r[1], r[2]);
    }
    else if (key == "grants")
    {
        QMap<QString, QStringList> byGrantee;
        for (const auto& r : q("select grantee, privilege from all_tab_privs where table_schema = " + O + " and table_name = " + N + " order by grantee, privilege", 2, 4000)) byGrantee[r[0]] << r[1];
        for (auto it = byGrantee.begin(); it != byGrantee.end(); ++it) out << vnode("item", it.key().toLower(), it.value().join(",").toLower());
    }
    Q_UNUSED(w);
    return out;
}

void DbWorker::describeFolder (const QString& ownerIn, const QString& name, const QString& type, const QString& key)
{
    if (!m_cn) return; Q_UNUSED(type);
    const QString owner = ownerIn.isEmpty() ? m_owner : ownerIn;
    QString err;
    auto q = [&](const QString& sql, int cols, int limit) { try { return queryRows(sql, cols, limit); } catch (const std::exception& e) { err = QString::fromUtf8(e.what()).trimmed().section('\n', 0, 0); return QVector<QStringList>(); } };
    const QVariantList kids = folderChildren(this, sqlQuote(owner), sqlQuote(name), owner, key, q);
    emit folderDescribed(owner, name, key, kids, err);
}

void DbWorker::describeObject (const QString& ownerIn, const QString& name, const QString& type, const QString& token)
{
    if (!m_cn) return;
    const QString owner = ownerIn.isEmpty() ? m_owner : ownerIn; const QString O = sqlQuote(owner), N = sqlQuote(name); QElapsedTimer tmDesc; tmDesc.start();
    try
    {
        QVariantList kids; QString err;
        auto safe = [&](const QString& sql, int cols, int limit = 4000) { try { return queryRows(sql, cols, limit); } catch (const std::exception& e) { err = QString::fromUtf8(e.what()).trimmed().section('\n', 0, 0); return QVector<QStringList>(); } };
        if (type == "TABLE" || type == "VIEW" || type == "MATERIALIZED VIEW")
        {
            // columnas (rápido); los valores por defecto y los comentarios van aparte para que un fallo ahí no deje la lista vacía
            auto cols = safe("select column_name, data_type, nvl(to_char(data_length),' '), nvl(to_char(data_precision),' '), nvl(to_char(data_scale),' '), nullable, nvl(char_used,' '), nvl(to_char(char_length),' ') from all_tab_columns where owner = " + O + " and table_name = " + N + " order by " + QString(QSettings().value("viewer/sortByName", false).toBool() ? "column_name" : "column_id"), 8, 4000);
            QHash<QString, QString> comments, defaults;
            for (const auto& r : safe("select column_name, comments from all_col_comments where owner = " + O + " and table_name = " + N, 2, 4000)) comments[r[0]] = r[1];
            for (const auto& r : safe("select column_name, data_default from all_tab_columns where owner = " + O + " and table_name = " + N + " and data_default is not null", 2, 4000)) defaults[r[0]] = r[1].trimmed();
            for (const auto& r : cols)
            {
                QStringList q = {r[1], r[2].trimmed(), r[3].trimmed(), r[4].trimmed(), r[5], r[6].trimmed(), r[7].trimmed(), defaults.value(r[0])};
                const QString cm = comments.value(r[0]).trimmed(); { QVariantMap cn = vnode("column", r[0], columnSpec(q), cm.isEmpty() ? QString() : "/*" + cm + " */"); cn["copy"] = "column"; kids << cn; }
            }
            if (cols.isEmpty() && !err.isEmpty()) kids << vnode("error", "No se pudieron leer las columnas: " + err);
            auto folder = [&](const char* key, const char* title) { QVariantMap m = vnode("folder", title); m["key"] = key; m["lazy"] = true; return QVariant(m); };
            if (type == "TABLE") kids << folder("constraints", "Constraints") << folder("indexes", "Indexes");
            kids << folder("triggers", "Triggers");
            if (type == "TABLE") kids << folder("references", "References") << folder("referencedby", "Referenced By");
            kids << folder("dependents", "Dependent Objects") << folder("grants", "Granted Privileges");
        }
        else if (type == "PACKAGE" || type == "PROCEDURE" || type == "FUNCTION")
        {
            // subprogramas y sus parámetros
            QMap<QString, QVariantList> params; QStringList order; QMap<QString, QString> retType;
            for (const auto& r : safe("select nvl(a.object_name,' '), a.argument_name, a.in_out, a.data_type, a.position, a.overload from all_arguments a where a.owner = " + O + " and " + (type == "PACKAGE" ? "a.package_name = " + N : "a.object_name = " + N + " and a.package_name is null") + " and a.data_level = 0 order by a.object_name, a.overload, a.position", 6, 300))
            {
                const QString key = type == "PACKAGE" ? r[0] + (r[5].trimmed().isEmpty() ? QString() : " #" + r[5].trimmed()) : name; if (!order.contains(key)) order << key;
                if (r[4] == "0") retType[key] = r[3].toLower(); else { QVariantMap pm = vnode("item", r[1].toLower(), r[3].toLower(), r[2].toLower()); pm["copy"] = "param"; pm["raw"] = r[1]; pm["icon"] = r[2] == "OUT" ? "param_out" : r[2] == "IN/OUT" ? "param_inout" : "param_in"; params[key] << pm; }
            }
            if (type == "PACKAGE") for (const auto& r : safe("select procedure_name from all_procedures where owner = " + O + " and object_name = " + N + " and procedure_name is not null order by subprogram_id", 1, 4000)) { const QString k = r[0]; bool found = false; for (const QString& o : order) if (o == k || o.startsWith(k + " #")) found = true; if (!found) order << k; }
            for (const QString& k : order)
            {
                QVariantMap mm = vnode("item", k.section(" #", 0, 0).toLower(), retType.contains(k) ? "function → " + retType[k] : "procedure", QString(), params.value(k));
                mm["icon"] = retType.contains(k) ? "function" : "procedure"; mm["copy"] = type == "PACKAGE" ? "member" : "proc"; mm["raw"] = k.section(" #", 0, 0); kids << mm;
            }
            if (kids.isEmpty()) kids << vnode("item", "(sin parámetros ni subprogramas visibles)");
        }
        else if (type == "INDEX")
        {
            QHash<int, QString> expr; for (const auto& r : safe("select column_position, column_expression from all_ind_expressions where index_owner = " + O + " and index_name = " + N, 2, 400)) expr[r[0].toInt()] = r[1].trimmed();
            for (const auto& r : safe("select column_position, column_name, descend from all_ind_columns where index_owner = " + O + " and index_name = " + N + " order by column_position", 3, 400))
            { QVariantMap cm = vnode("column", expr.contains(r[0].toInt()) ? expr[r[0].toInt()] : r[1], QString::number(r[0].toInt()) + (r[2] == "DESC" ? "  desc" : QString())); kids << cm; }
        }
        else if (type == "CONSTRAINT")
        {
            for (const auto& r : safe("select column_name from all_cons_columns where owner = " + O + " and constraint_name = " + N + " order by position", 1, 400)) kids << vnode("column", r[0]);
            for (const auto& r : safe("select p.table_name, (select listagg(pc.column_name, ', ') within group (order by pc.position) from all_cons_columns pc where pc.owner = p.owner and pc.constraint_name = p.constraint_name), p.owner from all_constraints c join all_constraints p on p.owner = c.r_owner and p.constraint_name = c.r_constraint_name where c.owner = " + O + " and c.constraint_name = " + N, 3, 400))
                kids << withId(vnode("item", "references " + r[0].toLower(), "(" + r[1].toLower() + ")"), r[2], r[0], "TABLE");
        }
        else if (type == "SEQUENCE")
        {
            for (const auto& r : safe("select min_value, max_value, increment_by, cache_size, cycle_flag, order_flag, last_number from all_sequences where sequence_owner = " + O + " and sequence_name = " + N, 7, 10))
                for (int i = 0; i < 7; ++i) kids << vnode("column", QStringList{"min_value", "max_value", "increment_by", "cache_size", "cycle", "order", "last_number"}[i], r[i]);
        }
        else if (type == "SYNONYM")
        {
            for (const auto& r : safe("select table_owner, table_name, nvl(db_link,' ') from all_synonyms where owner = " + O + " and synonym_name = " + N, 3, 10))
                { kids << vnode("column", "apunta a", (r[0] + "." + r[1]).toLower()); if (!r[2].trimmed().isEmpty()) kids << vnode("column", "db link", r[2].trimmed().toLower()); }
        }
        else if (type == "TRIGGER")
        {
            for (const auto& r : safe("select table_owner, table_name, trigger_type, triggering_event, status, nvl(when_clause,' ') from all_triggers where owner = " + O + " and trigger_name = " + N, 6, 10))
            { kids << vnode("column", "tabla", (r[0] + "." + r[1]).toLower()) << vnode("column", "tipo", r[2].toLower()) << vnode("column", "evento", r[3].toLower()) << vnode("column", "estado", r[4].toLower()); if (!r[5].trimmed().isEmpty()) kids << vnode("column", "when", r[5].trimmed()); }
        }
        else if (type == "TYPE")
        {
            for (const auto& r : safe("select attr_name, attr_type_name, nvl(to_char(length),' ') from all_type_attrs where owner = " + O + " and type_name = " + N + " order by attr_no", 3, 4000)) kids << vnode("column", r[0].toLower(), r[1].toLower() + (r[2].trimmed().isEmpty() ? QString() : "(" + r[2].trimmed() + ")"));
        }
        if (type != "TABLE" && type != "VIEW" && type != "MATERIALIZED VIEW" && type != "SEQUENCE")
        {   // el resto de objetos: de qué depende y qué depende de ellos
            auto folder = [&](const char* key, const char* title) { QVariantMap m = vnode("folder", title); m["key"] = key; m["lazy"] = true; return QVariant(m); };
            kids << folder("dependon", "Dependent On") << folder("dependents", "Dependent Objects");
        }
        if (type == "VIEW" || type == "MATERIALIZED VIEW") { QVariantMap m = vnode("folder", "Dependent On"); m["key"] = "dependon"; m["lazy"] = true; kids << QVariant(m); }
        emit describeTiming(int(tmDesc.elapsed())); emit objectDescribed(owner, name, type, kids, token);
    }
    catch (const std::exception& e) { emit metadataFailed("Object Viewer", QString::fromUtf8(e.what()).trimmed()); }
}

// «Load DDL in one file»: el DDL de varios objetos seleccionados en un solo texto
void DbWorker::loadSourcesInOne (const QString& ownerIn, const QStringList& objects)
{
    if (!m_cn) return;
    const QString owner = ownerIn.isEmpty() ? m_owner : ownerIn;
    try
    {
        QString out; int n = 0;
        for (const QString& o : objects)
        {
            const QString type = o.section(QChar(0x1f), 0, 0), name = o.section(QChar(0x1f), 1);
            const QString d = ddlFor(owner, type, name, false); if (d.isEmpty()) { out += "-- (sin DDL) " + type + " " + name + "\n\n"; continue; }
            out += d + "\n"; ++n;
        }
        emit sourceReady("DDL", QString("DDL (%1 objetos)").arg(n), out);
    }
    catch (const std::exception& e) { emit failed("No se pudo leer el DDL:\n" + QString::fromUtf8(e.what()).trimmed()); }
}

// Table Transformation Helper: genera (sin ejecutar nada) el script para reconstruir una tabla: bloquear, renombrar la original,
// quitar disparadores/restricciones/índices, crear la tabla nueva, copiar los datos y recrear todo lo demás. Solo lee el diccionario.
void DbWorker::transformTable (const QString& ownerIn, const QString& table)
{
    if (!m_cn) return;
    const QString owner = ownerIn.isEmpty() ? m_owner : ownerIn;
    try
    {
        auto exec = [&](const QString& sql) { try { m_cn->ExecuteStatement(sql.toUtf8().constData()); } catch (...) {} };
        auto meta = [&](const QString& kind, const QString& name, bool dependent) -> QString {
            try { auto r = queryRows("select dbms_metadata." + QString(dependent ? "get_dependent_ddl" : "get_ddl") + "(" + sqlQuote(kind) + ", " + sqlQuote(name) + ", " + sqlQuote(owner) + ") from dual", 1, 4000000); return r.isEmpty() ? QString() : r[0][0].trimmed() + "\n"; }
            catch (...) { return QString(); } };
        for (const char* p : { "SEGMENT_ATTRIBUTES", "STORAGE", "TABLESPACE" }) exec(QString("begin dbms_metadata.set_transform_param(dbms_metadata.session_transform, '%1', false); end;").arg(p));
        exec("begin dbms_metadata.set_transform_param(dbms_metadata.session_transform, 'SQLTERMINATOR', true); end;");
        const auto exists = queryRows("select count(*) from all_tables where owner = " + sqlQuote(owner) + " and table_name = " + sqlQuote(table), 1, 40);
        if (exists.isEmpty() || exists[0][0].toInt() == 0) { emit metadataFailed("transformación de " + table, "La tabla no existe en " + owner + " (o no hay permisos)."); return; }
        // nombre libre para la copia de respaldo
        QString bak; for (int i = 0; i < 100; ++i) { const QString n = QString("%1_$%2").arg(table.left(26)).arg(i, 2, 10, QChar('0')); if (queryRows("select 1 from all_objects where owner = " + sqlQuote(owner) + " and object_name = " + sqlQuote(n), 1, 40).isEmpty()) { bak = n; break; } }
        if (bak.isEmpty()) { emit metadataFailed("transformación de " + table, "No se pudo generar un nombre libre para la copia."); return; }
        const QString T = table.toLower(), B = bak.toLower();
        QString o; o += "PROMPT ================================================================\nPROMPT  Table Transformation Helper — tabla " + owner + "." + table + "\nPROMPT  PRECAUCIÓN: revise y ejecute cada sentencia paso a paso, a su riesgo.\nPROMPT  Si una sentencia falla, DETÉNGASE y repita antes de continuar.\nPROMPT ================================================================\n\n";
        o += "PROMPT BLOQUEAR la tabla\nLOCK TABLE " + T + " IN EXCLUSIVE MODE NOWAIT\n/\n\n";
        o += "PROMPT RENOMBRAR la tabla original (copia de respaldo)\nRENAME " + T + " TO " + B + "\n/\n\n";
        o += "PROMPT QUITAR disparadores\n";
        for (const auto& r : queryRows("select owner, trigger_name from all_triggers where table_owner = " + sqlQuote(owner) + " and table_name = " + sqlQuote(table) + " order by trigger_name", 2, 200)) o += "DROP TRIGGER " + (r[0] == owner ? QString() : r[0].toLower() + ".") + r[1].toLower() + "\n/\n\n";
        o += "PROMPT QUITAR restricciones (primarias, únicas y con nombre)\n";
        for (const auto& r : queryRows("select constraint_name, constraint_type from all_constraints where owner = " + sqlQuote(owner) + " and table_name = " + sqlQuote(table) + " and (constraint_type in ('P','U') or (generated = 'USER NAME' and constraint_type in ('C','R'))) order by decode(constraint_type,'R',1,'C',2,3), constraint_name", 2, 200))
            o += "ALTER TABLE " + B + "\n  DROP CONSTRAINT " + r[0].toLower() + (r[1] == "P" || r[1] == "U" ? " CASCADE" : "") + "\n/\n\n";
        o += "PROMPT QUITAR índices\n";
        const auto idx = queryRows("select index_name from all_indexes where table_owner = " + sqlQuote(owner) + " and table_name = " + sqlQuote(table) + " and index_name not in (select constraint_name from all_constraints where owner = " + sqlQuote(owner) + " and table_name = " + sqlQuote(table) + " and constraint_type in ('P','U')) order by index_name", 1, 200);
        for (const auto& r : idx) o += "DROP INDEX " + r[0].toLower() + "\n/\n\n";
        exec("begin dbms_metadata.set_transform_param(dbms_metadata.session_transform, 'CONSTRAINTS', false); end;");
        exec("begin dbms_metadata.set_transform_param(dbms_metadata.session_transform, 'REF_CONSTRAINTS', false); end;");
        o += "PROMPT CREAR la tabla " + T + "\n" + meta("TABLE", table, false) + "\n";
        exec("begin dbms_metadata.set_transform_param(dbms_metadata.session_transform, 'CONSTRAINTS', true); end;");
        exec("begin dbms_metadata.set_transform_param(dbms_metadata.session_transform, 'REF_CONSTRAINTS', true); end;");
        QStringList cols; for (const auto& r : queryRows("select column_name from all_tab_columns where owner = " + sqlQuote(owner) + " and table_name = " + sqlQuote(table) + " and hidden_column = 'NO' and virtual_column = 'NO' order by column_id", 1, 200)) cols << r[0].toLower();
        if (!cols.isEmpty()) { QStringList c2; for (const QString& c : cols) c2 << "  " + c; o += "PROMPT TRASPASAR los datos\nINSERT /*+append*/ INTO " + T + " (\n" + c2.join(",\n") + "\n)\n(\n  SELECT\n" + c2.join(",\n").replace(QRegularExpression("(^|\n)  "), "\\1    ") + "\n  FROM " + B + "\n)\n/\n\n"; }
        o += "PROMPT CREAR índices\n"; for (const auto& r : idx) o += meta("INDEX", r[0], false) + "\n";
        o += "PROMPT CREAR restricciones\n" + meta("CONSTRAINT", table, true) + "\n" + meta("REF_CONSTRAINT", table, true) + "\n";
        o += "PROMPT CREAR restricciones de otras tablas que apuntan a esta\n";
        for (const auto& r : queryRows("select c.owner, c.constraint_name from all_constraints c where c.constraint_type = 'R' and (c.r_owner, c.r_constraint_name) in (select owner, constraint_name from all_constraints where owner = " + sqlQuote(owner) + " and table_name = " + sqlQuote(table) + " and constraint_type in ('P','U')) and not (c.owner = " + sqlQuote(owner) + " and c.table_name = " + sqlQuote(table) + ")", 2, 200))
            { try { auto d = queryRows("select dbms_metadata.get_ddl('REF_CONSTRAINT', " + sqlQuote(r[1]) + ", " + sqlQuote(r[0]) + ") from dual", 1, 4000000); if (!d.isEmpty()) o += d[0][0].trimmed() + "\n\n"; } catch (...) {} }
        o += "PROMPT CREAR disparadores\n" + meta("TRIGGER", table, true) + "\n";
        o += "PROMPT CREAR comentarios\n" + meta("COMMENT", table, true) + "\n";
        o += "PROMPT OTORGAR privilegios\n" + meta("OBJECT_GRANT", table, true) + "\n";
        emit sourceReady("TABLE", "Transform " + table, o);
    }
    catch (const std::exception& e) { emit failed("No se pudo generar el script de transformación:\n" + QString::fromUtf8(e.what()).trimmed()); }
}

// Extract Schema DDL: recorre los objetos del esquema y concatena su DDL. Solo lee del diccionario.
void DbWorker::extractSchema (const QString& ownerIn, const QStringList& types)
{
    if (!m_cn) return;
    const QString owner = ownerIn.isEmpty() ? m_owner : ownerIn;
    try
    {
        QStringList in; for (const QString& t : types) in << sqlQuote(t);
        if (in.isEmpty()) { emit metadataFailed("DDL del esquema", "No se eligió ningún tipo de objeto."); return; }
        const auto objs = queryRows("select object_type, object_name from all_objects where owner = " + sqlQuote(owner) + " and object_type in (" + in.join(",") + ") and generated = 'N' and object_name not like 'BIN$%' order by decode(object_type,'SEQUENCE',1,'TYPE',2,'TABLE',3,'VIEW',4,'INDEX',5,'PACKAGE',6,'FUNCTION',7,'PROCEDURE',8,'TRIGGER',9,10), object_name", 2, 200);
        const int total = int(objs.size()); QString out = "-- DDL del esquema " + owner + " (" + QString::number(total) + " objetos)\n\n"; int ok = 0, bad = 0, n = 0;
        for (const auto& o : objs)
        {
            emit schemaDdlProgress(++n, total);
            try { const QString d = ddlFor(owner, o[0], o[1], false); if (d.isEmpty()) { ++bad; out += "-- (sin DDL) " + o[0] + " " + o[1] + "\n\n"; } else { ++ok; out += d + "\n"; } }
            catch (...) { ++bad; out += "-- (error al leer) " + o[0] + " " + o[1] + "\n\n"; }
        }
        emit schemaDdlReady(owner, out, ok, bad);
    }
    catch (const std::exception& e) { emit failed("No se pudo extraer el DDL del esquema:\n" + QString::fromUtf8(e.what()).trimmed()); }
}

void DbWorker::execute (const QString& sql) { executeScript(QStringList{sql}, QVector<int>{1}); }

void DbWorker::executeScript (const QStringList& statements, const QVector<int>& startLines)
{
    if (!m_cn) { emit failed("No hay conexión abierta."); return; }
    if (statements.isEmpty()) { emit failed("No hay nada que ejecutar."); return; }

    m_running = true;
    QElapsedTimer timer; timer.start();
    qint64 rows = 0; QString message; int errors = 0; m_extraResults = 0;
    const int total = statements.size();
    for (int idx = 0; idx < total; ++idx)
    {
        const bool last = (idx == total - 1);
        QElapsedTimer stmtTimer; stmtTimer.start();
        QMap<QString, double> before;
        if (m_sessionStats) try { before = statSnapshot(queryRows("select n.name, s.value from v$mystat s join v$statname n on n.statistic# = s.statistic#", 2)); } catch (...) { m_sessionStats = false; emit messageReady("No se pudieron leer las estadísticas de sesión (V$MYSTAT): desactivadas."); }
        try
        {
            m_stmtStartLine = idx < startLines.size() ? startLines[idx] : 1;
            runOne(statements[idx], last, idx, rows, message);
            emit statementDone(idx, statements[idx], rows, stmtTimer.elapsed() / 1000.0, QString());
            if (m_sessionStats && !before.isEmpty())
                try
                {
                    auto after = statSnapshot(queryRows("select n.name, s.value from v$mystat s join v$statname n on n.statistic# = s.statistic#", 2));
                    QVector<QStringList> diff;
                    for (auto it = after.begin(); it != after.end(); ++it) { const double d = it.value() - before.value(it.key()); if (d != 0) diff.push_back({it.key(), QString::number(d, 'f', 0)}); }
                    std::sort(diff.begin(), diff.end(), [](const QStringList& a, const QStringList& b){ return a[0] < b[0]; });
                    emit statsReady(diff);
                }
                catch (...) {}
            if (!last) emit messageReady(QString("[%1/%2] %3").arg(idx + 1).arg(total).arg(message.isEmpty() ? QString("Consulta ejecutada.") : message));
        }
        catch (const std::exception& e)
        {
            QString msg = QString::fromUtf8(e.what()).trimmed();
            emit statementDone(idx, statements[idx], 0, stmtTimer.elapsed() / 1000.0, msg.section('\n', 0, 0));
            if (total > 1) msg += QString("\n(sentencia %1 de %2, desde la línea %3)").arg(idx + 1).arg(total).arg(idx < startLines.size() ? startLines[idx] : 0);
            if (!m_haltOnError && total > 1 && !last)       // «Halt Execution on Errors» desactivado: se anota el error y se sigue
            { emit messageReady("ERROR " + msg.replace('\n', ' ')); ++errors; continue; }
            m_running = false;
            collectOutput();                         // lo impreso antes del error también interesa
            emit failed(msg);
            return;
        }
    }
    m_running = false;
    collectOutput();
    if (total > 1) message = QString("Script: %1 sentencias ejecutadas").arg(total) + (errors ? QString(" (%1 con error)").arg(errors) : QString()) + ". " + message;
    emit finished(rows, timer.elapsed() / 1000.0, message);
}
