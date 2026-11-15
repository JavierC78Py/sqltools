#include "FastDateFormat.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace Common
{
static const char* MONTHS[12] = { "January","February","March","April","May","June","July","August","September","October","November","December" };
static const char* DAYS[7]    = { "Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday" };

static bool startsWithNoCase (const char* s, const char* p)
{
    while (*p) { if (std::toupper((unsigned char)*s) != std::toupper((unsigned char)*p)) return false; ++s; ++p; }
    return true;
}
// Mayúsculas/minúsculas del elemento según el formato: "MON"->MonU, "Mon"->MonC, "mon"->MonL
static int caseOf (const char* s, size_t n)
{
    bool up = true, low = true;
    for (size_t i = 0; i < n; ++i) { if (!std::isupper((unsigned char)s[i])) up = false; if (!std::islower((unsigned char)s[i])) low = false; }
    if (up) return 0;
    if (low) return 2;
    if (std::isupper((unsigned char)s[0]) && n > 1) { bool rest = true; for (size_t i = 1; i < n; ++i) if (!std::islower((unsigned char)s[i])) rest = false; if (rest) return 1; }
    return -1;
}

bool FastDateFormat::clientLanguageIsEnglish ()
{
    auto ok = [](const char* v) { return !v || !*v || startsWithNoCase(v, "AMERICAN") || startsWithNoCase(v, "ENGLISH"); };
    if (!ok(std::getenv("NLS_DATE_LANGUAGE"))) return false;
    const char* lang = std::getenv("NLS_LANG");      // formato IDIOMA_TERRITORIO.JUEGO
    return ok(lang);
}

bool FastDateFormat::compile (const std::string& f)
{
    m_items.clear(); m_ok = false;
    if (f.empty()) return false;
    std::string lit;
    auto flush = [&]{ if (!lit.empty()) { m_items.push_back({Literal, lit}); lit.clear(); } };
    size_t i = 0, n = f.size();
    while (i < n)
    {
        const char* p = f.c_str() + i;
        char c = f[i];
        if (c == '"')                           // texto entre comillas
        {
            size_t j = f.find('"', i + 1);
            if (j == std::string::npos) return false;
            lit += f.substr(i + 1, j - i - 1); i = j + 1; continue;
        }
        if (!std::isalpha((unsigned char)c))    // separador
        {
            if (c == '-' || c == '/' || c == ',' || c == '.' || c == ':' || c == ';' || c == ' ') { lit += c; ++i; continue; }
            return false;
        }
        flush();
        auto take = [&](const char* tok, Kind k) { size_t L = std::strlen(tok); if (startsWithNoCase(p, tok)) { m_items.push_back({k, ""}); i += L; return true; } return false; };
        auto takeCase = [&](const char* tok, Kind u, Kind cc, Kind l) -> int {
            size_t L = std::strlen(tok);
            if (!startsWithNoCase(p, tok)) return 0;
            int cs = caseOf(p, L); if (cs < 0) return -1;
            m_items.push_back({cs == 0 ? u : cs == 1 ? cc : l, ""}); i += L; return 1; };
        int r;
        // RRRR/YYYY (4 dígitos); en salida RR == YY (últimos dos dígitos)
        if ((startsWithNoCase(p, "YYYY") && caseOf(p, 4) == 0) || (startsWithNoCase(p, "RRRR") && caseOf(p, 4) == 0)) { m_items.push_back({Y4, ""}); i += 4; continue; }
        if (startsWithNoCase(p, "YYY") && caseOf(p, 3) == 0) { m_items.push_back({Y3, ""}); i += 3; continue; }
        if ((startsWithNoCase(p, "YY") && caseOf(p, 2) == 0) || (startsWithNoCase(p, "RR") && caseOf(p, 2) == 0)) { m_items.push_back({Y2, ""}); i += 2; continue; }
        if (startsWithNoCase(p, "MONTH")) { r = takeCase("MONTH", MonthU, MonthC, MonthL); if (r <= 0) return false; continue; }
        if (startsWithNoCase(p, "MON"))   { r = takeCase("MON", MonU, MonC, MonL);             if (r <= 0) return false; continue; }
        if (startsWithNoCase(p, "DAY"))   { r = takeCase("DAY", DayU, DayC, DayL);             if (r <= 0) return false; continue; }
        if (startsWithNoCase(p, "DY"))    { r = takeCase("DY", DyU, DyC, DyL);                 if (r <= 0) return false; continue; }
        if (startsWithNoCase(p, "DDD") && caseOf(p, 3) == 0) { m_items.push_back({DDD, ""}); i += 3; continue; }
        if (caseOf(p, 2) == 0 || std::strlen(p) >= 2)
        {
            if (std::strncmp(p, "MM", 2) == 0)   { m_items.push_back({MM, ""});   i += 2; continue; }
            if (std::strncmp(p, "DD", 2) == 0)   { m_items.push_back({DD, ""});   i += 2; continue; }
            if (std::strncmp(p, "HH24", 4) == 0) { m_items.push_back({HH24, ""}); i += 4; continue; }
            if (std::strncmp(p, "HH12", 4) == 0) { m_items.push_back({HH12, ""}); i += 4; continue; }
            if (std::strncmp(p, "HH", 2) == 0)   { m_items.push_back({HH12, ""}); i += 2; continue; }
            if (std::strncmp(p, "MI", 2) == 0)   { m_items.push_back({MI, ""});   i += 2; continue; }
            if (std::strncmp(p, "SS", 2) == 0)   { m_items.push_back({SS, ""});   i += 2; continue; }
            if (std::strncmp(p, "AM", 2) == 0 || std::strncmp(p, "PM", 2) == 0) { m_items.push_back({AmPm, ""}); i += 2; continue; }
        }
        if (c == 'Q') { m_items.push_back({Q, ""}); ++i; continue; }
        (void)take;
        return false;                           // elemento no soportado -> usar OCI
    }
    flush();
    m_ok = !m_items.empty();
    return m_ok;
}

static int dayOfWeek (int y, int m, int d)      // 0 = domingo (algoritmo de Sakamoto)
{
    static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3) --y;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}
static int dayOfYear (int y, int m, int d)
{
    static const int cum[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    int r = cum[m - 1] + d;
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    if (leap && m > 2) ++r;
    return r;
}
static void appendCase (std::string& out, const std::string& s, int cs)
{
    for (size_t i = 0; i < s.size(); ++i)
        out += (cs == 0) ? (char)std::toupper((unsigned char)s[i]) : (cs == 2) ? (char)std::tolower((unsigned char)s[i]) : s[i];
}

void FastDateFormat::format (int y, int mo, int d, int h, int mi, int s, std::string& out) const
{
    out.clear();
    char buf[16];
    int dow = -1;
    for (const Item& it : m_items)
    {
        switch (it.kind)
        {
        case Literal: out += it.text; break;
        case Y4:  std::snprintf(buf, sizeof buf, "%04d", y); out += buf; break;
        case Y3:  std::snprintf(buf, sizeof buf, "%03d", y % 1000); out += buf; break;
        case Y2:  std::snprintf(buf, sizeof buf, "%02d", y % 100); out += buf; break;
        case MM:  std::snprintf(buf, sizeof buf, "%02d", mo); out += buf; break;
        case DD:  std::snprintf(buf, sizeof buf, "%02d", d); out += buf; break;
        case DDD: std::snprintf(buf, sizeof buf, "%03d", dayOfYear(y, mo, d)); out += buf; break;
        case HH24: std::snprintf(buf, sizeof buf, "%02d", h); out += buf; break;
        case HH12: std::snprintf(buf, sizeof buf, "%02d", (h % 12) == 0 ? 12 : h % 12); out += buf; break;
        case MI:  std::snprintf(buf, sizeof buf, "%02d", mi); out += buf; break;
        case SS:  std::snprintf(buf, sizeof buf, "%02d", s); out += buf; break;
        case AmPm: out += (h < 12) ? "AM" : "PM"; break;
        case Q:   out += (char)('0' + (mo - 1) / 3 + 1); break;
        case MonU: case MonC: case MonL: appendCase(out, std::string(MONTHS[mo - 1]).substr(0, 3), it.kind == MonU ? 0 : it.kind == MonC ? 1 : 2); break;
        case MonthU: case MonthC: case MonthL: { std::string m = MONTHS[mo - 1]; m.resize(9, ' '); appendCase(out, m, it.kind == MonthU ? 0 : it.kind == MonthC ? 1 : 2); break; }
        case DyU: case DyC: case DyL: if (dow < 0) dow = dayOfWeek(y, mo, d); appendCase(out, std::string(DAYS[dow]).substr(0, 3), it.kind == DyU ? 0 : it.kind == DyC ? 1 : 2); break;
        case DayU: case DayC: case DayL: { if (dow < 0) dow = dayOfWeek(y, mo, d); std::string n = DAYS[dow]; n.resize(9, ' '); appendCase(out, n, it.kind == DayU ? 0 : it.kind == DayC ? 1 : 2); break; }
        }
    }
}
}
