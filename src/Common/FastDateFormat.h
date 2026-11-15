#pragma once
// Formateador rápido de fechas Oracle (formatos simples) hecho en el propio programa.
// Evita una llamada al cliente Oracle (OCIDateToText) por cada celda, que cuesta ~38 µs por fecha.
// Solo cubre los elementos más comunes en inglés; si el formato usa algo no soportado, compile()
// devuelve false y el llamador debe usar OCIDateToText como antes (mismo resultado, más lento).
#include <string>
#include <vector>
namespace Common
{
class FastDateFormat
{
public:
    FastDateFormat () : m_ok(false) {}
    // fmt en UTF-8 (formato NLS de Oracle, p.ej. "DD-MON-RR" o "YYYY-MM-DD HH24:MI:SS")
    bool compile (const std::string& fmt);
    bool usable () const { return m_ok; }
    void format (int year, int month, int day, int hour, int minute, int second, std::string& out) const;

    // El cliente Oracle formatea con NLS_LANG/NLS_DATE_LANGUAGE del entorno. Solo es seguro el camino rápido
    // si el idioma efectivo es inglés (nombres de mes y día).
    static bool clientLanguageIsEnglish ();
private:
    enum Kind { Literal, Y4, Y3, Y2, MM, MonU, MonC, MonL, MonthU, MonthC, MonthL, DD, DDD, HH12, HH24, MI, SS, AmPm, DyU, DyC, DyL, DayU, DayC, DayL, Q };
    struct Item { Kind kind; std::string text; };
    std::vector<Item> m_items;
    bool m_ok;
};
}
