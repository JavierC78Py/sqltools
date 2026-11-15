#include "SqlFormatter.h"
#include "SqlSplitter.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QMap>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include <QVector>
#include <functional>

// ---------------- Opciones ----------------
FormatOptions FormatOptions::load ()
{
    QSettings s; FormatOptions o;
    o.indent = s.value("format/indent", o.indent).toInt(); o.useTabs = s.value("format/useTabs", o.useTabs).toBool();
    o.keywordCase = s.value("format/keywordCase", o.keywordCase).toInt(); o.style = s.value("format/style", o.style).toInt();
    o.commaLeading = s.value("format/commaLeading", o.commaLeading).toBool(); o.andOrNewLine = s.value("format/andOrNewLine", o.andOrNewLine).toBool();
    o.oneItemPerLine = s.value("format/oneItemPerLine", o.oneItemPerLine).toBool(); o.maxLine = s.value("format/maxLine", o.maxLine).toInt();
    o.contIndent = s.value("format/contIndent", o.contIndent).toInt(); o.andOrIndent = s.value("format/andOrIndent", o.andOrIndent).toInt(); o.joinIndent = s.value("format/joinIndent", o.joinIndent).toInt();
    o.kwAloneWhenMany = s.value("format/kwAloneWhenMany", o.kwAloneWhenMany).toBool(); o.onNewLine = s.value("format/onNewLine", o.onNewLine).toBool();
    o.spaceAfterComma = s.value("format/spaceAfterComma", o.spaceAfterComma).toBool(); o.spaceInParens = s.value("format/spaceInParens", o.spaceInParens).toBool();
    o.functionCase = s.value("format/functionCase", o.functionCase).toInt(); o.identifierCase = s.value("format/identifierCase", o.identifierCase).toInt();
    o.alignDeclarations = s.value("format/alignDeclarations", o.alignDeclarations).toBool(); o.alignAssignments = s.value("format/alignAssignments", o.alignAssignments).toBool();
    o.blankBeforeException = s.value("format/blankBeforeException", o.blankBeforeException).toBool(); o.blankAfterDeclare = s.value("format/blankAfterDeclare", o.blankAfterDeclare).toBool();
    return o;
}
void FormatOptions::save () const
{
    QSettings s; s.setValue("format/indent", indent); s.setValue("format/useTabs", useTabs); s.setValue("format/keywordCase", keywordCase); s.setValue("format/style", style);
    s.setValue("format/commaLeading", commaLeading); s.setValue("format/andOrNewLine", andOrNewLine); s.setValue("format/oneItemPerLine", oneItemPerLine); s.setValue("format/maxLine", maxLine);
    s.setValue("format/contIndent", contIndent); s.setValue("format/andOrIndent", andOrIndent); s.setValue("format/joinIndent", joinIndent); s.setValue("format/kwAloneWhenMany", kwAloneWhenMany); s.setValue("format/onNewLine", onNewLine);
    s.setValue("format/spaceAfterComma", spaceAfterComma); s.setValue("format/spaceInParens", spaceInParens); s.setValue("format/functionCase", functionCase); s.setValue("format/identifierCase", identifierCase);
    s.setValue("format/alignDeclarations", alignDeclarations); s.setValue("format/alignAssignments", alignAssignments); s.setValue("format/blankBeforeException", blankBeforeException); s.setValue("format/blankAfterDeclare", blankAfterDeclare);
}

QString FormatOptions::toJson () const
{
    QJsonObject j; j["indent"] = indent; j["useTabs"] = useTabs; j["keywordCase"] = keywordCase; j["style"] = style; j["commaLeading"] = commaLeading; j["andOrNewLine"] = andOrNewLine; j["oneItemPerLine"] = oneItemPerLine; j["maxLine"] = maxLine;
    j["contIndent"] = contIndent; j["andOrIndent"] = andOrIndent; j["joinIndent"] = joinIndent; j["kwAloneWhenMany"] = kwAloneWhenMany; j["onNewLine"] = onNewLine; j["spaceAfterComma"] = spaceAfterComma; j["spaceInParens"] = spaceInParens;
    j["functionCase"] = functionCase; j["identifierCase"] = identifierCase; j["alignDeclarations"] = alignDeclarations; j["alignAssignments"] = alignAssignments; j["blankBeforeException"] = blankBeforeException; j["blankAfterDeclare"] = blankAfterDeclare;
    j["app"] = "SQLToolsMac-format"; return QString::fromUtf8(QJsonDocument(j).toJson(QJsonDocument::Indented));
}

FormatOptions FormatOptions::fromJson (const QString& json, bool* ok)
{
    FormatOptions o; const QJsonObject j = QJsonDocument::fromJson(json.toUtf8()).object(); if (ok) *ok = j.value("app").toString() == "SQLToolsMac-format";
    auto I = [&](const char* k, int& v) { if (j.contains(k)) v = j[k].toInt(v); }; auto B = [&](const char* k, bool& v) { if (j.contains(k)) v = j[k].toBool(v); };
    I("indent", o.indent); B("useTabs", o.useTabs); I("keywordCase", o.keywordCase); I("style", o.style); B("commaLeading", o.commaLeading); B("andOrNewLine", o.andOrNewLine); B("oneItemPerLine", o.oneItemPerLine); I("maxLine", o.maxLine);
    I("contIndent", o.contIndent); I("andOrIndent", o.andOrIndent); I("joinIndent", o.joinIndent); B("kwAloneWhenMany", o.kwAloneWhenMany); B("onNewLine", o.onNewLine); B("spaceAfterComma", o.spaceAfterComma); B("spaceInParens", o.spaceInParens);
    I("functionCase", o.functionCase); I("identifierCase", o.identifierCase); B("alignDeclarations", o.alignDeclarations); B("alignAssignments", o.alignAssignments); B("blankBeforeException", o.blankBeforeException); B("blankAfterDeclare", o.blankAfterDeclare);
    return o;
}

QStringList FormatOptions::profileNames () { QJsonObject j = QJsonDocument::fromJson(QSettings().value("format/profiles").toString().toUtf8()).object(); QStringList n = j.keys(); n.sort(Qt::CaseInsensitive); return n; }
void FormatOptions::saveProfile (const QString& name, const FormatOptions& o)
{
    QSettings s; QJsonObject j = QJsonDocument::fromJson(s.value("format/profiles").toString().toUtf8()).object(); j[name] = QJsonDocument::fromJson(o.toJson().toUtf8()).object();
    s.setValue("format/profiles", QString::fromUtf8(QJsonDocument(j).toJson(QJsonDocument::Compact)));
}
bool FormatOptions::loadProfile (const QString& name, FormatOptions& o)
{
    const QJsonObject j = QJsonDocument::fromJson(QSettings().value("format/profiles").toString().toUtf8()).object(); if (!j.contains(name)) return false;
    QJsonObject x = j[name].toObject(); x["app"] = "SQLToolsMac-format"; o = fromJson(QString::fromUtf8(QJsonDocument(x).toJson()), nullptr); return true;
}
void FormatOptions::deleteProfile (const QString& name)
{
    QSettings s; QJsonObject j = QJsonDocument::fromJson(s.value("format/profiles").toString().toUtf8()).object(); j.remove(name); s.setValue("format/profiles", QString::fromUtf8(QJsonDocument(j).toJson(QJsonDocument::Compact)));
}

namespace {

// ---------------- Léxico ----------------
enum TT { Word, Num, Str, QId, Op, Punct, LCom, BCom, Hint, Bind };
struct Tok { TT type = Word; QString text; int start = 0, len = 0; };

const QSet<QString>& reserved ()
{
    static const QSet<QString> k = [] { QSet<QString> s; for (const char* w : {
        "select","from","where","and","or","not","in","is","null","like","between","exists","order","by","group","having","union","all","intersect","minus","insert","into","values","update","set","delete","merge","using",
        "when","matched","then","else","elsif","end","case","create","replace","alter","drop","truncate","table","view","index","sequence","trigger","package","body","procedure","function","type","join","inner","left","right",
        "full","outer","cross","natural","on","as","distinct","unique","begin","declare","exception","loop","if","for","while","return","returning","cursor","open","fetch","close","commit","rollback","savepoint","raise","goto","exit","continue",
        "default","constraint","primary","key","foreign","references","check","grant","revoke","with","connect","start","prior","level","asc","desc","nulls","first","last","rows","row","only","over","partition","before","after","each",
        "new","old","referencing","out","pragma","bulk","collect","forall","execute","immediate","record","subtype","constant","of","to","add","modify","column","some","any","next","true","false","number","varchar2","varchar","char","date","timestamp",
        "clob","blob","boolean","pls_integer","binary_integer","rowtype","siblings","nocycle","exclusive","wait","nowait","skip","locked","limit","offset","cycle","interval","day","month","year","hour","minute","second","global","temporary","preserve",
        "lateral","apply","pivot","unpivot","model","keep","dense_rank","rank","within","lead","lag","range","unbounded","preceding","following","current","isolation","transaction","work","force","authid","definer","deterministic","pipelined","result_cache",
        "others","no_data_found","too_many_rows","dup_val_on_index","value_error","zero_divide","invalid_number","inserting","updating","deleting","parallel_enable","accessible","by","using","varray","nested","of","ref","object","under","final","instantiable","member","static","overriding","self","extends","is","not","enable","disable","novalidate","validate","initially","deferred","immediate" }) s.insert(w); return s; }();
    return k;
}
const QSet<QString>& functions ()
{
    static const QSet<QString> k = [] { QSet<QString> s; for (const char* w : {
        "count","sum","avg","min","max","nvl","nvl2","coalesce","decode","to_char","to_date","to_number","to_timestamp","substr","instr","length","upper","lower","initcap","trim","ltrim","rtrim","lpad","rpad","replace","translate","round","trunc",
        "sysdate","systimestamp","add_months","months_between","last_day","next_day","listagg","row_number","lag","lead","first_value","last_value","ntile","abs","ceil","floor","mod","power","sqrt","sign","greatest","least","nullif","cast",
        "regexp_like","regexp_substr","regexp_replace","regexp_instr","extract","user","uid","sys_context","dbms_output.put_line","raise_application_error","sqlerrm","sqlcode","rownum","rowid","sys_guid","json_value","json_query","json_table","xmlagg","xmlelement","to_clob","dbms_lob" }) s.insert(w); return s; }();
    return k;
}

QVector<Tok> tokenize (const QString& s)
{
    QVector<Tok> out; int i = 0; const int n = s.size();
    auto push = [&](TT t, int a, int b) { Tok k; k.type = t; k.start = a; k.len = b - a; k.text = s.mid(a, b - a); out.push_back(k); };
    while (i < n)
    {
        const QChar c = s[i];
        if (c.isSpace()) { ++i; continue; }
        if (c == '-' && i + 1 < n && s[i + 1] == '-') { int j = s.indexOf('\n', i); if (j < 0) j = n; int e = j; while (e > i && s[e - 1] == '\r') --e; push(LCom, i, e); i = j; continue; }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') { int j = s.indexOf("*/", i + 2); j = j < 0 ? n : j + 2; push(s.mid(i, 3) == "/*+" ? Hint : BCom, i, j); i = j; continue; }
        if ((c == 'q' || c == 'Q') && i + 2 < n && s[i + 1] == '\'' && !s[i + 2].isSpace()) {                // q'[ ... ]'
            const QChar o = s[i + 2]; const QChar cl = o == '[' ? ']' : o == '(' ? ')' : o == '{' ? '}' : o == '<' ? '>' : o; int j = i + 3;
            while (j + 1 < n && !(s[j] == cl && s[j + 1] == '\'')) ++j; j = qMin(n, j + 2); push(Str, i, j); i = j; continue; }
        if (c == '\'') { int j = i + 1; while (j < n) { if (s[j] == '\'') { if (j + 1 < n && s[j + 1] == '\'') { j += 2; continue; } break; } ++j; } j = qMin(n, j + 1); push(Str, i, j); i = j; continue; }
        if (c == '"') { int j = s.indexOf('"', i + 1); j = j < 0 ? n : j + 1; push(QId, i, j); i = j; continue; }
        if (c.isDigit() || (c == '.' && i + 1 < n && s[i + 1].isDigit() && (out.isEmpty() || (out.last().type != Word && out.last().type != QId && out.last().text != ")")))) {
            int j = i; while (j < n && (s[j].isDigit() || s[j] == '.')) { if (s[j] == '.' && j + 1 < n && s[j + 1] == '.') break; ++j; }
            if (j < n && (s[j] == 'e' || s[j] == 'E')) { int k = j + 1; if (k < n && (s[k] == '+' || s[k] == '-')) ++k; if (k < n && s[k].isDigit()) { while (k < n && s[k].isDigit()) ++k; j = k; } }
            if (j < n && (s[j] == 'f' || s[j] == 'F' || s[j] == 'd' || s[j] == 'D')) ++j;
            push(Num, i, j); i = j; continue; }
        if (c.isLetter() || c == '_') { int j = i; while (j < n && (s[j].isLetterOrNumber() || s[j] == '_' || s[j] == '$' || s[j] == '#')) ++j; push(Word, i, j); i = j; continue; }
        if (c == ':' && i + 1 < n && (s[i + 1].isLetterOrNumber() || s[i + 1] == '_')) { int j = i + 1; while (j < n && (s[j].isLetterOrNumber() || s[j] == '_')) ++j; push(Bind, i, j); i = j; continue; }
        static const QStringList two = {":=", "=>", "..", "||", "<>", "!=", ">=", "<=", "**", "~="};
        bool done = false; for (const QString& t : two) if (s.mid(i, 2) == t) { push(Op, i, i + 2); i += 2; done = true; break; }
        if (done) continue;
        if (QString(",;()").contains(c)) { push(Punct, i, i + 1); ++i; continue; }
        push(Op, i, i + 1); ++i;                                         // + - * / = < > % @ . etc.
    }
    return out;
}

QString lowerOf (const Tok& t) { return t.type == Word ? t.text.toLower() : QString(); }
bool isW (const Tok& t, const char* w) { return t.type == Word && t.text.compare(QLatin1String(w), Qt::CaseInsensitive) == 0; }

QString indentStr (int cols, const FormatOptions& o)
{
    if (cols <= 0) return QString();
    if (o.useTabs && o.indent > 0) return QString(cols / o.indent, '\t') + QString(cols % o.indent, ' ');
    return QString(cols, ' ');
}

QString applyCase (const QString& w, int mode) { return mode == FormatOptions::Upper ? w.toUpper() : mode == FormatOptions::Lower ? w.toLower() : w; }

// ---------------- Árbol de paréntesis ----------------
struct Item { Tok t; bool group = false; QVector<Item> kids; };

QVector<Item> buildTree (const QVector<Tok>& toks, int& i, bool inGroup)
{
    QVector<Item> out;
    while (i < toks.size())
    {
        const Tok& t = toks[i];
        if (t.type == Punct && t.text == "(") { ++i; Item g; g.t = t; g.group = true; g.kids = buildTree(toks, i, true); out.push_back(g); continue; }
        if (t.type == Punct && t.text == ")") { if (inGroup) { ++i; return out; } ++i; continue; }
        Item it; it.t = t; out.push_back(it); ++i;
    }
    return out;
}

// ---------------- Formateador SQL ----------------
class Fmt
{
public:
    explicit Fmt (const FormatOptions& o) : o(o) {}
    QStringList query (const QVector<Item>& items, int base);
private:
    const FormatOptions& o;
    struct Clause { QString key; QString kw; QVector<Item> body; };

    QString kwText (const QString& w) const { return applyCase(w, o.keywordCase); }
    QString word (const Tok& t, bool callLike) const
    {
        if (t.type != Word) return t.text;
        const QString l = t.text.toLower(); const int fnCase = o.functionCase < 0 ? o.keywordCase : o.functionCase;
        if (reserved().contains(l)) return applyCase(t.text, o.keywordCase);
        if ((callLike && functions().contains(l)) || l == "sysdate" || l == "systimestamp" || l == "rownum" || l == "rowid") return applyCase(t.text, fnCase);
        return o.identifierCase == FormatOptions::Keep ? t.text : applyCase(t.text, o.identifierCase);
    }
    bool isSubquery (const Item& g) const { return g.group && !g.kids.isEmpty() && !g.kids[0].group && g.kids[0].t.type == Word && (isW(g.kids[0].t, "select") || isW(g.kids[0].t, "with")); }
    static bool isComma (const Item& i) { return !i.group && i.t.type == Punct && i.t.text == ","; }

    QString inlineText (const QVector<Item>& items, int col, bool spaceBeforeFirstParen = false);
    QStringList splitTop (const QVector<Item>& items, bool commas, int col, bool spaceParen);
    QVector<QVector<Item>> splitCommas (const QVector<Item>& items) const;
    QStringList condLines (const QVector<Item>& items, int col, int andCol, bool breaks);
    int matchClause (const QVector<Item>& items, int i, const QString& cur, int caseDepth, bool isMerge, QString& key, QString& kw) const;
    bool needSpace (const Item* prev, const Item& next, bool unaryPrev) const;
};

bool Fmt::needSpace (const Item* prev, const Item& next, bool unaryPrev) const
{
    if (!prev) return false;
    if (unaryPrev) return false;
    const Tok& p = prev->t; const Tok& n = next.t;
    if (!next.group) {
        if (n.type == Punct && (n.text == "," || n.text == ";")) return false;
        if (n.type == Op && (n.text == "." || n.text == "@" || n.text == "%")) return false;
        if (n.type == Op && n.text == ".." ) return false;
    }
    if (!prev->group) {
        if (p.type == Op && (p.text == "." || p.text == "@" || p.text == "%" || p.text == "..")) return false;
        if (p.type == Punct && p.text == "(") return false;
    }
    if (next.group)    // "(" : sin espacio tras un nombre de función o identificador; con espacio tras palabras reservadas y operadores
    {
        if (prev->group) return true;
        if (p.type == Word) { const QString l = p.text.toLower(); if (reserved().contains(l) && l != "left" && l != "right" && l != "replace" && l != "next" && l != "level" && l != "first" && l != "last" && l != "row" && l != "rank" && l != "dense_rank" && l != "lead" && l != "lag" && l != "range" && l != "interval" && l != "current" && l != "keep" && l != "model" && l != "number" && l != "varchar2" && l != "char" && l != "date" && l != "timestamp" && l != "to" && l != "end" ) return true; return false; }
        if (p.type == QId) return false;
        if (p.type == Bind || p.type == Num || p.type == Str) return true;
        if (p.type == Op && p.text == "%") return false;
        return true;
    }
    return true;
}

// Texto en una sola línea (salvo subconsultas, comentarios de línea y paréntesis muy largos). col = columna de continuación.
QString Fmt::inlineText (const QVector<Item>& items, int col, bool spaceBeforeFirstParen)
{
    QString out; const Item* prev = nullptr; bool prevUnary = false; bool firstParen = true;
    for (int i = 0; i < items.size(); ++i)
    {
        const Item& it = items[i];
        if (!it.group && it.t.type == Punct && it.t.text == ",") { out += o.spaceAfterComma ? ", " : ","; prev = &it; prevUnary = false; continue; }   // coma dentro de una expresión
        bool space = needSpace(prev, it, prevUnary);
        if (it.group && spaceBeforeFirstParen && firstParen && prev) space = true;
        if (it.group) firstParen = false;
        QString piece; bool unary = false;
        if (it.group)
        {
            if (isSubquery(it))
            {
                const QStringList inner = query(it.kids, col + o.indent);
                piece = "(\n" + inner.join("\n") + "\n" + indentStr(col, o) + ")";
            }
            else
            {
                // grupo normal: elementos separados por comas; si no cabe en la línea se parte una por línea
                const auto parts = splitCommas(it.kids); QStringList flat; for (const auto& p : parts) flat << inlineText(p, col);
                const QString oneLine = (o.spaceInParens ? "( " : "(") + flat.join(o.spaceAfterComma ? ", " : ",") + (o.spaceInParens ? " )" : ")");
                const int lastLen = out.contains('\n') ? out.size() - out.lastIndexOf('\n') - 1 : col + out.size();
                if (flat.size() > 1 && !oneLine.contains('\n') && lastLen + oneLine.size() > o.maxLine)
                {
                    QStringList ls; for (const QString& f : flat) ls << indentStr(col + o.indent, o) + f;
                    piece = "(\n" + ls.join(",\n") + "\n" + indentStr(col, o) + ")";
                }
                else piece = oneLine;
            }
        }
        else
        {
            const Tok& t = it.t;
            if (t.type == Word) { const bool call = (i + 1 < items.size() && items[i + 1].group); piece = word(t, call || functions().contains(t.text.toLower())); }
            else if (t.type == LCom) { piece = t.text; }
            else piece = t.text;
            if (t.type == Op && (t.text == "-" || t.text == "+"))
            {   // signo unario: tras un operador, una coma, un paréntesis, el inicio o una palabra reservada que espera un valor
                const bool afterValue = prev && ((prev->group) || (prev->t.type != Op && !(prev->t.type == Punct && (prev->t.text == "," || prev->t.text == "(")) && !(prev->t.type == Word && reserved().contains(prev->t.text.toLower()) && prev->t.text.compare("end", Qt::CaseInsensitive) != 0 && prev->t.text.compare("null", Qt::CaseInsensitive) != 0)));
                unary = !afterValue;
            }
            if (t.type == Op && t.text == "*") { const bool operand = !prev || (prev->t.type == Op && prev->t.text == ".") || (prev->t.type == Punct && prev->t.text == ",") || (!prev->group && prev->t.type == Word && isW(prev->t, "select")) || (!prev->group && prev->t.type == Word && isW(prev->t, "distinct")); if (operand && prev && prev->t.text == ".") space = false; }
        }
        if (space) out += ' ';
        out += piece;
        // un comentario de línea obliga a seguir en la línea siguiente
        if (!it.group && it.t.type == LCom && i + 1 < items.size()) out += "\n" + indentStr(col, o);
        prev = &it; prevUnary = unary;
    }
    return out;
}

QVector<QVector<Item>> Fmt::splitCommas (const QVector<Item>& items) const
{
    QVector<QVector<Item>> out(1);
    for (const Item& it : items) { if (isComma(it)) out.push_back({}); else out.last().push_back(it); }
    if (out.size() > 1 && out.last().isEmpty()) out.pop_back();
    return out;
}

int Fmt::matchClause (const QVector<Item>& items, int i, const QString& cur, int caseDepth, bool isMerge, QString& key, QString& kw) const
{
    const Item& it = items[i]; if (it.group || it.t.type != Word || caseDepth > 0) return 0;
    const QString w = it.t.text.toLower();
    auto nxt = [&](int d) -> QString { return i + d < items.size() && !items[i + d].group && items[i + d].t.type == Word ? items[i + d].t.text.toLower() : QString(); };
    auto set = [&](const QString& k, const QString& d, int n) { key = k; kw = d; return n; };
    if (w == "select") return set("select", "select", 1);
    if (w == "from" && cur != "delete") return set("from", "from", 1);
    if (w == "where") return set("where", "where", 1);
    if (w == "having") return set("having", "having", 1);
    if (w == "group" && nxt(1) == "by") return set("group by", "group by", 2);
    if (w == "order" && nxt(1) == "by") return set("order by", "order by", 2);
    if (w == "order" && nxt(1) == "siblings" && nxt(2) == "by") return set("order by", "order siblings by", 3);
    if (w == "connect" && nxt(1) == "by") return set("connect by", "connect by", 2);
    if (w == "start" && nxt(1) == "with") return set("start with", "start with", 2);
    if (w == "union" && nxt(1) == "all") return set("union", "union all", 2);
    if (w == "union" || w == "intersect" || w == "minus") return set("union", w, 1);
    if (w == "insert") return set("insert", "insert", 1);
    if (w == "update" && cur != "for") return set("update", "update", 1);
    if (w == "delete") return set("delete", "delete", 1);
    if (w == "merge") return set("merge", "merge", 1);
    if (w == "set" && (cur == "update" || cur == "merge" || cur == "when" || cur == "using" || cur == "on" || cur == "update-in-merge")) return set("set", "set", 1);
    if (w == "values") return set("values", "values", 1);
    if (w == "using" && isMerge) return set("using", "using", 1);
    if (w == "on" && isMerge && (cur == "using")) return set("on", "on", 1);
    if (w == "returning" || (w == "return" && false)) return set("returning", "returning", 1);
    if (w == "bulk" && nxt(1) == "collect" && nxt(2) == "into") return set("into", "bulk collect into", 3);
    if (w == "into" && (cur == "select")) return set("into", "into", 1);
    if (w == "for" && nxt(1) == "update") return set("for", "for update", 2);
    if (w == "fetch" && (nxt(1) == "first" || nxt(1) == "next")) return set("fetch", "fetch", 1);
    if (w == "with" && i == 0) return set("with", "with", 1);
    if (w == "when" && isMerge && (nxt(1) == "matched" || nxt(1) == "not")) { key = "when"; kw = "when"; int n = 1; while (i + n < items.size() && !(items[i + n].t.type == Word && !items[i + n].group && isW(items[i + n].t, "then"))) ++n; kw = "when"; return n + 1; }
    return 0;
}

// Condiciones: parte por AND / OR (no por el AND de un BETWEEN). Devuelve líneas; la primera sin sangría.
QStringList Fmt::condLines (const QVector<Item>& items, int col, int andCol, bool breaks)
{
    QVector<QVector<Item>> parts; QStringList conns; parts.push_back({}); bool between = false;
    for (const Item& it : items)
    {
        if (!it.group && it.t.type == Word)
        {
            if (isW(it.t, "between")) between = true;
            if (breaks && (isW(it.t, "and") || isW(it.t, "or")))
            {
                if (isW(it.t, "and") && between) { between = false; parts.last().push_back(it); continue; }
                conns << kwText(it.t.text); parts.push_back({}); continue;
            }
        }
        parts.last().push_back(it);
    }
    QStringList out; for (int i = 0; i < parts.size(); ++i) out << ((i ? conns[i - 1] + " " : QString()) + inlineText(parts[i], col));
    Q_UNUSED(andCol);
    return out;
}

QStringList Fmt::query (const QVector<Item>& items, int base)
{
    // 1) partir en cláusulas
    QVector<Clause> clauses; QString cur; int caseDepth = 0; bool isMerge = false;
    for (const Item& it : items) if (!it.group && it.t.type == Word && isW(it.t, "merge")) { isMerge = true; break; }
    Clause pre; // contenido antes de la primera cláusula (p.ej. comentarios)
    for (int i = 0; i < items.size();)
    {
        const Item& it = items[i];
        if (!it.group && it.t.type == Word) { if (isW(it.t, "case")) ++caseDepth; else if (isW(it.t, "end") && caseDepth > 0) --caseDepth; }
        QString key, kw; const int n = matchClause(items, i, cur, caseDepth, isMerge, key, kw);
        if (n > 0)
        {
            Clause c; c.key = key; c.kw = kw; if (key == "when") { QStringList ws; for (int k = 0; k < n; ++k) ws << items[i + k].t.text; c.kw = ws.join(' '); }
            clauses.push_back(c); cur = (key == "update" && isMerge) ? "update-in-merge" : key; i += n; continue;
        }
        if (clauses.isEmpty()) pre.body.push_back(it); else clauses.last().body.push_back(it);
        ++i;
    }
    QStringList lines;
    if (!pre.body.isEmpty()) lines << indentStr(base, o) + inlineText(pre.body, base);
    const bool river = o.style == FormatOptions::River; const int W = 6;
    const int selectBase = base;
    for (const Clause& c : clauses)
    {
        const QString kwDisp = [&]{ QStringList ws = c.kw.split(' '); for (QString& w : ws) w = kwText(w); return ws.join(' '); }();
        const QString first = c.kw.section(' ', 0, 0);
        const bool setOp = c.key == "union";
        const int pad = (river && !setOp) ? qMax(0, W - int(first.size())) : 0;
        const int kwCol = selectBase + pad; const int contCol = river ? kwCol + kwDisp.size() + 1 : selectBase + (o.contIndent >= 0 ? o.contIndent : o.indent);
        const bool listLike = (c.key == "select" || c.key == "group by" || c.key == "order by" || c.key == "set" || c.key == "from" || c.key == "into" || c.key == "with");
        const bool condLike = (c.key == "where" || c.key == "having" || c.key == "on" || c.key == "start with" || c.key == "connect by");
        if (setOp) { lines << indentStr(selectBase, o) + kwDisp; continue; }
        if (c.body.isEmpty()) { lines << indentStr(kwCol, o) + kwDisp; continue; }
        if (listLike)
        {
            auto parts = splitCommas(c.body); const bool many = parts.size() > 1 && o.oneItemPerLine;
            // FROM: cada elemento puede traer uniones (JOIN) en líneas propias
            auto renderElem = [&](const QVector<Item>& el, int col) -> QString {
                if (c.key != "from") return inlineText(el, col);
                QVector<QVector<Item>> segs(1); QStringList joinKw; segs.reserve(8);
                for (int k = 0; k < el.size(); ++k)
                {
                    const Item& x = el[k]; bool starts = false;
                    if (!x.group && x.t.type == Word) { const QString l = x.t.text.toLower();
                        if (l == "join" && !(k > 0 && !el[k - 1].group && el[k - 1].t.type == Word && (isW(el[k - 1].t, "left") || isW(el[k - 1].t, "right") || isW(el[k - 1].t, "inner") || isW(el[k - 1].t, "full") || isW(el[k - 1].t, "cross") || isW(el[k - 1].t, "outer") || isW(el[k - 1].t, "natural")))) starts = true;
                        if ((l == "left" || l == "right" || l == "inner" || l == "full" || l == "cross" || l == "natural") && !(k > 0 && !el[k - 1].group && el[k - 1].t.type == Word && (isW(el[k - 1].t, "left") || isW(el[k - 1].t, "right") || isW(el[k - 1].t, "inner") || isW(el[k - 1].t, "full") || isW(el[k - 1].t, "natural")))) { for (int m = k; m < el.size() && !el[m].group; ++m) { if (isW(el[m].t, "join")) { starts = true; break; } if (!(isW(el[m].t, "left") || isW(el[m].t, "right") || isW(el[m].t, "inner") || isW(el[m].t, "full") || isW(el[m].t, "outer") || isW(el[m].t, "cross") || isW(el[m].t, "natural"))) break; } }
                    }
                    if (starts) segs.push_back({});
                    segs.last().push_back(x);
                }
                const int jc = o.joinIndent >= 0 ? selectBase + o.joinIndent : (river ? kwCol : selectBase + o.indent);
                QStringList ls; for (int s = 0; s < segs.size(); ++s)
                {
                    // dentro de cada unión, el ON y sus AND
                    QVector<Item> pre2, onPart; bool inOn = false;
                    for (const Item& x : segs[s]) { if (!x.group && isW(x.t, "on") && !inOn) { inOn = true; continue; } (inOn ? onPart : pre2).push_back(x); }
                    QString line = inlineText(pre2, s ? jc : col);
                    if (inOn) { const QStringList cl = condLines(onPart, jc + o.indent, jc, o.andOrNewLine);
                        if (o.onNewLine) { line += "\n" + indentStr(jc + o.indent, o) + kwText("on") + " " + cl[0]; for (int q = 1; q < cl.size(); ++q) line += "\n" + indentStr(jc + o.indent * 2, o) + cl[q]; }
                        else { line += " " + kwText("on") + " " + cl[0]; for (int q = 1; q < cl.size(); ++q) line += "\n" + indentStr(jc + o.indent, o) + cl[q]; } }
                    ls << (s ? indentStr(jc, o) : QString()) + line;
                }
                return ls.join("\n");
            };
            if (river || !many || !o.kwAloneWhenMany)
            {
                const int ec = river ? contCol : contCol;
                for (int p = 0; p < parts.size(); ++p)
                {
                    const QString e = renderElem(parts[p], p == 0 ? kwCol : ec); const QString sep = p + 1 < parts.size() && !o.commaLeading ? "," : QString();
                    if (p == 0) lines << indentStr(kwCol, o) + kwDisp + " " + (o.commaLeading && river && parts.size() > 1 ? QString() : QString()) + e + sep;
                    else lines << (o.commaLeading ? indentStr(qMax(0, ec - 2), o) + ", " : indentStr(ec, o)) + e + sep;
                }
            }
            else
            {   // estándar con varios elementos: la palabra sola y cada elemento sangrado
                lines << indentStr(kwCol, o) + kwDisp;
                for (int p = 0; p < parts.size(); ++p)
                {
                    const QString e = renderElem(parts[p], contCol); const QString sep = p + 1 < parts.size() && !o.commaLeading ? "," : QString();
                    lines << indentStr(contCol, o) + (o.commaLeading && p > 0 ? QString(", ") : QString()) + e + sep;
                }
            }
            continue;
        }
        if (condLike)
        {
            const QStringList cl = condLines(c.body, kwCol, kwCol, o.andOrNewLine);
            lines << indentStr(kwCol, o) + kwDisp + " " + cl[0];
            for (int p = 1; p < cl.size(); ++p)
            {
                const QString conn = cl[p].section(' ', 0, 0);
                const int col = o.andOrIndent >= 0 ? selectBase + o.andOrIndent : (river ? selectBase + qMax(0, W - int(conn.size())) : selectBase + o.indent);
                lines << indentStr(col, o) + cl[p];
            }
            continue;
        }
        // resto: en una línea (INSERT INTO t (cols), VALUES (…), UPDATE t, DELETE FROM t…)
        const bool parenSpace = (c.key == "insert" || c.key == "values" || c.key == "merge");
        lines << indentStr(kwCol, o) + kwDisp + " " + inlineText(c.body, kwCol, parenSpace);
    }
    return lines;
}

} // namespace

// ---------------- API: SQL ----------------
QString SqlFormatter::formatSql (const QString& statement, const FormatOptions& opt, int baseIndent)
{
    QString s = statement.trimmed(); bool semi = false; if (s.endsWith(';')) { s.chop(1); semi = true; }
    const QVector<Tok> toks = tokenize(s); int i = 0; const QVector<Item> tree = buildTree(toks, i, false);
    Fmt f(opt); QStringList lines = f.query(tree, baseIndent);
    QString out = lines.join("\n"); if (semi) out += ";";
    return out;
}

// ---------------- API: PL/SQL ----------------
namespace {

struct Blk { QString type; int depth; bool exc = false; };

// ¿la línea (sin comentarios ni textos) termina en ';' con los paréntesis cerrados?
bool endsStmt (const QString& line, int& parenDepth)
{
    const auto toks = tokenize(line); QString lastCode;
    for (const Tok& t : toks) { if (t.type == LCom || t.type == BCom) continue; if (t.type == Punct && t.text == "(") ++parenDepth; else if (t.type == Punct && t.text == ")") --parenDepth; lastCode = t.text; }
    return lastCode == ";" && parenDepth <= 0;
}

QString caseWords (const QString& line, const FormatOptions& o)
{
    if (o.keywordCase == FormatOptions::Keep && o.functionCase < 0 && o.identifierCase == FormatOptions::Keep) return line;
    const auto toks = tokenize(line); QString out = line; int shift = 0;
    for (int k = 0; k < toks.size(); ++k)
    {
        const Tok& t = toks[k]; if (t.type != Word) continue;
        const QString l = t.text.toLower(); const bool afterDot = k > 0 && toks[k - 1].text == "." && toks[k - 1].type == Op;
        const bool attr = k > 0 && toks[k - 1].text == "%" && toks[k - 1].type == Op;
        const bool isCall = k + 1 < toks.size() && toks[k + 1].text == "(";
        if (afterDot) continue;
        const int fnCase = o.functionCase < 0 ? o.keywordCase : o.functionCase; QString r; bool change = false;
        if (reserved().contains(l) || attr) { r = applyCase(t.text, o.keywordCase); change = true; }
        else if ((isCall && functions().contains(l)) || l == "sysdate" || l == "systimestamp" || l == "sqlerrm" || l == "sqlcode" || l == "rownum") { r = applyCase(t.text, fnCase); change = true; }
        else if (o.identifierCase != FormatOptions::Keep) { r = applyCase(t.text, o.identifierCase); change = true; }
        if (change) { out.replace(t.start + shift, t.len, r); shift += r.size() - t.len; }
    }
    return out;
}

bool hasWord (const QVector<Tok>& toks, const char* w, int* parenOut = nullptr)
{
    int depth = 0; for (const Tok& t : toks) { if (t.type == Punct && t.text == "(") ++depth; else if (t.type == Punct && t.text == ")") --depth; else if (depth == 0 && isW(t, w)) { if (parenOut) *parenOut = depth; return true; } }
    return false;
}

} // namespace

QString SqlFormatter::formatPlsql (const QString& code, const FormatOptions& o)
{
    const QStringList src = code.split('\n'); QStringList out; QVector<Blk> st;
    auto cur = [&]() { return st.isEmpty() ? 0 : st.last().depth + (st.last().exc ? 1 : 0); };
    auto ind = [&](int levels) { return indentStr(qMax(0, levels) * o.indent, o); };
    auto origIndent = [](const QString& l) { int n = 0; for (QChar c : l) { if (c == ' ') ++n; else if (c == '\t') n += 4; else break; } return n; };
    enum Wait { None, Then, LoopW, IsAs, TrigBegin } wait = None; bool inStmt = false; int stmtParen = 0; int stmtOrig = 0; int stmtLevel = 0; int caseExpr = 0; bool lastBlank = false;
    bool trigHeader = false; QSet<int> declLines, assignLines;
    for (int li = 0; li < src.size(); ++li)
    {
        QString raw = src[li]; while (raw.endsWith('\r')) raw.chop(1);
        const QString t = raw.trimmed();
        if (t.isEmpty()) { if (!lastBlank && !out.isEmpty()) out << QString(); lastBlank = true; continue; }
        lastBlank = false;
        const auto toks = tokenize(t);
        QVector<Tok> code; for (const Tok& k : toks) if (k.type != LCom && k.type != BCom) code.push_back(k);
        // línea solo de comentario
        if (code.isEmpty()) { out << ind(inStmt ? stmtLevel + 1 : cur()) + t; continue; }
        const QString w1 = lowerOf(code[0]), w2 = code.size() > 1 ? lowerOf(code[1]) : QString();

        if (wait == TrigBegin && (w1 == "declare" || w1 == "begin")) { wait = None; inStmt = false; }      // fin de la cabecera del trigger
        // --- continuación de una sentencia o de una cabecera
        if (inStmt || wait != None)
        {
            const int rel = wait == None ? qMax(0, origIndent(raw) - stmtOrig) : 0;
            const QString line = caseWords(t, o);
            out << ind(stmtLevel + 1) + (rel > 0 ? QString(qMin(rel, 40), ' ') : QString()) + line;
            int pd = stmtParen; const bool ended = endsStmt(t, pd); stmtParen = pd;
            // contadores de CASE como expresión
            for (const Tok& k : code) { if (isW(k, "case")) ++caseExpr; else if (isW(k, "end") && caseExpr > 0) --caseExpr; }
            if (wait == Then && hasWord(code, "then")) { wait = None; inStmt = false; }
            else if (wait == LoopW && hasWord(code, "loop")) { wait = None; inStmt = false; }
            else if (wait == IsAs && (hasWord(code, "is") || hasWord(code, "as"))) { wait = None; inStmt = false; st.push_back({"decl", stmtLevel + 1}); }
            else if (wait == TrigBegin) { /* se resuelve en DECLARE/BEGIN, que no llegan aquí */ }
            else if (wait == None && ended) { inStmt = false; }
            continue;
        }

        // --- sentencias SQL (DML): se formatean con el formateador de SQL
        if ((w1 == "select" || w1 == "insert" || w1 == "update" || w1 == "delete" || w1 == "merge" || (w1 == "with" && w2 != "")) && !(w1 == "delete" && code.size() > 1 && code[1].text == "."))
        {
            QStringList block; int pd = 0; int j = li; bool done = false;
            while (j < src.size() && !done) { QString l = src[j]; while (l.endsWith('\r')) l.chop(1); block << l; int p2 = pd; if (endsStmt(l, p2)) done = true; pd = p2; ++j; }
            if (done)
            {
                const QString joined = block.join("\n");
                const QString f = SqlFormatter::formatSql(joined, o, cur() * o.indent);
                bool hasLineComment = false; for (const Tok& k : tokenize(joined)) if (k.type == LCom) hasLineComment = true;
                (void)hasLineComment;
                out << f; li = j - 1; continue;
            }
        }

        // --- estructura de bloques
        auto line = [&](int level) { out << ind(level) + caseWords(t, o); };
        auto startsStmt = [&]() { int pd = 0; if (!endsStmt(t, pd)) { inStmt = true; stmtParen = pd; stmtOrig = origIndent(raw); stmtLevel = cur(); } };
        if (w1 == "create" || w1 == "procedure" || w1 == "function" || w1 == "package" || w1 == "trigger")
        {
            const bool isTrig = (w1 == "trigger") || (w1 == "create" && (hasWord(code, "trigger")));
            line(cur()); int pd = 0; const bool semi = endsStmt(t, pd);
            const bool isAs = hasWord(code, "is") || hasWord(code, "as");
            if (isTrig && !semi) { wait = TrigBegin; trigHeader = true; stmtLevel = cur(); stmtOrig = origIndent(raw); }
            else if (isAs) st.push_back({"decl", cur() + 1});
            else if (!semi) { wait = IsAs; stmtLevel = cur(); stmtOrig = origIndent(raw); stmtParen = pd; }
            continue;
        }
        if (w1 == "declare") { if (trigHeader) { wait = None; trigHeader = false; } line(cur()); st.push_back({"decl", cur() + 1}); continue; }
        if (w1 == "begin")
        {
            if (trigHeader) { wait = None; trigHeader = false; }
            if (!st.isEmpty() && st.last().type == "decl") { const int d = st.last().depth; if (o.blankAfterDeclare && !out.isEmpty() && !out.last().isEmpty()) out << QString(); out << ind(d - 1) + caseWords(t, o); st.last().type = "body"; st.last().depth = d; }
            else { line(cur()); st.push_back({"body", cur() + 1}); }
            continue;
        }
        if (w1 == "exception" && !st.isEmpty()) { if (o.blankBeforeException && !out.isEmpty() && !out.last().isEmpty()) out << QString(); out << ind(st.last().depth - 1) + caseWords(t, o); st.last().type = "exc"; st.last().exc = false; continue; }
        if (w1 == "when" && !st.isEmpty() && (st.last().type == "exc" || st.last().type == "case")) { out << ind(st.last().depth) + caseWords(t, o); st.last().exc = true; if (!hasWord(code, "then")) { wait = Then; stmtLevel = st.last().depth; stmtOrig = origIndent(raw); } continue; }
        if (w1 == "if")
        {
            line(cur()); st.push_back({"if", cur() + 1}); if (!hasWord(code, "then")) { wait = Then; stmtLevel = cur() - 1; stmtOrig = origIndent(raw); } continue;
        }
        if ((w1 == "elsif" || w1 == "elseif") && !st.isEmpty() && st.last().type == "if") { out << ind(st.last().depth - 1) + caseWords(t, o); if (!hasWord(code, "then")) { wait = Then; stmtLevel = st.last().depth - 1; stmtOrig = origIndent(raw); } continue; }
        if (w1 == "else" && !st.isEmpty() && st.last().type == "if") { out << ind(st.last().depth - 1) + caseWords(t, o); continue; }
        if (w1 == "else" && !st.isEmpty() && st.last().type == "case") { out << ind(st.last().depth) + caseWords(t, o); st.last().exc = true; continue; }
        if (w1 == "loop") { line(cur()); st.push_back({"loop", cur() + 1}); continue; }
        if ((w1 == "for" || w1 == "while") && !(w1 == "for" && w2 == "update"))
        {
            line(cur());
            if (hasWord(code, "loop")) st.push_back({"loop", cur() + 1});
            else { st.push_back({"loop", cur() + 1}); wait = LoopW; stmtLevel = cur() - 1; stmtOrig = origIndent(raw); }
            continue;
        }
        if (w1 == "case") { line(cur()); st.push_back({"case", cur() + 1}); continue; }
        if (w1 == "end")
        {
            const bool typed = (w2 == "if" || w2 == "loop" || w2 == "case");
            if (!st.isEmpty())
            {
                const int d = st.last().depth; st.pop_back();
                out << ind(d - 1) + caseWords(t, o);
            }
            else out << caseWords(t, o);
            Q_UNUSED(typed);
            continue;
        }
        // sentencia normal
        line(cur());
        { static const QRegularExpression declRe(R"(^[A-Za-z_][\w$#]*\s+(?!(is|as)\b)\S.*;\s*$)", QRegularExpression::CaseInsensitiveOption);
          static const QRegularExpression asgRe(R"(^[A-Za-z_][^:]*?\s*:=\s*.+;\s*$)");
          static const QSet<QString> notDecl = {"cursor", "type", "subtype", "procedure", "function", "pragma", "exception", "return", "null", "raise", "open", "close", "fetch", "commit", "rollback", "exit", "continue", "goto", "execute", "forall", "if", "loop"};
          if (!st.isEmpty() && st.last().type == "decl" && !notDecl.contains(w1) && declRe.match(t).hasMatch()) declLines.insert(out.size() - 1);
          else if (asgRe.match(t).hasMatch() && !notDecl.contains(w1)) assignLines.insert(out.size() - 1); }
        startsStmt();
        for (const Tok& k : code) { if (isW(k, "case")) ++caseExpr; else if (isW(k, "end") && caseExpr > 0) --caseExpr; }
    }
    while (!out.isEmpty() && out.last().isEmpty()) out.removeLast();
    // alinear en columna las declaraciones y las asignaciones seguidas
    auto alignRuns = [&](const QSet<int>& set, const QRegularExpression& re, bool decl) {
        QList<int> idx = set.values(); std::sort(idx.begin(), idx.end());
        for (int i = 0; i < idx.size();)
        {
            int j = i; while (j + 1 < idx.size() && idx[j + 1] == idx[j] + 1) ++j;
            if (j > i)
            {
                int w = 0; QVector<QRegularExpressionMatch> ms;
                for (int k = i; k <= j; ++k) { ms << re.match(out[idx[k]]); w = qMax(w, ms.last().hasMatch() ? int(ms.last().captured(2).size()) : 0); }
                for (int k = i; k <= j; ++k) if (ms[k - i].hasMatch()) { const auto& m = ms[k - i]; out[idx[k]] = m.captured(1) + m.captured(2).leftJustified(w) + (decl ? " " : " := ") + m.captured(3); }
            }
            i = j + 1;
        }
    };
    if (o.alignDeclarations) alignRuns(declLines, QRegularExpression(R"(^(\s*)([A-Za-z_][\w$#]*)\s+(.*)$)"), true);
    if (o.alignAssignments) alignRuns(assignLines, QRegularExpression(R"(^(\s*)(.*?)\s*:=\s*(.*)$)"), false);
    return out.join("\n");
}

// ---------------- Deducir las opciones a partir de un ejemplo ya formateado ----------------
FormatOptions FormatOptions::inferFromExample (const QString& sample, QStringList* findings)
{
    FormatOptions o; QStringList say; const QStringList lines = sample.split('\n');
    auto indentOf = [](const QString& l) { int n = 0; for (QChar c : l) { if (c == ' ') ++n; else if (c == '\t') n += 4; else break; } return n; };
    auto firstWord = [](const QString& l) { return l.trimmed().section(QRegularExpression("[\\s(]"), 0, 0).toLower(); };
    // sangría: la diferencia positiva más frecuente entre líneas consecutivas
    QMap<int, int> deltas; int prev = -1; bool tabs = false;
    for (const QString& l : lines) { if (l.trimmed().isEmpty()) continue; if (l.startsWith('\t')) tabs = true; const int n = indentOf(l); if (prev >= 0 && n > prev && n - prev <= 8) deltas[n - prev]++; prev = n; }
    int best = 0, bestN = 0; for (auto it = deltas.begin(); it != deltas.end(); ++it) if (it.value() > bestN) { bestN = it.value(); best = it.key(); }
    if (best > 0) { o.indent = best; say << QString("Sangría: %1 espacios por nivel").arg(best); } if (tabs) { o.useTabs = true; say << "Sangría con tabuladores"; }
    // mayúsculas de las palabras reservadas
    int up = 0, lo = 0; for (const Tok& t : tokenize(sample)) if (t.type == Word && reserved().contains(t.text.toLower())) { if (t.text == t.text.toUpper()) ++up; else if (t.text == t.text.toLower()) ++lo; }
    if (up + lo > 0) { o.keywordCase = up >= lo ? FormatOptions::Upper : FormatOptions::Lower; say << (up >= lo ? "Palabras reservadas en MAYÚSCULAS" : "Palabras reservadas en minúsculas"); }
    // posiciones de las cláusulas
    int colSelect = -1, colFrom = -1, colWhere = -1, colAnd = -1, colJoin = -1, colItem = -1; bool selectAlone = false, leadingComma = false, onOwn = false, commaInline = false;
    for (int i = 0; i < lines.size(); ++i)
    {
        const QString t = lines[i].trimmed(); if (t.isEmpty()) continue; const QString w = firstWord(t); const int c = indentOf(lines[i]);
        if (t.startsWith(',')) leadingComma = true;
        if (w == "select" && colSelect < 0) { colSelect = c; const QString rest = t.mid(6).trimmed(); selectAlone = rest.isEmpty(); if (!rest.isEmpty() && rest.contains(',') && !rest.endsWith(',')) commaInline = true;
            if (selectAlone) for (int j = i + 1; j < lines.size(); ++j) if (!lines[j].trimmed().isEmpty()) { colItem = indentOf(lines[j]); break; } }
        else if (w == "from" && colFrom < 0 && colSelect >= 0) colFrom = c;
        else if (w == "where" && colWhere < 0) colWhere = c;
        else if ((w == "and" || w == "or") && colAnd < 0 && colWhere >= 0) colAnd = c;
        else if ((w == "join" || w == "left" || w == "inner" || w == "right" || w == "full") && colJoin < 0 && colFrom >= 0) colJoin = c;
        else if (w == "on" && colJoin >= 0) onOwn = true;
    }
    if (colSelect >= 0 && colFrom >= 0) { if (colFrom > colSelect) { o.style = River; say << "Estilo Riachuelo (FROM/WHERE alineadas por la derecha)"; } else { o.style = Standard; say << "Estilo Estándar (palabras a la izquierda)"; } }
    if (colWhere >= 0 && colAnd >= 0) { o.andOrIndent = qMax(0, colAnd - colWhere); say << QString("AND/OR a %1 columna(s) de WHERE").arg(o.andOrIndent); }
    if (colFrom >= 0 && colJoin >= 0) { o.joinIndent = qMax(0, colJoin - colFrom); say << QString("JOIN a %1 columna(s) de FROM").arg(o.joinIndent); }
    if (colSelect >= 0 && colItem >= 0 && selectAlone) { o.contIndent = qMax(0, colItem - colSelect); o.kwAloneWhenMany = true; say << QString("Columnas del SELECT a %1 columna(s) de la palabra").arg(o.contIndent); }
    else if (colSelect >= 0 && !selectAlone) { o.kwAloneWhenMany = false; say << "La primera columna va en la misma línea que SELECT"; }
    if (commaInline) { o.oneItemPerLine = false; say << "Varias columnas en la misma línea"; }
    if (leadingComma) { o.commaLeading = true; say << "Coma al principio de la línea"; }
    if (onOwn) { o.onNewLine = true; say << "ON en línea propia"; }
    o.spaceAfterComma = !sample.contains(QRegularExpression(",[A-Za-z0-9_']")); if (!o.spaceAfterComma) say << "Sin espacio después de la coma";
    o.spaceInParens = sample.contains(QRegularExpression("\\(\\s[A-Za-z0-9_']")) && !sample.contains(QRegularExpression("\\([A-Za-z0-9_']")); if (o.spaceInParens) say << "Espacios dentro de los paréntesis";
    // declaraciones alineadas: varias líneas seguidas «nombre   tipo;» con 2 o más espacios
    int alignedDecl = 0; for (const QString& l : lines) if (QRegularExpression(R"(^\s+[A-Za-z_][\w$#]*\s{2,}[A-Za-z_][^;]*;\s*$)").match(l).hasMatch()) ++alignedDecl;
    if (alignedDecl >= 2) { o.alignDeclarations = true; say << "Declaraciones alineadas en columna"; }
    int alignedAsg = 0; for (const QString& l : lines) if (QRegularExpression(R"(^\s+\S+\s{2,}:=\s*)").match(l).hasMatch()) ++alignedAsg;
    if (alignedAsg >= 2) { o.alignAssignments = true; say << "Asignaciones := alineadas"; }
    if (sample.contains(QRegularExpression("\\n\\s*\\n\\s*exception\\b", QRegularExpression::CaseInsensitiveOption))) { o.blankBeforeException = true; say << "Línea en blanco antes de EXCEPTION"; }
    if (sample.contains(QRegularExpression("\\n\\s*\\n\\s*begin\\b", QRegularExpression::CaseInsensitiveOption))) { o.blankAfterDeclare = true; say << "Línea en blanco antes de BEGIN"; }
    if (findings) *findings = say;
    return o;
}

// ---------------- API: texto con varias sentencias ----------------
QString SqlFormatter::format (const QString& text, const FormatOptions& opt)
{
    const auto stmts = SqlSplitter::split(text); QString out; int pos = 0;
    for (const auto& st : stmts)
    {
        { QString gap = text.mid(pos, st.start - pos); if (pos > 0 && !gap.contains('\n')) gap += "\n"; out += gap; }
        const QString body = text.mid(st.start, st.end - st.start);
        const QVector<Tok> toks = tokenize(body); QString w1; for (const Tok& t : toks) if (t.type == Word) { w1 = t.text.toLower(); break; }
        QString res;
        if (st.plsql || w1 == "begin" || w1 == "declare") res = SqlFormatter::formatPlsql(body, opt);
        else if (w1 == "select" || w1 == "with" || w1 == "insert" || w1 == "update" || w1 == "delete" || w1 == "merge") res = SqlFormatter::formatSql(body, opt, 0);
        else res = body;                                                             // DDL y otros: se dejan como están
        out += res; pos = st.end;
    }
    out += text.mid(pos);
    return out;
}
