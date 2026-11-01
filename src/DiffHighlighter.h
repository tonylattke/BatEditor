#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>

// Colorea la salida de un diff unificado (líneas +, -, @@ y cabeceras).
class DiffHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit DiffHighlighter(QTextDocument *document);

protected:
    void highlightBlock(const QString &text) override;

private:
    QTextCharFormat m_added, m_removed, m_hunk, m_header;
};
