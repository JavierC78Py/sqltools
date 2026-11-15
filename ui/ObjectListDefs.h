#pragma once
// Definición de las 22 pestañas de la lista de objetos (las mismas, en el mismo orden, que SQLTools para Windows):
// título, icono, columnas (con su tipo para ordenar) y la consulta al diccionario de datos. %O% = esquema.
#include <QList>
#include <QString>
#include <QStringList>

struct ObjectListDef
{
    QString key, title, icon;
    QString ddlType;                 // tipo del objeto para Load DDL / menú contextual ("" = sin acciones de objeto)
    QStringList headers;
    QString kinds;                   // un carácter por columna: s texto · n número · d fecha
    int statusCol = -1;              // columna con VALID/INVALID (-1 = ninguna)
    QString sql;
    bool ownerOnly = false;          // solo disponible para el esquema de la sesión (papelera de reciclaje)
};

const QList<ObjectListDef>& objectListDefs ();
