#pragma once
// Sustituto mínimo del módulo de lenguajes del editor de Windows: solo la lista de palabras reservadas de Oracle SQL
// (sirve para decidir qué nombres de objetos hay que entrecomillar al generar DDL).
#include "stdafx.h"
#include <map>
#include <memory>
#include <string>
namespace OpenEditor
{
    struct KeywordInfo { int groupIndex = 0; };
    typedef std::map<std::u16string, KeywordInfo> LanguageKeywordMap;
    typedef std::shared_ptr<LanguageKeywordMap> LanguageKeywordMapPtr;
    typedef LanguageKeywordMap::const_iterator LanguageKeywordMapConstIterator;
    struct Language
    {
        LanguageKeywordMapPtr GetLanguageKeywordMap ()
        {
            static LanguageKeywordMapPtr m = [] {
                auto p = std::make_shared<LanguageKeywordMap>();
                for (const char* w : {"ACCESS","ADD","ALL","ALTER","AND","ANY","AS","ASC","AUDIT","BETWEEN","BY","CHAR","CHECK","CLUSTER","COLUMN","COMMENT","COMPRESS","CONNECT","CREATE","CURRENT","DATE","DECIMAL","DEFAULT","DELETE","DESC","DISTINCT","DROP","ELSE","EXCLUSIVE","EXISTS","FILE","FLOAT","FOR","FROM","GRANT","GROUP","HAVING","IDENTIFIED","IMMEDIATE","IN","INCREMENT","INDEX","INITIAL","INSERT","INTEGER","INTERSECT","INTO","IS","LEVEL","LIKE","LOCK","LONG","MAXEXTENTS","MINUS","MLSLABEL","MODE","MODIFY","NOAUDIT","NOCOMPRESS","NOT","NOWAIT","NULL","NUMBER","OF","OFFLINE","ON","ONLINE","OPTION","OR","ORDER","PCTFREE","PRIOR","PUBLIC","RAW","RENAME","RESOURCE","REVOKE","ROW","ROWID","ROWNUM","ROWS","SELECT","SESSION","SET","SHARE","SIZE","SMALLINT","START","SUCCESSFUL","SYNONYM","SYSDATE","TABLE","THEN","TO","TRIGGER","UID","UNION","UNIQUE","UPDATE","USER","VALIDATE","VALUES","VARCHAR","VARCHAR2","VIEW","WHENEVER","WHERE","WITH"})
                { std::u16string k; for (const char* c = w; *c; ++c) k += (char16_t)*c; (*p)[k] = KeywordInfo(); }
                return p; }();
            return m;
        }
    };
    struct LanguagesCollection { static Language* Find (const char*) { static Language l; return &l; } };
    // Tabla de «¿es delimitador?» por carácter
    class DelimitersMap
    {
    public:
        explicit DelimitersMap (const char* charset) { for (int i = 0; i < 256; ++i) m_map[i] = false; for (const char* c = charset; c && *c; ++c) m_map[(unsigned char)*c] = true; }
        void assign (int ch, bool v) { m_map[(unsigned char)ch] = v; }
        bool operator[] (int ch) const { return m_map[(unsigned char)ch]; }
    private:
        bool m_map[256];
    };
}
