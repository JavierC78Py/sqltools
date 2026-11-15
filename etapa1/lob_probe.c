/* Sonda: lee CLOB/BLOB con OCI "a pelo" para saber si NULL/vacío fallan en el servidor/cliente o solo en la capa portada. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <oci.h>
static void chk(OCIError*e,sword rc,const char*w){ if(rc==OCI_SUCCESS||rc==OCI_SUCCESS_WITH_INFO)return; text m[512]; sb4 c; OCIErrorGet(e,1,0,&c,m,sizeof m,OCI_HTYPE_ERROR); printf("   ERROR %s: %s",w,(char*)m); }
static void probe(OCIEnv*env,OCIError*err,OCISvcCtx*svc,const char*sql,ub2 typ){
  OCIStmt*st=0; OCIDefine*d=0; OCILobLocator*lob=0; sb2 ind=0;
  printf("%s\n",sql);
  OCIHandleAlloc(env,(void**)&st,OCI_HTYPE_STMT,0,0);
  OCIDescriptorAlloc(env,(void**)&lob,OCI_DTYPE_LOB,0,0);
  chk(err,OCIStmtPrepare(st,err,(text*)sql,strlen(sql),OCI_NTV_SYNTAX,OCI_DEFAULT),"prepare");
  chk(err,OCIDefineByPos(st,&d,err,1,&lob,sizeof lob,typ,&ind,0,0,OCI_DEFAULT),"define");
  sword rc=OCIStmtExecute(svc,st,err,1,0,0,0,OCI_DEFAULT); chk(err,rc,"execute");
  if(rc==OCI_SUCCESS||rc==OCI_SUCCESS_WITH_INFO){ ub4 len=0; printf("   ind=%d ",(int)ind); if(ind!=-1){ sword r2=OCILobGetLength(svc,err,lob,&len); if(r2) chk(err,r2,"getlength"); else printf("longitud=%u",len);} printf("\n"); }
}
int main(int c,char**v){
  OCIEnv*env=0; OCIError*err=0; OCISvcCtx*svc=0;
  OCIEnvCreate(&env,OCI_THREADED|OCI_OBJECT,0,0,0,0,0,0); OCIHandleAlloc(env,(void**)&err,OCI_HTYPE_ERROR,0,0);
  const char*u=getenv("SQLT_USER"),*p=getenv("SQLT_PASS");
  if(OCILogon2(env,err,&svc,(text*)u,strlen(u),(text*)p,strlen(p),(text*)v[1],strlen(v[1]),OCI_DEFAULT)){chk(err,-1,"logon");return 1;}
  probe(env,err,svc,"select cast(null as clob) from dual",SQLT_CLOB);
  probe(env,err,svc,"select empty_clob() from dual",SQLT_CLOB);
  probe(env,err,svc,"select to_clob('abc') from dual",SQLT_CLOB);
  probe(env,err,svc,"select cast(null as blob) from dual",SQLT_BLOB);
  return 0; }
