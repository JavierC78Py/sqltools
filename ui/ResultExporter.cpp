#include "ResultExporter.h"
#include <QDateTime>
#include <QRegularExpression>
#include <QSettings>

QString ExportOptions::extension (int f) { static const char* e[] = {"csv", "tsv", "json", "xml", "sql", "html", "txt"}; return e[qBound(0, f, int(FormatCount) - 1)]; }
QString ExportOptions::formatName (int f) { static const char* n[] = {"CSV", "TSV (tabulador)", "JSON", "XML", "INSERT de SQL", "HTML", "Texto alineado"}; return n[qBound(0, f, int(FormatCount) - 1)]; }

QString ExportOptions::fileName () const
{
    QString n = filenameTemplate; const QDateTime now = QDateTime::currentDateTime();
    n.replace("{fecha}", now.toString("yyyyMMdd")).replace("{hora}", now.toString("HHmmss")).replace("{tabla}", sqlTable.toLower());
    n.remove(QRegularExpression("[/\\\\:*?\"<>|]")); return n.isEmpty() ? "resultados" : n;
}

ExportOptions ExportOptions::load (Destination d)
{
    QSettings s; ExportOptions o; const QString dst = d == File ? "file" : d == Clipboard ? "clip" : "editor";
    o.format = s.value("export/" + dst + "/format", d == Clipboard || d == Editor ? int(Tsv) : int(Csv)).toInt();
    o.header = s.value("export/" + dst + "/header", true).toBool(); o.jsonPretty = s.value("export/" + dst + "/jsonPretty", true).toBool();
    o.sep = s.value("export/sep", ",").toString(); o.quote = s.value("export/quote", "\"").toString(); o.escape = s.value("export/escape", "\"").toString();
    o.quoteAlways = s.value("export/quoteAlways", false).toBool(); o.nullText = s.value("export/nullText", "").toString();
    o.crlf = s.value("export/crlf", false).toBool(); o.bom = s.value("export/bom", false).toBool(); o.prefixChar = s.value("export/prefixChar", "").toString();
    o.xmlRoot = s.value("export/xmlRoot", "RESULT").toString(); o.xmlRecord = s.value("export/xmlRecord", "ROW").toString(); o.xmlCase = s.value("export/xmlCase", 0).toInt();
    o.sqlTable = s.value("export/sqlTable", "TABLE_NAME").toString(); o.ansiLiterals = s.value("export/ansi", false).toBool(); o.sqlDateFormat = s.value("export/sqlDateFormat", "YYYY.MM.DD HH24:MI:SS").toString();
    o.filenameTemplate = s.value("export/filenameTemplate", "resultados_{fecha}_{hora}").toString();
    return o;
}

void ExportOptions::save (Destination d) const
{
    QSettings s; const QString dst = d == File ? "file" : d == Clipboard ? "clip" : "editor";
    s.setValue("export/" + dst + "/format", format); s.setValue("export/" + dst + "/header", header); s.setValue("export/" + dst + "/jsonPretty", jsonPretty);
    s.setValue("export/sep", sep); s.setValue("export/quote", quote); s.setValue("export/escape", escape); s.setValue("export/quoteAlways", quoteAlways); s.setValue("export/nullText", nullText);
    s.setValue("export/crlf", crlf); s.setValue("export/bom", bom); s.setValue("export/prefixChar", prefixChar);
    s.setValue("export/xmlRoot", xmlRoot); s.setValue("export/xmlRecord", xmlRecord); s.setValue("export/xmlCase", xmlCase);
    s.setValue("export/sqlTable", sqlTable); s.setValue("export/ansi", ansiLiterals); s.setValue("export/sqlDateFormat", sqlDateFormat); s.setValue("export/filenameTemplate", filenameTemplate);
}

QString ResultExporter::jsonString (const QString& s)
{
    QString o = "\""; for (QChar c : s) { switch (c.unicode()) { case '"': o += "\\\""; break; case '\\': o += "\\\\"; break; case '\n': o += "\\n"; break; case '\r': o += "\\r"; break; case '\t': o += "\\t"; break;
        default: if (c.unicode() < 0x20) o += QString("\\u%1").arg(c.unicode(), 4, 16, QChar('0')); else o += c; } }
    return o + "\"";
}

QString ResultExporter::xmlName (const QString& name, int caseMode)
{
    QString n = name; n.replace(QRegularExpression("[^A-Za-z0-9_.\\-]"), "_"); if (n.isEmpty() || !(n[0].isLetter() || n[0] == '_')) n = "_" + n;
    return caseMode == 0 ? n.toUpper() : caseMode == 1 ? n.toLower() : n;
}

static QString xmlEsc (const QString& s) { QString o = s; o.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;"); return o; }
static bool looksNumeric (const QString& s) { static const QRegularExpression re(R"(^-?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$)"); return re.match(s).hasMatch(); }

QString ResultExporter::format (const QStringList& cols, const QVector<int>& kinds, const QVector<QStringList>& rows, const ExportOptions& o)
{
    const QString nl = o.crlf ? "\r\n" : "\n"; auto kind = [&](int c) { return c < kinds.size() ? kinds[c] : int(KindText); };
    QString out;
    switch (o.format)
    {
    case ExportOptions::Csv: case ExportOptions::Tsv:
    {
        const bool tsv = o.format == ExportOptions::Tsv; const QString sep = tsv ? "\t" : o.sep, q = tsv ? QString() : o.quote, esc = o.escape.isEmpty() ? q : o.escape;
        auto field = [&](const QString& v, bool isNull, int k) -> QString {
            if (isNull) return o.nullText;
            QString t = v; if (!o.prefixChar.isEmpty() && k == KindText && !t.isEmpty() && QString("=+@").contains(t[0])) t = o.prefixChar + t;
            if (tsv) { t.replace('\t', ' ').replace('\r', ' ').replace('\n', ' '); return t; }
            const bool need = o.quoteAlways ? (k == KindText || k == KindRaw) : (!q.isEmpty() && (t.contains(q) || t.contains(sep) || t.contains('\n') || t.contains('\r')));
            if (!need || q.isEmpty()) return t;
            t.replace(q, esc + q); return q + t + q; };
        if (o.header) { QStringList h; for (const QString& c : cols) h << field(c, false, KindText); out += h.join(sep) + nl; }
        for (const auto& r : rows) { QStringList f; for (int i = 0; i < r.size(); ++i) f << field(r[i], r[i].isNull(), kind(i)); out += f.join(sep) + nl; }
        break;
    }
    case ExportOptions::Json:
    {
        const QString ind = o.jsonPretty ? "  " : QString(), nlj = o.jsonPretty ? "\n" : QString(), sp = o.jsonPretty ? " " : QString();
        out += "[" + nlj; bool firstRow = true;
        for (const auto& r : rows)
        {
            if (!firstRow) out += "," + nlj; firstRow = false; out += ind + "{" + nlj;
            for (int i = 0; i < cols.size(); ++i)
            {
                const QString v = i < r.size() ? r[i] : QString(); const bool isNull = i >= r.size() || r[i].isNull();
                const QString jv = isNull ? "null" : (kind(i) == KindNumber && looksNumeric(v)) ? v : jsonString(v);
                out += ind + ind + jsonString(cols[i]) + ":" + sp + jv + (i + 1 < cols.size() ? "," : "") + nlj;
            }
            out += ind + "}";
        }
        out += nlj + "]" + nl; break;
    }
    case ExportOptions::Xml:
    {
        out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" + nl + "<" + xmlName(o.xmlRoot, 2) + ">" + nl; QStringList names; for (const QString& c : cols) names << xmlName(c, o.xmlCase);
        for (const auto& r : rows) { out += "  <" + xmlName(o.xmlRecord, 2) + ">" + nl; for (int i = 0; i < cols.size(); ++i) { const bool isNull = i >= r.size() || r[i].isNull(); out += isNull ? "    <" + names[i] + " xsi:nil=\"true\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"/>" + nl : "    <" + names[i] + ">" + xmlEsc(r[i]) + "</" + names[i] + ">" + nl; } out += "  </" + xmlName(o.xmlRecord, 2) + ">" + nl; }
        out += "</" + xmlName(o.xmlRoot, 2) + ">" + nl; break;
    }
    case ExportOptions::SqlInsert:
    {
        QStringList cn; for (const QString& c : cols) cn << (c.contains(QRegularExpression("^[A-Za-z][A-Za-z0-9_$#]*$")) ? c.toLower() : "\"" + c + "\"");
        const QString head = "INSERT INTO " + o.sqlTable + " (" + cn.join(", ") + ") VALUES (";
        for (const auto& r : rows)
        {
            QStringList v;
            for (int i = 0; i < cols.size(); ++i)
            {
                if (i >= r.size() || r[i].isNull()) { v << "NULL"; continue; } const QString s = r[i]; const int k = kind(i);
                if (k == KindNumber && looksNumeric(s)) v << s;
                else if ((k == KindDate || k == KindTimestamp) && !s.isEmpty())
                {   QString lit = s; lit.replace('\'', "''");
                    if (o.ansiLiterals) { QString a = s.left(19); a.replace('.', '-').replace(10, 1, ' '); a = a.left(10) + (a.size() > 10 ? a.mid(10) : QString()); v << (s.size() > 10 ? "TIMESTAMP '" : "DATE '") + a + "'"; }
                    else v << "TO_DATE('" + lit + "', '" + o.sqlDateFormat + "')"; }
                else { QString t = s; t.replace('\'', "''"); v << "'" + t + "'"; }
            }
            out += head + v.join(", ") + ");" + nl;
        }
        break;
    }
    case ExportOptions::Html:
    {
        out += "<!doctype html><meta charset=utf-8><style>body{font:13px -apple-system,sans-serif}table{border-collapse:collapse}th,td{border:1px solid #bbb;padding:3px 8px}th{background:#eef}td.n{text-align:right}td.null{color:#999;font-style:italic}</style><table>" + nl;
        if (o.header) { out += "<tr>"; for (const QString& c : cols) out += "<th>" + xmlEsc(c) + "</th>"; out += "</tr>" + nl; }
        for (const auto& r : rows) { out += "<tr>"; for (int i = 0; i < cols.size(); ++i) { if (i >= r.size() || r[i].isNull()) out += "<td class=null>(null)</td>"; else out += QString("<td%1>%2</td>").arg(kind(i) == KindNumber ? " class=n" : "", xmlEsc(r[i])); } out += "</tr>" + nl; }
        out += "</table>" + nl; break;
    }
    case ExportOptions::Text:
    {
        QVector<int> w(cols.size()); for (int i = 0; i < cols.size(); ++i) w[i] = cols[i].size();
        auto cell = [&](const QStringList& r, int i) { QString s = i < r.size() ? (r[i].isNull() ? o.nullText : r[i]) : QString(); s.replace('\n', ' ').replace('\r', ' ').replace('\t', ' '); return s; };
        for (const auto& r : rows) for (int i = 0; i < cols.size(); ++i) w[i] = qMax(w[i], int(cell(r, i).size()));
        auto line = [&](const QStringList& r, bool hdr) { QStringList f; for (int i = 0; i < cols.size(); ++i) { const QString s = hdr ? cols[i] : cell(r, i); f << (kind(i) == KindNumber && !hdr ? s.rightJustified(w[i]) : s.leftJustified(w[i])); } QString l = f.join("  "); while (l.endsWith(' ')) l.chop(1); return l + nl; };
        if (o.header) { out += line({}, true); QStringList u; for (int x : w) u << QString(x, '-'); out += u.join("  ") + nl; }
        for (const auto& r : rows) out += line(r, false);
        break;
    }
    }
    return out;
}
