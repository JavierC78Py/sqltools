#include "SqlSplitter.h"
#include <QRegularExpression>
#include <algorithm>

namespace {

enum class Kind { Sql, Anonymous, Create };

// Palabras clave que, tras "CREATE [OR REPLACE] [EDITIONABLE|NONEDITIONABLE]", empiezan un objeto con cuerpo PL/SQL.
bool isPlsqlCreate (const QStringList& w)
{
    int i = 1;                                                  // w[0] == "create"
    if (i < w.size() && w[i] == "or") i += 2;                   // or replace
    if (i < w.size() && (w[i] == "editionable" || w[i] == "noneditionable")) ++i;
    if (i >= w.size()) return false;
    static const QStringList kinds = { "procedure", "function", "package", "trigger", "type", "library", "java" };
    return kinds.contains(w[i]);
}

bool isWordChar (QChar c) { return c.isLetterOrNumber() || c == '_' || c == '$' || c == '#'; }

} // namespace

QVector<SqlStatement> SqlSplitter::split (const QString& s)
{
    QVector<SqlStatement> out;
    const int n = s.size();
    int i = 0;

    QVector<int> newlines; for (int k = 0; k < n; ++k) if (s[k] == '\n') newlines.push_back(k);
    auto lineOf = [&](int pos) { return int(std::lower_bound(newlines.begin(), newlines.end(), pos) - newlines.begin()) + 1; };
    auto isSlashLine = [&](int pos) -> int {            // si en 'pos' empieza una línea que es solo '/', devuelve el fin de esa línea
        int ls = pos; while (ls > 0 && s[ls - 1] != '\n') --ls;
        if (s.mid(ls, pos - ls).trimmed().isEmpty() && pos < n && s[pos] == '/')
        {
            int le = pos + 1; while (le < n && s[le] != '\n') ++le;
            if (s.mid(pos + 1, le - pos - 1).trimmed().isEmpty()) return le;
        }
        return -1;
    };

    while (i < n)
    {
        // saltar blancos y comentarios entre sentencias, y ';' o '/' sueltos
        while (i < n)
        {
            if (s[i].isSpace() || s[i] == ';') { ++i; continue; }
            if (s[i] == '-' && i + 1 < n && s[i + 1] == '-') { while (i < n && s[i] != '\n') ++i; continue; }
            if (s[i] == '/' && i + 1 < n && s[i + 1] == '*') { int e = s.indexOf("*/", i + 2); i = e < 0 ? n : e + 2; continue; }
            { int e = isSlashLine(i); if (e >= 0) { i = e; continue; } }
            break;
        }
        if (i >= n) break;

        SqlStatement st; st.start = i;
        Kind kind = Kind::Sql;
        QStringList firstWords;              // las primeras palabras, para clasificar
        bool classified = false;
        int depth = 0; bool sawBegin = false;
        int stmtEnd = -1, next = n;

        int j = i;
        while (j < n)
        {
            QChar c = s[j];
            if (c == '\'')                                    // texto '...' con '' como comilla escapada
            {
                ++j; while (j < n) { if (s[j] == '\'') { if (j + 1 < n && s[j + 1] == '\'') j += 2; else { ++j; break; } } else ++j; }
                continue;
            }
            if (c == '"') { ++j; while (j < n && s[j] != '"') ++j; if (j < n) ++j; continue; }
            if (c == 'q' || c == 'Q')                         // q'[ ... ]' literales alternativos
            {
                if (j + 2 < n && s[j + 1] == '\'' && (j == 0 || !isWordChar(s[j - 1])))
                {
                    QChar open = s[j + 2]; QChar close = open == '[' ? ']' : open == '(' ? ')' : open == '{' ? '}' : open == '<' ? '>' : open;
                    int e = s.indexOf(QString(close) + "'", j + 3);
                    j = e < 0 ? n : e + 2; continue;
                }
            }
            if (c == '-' && j + 1 < n && s[j + 1] == '-') { while (j < n && s[j] != '\n') ++j; continue; }
            if (c == '/' && j + 1 < n && s[j + 1] == '*') { int e = s.indexOf("*/", j + 2); j = e < 0 ? n : e + 2; continue; }

            if (c == '/')                                      // línea que solo contiene '/'
            {
                int e = isSlashLine(j);
                if (e >= 0) { stmtEnd = j; next = e; break; }
            }
            if (isWordChar(c) && (j == 0 || !isWordChar(s[j - 1])))
            {
                int k = j; while (k < n && isWordChar(s[k])) ++k;
                QString w = s.mid(j, k - j).toLower();
                if (!classified)
                {
                    firstWords << w;
                    if (firstWords[0] == "begin" || firstWords[0] == "declare") { kind = Kind::Anonymous; classified = true; }
                    else if (firstWords[0] == "create")
                    {
                        if (firstWords.size() >= 5 || (firstWords.size() >= 2 && firstWords[1] != "or" && firstWords[1] != "editionable" && firstWords[1] != "noneditionable") )
                        { kind = isPlsqlCreate(firstWords) ? Kind::Create : Kind::Sql; classified = true; }
                        else if (firstWords.size() >= 4 && firstWords[1] == "or") { kind = isPlsqlCreate(firstWords) ? Kind::Create : Kind::Sql; classified = true; }
                    }
                    else classified = true;
                }
                if (kind == Kind::Anonymous)
                {
                    if (w == "begin") { ++depth; sawBegin = true; }
                    else if (w == "if" || w == "loop" || w == "case") ++depth;
                    else if (w == "end")
                    {
                        // "end if", "end loop", "end case" cierran su propia estructura; "end" a secas cierra BEGIN o CASE-expresión
                        int m = k; while (m < n && s[m].isSpace()) ++m;
                        int m2 = m; while (m2 < n && isWordChar(s[m2])) ++m2;
                        QString nx = s.mid(m, m2 - m).toLower();
                        if (nx == "if" || nx == "loop" || nx == "case") k = m2;
                        --depth;
                    }
                }
                j = k; continue;
            }
            if (c == ';')
            {
                if (kind == Kind::Sql || (kind == Kind::Anonymous && sawBegin && depth <= 0)) { stmtEnd = (kind == Kind::Sql) ? j : j + 1; next = j + 1; break; }
                if (kind == Kind::Create) { ++j; continue; }
            }
            ++j;
        }
        if (stmtEnd < 0) { stmtEnd = n; next = n; }             // hasta el final del texto

        st.end = stmtEnd;
        st.plsql = kind != Kind::Sql;
        QString raw = s.mid(st.start, st.end - st.start);
        // quitar blancos finales
        int e = raw.size(); while (e > 0 && raw[e - 1].isSpace()) --e;
        raw.truncate(e); st.end = st.start + e;
        st.text = raw;
        st.startLine = lineOf(st.start);
        if (!st.text.trimmed().isEmpty()) out.push_back(st);
        i = next;
    }
    return out;
}

int SqlSplitter::statementAt (const QVector<SqlStatement>& v, int pos)
{
    int best = -1;
    for (int k = 0; k < v.size(); ++k)
    {
        if (pos >= v[k].start && pos <= v[k].end) return k;          // dentro (o justo al final)
        if (v[k].end < pos) best = k;                                // la última anterior
    }
    return best;
}
