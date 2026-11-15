#pragma once
// Colores que dependen del tema (claro/oscuro) de la aplicación. Se consultan en el momento de usarlos.
#include <QColor>
#include <QGuiApplication>
#include <QPalette>
#include <QString>

namespace Theme
{
    inline bool dark () { return QGuiApplication::palette().color(QPalette::Window).lightness() < 128; }
    inline QColor error ()  { return dark() ? QColor(0xff, 0x7b, 0x72) : QColor(0xb0, 0x20, 0x20); }
    inline QColor ok ()     { return dark() ? QColor(0x7e, 0xe0, 0x87) : QColor(0x2e, 0x7d, 0x32); }
    inline QColor muted ()  { return dark() ? QColor(0x9a, 0x9a, 0x9a) : QColor(0x66, 0x66, 0x66); }
    inline QColor nullCell(){ return dark() ? QColor(0x80, 0x80, 0x80) : QColor(150, 150, 150); }
    inline QString css (const QColor& c) { return "color: " + c.name() + ";"; }

    // Hoja de estilo moderna (bordes finos y redondeados, cabecera plana, scrollbars delgados) que sigue el tema claro/oscuro.
    // grid=true deja una cuadrícula tenue (para tablas de datos).
    inline QString modernCss (bool grid = false)
    {
        const QPalette pal = QGuiApplication::palette(); const bool dk = dark();
        const QString line = dk ? "#3d3d40" : "#d9dce1", hover = dk ? "#3a3d43" : "#eceff4", sel = pal.color(QPalette::Highlight).name(), selText = pal.color(QPalette::HighlightedText).name(),
                      base = pal.color(QPalette::Base).name(), alt = dk ? "#2a2b2e" : "#f7f8fa", head = dk ? "#2e2f33" : "#f3f4f6", text = pal.color(QPalette::Text).name(),
                      gl = grid ? (dk ? "#34353a" : "#e6e8ec") : QString("transparent");
        return QString(R"(
        QToolButton { border: 1px solid transparent; border-radius: 6px; padding: 2px 6px; }
        QToolButton:hover { background: %1; border-color: %2; }
        QToolButton:checked { background: %1; border-color: %3; }
        QToolButton:pressed { background: %2; }
        QComboBox { border: 1px solid %2; border-radius: 6px; padding: 2px 8px; background: %4; }
        QComboBox:hover { border-color: %3; }
        QComboBox::drop-down { border: none; width: 18px; }
        QLineEdit { border: 1px solid %2; border-radius: 5px; padding: 1px 6px; background: %4; color: %8; }
        QLineEdit:focus { border-color: %3; }
        QTableView, QTreeView { border: 1px solid %2; border-radius: 8px; background: %4; alternate-background-color: %5; gridline-color: %9; outline: 0; }
        QTableView::item { padding: 0 4px; border: none; }
        QTableView::item:selected, QTreeView::item:selected { background: %3; color: %7; }
        QTreeView::item { padding: 1px 0; }
        QHeaderView { background: transparent; }
        QHeaderView::section { background: %6; color: %8; border: none; border-right: 1px solid %2; border-bottom: 1px solid %2; padding: 3px 6px; font-weight: 600; }
        QScrollBar:vertical { width: 10px; background: transparent; margin: 2px; } QScrollBar:horizontal { height: 10px; background: transparent; margin: 2px; }
        QScrollBar::handle { background: %2; border-radius: 4px; min-height: 24px; min-width: 24px; } QScrollBar::handle:hover { background: %3; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; } QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
        )").arg(hover, line, sel, base, alt, head, selText, text, gl);
    }
}
