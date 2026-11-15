#pragma once
#include <string>
namespace Common
{
    // Carga libclntsh.dylib (Oracle Instant Client). Orden de búsqueda:
    //  1) variable SQLT_OCI_LIB (ruta completa a libclntsh.dylib)
    //  2) carpeta indicada por ORACLE_HOME / OCI_LIB_DIR
    //  3) "libclntsh.dylib" por la ruta estándar del sistema / rpath del ejecutable
    void* ociLoadLibrary (std::string* error = nullptr);
    void* ociProc (void* lib, const char* name);
    // Versión del cliente, p.ej. "19.16.0.0.0" (vacío si no se pudo obtener)
    std::string ociClientVersion (void* lib);
}
