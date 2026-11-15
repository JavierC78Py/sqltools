#include "SqlEditor.h"
#include <QGuiApplication>
#include <QStyleHints>
#include "SqlHighlighter.h"
#include "SqlSplitter.h"
#include "SqlContext.h"
#include <QSet>
#include "CompletionData.h"
#include <QAbstractItemView>
#include <QCompleter>
#include <QKeyEvent>
#include <QScrollBar>
#include <QStandardItemModel>
#include "Snippets.h"
#include <QDateTime>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextDocumentLayout>
#include <QMouseEvent>
#include "Theme.h"
#include <QFile>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextOption>
#include <QFileInfo>
#include <QFontDatabase>
#include <QApplication>
#include <QClipboard>
#include <QSaveFile>
#include <QSettings>
#include "TextCodec.h"
#include <QStringConverter>
#include <QTextBlock>
#include <QTextEdit>
#include <QTimer>

// El margen es un widget aparte que se pinta con los datos del editor
class GutterArea : public QWidget
{
public:
    explicit GutterArea (SqlEditor* e) : QWidget(e), m_ed(e) {}
    QSize sizeHint () const override { return QSize(m_ed->gutterWidth(), 0); }
protected:
    void paintEvent (QPaintEvent* ev) override { m_ed->gutterPaint(ev); }
    void mousePressEvent (QMouseEvent* ev) override { m_ed->gutterMouse(ev); }
private:
    SqlEditor* m_ed;
};


class RulerArea : public QWidget
{
public:
    explicit RulerArea (SqlEditor* e) : QWidget(e), m_ed(e) {}
protected:
    void paintEvent (QPaintEvent* ev) override { m_ed->rulerPaint(ev); }
private:
    SqlEditor* m_ed;
};

namespace { const int MatchRole = Qt::UserRole + 1, BodyRole = Qt::UserRole + 2; }

SqlEditor::SqlEditor (QWidget* parent) : QPlainTextEdit(parent)
{
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont); mono.setFamilies({"Menlo", "Monaco", "Courier New"}); mono.setStyleHint(QFont::Monospace); mono.setPointSize(13);
    setFont(mono);
    setTabStopDistance(4 * QFontMetricsF(mono).horizontalAdvance(' '));
    setPlaceholderText("Escriba SQL y pulse F5 para ejecutar la sentencia donde está el cursor (o la selección). Mayús+F5 ejecuta todo el script.");
    m_hl = new SqlHighlighter(document());
    setLineWrapMode(QSettings().value("editor/wordWrap", false).toBool() ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    m_timer = new QTimer(this); m_timer->setSingleShot(true); m_timer->setInterval(150);
    connect(m_timer, &QTimer::timeout, this, &SqlEditor::updateCurrentStatement);
    connect(this, &QPlainTextEdit::cursorPositionChanged, m_timer, qOverload<>(&QTimer::start));
    connect(this, &QPlainTextEdit::textChanged, m_timer, qOverload<>(&QTimer::start));
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this]{ m_hl->refreshColors(); updateCurrentStatement(); });   // tema claro/oscuro

    m_gutter = new GutterArea(this);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this]{ updateGutterWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& r, int dy){ if (dy) m_gutter->scroll(0, dy); else m_gutter->update(0, r.y(), m_gutter->width(), r.height()); });
    updateGutterWidth();
    m_foldTimer = new QTimer(this); m_foldTimer->setSingleShot(true); m_foldTimer->setInterval(350);
    connect(m_foldTimer, &QTimer::timeout, this, &SqlEditor::refreshFolds);
    connect(this, &QPlainTextEdit::textChanged, m_foldTimer, qOverload<>(&QTimer::start));
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this]{                // si el cursor cae en una línea oculta, se despliega
        if (textCursor().block().isVisible()) return;
        const int line = textCursor().blockNumber(); bool changed = false;
        for (const auto& r : m_regions) if (line > r.start && line <= r.end) if (auto* bi = dynamic_cast<BlockInfo*>(document()->findBlockByNumber(r.start).userData())) if (bi->collapsed) { bi->collapsed = false; changed = true; }
        if (changed) applyFolds(); });

    m_model = new QStandardItemModel(this);
    m_completer = new QCompleter(m_model, this);
    m_completer->setWidget(this);
    m_completer->setCompletionMode(QCompleter::PopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setFilterMode(Qt::MatchStartsWith);
    m_completer->setMaxVisibleItems(12);
    m_completer->setCompletionRole(MatchRole);       // se filtra por MatchRole; lo que se ve es DisplayRole
    connect(m_completer, qOverload<const QModelIndex&>(&QCompleter::activated), this, &SqlEditor::acceptCompletion);
}

static const QStringList& sqlKeywords ()
{
    static const QStringList k = {
        "SELECT", "FROM", "WHERE", "AND", "OR", "NOT", "IN", "IS NULL", "IS NOT NULL", "LIKE", "BETWEEN", "EXISTS", "ORDER BY", "GROUP BY", "HAVING", "UNION", "UNION ALL",
        "INSERT INTO", "VALUES", "UPDATE", "SET", "DELETE FROM", "MERGE INTO", "USING", "WHEN MATCHED THEN", "WHEN NOT MATCHED THEN", "CREATE OR REPLACE", "ALTER TABLE",
        "DROP", "TRUNCATE TABLE", "JOIN", "INNER JOIN", "LEFT JOIN", "RIGHT JOIN", "FULL OUTER JOIN", "CROSS JOIN", "ON", "AS", "DISTINCT", "CASE", "WHEN", "THEN", "ELSE", "END",
        "BEGIN", "DECLARE", "EXCEPTION", "LOOP", "END LOOP", "IF", "ELSIF", "END IF", "FOR", "WHILE", "RETURN", "CURSOR", "OPEN", "FETCH", "CLOSE", "COMMIT", "ROLLBACK",
        "NUMBER", "VARCHAR2", "DATE", "TIMESTAMP", "CLOB", "BLOB", "BOOLEAN", "PLS_INTEGER", "ROWTYPE", "BULK COLLECT INTO", "FORALL", "EXECUTE IMMEDIATE", "RAISE", "RAISE_APPLICATION_ERROR",
        "NVL", "NVL2", "COALESCE", "DECODE", "TO_CHAR", "TO_DATE", "TO_NUMBER", "TO_TIMESTAMP", "SUBSTR", "INSTR", "LENGTH", "UPPER", "LOWER", "TRIM", "LTRIM", "RTRIM", "REPLACE",
        "TRUNC", "ROUND", "SYSDATE", "SYSTIMESTAMP", "ADD_MONTHS", "MONTHS_BETWEEN", "LAST_DAY", "LISTAGG", "ROW_NUMBER", "RANK", "DENSE_RANK", "OVER", "PARTITION BY", "ROWNUM", "ROWID",
        "DUAL", "DBMS_OUTPUT.PUT_LINE", "SQLERRM", "SQLCODE", "FETCH FIRST", "ROWS ONLY" };
    return k;
}

// El editor de Qt reclama como propias combinaciones como ⌘↑ / ⇧⌘↑ (ir o seleccionar hasta el inicio del documento) y por eso
// los comandos del programa con ese atajo (Uppercase ⇧⌘↑, Lowercase ⇧⌘↓, Scroll ⌘↑/⌘↓) nunca se ejecutaban desde el teclado.
// Si una acción del menú tiene exactamente ese atajo, se le cede la tecla.
bool SqlEditor::event (QEvent* ev)
{
    if (ev->type() == QEvent::ShortcutOverride)
    {
        auto* k = static_cast<QKeyEvent*>(ev); const int key = k->key();
        if ((k->modifiers() & Qt::ControlModifier) && (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Backspace || key == Qt::Key_Delete || key == Qt::Key_Home || key == Qt::Key_End))
        {
            const QKeySequence ks(k->keyCombination());
            for (QWidget* w = window(); w; w = nullptr)
                for (QAction* a : w->findChildren<QAction*>())
                    if (a->isEnabled() && !a->isSeparator()) for (const QKeySequence& s : a->shortcuts()) if (s == ks) { ev->ignore(); return true; }      // la acción gana
        }
    }
    return QPlainTextEdit::event(ev);
}

void SqlEditor::keyPressEvent (QKeyEvent* e)
{
    // ---- Selección en columna ----
    {
        const bool shiftAlt = (e->modifiers() & Qt::ShiftModifier) && (e->modifiers() & Qt::AltModifier) && !(e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier));
        const int k = e->key();
        if (shiftAlt && (k == Qt::Key_Up || k == Qt::Key_Down || k == Qt::Key_Left || k == Qt::Key_Right))
        {
            if (!m_colMode) enterColumnMode();
            columnExtend(k == Qt::Key_Up ? -1 : k == Qt::Key_Down ? 1 : 0, k == Qt::Key_Left ? -1 : k == Qt::Key_Right ? 1 : 0); return;
        }
        if (m_colMode && k != Qt::Key_Shift && k != Qt::Key_Alt && k != Qt::Key_Control && k != Qt::Key_Meta && k != Qt::Key_AltGr && k != Qt::Key_CapsLock)   // las teclas modificadoras solas no cuentan
        {
            const bool plain = !(e->modifiers() & (Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier));
            if (k == Qt::Key_Escape) { exitColumnMode(); return; }
            if (k == Qt::Key_Backspace || k == Qt::Key_Delete)
            {
                int l0, l1, c0, c1; columnBlock(l0, l1, c0, c1);
                if (c1 > c0) { columnReplace({QString()}); return; }
                if (k == Qt::Key_Backspace && c0 == 0) return;
                QTextCursor c(document()); c.beginEditBlock();
                for (int l = l0; l <= l1; ++l) { QTextBlock b = document()->findBlockByNumber(l); const int at = k == Qt::Key_Backspace ? c0 - 1 : c0; if (at < b.text().size()) { QTextCursor x(b); x.setPosition(b.position() + at); x.deleteChar(); } }
                c.endEditBlock(); if (k == Qt::Key_Backspace) { --m_colC; --m_colAC; } viewport()->update(); return;
            }
            if (plain && !e->text().isEmpty() && e->text()[0].isPrint()) { columnReplace({e->text()}); return; }
            if (plain && (k == Qt::Key_Up || k == Qt::Key_Down || k == Qt::Key_Left || k == Qt::Key_Right || k == Qt::Key_Home || k == Qt::Key_End || k == Qt::Key_Return || k == Qt::Key_Enter || k == Qt::Key_Tab || k == Qt::Key_PageUp || k == Qt::Key_PageDown)) exitColumnMode();
            else if (plain || (e->modifiers() & Qt::ShiftModifier)) exitColumnMode();
        }
    }
    QAbstractItemView* popup = m_completer->popup();
    if (popup->isVisible())
    {
        // Normalmente el propio popup recibe estas teclas; si llegan al editor (p.ej. el foco sigue aquí) se tratan igual.
        switch (e->key())
        {
        case Qt::Key_Enter: case Qt::Key_Return: case Qt::Key_Tab:
        {
            const QModelIndex idx = popup->currentIndex().isValid() ? popup->currentIndex() : m_completer->completionModel()->index(0, 0);
            popup->hide();
            if (idx.isValid()) acceptCompletion(idx);
            e->accept(); return;
        }
        case Qt::Key_Escape: popup->hide(); e->accept(); return;
        case Qt::Key_Down: case Qt::Key_Up:
        {
            const int n = m_completer->completionModel()->rowCount(); if (n == 0) break;
            int row = popup->currentIndex().isValid() ? popup->currentIndex().row() : 0;
            row = (row + (e->key() == Qt::Key_Down ? 1 : n - 1)) % n;
            popup->setCurrentIndex(m_completer->completionModel()->index(row, 0));
            e->accept(); return;
        }
        default: break;
        }
    }
    // Ctrl+Espacio abre la lista a la fuerza (en macOS es la tecla Control, no Cmd)
    if (e->key() == Qt::Key_Space && (e->modifiers() & Qt::MetaModifier || e->modifiers() == Qt::ControlModifier)) { showCompletion(true); return; }

    QPlainTextEdit::keyPressEvent(e);

    const QString t = e->text();
    const bool typed = !t.isEmpty() && (t[0].isLetterOrNumber() || t[0] == '_' || t[0] == '$' || t[0] == '#' || t[0] == '.');
    const bool erased = e->key() == Qt::Key_Backspace && popup->isVisible();
    // Modo manual: la lista solo se abre con Ctrl+Espacio; una vez abierta se sigue filtrando al escribir.
    if (typed || erased) { if (autoMode == 2 || popup->isVisible() || (autoMode == 1 && t == ".")) showCompletion(false); }
    else if (popup->isVisible() && !t.isEmpty()) popup->hide();           // otro carácter (espacio, paréntesis…): se cierra
}

void SqlEditor::acceptCompletion (const QModelIndex& index)
{
    const QString completion = index.data(MatchRole).toString(), body = index.data(BodyRole).toString();
    const auto ctx = SqlContext::analyze(toPlainText(), textCursor().position());
    if (!body.isEmpty()) { insertSnippet(body, ctx.prefixStart); return; }
    QTextCursor tc = textCursor();
    tc.setPosition(ctx.prefixStart); tc.setPosition(textCursor().position(), QTextCursor::KeepAnchor);
    // respetar el estilo: si lo escrito estaba en minúsculas, se inserta en minúsculas
    QString text = completion;
    if (!ctx.prefix.isEmpty() && ctx.prefix == ctx.prefix.toLower()) text = completion.toLower();
    tc.insertText(text);
    setTextCursor(tc);
}

void SqlEditor::insertSnippet (QString body, int replaceFrom)
{
    QTextCursor tc = textCursor();
    const QString line = tc.block().text();
    int ind = 0; while (ind < line.size() && (line[ind] == ' ' || line[ind] == '\t')) ++ind;
    body.replace('\n', "\n" + line.left(ind));                    // las líneas siguientes conservan la sangría actual
    const int caret = body.indexOf("$0");
    body.remove("$0");
    tc.beginEditBlock();
    if (replaceFrom >= 0) { tc.setPosition(replaceFrom); tc.setPosition(textCursor().position(), QTextCursor::KeepAnchor); }
    const int start = tc.selectionStart();
    tc.insertText(body);
    if (caret >= 0) tc.setPosition(start + caret);
    tc.endEditBlock();
    setTextCursor(tc);
}

void SqlEditor::refreshCompletion () { if (!m_waitingFor.isEmpty() && hasFocus()) showCompletion(true); }

// Columnas de una tabla/vista (de la caché; se piden al servidor si faltan), de una CTE o de una subconsulta con alias.
QStringList SqlEditor::resolveColumns (const SqlContext& ctxIn, const QString& sourceIn, int depth)
{
    const SqlContext& ctx = ctxIn;
    QString source = sourceIn;
    if (!m_data->currentSchema.isEmpty() && source.startsWith(m_data->currentSchema + ".")) source = source.mid(m_data->currentSchema.size() + 1);   // el propio esquema
    QStringList out;
    if (depth > 6) return out;
    auto d = ctx.derived.constFind(source);
    if (d != ctx.derived.constEnd())
    {
        out = d->columns;
        for (const QString& t : d->starTables) out += resolveColumns(ctx, t, depth + 1);        // "select *" / "alias.*" -> columnas reales
        return out;
    }
    auto m = m_data->members.constFind(source);
    if (m != m_data->members.constEnd()) return m.value();
    const bool qualified = source.contains('.') && m_data->schemas.contains(source.section('.', 0, 0));
    bool usedInFrom = false;                                           // aparece en FROM/JOIN/UPDATE/INTO: p.ej. USER_TABLES o DUAL, que no están en la lista de tablas propias
    for (auto it = ctx.aliases.constBegin(); it != ctx.aliases.constEnd() && !usedInFrom; ++it) usedInFrom = (it.value() == sourceIn);
    if (qualified || usedInFrom || m_data->tables.contains(source, Qt::CaseInsensitive))
    {
        m_waitingFor = source;
        if (!m_data->requested.contains(source)) { m_data->requested.insert(source); emit columnsNeeded(source); }
    }
    return out;
}

void SqlEditor::showCompletion (bool force)
{
    if (!m_data) return;
    const QString text = toPlainText();
    const auto ctx = SqlContext::analyze(text, textCursor().position());
    if (!force && ctx.prefix.size() < 2 && ctx.qualifier.isEmpty()) { m_completer->popup()->hide(); return; }
    if (!force && ctx.prefix.isEmpty() && ctx.qualifier.isEmpty()) { m_completer->popup()->hide(); return; }

    QStringList cands;
    m_waitingFor.clear();
    if (!ctx.qualifier.isEmpty())
    {
        // "alias." o "tabla." -> columnas;  "paquete." -> miembros del paquete
        QString table = ctx.aliases.value(ctx.qualifier, ctx.qualifier);
        if (!ctx.qualifierSchema.isEmpty() && m_data->schemas.contains(ctx.qualifierSchema) && !ctx.aliases.contains(ctx.qualifier))
        {
            table = ctx.qualifierSchema + "." + ctx.qualifier;                  // "esquema.objeto." -> clave calificada
            if (ctx.qualifierSchema != m_data->currentSchema && !m_data->schemaTables.contains(ctx.qualifierSchema))
            {   // aún no se sabe qué objetos tiene ese esquema (¿es un paquete?): se piden y se reintenta al llegar
                m_waitingFor = ctx.qualifierSchema;
                if (!m_data->requested.contains("S:" + ctx.qualifierSchema)) { m_data->requested.insert("S:" + ctx.qualifierSchema); emit schemaObjectsNeeded(ctx.qualifierSchema); }
                m_model->clear(); m_completer->popup()->hide(); return;
            }
        }
        if (!ctx.aliases.contains(ctx.qualifier) && ctx.qualifierSchema.isEmpty() && !ctx.derived.contains(ctx.qualifier) && m_data->schemas.contains(ctx.qualifier))
        {   // "esquema." -> sus tablas y rutinas (se cargan a demanda)
            if (ctx.qualifier == m_data->currentSchema) { cands = m_data->tables; cands += m_data->routines; }
            else if (m_data->schemaTables.contains(ctx.qualifier)) { cands = m_data->schemaTables.value(ctx.qualifier); cands += m_data->schemaRoutines.value(ctx.qualifier); }
            else { m_waitingFor = ctx.qualifier; if (!m_data->requested.contains("S:" + ctx.qualifier)) { m_data->requested.insert("S:" + ctx.qualifier); emit schemaObjectsNeeded(ctx.qualifier); } }
        }
        else if (m_data->packages.contains(table))
        {
            auto it = m_data->members.constFind(table);
            if (it != m_data->members.constEnd()) cands = it.value();
            else { m_waitingFor = table; if (!m_data->requested.contains(table)) { m_data->requested.insert(table); emit membersNeeded(table); } }
        }
        else cands = resolveColumns(ctx, table);
    }
    else if (ctx.afterFrom) { cands = m_data->tables; cands += m_data->schemas; }
    else
    {
        QSet<QString> seen;
        for (auto it = ctx.aliases.constBegin(); it != ctx.aliases.constEnd(); ++it)        // columnas de las tablas, CTE y subconsultas de la sentencia
            if (!seen.contains(it.value())) { seen.insert(it.value()); cands += resolveColumns(ctx, it.value()); }
        cands += m_data->tables; cands += m_data->routines; cands += m_data->others; cands += sqlKeywords();
    }
    cands.removeDuplicates();
    m_model->clear();
    auto add = [&](const QString& display, const QString& match, const QString& body = QString()) {
        auto* it = new QStandardItem(display); it->setData(match, MatchRole); if (!body.isEmpty()) it->setData(body, BodyRole); m_model->appendRow(it); };
    const bool snippetsOk = ctx.qualifier.isEmpty() && !ctx.afterFrom;
    QList<const Snippet*> later;
    if (snippetsOk)
        for (const Snippet& sn : snippets())
        {
            if (!ctx.prefix.isEmpty() && sn.trigger.compare(ctx.prefix, Qt::CaseInsensitive) == 0) add(sn.trigger + "   ▸ " + sn.title, sn.trigger, sn.body);   // abreviatura exacta: primero
            else later << &sn;
        }
    for (const QString& c : cands) add(c, c);
    for (const Snippet* sn : later) add(sn->trigger + "   ▸ " + sn->title, sn->trigger, sn->body);
    m_completer->setCompletionPrefix(ctx.prefix);
    if (m_completer->completionCount() == 0) { m_completer->popup()->hide(); return; }
    if (!force && m_completer->completionCount() == 1 && m_completer->currentCompletion().compare(ctx.prefix, Qt::CaseInsensitive) == 0
        && !m_completer->completionModel()->index(0, 0).data(BodyRole).isValid())      // una plantilla sí se ofrece aunque ya esté completa
    { m_completer->popup()->hide(); return; }

    if (force && m_completer->completionCount() == 1 && !m_completer->completionModel()->index(0, 0).data(BodyRole).isValid()
        && m_completer->currentCompletion().compare(ctx.prefix, Qt::CaseInsensitive) != 0 && !ctx.prefix.isEmpty())
    { acceptCompletion(m_completer->completionModel()->index(0, 0)); return; }      // una sola coincidencia: se inserta directa
    m_completer->popup()->setCurrentIndex(m_completer->completionModel()->index(0, 0));
    QRect cr = cursorRect();
    QFontMetrics fm(font());
    // la lista se alinea con el inicio de la palabra, no con el cursor
    QTextCursor start = textCursor(); start.setPosition(ctx.prefixStart);
    cr.moveLeft(cursorRect(start).left());
    cr.setWidth(qMax(260, m_completer->popup()->sizeHintForColumn(0) + m_completer->popup()->verticalScrollBar()->sizeHint().width() + 16));
    m_completer->complete(cr);
}

QString SqlEditor::displayName () const { return !filePath.isEmpty() ? QFileInfo(filePath).fileName() : (!titleHint.isEmpty() ? titleHint : QStringLiteral("Sin título")); }

void SqlEditor::goToLine (int line, int column)
{
    QTextBlock b = document()->findBlockByNumber(qMax(0, line - 1));
    if (!b.isValid()) b = document()->lastBlock();
    QTextCursor c(b);
    c.setPosition(b.position() + qMin(qMax(0, column - 1), qMax(0, b.length() - 1)));
    setTextCursor(c); centerCursor(); setFocus();
}

void SqlEditor::updateCurrentStatement ()
{
    // Sin resaltado de la sentencia actual: el fondo del editor se mantiene uniforme.
    QList<QTextEdit::ExtraSelection> sels;
    setExtraSelections(sels);
}

bool SqlEditor::load (const QString& path, QString* error, const QString& forceEncoding)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = f.errorString(); return false; }
    QByteArray data = f.readAll();
    QString text; bom = false;
    if (!forceEncoding.isEmpty()) { encoding = forceEncoding; if (encoding == "UTF-8" && data.startsWith("\xEF\xBB\xBF")) { bom = true; data.remove(0, 3); } text = TextCodec::decode(data, encoding); }
    else if (data.startsWith("\xFF\xFE") || data.startsWith("\xFE\xFF")) { encoding = "UTF-16"; text = TextCodec::decode(data, encoding); }
    else
    {
        bom = data.startsWith("\xEF\xBB\xBF"); if (bom) data.remove(0, 3);
        // UTF-8 estricto; si no es válido se asume Windows-1252 (habitual en scripts creados en Windows)
        QStringDecoder utf8(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
        text = utf8(data);
        if (utf8.hasError()) { encoding = "Windows-1252"; text = TextCodec::decode(data, encoding); } else encoding = "UTF-8";
    }
    crlf = text.contains("\r\n"); cr = !crlf && text.contains('\r') && !text.contains('\n');
    if (crlf) text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    setPlainText(text);
    document()->setModified(false);
    filePath = path;
    return true;
}

bool SqlEditor::save (const QString& path, QString* error)
{
    QString text = toPlainText();
    if (crlf) text.replace('\n', "\r\n"); else if (cr) text.replace('\n', '\r');
    QByteArray data = TextCodec::encode(text, encoding);
    if (encoding == "UTF-8" && bom) data.prepend("\xEF\xBB\xBF");
    QSaveFile f(path);                                           // escritura atómica: no deja el archivo a medias
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) { if (error) *error = f.errorString(); return false; }
    filePath = path;
    document()->setModified(false);
    return true;
}

QStringList SqlEditor::candidates () const
{
    QStringList out;
    for (int i = 0; i < m_completer->completionCount(); ++i) out << m_completer->completionModel()->index(i, 0).data(MatchRole).toString();
    return out;
}

bool SqlEditor::completionPopupVisible () const { return m_completer->popup()->isVisible(); }
void SqlEditor::completionHide () { m_completer->popup()->hide(); }


// ======================= Comandos de texto =======================
namespace {
// Cursor que cubre las líneas completas tocadas por la selección (o la línea actual)
QTextCursor wholeLines (const QPlainTextEdit* e)
{
    QTextCursor c = e->textCursor();
    const int a = c.selectionStart(), b = c.selectionEnd();
    QTextCursor r(e->document());
    r.setPosition(a); r.movePosition(QTextCursor::StartOfBlock);
    r.setPosition(b, QTextCursor::KeepAnchor);
    if (b > a && e->document()->findBlock(b).position() == b) r.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor);   // la selección termina al inicio de una línea: esa no cuenta
    r.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    return r;
}
}

void SqlEditor::indentSelection (bool undent)
{
    QTextCursor r = wholeLines(this);
    QStringList lines = r.selectedText().split(QChar(0x2029));
    for (QString& l : lines)
    {
        if (!undent) { if (!l.isEmpty()) l.prepend('\t'); }
        else if (l.startsWith('\t')) l.remove(0, 1);
        else { int n = 0; while (n < 4 && n < l.size() && l[n] == ' ') ++n; l.remove(0, n); }
    }
    r.insertText(lines.join(QChar(0x2029)));
}

void SqlEditor::commentLines (bool comment)
{
    QTextCursor r = wholeLines(this);
    QStringList lines = r.selectedText().split(QChar(0x2029));
    for (QString& l : lines)
    {
        if (comment) { if (!l.trimmed().isEmpty()) l.prepend("-- "); }                                   // en la columna 1, como SQLTools
        else if (l.startsWith("--")) l.remove(0, l.mid(2, 1) == " " ? 3 : 2);
    }
    r.insertText(lines.join(QChar(0x2029)));
}

void SqlEditor::changeCase (int mode)
{
    QTextCursor c = textCursor(); const bool had = c.hasSelection(); const int origPos = c.position();
    if (!had) c.select(QTextCursor::WordUnderCursor);
    const int selStart = c.selectionStart();
    QString t = c.selectedText();
    switch (mode)
    {
    case 0: t = t.toLower(); break;
    case 1: t = t.toUpper(); break;
    case 2: { bool start = true; for (QChar& ch : t) { if (ch.isLetter()) { ch = start ? ch.toUpper() : ch.toLower(); start = false; } else start = true; } break; }
    default: for (QChar& ch : t) ch = ch.isUpper() ? ch.toLower() : ch.toUpper(); break;
    }
    c.insertText(t);
    if (had) { c.setPosition(selStart); c.setPosition(selStart + t.size(), QTextCursor::KeepAnchor); }      // la selección se conserva (se puede encadenar ⇧⌘↑ y ⇧⌘↓)
    else c.setPosition(origPos);
    setTextCursor(c);
}

void SqlEditor::duplicateLineOrSelection ()
{
    QTextCursor c = textCursor();
    if (c.hasSelection()) { const QString t = c.selectedText(); const int end = c.selectionEnd(); c.setPosition(end); c.insertText(t); return; }
    c.beginEditBlock(); c.movePosition(QTextCursor::EndOfBlock);
    const QString line = c.block().text(); c.insertText("\n" + line); c.endEditBlock(); setTextCursor(c);
}

void SqlEditor::deleteLine ()
{
    QTextCursor c = textCursor(); c.beginEditBlock();
    c.select(QTextCursor::BlockUnderCursor);
    if (c.selectedText().isEmpty() || c.selectionStart() == 0) { c.movePosition(QTextCursor::StartOfBlock); c.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor); }
    c.removeSelectedText(); c.endEditBlock(); setTextCursor(c);
}

void SqlEditor::selectLine () { QTextCursor c = textCursor(); c.movePosition(QTextCursor::StartOfBlock); c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor); setTextCursor(c); }
void SqlEditor::selectWord () { QTextCursor c = textCursor(); c.select(QTextCursor::WordUnderCursor); setTextCursor(c); }

QString SqlEditor::wordUnderCursor () const
{
    QTextCursor c = textCursor();
    if (c.hasSelection()) return c.selectedText();
    c.select(QTextCursor::WordUnderCursor);
    return c.selectedText();
}

void SqlEditor::trimSpaces (int mode)
{
    QTextCursor r = textCursor().hasSelection() ? wholeLines(this) : QTextCursor(document());
    if (!textCursor().hasSelection()) r.select(QTextCursor::Document);
    QStringList lines = r.selectedText().split(QChar(0x2029));
    for (QString& l : lines)
    {
        if (mode == 0) { int i = 0; while (i < l.size() && l[i].isSpace()) ++i; l.remove(0, i); }
        else if (mode == 1) { int n = l.size(); while (n > 0 && l[n - 1].isSpace()) --n; l.truncate(n); }
        else l.replace(QRegularExpression("(?<=\\S)[ \\t]{2,}(?=\\S)"), " ");
    }
    r.insertText(lines.join(QChar(0x2029)));
}

void SqlEditor::removeBlankLines (bool onlyExcessive)
{
    QTextCursor r = textCursor().hasSelection() ? wholeLines(this) : QTextCursor(document());
    if (!textCursor().hasSelection()) r.select(QTextCursor::Document);
    QStringList in = r.selectedText().split(QChar(0x2029)), out; bool prevBlank = false;
    for (const QString& l : in)
    {
        const bool blank = l.trimmed().isEmpty();
        if (blank && (!onlyExcessive || prevBlank)) { prevBlank = blank; continue; }
        out << l; prevBlank = blank;
    }
    r.insertText(out.join(QChar(0x2029)));
}

void SqlEditor::joinLines ()
{
    QTextCursor r = textCursor().hasSelection() ? wholeLines(this) : textCursor();
    if (!textCursor().hasSelection()) { r.movePosition(QTextCursor::StartOfBlock); r.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor); r.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor); }
    QStringList lines = r.selectedText().split(QChar(0x2029));
    for (QString& l : lines) l = l.trimmed();
    r.insertText(lines.join(' '));
}

void SqlEditor::sortLines (bool descending)
{
    QTextCursor r = wholeLines(this);
    QStringList lines = r.selectedText().split(QChar(0x2029));
    std::sort(lines.begin(), lines.end(), [descending](const QString& a, const QString& b){ return descending ? QString::compare(a, b, Qt::CaseInsensitive) > 0 : QString::compare(a, b, Qt::CaseInsensitive) < 0; });
    r.insertText(lines.join(QChar(0x2029)));
}

void SqlEditor::insertDateStamp () { textCursor().insertText(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")); }

void SqlEditor::setShowWhitespace (bool on)
{
    QTextOption o = document()->defaultTextOption();
    o.setFlags(on ? (o.flags() | QTextOption::ShowTabsAndSpaces) : (o.flags() & ~QTextOption::ShowTabsAndSpaces));
    document()->setDefaultTextOption(o);
}
bool SqlEditor::showWhitespace () const { return document()->defaultTextOption().flags() & QTextOption::ShowTabsAndSpaces; }

// ======================= Buscar y reemplazar =======================
static QRegularExpression buildRegex (const QString& text, bool cs, bool whole, bool regex)
{
    QString pat = regex ? text : QRegularExpression::escape(text);
    if (whole) pat = "\\b(?:" + pat + ")\\b";
    return QRegularExpression(pat, cs ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption);
}

bool SqlEditor::findText (const QString& text, bool backward, bool cs, bool whole, bool regex)
{
    if (text.isEmpty()) return false;
    const QRegularExpression re = buildRegex(text, cs, whole, regex);
    if (!re.isValid()) return false;
    QTextDocument::FindFlags fl; if (backward) fl |= QTextDocument::FindBackward;
    QTextCursor found = document()->find(re, textCursor(), fl);
    if (found.isNull())                                          // da la vuelta al documento
    {
        QTextCursor from(document()); from.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
        found = document()->find(re, from, fl);
    }
    if (found.isNull()) return false;
    setTextCursor(found); centerCursor();
    return true;
}

bool SqlEditor::replaceCurrent (const QString& text, const QString& with, bool cs, bool whole, bool regex)
{
    QTextCursor c = textCursor();
    const QRegularExpression re = buildRegex(text, cs, whole, regex);
    if (c.hasSelection() && re.isValid())
    {
        auto m = re.match(c.selectedText());
        if (m.hasMatch() && m.capturedLength() == c.selectedText().size())
        { QString r = with; if (regex) { r = c.selectedText(); r.replace(re, with); } c.insertText(r); setTextCursor(c); }
    }
    return findText(text, false, cs, whole, regex);
}

int SqlEditor::replaceAll (const QString& text, const QString& with, bool cs, bool whole, bool regex)
{
    if (text.isEmpty()) return 0;
    const QRegularExpression re = buildRegex(text, cs, whole, regex);
    if (!re.isValid()) return 0;
    QTextCursor edit(document()); edit.beginEditBlock();
    int n = 0; QTextCursor pos(document());
    while (true)
    {
        QTextCursor f = document()->find(re, pos);
        if (f.isNull()) break;
        QString r = with; if (regex) { r = f.selectedText(); r.replace(re, with); }
        f.insertText(r); pos = f; ++n;
        if (n > 1000000) break;
    }
    edit.endEditBlock();
    return n;
}


// ======================= Margen (números de línea, marcadores, plegado) =======================
namespace { constexpr int kMarkW = 14, kFoldW = 14; }

int SqlEditor::gutterWidth () const
{
    int w = 0;
    if (m_lineNumbers) { int digits = 1; for (int n = qMax(1, blockCount()); n >= 10; n /= 10) ++digits; w += 10 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(3, digits); }
    w += kMarkW;                                            // columna de marcadores (siempre)
    if (m_foldGutter) w += kFoldW;
    return w;
}

void SqlEditor::applyEditorSettings ()
{
    QSettings st; QFont f = font(); f.setPointSize(st.value("editor/fontSize", 13).toInt());
    const QString fam = st.value("editor/fontFamily", "").toString(); if (!fam.isEmpty()) { f.setFamilies({fam, "Menlo", "Monaco", "Courier New"}); f.setFamily(fam); }
    setFont(f); if (m_hl) m_hl->refreshColors();
    setTabStopDistance(st.value("editor/tabWidth", 4).toInt() * QFontMetricsF(f).horizontalAdvance(' '));
    setLineWrapMode(st.value("editor/wordWrap", false).toBool() ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);     // por defecto, como el original: la línea sigue a la derecha hasta el Enter
    if (m_gutter) { updateGutterWidth(); m_gutter->update(); }
    viewport()->update();
}

void SqlEditor::updateGutterWidth ()
{
    setViewportMargins(gutterWidth(), rulerHeight(), 0, 0); const QRect cr = contentsRect();
    if (m_gutter) m_gutter->setGeometry(QRect(cr.left(), cr.top() + rulerHeight(), gutterWidth(), cr.height() - rulerHeight()));
    if (m_rulerArea) { m_rulerArea->setGeometry(QRect(cr.left(), cr.top(), cr.width(), rulerHeight())); m_rulerArea->setVisible(m_ruler); }
}
void SqlEditor::setShowRuler (bool on)
{
    m_ruler = on; if (on && !m_rulerArea) { m_rulerArea = new RulerArea(this); connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]{ if (m_rulerArea) m_rulerArea->update(); }); }
    updateGutterWidth(); viewport()->update(); if (m_rulerArea) m_rulerArea->update();
}
void SqlEditor::rulerPaint (QPaintEvent*)
{
    QPainter p(m_rulerArea); const QColor bg = palette().color(QPalette::Window); p.fillRect(m_rulerArea->rect(), Theme::dark() ? bg.lighter(110) : bg.darker(103));
    const qreal cw = QFontMetricsF(font()).horizontalAdvance(' '); const qreal x0 = gutterWidth() + contentOffset().x() + document()->documentMargin();
    p.setPen(Theme::muted()); QFont f = font(); f.setPointSizeF(f.pointSizeF() * 0.75); p.setFont(f); const int h = m_rulerArea->height();
    for (int col = 0; col < 400; ++col)
    {
        const qreal x = x0 + col * cw; if (x < gutterWidth() - 1) continue; if (x > m_rulerArea->width()) break;
        const int len = col % 10 == 0 ? h / 2 : (col % 5 == 0 ? h / 3 : h / 5); p.drawLine(QPointF(x, h - 1), QPointF(x, h - 1 - len));
        if (col % 10 == 0 && col > 0) p.drawText(QRectF(x + 2, 0, 40, h - 3), Qt::AlignLeft | Qt::AlignTop, QString::number(col));
    }
    if (m_colMode || true) { const int c = textCursor().positionInBlock(); p.setPen(QPen(palette().color(QPalette::Highlight), 2)); p.drawLine(QPointF(x0 + c * cw, h - 1), QPointF(x0 + c * cw, h - 7)); }   // posición del cursor
}
void SqlEditor::resizeEvent (QResizeEvent* e) { QPlainTextEdit::resizeEvent(e); updateGutterWidth(); }
void SqlEditor::setShowLineNumbers (bool on) { m_lineNumbers = on; updateGutterWidth(); m_gutter->update(); }
void SqlEditor::setShowFoldGutter (bool on) { m_foldGutter = on; updateGutterWidth(); m_gutter->update(); }

int SqlEditor::regionAt (int line, bool innermost) const        // región que EMPIEZA en esa línea (la más grande o la más pequeña)
{
    int best = -1;
    for (int i = 0; i < m_regions.size(); ++i)
        if (m_regions[i].start == line) { if (best < 0 || (innermost ? m_regions[i].end < m_regions[best].end : m_regions[i].end > m_regions[best].end)) best = i; }
    return best;
}

void SqlEditor::gutterPaint (QPaintEvent* ev)
{
    QPainter p(m_gutter);
    const QColor bg = palette().color(QPalette::Window); p.fillRect(ev->rect(), Theme::dark() ? bg.lighter(110) : bg.darker(103));
    const QColor text = Theme::muted();
    QTextBlock block = firstVisibleBlock(); int n = block.blockNumber();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top(), bottom = top + blockBoundingRect(block).height();
    const int numW = m_lineNumbers ? gutterWidth() - kMarkW - (m_foldGutter ? kFoldW : 0) : 0;
    while (block.isValid() && top <= ev->rect().bottom())
    {
        if (block.isVisible() && bottom >= ev->rect().top())
        {
            const int h = int(bottom - top); auto* bi = dynamic_cast<BlockInfo*>(block.userData());
            if (bi && (bi->bookmark || bi->number >= 0))                                   // marcador
            {
                p.setRenderHint(QPainter::Antialiasing); p.setBrush(QColor(0x3c, 0x82, 0xf0)); p.setPen(Qt::NoPen);
                const QRectF r(2, top + (h - 11) / 2.0, 10, 11); p.drawRoundedRect(r, 3, 3);
                if (bi->number >= 0) { p.setPen(Qt::white); QFont f = font(); f.setPointSize(8); f.setBold(true); p.setFont(f); p.drawText(r, Qt::AlignCenter, QString::number(bi->number)); }
            }
            if (m_lineNumbers) { p.setPen(n == textCursor().blockNumber() ? palette().color(QPalette::Text) : text); p.setFont(font()); p.drawText(kMarkW, int(top), numW - 6, h, Qt::AlignRight | Qt::AlignVCenter, QString::number(n + 1)); }
            if (m_foldGutter)                                                               // flecha de plegado
            {
                const int ri = regionAt(n, false);
                if (ri >= 0)
                {
                    const bool collapsed = bi && bi->collapsed; const int x = gutterWidth() - kFoldW + 3, cy = int(top) + h / 2;
                    p.setRenderHint(QPainter::Antialiasing); p.setPen(Qt::NoPen); p.setBrush(text);
                    QPainterPath tri;
                    if (collapsed) { tri.moveTo(x, cy - 4); tri.lineTo(x + 6, cy); tri.lineTo(x, cy + 4); }
                    else { tri.moveTo(x - 1, cy - 2); tri.lineTo(x + 7, cy - 2); tri.lineTo(x + 3, cy + 4); }
                    tri.closeSubpath(); p.drawPath(tri);
                }
            }
        }
        block = block.next(); top = bottom; bottom = top + blockBoundingRect(block).height(); ++n;
    }
}

static BlockInfo* info (QTextBlock b, bool create);

void SqlEditor::gutterMouse (QMouseEvent* ev)
{
    QTextBlock block = firstVisibleBlock();
    qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
    while (block.isValid() && top <= ev->position().y())
    {
        const qreal bottom = top + blockBoundingRect(block).height();
        if (block.isVisible() && ev->position().y() < bottom)
        {
            const int line = block.blockNumber();
            if (m_foldGutter && ev->position().x() >= gutterWidth() - kFoldW)
            { if (m_regions.isEmpty()) refreshFolds(); const int ri = regionAt(line, false); if (ri >= 0) { auto* bi = info(document()->findBlockByNumber(m_regions[ri].start), true); bi->collapsed = !bi->collapsed; applyFolds(); } }
            else { QTextCursor c(block); setTextCursor(c); toggleBookmark(); }
            return;
        }
        top = bottom; block = block.next();
    }
}

// Pequeño recuadro «…» al final de las líneas plegadas
void SqlEditor::paintEvent (QPaintEvent* e)
{
    QPlainTextEdit::paintEvent(e);
    QPainter p(viewport()); p.setRenderHint(QPainter::Antialiasing);
    if (m_colMode)              // el bloque de la selección en columna (sin ancho = la línea vertical del cursor)
    {
        const qreal cw = QFontMetricsF(font()).horizontalAdvance(' '); const qreal x0 = contentOffset().x() + document()->documentMargin();
        int l0, l1, c0, c1; columnBlock(l0, l1, c0, c1); QColor hl = Theme::dark() ? QColor(90, 150, 255) : QColor(60, 120, 230);
        for (QTextBlock b = document()->findBlockByNumber(l0); b.isValid() && b.blockNumber() <= l1; b = b.next())
        {
            if (!b.isVisible()) continue; const QRectF g = blockBoundingGeometry(b).translated(contentOffset());
            if (g.bottom() < 0 || g.top() > viewport()->height()) continue;
            if (c1 > c0) { hl.setAlpha(80); p.fillRect(QRectF(x0 + c0 * cw, g.top(), (c1 - c0) * cw, g.height()), hl); }
            else p.fillRect(QRectF(x0 + c0 * cw - 1, g.top(), 2, g.height()), palette().color(QPalette::Text));
        }
    }
    if (m_columnMarkers)        // marcas de columna (80 y 120)
    {
        const qreal cw = QFontMetricsF(font()).horizontalAdvance(' '); const qreal x0 = contentOffset().x() + document()->documentMargin();
        QColor c = Theme::muted(); c.setAlpha(110); p.setPen(QPen(c, 1, Qt::DotLine));
        for (int col : {80, 120}) p.drawLine(QPointF(x0 + col * cw, 0), QPointF(x0 + col * cw, viewport()->height()));
    }
    if (m_indentGuides)         // guías de sangría: una línea por cada nivel de 4 espacios al principio de la línea
    {
        const qreal cw = QFontMetricsF(font()).horizontalAdvance(' '); const qreal x0 = contentOffset().x() + document()->documentMargin();
        QColor c = Theme::muted(); c.setAlpha(70); p.setPen(QPen(c, 1));
        for (QTextBlock b = firstVisibleBlock(); b.isValid(); b = b.next())
        {
            const QRectF g = blockBoundingGeometry(b).translated(contentOffset());
            if (g.top() > e->rect().bottom()) break;
            if (!b.isVisible()) continue;
            const QString t = b.text(); int col = 0;
            for (QChar ch : t) { if (ch == ' ') ++col; else if (ch == '\t') col += 4 - col % 4; else break; }
            for (int lv = 4; lv < col; lv += 4) p.drawLine(QPointF(x0 + lv * cw, g.top()), QPointF(x0 + lv * cw, g.bottom()));
        }
    }
    for (QTextBlock b = firstVisibleBlock(); b.isValid(); b = b.next())
    {
        const QRectF g = blockBoundingGeometry(b).translated(contentOffset());
        if (g.top() > e->rect().bottom()) break;
        if (!b.isVisible()) continue;
        auto* bi = dynamic_cast<BlockInfo*>(b.userData()); if (!bi || !bi->collapsed) continue;
        const int x = int(g.left()) + fontMetrics().horizontalAdvance(b.text().replace('\t', "    ")) + 12;
        const QRectF r(x, g.top() + 2, 30, g.height() - 4);
        p.setPen(Theme::muted()); p.setBrush(Theme::dark() ? QColor(255, 255, 255, 25) : QColor(0, 0, 0, 20)); p.drawRoundedRect(r, 4, 4);
        p.drawText(r, Qt::AlignCenter, QStringLiteral("⋯"));
    }
}

// ======================= Marcadores =======================
static BlockInfo* info (QTextBlock b, bool create)
{
    auto* bi = dynamic_cast<BlockInfo*>(b.userData());
    if (!bi && create) { bi = new BlockInfo(); b.setUserData(bi); }
    return bi;
}

void SqlEditor::toggleBookmark ()
{
    QTextBlock b = textCursor().block(); BlockInfo* bi = info(b, true);
    if (bi->bookmark || bi->number >= 0) { bi->bookmark = false; bi->number = -1; } else bi->bookmark = true;
    m_gutter->update(); emit bookmarksChanged();
}

void SqlEditor::nextBookmark (bool forward)
{
    const int cur = textCursor().blockNumber(), total = document()->blockCount();
    for (int i = 1; i <= total; ++i)
    {
        const int idx = ((cur + (forward ? i : -i)) % total + total) % total;
        QTextBlock b = document()->findBlockByNumber(idx); auto* bi = info(b, false);
        if (bi && (bi->bookmark || bi->number >= 0)) { setTextCursor(QTextCursor(b)); centerCursor(); return; }
    }
}

void SqlEditor::clearBookmarks () { for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) if (auto* bi = info(b, false)) { bi->bookmark = false; bi->number = -1; } m_gutter->update(); emit bookmarksChanged(); }

void SqlEditor::setNumberedBookmark (int n)
{
    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) if (auto* bi = info(b, false); bi && bi->number == n) bi->number = -1;
    info(textCursor().block(), true)->number = n; m_gutter->update(); emit bookmarksChanged();
}

int SqlEditor::numberedBookmarkLine (int n) const { for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) if (auto* bi = dynamic_cast<BlockInfo*>(b.userData()); bi && bi->number == n) return b.blockNumber() + 1; return 0; }

void SqlEditor::gotoNumberedBookmark (int n) { const int line = numberedBookmarkLine(n); if (line > 0) { setTextCursor(QTextCursor(document()->findBlockByNumber(line - 1))); centerCursor(); } }

QList<int> SqlEditor::bookmarkedLines () const { QList<int> out; for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) if (auto* bi = dynamic_cast<BlockInfo*>(b.userData()); bi && (bi->bookmark || bi->number >= 0)) out << b.blockNumber() + 1; return out; }

QString SqlEditor::takeBookmarkedLines (bool remove)
{
    QStringList lines; QList<int> nums = bookmarkedLines();
    for (int l : nums) lines << document()->findBlockByNumber(l - 1).text();
    if (remove)
    {
        QTextCursor c(document()); c.beginEditBlock();
        for (int i = nums.size() - 1; i >= 0; --i) { QTextBlock b = document()->findBlockByNumber(nums[i] - 1); QTextCursor d(b); d.select(QTextCursor::BlockUnderCursor); if (b.blockNumber() == 0) { d.setPosition(b.position()); d.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor); } d.removeSelectedText(); }
        c.endEditBlock();
    }
    return lines.join('\n');
}

// ======================= Plegado =======================
void SqlEditor::refreshFolds ()
{
    m_regions = FoldRegions::compute(toPlainText());
    // un bloque marcado como plegado que ya no inicia ninguna región deja de estarlo
    QSet<int> starts; for (const auto& r : m_regions) starts.insert(r.start);
    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) if (auto* bi = info(b, false); bi && bi->collapsed && !starts.contains(b.blockNumber())) bi->collapsed = false;
    applyFolds();
}

void SqlEditor::applyFolds ()
{
    bool changed = false;
    for (QTextBlock b = document()->begin(); b.isValid(); b = b.next()) if (!b.isVisible()) { b.setVisible(true); changed = true; }
    for (const auto& r : m_regions)
    {
        auto* bi = dynamic_cast<BlockInfo*>(document()->findBlockByNumber(r.start).userData());
        if (!bi || !bi->collapsed) continue;
        for (int l = r.start + 1; l <= r.end; ++l) { QTextBlock b = document()->findBlockByNumber(l); if (b.isValid()) { b.setVisible(false); changed = true; } }
    }
    document()->markContentsDirty(0, document()->characterCount());
    if (auto* lay = qobject_cast<QPlainTextDocumentLayout*>(document()->documentLayout())) lay->requestUpdate();
    viewport()->update(); m_gutter->update();
    Q_UNUSED(changed)
}

bool SqlEditor::isLineVisible (int line) const { QTextBlock b = document()->findBlockByNumber(line); return b.isValid() && b.isVisible(); }

void SqlEditor::foldCurrent (int mode)
{
    if (m_regions.isEmpty()) refreshFolds();
    const int line = textCursor().blockNumber(); int best = -1;
    for (int i = 0; i < m_regions.size(); ++i) if (line >= m_regions[i].start && line <= m_regions[i].end) if (best < 0 || (m_regions[i].start >= m_regions[best].start && m_regions[i].end <= m_regions[best].end)) best = i;
    if (mode == 0)                                                   // plegar la región más interna que aún esté desplegada y contenga el cursor
        for (int i = m_regions.size() - 1; i >= 0; --i)
            if (line >= m_regions[i].start && line <= m_regions[i].end) { auto* bi = info(document()->findBlockByNumber(m_regions[i].start), true); if (!bi->collapsed) { bi->collapsed = true; best = i; break; } }
    if (best < 0) return;
    auto* bi = info(document()->findBlockByNumber(m_regions[best].start), true);
    if (mode == 1) bi->collapsed = false; else if (mode == 2) bi->collapsed = !bi->collapsed;
    if (bi->collapsed && textCursor().blockNumber() > m_regions[best].start) setTextCursor(QTextCursor(document()->findBlockByNumber(m_regions[best].start)));
    applyFolds();
}

void SqlEditor::expandSubtree ()
{
    const int line = textCursor().blockNumber(); int root = -1;
    for (int i = 0; i < m_regions.size(); ++i) if (m_regions[i].start == line || (line >= m_regions[i].start && line <= m_regions[i].end && root < 0)) root = i;
    if (root < 0) return;
    for (const auto& r : m_regions) if (r.start >= m_regions[root].start && r.end <= m_regions[root].end) if (auto* bi = info(document()->findBlockByNumber(r.start), false)) bi->collapsed = false;
    applyFolds();
}

void SqlEditor::foldAll (bool collapse)
{
    if (m_regions.isEmpty()) refreshFolds();
    for (const auto& r : m_regions) { auto* bi = info(document()->findBlockByNumber(r.start), collapse); if (bi) bi->collapsed = collapse; }
    if (collapse) { const int line = textCursor().blockNumber(); for (const auto& r : m_regions) if (line > r.start && line <= r.end) { setTextCursor(QTextCursor(document()->findBlockByNumber(r.start))); break; } }
    applyFolds();
}

void SqlEditor::toggleAllFolds ()
{
    bool anyCollapsed = false; for (const auto& r : m_regions) if (auto* bi = info(document()->findBlockByNumber(r.start), false)) anyCollapsed = anyCollapsed || bi->collapsed;
    foldAll(!anyCollapsed);
}

void SqlEditor::foldToLevel (int level)
{
    if (m_regions.isEmpty()) refreshFolds();
    // "nivel N" = se ven N niveles de anidamiento: las regiones de profundidad N-1 en adelante quedan plegadas
    for (const auto& r : m_regions) { auto* bi = info(document()->findBlockByNumber(r.start), true); bi->collapsed = r.level >= level - 1; }
    const int line = textCursor().blockNumber(); for (const auto& r : m_regions) if (r.level >= level - 1 && line > r.start && line <= r.end) { setTextCursor(QTextCursor(document()->findBlockByNumber(r.start))); break; }
    applyFolds();
}

// ======================= Paréntesis pareja =======================
bool SqlEditor::findMatchingBracket (bool select)
{
    const QString t = toPlainText(); int pos = textCursor().position();
    auto isOpen = [](QChar c) { return c == '(' || c == '[' || c == '{'; }; auto isClose = [](QChar c) { return c == ')' || c == ']' || c == '}'; };
    int at = -1; if (pos < t.size() && (isOpen(t[pos]) || isClose(t[pos]))) at = pos; else if (pos > 0 && (isOpen(t[pos - 1]) || isClose(t[pos - 1]))) at = pos - 1;
    if (at < 0) return false;
    const QChar c = t[at]; const QChar mate = c == '(' ? ')' : c == ')' ? '(' : c == '[' ? ']' : c == ']' ? '[' : c == '{' ? '}' : '{'; const bool fwd = isOpen(c);
    int depth = 0;
    for (int i = at; i >= 0 && i < t.size(); i += fwd ? 1 : -1)
    {
        if (t[i] == c) ++depth; else if (t[i] == mate && --depth == 0)
        {
            QTextCursor cur = textCursor();
            if (select) { cur.setPosition(qMin(at, i)); cur.setPosition(qMax(at, i) + 1, QTextCursor::KeepAnchor); } else cur.setPosition(i + (fwd ? 1 : 0));
            setTextCursor(cur); return true;
        }
    }
    return false;
}


// Esquema del código: una entrada por sentencia (con el nombre del objeto en los CREATE) y, dentro, los subprogramas.
QVector<SqlEditor::OutlineItem> SqlEditor::outline () const
{
    QVector<OutlineItem> out; const QString text = toPlainText(); const auto stmts = SqlSplitter::split(text);
    static const QRegularExpression create(R"(^\s*create\s+(?:or\s+replace\s+)?(?:(?:non)?editionable\s+)?(package\s+body|package|procedure|function|trigger|type\s+body|type|view|materialized\s+view|table|index|sequence|synonym)\s+(?:"?\w+"?\.)?("?[\w$#]+"?))", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression sub(R"(^\s*(procedure|function)\s+("?[\w$#]+"?))", QRegularExpression::CaseInsensitiveOption);
    const auto regions = FoldRegions::compute(text);
    for (const auto& st : stmts)
    {
        const QString first = st.text.section('\n', 0, 0).simplified();
        QString title, kind = "sql"; auto m = create.match(st.text);
        if (m.hasMatch()) { kind = m.captured(1).toLower().simplified(); title = kind + " " + m.captured(2); }
        else if (st.plsql) { kind = "block"; title = first.left(40); }
        else title = first.left(50);
        out.push_back({0, st.startLine, title, kind});
        if (!st.plsql) continue;
        const int sl = st.startLine - 1, el = sl + st.text.count('\n');
        for (const auto& r : regions)                       // subprogramas dentro de este paquete/bloque
            if ((r.kind == "sub") && r.start > sl && r.end <= el)
            {
                auto sm = sub.match(document()->findBlockByNumber(r.start).text());
                if (sm.hasMatch()) out.push_back({1 + qMax(0, r.level - 1), r.start + 1, sm.captured(1).toLower() + " " + sm.captured(2), sm.captured(1).toLower()});
            }
    }
    return out;
}


// ---------------- Más comandos de texto ----------------
void SqlEditor::deleteWord (bool forward)
{
    QTextCursor c = textCursor();
    if (!c.hasSelection()) c.movePosition(forward ? QTextCursor::NextWord : QTextCursor::PreviousWord, QTextCursor::KeepAnchor);
    c.removeSelectedText(); setTextCursor(c);
}

void SqlEditor::cutAppend ()
{
    QTextCursor c = textCursor(); if (!c.hasSelection()) return;
    QClipboard* cb = QApplication::clipboard(); cb->setText(cb->text() + c.selectedText().replace(QChar(0x2029), '\n'));
    c.removeSelectedText(); setTextCursor(c);
}

void SqlEditor::scrollLines (int delta) { verticalScrollBar()->setValue(verticalScrollBar()->value() + delta); }

// Rango de bloques que afecta un comando: la selección, o todo el documento si no hay.
static void blockRange (const QTextCursor& cur, QTextDocument* doc, int& first, int& last)
{
    if (!cur.hasSelection()) { first = 0; last = doc->blockCount() - 1; return; }
    first = doc->findBlock(cur.selectionStart()).blockNumber();
    QTextBlock lb = doc->findBlock(cur.selectionEnd());
    last = (lb.position() == cur.selectionEnd() && lb.blockNumber() > first) ? lb.blockNumber() - 1 : lb.blockNumber();
}

static QString tabifyLine (const QString& in, bool leadingOnly, int tw)
{
    // 1) expande tabuladores a espacios según columnas; 2) vuelve a agrupar los espacios en tabuladores
    QString ex; for (QChar ch : in) { if (ch == '\t') ex += QString(tw - ex.size() % tw, ' '); else ex += ch; }
    if (leadingOnly) { int n = 0; while (n < ex.size() && ex[n] == ' ') ++n; return QString(n / tw, '\t') + QString(n % tw, ' ') + ex.mid(n); }
    QString out; int i = 0, col = 0; bool inStr = false;
    while (i < ex.size())
    {
        if (ex[i] == '\'') inStr = !inStr;
        if (!inStr && ex[i] == ' ')
        {
            int j = i; while (j < ex.size() && ex[j] == ' ') ++j;
            int c = col, end = col + (j - i);
            while ((c / tw + 1) * tw <= end && (c / tw + 1) * tw - c >= 2) { out += '\t'; c = (c / tw + 1) * tw; }       // solo cambia tramos que llegan a un tope de tabulación
            out += QString(end - c, ' '); col = end; i = j; continue;
        }
        out += ex[i]; ++col; ++i;
    }
    return out;
}

void SqlEditor::tabify (bool leadingOnly)
{
    int a, b; blockRange(textCursor(), document(), a, b);
    QTextCursor c(document()); c.beginEditBlock();
    for (int n = a; n <= b; ++n) { QTextBlock bl = document()->findBlockByNumber(n); const QString t = bl.text(), r = tabifyLine(t, leadingOnly, 4); if (r != t) { QTextCursor x(bl); x.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor); x.insertText(r); } }
    c.endEditBlock();
}

void SqlEditor::untabify ()
{
    int a, b; blockRange(textCursor(), document(), a, b);
    QTextCursor c(document()); c.beginEditBlock();
    for (int n = a; n <= b; ++n)
    {
        QTextBlock bl = document()->findBlockByNumber(n); const QString t = bl.text(); if (!t.contains('\t')) continue;
        QString ex; for (QChar ch : t) { if (ch == '\t') ex += QString(4 - ex.size() % 4, ' '); else ex += ch; }
        QTextCursor x(bl); x.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor); x.insertText(ex);
    }
    c.endEditBlock();
}

void SqlEditor::normalizeKeywords ()
{
    static const QSet<QString> kw = [] { QSet<QString> k; for (const char* w : {
        "select","from","where","and","or","not","in","is","null","like","between","exists","order","by","group","having","union","all","intersect","minus","insert","into","values","update","set","delete","merge","using","when","matched","then","else","elsif","end","case","create","replace","alter","drop","truncate","table","view","index","sequence","trigger","package","body","procedure","function","type","join","inner","left","right","full","outer","cross","on","as","distinct","begin","declare","exception","loop","if","for","while","return","returns","cursor","open","fetch","close","commit","rollback","savepoint","raise","goto","exit","continue","default","constraint","primary","key","foreign","references","unique","check","grant","revoke","with","connect","start","prior","level","asc","desc","nulls","first","last","rows","only","over","partition","before","after","each","row","new","old","referencing","out","pragma","autonomous_transaction","bulk","collect","forall","execute","immediate","record","subtype","constant","of","to","add","modify","column","number","varchar2","char","date","timestamp","clob","blob","boolean","pls_integer","binary_integer","rowtype","some","any","limit","offset","fetch","next","is","true","false" }) k.insert(w); return k; }();
    int a, b; blockRange(textCursor(), document(), a, b);
    QTextCursor c(document()); c.beginEditBlock();
    bool inComment = false;
    for (int n = 0; n <= b; ++n)                // se recorre desde el principio para conocer el estado de los comentarios /* */
    {
        QTextBlock bl = document()->findBlockByNumber(n); const QString t = bl.text(); QString out; int i = 0; bool changed = false;
        while (i < t.size())
        {
            if (inComment) { const int e = t.indexOf("*/", i); if (e < 0) { out += t.mid(i); i = t.size(); } else { out += t.mid(i, e + 2 - i); i = e + 2; inComment = false; } continue; }
            const QChar ch = t[i];
            if (ch == '-' && i + 1 < t.size() && t[i + 1] == '-') { out += t.mid(i); i = t.size(); }
            else if (ch == '/' && i + 1 < t.size() && t[i + 1] == '*') { inComment = true; out += "/*"; i += 2; }
            else if (ch == '\'' || ch == '"') { int j = i + 1; while (j < t.size() && t[j] != ch) ++j; out += t.mid(i, qMin(j + 1, int(t.size())) - i); i = j + 1; }
            else if (ch.isLetter() || ch == '_')
            {
                int j = i; while (j < t.size() && (t[j].isLetterOrNumber() || t[j] == '_' || t[j] == '$' || t[j] == '#')) ++j;
                const QString w = t.mid(i, j - i); const bool afterDot = i > 0 && t[i - 1] == '.';
                if (!afterDot && n >= a && kw.contains(w.toLower())) { out += w.toUpper(); if (w != w.toUpper()) changed = true; } else out += w;
                i = j;
            }
            else { out += ch; ++i; }
        }
        if (changed && n >= a) { QTextCursor x(bl); x.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor); x.insertText(out); }
    }
    c.endEditBlock();
}

void SqlEditor::splitLines (int width)
{
    int a, b; blockRange(textCursor(), document(), a, b);
    QTextCursor c(document()); c.beginEditBlock();
    for (int n = b; n >= a; --n)                // de abajo hacia arriba para que los números de línea no se muevan
    {
        QTextBlock bl = document()->findBlockByNumber(n); QString t = bl.text(); if (t.size() <= width) continue;
        const int ind = t.size() - t.trimmed().size() > 0 ? t.indexOf(QRegularExpression("\\S")) : 0; QStringList parts;
        while (t.size() > width) { int cut = t.lastIndexOf(' ', width); if (cut <= ind) cut = t.indexOf(' ', width); if (cut < 0) break; parts << t.left(cut).trimmed(); t = QString(ind, ' ') + t.mid(cut + 1).trimmed(); }
        parts << t; QStringList out; for (int k = 0; k < parts.size(); ++k) out << (k == 0 ? QString(ind, ' ') + parts[k] : parts[k]);
        QTextCursor x(bl); x.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor); x.insertText(out.join('\n').mid(0));
    }
    c.endEditBlock();
}

void SqlEditor::insertNumbers (int start, int step, int width)
{
    if (m_colMode)      // en modo columna: un número por línea del bloque, en su columna izquierda
    {
        int l0, l1, c0, c1; columnBlock(l0, l1, c0, c1); QStringList nums; int v0 = start;
        for (int l = l0; l <= l1; ++l, v0 += step) nums << QString::number(v0).rightJustified(width, '0');
        m_colAC = m_colC = c0; columnReplace(nums); return;
    }
    QTextCursor cur = textCursor(); const int col = cur.hasSelection() ? cur.selectionStart() - document()->findBlock(cur.selectionStart()).position() : cur.positionInBlock();
    int a, b; blockRange(cur, document(), a, b); if (!cur.hasSelection()) { a = b = cur.blockNumber(); }
    QTextCursor c(document()); c.beginEditBlock(); int v = start;
    for (int n = a; n <= b; ++n)
    {
        QTextBlock bl = document()->findBlockByNumber(n); QTextCursor x(bl);
        const int pos = qMin(col, bl.length() - 1); if (pos > bl.text().size()) x.movePosition(QTextCursor::EndOfBlock); else x.setPosition(bl.position() + pos);
        if (bl.text().size() < col) x.insertText(QString(col - bl.text().size(), ' '));
        x.insertText(QString::number(v).rightJustified(width, '0')); v += step;
    }
    c.endEditBlock();
}

QString SqlEditor::pathUnderCursor () const
{
    const QTextCursor c = textCursor(); if (c.hasSelection()) return c.selectedText().trimmed();
    const QString t = c.block().text(); int i = c.positionInBlock(), s = i, e = i;
    auto ok = [](QChar ch) { return !(ch.isSpace() || ch == '\'' || ch == '"' || ch == '<' || ch == '>' || ch == '(' || ch == ')' || ch == ',' || ch == ';'); };
    while (s > 0 && ok(t[s - 1])) --s; while (e < t.size() && ok(t[e])) ++e;
    QString p = t.mid(s, e - s); if (p.startsWith('@')) p.remove(0, p.startsWith("@@") ? 2 : 1); return p;
}


// ---------------- Selección en columna ----------------
void SqlEditor::enterColumnMode ()
{
    QTextCursor c = textCursor();
    if (c.hasSelection())
    {
        const QTextBlock b0 = document()->findBlock(c.selectionStart()), b1 = document()->findBlock(c.selectionEnd());
        m_colAL = b0.blockNumber(); m_colAC = c.selectionStart() - b0.position(); m_colL = b1.blockNumber(); m_colC = c.selectionEnd() - b1.position();
    }
    else { m_colAL = m_colL = c.blockNumber(); m_colAC = m_colC = c.positionInBlock(); }
    m_colMode = true; viewport()->update();
}

void SqlEditor::exitColumnMode (bool keepCaret)
{
    if (!m_colMode) return;
    m_colMode = false;
    if (keepCaret) { QTextBlock b = document()->findBlockByNumber(qBound(0, m_colL, document()->blockCount() - 1)); QTextCursor c(b); c.setPosition(b.position() + qMin(m_colC, b.text().size())); setTextCursor(c); }
    viewport()->update();
}

void SqlEditor::columnExtend (int dLine, int dCol)
{
    m_colL = qBound(0, m_colL + dLine, document()->blockCount() - 1); m_colC = qMax(0, m_colC + dCol);
    QTextBlock b = document()->findBlockByNumber(m_colL); QTextCursor c(b); c.setPosition(b.position() + qMin(m_colC, b.text().size()));
    { QSignalBlocker sb(this); setTextCursor(c); }              // el cursor real acompaña al extremo móvil (para que el scroll lo siga)
    ensureCursorVisible(); viewport()->update(); if (m_rulerArea) m_rulerArea->update();
}

void SqlEditor::columnBlock (int& l0, int& l1, int& c0, int& c1) const
{
    l0 = qMin(m_colAL, m_colL); l1 = qMax(m_colAL, m_colL); c0 = qMin(m_colAC, m_colC); c1 = qMax(m_colAC, m_colC);
}

QString SqlEditor::columnText () const
{
    int l0, l1, c0, c1; columnBlock(l0, l1, c0, c1); QStringList out;
    for (int l = l0; l <= l1; ++l) out << document()->findBlockByNumber(l).text().mid(c0, c1 - c0);
    return out.join('\n');
}

// Sustituye el contenido del bloque por piezas (una por línea; con una sola se repite) y deja el bloque sin ancho tras lo insertado.
void SqlEditor::columnReplace (const QStringList& pieces)
{
    int l0, l1, c0, c1; columnBlock(l0, l1, c0, c1); if (pieces.isEmpty()) return;
    QTextCursor c(document()); c.beginEditBlock(); int maxLen = 0;
    for (int l = l0; l <= l1; ++l)
    {
        QTextBlock b = document()->findBlockByNumber(l); const QString piece = pieces.size() == 1 ? pieces[0] : pieces.value(l - l0);
        const int len = b.text().size(); QTextCursor x(b);
        if (len < c0) { x.setPosition(b.position() + len); x.insertText(QString(c0 - len, ' ') + piece); }
        else { x.setPosition(b.position() + c0); x.setPosition(b.position() + qMin(c1, len), QTextCursor::KeepAnchor); x.insertText(piece); }
        maxLen = qMax(maxLen, int(piece.size()));
    }
    c.endEditBlock();
    m_colAC = m_colC = c0 + maxLen; viewport()->update();
}

void SqlEditor::columnFill (const QString& text, bool fill)
{
    if (!m_colMode || text.isEmpty()) return;
    int l0, l1, c0, c1; columnBlock(l0, l1, c0, c1);
    if (fill && c1 > c0) { const int w = c1 - c0; QString rep; while (rep.size() < w) rep += text; columnReplace({rep.left(w)}); m_colAC = c0; m_colC = c0 + w; viewport()->update(); }   // rellena el ancho del bloque
    else { const int saveC = m_colC, saveA = m_colAC; m_colAC = m_colC = c0; columnReplace({text}); (void)saveC; (void)saveA; }                                                          // inserta en la columna
}

void SqlEditor::columnJustify (int how)
{
    if (!m_colMode) return;
    int l0, l1, c0, c1; columnBlock(l0, l1, c0, c1); const int w = c1 - c0; if (w <= 0) return;
    QTextCursor c(document()); c.beginEditBlock();
    for (int l = l0; l <= l1; ++l)
    {
        QTextBlock b = document()->findBlockByNumber(l); QString t = b.text(); if (t.size() < c1) t += QString(c1 - t.size(), ' ');
        const QString seg = t.mid(c0, w).trimmed(); QString r;
        if (how == 0) r = seg.leftJustified(w); else if (how == 2) r = seg.rightJustified(w);
        else { const int left = qMax(0, (w - int(seg.size())) / 2); r = (QString(left, ' ') + seg).leftJustified(w); }
        r = r.left(w); QTextCursor x(b); x.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor); x.insertText(t.left(c0) + r + t.mid(c1));
    }
    c.endEditBlock(); viewport()->update();
}

void SqlEditor::copy () { if (m_colMode) QApplication::clipboard()->setText(columnText()); else QPlainTextEdit::copy(); }
void SqlEditor::cut ()  { if (m_colMode) { QApplication::clipboard()->setText(columnText()); columnReplace({QString()}); } else QPlainTextEdit::cut(); }
void SqlEditor::paste ()
{
    if (!m_colMode) { QPlainTextEdit::paste(); return; }
    QString t = QApplication::clipboard()->text(); t.replace("\r\n", "\n"); if (t.endsWith('\n')) t.chop(1);
    columnReplace(t.split('\n'));
}

void SqlEditor::mousePressEvent (QMouseEvent* e) { if (m_colMode) exitColumnMode(false); QPlainTextEdit::mousePressEvent(e); }
