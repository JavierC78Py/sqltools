// Construye el manual del usuario (HTML de un solo archivo y PDF) a partir de docs/manual/index.txt.
// Uso: build_manual <carpeta docs> <carpeta de salida> [índice] [nombre]
#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPageSize>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextStream>
#include <cstdio>

static QString readAll (const QString& path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) { fprintf(stderr, "No se pudo leer %s\n", qPrintable(path)); return QString(); } return QString::fromUtf8(f.readAll()); }

// Une los capítulos en un solo texto Markdown y genera el índice (H1 y H2)
static QString assemble (const QString& docs, const QString& indexName)
{
    QString out, toc; const QStringList lines = readAll(docs + "/manual/" + indexName).split('\n'); QString pendingTitle; int chapters = 0;
    QStringList parts;
    for (const QString& raw : lines)
    {
        const QString l = raw.trimmed(); if (l.isEmpty() || l.startsWith('#')) continue;
        if (l.startsWith("@apendice:")) { pendingTitle = l.mid(10); continue; }
        QString t = readAll(docs + "/" + l); if (t.isEmpty()) continue;
        if (!pendingTitle.isEmpty()) { QStringList ls = t.split('\n'); if (!ls.isEmpty() && ls[0].startsWith("# ")) ls.removeFirst(); t = "# " + pendingTitle + "\n" + ls.join('\n'); pendingTitle.clear(); }
        // índice: títulos de nivel 1 y 2 (fuera de bloques de código)
        bool code = false; for (const QString& ln : t.split('\n')) { if (ln.startsWith("```")) code = !code; if (code) continue;
            if (ln.startsWith("# ")) { toc += "- **" + ln.mid(2).trimmed() + "**\n"; ++chapters; } else if (ln.startsWith("## ") && chapters > 1 && t.size() < 60000) toc += "    - " + ln.mid(3).trimmed() + "\n"; }
        parts << t;
    }
    // el índice va después del capítulo de inicio
    for (int i = 0; i < parts.size(); ++i) { out += parts[i] + "\n\n"; if (i == 0) out += "## Contenido\n\n" + toc + "\n\n"; }
    return out;
}

// Ajusta el tamaño de las imágenes (las capturas son @2x) y devuelve cuántas hay
static int scaleImages (QTextDocument& doc, const QString& imgDir, int maxW)
{
    int n = 0;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next())
        for (QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it)
        {
            QTextFragment f = it.fragment(); if (!f.isValid() || !f.charFormat().isImageFormat()) continue;
            QTextImageFormat fmt = f.charFormat().toImageFormat(); QImage img(imgDir + "/" + fmt.name()); if (img.isNull()) { fprintf(stderr, "Falta la imagen %s\n", qPrintable(fmt.name())); continue; }
            const int w = qMin(img.width() / 2, maxW); fmt.setWidth(w); fmt.setHeight(qRound(double(w) * img.height() / img.width()));
            QTextCursor c(&doc); c.setPosition(f.position()); c.setPosition(f.position() + f.length(), QTextCursor::KeepAnchor); c.setCharFormat(fmt); ++n;
        }
    return n;
}

static const char* kCss =
    "body { font-family: -apple-system, 'Helvetica Neue', Arial, sans-serif; font-size: 11pt; line-height: 1.45; color: #202020; max-width: 900px; margin: 24px auto; padding: 0 16px; }"
    "h1 { color: #1a3f7a; border-bottom: 2px solid #c8d4e8; padding-bottom: 4px; margin-top: 40px; } h2 { color: #1a3f7a; margin-top: 26px; } h3 { color: #333; }"
    "table { border-collapse: collapse; margin: 8px 0; } td, th { border: 1px solid #c8c8c8; padding: 4px 8px; vertical-align: top; } th { background: #eef2f8; }"
    "pre, code { font-family: Menlo, monospace; font-size: 10pt; background: #f4f4f4; } pre { padding: 8px; border: 1px solid #ddd; }"
    "img { max-width: 100%; border: 1px solid #ddd; margin: 6px 0; } blockquote { border-left: 4px solid #c8d4e8; margin-left: 0; padding-left: 12px; color: #444; }";

int main (int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    if (argc < 3) { fprintf(stderr, "Uso: build_manual <carpeta docs> <carpeta de salida> [índice] [nombre]\n"); return 2; }
    const QString docs = QDir(argv[1]).absolutePath(), outDir = QDir(argv[2]).absolutePath(); QDir().mkpath(outDir);
    const QString md = assemble(docs, argc > 3 ? QString(argv[3]) : QString("index.txt")); if (md.isEmpty()) return 1;
    const QString imgDir = docs + "/manual";
    // ---- HTML de un solo archivo (imágenes incrustadas) ----
    {
        QTextDocument doc; doc.setBaseUrl(QUrl::fromLocalFile(imgDir + "/")); doc.setMarkdown(md, QTextDocument::MarkdownDialectGitHub); const int n = scaleImages(doc, imgDir, 800);
        QString html = doc.toHtml();
        // Qt escribe cada imagen como <img src="img/x.png" width=.. height=..>: se incrusta en base64
        QRegularExpression re("<img src=\"([^\"]+)\"([^>]*)>"); QString res; int last = 0; auto it = re.globalMatch(html);
        while (it.hasNext()) { auto m = it.next(); res += html.mid(last, m.capturedStart() - last); QFile f(imgDir + "/" + m.captured(1)); QString tag = m.captured(0);
            if (f.open(QIODevice::ReadOnly)) tag = "<img src=\"data:image/png;base64," + QString::fromLatin1(f.readAll().toBase64()) + "\"" + m.captured(2) + ">"; res += tag; last = m.capturedEnd(); }
        res += html.mid(last); res.replace("</head>", QString("<style>%1</style></head>").arg(kCss)); res.replace(QRegularExpression("<body style=\"[^\"]*\">"), "<body>");
        QFile out(outDir + "/" + (argc > 4 ? QString(argv[4]) : QString("SQLTools-Mac-Manual")) + ".html"); if (!out.open(QIODevice::WriteOnly)) return 1; out.write(res.toUtf8()); out.close();
        printf("HTML: %s (%d imágenes, %lld KB)\n", qPrintable(out.fileName()), n, out.size() / 1024);
    }
    // ---- PDF ----
    {
        QPdfWriter pdf(outDir + "/" + (argc > 4 ? QString(argv[4]) : QString("SQLTools-Mac-Manual")) + ".pdf"); pdf.setPageSize(QPageSize(QPageSize::A4)); pdf.setPageMargins(QMarginsF(16, 16, 16, 16), QPageLayout::Millimeter); pdf.setTitle("SQLTools Mac — Manual del usuario"); pdf.setResolution(150);
        QTextDocument doc; doc.setBaseUrl(QUrl::fromLocalFile(imgDir + "/")); doc.setDefaultFont(QFont("Helvetica Neue", 10)); doc.setDefaultStyleSheet("h1 { color: #1a3f7a; } h2 { color: #1a3f7a; } td, th { padding: 3px; }");
        doc.setMarkdown(md, QTextDocument::MarkdownDialectGitHub); const int pageW = 470; scaleImages(doc, imgDir, pageW);
        doc.setPageSize(QSizeF(pdf.width() * 72.0 / pdf.resolution(), pdf.height() * 72.0 / pdf.resolution()));
        doc.print(&pdf);
        printf("PDF: %s\n", qPrintable(outDir + "/SQLTools-Mac-Manual.pdf"));
    }
    return 0;
}
