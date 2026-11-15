#pragma once
#include <QPlainTextEdit>
#include <QTextBlockUserData>
#include "FoldRegions.h"
#include <QString>
class QTimer; class SqlHighlighter; struct SqlContext; class QCompleter; class QStandardItemModel; struct CompletionData;

// Un script SQL en una pestaña: texto, archivo asociado, codificación y saltos de línea originales.
// Datos que viajan con cada línea (se mueven con el texto al insertar o borrar líneas): marcador, marcador numerado y plegado.
struct BlockInfo : QTextBlockUserData { bool bookmark = false; int number = -1; bool collapsed = false; };

class GutterArea; class RulerArea;
class SqlEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit SqlEditor (QWidget* parent = nullptr);
    QString filePath;                  // vacío = sin guardar
    QString titleHint;                 // nombre para scripts sin archivo (p.ej. código abierto desde el explorador)
    QString encoding = "UTF-8";        // "UTF-8" o "Windows-1252" (se conserva al guardar)
    bool crlf = false;                 // el archivo original usaba \r\n
    bool cr = false;                   // Mac antiguo: solo \r
    bool bom = false;

    bool load (const QString& path, QString* error = nullptr, const QString& forceEncoding = QString());   // forceEncoding: "UTF-8", "Windows-1252", "IBM850", "UTF-16"
    bool save (const QString& path, QString* error = nullptr);
    QString displayName () const;      // nombre del archivo o "Sin título"
    void goToLine (int line, int column = 1);

    void applyEditorSettings ();                           // tamaño de letra y ancho del tabulador (Settings)

    // ---- Comandos de texto (menús Text, Text/Format, Edit) ----
    void indentSelection (bool undent);
    void commentLines (bool comment);                       // antepone/quita "-- " en las líneas tocadas por la selección
    void changeCase (int mode);                             // 0 minúsculas, 1 MAYÚSCULAS, 2 Capitalizar, 3 invertir
    void duplicateLineOrSelection ();
    void deleteLine ();
    void selectLine ();
    void selectWord ();
    void trimSpaces (int mode);                             // 0 iniciales, 1 finales, 2 excesivas (varios espacios seguidos -> uno)
    void removeBlankLines (bool onlyExcessive);
    void joinLines ();
    void insertDateStamp ();
    void sortLines (bool descending);
    void setShowWhitespace (bool on);
    bool showWhitespace () const;
    // ---- Margen: números de línea, marcadores y flechas de plegado ----
    void setShowLineNumbers (bool on);  bool showLineNumbers () const { return m_lineNumbers; }
    void setShowFoldGutter (bool on);   bool showFoldGutter () const { return m_foldGutter; }
    int gutterWidth () const;
    // Marcadores (Search > Bookmarks)
    void toggleBookmark ();
    void nextBookmark (bool forward);
    void clearBookmarks ();
    void setNumberedBookmark (int n);                     // Cmd+Mayús+0..9
    void gotoNumberedBookmark (int n);                    // Cmd+0..9
    QList<int> bookmarkedLines () const;                  // líneas (desde 1) con marcador
    int numberedBookmarkLine (int n) const;               // 0 si no existe
    QString takeBookmarkedLines (bool remove);            // texto de las líneas marcadas (y las borra si remove)
    // Plegado (Search > Outlining)
    void foldCurrent (int mode);                          // 0 plegar, 1 desplegar, 2 alternar la región actual
    void expandSubtree ();
    void foldAll (bool collapse);
    void toggleAllFolds ();
    void foldToLevel (int level);                         // deja visibles N niveles de anidamiento (1 = solo el exterior)
    void refreshFolds ();                                 // recalcula las regiones (se llama sola al editar)
    const QVector<FoldRegion>& foldRegions () const { return m_regions; }
    bool isLineVisible (int line) const;                  // línea desde 0
    bool findMatchingBracket (bool select);               // Ctrl+] : salta al paréntesis/corchete pareja
    void gutterPaint (QPaintEvent* ev);                   // (lo usa GutterArea)
    void gutterMouse (QMouseEvent* ev);

    // ---- Más comandos de texto ----
    void deleteWord (bool forward);                         // Delete Word Back / Delete to End of Word
    void cutAppend ();                                      // corta y añade al portapapeles
    void scrollLines (int delta);                           // Scroll Up/Down (el cursor no se mueve)
    void tabify (bool leadingOnly);                         // espacios -> tabuladores en la selección (o todo el texto)
    void untabify ();                                       // tabuladores -> espacios
    void normalizeKeywords ();                              // palabras reservadas en MAYÚSCULAS (fuera de textos y comentarios)
    void splitLines (int width);                            // parte las líneas largas en el último espacio antes de la columna
    void insertNumbers (int start, int step, int width);    // inserta un número por línea, en la columna del cursor
    QString pathUnderCursor () const;                       // texto con aspecto de ruta bajo el cursor
    void setIndentGuides (bool on) { m_indentGuides = on; viewport()->update(); }
    void setColumnMarkers (bool on) { m_columnMarkers = on; viewport()->update(); }
    bool m_indentGuides = false, m_columnMarkers = false;

    // ---- Selección en columna (rectangular), como en SQLTools: Mayús+Opción+flechas ----
    bool columnMode () const { return m_colMode; }
    void enterColumnMode ();                                // parte de la selección actual (o del cursor)
    void exitColumnMode (bool keepCaret = true);
    void toggleColumnMode () { if (m_colMode) exitColumnMode(); else enterColumnMode(); }
    void columnExtend (int dLine, int dCol);
    void columnBlock (int& l0, int& l1, int& c0, int& c1) const;        // líneas y columnas (desde 0) del bloque; c1 exclusivo
    QString columnText () const;                            // texto del bloque, una línea por fila
    void columnReplace (const QStringList& pieces);         // sustituye el contenido del bloque (1 pieza = se repite en todas las líneas)
    void columnFill (const QString& text, bool fill);       // Insert/Fill Columns
    void columnJustify (int how);                           // 0 izquierda, 1 centro, 2 derecha
    void copy ();  void cut ();  void paste ();              // ocultan los de QPlainTextEdit: respetan el modo columna
    // Regla de columnas sobre el texto (View > Ruler)
    void setShowRuler (bool on);  bool showRuler () const { return m_ruler; }
    int rulerHeight () const { return m_ruler ? fontMetrics().height() + 6 : 0; }
    void rulerPaint (QPaintEvent* ev);
    bool m_colMode = false; int m_colAL = 0, m_colAC = 0, m_colL = 0, m_colC = 0;    // ancla y extremo móvil (línea, columna)
    bool m_ruler = false; RulerArea* m_rulerArea = nullptr;

    // ---- Esquema del código (Code Outline) ----
    struct OutlineItem { int level; int line; QString title; QString kind; };    // line desde 1
    QVector<OutlineItem> outline () const;

    // Buscar y reemplazar
    bool findText (const QString& text, bool backward, bool caseSensitive, bool wholeWord, bool regex);
    bool replaceCurrent (const QString& text, const QString& with, bool caseSensitive, bool wholeWord, bool regex);
    int replaceAll (const QString& text, const QString& with, bool caseSensitive, bool wholeWord, bool regex);
    QString wordUnderCursor () const;
    // Inserta una plantilla (con $0 = posición final del cursor), sangrada como la línea actual. replaceFrom >= 0 reemplaza desde esa posición hasta el cursor.
    void insertSnippet (QString body, int replaceFrom = -1);

    // Autocompletado
    static inline int autoMode = 0;         // 0 = solo con el atajo (Ctrl+Espacio), 1 = automático tras el punto, 2 = siempre automático
    void setCompletionData (CompletionData* data) { m_data = data; }
    void showCompletion (bool force);
    QStringList resolveColumns (const SqlContext& ctx, const QString& source, int depth = 0);   // columnas de tabla/vista, CTE o subconsulta       // calcula candidatos y abre la lista (force: aunque no haya prefijo)
    void refreshCompletion ();
    QStringList candidates () const;        // candidatos actuales de la lista (para pruebas)
    bool completionPopupVisible () const;
    void completionHide ();              // llegaron columnas/miembros que se estaban esperando
signals:
    void bookmarksChanged ();                        // se pusieron o quitaron marcadores
    void columnsNeeded (const QString& table);     // pedir columnas de tabla/vista al servidor
    void membersNeeded (const QString& package);   // pedir miembros de un paquete ("ESQUEMA.PAQ" si es de otro esquema)
    void schemaObjectsNeeded (const QString& owner); // pedir los objetos de otro esquema
protected:
    bool event (QEvent*) override;                          // deja que los atajos del programa ganen a los del editor (⌘↑, ⇧⌘↑…)
    void keyPressEvent (QKeyEvent*) override;
    void mousePressEvent (QMouseEvent*) override;
    void resizeEvent (QResizeEvent*) override;
    void paintEvent (QPaintEvent*) override;
private slots:
    void updateCurrentStatement ();
    void acceptCompletion (const QModelIndex& index);
private:
    SqlHighlighter* m_hl;
    QTimer* m_timer;
    QCompleter* m_completer; QStandardItemModel* m_model;
    CompletionData* m_data = nullptr;
    GutterArea* m_gutter = nullptr; bool m_lineNumbers = true, m_foldGutter = true;
    QVector<FoldRegion> m_regions; QTimer* m_foldTimer = nullptr;
    void updateGutterWidth ();
    void applyFolds ();
    int regionAt (int line, bool innermost) const;
    QString m_waitingFor;                   // tabla/paquete cuyos miembros se esperan para completar
    QString m_prefixNow;
};
