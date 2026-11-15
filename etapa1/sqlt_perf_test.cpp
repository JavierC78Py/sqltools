// Mide filas/s y CPU de usuario de AutoCursor para una consulta dada. Uso: sqlt_perf_test <alias> "<sql>" [solo_fetch=0|1]
#include "stdafx.h"
#include "OCI8/Connect.h"
#include "OCI8/ACursor.h"
#include <chrono>
#include <sys/resource.h>
static double cpu(){ rusage r; getrusage(RUSAGE_SELF,&r); return r.ru_utime.tv_sec + r.ru_utime.tv_usec/1e6; }
int main(int argc,char**argv){
  const char*u=getenv("SQLT_USER"),*p=getenv("SQLT_PASS"); if(argc<3||!u||!p) return 2;
  bool onlyFetch = argc>3 && argv[3][0]=='1';
  OCI8::Connect cn; cn.Open(u,p,argv[1],OCI8::ecmDefault,false,false);
  int pf = getenv("SQLT_PREFETCH") ? atoi(getenv("SQLT_PREFETCH")) : 1000; OCI8::AutoCursor cur(cn, argv[2], pf); cur.Execute();
  auto t0=std::chrono::steady_clock::now(); double c0=cpu(); long n=0; std::string s;
  bool pr=getenv("SQLT_PRINT"); while(cur.Fetch()){ ++n; if(!onlyFetch) for(int i=0;i<cur.GetFieldCount();++i){ cur.GetString(i,s); if(pr) std::printf("%s%s", i?" | ":"", s.c_str()); } if(pr && !onlyFetch) std::printf("\n"); }
  double sec=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count(), c=cpu()-c0;
  std::printf("%8ld filas  %6.2f s real  %6.2f s CPU  %7.0f filas/s  %5.1f us CPU/fila\n",n,sec,c,n/sec,c*1e6/n);
  cn.Close(); return 0; }
