#pragma once

#include "FindInFiles.h"

#include <QDateTime>
#include <QWidget>

class CodeEditor;
class FindBar;
class QSyntaxHighlighter;
class SyntaxHighlighter;

// Una pestaña del área central: un archivo (o una vista de diff) + su barra de búsqueda.
class EditorTab : public QWidget
{
    Q_OBJECT
public:
    enum class Kind { File, Diff, FindResults };

    explicit EditorTab(Kind kind = Kind::File, QWidget *parent = nullptr);

    CodeEditor *editor() const { return m_editor; }
    FindBar *findBar() const { return m_findBar; }
    Kind kind() const { return m_kind; }

    // Archivos
    bool load(const QString &path, QString *error);
    bool save(QString *error);
    bool saveAs(const QString &path, QString *error);
    QString filePath() const { return m_path; }
    void setFilePath(const QString &path);   // el archivo se renombró/movió fuera del editor
    bool isUntitled() const { return m_path.isEmpty(); }
    bool isModified() const;

    // Cambios hechos fuera del editor
    enum class DiskState { Unchanged, Changed, Missing };
    DiskState diskState() const;            // respecto a lo último que se leyó o guardó
    bool reloadFromDisk(QString *error);    // conserva cursor, scroll y deshacer
    QString displayName() const;
    QString languageName() const;
    // Lenguaje elegido a mano (barra de estado); vacío = según el nombre del archivo
    void setLanguageOverride(const QString &name);
    QString languageOverride() const { return m_languageOverride; }
    void setUntitledIndex(int index) { m_untitledIndex = index; }

    // Diff
    void setDiff(const QString &title, const QString &diffText);

    // Resultados de "Buscar en archivos"
    void setFindResults(const FindInFiles::Document &document);
    QList<FindInFiles::Location> matchLocations() const;   // en orden, para F4 / Shift+F4
    int matchIndexAtCursor() const;                        // coincidencia en la línea actual o -1
    void revealMatch(int index);                           // mueve el cursor a esa coincidencia

Q_SIGNALS:
    void titleChanged();
    void locationActivated(const FindInFiles::Location &location);  // doble clic / Enter en un resultado

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void applyLanguage(const QString &firstLine);   // la primera línea sirve para el shebang
    void applyLanguage();
    static bool readText(const QString &path, QString *text, QByteArray *bytes, QString *error);
    void rememberDisk(const QByteArray &bytes);

    Kind m_kind;
    CodeEditor *m_editor;
    FindBar *m_findBar;
    QSyntaxHighlighter *m_highlighter = nullptr;
    QString m_path;
    QString m_diffTitle;
    QString m_languageOverride;
    FindInFiles::Document m_results;
    bool m_crlf = false;
    int m_untitledIndex = 1;
    // Huella de la versión en disco: tamaño y fecha para descartar rápido, hash para confirmar
    QByteArray m_diskHash;
    qint64 m_diskSize = -1;
    QDateTime m_diskModified;
};
