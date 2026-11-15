#pragma once
// Colores y estilos del resaltado de sintaxis (editables en Settings > Colores; uno para el tema claro y otro para el oscuro).
#include <QColor>
#include <QVector>
#include <QString>

enum SynClass { SynKeyword, SynFunction, SynNumber, SynString, SynComment, SynOperator, SynBind, SynHint, SynUserObject, SynClassCount };
struct SynStyle { QColor color; bool bold = false, italic = false, underline = false; };

namespace SyntaxColors
{
    QString className (int c);                           // nombre legible del elemento
    QVector<SynStyle> defaults (bool dark);              // valores predeterminados de este programa
    QVector<SynStyle> classic (bool dark);               // aspecto del SQLTools original de Windows (palabras con subrayado, números en negrita…)
    QVector<SynStyle> load (bool dark);                  // los guardados por el usuario (o los predeterminados)
    void save (bool dark, const QVector<SynStyle>& v);
    void reset (bool dark);                              // vuelve a los predeterminados
    QString sampleSql ();                                // texto de muestra para la vista previa
}
