#pragma once
// Variables de enlace (:nombre) de una sentencia SQL y su sustitución por literales.
// Las variables se localizan fuera de textos, comentarios y literales q'[...]'. La sustitución por literales sirve para SELECT/DML;
// los bloques PL/SQL con parámetros OUT no se pueden ejecutar así (el llamador debe avisar).
#include <QMap>
#include <QString>
#include <QStringList>

struct BindValue { QString type = "Auto"; QString value; };      // type: Auto | String | Number | Date | NULL

namespace BindParser
{
    QStringList names (const QString& sql);                                              // únicas, en orden de aparición
    QString substitute (const QString& sql, const QMap<QString, BindValue>& values, QStringList* warnings = nullptr);   // claves en MAYÚSCULAS
    QString literal (const BindValue& v, QString* warning = nullptr);                    // SQL del valor: NULL, 42, 'texto', to_date(...)
}
