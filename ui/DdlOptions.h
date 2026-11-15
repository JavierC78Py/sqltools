#pragma once
// Opciones del DDL que genera «Load DDL» (las mismas que el diálogo «Local DDL Preferences» de SQLTools para Windows).
#include <QString>
#include <QVariantMap>

struct DdlOptions
{
    // ---- Common DDL ----
    bool sqlPlus = true;              // compatibilidad con SQL*Plus (sin líneas en blanco en vistas; recorta espacios finales)
    bool schemaPrefix = false;        // anteponer el nombre del esquema a los objetos
    bool lowerNames = true;           // nombres de objetos en minúsculas
    bool prompts = false;             // generar PROMPT
    bool slashAlways = false;         // usar / como fin de toda sentencia (no solo las largas)
    bool grants = true;               // incluir los permisos (GRANT)
    bool comments = true;             // incluir los comentarios
    bool specWithBody = true;         // paquete/tipo: especificación junto con el cuerpo
    bool bodyWithSpec = false;        // cuerpo: junto con la especificación
    bool viewTriggers = true;         // vistas con sus triggers INSTEAD OF
    bool viewForce = false;           // CREATE FORCE VIEW
    bool sequenceStart = false;       // secuencias con START WITH
    bool onlyWithShift = false;       // el diálogo solo aparece con la tecla Mayús pulsada
    // ---- Table Specific DDL ----
    bool tableDefinition = true;      // definición de la tabla
    bool constraints = true;          // restricciones
    bool indexes = true;              // índices
    bool triggers = true;             // triggers
    int storage = 1;                  // cláusulas físicas: 0 ninguna · 1 STORAGE y TABLESPACE · 2 siempre con TABLESPACE
    bool noStorageForConstraint = false;   // sin STORAGE en las claves primarias y únicas
    bool commentsAfterColumn = false; // comentarios de columna como nota al final de la línea
    int commentsPos = 48;             // posición de esas notas

    static DdlOptions load ();
    void save () const;
    QVariantMap toMap () const;
    static DdlOptions fromMap (const QVariantMap& m);
};
