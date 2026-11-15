#pragma once
// Exportación de resultados a texto: CSV (configurable), TSV, JSON, XML, INSERT de SQL, HTML y texto alineado.
#include <QString>
#include <QStringList>
#include <QVector>

// Tipo de cada columna del resultado (lo que sabe la rejilla): decide números sin comillas, fechas con TO_DATE, etc.
enum ColKind { KindText = 0, KindNumber = 1, KindDate = 2, KindTimestamp = 3, KindInterval = 4, KindRaw = 5 };

struct ExportOptions
{
    enum Format { Csv, Tsv, Json, Xml, SqlInsert, Html, Text, FormatCount };
    enum Destination { File, Clipboard, Editor };
    int format = Csv;
    bool header = true;
    bool jsonPretty = true;
    // CSV
    QString sep = ",", quote = "\"", escape = "\"";      // escape = carácter que precede a la comilla dentro del campo (por defecto se duplica la comilla)
    bool quoteAlways = false;                            // entrecomillar todos los textos (si no, solo cuando hace falta)
    QString nullText;                                    // cómo se escribe NULL en CSV/TSV/Texto (vacío por defecto)
    bool crlf = false; bool bom = false;
    QString prefixChar;                                  // se antepone a los textos que empiezan por = + @ (evita fórmulas en Excel); vacío = no
    // XML
    QString xmlRoot = "RESULT", xmlRecord = "ROW"; int xmlCase = 0;      // 0 MAYÚSCULAS, 1 minúsculas, 2 como está
    // SQL
    QString sqlTable = "TABLE_NAME"; bool ansiLiterals = false; QString sqlDateFormat = "YYYY.MM.DD HH24:MI:SS";
    // Archivo
    QString filenameTemplate = "resultados_{fecha}_{hora}";

    static ExportOptions load (Destination d);
    void save (Destination d) const;
    static QString extension (int format);
    static QString formatName (int format);
    QString fileName () const;                           // plantilla con {fecha} {hora} {tabla}
};

class ResultExporter
{
public:
    static QString format (const QStringList& cols, const QVector<int>& kinds, const QVector<QStringList>& rows, const ExportOptions& o);
    static QString xmlName (const QString& name, int caseMode);
    static QString jsonString (const QString& s);
};
