// Pruebas de carga/guardado de scripts: codificación (UTF-8, UTF-8 con BOM, Windows-1252), CRLF y ida y vuelta sin cambios.
#include "../ui/SqlEditor.h"
#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

static int fails = 0;
static void check (const char* name, bool ok) { std::printf("[%s] %s\n", ok ? " OK " : "FAIL", name); if (!ok) ++fails; }
static QByteArray read (const QString& p) { QFile f(p); f.open(QIODevice::ReadOnly); return f.readAll(); }
static void write (const QString& p, const QByteArray& d) { QFile f(p); f.open(QIODevice::WriteOnly); f.write(d); }

int main (int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir dir; const QString d = dir.path();

    { // UTF-8 con acentos y símbolos, saltos \n
        QByteArray data = QString("select 'Ñandú áéíóú ✓' from dual;\nselect 2 from dual;\n").toUtf8();
        write(d + "/a.sql", data); SqlEditor e; QString err;
        check("UTF-8: carga", e.load(d + "/a.sql", &err) && e.encoding == "UTF-8" && !e.crlf && !e.bom && e.toPlainText().contains("Ñandú áéíóú ✓"));
        check("UTF-8: guardar sin cambios = mismos bytes", e.save(d + "/a2.sql") && read(d + "/a2.sql") == data);
    }
    { // UTF-8 con BOM y CRLF (típico de Windows)
        QByteArray data = QByteArray("\xEF\xBB\xBF") + QString("select 'ñ' from dual;\r\nselect 2 from dual;\r\n").toUtf8();
        write(d + "/b.sql", data); SqlEditor e;
        check("BOM+CRLF: detectados", e.load(d + "/b.sql") && e.bom && e.crlf && e.encoding == "UTF-8" && !e.toPlainText().contains('\r'));
        check("BOM+CRLF: ida y vuelta idéntica", e.save(d + "/b2.sql") && read(d + "/b2.sql") == data);
    }
    { // Windows-1252 (bytes inválidos en UTF-8): é = 0xE9, ñ = 0xF1, € = 0x80
        QByteArray data = QByteArray("select 'caf\xE9 ni\xF1o \x80' from dual;\r\n");
        write(d + "/c.sql", data); SqlEditor e;
        check("Windows-1252: se detecta", e.load(d + "/c.sql") && e.encoding == "Windows-1252" && e.toPlainText().contains("café niño"));
        check("Windows-1252: ida y vuelta (sin €)", e.save(d + "/c2.sql") && read(d + "/c2.sql").startsWith("select 'caf\xE9 ni\xF1o"));
    }
    { // editar y guardar marca el documento como limpio
        write(d + "/e.sql", "select 1 from dual;\n"); SqlEditor e; e.load(d + "/e.sql");
        e.appendPlainText("select 2 from dual;");
        bool dirty = e.document()->isModified(); e.save(d + "/e.sql");
        check("editar -> modificado; guardar -> limpio", dirty && !e.document()->isModified() && read(d + "/e.sql").contains("select 2"));
    }
    { // archivo inexistente
        SqlEditor e; QString err; check("archivo inexistente falla con mensaje", !e.load(d + "/no_existe.sql", &err) && !err.isEmpty());
    }
    { // goToLine
        SqlEditor e; e.setPlainText("uno\ndos\ntres\n"); e.goToLine(2, 2);
        check("goToLine(2,2)", e.textCursor().blockNumber() == 1 && e.textCursor().positionInBlock() == 1);
    }
    std::printf("\n%s\n", fails ? "HAY FALLOS" : "Todas las pruebas del editor pasaron");
    return fails ? 1 : 0;
}
