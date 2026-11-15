#include "../ui/ResultExporter.h"
#include <QCoreApplication>
#include <cstdio>
static int bad = 0, total = 0;
static void check (const char* n, const QString& got, const QString& want) { ++total; if (got == want) { printf("[ OK ] %s\n", n); return; } ++bad; printf("[FAIL] %s\n--- obtenido:\n%s\n--- esperado:\n%s\n", n, got.toUtf8().constData(), want.toUtf8().constData()); }
int main (int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList cols = {"ID", "NOMBRE", "FECHA", "NOTA"}; const QVector<int> kinds = {KindNumber, KindText, KindDate, KindText};
    QStringList r1 = {"1", "Pérez, Ana", "2026.01.31 10:20:30", QString()}; QStringList r2 = {"2", "dice \"hola\"", "2026.02.01 00:00:00", "=SUMA(A1)"}; r2[3] = "=SUMA(A1)";
    const QVector<QStringList> rows = {r1, r2};
    ExportOptions o;
    check("CSV estándar", ResultExporter::format(cols, kinds, rows, o), "ID,NOMBRE,FECHA,NOTA\n1,\"Pérez, Ana\",2026.01.31 10:20:30,\n2,\"dice \"\"hola\"\"\",2026.02.01 00:00:00,=SUMA(A1)\n");
    o.sep = ";"; o.header = false; o.quoteAlways = true; o.nullText = "NULL"; o.prefixChar = "'"; o.crlf = true;
    check("CSV punto y coma, siempre comillas, NULL, prefijo, CRLF", ResultExporter::format(cols, kinds, rows, o), "1;\"Pérez, Ana\";2026.01.31 10:20:30;NULL\r\n2;\"dice \"\"hola\"\"\";2026.02.01 00:00:00;\"'=SUMA(A1)\"\r\n");
    o = ExportOptions(); o.quote = "\""; o.escape = "\\";
    check("CSV con escape de barra", ResultExporter::format({"A"}, {KindText}, {QStringList{"x\"y"}}, o), "A\n\"x\\\"y\"\n");
    o = ExportOptions(); o.format = ExportOptions::Tsv;
    check("TSV", ResultExporter::format(cols, kinds, rows, o), "ID\tNOMBRE\tFECHA\tNOTA\n1\tPérez, Ana\t2026.01.31 10:20:30\t\n2\tdice \"hola\"\t2026.02.01 00:00:00\t=SUMA(A1)\n");
    o.format = ExportOptions::Json;
    check("JSON legible (números sin comillas, null)", ResultExporter::format({"ID", "N"}, {KindNumber, KindText}, {QStringList{"7", QString()}, QStringList{"8", "a\"b"}}, o),
        "[\n  {\n    \"ID\": 7,\n    \"N\": null\n  },\n  {\n    \"ID\": 8,\n    \"N\": \"a\\\"b\"\n  }\n]\n");
    o.jsonPretty = false;
    check("JSON compacto", ResultExporter::format({"ID"}, {KindNumber}, {QStringList{"7"}}, o), "[{\"ID\":7}]\n");
    o = ExportOptions(); o.format = ExportOptions::Xml; o.xmlCase = 1; o.xmlRoot = "datos"; o.xmlRecord = "fila";
    check("XML", ResultExporter::format({"ID", "Mi Col"}, {KindNumber, KindText}, {QStringList{"1", "a<b&c"}}, o), "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<datos>\n  <fila>\n    <id>1</id>\n    <mi_col>a&lt;b&amp;c</mi_col>\n  </fila>\n</datos>\n");
    o = ExportOptions(); o.format = ExportOptions::SqlInsert; o.sqlTable = "emp";
    check("INSERT de SQL (número, texto con comilla, fecha, NULL)", ResultExporter::format({"ID", "NOM", "F", "X"}, {KindNumber, KindText, KindDate, KindText}, {QStringList{"1", "O'Brien", "2026.01.31 10:20:30", QString()}}, o),
        "INSERT INTO emp (id, nom, f, x) VALUES (1, 'O''Brien', TO_DATE('2026.01.31 10:20:30', 'YYYY.MM.DD HH24:MI:SS'), NULL);\n");
    o.ansiLiterals = true;
    check("INSERT con literal ANSI", ResultExporter::format({"F"}, {KindDate}, {QStringList{"2026.01.31 10:20:30"}}, o), "INSERT INTO emp (f) VALUES (TIMESTAMP '2026-01-31 10:20:30');\n");
    o = ExportOptions(); o.format = ExportOptions::Text;
    check("Texto alineado (números a la derecha)", ResultExporter::format({"ID", "NOMBRE"}, {KindNumber, KindText}, {QStringList{"1", "ab"}, QStringList{"100", "c"}}, o), "ID   NOMBRE\n---  ------\n  1  ab\n100  c\n");
    check("Nombre de archivo con plantilla", QString(ExportOptions().fileName().startsWith("resultados_") ? "ok" : "mal"), "ok");
    printf("\n%d pruebas, %d fallos\n", total, bad); return bad ? 1 : 0;
}
