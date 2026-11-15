/* Línea base: 200000 filas con OCI "a pelo" (array fetch de 1000), sin la capa de SQLTools. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <oci.h>
#define ARR 1000
static double now(){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec/1e9; }
int main(int c,char**v){
  OCIEnv*env=0; OCIError*err=0; OCISvcCtx*svc=0; OCIStmt*st=0; OCIDefine*d1=0,*d2=0;
  OCIEnvCreate(&env,OCI_THREADED|OCI_OBJECT,0,0,0,0,0,0); OCIHandleAlloc(env,(void**)&err,OCI_HTYPE_ERROR,0,0);
  const char*u=getenv("SQLT_USER"),*p=getenv("SQLT_PASS");
  if(OCILogon2(env,err,&svc,(text*)u,strlen(u),(text*)p,strlen(p),(text*)v[1],strlen(v[1]),OCI_DEFAULT)){puts("logon fallo");return 1;}
  const char*sql="select level, 'fila numero ' || level from dual connect by level <= 200000";
  static int ids[ARR]; static char txt[ARR][32];
  OCIHandleAlloc(env,(void**)&st,OCI_HTYPE_STMT,0,0);
  ub4 pf=ARR; OCIAttrSet(st,OCI_HTYPE_STMT,&pf,0,OCI_ATTR_PREFETCH_ROWS,err);
  OCIStmtPrepare(st,err,(text*)sql,strlen(sql),OCI_NTV_SYNTAX,OCI_DEFAULT);
  OCIDefineByPos(st,&d1,err,1,ids,sizeof(int),SQLT_INT,0,0,0,OCI_DEFAULT);
  OCIDefineByPos(st,&d2,err,2,txt,32,SQLT_STR,0,0,0,OCI_DEFAULT);
  double t0=now(); long n=0; sword rc=OCIStmtExecute(svc,st,err,ARR,0,0,0,OCI_DEFAULT);
  while(1){ ub4 rows=0; OCIAttrGet(st,OCI_HTYPE_STMT,&rows,0,OCI_ATTR_ROW_COUNT,err); n=rows; if(rc==OCI_NO_DATA)break; if(rc&&rc!=OCI_SUCCESS_WITH_INFO)break; rc=OCIStmtFetch2(st,err,ARR,OCI_FETCH_NEXT,0,OCI_DEFAULT); }
  double s=now()-t0; printf("[BASE OCI a pelo] %ld filas en %.2f s = %.0f filas/s\n",n,s,n/s); return 0; }
