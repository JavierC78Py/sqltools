/* Prueba mínima OCI: conecta y ejecuta SELECT. Credenciales por variables de entorno (nunca en el código).
   Uso: SQLT_USER=... SQLT_PASS=... TNS_ADMIN=<carpeta wallet> ./oci_test <alias_tns> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <oci.h>

static int chk(OCIError *err, sword rc, const char *what) {
    if (rc == OCI_SUCCESS || rc == OCI_SUCCESS_WITH_INFO) return 0;
    text msg[512]; sb4 code = 0;
    OCIErrorGet(err, 1, NULL, &code, msg, sizeof msg, OCI_HTYPE_ERROR);
    fprintf(stderr, "ERROR en %s: %s\n", what, (char *)msg);
    return 1;
}

int main(int argc, char **argv) {
    const char *user = getenv("SQLT_USER"), *pass = getenv("SQLT_PASS");
    if (argc < 2 || !user || !pass) { fprintf(stderr, "faltan alias o variables SQLT_USER/SQLT_PASS\n"); return 2; }
    OCIEnv *env = NULL; OCIError *err = NULL; OCISvcCtx *svc = NULL; OCIStmt *stmt = NULL; OCIDefine *def = NULL;
    if (OCIEnvCreate(&env, OCI_DEFAULT, NULL, NULL, NULL, NULL, 0, NULL)) { fprintf(stderr, "OCIEnvCreate fallo\n"); return 1; }
    OCIHandleAlloc(env, (void **)&err, OCI_HTYPE_ERROR, 0, NULL);
    if (chk(err, OCILogon2(env, err, &svc, (text *)user, strlen(user), (text *)pass, strlen(pass),
                           (text *)argv[1], strlen(argv[1]), OCI_DEFAULT), "OCILogon2")) return 1;
    puts("Conexion OK");
    const char *sql = "select user || ' @ ' || sys_context('USERENV','DB_NAME') || ' | ' || (select banner from v$version where rownum=1) from dual";
    char out[512] = {0}; sb2 ind = 0;
    OCIHandleAlloc(env, (void **)&stmt, OCI_HTYPE_STMT, 0, NULL);
    if (chk(err, OCIStmtPrepare(stmt, err, (text *)sql, strlen(sql), OCI_NTV_SYNTAX, OCI_DEFAULT), "prepare")) return 1;
    OCIDefineByPos(stmt, &def, err, 1, out, sizeof out, SQLT_STR, &ind, NULL, NULL, OCI_DEFAULT);
    if (chk(err, OCIStmtExecute(svc, stmt, err, 1, 0, NULL, NULL, OCI_DEFAULT), "execute")) return 1;
    printf("Resultado: %s\n", out);
    OCILogoff(svc, err);
    return 0;
}
