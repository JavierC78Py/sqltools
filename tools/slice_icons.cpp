// Corta las tiras de iconos de las barras de herramientas de SQLTools (.bmp) en un PNG por botón, nombrado con el ID del comando.
// Los .bmp del proyecto original (GPLv3) usan el color del píxel (0,0) como transparente. Uso: slice_icons <spec.json> <carpeta res/> <salida>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

int main (int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    if (argc < 4) { std::fprintf(stderr, "uso: slice_icons spec.json carpeta_res salida\n"); return 2; }
    QFile f(argv[1]); f.open(QIODevice::ReadOnly);
    const QJsonObject tbs = QJsonDocument::fromJson(f.readAll()).object()["toolbars"].toObject();
    const QString res = argv[2], out = argv[3]; QDir().mkpath(out);
    const QMap<QString, QString> files = { {"IDT_FILE", "FileToolbar.bmp"}, {"IDT_SQL", "SqlToolbar.bmp"}, {"IDT_CONNECT", "ConnectToolbar.bmp"},
                                           {"IDT_BOOKLET", "BookletToolbar.bmp"}, {"IDT_GRIDPOPUP", "GridPopupToolbar.bmp"} };
    int total = 0, problems = 0;
    for (auto it = files.begin(); it != files.end(); ++it)
    {
        const QJsonObject tb = tbs[it.key()].toObject(); const int size = tb["size"].toInt();
        QImage strip(res + "/" + it.value());
        if (strip.isNull()) { std::printf("[FALLO] no se pudo leer %s\n", qPrintable(it.value())); ++problems; continue; }
        strip = strip.convertToFormat(QImage::Format_ARGB32);
        const QRgb key = strip.pixel(0, 0);
        for (int y = 0; y < strip.height(); ++y) for (int x = 0; x < strip.width(); ++x) if (strip.pixel(x, y) == key) strip.setPixel(x, y, 0);   // transparente
        int idx = 0, n = 0;
        for (const auto& b : tb["buttons"].toArray())
        {
            if (b.isNull()) continue;
            ++n;
            const QImage icon = strip.copy(idx * size, 0, size, strip.height());
            // se amplía el doble con vecino más cercano: nítido en pantallas Retina
            icon.scaled(icon.width() * 2, icon.height() * 2, Qt::IgnoreAspectRatio, Qt::FastTransformation).save(out + "/" + b.toString() + ".png");
            ++idx; ++total;
        }
        const bool ok = strip.width() == n * size;
        std::printf("[%s] %-14s %2d botones, tira de %d px = %d iconos de %d px\n", ok ? " OK " : "FAIL", qPrintable(it.key()), n, strip.width(), strip.width() / size, size);
        if (!ok) ++problems;
    }
    // qrc
    QStringList names = QDir(out).entryList({"*.png"}, QDir::Files, QDir::Name);
    QFile q(QString(out) + "/../icons.qrc"); q.open(QIODevice::WriteOnly | QIODevice::Text);
    q.write("<RCC>\n  <qresource prefix=\"/icons\">\n");
    for (const QString& n : names) q.write(("    <file alias=\"" + n + "\">icons/" + n + "</file>\n").toUtf8());
    q.write("  </qresource>\n  <qresource prefix=\"/spec\">\n    <file alias=\"menu_spec.json\">spec/menu_spec.json</file>\n  </qresource>\n</RCC>\n");
    std::printf("%d iconos escritos en %s (%d problemas)\n", total, argv[3], problems);
    return problems ? 1 : 0;
}
