// Tipos, macros y funciones de Windows que el código original usa, reimplementados de forma portable.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <cassert>
#include <stdexcept>
#include <chrono>

typedef uint32_t DWORD;
typedef const char* LPCSTR;
typedef const char* LPCTSTR;
#define LPSCSTR static const char*
#define _T(x) x
#define TRACE(...) ((void)0)
#ifndef MB_OK
#define MB_OK 0
#define MB_ICONEXCLAMATION 0
#define MB_ICONSTOP 0
#endif

// AfxMessageBox: por ahora solo escribe a stderr (la UI de Qt lo reemplazará).
namespace Common { std::string str (const std::u16string&); }
inline int AfxMessageBox (const std::u16string& msg, int = 0, int = 0)
{
    std::fprintf(stderr, "%s\n", Common::str(msg).c_str());
    return 0;
}

// Id del hilo actual (reemplaza GetCurrentThreadId()).
inline DWORD GetCurrentThreadId ()
{
    return (DWORD)std::hash<std::thread::id>()(std::this_thread::get_id());
}

namespace Common
{
    inline void TraceStackOnThrow () {}
    inline void DefaultHandler (const std::exception& x, const char* file, int line)
    {
        std::fprintf(stderr, "Error: %s (%s:%d)\n", x.what(), file, line);
    }
    inline std::string getDllVersionProperty (LPCSTR, LPCSTR) { return std::string(); }
    // wstr()/str(): UTF-8 <-> UTF-16 (char16_t). Ver Common/MyUtf.cpp.
    std::u16string wstr (const std::string& str);
    std::u16string wstr (const char* str);
    std::u16string wstr (const char* str, int len);
    std::string    str  (const std::u16string& wstr);
    std::string    str  (const char16_t* wstr);
    std::string    str  (const char16_t* wstr, int len);
}

typedef int64_t clock64_t;

// ---- Sincronización (reemplaza CCriticalSection / CSingleLock de MFC) ----
#include <mutex>
class CCriticalSection
{
public:
    void Lock ()   { m_m.lock(); }
    void Unlock () { m_m.unlock(); }
private:
    std::recursive_mutex m_m;
};
class CSingleLock
{
public:
    CSingleLock (CCriticalSection* cs, int lockNow = 0) : m_cs(cs), m_locked(false) { if (lockNow) Lock(); }
    ~CSingleLock () { if (m_locked) m_cs->Unlock(); }
    void Lock ()   { m_cs->Lock(); m_locked = true; }
    void Unlock () { if (m_locked) { m_cs->Unlock(); m_locked = false; } }
private:
    CCriticalSection* m_cs;
    bool m_locked;
};
#define TRUE 1
#define FALSE 0
#define ASSERT_EXCEPTION_FRAME ((void)0)

// ---- Tipos/macros MSVC ----
#define __int64 long long
#define CDECL
#define _ASSERTE(x) assert(x)
#define _CHECK_AND_THROW_(x, msg) { if (!(x)) { throw std::runtime_error((msg)); } }

#define _RAISE(x) throw (x)
#define EXCEPTION_FRAME ((void)0)
#define THROW_APP_EXCEPTION(msg) do { throw std::runtime_error((msg)); } while(false)

// ---- Funciones CRT de MSVC ----
#include <cmath>
#include <cstdlib>
#define STACK_OVERFLOW_GUARD(n) ((void)0)
#define _snprintf snprintf
#define sprintf_s snprintf
#define _isnan(x) std::isnan(x)
namespace Common { std::string str (const char16_t*); }
inline int       _wtoi   (const char16_t* w) { return std::atoi(Common::str(w).c_str()); }
inline long long _wtoi64 (const char16_t* w) { return std::atoll(Common::str(w).c_str()); }
inline double    _wtof   (const char16_t* w) { return std::atof(Common::str(w).c_str()); }
#define _finite(x) std::isfinite(x)
#define _TRUNCATE 0
#define _snprintf_s(buf, size, trunc, ...) snprintf((buf), (size), __VA_ARGS__)


// ---- Más funciones CRT de MSVC (usadas por MetaDictionary y StrHelpers) ----
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <string>
using std::wstring;
inline char* itoa (int v, char* buf, int)               { std::snprintf(buf, 34, "%d", v); return buf; }
inline char* ultoa (unsigned long v, char* buf, int)    { std::snprintf(buf, 34, "%lu", v); return buf; }
inline char* _i64toa (long long v, char* buf, int)      { std::snprintf(buf, 66, "%lld", v); return buf; }
inline char* _ui64toa (unsigned long long v, char* buf, int) { std::snprintf(buf, 66, "%llu", v); return buf; }
inline char* _gcvt (double v, int digits, char* buf)    { std::snprintf(buf, 66, "%.*g", digits, v); return buf; }
inline unsigned long long _strtoui64 (const char* s, char** e, int base) { return std::strtoull(s, e, base); }
inline long long _atoi64 (const char* s)                { return std::atoll(s); }
inline int stricmp (const char* a, const char* b)       { return strcasecmp(a, b); }
inline int strnicmp (const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }
inline int _stricmp (const char* a, const char* b)      { return strcasecmp(a, b); }
inline int _strnicmp (const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }

#define _ASSERT(x) assert(x)
#define TRACE1(...) ((void)0)
#define TRACE(...) ((void)0)
#define ASSERT(x) assert(x)
