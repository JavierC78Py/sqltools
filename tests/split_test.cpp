// Pruebas del partidor de sentencias. No necesita Oracle ni pantalla.
#include "../ui/SqlSplitter.h"
#include <QCoreApplication>
#include <cstdio>

static int fails = 0;
static void expect (const char* name, const QString& script, const QStringList& want, const QVector<bool>& plsql = {})
{
    auto got = SqlSplitter::split(script);
    bool ok = got.size() == want.size();
    for (int i = 0; ok && i < got.size(); ++i) { ok = got[i].text == want[i]; if (ok && i < plsql.size()) ok = got[i].plsql == plsql[i]; }
    std::printf("[%s] %s\n", ok ? " OK " : "FAIL", name);
    if (!ok) { ++fails; std::printf("   esperadas %lld, obtenidas %lld\n", (long long)want.size(), (long long)got.size()); for (auto& g : got) std::printf("   -> [%s]\n", qPrintable(g.text)); }
}

int main (int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    expect("dos selects", "select 1 from dual;\nselect 2 from dual;", {"select 1 from dual", "select 2 from dual"});
    expect("punto y coma en texto", "select 'a;b' x from dual; select 2 from dual", {"select 'a;b' x from dual", "select 2 from dual"});
    expect("comilla escapada", "select 'it''s; ok' from dual;", {"select 'it''s; ok' from dual"});
    expect("punto y coma en comentario de línea", "select 1 -- uno; dos\nfrom dual;", {"select 1 -- uno; dos\nfrom dual"});
    expect("punto y coma en comentario de bloque", "select /* a; b */ 1 from dual;", {"select /* a; b */ 1 from dual"});
    expect("literal q'[ ]'", "select q'[a;b]' from dual; select 2 from dual", {"select q'[a;b]' from dual", "select 2 from dual"});
    expect("bloque anonimo simple", "begin null; end;\nselect 1 from dual;", {"begin null; end;", "select 1 from dual"}, {true, false});
    expect("bloque con if/loop/case",
           "begin\n  if x then\n    for i in 1..3 loop\n      null;\n    end loop;\n  elsif y then\n    case when a then null; end case;\n  end if;\n  v := case when 1=1 then 1 else 2 end;\nend;\nselect 1 from dual;",
           {"begin\n  if x then\n    for i in 1..3 loop\n      null;\n    end loop;\n  elsif y then\n    case when a then null; end case;\n  end if;\n  v := case when 1=1 then 1 else 2 end;\nend;", "select 1 from dual"}, {true, false});
    expect("declare + begin", "declare v number; begin v := 1; end;\nselect 2 from dual;", {"declare v number; begin v := 1; end;", "select 2 from dual"}, {true, false});
    expect("bloques anidados", "begin begin null; end; null; end;\nselect 1 from dual;", {"begin begin null; end; null; end;", "select 1 from dual"});
    expect("create procedure con /",
           "create or replace procedure p is\nbegin\n  null;\nend;\n/\nselect 1 from dual;",
           {"create or replace procedure p is\nbegin\n  null;\nend;", "select 1 from dual"}, {true, false});
    expect("create package y body",
           "create or replace package pk as\n  procedure a;\nend pk;\n/\ncreate or replace package body pk as\n  procedure a is begin null; end;\nend pk;\n/",
           {"create or replace package pk as\n  procedure a;\nend pk;", "create or replace package body pk as\n  procedure a is begin null; end;\nend pk;"}, {true, true});
    expect("create table es SQL suelto", "create table t (a number);\nselect 1 from dual;", {"create table t (a number)", "select 1 from dual"}, {false, false});
    expect("create trigger", "create or replace trigger tg before insert on t for each row\nbegin\n :new.a := 1;\nend;\n/", {"create or replace trigger tg before insert on t for each row\nbegin\n :new.a := 1;\nend;"}, {true});
    expect("sin punto y coma final", "select 1 from dual", {"select 1 from dual"});
    expect("comentarios al inicio", "-- hola\nselect 1 from dual;\n/* x */ select 2 from dual;", {"select 1 from dual", "select 2 from dual"});
    expect("barra sola cierra sql", "select 1 from dual\n/\nselect 2 from dual\n/", {"select 1 from dual", "select 2 from dual"});
    expect("vacio", "  \n ; \n", {});

    // statementAt
    auto v = SqlSplitter::split("select 1 from dual;\n\nselect 2 from dual;");
    int a = SqlSplitter::statementAt(v, 5), b = SqlSplitter::statementAt(v, 20), c = SqlSplitter::statementAt(v, 21), d = SqlSplitter::statementAt(v, 19);
    bool ok = (a == 0 && b == 0 && c == 1 && d == 0);
    std::printf("[%s] statementAt (dentro/blancos/siguiente)\n", ok ? " OK " : "FAIL"); if (!ok) ++fails;
    std::printf("\n%s\n", fails ? "HAY FALLOS" : "Todas las pruebas del partidor pasaron");
    return fails ? 1 : 0;
}
