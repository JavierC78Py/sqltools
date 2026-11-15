#pragma once
// Contraseñas en el Llavero de macOS (nunca en archivos de configuración ni en texto plano).
#include <QString>

namespace Keychain
{
    bool set (const QString& service, const QString& account, const QString& secret);
    bool get (const QString& service, const QString& account, QString* secret);   // false si no existe o no hay acceso
    bool remove (const QString& service, const QString& account);
}
