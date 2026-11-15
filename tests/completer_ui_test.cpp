// Prueba del popup de autocompletado con teclado simulado (sin Oracle ni pantalla real).
#include "../ui/SqlEditor.h"
#include "../ui/CompletionData.h"
#include <QApplication>
#include <QTest>
#include <QAbstractItemView>
#include <cstdio>

static int fails = 0;
static void check (const char* name, bool ok, const QString& extra = QString())
{ std::printf("[%s] %s%s\n", ok ? " OK " : "FAIL", name, ok || extra.isEmpty() ? "" : qPrintable("   -> " + extra)); if (!ok) ++fails; }

int main (int argc, char** argv)
{
    QApplication app(argc, argv);
    CompletionData data;
    data.tables << "EMPLEADOS" << "EMPRESAS" << "DEPARTAMENTOS";
    data.members.insert("EMPLEADOS", QStringList{"ID", "NOMBRE", "SUELDO"});

    SqlEditor::autoMode = 2;
    SqlEditor ed; ed.setCompletionData(&data); ed.resize(600, 300); ed.show();
    QTest::qWaitForWindowExposed(&ed); ed.setFocus(); QApplication::processEvents();

    { // modo manual: escribir no abre nada; Ctrl+Espacio sí (y con una sola coincidencia inserta directo)
        SqlEditor::autoMode = 0; QTest::keyClicks(&ed, "select * from emp");
        check("modo manual: escribir no abre la lista", !ed.completionPopupVisible());
        QTest::keyClick(&ed, Qt::Key_Space, Qt::MetaModifier);
        check("modo manual: el atajo abre la lista", ed.completionPopupVisible());
        QTest::keyClick(&ed, Qt::Key_Escape); ed.clear(); QTest::keyClicks(&ed, "select * from dep");
        QTest::keyClick(&ed, Qt::Key_Space, Qt::MetaModifier);
        check("modo manual: una sola coincidencia se inserta directa", ed.toPlainText() == "select * from departamentos" && !ed.completionPopupVisible(), ed.toPlainText());
        ed.clear(); SqlEditor::autoMode = 2;
    }
    { // escribir "select * from emp" abre la lista; Intro acepta el primero
        QTest::keyClicks(&ed, "select * from emp");
        auto c = ed.candidates();
        check("al escribir 'emp' tras FROM ofrece EMPLEADOS y EMPRESAS", c.contains("EMPLEADOS") && c.contains("EMPRESAS") && !c.contains("DEPARTAMENTOS"), c.join(","));
        check("el popup está visible", ed.completionPopupVisible());
        QTest::keyClick(&ed, Qt::Key_Return);
        check("Intro inserta el primer candidato en minúsculas (se escribió en minúsculas)", ed.toPlainText() == "select * from empleados", ed.toPlainText());
        check("el popup se cierra tras aceptar", !ed.completionPopupVisible());
    }
    { // Flecha abajo + Tab elige el segundo
        ed.clear(); QTest::keyClicks(&ed, "select * from emp");
        QTest::keyClick(&ed, Qt::Key_Down); QTest::keyClick(&ed, Qt::Key_Tab);
        check("Flecha abajo + Tab elige el segundo", ed.toPlainText() == "select * from empresas", ed.toPlainText());
    }
    { // Escape cierra sin insertar
        ed.clear(); QTest::keyClicks(&ed, "select * from dep");
        bool was = ed.completionPopupVisible(); QTest::keyClick(&ed, Qt::Key_Escape);
        check("Escape cierra la lista y no inserta nada", was && !ed.completionPopupVisible() && ed.toPlainText() == "select * from dep", ed.toPlainText());
    }
    { // columnas por alias tras escribir el punto
        ed.clear(); QTest::keyClicks(&ed, "select e. from empleados e");
        QTextCursor tc = ed.textCursor(); tc.setPosition(9); ed.setTextCursor(tc);   // justo tras "e."
        ed.showCompletion(true);
        auto c = ed.candidates();
        check("'e.' ofrece las columnas de EMPLEADOS", c == QStringList({"ID", "NOMBRE", "SUELDO"}), c.join(","));
        QTest::keyClick(&ed, Qt::Key_Return);
        check("Intro inserta la columna tras el punto", ed.toPlainText().startsWith("select e.ID from") || ed.toPlainText().startsWith("select e.id from"), ed.toPlainText());
    }
    { // seguir escribiendo filtra; Backspace amplía
        ed.clear(); QTest::keyClicks(&ed, "select * from emp");
        auto n1 = ed.candidates().size();
        QTest::keyClicks(&ed, "l");
        auto n2 = ed.candidates().size();
        check("escribir 'l' reduce los candidatos (2 -> 1)", n1 == 2 && n2 == 1, QString("%1 -> %2").arg(n1).arg(n2));
        QTest::keyClick(&ed, Qt::Key_Backspace);
        check("Backspace los vuelve a ampliar", ed.candidates().size() == 2);
        QTest::keyClick(&ed, Qt::Key_Space);
        check("un espacio cierra la lista", !ed.completionPopupVisible());
    }
    { // Tab normal (sin popup) sigue insertando tabulación
        ed.clear(); QTest::keyClicks(&ed, "x"); ed.completionHide(); QTest::keyClick(&ed, Qt::Key_Tab);
        check("Tab sin lista inserta tabulación", ed.toPlainText() == "x\t", ed.toPlainText());
    }
    { // plantillas
        ed.clear(); QTest::keyClicks(&ed, "proc");
        auto c = ed.candidates();
        check("'proc' ofrece la plantilla proc primero", !c.isEmpty() && c.first() == "proc", c.join(","));
        QTest::keyClick(&ed, Qt::Key_Return);
        const QString t = ed.toPlainText();
        check("Intro expande el procedimiento completo", t.startsWith("create or replace procedure  (") && t.contains("end;\n/") && !t.contains("$0") && !t.startsWith("procproc"), t.left(60));
        check("el cursor queda donde estaba $0 (tras 'procedure ')", ed.textCursor().position() == QString("create or replace procedure ").size(), QString::number(ed.textCursor().position()));
    }
    { // sangría: dentro de un bloque indentado
        ed.clear(); ed.setPlainText("begin\n    "); ed.moveCursor(QTextCursor::End); QTest::keyClicks(&ed, "ifs"); QTest::keyClick(&ed, Qt::Key_Tab);
        check("las líneas de la plantilla heredan la sangría", ed.toPlainText() == "begin\n    if  then\n      null;\n    else\n      null;\n    end if;" || ed.toPlainText().contains("\n    else\n"), ed.toPlainText());
    }
    { // un identificador normal que no es plantilla no se ve afectado
        ed.clear(); QTest::keyClicks(&ed, "select * from emp"); QTest::keyClick(&ed, Qt::Key_Return);
        check("tras FROM no se ofrecen plantillas", ed.toPlainText() == "select * from empleados", ed.toPlainText());
    }
    { // CTE y subconsultas en el editor (con tabla de la caché para expandir '*')
        ed.clear(); ed.setPlainText("with v as (select id, nombre n from empleados) select v. from v");
        QTextCursor tc = ed.textCursor(); tc.setPosition(ed.toPlainText().indexOf("v. from") + 2); ed.setTextCursor(tc); ed.showCompletion(true);
        check("CTE: 'v.' ofrece ID y N", ed.candidates() == QStringList({"ID", "N"}), ed.candidates().join(","));
        ed.setPlainText("select t. from (select * from empleados) t");
        tc = ed.textCursor(); tc.setPosition(ed.toPlainText().indexOf("t. from") + 2); ed.setTextCursor(tc); ed.showCompletion(true);
        check("subconsulta con *: expande las columnas de EMPLEADOS", ed.candidates() == QStringList({"ID", "NOMBRE", "SUELDO"}), ed.candidates().join(","));
        ed.setPlainText("select t. from (select e.*, 1 extra from empleados e) t");
        tc = ed.textCursor(); tc.setPosition(ed.toPlainText().indexOf("t. from") + 2); ed.setTextCursor(tc); ed.showCompletion(true);
        check("alias.* más columna con alias", ed.candidates() == QStringList({"EXTRA", "ID", "NOMBRE", "SUELDO"}), ed.candidates().join(","));
    }
    std::printf("\n%s\n", fails ? "HAY FALLOS" : "Todas las pruebas del popup pasaron");
    return fails ? 1 : 0;
}
