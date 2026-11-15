#pragma once
// Divide un script en sentencias (SQL suelto o bloques PL/SQL) y encuentra la que está bajo el cursor.
// Reglas:
//  - SQL suelto termina en ';' fuera de textos y comentarios.
//  - Un bloque anónimo (BEGIN/DECLARE) termina en el ';' que cierra su último END (se cuentan BEGIN/IF/LOOP/CASE contra sus END).
//  - CREATE [OR REPLACE] PROCEDURE/FUNCTION/PACKAGE/TRIGGER/TYPE y similares terminan solo en una línea que contenga únicamente '/'.
//  - Una línea con solo '/' siempre cierra la sentencia en curso.
#include <QString>
#include <QVector>

struct SqlStatement
{
    int start = 0;          // desplazamiento del primer carácter útil
    int end = 0;            // desplazamiento justo después del último carácter (sin ';' ni '/')
    bool plsql = false;     // bloque PL/SQL (conserva el ';' final del END al ejecutar)
    int startLine = 1;      // línea (desde 1) donde empieza
    QString text;           // texto listo para ejecutar
};

class SqlSplitter
{
public:
    static QVector<SqlStatement> split (const QString& script);
    // Índice de la sentencia que contiene la posición (o la anterior si cae en blancos entre dos); -1 si no hay.
    static int statementAt (const QVector<SqlStatement>& stmts, int position);
};
