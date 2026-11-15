// Conversión UTF-8 <-> UTF-16 portable. En Windows el original usaba wchar_t (2 bytes = UTF-16);
// en macOS wchar_t son 4 bytes, por eso la capa OCI usa char16_t explícitamente.
#include "stdafx.h"
namespace Common
{
static void putUtf8 (std::string& out, uint32_t cp)
{
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
}

std::u16string wstr (const char* s, int len)
{
    std::u16string out;
    if (!s) return out;
    const unsigned char* p = (const unsigned char*)s;
    const unsigned char* e = p + (len < 0 ? std::strlen(s) : (size_t)len);
    while (p < e)
    {
        uint32_t cp = *p; int n = 0;
        if (cp < 0x80) n = 0;
        else if ((cp >> 5) == 6)  { cp &= 0x1F; n = 1; }
        else if ((cp >> 4) == 14) { cp &= 0x0F; n = 2; }
        else if ((cp >> 3) == 30) { cp &= 0x07; n = 3; }
        else { out += (char16_t)cp; ++p; continue; }      // byte inválido: se conserva tal cual
        ++p;
        while (n-- > 0 && p < e && (*p & 0xC0) == 0x80) cp = (cp << 6) | (*p++ & 0x3F);
        if (cp >= 0x10000) { cp -= 0x10000; out += (char16_t)(0xD800 + (cp >> 10)); out += (char16_t)(0xDC00 + (cp & 0x3FF)); }
        else out += (char16_t)cp;
    }
    return out;
}
std::u16string wstr (const std::string& s) { return wstr(s.c_str(), (int)s.size()); }
std::u16string wstr (const char* s)        { return wstr(s, -1); }

std::string str (const char16_t* w, int len)
{
    std::string out;
    if (!w) return out;
    const char16_t* e = w + (len < 0 ? std::char_traits<char16_t>::length(w) : (size_t)len);
    while (w < e)
    {
        uint32_t cp = *w++;
        if (cp >= 0xD800 && cp < 0xDC00 && w < e && *w >= 0xDC00 && *w < 0xE000)
            cp = 0x10000 + ((cp - 0xD800) << 10) + (*w++ - 0xDC00);
        putUtf8(out, cp);
    }
    return out;
}
std::string str (const char16_t* w)           { return str(w, -1); }
std::string str (const std::u16string& w)     { return str(w.c_str(), (int)w.size()); }
}
