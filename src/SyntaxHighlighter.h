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
    enum State { Normal = 0, InBlockComment = 1, InTripleString = 2 };

    bool isKeyword(const QString &word) const;
    QString normalize(const QString &word) const;

    LanguageDefinition m_lang;
    QSet<QString> m_keywords, m_types, m_constants;

    QTextCharFormat m_keywordFmt, m_typeFmt, m_constantFmt, m_stringFmt, m_numberFmt,
        m_commentFmt, m_functionFmt, m_preprocFmt, m_headingFmt;
};
