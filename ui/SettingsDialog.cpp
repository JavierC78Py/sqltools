#include "SettingsDialog.h"
#include "ExportDialog.h"
#include "DdlPreferencesDialog.h"
#include "SqlEditor.h"
#include "SqlFormatter.h"
#include "SqlHighlighter.h"
#include "Theme.h"
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QMessageBox>
#include <QSplitter>
#include <QFile>
#include <QDir>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

static const char* kSample =
    "select e.id,e.nombre,d.nombre depto,(select count(*) from hijos h where h.id_emp=e.id) hijos from empleados e left join deptos d on d.id=e.id_depto where e.sueldo>1000 and (e.activo=1 or e.tipo in ('A','B')) order by e.nombre;\n"
    "create or replace procedure p_demo(p_id number) is\nv_estado cab.estado%type;\nbegin\nif p_id is null then\nraise_application_error(-20999,'Falta el id');\nend if;\nbegin\nselect estado into v_estado from cab where id_pedido=p_id and activo=1;\nexception\nwhen no_data_found then\nnull;\nend;\nend;\n/";

static QWidget* scrolled (QWidget* inner) { auto* sa = new QScrollArea(); sa->setWidgetResizable(true); sa->setFrameShape(QFrame::NoFrame); sa->setWidget(inner); return sa; }

SettingsDialog::SettingsDialog (QWidget* parent, int tab) : QDialog(parent)
{
    setWindowTitle("Settings"); resize(1180, 820);
    const FormatOptions fo = FormatOptions::load(); QSettings st;
    m_tabs = new QTabWidget();
    // ---- Format SQL: perfiles + estructura personalizada + vista previa con TU código ----
    auto* fmt = new QWidget(); auto* fh = new QVBoxLayout(fmt);
    auto* top = new QHBoxLayout(); m_profile = new QComboBox(); m_profile->setMinimumWidth(180);
    auto* bSave = new QPushButton("Guardar como…"); auto* bDel = new QPushButton("Eliminar"); auto* bImp = new QPushButton("Importar…"); auto* bExp = new QPushButton("Exportar…"); auto* bInfer = new QPushButton("Deducir de un ejemplo…");
    top->addWidget(new QLabel("Perfil")); top->addWidget(m_profile, 1); top->addWidget(bSave); top->addWidget(bDel); top->addWidget(bImp); top->addWidget(bExp); top->addWidget(bInfer); fh->addLayout(top);
    auto* hs = new QSplitter(Qt::Horizontal);
    auto* ctl = new QWidget(); auto* form = new QFormLayout(ctl); form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow); ctl->setMinimumWidth(500);
    auto auto_ = [](int lo, int hi) { auto* sp = new QSpinBox(); sp->setRange(lo, hi); return sp; };
    m_indent = auto_(1, 8); m_tabs2 = new QCheckBox("Sangrar con tabuladores"); m_case = new QComboBox(); m_case->addItems({"MAYÚSCULAS", "minúsculas", "Como está escrito"});
    m_fnCase = new QComboBox(); m_fnCase->addItems({"Igual que las palabras reservadas", "MAYÚSCULAS", "minúsculas", "Como está escrito"}); m_idCase = new QComboBox(); m_idCase->addItems({"MAYÚSCULAS", "minúsculas", "Como está escrito"});
    m_style = new QComboBox(); m_style->addItems({"Riachuelo (palabras alineadas a la derecha)", "Estándar (a la izquierda, contenido sangrado)"});
    m_contInd = auto_(-1, 20); m_contInd->setSpecialValueText("automático"); m_andOrInd = auto_(-1, 20); m_andOrInd->setSpecialValueText("automático"); m_joinInd = auto_(-1, 20); m_joinInd->setSpecialValueText("automático");
    m_kwAlone = new QCheckBox("SELECT solo en su línea si hay varias columnas"); m_comma = new QCheckBox("Coma al principio de la línea"); m_andOr = new QCheckBox("AND / OR en línea propia"); m_oneItem = new QCheckBox("Una columna por línea");
    m_onNew = new QCheckBox("La condición ON del JOIN en su propia línea"); m_spComma = new QCheckBox("Espacio después de la coma"); m_spParens = new QCheckBox("Espacios dentro de los paréntesis ( a, b )");
    m_alignDecl = new QCheckBox("PL/SQL: alinear los tipos de las declaraciones"); m_alignAsg = new QCheckBox("PL/SQL: alinear los := de asignaciones seguidas"); m_blankExc = new QCheckBox("PL/SQL: línea en blanco antes de EXCEPTION"); m_blankBegin = new QCheckBox("PL/SQL: línea en blanco antes de BEGIN");
    m_maxLine = auto_(40, 300);
    form->addRow(new QLabel("<b>Sangría y mayúsculas</b>")); form->addRow("Espacios por nivel", m_indent); form->addRow(m_tabs2); form->addRow("Palabras reservadas", m_case); form->addRow("Funciones", m_fnCase); form->addRow("Nombres de tablas y columnas", m_idCase);
    form->addRow(new QLabel("<b>Estructura de las consultas</b>")); form->addRow("Estilo base", m_style); form->addRow("Columnas y condiciones (sangría)", m_contInd); form->addRow("AND / OR (sangría desde WHERE)", m_andOrInd); form->addRow("JOIN (sangría desde FROM)", m_joinInd);
    form->addRow(m_kwAlone); form->addRow(m_oneItem); form->addRow(m_andOr); form->addRow(m_onNew); form->addRow(m_comma); form->addRow(m_spComma); form->addRow(m_spParens); form->addRow("Partir paréntesis de más de (caracteres)", m_maxLine);
    form->addRow(new QLabel("<b>PL/SQL</b>")); form->addRow(m_alignDecl); form->addRow(m_alignAsg); form->addRow(m_blankExc); form->addRow(m_blankBegin);
    hs->addWidget(scrolled(ctl));
    auto* vs = new QSplitter(Qt::Vertical); auto* srcBox = new QWidget(); auto* sl = new QVBoxLayout(srcBox); sl->setContentsMargins(0, 0, 0, 0); sl->addWidget(new QLabel("Tu código (puedes pegar el tuyo y editarlo):"));
    m_source = new QPlainTextEdit(); m_source->setPlainText(QString::fromUtf8(kSample)); m_source->setLineWrapMode(QPlainTextEdit::NoWrap); sl->addWidget(m_source);
    auto* resBox = new QWidget(); auto* rl = new QVBoxLayout(resBox); rl->setContentsMargins(0, 0, 0, 0); rl->addWidget(new QLabel("Resultado con estas opciones:"));
    m_preview = new QPlainTextEdit(); m_preview->setReadOnly(true); m_preview->setLineWrapMode(QPlainTextEdit::NoWrap); rl->addWidget(m_preview);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont); mono.setPointSize(12); m_source->setFont(mono); m_preview->setFont(mono);
    vs->addWidget(srcBox); vs->addWidget(resBox); hs->addWidget(vs); hs->setSizes({520, 480}); fh->addWidget(hs, 1);
    m_tabs->addTab(fmt, "Format SQL");
    applyFormat(fo); refreshProfiles();
    connect(m_profile, &QComboBox::activated, this, [this](int){ FormatOptions o; if (FormatOptions::loadProfile(m_profile->currentText(), o)) applyFormat(o); });
    connect(bSave, &QPushButton::clicked, this, [this]{ bool ok = false; const QString n = QInputDialog::getText(this, "Guardar perfil", "Nombre del perfil (si ya existe, se reemplaza):", QLineEdit::Normal, m_profile->currentText(), &ok).trimmed(); if (ok && !n.isEmpty()) { FormatOptions::saveProfile(n, collectFormat()); refreshProfiles(n); } });
    connect(bDel, &QPushButton::clicked, this, [this]{ const QString n = m_profile->currentText(); if (!n.isEmpty() && QMessageBox::question(this, "Eliminar perfil", "¿Eliminar el perfil «" + n + "»?") == QMessageBox::Yes) { FormatOptions::deleteProfile(n); refreshProfiles(); } });
    connect(bExp, &QPushButton::clicked, this, [this]{ const QString f = QFileDialog::getSaveFileName(this, "Exportar perfil", QDir::homePath() + "/mi_formato.sqlfmt", "Perfil de formato (*.sqlfmt)"); if (f.isEmpty()) return; QFile x(f); if (x.open(QIODevice::WriteOnly)) x.write(collectFormat().toJson().toUtf8()); });
    connect(bImp, &QPushButton::clicked, this, [this]{ const QString f = QFileDialog::getOpenFileName(this, "Importar perfil", QDir::homePath(), "Perfil de formato (*.sqlfmt *.json)"); if (f.isEmpty()) return; QFile x(f); if (!x.open(QIODevice::ReadOnly)) return; bool ok = false; const FormatOptions o = FormatOptions::fromJson(QString::fromUtf8(x.readAll()), &ok);
        if (!ok) { QMessageBox::warning(this, "Importar", "El archivo no es un perfil de formato de SQLTools Mac."); return; } applyFormat(o); const QString n = QFileInfo(f).completeBaseName(); FormatOptions::saveProfile(n, o); refreshProfiles(n); });
    connect(bInfer, &QPushButton::clicked, this, [this]{
        QDialog d(this); d.setWindowTitle("Deducir el formato de un ejemplo"); auto* l = new QVBoxLayout(&d); l->addWidget(new QLabel("Pegue código que YA tenga el formato que le gusta (una consulta y/o un bloque PL/SQL).\\nEl programa deduce la sangría, las mayúsculas y la estructura, y rellena las opciones."));
        auto* t = new QPlainTextEdit(); t->setPlainText(m_source->toPlainText()); QFont f = m_source->font(); t->setFont(f); l->addWidget(t, 1); auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); l->addWidget(bb); connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept); connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject); d.resize(640, 520);
        if (d.exec() != QDialog::Accepted) return; QStringList found; const FormatOptions o = FormatOptions::inferFromExample(t->toPlainText(), &found); applyFormat(o); m_source->setPlainText(t->toPlainText());
        QMessageBox::information(this, "Opciones deducidas", found.isEmpty() ? "No se pudo deducir nada; el ejemplo es muy corto." : "Se dedujo:\\n• " + found.join("\\n• ") + "\\n\\nAjuste lo que haga falta y guárdelo como perfil."); });
    connect(m_source, &QPlainTextEdit::textChanged, this, &SettingsDialog::updatePreview);
    // ---- Editor ----
    auto* ed = new QWidget(); auto* ef = new QFormLayout(ed);
    m_fontFamily = new QFontComboBox(); m_fontFamily->setFontFilters(QFontComboBox::MonospacedFonts); m_fontFamily->setCurrentFont(QFont(st.value("editor/fontFamily", "Menlo").toString()));
    m_fontSize = new QSpinBox(); m_fontSize->setRange(8, 32); m_fontSize->setValue(st.value("editor/fontSize", 13).toInt());
    m_tabWidth = new QSpinBox(); m_tabWidth->setRange(1, 12); m_tabWidth->setValue(st.value("editor/tabWidth", 4).toInt());
    ef->addRow("Letra del editor", m_fontFamily); ef->addRow("Tamaño de la letra", m_fontSize); ef->addRow("Ancho del tabulador (espacios)", m_tabWidth);
    m_wrap = new QCheckBox("Ajustar las líneas largas al ancho de la ventana (si no, la línea sigue a la derecha hasta el Enter)"); m_wrap->setChecked(st.value("editor/wordWrap", false).toBool()); ef->addRow(m_wrap);
    m_hlObjects = new QCheckBox("Resaltar los nombres de objetos del esquema conectado (tablas, vistas, paquetes…)"); m_hlObjects->setChecked(st.value("editor/highlightObjects", true).toBool()); ef->addRow(m_hlObjects);
    ef->addRow(new QLabel("<b>Object Viewer</b> (al arrastrar o copiar un elemento al editor)"));
    m_viewDbl = new QCheckBox("El doble clic inserta el elemento en el editor (como en Windows)"); m_viewDbl->setChecked(st.value("viewer/dblInsert", true).toBool()); ef->addRow(m_viewDbl);
    m_viewPkg = new QCheckBox("Procedimientos de paquete: anteponer el nombre del paquete"); m_viewPkg->setChecked(st.value("viewer/copyWithPackage", false).toBool()); ef->addRow(m_viewPkg);
    m_viewMax = new QSpinBox(); m_viewMax->setRange(20, 400); m_viewMax->setValue(st.value("viewer/maxLine", 60).toInt()); ef->addRow("Largo máximo de línea (varios elementos)", m_viewMax);
    ef->addRow(new QLabel("Los colores de la sintaxis están en la pestaña «Colores». El tema claro/oscuro está en View > Appearance."));
    m_tabs->addTab(ed, "Editor");
    // ---- Colores ----
    buildColors(m_tabs);
    // ---- Ejecución e historial ----
    auto* ex = new QWidget(); auto* xl = new QVBoxLayout(ex);
    auto* g1 = new QGroupBox("Ejecución"); auto* f1 = new QFormLayout(g1);
    m_execStyle = new QComboBox(); m_execStyle->addItems({"Sentencia delimitada por ; y /  (Alt+F10 usa las líneas en blanco)", "Bloque entre líneas en blanco  (Alt+F10 usa ; y /)"}); m_execStyle->setCurrentIndex(st.value("exec/style", 0).toInt());
    m_autocommit = new QCheckBox("Commit automático tras cada sentencia que modifica datos"); m_autocommit->setChecked(st.value("exec/autocommit", false).toBool());
    m_commitDisc = new QComboBox(); m_commitDisc->addItems({"Deshacer (rollback)", "Confirmar (commit)", "Preguntar"}); m_commitDisc->setCurrentIndex(st.value("exec/commitOnDisconnect", 0).toInt());
    m_quickLower = new QCheckBox("Quick Query / Quick Count escritos en minúsculas"); m_quickLower->setChecked(st.value("quick/lower", true).toBool());
    f1->addRow("Execute Current (F10) ejecuta", m_execStyle); f1->addRow(m_autocommit); f1->addRow("Al desconectar con cambios sin confirmar", m_commitDisc); f1->addRow(m_quickLower);
    xl->addWidget(g1);
    auto* g2 = new QGroupBox("Historial"); auto* f2 = new QFormLayout(g2);
    m_histOn = new QCheckBox("Guardar las sentencias ejecutadas"); m_histOn->setChecked(st.value("history/enabled", true).toBool());
    m_histMax = new QSpinBox(); m_histMax->setRange(10, 100000); m_histMax->setValue(st.value("history/maxCount", 2000).toInt());
    m_histValid = new QCheckBox("Solo las que terminaron sin error"); m_histValid->setChecked(st.value("history/validOnly", false).toBool());
    m_histDistinct = new QCheckBox("Sin repetidos (la sentencia repetida sube al final)"); m_histDistinct->setChecked(st.value("history/distinct", false).toBool());
    m_histPersist = new QCheckBox("Conservar el historial al cerrar el programa"); m_histPersist->setChecked(st.value("history/persistent", true).toBool());
    f2->addRow(m_histOn); f2->addRow("Máximo de sentencias", m_histMax); f2->addRow(m_histValid); f2->addRow(m_histDistinct); f2->addRow(m_histPersist);
    xl->addWidget(g2); xl->addStretch(1);
    m_tabs->addTab(ex, "Ejecución e historial");
    // ---- Autocompletado ----
    auto* ac = new QWidget(); auto* al = new QVBoxLayout(ac); auto* af = new QFormLayout();
    m_auto = new QComboBox(); m_auto->addItems({"Solo con el atajo (Ctrl+Espacio)", "Automático después del punto (alias.)", "Siempre automático"}); m_auto->setCurrentIndex(SqlEditor::autoMode);
    af->addRow("Cuándo se abre la lista", m_auto); al->addLayout(af);
    auto* g3 = new QGroupBox("Objetos que se ofrecen"); auto* l3 = new QVBoxLayout(g3);
    for (const char* t : {"table:Tablas", "view:Vistas", "synonym:Sinónimos", "package:Paquetes", "procedure:Procedimientos", "function:Funciones", "sequence:Secuencias", "type:Tipos"})
    { const QString k = QString(t).section(':', 0, 0), n = QString(t).section(':', 1); auto* c = new QCheckBox(n); c->setProperty("key", k); c->setChecked(st.value("ac/" + k, true).toBool()); m_acTypes << c; l3->addWidget(c); }
    al->addWidget(g3); al->addWidget(new QLabel("Los cambios de los filtros se aplican al refrescar la lista de objetos (Session > Refresh Object Name Cache).")); al->addStretch(1);
    m_tabs->addTab(ac, "Autocompletado");
    // ---- Exportación ----
    auto* xp = new QWidget(); auto* xv = new QVBoxLayout(xp); auto* g4 = new QGroupBox("Formato por destino"); auto* f4 = new QFormLayout(g4);
    const char* dn[] = {"Archivo (Grid > Export)", "Portapapeles (Copy All)", "Editor (Open in Editor)"};
    for (int i = 0; i < 3; ++i)
    {
        const ExportOptions eo = ExportOptions::load(ExportOptions::Destination(i)); m_expFmt[i] = new QComboBox(); for (int f = 0; f < ExportOptions::FormatCount; ++f) m_expFmt[i]->addItem(ExportOptions::formatName(f));
        m_expFmt[i]->setCurrentIndex(eo.format); m_expHdr[i] = new QCheckBox("con nombres de columna"); m_expHdr[i]->setChecked(eo.header);
        auto* row = new QHBoxLayout(); row->addWidget(m_expFmt[i], 1); row->addWidget(m_expHdr[i]); f4->addRow(dn[i], row);
    }
    xv->addWidget(g4); m_expCommon = new ExportOptionsWidget(false); m_expCommon->setValue(ExportOptions::load(ExportOptions::File)); xv->addWidget(m_expCommon); xv->addStretch(1);
    m_tabs->addTab(scrolled(xp), "Exportación");

    // ---- DDL (valores predeterminados de «Load DDL»; el diálogo local parte de ellos) ----
    auto* dd = new QWidget(); auto* dl = new QVBoxLayout(dd); m_ddlW = new DdlOptionsWidget(); m_ddlW->setValue(DdlOptions::load()); dl->addWidget(m_ddlW);
    m_ddlShift = new QCheckBox("Mostrar el diálogo «Local DDL Preferences» solo con la tecla Mayús pulsada"); m_ddlShift->setChecked(DdlOptions::load().onlyWithShift); dl->addWidget(m_ddlShift);
    dl->addWidget(new QLabel("Al hacer Load DDL se muestra un diálogo con estas mismas opciones (para esa carga). Solo se lee el diccionario; no se modifica nada.")); dl->addStretch(1);
    m_tabs->addTab(dd, "DDL");
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); connect(bb, &QDialogButtonBox::accepted, this, &SettingsDialog::accept); connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* lay = new QVBoxLayout(this); lay->addWidget(m_tabs); lay->addWidget(bb);
    for (QSpinBox* s : {m_indent, m_maxLine, m_contInd, m_andOrInd, m_joinInd}) connect(s, &QSpinBox::valueChanged, this, &SettingsDialog::updatePreview);
    for (QComboBox* c : {m_case, m_style, m_fnCase, m_idCase}) connect(c, &QComboBox::currentIndexChanged, this, &SettingsDialog::updatePreview);
    for (QCheckBox* c : {m_tabs2, m_comma, m_andOr, m_oneItem, m_kwAlone, m_onNew, m_spComma, m_spParens, m_alignDecl, m_alignAsg, m_blankExc, m_blankBegin}) connect(c, &QCheckBox::toggled, this, &SettingsDialog::updatePreview);
    m_tabs->setCurrentIndex(tab); updatePreview();
}

// ---------------- Colores ----------------
void SettingsDialog::buildColors (QTabWidget* tabs)
{
    auto* w = new QWidget(); auto* lay = new QVBoxLayout(w);
    m_light = SyntaxColors::load(false); m_dark = SyntaxColors::load(true); m_shownTheme = Theme::dark() ? 1 : 0;
    auto* top = new QHBoxLayout(); m_colorTheme = new QComboBox(); m_colorTheme->addItems({"Tema claro", "Tema oscuro"}); m_colorTheme->setCurrentIndex(m_shownTheme);
    auto* bDef = new QPushButton("Predeterminados"); auto* bClassic = new QPushButton("Aspecto SQLTools clásico");
    top->addWidget(new QLabel("Editar los colores del")); top->addWidget(m_colorTheme); top->addStretch(1); top->addWidget(bDef); top->addWidget(bClassic); lay->addLayout(top);
    m_colorTable = new QTableWidget(SynClassCount, 5); m_colorTable->setHorizontalHeaderLabels({"Elemento", "Color (clic para cambiar)", "Negrita", "Cursiva", "Subrayado"}); m_colorTable->verticalHeader()->setDefaultSectionSize(30); m_colorTable->setFixedHeight(30 * SynClassCount + 32); m_colorTable->verticalHeader()->hide(); m_colorTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_colorTable->horizontalHeader()->setStretchLastSection(true); m_colorTable->setColumnWidth(0, 290); m_colorTable->setColumnWidth(1, 150); m_colorTable->setColumnWidth(2, 70); m_colorTable->setColumnWidth(3, 70);
    m_colorTable->setSelectionMode(QAbstractItemView::NoSelection); lay->addWidget(m_colorTable);
    lay->addWidget(new QLabel("Vista previa:")); m_colorPreview = new QPlainTextEdit(); m_colorPreview->setReadOnly(true); m_colorPreview->setPlainText(SyntaxColors::sampleSql());
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont); mono.setPointSize(12); m_colorPreview->setFont(mono); m_colorHl = new SqlHighlighter(m_colorPreview->document()); m_colorHl->setExtraObjects({"EMPLEADOS", "PKG_UTIL"}); lay->addWidget(m_colorPreview, 1);
    tabs->addTab(w, "Colores"); loadColorTable();
    connect(m_colorTheme, &QComboBox::currentIndexChanged, this, [this](int i){ storeColorTable(); m_shownTheme = i; loadColorTable(); });
    connect(bDef, &QPushButton::clicked, this, [this]{ (m_shownTheme ? m_dark : m_light) = SyntaxColors::defaults(m_shownTheme == 1); loadColorTable(); });
    connect(bClassic, &QPushButton::clicked, this, [this]{ (m_shownTheme ? m_dark : m_light) = SyntaxColors::classic(m_shownTheme == 1); loadColorTable(); });
    connect(m_colorTable, &QTableWidget::cellClicked, this, [this](int row, int col){
        if (col != 1) return; auto& v = m_shownTheme ? m_dark : m_light; const QColor c = QColorDialog::getColor(v[row].color, this, "Color de " + SyntaxColors::className(row)); if (c.isValid()) { v[row].color = c; loadColorTable(); } });
}

void SettingsDialog::loadColorTable ()
{
    const auto& v = m_shownTheme ? m_dark : m_light; m_colorTable->blockSignals(true);
    for (int i = 0; i < SynClassCount; ++i)
    {
        m_colorTable->setItem(i, 0, new QTableWidgetItem(SyntaxColors::className(i)));
        auto* sw = new QTableWidgetItem(v[i].color.name().toUpper()); sw->setBackground(v[i].color); sw->setForeground(v[i].color.lightness() < 140 ? Qt::white : Qt::black); sw->setTextAlignment(Qt::AlignCenter); m_colorTable->setItem(i, 1, sw);
        for (int c = 0; c < 3; ++c) { auto* b = new QCheckBox(); b->setChecked(c == 0 ? v[i].bold : c == 1 ? v[i].italic : v[i].underline); auto* host = new QWidget(); auto* hl = new QHBoxLayout(host); hl->setContentsMargins(0, 0, 0, 0); hl->setAlignment(Qt::AlignCenter); hl->addWidget(b); m_colorTable->setCellWidget(i, 2 + c, host); connect(b, &QCheckBox::toggled, this, [this]{ storeColorTable(); refreshColorPreview(); }); }
    }
    m_colorTable->blockSignals(false); refreshColorPreview();
}

void SettingsDialog::storeColorTable ()
{
    auto& v = m_shownTheme ? m_dark : m_light;
    for (int i = 0; i < SynClassCount; ++i) for (int c = 0; c < 3; ++c) if (auto* host = m_colorTable->cellWidget(i, 2 + c)) if (auto* b = host->findChild<QCheckBox*>()) (c == 0 ? v[i].bold : c == 1 ? v[i].italic : v[i].underline) = b->isChecked();
}

void SettingsDialog::refreshColorPreview ()
{
    const bool dk = m_shownTheme == 1;
    m_colorPreview->setStyleSheet(dk ? "QPlainTextEdit { background: #1e1e1e; color: #d4d4d4; }" : "QPlainTextEdit { background: #ffffff; color: #202020; }");
    m_colorHl->setStyles(m_shownTheme ? m_dark : m_light);
}

FormatOptions SettingsDialog::collectFormat () const
{
    FormatOptions o; o.indent = m_indent->value(); o.useTabs = m_tabs2->isChecked(); o.keywordCase = m_case->currentIndex(); o.style = m_style->currentIndex() == 0 ? FormatOptions::River : FormatOptions::Standard;
    o.commaLeading = m_comma->isChecked(); o.andOrNewLine = m_andOr->isChecked(); o.oneItemPerLine = m_oneItem->isChecked(); o.maxLine = m_maxLine->value();
    o.contIndent = m_contInd->value(); o.andOrIndent = m_andOrInd->value(); o.joinIndent = m_joinInd->value(); o.kwAloneWhenMany = m_kwAlone->isChecked(); o.onNewLine = m_onNew->isChecked();
    o.spaceAfterComma = m_spComma->isChecked(); o.spaceInParens = m_spParens->isChecked(); o.functionCase = m_fnCase->currentIndex() - 1; o.identifierCase = m_idCase->currentIndex();
    o.alignDeclarations = m_alignDecl->isChecked(); o.alignAssignments = m_alignAsg->isChecked(); o.blankBeforeException = m_blankExc->isChecked(); o.blankAfterDeclare = m_blankBegin->isChecked();
    return o;
}

void SettingsDialog::applyFormat (const FormatOptions& o)
{
    m_loading = true;
    m_indent->setValue(o.indent); m_tabs2->setChecked(o.useTabs); m_case->setCurrentIndex(o.keywordCase); m_style->setCurrentIndex(o.style == FormatOptions::River ? 0 : 1); m_comma->setChecked(o.commaLeading); m_andOr->setChecked(o.andOrNewLine); m_oneItem->setChecked(o.oneItemPerLine); m_maxLine->setValue(o.maxLine);
    m_contInd->setValue(o.contIndent); m_andOrInd->setValue(o.andOrIndent); m_joinInd->setValue(o.joinIndent); m_kwAlone->setChecked(o.kwAloneWhenMany); m_onNew->setChecked(o.onNewLine); m_spComma->setChecked(o.spaceAfterComma); m_spParens->setChecked(o.spaceInParens);
    m_fnCase->setCurrentIndex(o.functionCase + 1); m_idCase->setCurrentIndex(o.identifierCase); m_alignDecl->setChecked(o.alignDeclarations); m_alignAsg->setChecked(o.alignAssignments); m_blankExc->setChecked(o.blankBeforeException); m_blankBegin->setChecked(o.blankAfterDeclare);
    m_loading = false; updatePreview();
}

void SettingsDialog::refreshProfiles (const QString& select)
{
    m_profile->clear(); m_profile->addItems(FormatOptions::profileNames()); if (!select.isEmpty()) m_profile->setCurrentText(select); else m_profile->setCurrentIndex(-1);
    m_profile->setPlaceholderText("(sin perfil guardado)");
}

void SettingsDialog::updatePreview () { if (m_loading || !m_preview || !m_source) return; m_preview->setPlainText(SqlFormatter::format(m_source->toPlainText(), collectFormat())); }

void SettingsDialog::accept ()
{
    collectFormat().save();
    QSettings st; st.setValue("editor/fontSize", m_fontSize->value()); st.setValue("editor/tabWidth", m_tabWidth->value()); st.setValue("editor/wordWrap", m_wrap->isChecked()); st.setValue("viewer/dblInsert", m_viewDbl->isChecked()); st.setValue("viewer/copyWithPackage", m_viewPkg->isChecked()); st.setValue("viewer/maxLine", m_viewMax->value()); st.setValue("editor/highlightObjects", m_hlObjects->isChecked()); st.setValue("editor/fontFamily", m_fontFamily->currentFont().family());
    storeColorTable(); SyntaxColors::save(false, m_light); SyntaxColors::save(true, m_dark);
    st.setValue("exec/style", m_execStyle->currentIndex()); st.setValue("exec/autocommit", m_autocommit->isChecked()); st.setValue("exec/commitOnDisconnect", m_commitDisc->currentIndex()); st.setValue("quick/lower", m_quickLower->isChecked());
    st.setValue("history/enabled", m_histOn->isChecked()); st.setValue("history/maxCount", m_histMax->value()); st.setValue("history/validOnly", m_histValid->isChecked()); st.setValue("history/distinct", m_histDistinct->isChecked()); st.setValue("history/persistent", m_histPersist->isChecked());
    SqlEditor::autoMode = m_auto->currentIndex(); st.setValue("completion/autoMode", SqlEditor::autoMode);
    for (auto* c : m_acTypes) st.setValue("ac/" + c->property("key").toString(), c->isChecked());
    { DdlOptions d = m_ddlW->value(); d.onlyWithShift = m_ddlShift->isChecked(); d.save(); }
    ExportOptions common = m_expCommon->value();
    for (int i = 0; i < 3; ++i) { ExportOptions e = common; e.format = m_expFmt[i]->currentIndex(); e.header = m_expHdr[i]->isChecked(); e.jsonPretty = ExportOptions::load(ExportOptions::Destination(i)).jsonPretty; e.save(ExportOptions::Destination(i)); }
    QDialog::accept();
}
