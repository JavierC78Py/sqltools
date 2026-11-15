#pragma once
// Codificaciones de texto de los scripts: UTF-8, ANSI (Windows-1252), OEM (CP850, la «consola de Windows» en español) y UTF-16.
#include <QByteArray>
#include <QString>
#include <QStringList>

namespace TextCodec
{
    QStringList names ();
    QStringList extraNames ();                                                 // las del submenú «Others»                                                      // "UTF-8", "Windows-1252", "IBM850", "UTF-16"
    QString decode (const QByteArray& data, const QString& enc);               // data sin BOM (UTF-16: con BOM o LE por defecto)
    QByteArray encode (const QString& text, const QString& enc, bool* lossy = nullptr);   // lossy = algún carácter no cabe en la codificación (queda '?')
    QString label (const QString& enc);                                        // nombre legible
}
