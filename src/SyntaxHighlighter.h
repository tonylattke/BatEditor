#pragma once

#include "LanguageDefinitions.h"

#include <QSet>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

// Resaltador basado en un pequeño analizador léxico por línea.
// Maneja comentarios de bloque y cadenas triples que cruzan varias líneas.
class SyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit SyntaxHighlighter(QTextDocument *document);

    void setLanguage(const LanguageDefinition &language);
    const LanguageDefinition &language() const { return m_lang; }

protected:
    void highlightBlock(const QString &text) override;

private:
    // InTripleString + k: dentro de la cadena multilínea m_lang.tripleQuotes[k]
    enum State { Normal = 0, InBlockComment = 1, InTripleString = 2 };

    QString normalize(const QString &word) const;
    int variableLength(const QString &text, int i) const;   // $nombre, ${...}, $1; 0 si no hay
    void highlightVariables(const QString &text, int from, int to);
    bool lineCommentAt(const QString &text, int i, int firstNonSpace) const;

    LanguageDefinition m_lang;
    QSet<QString> m_keywords, m_types, m_constants;

    QTextCharFormat m_keywordFmt, m_typeFmt, m_constantFmt, m_stringFmt, m_numberFmt,
        m_commentFmt, m_functionFmt, m_preprocFmt, m_headingFmt, m_keyFmt, m_variableFmt;
};
