#pragma once
#include <QSet>
#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QTextCharFormat>
#include "SyntaxColors.h"

class SqlHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit SqlHighlighter (QTextDocument* doc);
protected:
    void highlightBlock (const QString& text) override;
public:
    static void setUserObjects (const QSet<QString>& namesUpper);   // objetos del esquema conectado (MAYÚSCULAS); vacío = sin conexión
    void setExtraObjects (const QSet<QString>& namesUpper);         // solo para este resaltador (vista previa de Settings)
    void refreshColors ();                    // recalcula los colores (al cambiar el tema o los ajustes)
    void setStyles (const QVector<SynStyle>& v);   // estilos explícitos (vista previa de Settings)
protected:
private:
    QRegularExpression m_keywords, m_number, m_string, m_lineComment, m_blockStart, m_blockEnd, m_func, m_op, m_bind;
    QTextCharFormat m_userObj; QSet<QString> m_extra; bool m_userObjOn = true;
    QTextCharFormat m_kw, m_num, m_str, m_com, m_fn, m_opf, m_bindf, m_hint;
};
