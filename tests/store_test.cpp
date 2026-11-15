// Pruebas del almacén de conexiones y del Llavero de macOS. Usa un archivo temporal y un servicio de Llavero propio de la prueba.
#include "../ui/ConnectionStore.h"
#include "../ui/Keychain.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cstdio>

static int fails = 0;
static void check (const char* name, bool ok, const QString& extra = QString()) { std::printf("[%s] %s%s\n", ok ? " OK " : "FAIL", name, ok || extra.isEmpty() ? "" : qPrintable("   -> " + extra)); if (!ok) ++fails; }

int main (int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir; QSettings s(dir.path() + "/t.ini", QSettings::IniFormat);
    const QString svc = "SQLToolsMac-PRUEBA-" + ConnectionStore::newId();
    ConnectionStore st(&s, svc);

    ConnectionProfile a; a.id = ConnectionStore::newId(); a.tag = "Km"; a.user = "kmelot"; a.alias = "dbkmelot_high"; a.tnsAdmin = "/x/wallet"; a.readOnly = true;
    ConnectionProfile b; b.id = ConnectionStore::newId(); b.tag = "KmBr"; b.user = "venta01"; b.bypass = true; b.host = "db.ejemplo"; b.port = "1522"; b.sid = "svc"; b.mode = "SYSDBA"; b.slow = true;
    st.save(a); st.save(b);
    auto all = st.all();
    check("guardar y listar 2 conexiones", all.size() == 2);
    check("campos conservados", all[0].tag == "Km" && all[0].readOnly && all[0].tnsAdmin == "/x/wallet" && all[1].bypass && all[1].mode == "SYSDBA" && all[1].port == "1522" && all[1].slow);
    a.tag = "Km2"; st.save(a);
    check("guardar de nuevo actualiza (no duplica)", st.all().size() == 2 && st.find(a.id).tag == "Km2");
    st.touch(a.id); st.touch(a.id);
    check("touch: contador 2 y fecha", st.find(a.id).count == 2 && st.find(a.id).lastUsed.isValid());

    // Llavero real de macOS
    QString got;
    check("Llavero: guardar contraseña", st.setPassword(a.id, "p@ss ñ✓ 'raro\"$!"));
    check("Llavero: leerla (con acentos y símbolos)", st.password(a.id, &got) && got == "p@ss ñ✓ 'raro\"$!", got);
    check("Llavero: actualizar", st.setPassword(a.id, "otra") && st.password(a.id, &got) && got == "otra");
    check("la contraseña NO está en el archivo de configuración", !QFile(dir.path() + "/t.ini").exists() || ![&]{ QFile f(dir.path() + "/t.ini"); f.open(QIODevice::ReadOnly); return f.readAll().contains("otra"); }());
    st.remove(a.id);
    check("borrar conexión la quita de la lista y del Llavero", st.all().size() == 1 && !st.password(a.id, &got));
    st.remove(b.id);
    check("lista vacía al final", st.all().isEmpty());
    check("Llavero: leer inexistente es false", !Keychain::get(svc, "no-existe", &got));
    std::printf("\n%s\n", fails ? "HAY FALLOS" : "Todas las pruebas del almacén pasaron");
    return fails ? 1 : 0;
}
