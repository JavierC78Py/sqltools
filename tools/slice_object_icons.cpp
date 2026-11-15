// Corta las tiras de iconos de objetos de SQLTools (db_obj_list.bmp, sql_gen_list.bmp, db_obj_state_list_v2.bmp) en un PNG por icono
// y convierte los .ico sueltos. Los .bmp usan el verde (0,255,0) como color transparente. Uso: slice_object_icons <carpeta res/> <salida>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <cstdio>

static int cutSheet (const QString& file, const QStringList& names, const QString& prefix, const QString& out, int w = 16)
{
    QImage strip(file); if (strip.isNull()) { std::printf("[FALLO] %s\n", qPrintable(file)); return 0; }
    strip = strip.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < strip.height(); ++y) for (int x = 0; x < strip.width(); ++x) { const QRgb p = strip.pixel(x, y); if (qRed(p) == 0 && qGreen(p) == 255 && qBlue(p) == 0) strip.setPixel(x, y, 0); }
    const int n = strip.width() / w;
    for (int i = 0; i < n; ++i)
    {
        const QString name = i < names.size() && !names[i].isEmpty() ? names[i] : QString::number(i);
        strip.copy(i * w, 0, w, strip.height()).scaled(w * 2, strip.height() * 2, Qt::IgnoreAspectRatio, Qt::FastTransformation).save(out + "/" + prefix + name + ".png");
    }
    std::printf("%s: %d iconos\n", qPrintable(QFileInfo(file).fileName()), n); return n;
}

int main (int argc, char** argv)
{
    QGuiApplication app(argc, argv); if (argc < 3) return 2;
    const QString res = argv[1], out = argv[2]; QDir().mkpath(out);
    // db_obj_list.bmp (pestañas de la lista de objetos): índices de DbBrowserList.h
    cutSheet(res + "/db_obj_list.bmp", {"function","procedure","package","package_body","trigger","view","table","sequence","synonym","grantee","cluster","dblink","snapshot","snapshot_log","type","type_body","index","chk","pk","uk","fk","recyclebin","invalid","java"}, "list_", out);
    // sql_gen_list.bmp (árbol del Object Viewer): índices de ObjectTree_Builder.h
    cutSheet(res + "/sql_gen_list.bmp", {"folder","open_folder","table","cluster","dblink","index","package","sequence","synonym","trigger","view","schema","function","procedure","column","chk","pk","uk","fk","","","","","grant","package_body","type","type_body","","unknown","param_in","param_out","param_inout","partition"}, "tree_", out);
    cutSheet(res + "/db_obj_state_list_v2.bmp", {}, "state_", out);
    const char* icos[] = {"refresh","filter","settings","as_list_btn","as_report_btn","valid_btn","Invalid_btn","ObjectList","ObjectViewer","properties","Snippets","CodeOutline","OpenFiles","FindInFiles"};
    for (const char* n : icos) { QImage im(res + "/" + n + ".ico"); if (im.isNull()) { std::printf("[FALLO] %s.ico\n", n); continue; } im.save(out + "/ico_" + QString(n).toLower() + ".png"); }
    // hoja de contacto para revisar a ojo (opcional: tercer argumento)
    if (argc > 3)
    {
        QStringList files; for (const QString& f : QDir(out).entryList({"list_*.png", "tree_*.png"}, QDir::Files, QDir::Name)) files << f;
        const int cols = 8, cw = 150, ch = 44; QImage sheet(cols * cw, ((files.size() + cols - 1) / cols) * ch, QImage::Format_ARGB32); sheet.fill(Qt::white); QPainter p(&sheet);
        for (int i = 0; i < files.size(); ++i) { const int x = (i % cols) * cw, y = (i / cols) * ch; p.drawImage(x + 2, y + 2, QImage(out + "/" + files[i])); p.setPen(Qt::black); QFont f = p.font(); f.setPointSize(8); p.setFont(f); p.drawText(x + 40, y + 24, QString(files[i]).remove(".png")); }
        sheet.save(argv[3]);
    }
    return 0;
}
