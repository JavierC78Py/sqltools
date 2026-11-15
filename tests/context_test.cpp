// Pruebas del análisis de contexto del autocompletado (sin Oracle).
#include "../ui/SqlContext.h"
#include <QCoreApplication>
#include <cstdio>

static int fails = 0;
static void check (const char* name, bool ok, const QString& extra = QString()) { std::printf("[%s] %s%s\n", ok ? " OK " : "FAIL", name, ok || extra.isEmpty() ? "" : qPrintable("   -> " + extra)); if (!ok) ++fails; }
// El cursor se indica con '|' en el texto.
static SqlContext ctxOf (QString t) { int p = t.indexOf('|'); t.remove(p, 1); return SqlContext::analyze(t, p); }

int main (int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    { auto c = ctxOf("select e.| from emp e"); check("calificador 'e' y alias definido después del cursor", c.qualifier == "E" && c.prefix.isEmpty() && c.aliases.value("E") == "EMP"); }
    { auto c = ctxOf("select e.nom| from emp e"); check("prefijo tras el punto", c.qualifier == "E" && c.prefix == "nom"); }
    { auto c = ctxOf("select emp.| from emp"); check("tabla sin alias", c.qualifier == "EMP" && c.aliases.value("EMP") == "EMP"); }
    { auto c = ctxOf("select * from emp e join dept d on e.deptno = d.|"); check("join con dos alias", c.aliases.value("E") == "EMP" && c.aliases.value("D") == "DEPT" && c.qualifier == "D"); }
    { auto c = ctxOf("select * from emp as e, dept d where |"); check("AS y coma", c.aliases.value("E") == "EMP" && c.aliases.value("D") == "DEPT" && !c.afterFrom); }
    { auto c = ctxOf("select * from kmelot.emp x where x.|"); check("esquema.tabla alias conserva el esquema", c.aliases.value("X") == "KMELOT.EMP" && c.aliases.value("EMP") == "KMELOT.EMP" && c.qualifier == "X"); }
    { auto c = ctxOf("select * from |"); check("después de FROM", c.afterFrom && c.prefix.isEmpty()); }
    { auto c = ctxOf("select * from emp e join |"); check("después de JOIN", c.afterFrom); }
    { auto c = ctxOf("select * from emp e where e.sal > 1 and |"); check("después de WHERE no es tabla", !c.afterFrom); }
    { auto c = ctxOf("insert into |"); check("después de INTO", c.afterFrom); }
    { auto c = ctxOf("update emp set sal = 1 where |"); check("UPDATE: tabla en alias", c.aliases.value("EMP") == "EMP" && !c.afterFrom); }
    { auto c = ctxOf("select 1 from dual;\nselect * from dept d where d.|"); check("solo la sentencia actual", c.aliases.value("D") == "DEPT" && !c.aliases.contains("DUAL")); }
    { auto c = ctxOf("select * from (select * from emp) t where t.|"); check("subconsulta: no inventa alias de tabla", c.qualifier == "T"); }
    { auto c = ctxOf("select 'a.b' x, \"Mi Tabla\".| from \"Mi Tabla\""); check("identificador entre comillas", c.qualifier == "Mi Tabla"); }
    { auto c = ctxOf("begin\n  pkg_wa_api.|\nend;"); check("calificador de paquete en PL/SQL", c.qualifier == "PKG_WA_API"); }
    { auto c = ctxOf("sel|"); check("prefijo simple", c.prefix == "sel" && c.qualifier.isEmpty()); }
    { auto c = ctxOf("begin\n  rman$catalog.dbms_rcvman.|\nend;"); check("esquema.paquete. -> calificador y esquema", c.qualifier == "DBMS_RCVMAN" && c.qualifierSchema == "RMAN$CATALOG", c.qualifier + " / " + c.qualifierSchema); }
    { auto c = ctxOf("select * from emp e where e.|"); check("sin esquema: qualifierSchema vacío", c.qualifierSchema.isEmpty()); }
    { auto c = ctxOf("select * from rman$catalog.|"); check("'FROM esquema.' no registra el esquema como tabla", !c.aliases.contains("RMAN$CATALOG") && c.qualifier == "RMAN$CATALOG" && c.afterFrom == false, QString::number(c.aliases.size())); }
    // ---- CTE y subconsultas ----
    { auto c = ctxOf("with v as (select id, nombre n, sueldo * 2 doble from emp) select v.| from v"); auto d = c.derived.value("V");
      check("CTE: columnas con alias y sin alias", d.columns == QStringList({"ID", "N", "DOBLE"}) && c.qualifier == "V", d.columns.join(",")); }
    { auto c = ctxOf("with v (a, b) as (select 1, 2 from dual) select v.| from v"); check("CTE con lista explícita de columnas", c.derived.value("V").columns == QStringList({"A", "B"})); }
    { auto c = ctxOf("with a as (select x from t1), b as (select y, z from t2) select b.| from a, b"); check("varias CTE", c.derived.value("A").columns == QStringList({"X"}) && c.derived.value("B").columns == QStringList({"Y", "Z"})); }
    { auto c = ctxOf("select t.| from (select e.id, e.nombre, count(*) cnt from emp e group by e.id, e.nombre) t");
      check("subconsulta con alias t: id, nombre, cnt", c.derived.value("T").columns == QStringList({"ID", "NOMBRE", "CNT"}), c.derived.value("T").columns.join(",")); }
    { auto c = ctxOf("select t.| from (select * from emp) t"); check("subconsulta con *: pide expandir EMP", c.derived.value("T").starTables == QStringList({"EMP"}) && c.derived.value("T").columns.isEmpty()); }
    { auto c = ctxOf("select t.| from (select e.*, d.nombre dn from emp e join dept d on e.d = d.id) t"); auto d = c.derived.value("T");
      check("alias.* y columna con alias", d.starTables == QStringList({"EMP"}) && d.columns == QStringList({"DN"}), d.starTables.join(",") + " / " + d.columns.join(",")); }
    { auto c = ctxOf("select * from emp where id in (select id from dept) and |"); check("subconsulta sin alias no crea origen", c.derived.isEmpty()); }
    { auto c = ctxOf("select x.| from (select case when a = 1 then 'u' else 'd' end tipo, nvl(b, 0) from t) x"); check("CASE ... END alias; expresión sin alias se omite", c.derived.value("X").columns == QStringList({"TIPO"}), c.derived.value("X").columns.join(",")); }
    { auto c = ctxOf("select v.| from (select a + b suma, c from t) v"); check("expresión con operador y alias", c.derived.value("V").columns == QStringList({"SUMA", "C"}), c.derived.value("V").columns.join(",")); }
    { auto c = ctxOf("with v as (select id from emp) select * from v x where x.|"); check("alias de una CTE (x -> V)", c.aliases.value("X") == "V" && c.derived.contains("V")); }
    std::printf("\n%s\n", fails ? "HAY FALLOS" : "Todas las pruebas de contexto pasaron");
    return fails ? 1 : 0;
}
