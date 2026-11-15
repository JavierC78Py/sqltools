// Valida el formateador rápido de fechas contra el servidor Oracle: miles de fechas aleatorias x varios formatos.
// Para cada fecha compara FastDateFormat con TO_CHAR(fecha, formato) de Oracle (sesión en inglés/AMERICAN).
#include "stdafx.h"
#include "OCI8/Connect.h"
#include "OCI8/ACursor.h"
#include "Common/FastDateFormat.h"

int main (int argc, char** argv)
{
    const char* user = getenv("SQLT_USER"); const char* pass = getenv("SQLT_PASS");
    if (argc < 2 || !user || !pass) return 2;
    const char* formats[] = {
        "DD-MON-RR", "DD-MON-YYYY", "YYYY-MM-DD HH24:MI:SS", "YYYY-MM-DD", "DD/MM/YYYY", "DD/MM/YYYY HH24:MI:SS",
        "DD-MON-RR HH.MI.SS AM", "MM/DD/YYYY HH:MI:SS AM", "Day, Month DD, YYYY", "DY DD Mon YYYY", "DAY DD-MON-YY",
        "YYYY-MM-DD\"T\"HH24:MI:SS", "DDD YYYY Q", "dd-mon-rr", "FMDD-MON-YY", "DD-MON-RR HH24:MI:SSXFF"
    };
    setvbuf(stdout, NULL, _IONBF, 0);
    OCI8::Connect cn; cn.Open(user, pass, argv[1], OCI8::ecmDefault, false, false);
    int totalFail = 0;
    for (const char* f : formats)
    {
        Common::FastDateFormat ff;
        if (!ff.compile(f)) { std::printf("[ -- ] %-28s no soportado por el camino rápido (usa OCI, resultado idéntico)\n", f); continue; }
        std::string q = std::string("select to_char(d,'YYYY-MM-DD HH24:MI:SS'), to_char(d,'") + f + "') from ("
            "select date '1900-01-01' + trunc(dbms_random.value(0, 73000)) + trunc(dbms_random.value(0,86400))/86400 d from dual connect by level <= 20000 "
            "union all select date '2024-02-29' from dual union all select date '1900-03-01' from dual union all select date '2000-12-31' + 23/24 + 59/1440 + 59/86400 from dual "
            "union all select date '2026-01-01' from dual)";
        long n = 0, bad = 0;
        try {
        OCI8::AutoCursor cur(cn, q.c_str(), 1000); cur.Execute();
        while (cur.Fetch())
        {
            std::string iso, want, got; cur.GetString(0, iso); cur.GetString(1, want);
            int y, mo, d, h, mi, s;
            if (std::sscanf(iso.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6) continue;
            ff.format(y, mo, d, h, mi, s, got); ++n;
            if (got != want) { if (++bad <= 3) std::printf("        distinto: fecha=%s  oracle=[%s]  nuestro=[%s]\n", iso.c_str(), want.c_str(), got.c_str()); }
        }
        } catch (const std::exception& e) { std::printf("[ERR ] %-28s %s", f, e.what()); ++totalFail; continue; }
        std::printf("[%s] %-28s %ld fechas, %ld diferencias\n", bad ? "FAIL" : " OK ", f, n, bad);
        totalFail += bad ? 1 : 0;
    }
    cn.Close();
    std::printf("\n%s\n", totalFail ? "HAY DIFERENCIAS" : "Todos los formatos soportados coinciden con Oracle");
    return totalFail ? 3 : 0;
}
