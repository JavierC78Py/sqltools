#pragma once
// Metadatos del esquema que usa el autocompletado. Los llena MainWindow desde el explorador de objetos; los editores solo leen.
#include <QHash>
#include <QSet>
#include <QStringList>

struct CompletionData
{
    QStringList tables;                    // tablas, vistas, vistas materializadas y sinónimos
    QStringList routines;                  // procedimientos, funciones y paquetes
    QStringList others;                    // secuencias y tipos
    QSet<QString> packages;                // nombres (MAYÚSCULAS) que son paquetes: "pkg." completa con sus miembros
    QHash<QString, QStringList> members;   // MAYÚSCULAS -> columnas de una tabla/vista o miembros de un paquete
    QString currentSchema;                 // esquema de la sesión
    QStringList schemas;                   // esquemas de usuario visibles (para "esquema." y tras FROM)
    QHash<QString, QStringList> schemaTables;    // ESQUEMA -> tablas, vistas y sinónimos (cargado a demanda)
    QHash<QString, QStringList> schemaRoutines;  // ESQUEMA -> procedimientos, funciones y paquetes
    QSet<QString> requested;               // ya solicitados al servidor (evita pedir dos veces)
};
