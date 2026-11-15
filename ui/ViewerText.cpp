#include "ViewerText.h"
#include "DdlOptions.h"
#include <QSettings>
#include <QTreeWidgetItem>

// ---- Object Viewer: texto que se copia / arrastra al editor. Reglas tomadas de SQLTools para Windows (CTreeViewer::GetSelectedItemsAsText y
// ObjectTree_Builder_1.cpp: *::GetTextToCopy). Sin teclas: un objeto → su nombre; procedimiento/función → «nombre(p1 => , p2 => );».
// Mayús = en una línea (columnas con su tipo, columnas de índices/constraints); ⌘ (Ctrl en Windows) = en columnas alineadas. Varios elementos → lista corta de nombres.

namespace {
QString safeName (const QString& raw, bool lower)        // SQLUtilities::GetSafeDatabaseObjectName: nombres «normales» tal cual (o en minúsculas); el resto entre comillas
{
    if (raw.isEmpty()) return raw;
    bool regular = raw[0] >= 'A' && raw[0] <= 'Z';
    for (QChar c : raw) if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' || c == '#')) { regular = false; break; }
    return regular ? (lower ? raw.toLower() : raw) : "\"" + raw + "\"";
}
QString fullName (const QString& owner, const QString& name, const DdlOptions& o) { return (o.schemaPrefix && !owner.isEmpty() ? safeName(owner, o.lowerNames) + "." : QString()) + safeName(name, o.lowerNames); }
QTreeWidgetItem* ownerObject (QTreeWidgetItem* it) { while (it && it->data(0, RoleType).toString().isEmpty()) it = it->parent(); return it; }

// 0 = por defecto, 1 = corto (varios elementos), 2 = en una línea (Mayús), 3 = en columnas (⌘)
QString itemText (QTreeWidgetItem* it, int form, const DdlOptions& o, const QSettings& st)
{
    const QString kind = it->data(0, RoleCopy).toString(), raw = it->data(0, RoleRaw).toString();
    if (kind == "member" || kind == "proc")
    {
        QString head;
        if (kind == "proc") { if (auto* ob = ownerObject(it)) head = fullName(ob->data(0, RoleOwner).toString(), ob->data(0, RoleName).toString(), o); else head = safeName(raw, o.lowerNames); }
        else
        {
            head = safeName(raw, o.lowerNames);
            if (st.value("viewer/copyWithPackage", false).toBool()) if (auto* ob = ownerObject(it)) head = fullName(ob->data(0, RoleOwner).toString(), ob->data(0, RoleName).toString(), o) + "." + head;
        }
        QStringList params; for (int i = 0; i < it->childCount(); ++i) if (it->child(i)->data(0, RoleCopy).toString() == "param") params << safeName(it->child(i)->data(0, RoleRaw).toString(), o.lowerNames);
        if (form == 1) return head;
        if (form == 3)
        {
            if (params.isEmpty()) return head + "();\n";
            int w = 0; for (const QString& p : params) w = qMax(w, int(p.size()));
            QStringList lines; for (const QString& p : params) lines << "  " + p + QString(w - p.size(), ' ') + " => ";
            return head + "\n(\n" + lines.join(",\n") + "\n);\n";
        }
        QStringList a; for (const QString& p : params) a << p + " => ";
        return head + "(" + a.join(", ") + ");";
    }
    if (kind == "column") { QString t = it->text(0); if (form == 2) { QString ty = it->text(1); const int k = ty.indexOf(" not null"); if (k > 0) ty.truncate(k); const int d = ty.indexOf(" default "); if (d > 0) ty.truncate(d); t += " " + ty.trimmed().toUpper(); } return t; }
    if (kind == "param") return safeName(raw, o.lowerNames);
    if (!it->data(0, RoleKey).toString().isEmpty()) return it->text(0);                          // carpeta: su título
    const QString type = it->data(0, RoleType).toString();
    if (type == "INDEX" || type == "CONSTRAINT")
    {
        if (form >= 2) { QStringList cols; for (int i = 0; i < it->childCount(); ++i) if (!it->child(i)->data(0, RolePlaceholder).toBool()) cols << safeName(it->child(i)->text(0), o.lowerNames); if (!cols.isEmpty()) return cols.join(", "); }
        return safeName(it->data(0, RoleName).toString(), o.lowerNames);
    }
    if (!type.isEmpty()) return fullName(it->data(0, RoleOwner).toString(), it->data(0, RoleName).toString(), o);
    return it->text(0);
}

} // namespace

QString viewerText (const QList<QTreeWidgetItem*>& items, Qt::KeyboardModifiers mods)
{
    if (items.isEmpty()) return QString();
    const DdlOptions o = DdlOptions::load(); QSettings st;
    int form = items.size() > 1 ? 1 : (mods & Qt::ShiftModifier) ? 2 : (mods & Qt::ControlModifier) ? 3 : 0;
    QStringList parts; for (auto* it : items) if (!it->data(0, RolePlaceholder).toBool()) parts << itemText(it, form, o, st);
    if (form == 3 && items.size() == 1) return parts.value(0);
    // TextFormatter: elementos separados por «, » y saltos de línea al pasar de N caracteres (Max line length, 60 por defecto)
    const int maxLen = form == 3 ? 0 : st.value("viewer/maxLine", 60).toInt(); QString out, cur;
    for (const QString& p : parts)
    {
        if (!cur.isEmpty() && maxLen > 0 && cur.size() + 2 + p.size() > maxLen) { out += (out.isEmpty() ? QString() : ",\n") + cur; cur.clear(); }
        cur += (cur.isEmpty() ? QString() : ", ") + p;
    }
    return out.isEmpty() ? cur : out + ",\n" + cur;
}
