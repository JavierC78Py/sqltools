// Batería de pruebas de tipos de datos sobre la capa OCI8 portada. Solo consultas de lectura (SELECT ... FROM dual).
// Credenciales por variables de entorno: SQLT_USER, SQLT_PASS. Alias TNS como argumento; TNS_ADMIN = carpeta del wallet.
#include "stdafx.h"
#include "OCI8/Connect.h"
#include "OCI8/ACursor.h"
#include <vector>\n#include <chrono>

struct Case { const char* label; const char* sql; const char* expect; /* texto esperado de la 1ª fila, columnas con " | "; "" = solo mostrar */ };

static int g_pass = 0, g_fail = 0, g_info = 0;

static void run (OCI8::Connect& cn, const Case& c)
{
    std::string got;
    try
    {
        OCI8::AutoCursor cur(cn, c.sql);
        cur.Execute();
        int rows = 0;
        while (cur.Fetch())
        {
            if (rows++ == 0)
                for (int i = 0; i < cur.GetFieldCount(); ++i)
                {
                    std::string s; cur.GetString(i, s);
                    if (i) got += " | ";
                    got += cur.IsNull(i) ? "<NULL>" : s;
                }
        }
        if (rows == 0) got = "<sin filas>";
    }
    catch (const std::exception& e) { got = std::string("EXCEPCION: ") + e.what(); }

    std::string shown = got.size() > 150 ? got.substr(0, 150) + "…(+" + std::to_string(got.size() - 150) + " bytes)" : got;
    if (!*c.expect) { ++g_info; std::printf("[INFO] %-34s %s\n", c.label, shown.c_str()); }
    else if (got == c.expect) { ++g_pass; std::printf("[ OK ] %-34s %s\n", c.label, shown.c_str()); }
    else { ++g_fail; std::printf("[FAIL] %-34s obtenido: %s\n       %-34s esperado: %s\n", c.label, shown.c_str(), "", c.expect); }
}

int main (int argc, char** argv)
{
    const char* user = getenv("SQLT_USER"); const char* pass = getenv("SQLT_PASS");
    if (argc < 2 || !user || !pass) { std::fprintf(stderr, "uso: SQLT_USER=.. SQLT_PASS=.. TNS_ADMIN=.. %s <alias>\n", argv[0]); return 2; }
    try
    {
        OCI8::Connect cn;
        cn.Open(user, pass, argv[1], OCI8::ecmDefault, false, false);
        std::printf("Conectado a %s (cliente enum %d)\n\n", cn.GetVersionStr(), (int)OCI8::Connect::GetClientVersion());

        std::vector<Case> cases = {
            // --- Numéricos
            {"NUMBER entero",            "select 42 from dual", "42"},
            {"NUMBER negativo",          "select -7 from dual", "-7"},
            {"NUMBER decimal",           "select 3.14159 from dual", "3.14159"},
            {"NUMBER <1 (.5, formato Oracle TM9)", "select 0.5 from dual", ".5"},
            {"NUMBER 38 dígitos",        "select 12345678901234567890123456789012345678 from dual", "12345678901234567890123456789012345678"},
            {"NUMBER muy pequeño",       "select 1e-10 from dual", ""},
            {"NUMBER muy grande 1e100",  "select 1e100 from dual", ""},
            {"NUMBER(10,2)",             "select cast(1234.5 as number(10,2)) from dual", "1234.5"},
            {"BINARY_FLOAT",             "select cast(1.5 as binary_float) from dual", "1.5"},
            {"BINARY_DOUBLE",            "select cast(2.25 as binary_double) from dual", "2.25"},
            {"BINARY_DOUBLE NaN",        "select binary_double_nan from dual", ""},
            {"BINARY_DOUBLE +Inf",       "select binary_double_infinity from dual", ""},
            {"NUMBER NULL",              "select cast(null as number) from dual", "<NULL>"},
            // --- Texto
            {"VARCHAR2 ASCII",           "select 'hola' from dual", "hola"},
            {"VARCHAR2 acentos/ñ",       "select 'Ñandú áéíóú üñ' from dual", "Ñandú áéíóú üñ"},
            {"VARCHAR2 emoji (UTF-16 sub)","select 'ok 😀 fin' from dual", "ok 😀 fin"},
            {"VARCHAR2 chino/árabe",     "select '漢字 العربية' from dual", "漢字 العربية"},
            {"CHAR(10)",                 "select cast('ab' as char(10)) from dual", "ab        "},
            {"NVARCHAR2",                "select cast('Ñandú' as nvarchar2(20)) from dual", "Ñandú"},
            {"NCHAR",                    "select cast('ñ' as nchar(3)) from dual", ""},
            {"VARCHAR2 vacío (NULL)",    "select '' from dual", "<NULL>"},
            {"VARCHAR2 4000 bytes",      "select rpad('x', 4000, 'x') from dual", ""},
            {"LONG literal",             "select to_char(sysdate,'YYYY') from dual", ""},
            // --- Fecha / hora
            {"DATE",                     "select to_date('2026-03-15 13:45:09','YYYY-MM-DD HH24:MI:SS') from dual", ""},
            {"DATE to_char",             "select to_char(to_date('2026-03-15 13:45:09','YYYY-MM-DD HH24:MI:SS'),'YYYY-MM-DD HH24:MI:SS') from dual", "2026-03-15 13:45:09"},
            {"TIMESTAMP(6)",             "select cast(timestamp '2026-03-15 13:45:09.123456' as timestamp(6)) from dual", ""},
            {"TIMESTAMP(9)",             "select cast(timestamp '2026-03-15 13:45:09.123456789' as timestamp(9)) from dual", ""},
            {"TIMESTAMP WITH TIME ZONE", "select timestamp '2026-03-15 13:45:09.5 -03:00' from dual", ""},
            {"TIMESTAMP WITH LOCAL TZ",  "select cast(timestamp '2026-03-15 13:45:09' as timestamp with local time zone) from dual", ""},
            {"INTERVAL YEAR TO MONTH",   "select interval '3-5' year to month from dual", ""},
            {"INTERVAL DAY TO SECOND",   "select interval '2 03:04:05.678' day to second from dual", ""},
            {"DATE NULL",                "select cast(null as date) from dual", "<NULL>"},
            {"DATE año 0001 / 9999",     "select to_char(date '0001-01-01','YYYY') || '/' || to_char(date '9999-12-31','YYYY') from dual", "0001/9999"},
            // --- Binarios y objetos grandes
            {"RAW",                      "select hextoraw('DEADBEEF') from dual", ""},
            {"CLOB corto",               "select to_clob('texto clob Ñ') from dual", "texto clob Ñ"},
            {"CLOB 100 KB",              "select to_clob(rpad('c', 4000, 'c')) || to_clob(rpad('c', 4000, 'c')) from dual", ""},
            {"CLOB vacío",               "select empty_clob() from dual", ""},
            {"CLOB NULL",                "select to_clob(null) from dual", "<NULL>"},
            {"BLOB",                     "select to_blob(hextoraw('0102030405')) from dual", ""},
            {"BLOB NULL",                "select to_blob(null) from dual", "<NULL>"},
            {"NCLOB",                    "select to_nclob('nclob Ñandú') from dual", ""},
            // --- Otros
            {"ROWID",                    "select rowid from dual", ""},
            {"UROWID",                   "select cast(rowid as urowid) from dual", ""},
            {"XMLTYPE",                  "select xmltype('<a><b>1</b></a>') from dual", ""},
            {"JSON (texto)",             "select json_object('k' value 'v', 'n' value 1) from dual", "{\"k\":\"v\",\"n\":1}"},
            {"Colección (ODCINUMBERLIST)","select sys.odcinumberlist(1,2,3) from dual", ""},
            {"Colección varchar",        "select sys.odcivarchar2list('a','b') from dual", ""},
            {"Tipo objeto",              "select sys.anydata.convertnumber(5) from dual", ""},
            {"REF CURSOR anidado",       "select cursor(select 1 x from dual) from dual", ""},
            // --- Varias columnas / filas / errores
            {"NLS_TIMESTAMP_FORMAT sesión", "select value from nls_session_parameters where parameter='NLS_TIMESTAMP_FORMAT'", ""},
            {"NLS_DATE_FORMAT sesión",      "select value from nls_session_parameters where parameter='NLS_DATE_FORMAT'", ""},
            {"Multi-columna mixta",       "select 1, 'a', date '2026-01-02', 2.5, null from dual", ""},
            {"Alias con ñ y espacios",   "select 1 as \"Año Ñ\" from dual", "1"},
            {"ERROR ORA-00942",          "select * from tabla_que_no_existe_xyz", ""},
            {"ERROR sintaxis ORA-00923", "select 1 frm dual", ""},
            {"ERROR división por cero",  "select 1/0 from dual", ""},
            {"Nombre columna largo",     "select 1 as \"columna_con_un_nombre_bastante_largo_123456\" from dual", "1"},
        };
        for (auto& c : cases) run(cn, c);
        {   // Rendimiento: filas por segundo con AutoCursor (incluye latencia de red hasta la nube de Chile)
            auto t0 = std::chrono::steady_clock::now();
            OCI8::AutoCursor cur(cn, "select level, 'fila numero ' || level, sysdate + level / 1440, level * 1.5 from dual connect by level <= 200000", 1000);
            cur.Execute(); long n = 0; std::string s;
            while (cur.Fetch()) { ++n; for (int i = 0; i < cur.GetFieldCount(); ++i) cur.GetString(i, s); }
            double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::printf("\n[PERF] %ld filas x 4 columnas en %.2f s = %.0f filas/s (prefetch 1000)\n", n, sec, n / sec);
        }
        std::printf("\nResumen: %d OK, %d FAIL, %d informativos (revisar a ojo)\n", g_pass, g_fail, g_info);
        cn.Close();
    }
    catch (const std::exception& e) { std::fprintf(stderr, "ERROR fatal: %s\n", e.what()); return 1; }
    return g_fail ? 3 : 0;
}
