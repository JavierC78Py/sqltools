#pragma once
// Texto que el Object Viewer copia o arrastra al editor (reglas de SQLTools para Windows: CTreeViewer::GetSelectedItemsAsText y *::GetTextToCopy).
#include <QList>
#include <QString>
#include <Qt>
class QTreeWidgetItem;

enum { RoleKey = Qt::UserRole, RolePlaceholder = Qt::UserRole + 1, RoleOwner = Qt::UserRole + 2, RoleName = Qt::UserRole + 3, RoleType = Qt::UserRole + 4, RoleCopy = Qt::UserRole + 5, RoleRaw = Qt::UserRole + 6 };

// Mayús = en una línea · ⌘ (Ctrl en Windows) = en columnas alineadas · varios elementos = lista corta de nombres
QString viewerText (const QList<QTreeWidgetItem*>& items, Qt::KeyboardModifiers mods);
