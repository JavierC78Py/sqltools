#include "SqlHighlighter.h"
#include "Theme.h"
#include <QSettings>

namespace { QSet<QString> g_userObjects; QList<SqlHighlighter*> g_instances; }

SqlHighlighter::SqlHighlighter (QTextDocument* doc) : QSyntaxHighlighter(doc)
{
    g_instances << this; connect(doc, &QObject::destroyed, this, [this]{ g_instances.removeAll(this); });
    const QString kws = "select|from|where|and|or|not|in|is|null|like|between|exists|order|by|group|having|union|all|intersect|minus|"
        "insert|into|values|update|set|delete|merge|using|when|matched|then|else|elsif|end|case|create|alter|drop|truncate|table|view|"
        "index|sequence|synonym|trigger|procedure|function|package|body|type|replace|or|begin|declare|exception|loop|for|while|if|"
        "return|returns|cursor|open|fetch|close|commit|rollback|savepoint|grant|revoke|join|inner|left|right|full|outer|cross|on|as|"
        "distinct|connect|start|with|prior|level|rownum|rowid|dual|default|constraint|primary|foreign|key|references|unique|check|"
        "number|varchar2|nvarchar2|char|date|timestamp|clob|blob|raw|boolean|integer|out|nocopy|desc|asc|nulls|first|last|over|"
        "partition|rows|row|only|fetch|offset|limit|pragma|autonomous_transaction|raise|exit|continue|goto|bulk|collect|forall|execute|immediate";
    m_keywords = QRegularExpression("\\b(" + kws + ")\\b", QRegularExpression::CaseInsensitiveOption);
    m_number = QRegularExpression(R"(\b\d+(\.\d+)?\b)");
    m_string = QRegularExpression(R"('([^']|'')*')");
    m_lineComment = QRegularExpression("--[^\n]*");
    m_blockStart = QRegularExpression(R"(/\*)");
    m_blockEnd = QRegularExpression(R"(\*/)");
    m_func = QRegularExpression(R"(\b[A-Za-z_][\w$#]*(?=\s*\())");
    m_op = QRegularExpression(R"([-+*/=<>!|:%^~;,()@]+)");
    m_bind = QRegularExpression(R"(:[A-Za-z_]\w*)");
    refreshColors();
}

static QTextCharFormat fmt (const SynStyle& st) { QTextCharFormat f; f.setForeground(st.color); if (st.bold) f.setFontWeight(QFont::DemiBold); f.setFontItalic(st.italic); f.setFontUnderline(st.underline); return f; }

void SqlHighlighter::setStyles (const QVector<SynStyle>& v)
{
    if (v.size() < SynClassCount) return;
    m_kw = fmt(v[SynKeyword]); m_fn = fmt(v[SynFunction]); m_num = fmt(v[SynNumber]); m_str = fmt(v[SynString]); m_com = fmt(v[SynComment]); m_opf = fmt(v[SynOperator]); m_bindf = fmt(v[SynBind]); m_hint = fmt(v[SynHint]); m_userObj = fmt(v[SynUserObject]); m_userObjOn = QSettings().value("editor/highlightObjects", true).toBool();
    rehighlight();
}

// Colores del resaltado: los de Settings > Colores (uno para el tema claro y otro para el oscuro)
// Como el SQLTools de Windows (SessionCache / OEHighlighter): cualquier palabra del texto que coincida (sin distinguir mayúsculas) con el nombre de un
// objeto del esquema actual se pinta con el atributo «User Objects» (negrita, RGB 128,0,255). Las palabras reservadas tienen prioridad; no hay regla del punto.
void SqlHighlighter::setUserObjects (const QSet<QString>& names)
{
    g_userObjects = names;
    for (auto* h : g_instances) h->rehighlight();
}
void SqlHighlighter::setExtraObjects (const QSet<QString>& names) { m_extra = names; rehighlight(); }

void SqlHighlighter::refreshColors () { setStyles(SyntaxColors::load(Theme::dark())); }

void SqlHighlighter::highlightBlock (const QString& text)
{
    auto apply = [&](const QRegularExpression& re, const QTextCharFormat& f) {
        for (auto it = re.globalMatch(text); it.hasNext();) { auto m = it.next(); setFormat(m.capturedStart(), m.capturedLength(), f); } };
    apply(m_op, m_opf); apply(m_func, m_fn);
    if (m_userObjOn && (!g_userObjects.isEmpty() || !m_extra.isEmpty()))
    {
        static const QRegularExpression word(R"([A-Za-z_][\w$#]*)");
        for (auto it = word.globalMatch(text); it.hasNext();) { auto m = it.next(); const QString w = m.captured().toUpper(); if (g_userObjects.contains(w) || m_extra.contains(w)) setFormat(m.capturedStart(), m.capturedLength(), m_userObj); }
    }
    apply(m_keywords, m_kw); apply(m_number, m_num); apply(m_bind, m_bindf); apply(m_string, m_str); apply(m_lineComment, m_com);

    // comentarios /* ... */ que pueden abarcar varias líneas
    setCurrentBlockState(0);
    int start = previousBlockState() == 1 ? 0 : text.indexOf(m_blockStart);
    while (start >= 0)
    {
        auto end = m_blockEnd.match(text, start);
        int len;
        if (!end.hasMatch()) { setCurrentBlockState(1); len = text.length() - start; }
        else len = end.capturedEnd() - start;
        setFormat(start, len, text.mid(start, 3) == "/*+" ? m_hint : m_com);
        start = text.indexOf(m_blockStart, start + len);
    }
}
