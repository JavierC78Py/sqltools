#pragma once
#include <QMainWindow>
#include <QJsonObject>
#include <functional>
#include "ResultExporter.h"
#include <QHash>
#include <QThread>
#include <QMap>
#include "BindParser.h"
#include <QVariantMap>
class QPrinter; class QStackedWidget; class QToolBar; class QActionGroup; class RotatedModel; class HistoryStore; class QTreeWidget; class QMenu; class FindInFilesDialog; class QDockWidget; class CommandMenu; class FindReplaceDialog; class QPlainTextEdit; class QTableView; class QTabWidget; class QTimer; class QTableWidget; class SqlEditor; class QCloseEvent; class QLabel; class QAction; class QSplitter;
struct CompletionData; class CommandMenu; class FindReplaceDialog; class DbWorker; class ResultModel; class SqlHighlighter; class ObjectBrowser;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow ();
    static void applyAppearance (const QString& mode);      // "light" | "dark" | "system"
    void applyTheme ();                                     // barras de iconos, etiqueta de conexión, colores (al cambiar el tema)
    QStringList dumpCommands () const;      // "ID\tetiqueta\tmenú\tWindows\tMac\testado" (documentación)
    void selfTestCommands ();               // autoprueba de menús y atajos (SQLT_SELFTEST=commands)
    ~MainWindow () override;
    void autoStartFromEnvironment ();        // solo para pruebas sin pantalla (variables SQLT_*)
private slots:
    void showConnectDialog ();
    void setupCommands ();
    void addRecent (const QString& path);
    // espacios de trabajo e instantáneas (MainWorkspace.cpp)
    void setupWorkspaceCommands ();
    void setupExtraCommands ();
    void generateManualShots (const QString& dir);          // ManualShots.cpp: capturas del manual (SQLT_MANUAL_SHOTS)
    void loadDdl (const QStringList& objects, bool oneFile, bool askDialog, const QString& owner = QString());      // Load DDL con las opciones «Local DDL Preferences»
    void onBrowserAction (const QString& action, const QStringList& objects);   // menú contextual del explorador de objetos
    void quickQueryOn (const QString& object, bool count);
    void applyExecOptions ();                               // transacciones, historial y filtros del autocompletado (Settings)
    bool confirmPending ();                                 // pregunta qué hacer con los cambios sin confirmar; false = cancelar
    void runBlock (bool byBlankLines);                      // Execute Current con un estilo de delimitación
    void syncSplitView ();                             // MainExtras.cpp: ayuda, impresión, SQL*Plus, sustitución, plantillas…
    bool applySubstitution (QStringList& stmts);
    void setupPanelCommands ();                             // paneles View > File Panel, Object Viewer, Properties, Snippets, History, Open Files (MainPanels.cpp)
    QJsonObject captureWorkspace () const;
    int applyWorkspace (const QJsonObject& ws, bool closeCurrent);
    bool closeAllTabs ();
    void saveWorkspaceTo (const QString& path);
    void openWorkspaceFile (const QString& path);
    QString saveSnapshot (bool autosave);
    void openSnapshotFile (const QString& path);
    void rebuildWorkspaceMenus ();
    QString snapshotDir () const;
    void rebuildRecentMenu ();
    void refreshOutline ();
    void applyEditorView (SqlEditor* only = nullptr);                                  // menús, barras de iconos y atajos (ver MainCommands.cpp)
    void reloadScript ();
    void saveAll ();
    void closeAllScripts ();
    void navigateError (int delta);
    void findObjectUnderCursor (bool viaMetadata = false);
    void extractSchemaDialog ();
    void refreshCaches ();
    void copyGrid (bool all, bool headersOnly);
    // ---- Panel de resultados (booklet): MainResults.cpp ----
    void onResultColumns (int index, const QStringList& names, const QVector<int>& right);
    void onResultRows (int index, const QVector<QStringList>& rows);
    void onResultDone (int index, qint64 rows, bool truncated);
    void clearRunResults ();                                // quita las pestañas de resultados de la ejecución anterior
    QString objectUnderCursor () const;                     // nombre (con esquema.) bajo el cursor: para Quick Query/Count
    void quickQuery (bool count);
    QStringList statementsFromCursor (QVector<int>* lines = nullptr) const;
    void executeAndStep ();
    void setupResultsPanel ();                              // crea las páginas y la barra de iconos
    void setupResultsCommands ();                           // enlaza los comandos ID_BOOKLET_* / ID_GRID_*
    void showPage (int index);
    void setPageBadge (int index, const QString& badge);    // punto/contador sobre el icono de la página (Output, Errors)
    void rotateGrid (bool on);
    void moveGridRow (int how);                             // 0 inicio, 1 página arriba, 2 arriba, 3 abajo, 4 página abajo, 5 fin
    void pinResult ();
    void unpinResult ();
    void openResultWith (bool excel);
    void clearResult ();
    void gridSettings ();
    void explainPlan ();
    void refreshBinds (const QString& text);
    QStringList applyBinds (QStringList stmts);             // sustituye :variables por literales; devuelve las sentencias
    void addHistory (int index, const QString& sql, qint64 rows, double seconds, const QString& error);
    void reloadHistoryTable ();
    void historyStep (int delta);
    void showStats (const QVector<QStringList>& rows);
    void applyGridOptions ();
    QVariantMap paramsFromEnvironment () const;
    void runCurrent ();                    // selección, o la sentencia bajo el cursor
    void runScript ();                     // selección, o todo el texto, sentencia por sentencia
    void onCompileErrors (const QVector<QStringList>& rows, const QString& objectName);
    // Pestañas de script
    void newScript ();
    void openScript ();
    bool saveScript ();
    bool saveScriptAs ();
    bool closeScript (int index);
    void onOutput (const QString& text);
    void onMessage (const QString& line);
    void cancelQuery ();
    void onConnected (const QString& version, const QString& who);
    void onDisconnected ();
    void onColumns (const QStringList& names, const QVector<int>& right);
    void onRows (const QVector<QStringList>& rows);
    void onFinished (qint64 rows, double seconds, const QString& message);
    void onFailed (const QString& message);
private:
    void setBusy (bool busy);
    void copySelection (bool withHeaders);
    void exportCsv ();
    QString exportText (const ExportOptions& o) const;
    void saveScreenshotAndQuit ();

    void startRun (const QStringList& stmts, const QVector<int>& lines);
    SqlEditor* editor () const;
    SqlEditor* addEditor (const QString& path = QString());
    SqlEditor* blankEditor ();
    void updateEditorTitle (SqlEditor* e);
    bool saveEditor (SqlEditor* e, bool saveAs);
    void restoreSession ();
    void saveSession ();
    void closeEvent (QCloseEvent*) override;
    QStringList m_synonymNames;
    QString m_pendingProfileId, m_pendingTag;
    ObjectBrowser* m_browser; CompletionData* m_meta;
    void updateBookmarkActions ();
    QTimer* m_bkmTimer = nullptr; int m_srcDbMs = -1;
    QTabWidget* m_editors; QTableView* m_table; QSplitter* m_split;
    QTabWidget* m_tabs; QPlainTextEdit* m_output; QTableWidget* m_errors;
    QLabel* m_status; QLabel* m_conn;
    QWidget* m_resultsHost = nullptr; QToolBar* m_booklet = nullptr; QActionGroup* m_pageGroup = nullptr; QList<QAction*> m_pageActions;
    QStackedWidget* m_resultStack = nullptr; QTabWidget* m_resultTabs = nullptr; QTableView* m_rotated = nullptr; RotatedModel* m_rotatedModel = nullptr;
    QTableWidget *m_stats = nullptr, *m_history = nullptr, *m_binds = nullptr; QPlainTextEdit* m_plan = nullptr;
    HistoryStore* m_historyStore = nullptr; int m_histPos = -1; int m_pinCount = 0;
    QMap<QString, BindValue> m_bindValues; QVector<QStringList> m_statsExtra; double m_lastSeconds = 0; qint64 m_lastRows = 0; QStringList m_lastStatements;
    QMap<int, ResultModel*> m_extraModels; QMap<int, int> m_extraTabs;
    QSplitter* m_editorSplit = nullptr; QPlainTextEdit* m_view2 = nullptr;
    bool m_dirty = false; std::function<void(const QString&)> m_showInViewer;
    QHash<QString, QString> m_substValues; QPrinter* m_printer = nullptr; QString m_lastUser, m_lastAlias;
    QString m_workspacePath; QMenu *m_wsRecentMenu = nullptr, *m_snapRecentMenu = nullptr;
    QMenu* m_recentMenu = nullptr; QDockWidget* m_outlineDock = nullptr; QTreeWidget* m_outline = nullptr; FindInFilesDialog* m_fifDlg = nullptr;
    QDockWidget* m_dock = nullptr; CommandMenu* m_menu = nullptr; FindReplaceDialog* m_findDlg; QLabel* m_connLabel;
    ResultModel* m_model;
    QThread m_thread; DbWorker* m_worker;
    // Segunda conexión en segundo plano (como SQLTools de Windows): las listas del explorador se leen aquí para no retrasar lo que pide el usuario
    QThread m_bgThread; DbWorker* m_bgWorker = nullptr; bool m_bgReady = false; QVariantMap m_connParams;
    void startConnect (const QVariantMap& params);
    DbWorker* listWorker () const { return m_bgReady ? m_bgWorker : m_worker; }
    bool m_connected = false, m_busy = false, m_firstBatch = true;
    QString m_shotPath;
};
