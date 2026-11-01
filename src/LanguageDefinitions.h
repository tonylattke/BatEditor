#pragma once

#include <QString>
#include <QStringList>

// Descripción declarativa de un lenguaje para el resaltador de sintaxis.
struct LanguageDefinition {
    QString name;                 // Nombre visible ("C++", "Python"...)
    QStringList extensions;       // Extensiones sin punto ("cpp", "h"...)
    QStringList fileNames;        // Nombres exactos ("CMakeLists.txt", "Makefile")
    QStringList keywords;
    QStringList types;
    QStringList constants;
    QString lineComment;          // "//", "#", "--"...
    QString blockCommentStart;    // "/*"
    QString blockCommentEnd;      // "*/"
    QString stringDelimiters = QStringLiteral("\"'");
    QString tripleQuote;          // "\"\"\"" en Python
    bool preprocessor = false;    // Líneas que empiezan por '#'
    bool caseInsensitiveKeywords = false;
    bool highlightFunctions = true;
};

namespace Languages {

const LanguageDefinition &plainText();
const LanguageDefinition &forFile(const QString &filePath);
const QList<LanguageDefinition> &all();

} // namespace Languages
