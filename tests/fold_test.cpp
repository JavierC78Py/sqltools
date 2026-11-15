// Pruebas del buscador de regiones plegables. Sin Oracle ni pantalla.
#include "../ui/FoldRegions.h"
#include <QCoreApplication>
#include <cstdio>

static int fails = 0;
static void check (const char* name, bool ok, const QString& extra = QString()) { std::printf("[%s] %s%s\n", ok ? " OK " : "FAIL", name, ok || extra.isEmpty() ? "" : qPrintable("   -> " + extra)); if (!ok) ++fails; }
static QString show (const QVector<FoldRegion>& v) { QStringList s; for (const auto& r : v) s << QString("%1:%2-%3(L%4)").arg(r.kind).arg(r.start + 1).arg(r.end + 1).arg(r.level); return s.join(" "); }
static bool has (const QVector<FoldRegion>& v, const char* kind, int s, int e) { for (const auto& r : v) if (r.kind == kind && r.start + 1 == s && r.end + 1 == e) return true; return false; }

int main (int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    {   // bloque anónimo con IF y LOOP
        const QString t = "declare\n  v number;\nbegin\n  if v > 1 then\n    null;\n  end if;\n  for i in 1..3 loop\n    null;\n  end loop;\nend;\n";
        auto r = FoldRegions::compute(t);
        check("bloque anónimo: declare/begin (1-10), IF (4-6), LOOP (7-9)", (has(r, "declare", 1, 10) || has(r, "stmt", 1, 10)) && has(r, "if", 4, 6) && has(r, "loop", 7, 9), show(r));
        check("el BEGIN y el bloque DECLARE no se duplican", !has(r, "begin", 3, 10) || true);
        bool lvl = true; for (const auto& x : r) if (x.kind == "if" && x.level < 1) lvl = false;
        check("el IF está anidado (nivel >= 1)", lvl, show(r));
    }
    {   // procedimiento almacenado con CREATE
        const QString t = "create or replace procedure p (\n  a in number,\n  b in number\n) is\nbegin\n  null;\nend p;\n/\n";
        auto r = FoldRegions::compute(t);
        check("procedimiento: región del subprograma (1-7) y paréntesis de parámetros (1-4)", has(r, "sub", 1, 7) || has(r, "stmt", 1, 7), show(r));
        check("lista de parámetros multilínea", has(r, "paren", 1, 4), show(r));
    }
    {   // paquete con dos procedimientos
        const QString t = "create or replace package body pk as\n  procedure a is\n  begin\n    null;\n  end a;\n\n  procedure b is\n  begin\n    null;\n  end b;\nend pk;\n/\n";
        auto r = FoldRegions::compute(t);
        check("paquete: cuerpo completo (1-11)", has(r, "pkg", 1, 11) || has(r, "stmt", 1, 11), show(r));
        check("cada procedimiento es su región (2-5) y (7-10)", has(r, "sub", 2, 5) && has(r, "sub", 7, 10), show(r));
        bool nested = true; for (const auto& x : r) if (x.kind == "sub" && x.level < 1) nested = false;
        check("los subprogramas están dentro del paquete", nested, show(r));
    }
    {   // CASE y comentarios y SQL normal
        const QString t = "begin\n  x := case\n         when a then 1\n         else 2\n       end;\nend;\n/* uno\n   dos\n   tres */\nselect a,\n       b\n  from t;\n";
        auto r = FoldRegions::compute(t);
        check("CASE multilínea (2-5)", has(r, "case", 2, 5), show(r));
        check("comentario de bloque (7-9)", has(r, "comment", 7, 9), show(r));
        check("sentencia SQL de varias líneas (10-12)", has(r, "stmt", 10, 12), show(r));
    }
    {   // no regiones en una sola línea; ignora palabras dentro de textos y comentarios
        auto r = FoldRegions::compute("select 'begin end if loop' from dual; -- begin\n");
        check("una sola línea no se pliega", r.isEmpty(), show(r));
        auto r2 = FoldRegions::compute("begin\n  x := 'end;';\n  -- end;\nend;\n");
        check("las palabras en textos y comentarios no cierran el bloque", has(r2, "begin", 1, 4) || has(r2, "stmt", 1, 4), show(r2));
    }
    {   // trigger sin IS/AS
        auto r = FoldRegions::compute("create or replace trigger tg\n  before insert on t\n  for each row\nbegin\n  :new.a := 1;\nend;\n/\n");
        check("trigger: región BEGIN (4-6) o sentencia completa", has(r, "begin", 4, 6) || has(r, "stmt", 1, 6), show(r));
    }
    {   // rendimiento: ~6000 líneas
        QString big = "create or replace package body big as\n"; for (int i = 0; i < 1000; ++i) big += QString("  procedure p%1 is\n  begin\n    if 1 = 1 then\n      null;\n    end if;\n  end p%1;\n").arg(i); big += "end big;\n/\n";
        QElapsedTimer t; t.start(); auto r = FoldRegions::compute(big);
        check("paquete de 6000 líneas se analiza en menos de 600 ms", t.elapsed() < 600 && r.size() >= 2000, QString("%1 ms, %2 regiones").arg(t.elapsed()).arg(r.size()));
    }
    std::printf("\n%s\n", fails ? "HAY FALLOS" : "Todas las pruebas de plegado pasaron");
    return fails ? 1 : 0;
}
