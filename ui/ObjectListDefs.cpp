#include "ObjectListDefs.h"

static const char* D = "'YYYY-MM-DD HH24:MI:SS'";
static QString dt (const char* col) { return QString("to_char(%1, %2)").arg(col, D); }
static QString objList (const char* type, const char* extraWhere = "")
{
    return QString("select o.object_name, %1, %2, o.status from all_objects o where o.owner = %O% and o.object_type = '%3' %4 order by 1").arg(dt("o.created"), dt("o.last_ddl_time"), type, extraWhere);
}
const QList<ObjectListDef>& objectListDefs ()
{
    static const QList<ObjectListDef> defs = []{
        // Una sola pasada por all_cons_columns / all_ind_columns (agrupada) en vez de una subconsulta por fila: mucho más rápido en esquemas grandes
        const QString colsAgg = "cc.cols";
        const QString colsJoin = "(select constraint_name, listagg(column_name, ', ') within group (order by position) cols from all_cons_columns where owner = %O% group by constraint_name) cc";
        QList<ObjectListDef> d;
        auto add = [&](const QString& key, const QString& title, const QString& icon, const QString& ddl, const QStringList& h, const QString& k, int status, const QString& sql, bool ownerOnly = false)
        { ObjectListDef x; x.key = key; x.title = title; x.icon = icon; x.ddlType = ddl; x.headers = h; x.kinds = k; x.statusCol = status; x.sql = sql; x.ownerOnly = ownerOnly; d << x; };
        add("tables", "Tables", "table", "TABLE", {"Name", "Type", "Tablespace", "Compression", "Created", "Modified", "Rows", "Blocks", "Last Analyzed"}, "sssddd" "nnd" , -1,
            "select t.table_name, case when t.iot_type is not null then 'IOT' when t.temporary = 'Y' then 'TEMPORARY' when t.partitioned = 'YES' then 'PARTITIONED' else '.' end, nvl(t.tablespace_name, '.'), nvl(to_char(t.compression), '.'), " + dt("o.created") + ", " + dt("o.last_ddl_time") + ", to_char(t.num_rows), to_char(t.blocks), " + dt("t.last_analyzed") +
            " from all_tables t, all_objects o where t.owner = %O% and o.owner = %O% and o.object_type = 'TABLE' and o.object_name = t.table_name and t.iot_name is null order by 1");
        // el orden de las columnas de la tabla en pantalla: nombre, tipo, tablespace, compresión, creada, modificada, filas, bloques, analizada
        d.last().kinds = "ssssddnnd";
        add("pks", "PKs", "pk", "", {"Constraint", "Table", "Columns", "Deferrable", "Deferred", "Status"}, "ssssss", 5,
            "select c.constraint_name, c.table_name, " + colsAgg + ", c.deferrable, c.deferred, c.status from all_constraints c, " + colsJoin + " where cc.constraint_name(+) = c.constraint_name and c.owner = %O% and c.constraint_type = 'P' order by 1");
        add("uks", "UKs", "uk", "", {"Constraint", "Table", "Columns", "Deferrable", "Deferred", "Status"}, "ssssss", 5,
            "select c.constraint_name, c.table_name, " + colsAgg + ", c.deferrable, c.deferred, c.status from all_constraints c, " + colsJoin + " where cc.constraint_name(+) = c.constraint_name and c.owner = %O% and c.constraint_type = 'U' order by 1");
        add("fks", "FKs", "fk", "", {"Constraint", "Table", "Columns", "Ref Owner", "Ref Constraint", "Delete Rule", "Deferrable", "Deferred", "Status"}, "sssssssss", 8,
            "select c.constraint_name, c.table_name, " + colsAgg + ", c.r_owner, c.r_constraint_name, c.delete_rule, c.deferrable, c.deferred, c.status from all_constraints c, " + colsJoin + " where cc.constraint_name(+) = c.constraint_name and c.owner = %O% and c.constraint_type = 'R' order by 1");
        add("chks", "CHKs", "chk", "", {"Constraint", "Table", "Condition", "Deferrable", "Deferred", "Status"}, "ssssss", 5,
            "select c.constraint_name, c.table_name, c.search_condition, c.deferrable, c.deferred, c.status from all_constraints c where c.owner = %O% and c.constraint_type = 'C' and c.generated = 'USER NAME' order by 1");
        add("indexes", "Indexes", "index", "INDEX", {"Index", "Table", "Type", "Unique", "Tablespace", "Partitioned", "Created", "Modified", "Last analyzed", "Columns"}, "ssssssddds", -1,
            "select i.index_name, i.table_name, i.index_type, i.uniqueness, nvl(i.tablespace_name, '.'), i.partitioned, " + dt("o.created") + ", " + dt("o.last_ddl_time") + ", " + dt("i.last_analyzed") + ", ic.cols from all_indexes i, all_objects o, (select index_name, listagg(column_name, ', ') within group (order by column_position) cols from all_ind_columns where index_owner = %O% group by index_name) ic where ic.index_name(+) = i.index_name and i.owner = %O% and o.owner = i.owner and o.object_name = i.index_name and o.object_type like 'INDEX%' order by 1");
        add("triggers", "Triggers", "trigger", "TRIGGER", {"Name", "Table Owner", "Table", "Type", "Event", "When", "Created", "Modified", "Enabled", "Status"}, "ssssssddss", 9,
            "select t.trigger_name, nvl(t.table_owner, '.'), nvl(t.table_name, '.'), t.trigger_type, t.triggering_event, nvl(t.when_clause, '.'), " + dt("o.created") + ", " + dt("o.last_ddl_time") + ", decode(t.status, 'ENABLED', 'Y', 'N'), o.status from all_triggers t, all_objects o where t.owner = %O% and o.owner = t.owner and o.object_name = t.trigger_name and o.object_type = 'TRIGGER' order by 1");
        add("procedures", "Procedures", "procedure", "PROCEDURE", {"Name", "Created", "Modified", "Status"}, "sdds", 3, objList("PROCEDURE"));
        add("functions", "Functions", "function", "FUNCTION", {"Name", "Created", "Modified", "Status"}, "sdds", 3, objList("FUNCTION"));
        add("packages", "Packages", "package", "PACKAGE", {"Name", "Created", "Modified", "Status"}, "sdds", 3, objList("PACKAGE"));
        add("packagebodies", "Package Bodies", "package_body", "PACKAGE", {"Name", "Created", "Modified", "Status"}, "sdds", 3, objList("PACKAGE BODY"));
        add("types", "Types", "type", "TYPE", {"Name", "Type", "Attrs", "Methods", "Incomplete", "Created", "Modified", "Status"}, "ssnnsdds", 7,
            "select t.type_name, t.typecode, to_char(t.attributes), to_char(t.methods), t.incomplete, " + dt("o.created") + ", " + dt("o.last_ddl_time") + ", o.status from all_types t, all_objects o where t.owner = %O% and o.owner = t.owner and o.object_name = t.type_name and o.object_type = 'TYPE' order by 1");
        add("typebodies", "Type Bodies", "type_body", "TYPE", {"Name", "Created", "Modified", "Status"}, "sdds", 3, objList("TYPE BODY"));
        add("java", "Java", "java", "", {"Name", "Type", "Created", "Modified", "Status"}, "ssdds", 4,
            "select o.object_name, o.object_type, " + dt("o.created") + ", " + dt("o.last_ddl_time") + ", o.status from all_objects o where o.owner = %O% and o.object_type like 'JAVA%' order by 1");
        add("sequences", "Sequences", "sequence", "SEQUENCE", {"Name", "Last Number", "Min Value", "Max Value", "Interval", "Cache", "Cycle", "Order", "Generated", "Created", "Modified"}, "snnnnnssSdd" , -1,
            "select s.sequence_name, to_char(s.last_number), to_char(s.min_value), to_char(s.max_value), to_char(s.increment_by), to_char(s.cache_size), s.cycle_flag, s.order_flag, o.generated, " + dt("o.created") + ", " + dt("o.last_ddl_time") + " from all_sequences s, all_objects o where s.sequence_owner = %O% and o.owner = s.sequence_owner and o.object_name = s.sequence_name and o.object_type = 'SEQUENCE' order by 1");
        d.last().kinds = "snnnnnsssdd";
        add("views", "Views", "view", "VIEW", {"Name", "Text Length", "Created", "Modified", "Status"}, "sndds", 4,
            "select v.view_name, to_char(v.text_length), " + dt("o.created") + ", " + dt("o.last_ddl_time") + ", o.status from all_views v, all_objects o where v.owner = %O% and o.owner = v.owner and o.object_name = v.view_name and o.object_type = 'VIEW' order by 1");
        add("synonyms", "Synonyms", "synonym", "SYNONYM", {"Synonym", "Owner", "Object", "DB Link", "Status"}, "sssss", 4,
            "select s.synonym_name, s.table_owner, s.table_name, nvl(s.db_link, '.'), nvl(o.status, 'VALID') from all_synonyms s, all_objects o where s.owner = %O% and o.owner(+) = s.owner and o.object_name(+) = s.synonym_name and o.object_type(+) = 'SYNONYM' order by 1");
        add("dblinks", "DB Links", "dblink", "", {"Db Link", "Username", "Host"}, "sss", -1, "select db_link, nvl(username, '.'), nvl(host, '.') from all_db_links where owner = %O% order by 1");
        add("clusters", "Clusters", "cluster", "", {"Name", "Type", "Function", "Hashkeys", "Key size", "Tablespace", "Initial", "Next", "Increase", "Max Ext", "Pct Free", "Pct Used"}, "sssnnsnnnnnn", -1,
            "select cluster_name, cluster_type, nvl(function, '.'), to_char(hashkeys), to_char(key_size), nvl(tablespace_name, '.'), to_char(initial_extent), to_char(next_extent), to_char(pct_increase), to_char(max_extents), to_char(pct_free), to_char(pct_used) from all_clusters where owner = %O% order by 1");
        add("grantee", "Grantee", "grantee", "", {"Grantee"}, "s", -1, "select distinct grantee from all_tab_privs where table_schema = %O% order by 1");
        add("recyclebin", "Recyclebin", "recyclebin", "", {"Name", "Oper", "Type", "Partition", "Tablespace", "Created", "Dropped", "Can Undrop", "Can Purge", "Space (blk)"}, "sssssssssn", -1,
            "select object_name, operation, type, nvl(partition_name, '.'), nvl(ts_name, '.'), createtime, droptime, can_undrop, can_purge, to_char(space) from user_recyclebin order by droptime desc", true);
        d.last().kinds = "sssssddssn";
        add("invalid", "Invalid Objects", "invalid", "", {"Name", "Type", "Created", "Modified", "Status"}, "ssdds", 4,
            "select o.object_name, o.object_type, " + dt("o.created") + ", " + dt("o.last_ddl_time") + ", o.status from all_objects o where o.owner = %O% and o.status <> 'VALID' order by 1");
        return d; }();
    return defs;
}
