#pragma once

#include <QString>
#include <QStringList>

// Descripción declarativa de un lenguaje para el resaltador de sintaxis.
struct LanguageDefinition {
    QString name;                 // Nombre visible ("C++", "Python"...)
    QStringList extensions;       // Extensiones sin punto ("cpp", "h"...)
    QStringList fileNames;        // Nombres exactos ("CMakeLists.txt", "Makefile")
    QStringList interpreters;     // Programas del shebang ("python", "bash"...)
    QStringList keywords;
    QStringList types;
    QStringList constants;
    QString lineComment;          // "//", "#", "--"... (también el de Ctrl+/)
    QStringList extraLineComments;  // Otros comentarios de línea ("#" en PHP, "REM" en Batch)
    QString blockCommentStart;    // "/*"
    QString blockCommentEnd;      // "*/"
    QString stringDelimiters = QStringLiteral("\"'");
    QStringList tripleQuotes;     // Cadenas multilínea: "\"\"\"" y "'''" en Python
    QString variablePrefixes;     // "$" en Shell / PHP: resalta $nombre y ${...}
    bool preprocessor = false;    // Líneas que empiezan por '#'
    bool annotations = false;     // @Override, @decorador, @media
    bool keyValue = false;        // Claves antes de ':' o '=' (JSON, YAML, INI, CSS)
    bool sectionHeaders = false;  // Líneas "[sección]" (INI, TOML)
    bool caseInsensitiveKeywords = false;
    bool highlightFunctions = true;
};

namespace Languages {

const LanguageDefinition &plainText();
// Por nombre o extensión; si no hay coincidencia, por el shebang de la primera línea
const LanguageDefinition &forFile(const QString &filePath, const QString &firstLine = QString());
const LanguageDefinition &byName(const QString &name);   // plainText() si no existe
const QList<LanguageDefinition> &all();

} // namespace Languages
