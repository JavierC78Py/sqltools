// Pruebas de variables de enlace (bind), PL/SQL con parámetros OUT, cancelación (Break) y NLS de sesión.
// Credenciales por variables de entorno: SQLT_USER, SQLT_PASS. Alias como argumento; TNS_ADMIN = carpeta del wallet.
#include "stdafx.h"
#include "OCI8/Connect.h"
#include "OCI8/Statement.h"
#include "OCI8/ACursor.h"
#include <chrono>
#include <thread>

static int g_ok = 0, g_fail = 0;
static void check (const char* label, const std::string& got, const std::string& expect)
{
    if (got == expect) { ++g_ok; std::printf("[ OK ] %-36s %s\n", label, got.c_str()); }
    else { ++g_fail; std::printf("[FAIL] %-36s obtenido: [%s]\n       %-36s esperado: [%s]\n", label, got.c_str(), "", expect.c_str()); }
}
static std::string S (OCI8::Variable& v) { std::string s; v.GetString(s, "<NULL>"); return s; }

int main (int argc, char** argv)
{
    const char* user = getenv("SQLT_USER"); const char* pass = getenv("SQLT_PASS");
    if (argc < 2 || !user || !pass) return 2;
    try
    {
        OCI8::Connect cn;
        cn.Open(user, pass, argv[1], OCI8::ecmDefault, false, false);
        std::printf("Conectado a %s\n\n", cn.GetVersionStr());

        { // 1. bind de texto con acentos, emoji y CJK
            OCI8::Statement st(cn, "select :t from dual");
            OCI8::StringVar in("Ñandú 😀 漢字"), out(200);
            st.Bind(":t", in); st.Define(1, out); st.Execute(); st.Fetch();
            check("bind texto (ñ, emoji, CJK)", S(out), "Ñandú 😀 漢字");
        }
        { // 2. bind numérico entero y decimal
            OCI8::Statement st(cn, "select :n + 1, :f * 2 from dual");
            OCI8::NumberVar n(cn, 41), f(cn, 1.25), o1(cn), o2(cn);
            st.Bind(":n", n); st.Bind(":f", f); st.Define(1, o1); st.Define(2, o2); st.Execute(); st.Fetch();
            check("bind NUMBER entero (41+1)", S(o1), "42");
            check("bind NUMBER decimal (1.25*2)", S(o2), "2.5");
        }
        { // 3. el mismo bind repetido y bind en WHERE
            OCI8::Statement st(cn, "select count(*) from dual where dummy = :x and :x = 'X'");
            OCI8::StringVar x("X"); OCI8::NumberVar cnt(cn);
            st.Bind(":x", x); st.Define(1, cnt); st.Execute(); st.Fetch();
            check("bind repetido en WHERE", S(cnt), "1");
        }
        { // 4. fecha vía to_date(:s)
            OCI8::Statement st(cn, "select to_char(to_date(:s,'YYYY-MM-DD HH24:MI:SS') + 1, 'YYYY-MM-DD HH24:MI:SS') from dual");
            OCI8::StringVar s("2026-12-31 23:59:59"), out(50);
            st.Bind(":s", s); st.Define(1, out); st.Execute(); st.Fetch();
            check("bind fecha (to_date +1 día)", S(out), "2027-01-01 23:59:59");
        }
        { // 5. bind NULL (cadena vacía)
            OCI8::Statement st(cn, "select nvl(:v, 'era nulo') from dual");
            OCI8::StringVar v(10), out(50);
            st.Bind(":v", v); st.Define(1, out); st.Execute(); st.Fetch();
            check("bind NULL (nvl)", S(out), "era nulo");
        }
        { // 6. re-ejecutar la misma sentencia con otro valor
            OCI8::Statement st(cn, "select :a * 10 from dual");
            OCI8::NumberVar a(cn, 1), out(cn);
            st.Bind(":a", a); st.Define(1, out);
            st.Execute(); st.Fetch(); std::string r1 = S(out);
            a.Assign(7); st.Execute(); st.Fetch(); std::string r2 = S(out);
            check("re-ejecutar con otro bind", r1 + "," + r2, "10,70");
        }
        { // 7. bloque PL/SQL con parámetro de salida
            OCI8::Statement st(cn, "begin :o := upper(:i) || ' ' || length(:i); end;");
            OCI8::StringVar i("áéí abc"), o(100);
            st.Bind(":i", i); st.Bind(":o", o); st.Execute(1);
            check("PL/SQL OUT (upper + length)", S(o), "ÁÉÍ ABC 7");
        }
        { // 8. error ORA con bind inválido
            std::string msg;
            try { OCI8::Statement st(cn, "select to_number(:x) from dual"); OCI8::StringVar x("abc"), o(20);
                  st.Bind(":x", x); st.Define(1, o); st.Execute(); st.Fetch(); }
            catch (const std::exception& e) { msg = e.what(); }
            check("error ORA-01722 con bind", msg.substr(0, 9), "ORA-01722");
        }

        // 9. Cancelar una consulta larga (Break) desde otro hilo
        {
            auto bh = cn.CreateBreakHandler();
            std::thread killer([&]{ std::this_thread::sleep_for(std::chrono::seconds(2)); bh->Break(); });
            auto t0 = std::chrono::steady_clock::now();
            std::string msg;
            try { cn.ExecuteStatement("begin dbms_session.sleep(30); end;"); }
            catch (const std::exception& e) { msg = e.what(); }
            double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            killer.join();
            std::printf("       (cancelación tardó %.1f s)\n", sec);
            check("Break cancela (ORA-01013)", msg.substr(0, 9), "ORA-01013");
            // la conexión debe seguir usable
            OCI8::AutoCursor cur(cn, "select 'sigue viva' from dual"); cur.Execute(); cur.Fetch();
            std::string s; cur.GetString(0, s);
            check("conexión usable tras cancelar", s, "sigue viva");
        }

        // 10. NLS de sesión: formatos de fecha y timestamp
        {
            cn.SetNlsDateFormat("YYYY-MM-DD HH24:MI:SS");
            cn.AlterSessionNlsParams();
            OCI8::AutoCursor cur(cn, "select to_date('2026-03-15 13:45:09','YYYY-MM-DD HH24:MI:SS') from dual");
            cur.Execute(); cur.Fetch(); std::string s; cur.GetString(0, s);
            check("NLS_DATE_FORMAT aplicado", s, "2026-03-15 13:45:09");
        }
        {
            cn.SetNlsTimestampFormat("YYYY-MM-DD HH24:MI:SS.FF3");
            OCI8::AutoCursor cur(cn, "select timestamp '2026-03-15 13:45:09.123456' from dual");
            cur.Execute(); cur.Fetch(); std::string s; cur.GetString(0, s);
            std::printf("[INFO] TIMESTAMP con formato propio FF3: %s\n", s.c_str());
        }

        std::printf("\nResumen: %d OK, %d FAIL\n", g_ok, g_fail);
        cn.Close();
    }
    catch (const std::exception& e) { std::fprintf(stderr, "ERROR fatal: %s\n", e.what()); return 1; }
    return g_fail ? 3 : 0;
}
