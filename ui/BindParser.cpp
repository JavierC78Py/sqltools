#include "BindParser.h"
#include <QRegularExpression>
#include <QSet>

namespace {
// Recorre el SQL y llama a onBind(inicio, longitud, nombre) por cada :variable fuera de textos y comentarios.
template <class F> void scan (const QString& s, F onBind)
{
    const int n = s.size(); int i = 0;
    auto isPart = [](QChar c) { return c.isLetterOrNumber() || c == '_' || c == '$' || c == '#'; };
    while (i < n)
    {
        const QChar c = s[i];
        if (c == '\'') { ++i; while (i < n) { if (s[i] == '\'') { if (i + 1 < n && s[i + 1] == '\'') i += 2; else { ++i; break; } } else ++i; } continue; }
        if ((c == 'q' || c == 'Q') && i + 2 < n && s[i + 1] == '\'' && (i == 0 || !isPart(s[i - 1])))          // q'[ ... ]'
        {
            const QChar open = s[i + 2], close = open == '[' ? ']' : open == '(' ? ')' : open == '{' ? '}' : open == '<' ? '>' : open;
            const int e = s.indexOf(QString(close) + "'", i + 3); i = e < 0 ? n : e + 2; continue;
        }
        if (c == '"') { ++i; while (i < n && s[i] != '"') ++i; if (i < n) ++i; continue; }
        if (c == '-' && i + 1 < n && s[i + 1] == '-') { while (i < n && s[i] != '\n') ++i; continue; }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') { const int e = s.indexOf("*/", i + 2); i = e < 0 ? n : e + 2; continue; }
        if (c == ':' && i + 1 < n && (i == 0 || s[i - 1] != ':') && (s[i + 1].isLetter() || s[i + 1].isDigit() || s[i + 1] == '_'))
        {
            int j = i + 1; while (j < n && isPart(s[j])) ++j;
            onBind(i, j - i, s.mid(i + 1, j - i - 1)); i = j; continue;
        }
        ++i;
    }
}
}

QStringList BindParser::names (const QString& sql)
{
    QStringList out; QSet<QString> seen;
    scan(sql, [&](int, int, const QString& name) { if (!seen.contains(name.toUpper())) { seen.insert(name.toUpper()); out << name; } });
    return out;
}

QString BindParser::literal (const BindValue& v, QString* warning)
{
    auto quote = [](QString t) { return "'" + t.replace('\'', "''") + "'"; };
    static const QRegularExpression num(R"(^[+-]?(\d+(\.\d*)?|\.\d+)([eE][+-]?\d+)?$)");
    static const QRegularExpression date(R"(^\d{4}-\d{2}-\d{2}( \d{2}:\d{2}:\d{2})?$)");
    const QString t = v.value.trimmed();
    if (v.type == "NULL" || (v.value.isEmpty() && v.type != "String")) return "NULL";
    if (v.type == "Number") { if (num.match(t).hasMatch()) return t; if (warning) *warning = "«" + t + "» no es un número: se usa NULL."; return "NULL"; }
    if (v.type == "Date")
    {
        if (date.match(t).hasMatch()) return t.size() > 10 ? "to_date(" + quote(t) + ", 'YYYY-MM-DD HH24:MI:SS')" : "to_date(" + quote(t) + ", 'YYYY-MM-DD')";
        if (warning) *warning = "«" + t + "» no es una fecha AAAA-MM-DD [HH:MM:SS]: se usa NULL."; return "NULL";
    }
    if (v.type == "String") return v.value.isEmpty() ? "NULL" : quote(v.value);       // en Oracle '' es NULL
    return num.match(t).hasMatch() ? t : quote(v.value);                                // Auto
}

QString BindParser::substitute (const QString& sql, const QMap<QString, BindValue>& values, QStringList* warnings)
{
    struct Hit { int pos, len; QString name; };
    QVector<Hit> hits; scan(sql, [&](int pos, int len, const QString& name) { hits.push_back({pos, len, name}); });
    QString out = sql;
    for (int k = hits.size() - 1; k >= 0; --k)                                   // de atrás hacia delante para no desplazar posiciones
    {
        QString w; const BindValue v = values.value(hits[k].name.toUpper());
        const QString lit = literal(v, &w);
        if (!w.isEmpty() && warnings) *warnings << (":" + hits[k].name + " — " + w);
        out.replace(hits[k].pos, hits[k].len, lit);
    }
    return out;
}
