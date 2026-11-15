#pragma once
// Format SQL: reescribe SQL y PL/SQL con un estilo uniforme sin cambiar lo que hace.
//  - SQL (SELECT / INSERT / UPDATE / DELETE / MERGE / WITH): una cláusula por línea, columnas una por línea, AND/OR en línea propia, subconsultas sangradas.
//  - PL/SQL: re-sangra los bloques (BEGIN/END, IF, LOOP, CASE, EXCEPTION, declaraciones) y da formato a las sentencias SQL que contienen.
// Los textos entre comillas, los comentarios y las pistas (hints) no se tocan.
#include <QString>

struct FormatOptions
{
    int indent = 2;                 // espacios por nivel de sangría
    bool useTabs = false;           // sangrar con tabuladores en vez de espacios
    enum Case { Upper, Lower, Keep };
    int keywordCase = Upper;        // palabras reservadas y funciones
    enum Style { River, Standard };
    int style = Standard;           // Riachuelo: SELECT/FROM/WHERE alineadas por la derecha; Estándar: a la izquierda con el contenido sangrado
    bool commaLeading = false;      // coma al principio de la línea
    bool andOrNewLine = true;       // AND / OR en línea propia
    bool oneItemPerLine = true;     // columnas de SELECT, GROUP BY, ORDER BY, SET… una por línea
    int maxLine = 100;              // los paréntesis más largos que esto se parten
    // ---- estructura personalizada ----
    int contIndent = -1;            // sangría de las columnas/condiciones respecto a la palabra de la cláusula (-1 = según el estilo)
    int andOrIndent = -1;           // sangría de AND/OR respecto a WHERE (0 = bajo WHERE; -1 = según el estilo)
    int joinIndent = -1;            // sangría de los JOIN respecto a FROM (-1 = según el estilo)
    bool kwAloneWhenMany = true;    // estilo Estándar: la palabra SELECT sola en su línea cuando hay varias columnas
    bool onNewLine = false;         // la condición ON de un JOIN en su propia línea
    bool spaceAfterComma = true;    // «a, b» en vez de «a,b»
    bool spaceInParens = false;     // «( a, b )»
    int functionCase = -1;          // funciones: -1 = igual que las palabras reservadas
    int identifierCase = Keep;      // nombres de tablas y columnas (sin comillas)
    bool alignDeclarations = false; // PL/SQL: alinear los tipos de las declaraciones en columna
    bool alignAssignments = false;  // PL/SQL: alinear los := de asignaciones seguidas
    bool blankBeforeException = false;   // PL/SQL: línea en blanco antes de EXCEPTION
    bool blankAfterDeclare = false;      // PL/SQL: línea en blanco antes de BEGIN (tras las declaraciones)

    static FormatOptions load ();   // desde QSettings ("format/…")
    void save () const;
    QString toJson () const;                                   // perfiles: guardar / compartir
    static FormatOptions fromJson (const QString& json, bool* ok = nullptr);
    static QStringList profileNames ();
    static void saveProfile (const QString& name, const FormatOptions& o);
    static bool loadProfile (const QString& name, FormatOptions& o);
    static void deleteProfile (const QString& name);
    static FormatOptions inferFromExample (const QString& sample, QStringList* findings = nullptr);   // deduce las opciones de un código ya formateado
};

class SqlFormatter
{
public:
    // Da formato a un texto con una o varias sentencias (separadas por ';' o '/'). Conserva la separación entre sentencias.
    static QString format (const QString& text, const FormatOptions& opt);
    // Una sola sentencia SQL (sin PL/SQL). baseIndent = columna inicial.
    static QString formatSql (const QString& statement, const FormatOptions& opt, int baseIndent = 0);
    // Código PL/SQL (bloque o CREATE de objeto): re-sangrado + SQL embebido.
    static QString formatPlsql (const QString& code, const FormatOptions& opt);
};
