#include "OciLoader.h"
#include <dlfcn.h>
#include <cstdlib>
#include <cstdio>
namespace Common
{
void* ociLoadLibrary (std::string* error)
{
    std::string tried;
    auto attempt = [&](const std::string& path) -> void* {
        void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!h) { const char* e = dlerror(); tried += path + ": " + (e ? e : "?") + "\n"; }
        return h;
    };
    if (const char* p = getenv("SQLT_OCI_LIB")) if (void* h = attempt(p)) return h;
    for (const char* var : { "OCI_LIB_DIR", "ORACLE_HOME" })
        if (const char* d = getenv(var)) if (void* h = attempt(std::string(d) + "/libclntsh.dylib")) return h;
    if (void* h = attempt("@rpath/libclntsh.dylib.19.1")) return h;                          // app empaquetada: la librería va dentro del .app
    if (void* h = attempt("@executable_path/../Frameworks/libclntsh.dylib.19.1")) return h;
    if (void* h = attempt("libclntsh.dylib")) return h;
    if (error) *error = tried;
    return nullptr;
}

void* ociProc (void* lib, const char* name) { return lib ? dlsym(lib, name) : nullptr; }

std::string ociClientVersion (void* lib)
{
    typedef void (*OCIClientVersion_t)(int*, int*, int*, int*, int*);
    auto fn = (OCIClientVersion_t)ociProc(lib, "OCIClientVersion");
    if (!fn) return std::string();
    int a = 0, b = 0, c = 0, d = 0, e = 0;
    fn(&a, &b, &c, &d, &e);
    char buf[64]; std::snprintf(buf, sizeof buf, "%d.%d.%d.%d.%d", a, b, c, d, e);
    return buf;
}
}
