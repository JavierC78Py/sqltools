#pragma once
// Hilo de base de datos: TODA operación OCI ocurre en este objeto (que vive en su propio QThread),
// porque la capa OCI8 de SQLTools está pensada para que cada conexión se use desde un solo hilo.
// Solo cancel() puede llamarse desde otro hilo (usa el BreakHandler de OCI8).
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QStringList>
#include <QVector>
#include <memory>
#include <atomic>

namespace OCI8 { class Connect; class BreakHandler; }

class DbWorker : public QObject
{
    Q_OBJECT
public:
    explicit DbWorker (QObject* parent = nullptr);
    static QString clientVersion ();                         // versión del Oracle Instant Client en uso, p.ej. "19.16.0.0.0"
    ~DbWorker () override;
    void cancel ();                          // seguro desde cualquier hilo

public slots:
    // params: user, password, alias, tnsAdmin, bypass, host, port, sid, useService, mode ("Normal"|"SYSDBA"|"SYSOPER"), readOnly, slow, tag
    void connectDb (const QVariantMap& params);
    void testConnection (const QVariantMap& params);         // abre una conexión aparte, lee la versión y la cierra
    void disconnectDb ();
    void execute (const QString& sql);
    // Ejecuta varias sentencias en orden; se detiene en el primer error. Solo la última, si es consulta, llena la rejilla.
    void executeScript (const QStringList& statements, const QVector<int>& startLines);
    // Metadatos para el explorador y el autocompletado (consultas al diccionario; corren en este hilo, en cola tras lo que esté ejecutándose)
    void loadSchemas ();
    void loadUserObjects ();                                                    // nombres de los objetos del esquema actual → userObjectsLoaded
    void loadObjectsOf (const QString& owner);                                  // objetos visibles de un esquema ("" = el actual)
    void loadSynonyms ();                                                       // privados propios y públicos hacia esquemas de usuario
    void loadColumns (const QString& owner, const QString& table);              // también resuelve sinónimos hacia su destino
    void loadSource (const QString& owner, const QString& type, const QString& name, bool viaMetadata = false);
    void transformTable (const QString& owner, const QString& table);             // script para reconstruir una tabla (Table Transformation Helper)
    void extractSchema (const QString& owner, const QStringList& types);
    void loadObjectDdl (const QString& owner, const QStringList& objects, const QVariantMap& options, bool oneFile);   // «Load DDL» con la biblioteca MetaDictionary de SQLTools
    void loadObjectList (const QString& owner, const QString& key, const QString& sql, int columns);   // una pestaña de la lista de objetos
    void describeObject (const QString& owner, const QString& name, const QString& type, const QString& token = QString());   // token vacío = raíz del Object Viewer; si no, el nodo que se está abriendo
    void describeFolder (const QString& owner, const QString& name, const QString& type, const QString& key);   // carpeta del Object Viewer (se carga al abrirla)   // Object Viewer (F12): propiedades y relaciones del objeto
    void loadSourcesInOne (const QString& owner, const QStringList& objects);       // "TIPO\x1fNOMBRE"… -> un solo texto
    void setDdlOptions (bool storage, bool terminator, bool specWithBody);          // DDL de todo un esquema (solo lectura del diccionario)
    void loadMembers (const QString& owner, const QString& package);
    void setReadOnly (bool on);                              // Session > Read Only
    void setDbmsOutput (bool on);                            // Session > Enable Dbms Output
    void setHaltOnError (bool on);
    void setTransactionOptions (bool autocommit, int commitOnDisconnect);   // commitOnDisconnect: 0 deshacer, 1 confirmar (la pregunta la hace la ventana)                           // Script > Halt Execution on Errors
    void reconnect ();
    void setFetchOptions (int prefetch, int stringLimit);    // Grid > Settings
    void setSessionStats (bool on);                          // Session > Enable Session Statistics
    void explainPlan (const QString& sql);                   // Script > Explain Plan                                       // Session > Test Connection/Reconnect
    void commit ();
    void rollback ();

signals:
    void connected (const QString& serverVersion, const QString& who);
    void testResult (bool ok, const QString& message);
    void disconnected ();
    void columnsReady (const QStringList& names, const QVector<int>& rightAligned);
    void rowsReady (const QVector<QStringList>& rows);
    // Consultas intermedias de un script (todas menos la última): cada una llega con su índice y se muestra en su propia pestaña
    void resultColumns (int index, const QStringList& names, const QVector<int>& rightAligned);
    void resultRows (int index, const QVector<QStringList>& rows);
    void resultDone (int index, qint64 rows, bool truncated);       // lotes de filas; una celda NULL = QString() nula
    // Errores/avisos de compilación de un objeto PL/SQL recién creado: cada fila = {tipo, línea absoluta del editor, columna, texto}
    void compileErrors (const QVector<QStringList>& rows, const QString& objectName);
    void schemasLoaded (const QStringList& names, const QString& current);
    void userObjectsLoaded (const QStringList& namesUpper);                                         // nombres de los objetos del esquema actual (para colorearlos en el editor)
    void objectsLoaded (const QString& owner, const QVector<QStringList>& rows);                      // {tipo, nombre, estado}
    void synonymsLoaded (const QVector<QStringList>& rows);                                           // {sinónimo, esquema del sinónimo, esquema destino, objeto destino}
    void columnsLoaded (const QString& owner, const QString& table, const QVector<QStringList>& rows); // {columna, tipo, anulable}
    void describeTiming (int dbMs);                                                                 // tiempo de la base al describir un objeto (Object Viewer)
    void sourceTiming (int dbMs);                                                                   // tiempo que tardó la base en entregar el código (se muestra en la barra de estado)
    void sourceReady (const QString& type, const QString& name, const QString& text);
    void objectListLoaded (const QString& owner, const QString& key, const QVector<QStringList>& rows, const QString& error);
    void objectDescribed (const QString& owner, const QString& name, const QString& type, const QVariantList& children, const QString& token);   // nodos: {kind, text, c1, c2, children}
    void folderDescribed (const QString& owner, const QString& name, const QString& key, const QVariantList& children, const QString& error);
    void schemaDdlProgress (int done, int total);
    void schemaDdlReady (const QString& owner, const QString& text, int count, int failed);
    void metadataFailed (const QString& what, const QString& message);           // silencioso: barra de estado
    void statementDone (int index, const QString& sql, qint64 rows, double seconds, const QString& error);   // para el historial
    void transactionState (bool pending);                    // hay cambios sin confirmar
    void statsReady (const QVector<QStringList>& rows);       // {estadística, diferencia} de la última ejecución (si están activadas)
    void planReady (const QString& text);                     // salida de DBMS_XPLAN
    void outputReady (const QString& text);                  // salida de DBMS_OUTPUT
    void messageReady (const QString& line);                 // avisos de progreso de un script
    void finished (qint64 rows, double seconds, const QString& message);
    void failed (const QString& message);

private:
    std::unique_ptr<OCI8::Connect> openFrom (const QVariantMap& params, QString* error, QString* note = nullptr);
    bool runOne (const QString& sql, bool live, int resultIndex, qint64& rows, QString& message);
    int m_extraResults = 0;                                  // pestañas de resultados intermedios ya emitidas en este script
    void collectOutput ();
    void fetchCompileErrors (const QString& sql);
    QString metaDdl (const QString& owner, const QString& type, const QString& name, const QVariantMap& options);
    QString ddlFor (const QString& owner, const QString& type, const QString& name, bool viaMetadata);
    QVector<QStringList> queryRows (const QString& sql, int columns, int stringLimit = 4000);
    bool m_ddlConfigured = false;
    QString m_owner;
    QVariantMap m_params;                  // parámetros de la última conexión (para reconectar)
    bool m_ddlStorage = false, m_ddlTerminator = true, m_specWithBody = true;
    bool m_autocommit = false, m_dirty = false; int m_commitOnDisconnect = 0;
    bool m_outputOn = true, m_haltOnError = true, m_sessionStats = false;
    int m_prefetch = 10000, m_stringLimit = 4000;                       // esquema actual de la sesión
    int m_stmtStartLine = 1;
    std::unique_ptr<OCI8::Connect> m_cn;
    std::shared_ptr<OCI8::BreakHandler> m_break;
    std::atomic<bool> m_running {false};
};
