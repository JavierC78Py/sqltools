// Pruebas de las variables de enlace (:nombre) y de su sustitución por literales, y del historial. Sin Oracle.
#include "../ui/BindParser.h"
#include "../ui/HistoryStore.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdio>

static int fails = 0;
static void check (const char* name, bool ok, const QString& extra = QString()) { std::printf("[%s] %s%s\n", ok ? " OK " : "FAIL", name, ok || extra.isEmpty() ? "" : qPrintable("   -> " + extra)); if (!ok) ++fails; }

int main (int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    check("detecta :a y :b una vez cada una", BindParser::names("select * from t where a = :a and b = :b or c = :a") == QStringList({"a", "b"}));
    check("ignora textos, comentarios y q'[...]'", BindParser::names("select ':x' , q'[:y]' from t -- :z\n where /* :w */ a = :ok") == QStringList({"ok"}), BindParser::names("select ':x' , q'[:y]' from t -- :z\n where /* :w */ a = :ok").join(","));
    check("ignora := y ::", BindParser::names("begin v := 1; x := y::z; end;").isEmpty());
    check("binds posicionales :1 :2", BindParser::names("insert into t values (:1, :2)") == QStringList({"1", "2"}));
    check("identificador entre comillas no es bind", BindParser::names("select \":x\" from t").isEmpty());

    QMap<QString, BindValue> v;
    v["N"] = {"Auto", "42"}; v["T"] = {"Auto", "O'Brien"}; v["D"] = {"Date", "2026-03-15 13:45:09"}; v["D2"] = {"Date", "2026-03-15"}; v["Z"] = {"NULL", "x"};
    v["S"] = {"String", "123"}; v["E"] = {"Auto", ""}; v["BAD"] = {"Number", "abc"}; v["BD"] = {"Date", "15/03/2026"}; v["F"] = {"Auto", "-1.5e3"};
    QStringList w;
    check("Auto numérico -> número; texto -> comillas con '' escapada", BindParser::substitute("a = :n and b = :t", v, &w) == "a = 42 and b = 'O''Brien'");
    check("Date con y sin hora", BindParser::substitute(":d / :d2", v) == "to_date('2026-03-15 13:45:09', 'YYYY-MM-DD HH24:MI:SS') / to_date('2026-03-15', 'YYYY-MM-DD')");
    check("NULL explícito, vacío y String numérico", BindParser::substitute(":z|:e|:s", v) == "NULL|NULL|'123'");
    check("notación científica es número", BindParser::substitute(":f", v) == "-1.5e3");
    QStringList w2;
    check("valores inválidos dan NULL con aviso", BindParser::substitute(":bad :bd", v, &w2) == "NULL NULL" && w2.size() == 2, w2.join(" | "));
    check("no toca textos ni comentarios", BindParser::substitute("select ':n' x, :n y -- :n\n from t", v) == "select ':n' x, 42 y -- :n\n from t");
    check("variable sin valor definido = NULL", BindParser::substitute("a = :nueva", v) == "a = NULL");
    check("los nombres no distinguen mayúsculas", BindParser::substitute("a = :N and b = :n", v) == "a = 42 and b = 42");

    QTemporaryDir dir; const QString f = dir.path() + "/sub/history.json";
    { HistoryStore h(f, 3);
      for (int i = 1; i <= 5; ++i) h.add({QDateTime::currentDateTime(), QString("select %1 from dual").arg(i), 0.1 * i, i, i == 4 ? "ORA-00942" : QString(), "Km"});
      check("el historial conserva solo las últimas 3", h.entries().size() == 3 && h.entries().first().sql == "select 3 from dual" && h.entries().last().sql == "select 5 from dual"); }
    { HistoryStore h(f, 3);
      check("el historial se recupera del disco (con error y conexión)", h.entries().size() == 3 && h.entries()[1].error == "ORA-00942" && h.entries()[1].connection == "Km" && h.entries()[2].rows == 5);
      h.clear(); }
    { HistoryStore h(f, 3); check("clear() vacía también el archivo", h.entries().isEmpty()); }
    std::printf("\n%s\n", fails ? "HAY FALLOS" : "Todas las pruebas de binds e historial pasaron");
    return fails ? 1 : 0;
}
