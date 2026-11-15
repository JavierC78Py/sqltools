#include "SqlContext.h"
#include "SqlSplitter.h"
#include <QSet>
#include <QStringList>

namespace {

struct Tok { QString text; bool word = false; bool quoted = false; int pos = 0; };

QVector<Tok> tokenize (const QString& s, int base)
{
    QVector<Tok> out; const int n = s.size(); int i = 0;
    auto isStart = [](QChar c) { return c.isLetter() || c == '_'; };
    auto isPart = [](QChar c) { return c.isLetterOrNumber() || c == '_' || c == '$' || c == '#'; };
    while (i < n)
    {
        QChar c = s[i];
        if (c.isSpace()) { ++i; continue; }
        if (c == '-' && i + 1 < n && s[i + 1] == '-') { while (i < n && s[i] != '\n') ++i; continue; }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') { int e = s.indexOf("*/", i + 2); i = e < 0 ? n : e + 2; continue; }
        if (c == '\'') { ++i; while (i < n) { if (s[i] == '\'') { if (i + 1 < n && s[i + 1] == '\'') i += 2; else { ++i; break; } } else ++i; } continue; }
        if (c == '"') { int e = s.indexOf('"', i + 1); if (e < 0) e = n - 1; out.push_back({s.mid(i + 1, e - i - 1), true, true, base + i}); i = e + 1; continue; }
        if (isStart(c)) { int j = i; while (j < n && isPart(s[j])) ++j; out.push_back({s.mid(i, j - i), true, false, base + i}); i = j; continue; }
        if (c.isDigit()) { int j = i; while (j < n && (s[j].isLetterOrNumber() || s[j] == '.')) ++j; out.push_back({s.mid(i, j - i), false, false, base + i}); i = j; continue; }
        out.push_back({QString(c), false, false, base + i}); ++i;
    }
    return out;
}

const QSet<QString>& stopWords ()
{
    static const QSet<QString> w = { "WHERE", "ON", "JOIN", "INNER", "LEFT", "RIGHT", "FULL", "CROSS", "NATURAL", "OUTER", "GROUP", "ORDER", "UNION", "INTERSECT",
        "MINUS", "SET", "USING", "CONNECT", "START", "HAVING", "FETCH", "OFFSET", "PARTITION", "RETURNING", "SELECT", "VALUES", "WHEN", "THEN", "LOOP", "FOR",
        "WITH", "AND", "OR", "AS", "FROM", "INTO", "UPDATE", "MERGE", "LATERAL", "SAMPLE", "PIVOT", "UNPIVOT", "MODEL", "WINDOW", "KEEP", "NOWAIT", "SKIP", "OF", "CONNECT_BY" };
    return w;
}
QString up (const QString& s) { return s.toUpper(); }


// Recoge alias de tablas en toks[from, to): FROM/JOIN/UPDATE/INTO tabla [AS] [alias] {, tabla [alias]}
void collectAliases (const QVector<Tok>& toks, int from, int to, QHash<QString, QString>& out)
{
    for (int i = from; i < to; ++i)
    {
        const QString kw = toks[i].quoted ? QString() : up(toks[i].text);
        if (!toks[i].word || (kw != "FROM" && kw != "JOIN" && kw != "UPDATE" && kw != "INTO")) continue;
        int j = i + 1;
        while (j < to)
        {
            if (!toks[j].word) break;                                            // subconsulta "(" u otra cosa: no es una tabla
            QString table = toks[j].quoted ? toks[j].text : up(toks[j].text), bare = table; ++j;
            while (j + 1 < to && toks[j].text == "." && toks[j + 1].word)                                           // esquema.tabla: se conserva el esquema
            { bare = toks[j + 1].quoted ? toks[j + 1].text : up(toks[j + 1].text); table = table + "." + bare; j += 2; }
            if (j < to && toks[j].text == "." && !toks[j].word) break;                                               // "esquema." sin nada detrás: nombre incompleto, no es una tabla
            if (stopWords().contains(bare) && !toks[j - 1].quoted) break;
            out.insert(bare, table);
            if (j < to && toks[j].word && !toks[j].quoted && up(toks[j].text) == "AS") ++j;
            if (j < to && toks[j].word && (toks[j].quoted || !stopWords().contains(up(toks[j].text))))
            { out.insert(toks[j].quoted ? toks[j].text : up(toks[j].text), table); ++j; }
            if (kw == "FROM" && j < to && toks[j].text == ",") { ++j; continue; }
            break;
        }
    }
}

QString nameOf (const Tok& t) { return t.quoted ? t.text : up(t.text); }
bool isKw (const Tok& t, const char* kw) { return t.word && !t.quoted && up(t.text) == kw; }

// Deduce las columnas que produce el SELECT de toks[from, to) (rango interior de unos paréntesis).
DerivedSource columnsOfSelect (const QVector<Tok>& toks, int from, int to, const QHash<QString, QString>& outerAliases)
{
    DerivedSource d;
    int i = from; while (i < to && !isKw(toks[i], "SELECT")) ++i;
    if (i >= to) return d;
    ++i;
    while (i < to && (isKw(toks[i], "DISTINCT") || isKw(toks[i], "UNIQUE") || isKw(toks[i], "ALL"))) ++i;
    // fin de la lista: FROM al mismo nivel de paréntesis
    int depth = 0, end = to;
    for (int k = i; k < to; ++k)
    {
        if (toks[k].text == "(" && !toks[k].word) ++depth; else if (toks[k].text == ")" && !toks[k].word) --depth;
        else if (depth == 0 && isKw(toks[k], "FROM")) { end = k; break; }
    }
    // tablas del FROM interno (para expandir "*")
    QHash<QString, QString> inner = outerAliases;
    collectAliases(toks, end, to, inner);
    QStringList innerTables; { int dd = 0; for (int k = end; k < to; ++k) { if (toks[k].text == "(" && !toks[k].word) ++dd; else if (toks[k].text == ")" && !toks[k].word) --dd; } }
    {
        QHash<QString, QString> only; collectAliases(toks, end, to, only);
        for (auto it = only.constBegin(); it != only.constEnd(); ++it) if (it.key() == it.value() && !innerTables.contains(it.value())) innerTables << it.value();
    }
    // partir por comas al nivel 0
    QVector<QVector<int>> items(1); depth = 0;
    for (int k = i; k < end; ++k)
    {
        const Tok& t = toks[k];
        if (!t.word && t.text == "(") ++depth; else if (!t.word && t.text == ")") --depth;
        if (depth == 0 && !t.word && t.text == ",") { items.push_back({}); continue; }
        items.last().push_back(k);
    }
    for (const auto& it : items)
    {
        if (it.isEmpty()) continue;
        const int n = it.size();
        const Tok& last = toks[it[n - 1]];
        if (n == 1 && !last.word && last.text == "*") { for (const QString& t : innerTables) if (!d.starTables.contains(t)) d.starTables << t; continue; }
        if (n >= 3 && toks[it[n - 1]].text == "*" && toks[it[n - 2]].text == "." && toks[it[n - 3]].word)       // alias.*
        { const QString q = nameOf(toks[it[n - 3]]); const QString t = inner.value(q, q); if (!d.starTables.contains(t)) d.starTables << t; continue; }
        QString name;
        if (n >= 3 && isKw(toks[it[n - 2]], "AS") && last.word) name = nameOf(last);
        else if (n == 1 && last.word) name = nameOf(last);
        else if (n >= 3 && toks[it[n - 2]].text == "." && last.word) name = nameOf(last);                          // calificador.columna
        else if (n >= 2 && last.word && !isKw(last, "END") && toks[it[n - 2]].text != "." && !(toks[it[n - 2]].text.size() == 1 && QString("+-*/|(<>=,").contains(toks[it[n - 2]].text[0]) && !toks[it[n - 2]].word))
            name = nameOf(last);                                                                                    // expresión alias
        if (!name.isEmpty() && !d.columns.contains(name)) d.columns << name;
    }
    return d;
}

// Subconsultas con alias en el FROM y CTEs
void collectDerived (const QVector<Tok>& toks, SqlContext& ctx)
{
    const int n = toks.size();
    QVector<int> match(n, -1), stack;
    for (int i = 0; i < n; ++i)
    {
        if (toks[i].word) continue;
        if (toks[i].text == "(") stack.push_back(i);
        else if (toks[i].text == ")" && !stack.isEmpty()) { match[stack.last()] = i; match[i] = stack.last(); stack.pop_back(); }
    }
    auto add = [&](const QString& name, int open, const QStringList& explicitCols) {
        const int close = match[open]; if (close < 0) return;
        DerivedSource d = columnsOfSelect(toks, open + 1, close, ctx.aliases);
        if (!explicitCols.isEmpty()) { d.columns = explicitCols; d.starTables.clear(); }
        ctx.derived.insert(name, d); ctx.aliases.insert(name, name);
    };
    // CTE: WITH nombre [(c1, c2)] AS ( ... ) {, nombre [(...)] AS ( ... )}
    for (int i = 0; i < n; ++i)
    {
        if (!isKw(toks[i], "WITH")) continue;
        int j = i + 1;
        while (j < n && toks[j].word)
        {
            const QString name = nameOf(toks[j]); ++j;
            QStringList cols;
            if (j < n && !toks[j].word && toks[j].text == "(" && match[j] >= 0)
            {
                for (int k = j + 1; k < match[j]; ++k) if (toks[k].word) cols << nameOf(toks[k]);
                j = match[j] + 1;
            }
            if (j >= n || !isKw(toks[j], "AS")) break;
            ++j;
            if (j >= n || toks[j].text != "(" || match[j] < 0) break;
            add(name, j, cols);
            j = match[j] + 1;
            if (j < n && !toks[j].word && toks[j].text == ",") { ++j; continue; }
            break;
        }
    }
    // Subconsulta con alias:  ( SELECT ... ) [AS] alias
    for (int i = 0; i + 1 < n; ++i)
    {
        if (toks[i].word || toks[i].text != "(" || match[i] < 0) continue;
        if (!isKw(toks[i + 1], "SELECT") && !isKw(toks[i + 1], "WITH")) continue;
        int j = match[i] + 1;
        if (j < n && isKw(toks[j], "AS")) ++j;
        if (j < n && toks[j].word && (toks[j].quoted || !stopWords().contains(up(toks[j].text)))) add(nameOf(toks[j]), i, {});
    }
}

} // namespace

SqlContext SqlContext::analyze (const QString& text, int pos)
{
    SqlContext ctx;
    pos = qBound(0, pos, int(text.size()));
    auto isPart = [](QChar c) { return c.isLetterOrNumber() || c == '_' || c == '$' || c == '#'; };

    // palabra actual y calificador
    int ps = pos; while (ps > 0 && isPart(text[ps - 1])) --ps;
    ctx.prefix = text.mid(ps, pos - ps); ctx.prefixStart = ps;
    if (ps > 0 && text[ps - 1] == '.')
    {
        int qe = ps - 1, qs = qe;
        if (qs > 0 && text[qs - 1] == '"') { int o = text.lastIndexOf('"', qs - 2); if (o >= 0) { ctx.qualifier = text.mid(o + 1, qs - 1 - o - 1); } }
        else { while (qs > 0 && isPart(text[qs - 1])) --qs; ctx.qualifier = up(text.mid(qs, qe - qs)); }
        if (qs > 0 && text[qs - 1] == '.')                                   // esquema.objeto.
        {
            int se = qs - 1, ss = se; while (ss > 0 && isPart(text[ss - 1])) --ss;
            if (ss < se) ctx.qualifierSchema = up(text.mid(ss, se - ss));
        }
    }

    // sentencia actual (para resolver alias, incluso los definidos después del cursor)
    const auto stmts = SqlSplitter::split(text);
    const int k = SqlSplitter::statementAt(stmts, pos);
    int sStart = 0, sEnd = text.size();
    if (k >= 0) { sStart = stmts[k].start; sEnd = stmts[k].end; if (pos > sEnd) sEnd = text.size(); if (pos < sStart) sStart = pos; }
    const QString stmt = text.mid(sStart, sEnd - sStart);
    const QVector<Tok> toks = tokenize(stmt, sStart);

    // FROM / JOIN / UPDATE / INTO  <tabla> [AS] [alias] {, <tabla> [alias]}  (sobre todo el texto de la sentencia)
    collectAliases(toks, 0, toks.size(), ctx.aliases);
    collectDerived(toks, ctx);

    // ¿se espera un nombre de tabla en el cursor? Se mira hacia atrás hasta la última palabra clave "de cláusula".
    if (ctx.qualifier.isEmpty())
    {
        for (int i = toks.size() - 1; i >= 0; --i)
        {
            if (toks[i].pos >= ps) continue;                                     // tokens desde la palabra actual en adelante
            if (!toks[i].word || toks[i].quoted) { if (toks[i].text == ")" ) break; continue; }
            const QString w = up(toks[i].text);
            if (w == "FROM" || w == "JOIN" || w == "INTO" || w == "UPDATE") { ctx.afterFrom = true; break; }
            static const QSet<QString> clause = { "SELECT", "WHERE", "ON", "GROUP", "ORDER", "HAVING", "SET", "VALUES", "USING", "AND", "OR", "BY" };
            if (clause.contains(w)) break;
        }
    }
    return ctx;
}
