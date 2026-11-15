#include "DdlOptions.h"
#include <QSettings>

DdlOptions DdlOptions::load ()
{
    QSettings s; DdlOptions o;
    auto B = [&](const char* k, bool& v) { v = s.value(QString("ddl2/") + k, v).toBool(); }; auto I = [&](const char* k, int& v) { v = s.value(QString("ddl2/") + k, v).toInt(); };
    B("sqlPlus", o.sqlPlus); B("schemaPrefix", o.schemaPrefix); B("lowerNames", o.lowerNames); B("prompts", o.prompts); B("slashAlways", o.slashAlways); B("grants", o.grants); B("comments", o.comments);
    B("specWithBody", o.specWithBody); B("bodyWithSpec", o.bodyWithSpec); B("viewTriggers", o.viewTriggers); B("viewForce", o.viewForce); B("sequenceStart", o.sequenceStart); B("onlyWithShift", o.onlyWithShift);
    B("tableDefinition", o.tableDefinition); B("constraints", o.constraints); B("indexes", o.indexes); B("triggers", o.triggers); I("storage", o.storage); B("noStorageForConstraint", o.noStorageForConstraint);
    B("commentsAfterColumn", o.commentsAfterColumn); I("commentsPos", o.commentsPos);
    return o;
}

void DdlOptions::save () const
{
    QSettings s; auto set = [&](const char* k, const QVariant& v) { s.setValue(QString("ddl2/") + k, v); };
    set("sqlPlus", sqlPlus); set("schemaPrefix", schemaPrefix); set("lowerNames", lowerNames); set("prompts", prompts); set("slashAlways", slashAlways); set("grants", grants); set("comments", comments);
    set("specWithBody", specWithBody); set("bodyWithSpec", bodyWithSpec); set("viewTriggers", viewTriggers); set("viewForce", viewForce); set("sequenceStart", sequenceStart); set("onlyWithShift", onlyWithShift);
    set("tableDefinition", tableDefinition); set("constraints", constraints); set("indexes", indexes); set("triggers", triggers); set("storage", storage); set("noStorageForConstraint", noStorageForConstraint);
    set("commentsAfterColumn", commentsAfterColumn); set("commentsPos", commentsPos);
}

QVariantMap DdlOptions::toMap () const
{
    return { {"sqlPlus", sqlPlus}, {"schemaPrefix", schemaPrefix}, {"lowerNames", lowerNames}, {"prompts", prompts}, {"slashAlways", slashAlways}, {"grants", grants}, {"comments", comments}, {"specWithBody", specWithBody},
             {"bodyWithSpec", bodyWithSpec}, {"viewTriggers", viewTriggers}, {"viewForce", viewForce}, {"sequenceStart", sequenceStart}, {"tableDefinition", tableDefinition}, {"constraints", constraints}, {"indexes", indexes},
             {"triggers", triggers}, {"storage", storage}, {"noStorageForConstraint", noStorageForConstraint}, {"commentsAfterColumn", commentsAfterColumn}, {"commentsPos", commentsPos} };
}

DdlOptions DdlOptions::fromMap (const QVariantMap& m)
{
    DdlOptions o; auto B = [&](const char* k, bool& v) { if (m.contains(k)) v = m[k].toBool(); }; auto I = [&](const char* k, int& v) { if (m.contains(k)) v = m[k].toInt(); };
    B("sqlPlus", o.sqlPlus); B("schemaPrefix", o.schemaPrefix); B("lowerNames", o.lowerNames); B("prompts", o.prompts); B("slashAlways", o.slashAlways); B("grants", o.grants); B("comments", o.comments);
    B("specWithBody", o.specWithBody); B("bodyWithSpec", o.bodyWithSpec); B("viewTriggers", o.viewTriggers); B("viewForce", o.viewForce); B("sequenceStart", o.sequenceStart);
    B("tableDefinition", o.tableDefinition); B("constraints", o.constraints); B("indexes", o.indexes); B("triggers", o.triggers); I("storage", o.storage); B("noStorageForConstraint", o.noStorageForConstraint);
    B("commentsAfterColumn", o.commentsAfterColumn); I("commentsPos", o.commentsPos);
    return o;
}
