// Genera el icono de la aplicación a partir de los recursos ORIGINALES de SQLTools (GPLv3):
// 16/32/64 px desde SQLToolsMainFrame.ico y 128+ px desde el logo SQLTools256s.bmp (fondo blanco -> transparente).
// Los tamaños grandes se amplían por múltiplos enteros sin suavizar (es arte de 16 colores: se mantiene nítido).
// Uso: make_icon <res> <salida.iconset> <app_256.png>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QQueue>
#include <cstdio>

static QImage makeTransparentBackground (QImage img)       // rellena desde los bordes: solo el fondo conectado, no los blancos internos
{
    img = img.convertToFormat(QImage::Format_ARGB32);
    const QRgb white = qRgb(255, 255, 255);
    QQueue<QPoint> q; QVector<bool> seen(img.width() * img.height(), false);
    auto push = [&](int x, int y) { if (x < 0 || y < 0 || x >= img.width() || y >= img.height() || seen[y * img.width() + x] || (img.pixel(x, y) | 0xff000000) != (white | 0xff000000)) return; seen[y * img.width() + x] = true; q.enqueue({x, y}); };
    for (int x = 0; x < img.width(); ++x) { push(x, 0); push(x, img.height() - 1); }
    for (int y = 0; y < img.height(); ++y) { push(0, y); push(img.width() - 1, y); }
    while (!q.isEmpty()) { QPoint p = q.dequeue(); img.setPixel(p, 0); push(p.x() + 1, p.y()); push(p.x() - 1, p.y()); push(p.x(), p.y() + 1); push(p.x(), p.y() - 1); }
    return img;
}

int main (int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    if (argc < 4) return 2;
    const QString res = argv[1], out = argv[2];
    // fotogramas del .ico por tamaño (se queda con el de más profundidad de color)
    QMap<int, QImage> ico; QImageReader r(res + "/SQLToolsMainFrame.ico");
    for (int i = 0; i < r.imageCount(); ++i) { r.jumpToImage(i); QImage f = r.read(); if (!f.isNull()) ico[f.width()] = f.convertToFormat(QImage::Format_ARGB32); }
    QImage big = makeTransparentBackground(QImage(res + "/SQLTools256s.bmp"));
    if (big.isNull() || !ico.contains(16) || !ico.contains(32) || !ico.contains(64)) { std::fprintf(stderr, "faltan recursos (ico: %lld tamaños, bmp %s)\n", (long long)ico.size(), big.isNull() ? "no" : "si"); return 1; }
    QDir().mkpath(out);
    auto scaled = [&](int px) { return big.scaled(px, px, Qt::IgnoreAspectRatio, Qt::FastTransformation); };
    struct E { const char* name; QImage img; } list[] = {
        {"icon_16x16.png", ico[16]}, {"icon_16x16@2x.png", ico[32]}, {"icon_32x32.png", ico[32]}, {"icon_32x32@2x.png", ico[64]},
        {"icon_128x128.png", big}, {"icon_128x128@2x.png", scaled(256)}, {"icon_256x256.png", scaled(256)}, {"icon_256x256@2x.png", scaled(512)},
        {"icon_512x512.png", scaled(512)}, {"icon_512x512@2x.png", scaled(1024)} };
    for (auto& e : list) { e.img.save(out + "/" + e.name); std::printf("  %-22s %dx%d\n", e.name, e.img.width(), e.img.height()); }
    scaled(256).save(argv[3]);
    return 0;
}
