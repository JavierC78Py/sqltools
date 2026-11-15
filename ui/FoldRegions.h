#pragma once
// Regiones plegables de un script SQL/PL-SQL: bloques BEGIN…END, IF, LOOP, CASE, subprogramas (PROCEDURE/FUNCTION … IS/AS … END),
// paquetes y tipos, bloques DECLARE, sentencias de varias líneas, paréntesis y comentarios /* … */ de varias líneas.
#include <QString>
#include <QVector>

struct FoldRegion
{
    int start = 0;           // línea inicial (desde 0): se queda visible; el resto del bloque se oculta al plegar
    int end = 0;             // última línea de la región (incluida)
    int level = 0;           // profundidad de anidamiento (0 = exterior)
    QString kind;            // stmt | begin | if | loop | case | sub | pkg | declare | paren | comment
};

namespace FoldRegions
{
    QVector<FoldRegion> compute (const QString& text);          // ordenadas por línea inicial; una región tiene al menos 2 líneas
}
