#include "SyntaxColors.h"
#include <QSettings>

QString SyntaxColors::className (int c)
{
    static const char* n[] = {"Palabras reservadas (SELECT, BEGIN…)", "Funciones y llamadas (NVL, pkg.proc…)", "Números", "Textos entre comillas", "Comentarios", "Operadores y signos (:= = ; ( ))", "Variables de enlace (:nombre)", "Hints (/*+ … */)", "Objetos del esquema (tablas, paquetes…)"};
    return n[qBound(0, c, int(SynClassCount) - 1)];
}

static SynStyle st (const char* hex, bool b = false, bool i = false, bool u = false) { SynStyle s; s.color = QColor(hex); s.bold = b; s.italic = i; s.underline = u; return s; }

QVector<SynStyle> SyntaxColors::defaults (bool dark)
{
    if (dark) return { st("#6cb6ff", true), st("#c8a2ff"), st("#f2a65a"), st("#7bd88f"), st("#8b949e", false, true), st("#9aa7ff"), st("#5fd3d3"), st("#f0c674", false, true), st("#b78cff", true) };
    return { st("#2a5db0", true), st("#7a3db8"), st("#b05a00"), st("#2e8b57"), st("#808080", false, true), st("#3b3b8f"), st("#007a7a"), st("#8a4b00", false, true), st("#8000ff", true) };
}

QVector<SynStyle> SyntaxColors::classic (bool dark)
{
    if (dark) return { st("#7aa7ff", true, false, true), st("#c586ff"), st("#7ddc7d", true), st("#7ddc7d"), st("#9aa0a6", false, true), st("#7aa7ff", true), st("#ffb060"), st("#f0c674", false, true), st("#b78cff", true) };
    return { st("#1a3fa0", true, false, true), st("#8a2be2"), st("#007a00", true), st("#1f7a3a"), st("#808080", false, true), st("#1a3fa0", true), st("#c06000"), st("#8a4b00", false, true), st("#8000ff", true) };
}

QVector<SynStyle> SyntaxColors::load (bool dark)
{
    QVector<SynStyle> v = defaults(dark); QSettings s; const QString p = dark ? "colors/dark/" : "colors/light/";
    for (int i = 0; i < SynClassCount; ++i)
    {
        const QString k = p + QString::number(i); if (!s.contains(k)) continue;
        const QStringList f = s.value(k).toString().split(','); if (f.size() < 4) continue;
        v[i].color = QColor(f[0]); v[i].bold = f[1] == "1"; v[i].italic = f[2] == "1"; v[i].underline = f[3] == "1";
    }
    return v;
}

void SyntaxColors::save (bool dark, const QVector<SynStyle>& v)
{
    QSettings s; const QString p = dark ? "colors/dark/" : "colors/light/";
    for (int i = 0; i < v.size() && i < SynClassCount; ++i) s.setValue(p + QString::number(i), QString("%1,%2,%3,%4").arg(v[i].color.name(), v[i].bold ? "1" : "0", v[i].italic ? "1" : "0", v[i].underline ? "1" : "0"));
}

void SyntaxColors::reset (bool dark) { QSettings s; const QString p = dark ? "colors/dark/" : "colors/light/"; for (int i = 0; i < SynClassCount; ++i) s.remove(p + QString::number(i)); }

QString SyntaxColors::sampleSql ()
{
    return "-- Consulta de ejemplo\n/*+ hint */\nselect e.id, nvl(e.nombre, 'sin nombre') nombre, count(*) total\n  from empleados e\n where e.sueldo > 1500.50 and e.id = :id\n group by e.id;\n\n"
           "create or replace procedure p_demo (p_id in number) is\n  v_n number := 0;\nbegin\n  if p_id is null then\n    raise_application_error(-20001, 'Falta el id');\n  end if;\n  pkg_util.registrar(p_id);\nend;\n";
}
