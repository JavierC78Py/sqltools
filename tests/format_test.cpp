#include "../ui/SqlFormatter.h"
#include <QCoreApplication>
#include <QTextStream>
#include <QFile>
#include <cstdio>
static int bad = 0, total = 0;
static void check (const char* name, const QString& got, const QString& want)
{ ++total; if (got == want) { printf("[ OK ] %s\n", name); return; } ++bad; printf("[FAIL] %s\n--- obtenido:\n%s\n--- esperado:\n%s\n", name, got.toUtf8().constData(), want.toUtf8().constData()); }
int main (int argc, char** argv)
{
    QCoreApplication app(argc, argv); setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc > 1) { QFile f(argv[1]); f.open(QIODevice::ReadOnly); FormatOptions fo = FormatOptions(); if (argc > 2 && QString(argv[2]) == "river") fo.style = FormatOptions::River; printf("%s", SqlFormatter::format(QString::fromUtf8(f.readAll()), fo).toUtf8().constData()); return 0; }
    FormatOptions o; o.style = FormatOptions::Standard; o.indent = 2;
    const QString q1 = "select a.id,b.nombre,sum(c.total) from cab_pedido a join cliente b on a.id_cliente=b.id left join det_pedido c on c.id_pedido=a.id where a.estado='A' and b.activo=1 group by a.id,b.nombre order by 3 desc;";
    check("SQL estándar", SqlFormatter::format(q1, o),
        "SELECT\n  a.id,\n  b.nombre,\n  SUM(c.total)\nFROM cab_pedido a\n  JOIN cliente b ON a.id_cliente = b.id\n  LEFT JOIN det_pedido c ON c.id_pedido = a.id\nWHERE a.estado = 'A'\n  AND b.activo = 1\nGROUP BY\n  a.id,\n  b.nombre\nORDER BY 3 DESC;");
    o.style = FormatOptions::River;
    check("SQL riachuelo", SqlFormatter::format(q1, o),
        "SELECT a.id,\n       b.nombre,\n       SUM(c.total)\n  FROM cab_pedido a\n  JOIN cliente b ON a.id_cliente = b.id\n  LEFT JOIN det_pedido c ON c.id_pedido = a.id\n WHERE a.estado = 'A'\n   AND b.activo = 1\n GROUP BY a.id,\n          b.nombre\n ORDER BY 3 DESC;");
    o.style = FormatOptions::Standard; o.indent = 4;
    check("Sangría de 4", SqlFormatter::format("select a from t where x=1 and y=2", o), "SELECT a\nFROM t\nWHERE x = 1\n    AND y = 2");
    o.indent = 2; o.keywordCase = FormatOptions::Lower;
    check("Minúsculas", SqlFormatter::format("SELECT A FROM T WHERE X=1", o), "select A\nfrom T\nwhere X = 1");
    o.keywordCase = FormatOptions::Upper; o.commaLeading = true;
    check("Coma al inicio", SqlFormatter::format("select a,b,c from t", o), "SELECT\n  a\n  , b\n  , c\nFROM t");
    o.commaLeading = false;
    check("Subconsulta", SqlFormatter::format("select x from (select id x from t where a in (1,2)) where x>0", o),
        "SELECT x\nFROM (\n  SELECT id x\n  FROM t\n  WHERE a IN (1, 2)\n)\nWHERE x > 0");
    check("Insert y update", SqlFormatter::format("insert into t(a,b) values(1,'x');update t set a=1,b=2 where id=3;delete from t where id=3", o),
        "INSERT INTO t (a, b)\nVALUES (1, 'x');\nUPDATE t\nSET\n  a = 1,\n  b = 2\nWHERE id = 3;\nDELETE FROM t\nWHERE id = 3");
    check("Textos y comentarios intactos", SqlFormatter::format("select 'a,b  from' x, \"Mi Col\" -- nota, from\nfrom t", o), "SELECT\n  'a,b  from' x,\n  \"Mi Col\" -- nota, from\nFROM t");
    check("BETWEEN no parte el AND", SqlFormatter::format("select 1 from t where a between 1 and 5 and b=2", o), "SELECT 1\nFROM t\nWHERE a BETWEEN 1 AND 5\n  AND b = 2");
    check("Operadores y negativos", SqlFormatter::format("select -1+2*3,a||b,count(*),t.* from t", o), "SELECT\n  -1 + 2 * 3,\n  a || b,\n  COUNT(*),\n  t.*\nFROM t");
    const QString pl = "create or replace procedure p_demo(p_id number) is\nv_id number;\nv_estado cab.estado%type;\nbegin\nif p_id is null then\nRAISE_APPLICATION_ERROR(-20999,'Debe declarar');\nend if;\nbegin\nselect estado into v_estado from cab where id_pedido=p_id and id_empresa=v_id;\nexception\nwhen no_data_found then\nRAISE_APPLICATION_ERROR(-20999,'No existe');\nend;\nfor r in (select 1 from dual) loop\nnull;\nend loop;\nend;\n/\n";
    o.keywordCase = FormatOptions::Upper;
    check("PL/SQL", SqlFormatter::format(pl, o),
        "CREATE OR REPLACE PROCEDURE p_demo(p_id NUMBER) IS\n  v_id NUMBER;\n  v_estado cab.estado%TYPE;\nBEGIN\n  IF p_id IS NULL THEN\n    RAISE_APPLICATION_ERROR(-20999,'Debe declarar');\n  END IF;\n  BEGIN\n    SELECT estado\n    INTO v_estado\n    FROM cab\n    WHERE id_pedido = p_id\n      AND id_empresa = v_id;\n  EXCEPTION\n    WHEN NO_DATA_FOUND THEN\n      RAISE_APPLICATION_ERROR(-20999,'No existe');\n  END;\n  FOR r IN (SELECT 1 FROM dual) LOOP\n    NULL;\n  END LOOP;\nEND;\n/\n");
    // ---- estructura personalizada ----
    { FormatOptions c; c.style = FormatOptions::Standard; c.kwAloneWhenMany = false; c.contIndent = 7; c.andOrIndent = 0;
      check("Estructura propia: palabras a la izquierda, AND bajo WHERE, columnas alineadas", SqlFormatter::format("select a,b from t where x=1 and y=2", c), "SELECT a,\n       b\nFROM t\nWHERE x = 1\nAND y = 2"); }
    { FormatOptions c; c.joinIndent = 4; c.onNewLine = true;
      check("JOIN sangrado a 4 y ON en línea propia", SqlFormatter::format("select a from t join u on t.id=u.id and t.x=u.x", c), "SELECT a\nFROM t\n    JOIN u\n      ON t.id = u.id\n        AND t.x = u.x"); }
    { FormatOptions c; c.functionCase = FormatOptions::Lower; c.identifierCase = FormatOptions::Lower;
      check("Funciones e identificadores en minúsculas, palabras en mayúsculas", SqlFormatter::format("select NVL(A,B) from MI_TABLA", c), "SELECT nvl(a, b)\nFROM mi_tabla"); }
    { FormatOptions c; c.spaceAfterComma = false; c.spaceInParens = true;
      check("Sin espacio tras coma y con espacios en paréntesis", SqlFormatter::format("select 1 from t where a in (1,2)", c), "SELECT 1\nFROM t\nWHERE a IN ( 1,2 )"); }
    { FormatOptions c; c.alignDeclarations = true; c.alignAssignments = true; c.blankBeforeException = true; c.blankAfterDeclare = true;
      check("PL/SQL: declaraciones y := alineados, líneas en blanco", SqlFormatter::format("declare\nv_id number;\nv_estado_largo varchar2(10);\nbegin\nv_id := 1;\nv_estado_largo := 'x';\nexception\nwhen others then\nnull;\nend;", c),
            "DECLARE\n  v_id           NUMBER;\n  v_estado_largo VARCHAR2(10);\n\nBEGIN\n  v_id           := 1;\n  v_estado_largo := 'x';\n\nEXCEPTION\n  WHEN OTHERS THEN\n    NULL;\nEND;"); }
    { FormatOptions c; c.indent = 3; c.style = FormatOptions::River; c.commaLeading = true; c.andOrIndent = 2; c.alignDeclarations = true; bool ok = false;
      const FormatOptions d = FormatOptions::fromJson(c.toJson(), &ok);
      check("Perfil: ida y vuelta por JSON", QString(ok && d.indent == 3 && d.style == FormatOptions::River && d.commaLeading && d.andOrIndent == 2 && d.alignDeclarations ? "ok" : "mal"), "ok");
      FormatOptions::saveProfile("__prueba__", c); FormatOptions e; const bool found = FormatOptions::loadProfile("__prueba__", e); const bool listed = FormatOptions::profileNames().contains("__prueba__"); FormatOptions::deleteProfile("__prueba__");
      check("Perfil: guardar, listar, cargar y borrar", QString(found && listed && e.indent == 3 && e.commaLeading && !FormatOptions::profileNames().contains("__prueba__") ? "ok" : "mal"), "ok"); }
    { QStringList f; const FormatOptions i = FormatOptions::inferFromExample("select a,\n       b\n  from t\n where x = 1\n   and y = 2\n   and z = 3;", &f);
      check("Deducir de un ejemplo Riachuelo", QString(i.style == FormatOptions::River && i.keywordCase == FormatOptions::Lower ? "ok" : "mal"), "ok"); }
    { const FormatOptions i = FormatOptions::inferFromExample("SELECT estado\nINTO v_estado\nFROM cab_pedido_venta\nWHERE id_pedido = p_id\nAND id_empresa = v_id\nAND x = 1;\nBEGIN\n    IF a THEN\n        b := 1;\n    END IF;\nEND;", nullptr);
      check("Deducir de un ejemplo propio (AND bajo WHERE, sangría 4, mayúsculas)", QString(i.style == FormatOptions::Standard && i.andOrIndent == 0 && i.indent == 4 && i.keywordCase == FormatOptions::Upper && !i.kwAloneWhenMany ? "ok" : "mal"), "ok"); }
    printf("\n%d pruebas, %d fallos\n", total, bad); return bad ? 1 : 0;
}
