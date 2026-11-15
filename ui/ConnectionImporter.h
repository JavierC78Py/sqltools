#pragma once
// Importa la lista de conexiones del SQLTools de Windows (archivo connections.xml). Las contraseñas NO se importan:
// en ese archivo están cifradas con la contraseña maestra de Windows y esta aplicación las guarda en el Llavero de macOS.
#include "ConnectionStore.h"
#include <QStringList>

namespace ConnectionImporter
{
    // Lee las conexiones de un connections.xml de SQLTools. Devuelve false y un mensaje si el archivo no es válido.
    bool parseSqlToolsXml (const QString& path, QList<ConnectionProfile>* out, QString* error);

    // Busca la carpeta con tnsnames.ora que define el alias: primero subcarpetas (wallets) hasta 2 niveles bajo 'root', y por último el propio 'root'.
    QString findTnsFolder (const QString& alias, const QString& root);

    // Mismos datos de conexión (etiqueta, usuario y destino) que otra ya guardada
    bool sameConnection (const ConnectionProfile& a, const ConnectionProfile& b);
}
