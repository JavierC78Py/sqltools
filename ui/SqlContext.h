#pragma once
// Análisis del texto alrededor del cursor para el autocompletado: palabra que se está escribiendo, calificador
// ("alias." / "tabla." / "paquete."), y qué alias/tablas hay en la sentencia actual (FROM/JOIN/UPDATE/INTO).
#include <QHash>
#include <QString>
#include <QStringList>

// Origen "derivado": una subconsulta en el FROM con alias, o una CTE (WITH nombre AS (...)).
struct DerivedSource
{
    QStringList columns;       // nombres de columna deducidos de la lista del SELECT (o de la lista explícita de la CTE)
    QStringList starTables;    // tablas cuyo "*" / "alias.*" hay que expandir con sus columnas reales
};

struct SqlContext
{
    QString prefix;                     // lo ya escrito de la palabra actual (puede estar vacío)
    int prefixStart = 0;                // posición donde empieza esa palabra
    QString qualifierSchema;            // esquema si se escribió "esquema.objeto." (p.ej. "SYS" en "sys.dbms_output.")
    QString qualifier;                  // identificador antes del punto ("e" en "e.nom"), en mayúsculas; vacío si no hay
    bool afterFrom = false;             // el cursor está donde se espera un nombre de tabla/vista
    QHash<QString, DerivedSource> derived;   // ALIAS/CTE (mayúsculas) -> columnas deducidas
    QHash<QString, QString> aliases;    // ALIAS o TABLA (mayúsculas) -> TABLA (mayúsculas), de la sentencia actual
    static SqlContext analyze (const QString& text, int position);
};
