#pragma once

#include <QHash>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QSyntaxHighlighter>

#include <atomic>
#include <functional>

// Búsqueda (y reemplazo) en varios archivos, al estilo "Find in Files".
namespace FindInFiles {

// Marcadores especiales del campo "Donde" (como en otros editores, van en inglés)
inline const QString kOpenFolders = QStringLiteral("<open folders>");
inline const QString kOpenFiles = QStringLiteral("<open files>");

struct Options {
    QString pattern;
    QString replacement;
    bool regex = false;
    bool caseSensitive = false;
    bool wholeWord = false;
    bool showContext = true;        // 2 líneas antes y después de cada coincidencia
    bool respectGitignore = true;   // omite lo ignorado por .gitignore (y siempre .git/)
    bool preserveCase = false;      // al reemplazar: "Foo"→"Bar", "FOO"→"BAR", "foo"→"bar"
    QString where;                  // entradas separadas por comas (carpetas, archivos, *.cpp, -*.log, <open folders>…)
};

// Lo que hay abierto en el editor en el momento de buscar
struct Workspace {
    QStringList openFolders;
    QStringList openFiles;
    QHash<QString, QString> buffers;  // ruta absoluta → texto actual (incluye cambios sin guardar)
};

struct Line {
    int number = 0;                      // 1-based
    QString text;
    QList<QPair<int, int>> matches;      // (columna, longitud); vacío = línea de contexto
};

struct FileResult {
    QString path;
    int matchCount = 0;
    QList<Line> lines;                   // coincidencias y contexto, en orden
};

struct Results {
    Options options;
    int filesSearched = 0;
    int totalMatches = 0;
    bool truncated = false;              // se alcanzó el límite de coincidencias
    bool cancelled = false;
    QString error;                       // p. ej. expresión regular inválida
    QList<FileResult> files;
};

QRegularExpression buildRegex(const Options &options, QString *error = nullptr);

// Resuelve el campo "Donde" en la lista de archivos a revisar.
QStringList collectFiles(const Options &options, const Workspace &workspace, QString *error,
                         const std::atomic_bool *cancel = nullptr);

// Busca (en un hilo de trabajo). `progress` recibe el número de archivos revisados.
Results search(const Options &options, const Workspace &workspace, const std::atomic_bool &cancel,
               const std::function<void(int)> &progress = {});

// Texto de reemplazo para una coincidencia (\1..\9 en modo regex y conservación de mayúsculas).
QString replacementFor(const QRegularExpressionMatch &match, const Options &options);

// Aplica el reemplazo a un texto completo; devuelve cuántas sustituciones hizo.
int replaceInText(QString &text, const QRegularExpression &regex, const Options &options);

// --- Documento de resultados -------------------------------------------------
struct Location {
    QString path;
    int line = 0;       // 1-based
    int column = 0;     // 0-based
    int length = 0;
};

struct Document {
    QString text;
    QHash<int, Location> locations;              // nº de bloque → destino al hacer doble clic
    QList<int> matchBlocks;                      // bloques con coincidencias, en orden (para F4)
    QList<std::tuple<int, int, int>> highlights; // (bloque, columna, longitud) de cada coincidencia
};

Document buildDocument(const Results &results, const QString &title);

// Colores del documento de resultados: rutas, números de línea y separadores.
class ResultsHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    using QSyntaxHighlighter::QSyntaxHighlighter;

protected:
    void highlightBlock(const QString &text) override;
};

} // namespace FindInFiles
