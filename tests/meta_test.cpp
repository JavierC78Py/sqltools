// Prueba SIN base de datos de la biblioteca MetaDictionary portada: se arma una tabla a mano y se compara su DDL con el que genera SQLTools para Windows.
#include "stdafx.h"
#include "MetaDictionary/MetaDictionary.h"
#include "MetaDictionary/MetaObjects.h"
#include "MetaDictionary/TextOutput.h"
#include <cstdio>
using namespace OraMetaDict;
static int bad = 0, total = 0;
static void check (const char* n, const std::string& got, const std::string& want) { ++total; if (got == want) { printf("[ OK ] %s\n", n); return; } ++bad; printf("[FAIL] %s\n--- obtenido:\n%s\n--- esperado:\n%s\n", n, got.c_str(), want.c_str()); }
int main ()
{
    Dictionary dict; Table& t = dict.CreateTable("ANAMNESIS", "CLIENTE"); t.m_strOwner = "ANAMNESIS"; t.m_strName = "CLIENTE";
    auto add = [&](int i, const char* name, const char* type, int len, int prec, int scale, bool nullable, const char* def = "") {
        auto c = std::make_shared<TabColumn>(); c->m_strColumnName = name; c->m_strDataType = type; c->m_nDataLength = len; c->m_nDataPrecision = prec; c->m_nDataScale = scale; c->m_bNullable = nullable; c->m_strDataDefault = def; t.m_Columns[i] = c; };
    add(1, "ID_CLIENTE", "NUMBER", 22, 10, 0, false); add(2, "CI", "VARCHAR2", 50, 0, 0, false); add(3, "RUC", "VARCHAR2", 50, 0, 0, true); add(4, "SEXO", "CHAR", 10, 0, 0, false);
    add(5, "MINUSVALIDO", "CHAR", 2, 0, 0, true, "'NO'"); add(6, "FECHA_ALTA", "DATE", 7, 0, 0, true); add(7, "COMMENT", "VARCHAR2", 100, 0, 0, true);
    WriteSettings ws; ws.m_GeneratePrompts = false; ws.m_StorageClause = 0;
    TextOutputInMEM out(true, 16 * 1024); out.SetLowerDBName(true);
    t.WriteDefinition(out, ws);
    check("CREATE TABLE con columnas alineadas como en Windows", std::string(out.GetData(), out.GetLength()),
        "CREATE TABLE cliente (\n  id_cliente  NUMBER(10,0)  NOT NULL,\n  ci          VARCHAR2(50)  NOT NULL,\n  ruc         VARCHAR2(50)  NULL,\n  sexo        CHAR(10)      NOT NULL,\n  minusvalido CHAR(2)       DEFAULT 'NO' NULL,\n  fecha_alta  DATE          NULL,\n  \"COMMENT\"     VARCHAR2(100) NULL\n)\n/\n\n");
    // con almacenamiento (como la tabla CLIENTE de Windows: TABLESPACE + STORAGE NEXT 1024 K)
    t.m_strTablespaceName.Set("FACTURACION_DATA"); t.m_nNextExtent.Set(1024 * 1024); ws.m_StorageClause = 1;
    TextOutputInMEM out2(true, 16 * 1024); out2.SetLowerDBName(true); t.WriteDefinition(out2, ws); const std::string d2(out2.GetData(), out2.GetLength());
    check("TABLESPACE y STORAGE al estilo de Windows", d2.substr(d2.find(")\n")), ")\n  TABLESPACE facturacion_data\n  STORAGE (\n    NEXT       1024 K\n  )\n/\n\n");
    // cabecera de trigger: Oracle trae un espacio antes de cada salto de línea y SQLTools lo quita
    {
        Trigger& tr = dict.CreateTrigger("ANAMNESIS", "CLIENTE_DESP_TRIG"); tr.m_strOwner = "ANAMNESIS"; tr.m_strName = "CLIENTE_DESP_TRIG"; tr.m_strTableOwner = "ANAMNESIS"; tr.m_strTableName = "CLIENTE";
        tr.m_strDescription = "CLIENTE_DESP_TRIG \nAFTER INSERT OR DELETE OR UPDATE ON CLIENTE \nREFERENCING NEW AS NEW OLD AS OLD \nFOR EACH ROW\n"; tr.m_strTriggerBody = "BEGIN\n  NULL;\nEND;"; tr.m_strActionType = "PL/SQL"; tr.m_strBaseObjectType = "TABLE";
        WriteSettings w2; w2.m_GeneratePrompts = false; TextOutputInMEM o3(true, 16 * 1024); o3.SetLowerDBName(true); tr.Write(o3, w2);
        const std::string t3(o3.GetData(), o3.GetLength());
        check("Trigger: la cabecera no lleva espacios al final de las líneas", t3.substr(0, t3.find("FOR EACH ROW")), "CREATE OR REPLACE TRIGGER cliente_desp_trig\nAFTER INSERT OR DELETE OR UPDATE ON cliente\nREFERENCING NEW AS NEW OLD AS OLD\n");
    }
    printf("\n%d pruebas, %d fallos\n", total, bad); return bad ? 1 : 0;
}
