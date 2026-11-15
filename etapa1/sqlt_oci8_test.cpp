// Prueba de la capa OCI8 portada: conecta con OCI8::Connect y lee filas con OCI8::AutoCursor.
// Credenciales por variables de entorno: SQLT_USER, SQLT_PASS. Alias TNS como argumento; TNS_ADMIN = carpeta del wallet.
#include "stdafx.h"
#include "OCI8/Connect.h"
#include "OCI8/ACursor.h"

int main (int argc, char** argv)
{
    const char* user = getenv("SQLT_USER");
    const char* pass = getenv("SQLT_PASS");
    if (argc < 2 || !user || !pass) { std::fprintf(stderr, "uso: SQLT_USER=.. SQLT_PASS=.. TNS_ADMIN=.. %s <alias>\n", argv[0]); return 2; }
    try
    {
        OCI8::Connect cn;
        cn.Open(user, pass, argv[1], OCI8::ecmDefault, false, false);
        std::printf("Conectado. Servidor: %s\n", cn.GetVersionStr());
        std::printf("Cliente OCI detectado (enum): %d\n", (int)OCI8::Connect::GetClientVersion());

        OCI8::AutoCursor cur(cn, "select level n, 'Ñandú áéíóú ' || level || ' ✓' texto, sysdate d, 12345.678 num from dual connect by level <= 3");
        cur.Execute();
        for (int i = 0; i < cur.GetFieldCount(); ++i) std::printf("%s%s", i ? " | " : "", cur.GetFieldName(i).c_str());
        std::printf("\n");
        int rows = 0;
        while (cur.Fetch())
        {
            ++rows;
            for (int i = 0; i < cur.GetFieldCount(); ++i)
            {
                std::string s; cur.GetString(i, s);
                std::printf("%s%s", i ? " | " : "", s.c_str());
            }
            std::printf("\n");
        }
        std::printf("%d filas\n", rows);
        cn.Close();
    }
    catch (const std::exception& e) { std::fprintf(stderr, "ERROR: %s\n", e.what()); return 1; }
    return 0;
}
