#include "FoldRegions.h"
#include "SqlSplitter.h"
#include <QSet>
#include <algorithm>

namespace {

struct Tok { QString w; int line; int pos; };      // w: palabra en minúsculas o un signo suelto

bool isPart (QChar c) { return c.isLetterOrNumber() || c == '_' || c == '$' || c == '#'; }

// Tokeniza ignorando textos y comentarios. Los comentarios /* */ de varias líneas se devuelven en 'comments' como regiones.
QVector<Tok> tokenize (const QString& s, QVector<FoldRegion>* comments)
{
    QVector<Tok> out; const int n = s.size(); int i = 0, line = 0;
    auto adv = [&](int upto) { for (; i < upto && i < n; ++i) if (s[i] == '\n') ++line; };
    while (i < n)
    {
        const QChar c = s[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (c.isSpace()) { ++i; continue; }
        if (c == '-' && i + 1 < n && s[i + 1] == '-') { while (i < n && s[i] != '\n') ++i; continue; }
        if (c == '/' && i + 1 < n && s[i + 1] == '*')
        {
            const int startLine = line; const int e = s.indexOf("*/", i + 2); adv(e < 0 ? n : e + 2);
            if (comments && line > startLine) comments->push_back({startLine, line, 0, "comment"});
            continue;
        }
        if (c == '\'') { ++i; while (i < n) { if (s[i] == '\'') { if (i + 1 < n && s[i + 1] == '\'') i += 2; else { ++i; break; } } else { if (s[i] == '\n') ++line; ++i; } } continue; }
        if ((c == 'q' || c == 'Q') && i + 2 < n && s[i + 1] == '\'' && (i == 0 || !isPart(s[i - 1])))
        {
            const QChar open = s[i + 2], close = open == '[' ? ']' : open == '(' ? ')' : open == '{' ? '}' : open == '<' ? '>' : open;
            const int e = s.indexOf(QString(close) + "'", i + 3); adv(e < 0 ? n : e + 2); continue;
        }
        if (c == '"') { ++i; while (i < n && s[i] != '"') { if (s[i] == '\n') ++line; ++i; } if (i < n) ++i; continue; }
        if (isPart(c) && (i == 0 || !isPart(s[i - 1]))) { int j = i; while (j < n && isPart(s[j])) ++j; out.push_back({s.mid(i, j - i).toLower(), line, i}); i = j; continue; }
        out.push_back({QString(c), line, i}); ++i;
    }
    return out;
}

} // namespace

QVector<FoldRegion> FoldRegions::compute (const QString& text)
{
    QVector<FoldRegion> res;
    QVector<int> lineStart; lineStart << 0; for (int i = 0; i < text.size(); ++i) if (text[i] == '\n') lineStart << i + 1;
    auto lineOf = [&](int pos) { return int(std::upper_bound(lineStart.begin(), lineStart.end(), pos) - lineStart.begin()) - 1; };

    QVector<FoldRegion> comments;
    const QVector<Tok> toks = tokenize(text, &comments);
    res += comments;

    // paréntesis de varias líneas
    { QVector<Tok> st; for (const Tok& t : toks) { if (t.w == "(") st.push_back(t); else if (t.w == ")" && !st.isEmpty()) { const Tok o = st.takeLast(); if (t.line > o.line) res.push_back({o.line, t.line, 0, "paren"}); } } }

    // sentencias y bloques PL/SQL
    for (const SqlStatement& st : SqlSplitter::split(text))
    {
        const int sl = lineOf(st.start), el = lineOf(qMax(st.start, st.end - 1));
        if (el > sl) res.push_back({sl, el, 0, "stmt"});
        if (!st.plsql) continue;

        struct Open { QString kind; int line; };
        QVector<Open> stack; int pending = -1; int paren = 0;            // pending: línea de un PROCEDURE/FUNCTION/PACKAGE/TRIGGER/TYPE cuya cabecera aún no ha terminado
        auto close = [&](int endLine) { if (stack.isEmpty()) return; const Open o = stack.takeLast(); if (endLine > o.line) res.push_back({o.line, endLine, 0, o.kind}); };
        for (int k = 0; k < toks.size(); ++k)
        {
            const Tok& t = toks[k];
            if (t.pos < st.start || t.pos >= st.end) continue;
            const QString& w = t.w;
            if (w == "(") ++paren; else if (w == ")") --paren;
            else if (w == ";" && paren <= 0) pending = -1;
            else if (w == "procedure" || w == "function" || w == "package" || w == "trigger" || w == "type") { if (pending < 0) pending = t.line; }
            else if ((w == "is" || w == "as") && pending >= 0 && paren <= 0)
            { stack.push_back({(stack.isEmpty() && pending == sl) ? "pkg" : "sub", pending}); pending = -1; }
            else if (w == "declare") stack.push_back({"declare", t.line});
            else if (w == "begin") stack.push_back({"begin", t.line});
            else if (w == "case") stack.push_back({"case", t.line});
            else if (w == "if") { if (k == 0 || toks[k - 1].w != "end") stack.push_back({"if", t.line}); }
            else if (w == "loop") { if (k == 0 || toks[k - 1].w != "end") stack.push_back({"loop", t.line}); }
            else if (w == "end")
            {
                const QString next = k + 1 < toks.size() ? toks[k + 1].w : QString();
                if (next == "if" || next == "loop" || next == "case") { while (!stack.isEmpty() && stack.last().kind != next) stack.removeLast(); close(t.line); ++k; continue; }
                // END de un BEGIN (con su DECLARE o subprograma si lo hay), de un CASE o de un paquete
                if (!stack.isEmpty() && stack.last().kind == "begin") { close(t.line); if (!stack.isEmpty() && (stack.last().kind == "sub" || stack.last().kind == "pkg" || stack.last().kind == "declare")) close(t.line); }
                else close(t.line);
            }
        }
    }

    // quitar duplicados exactos y calcular niveles
    std::sort(res.begin(), res.end(), [](const FoldRegion& a, const FoldRegion& b) { return a.start != b.start ? a.start < b.start : a.end > b.end; });
    QVector<FoldRegion> uniq; QSet<QPair<int, int>> seen;
    for (const auto& r : res) { if (r.end <= r.start) continue; if (seen.contains({r.start, r.end})) continue; seen.insert({r.start, r.end}); uniq.push_back(r); }
    QVector<int> ends;                                                  // pila de fines de las regiones abiertas
    for (auto& r : uniq) { while (!ends.isEmpty() && ends.last() < r.start) ends.removeLast(); r.level = ends.size(); ends.push_back(r.end); }
    return uniq;
}
