#pragma once

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
    enum class Kind { File, Diff };

    explicit EditorTab(Kind kind = Kind::File, QWidget *parent = nullptr);

    CodeEditor *editor() const { return m_editor; }
    FindBar *findBar() const { return m_findBar; }
    Kind kind() const { return m_kind; }

    // Archivos
    bool load(const QString &path, QString *error);
    bool save(QString *error);
    bool saveAs(const QString &path, QString *error);
    QString filePath() const { return m_path; }
    bool isUntitled() const { return m_path.isEmpty(); }
    bool isModified() const;
    QString displayName() const;
    QString languageName() const;
    void setUntitledIndex(int index) { m_untitledIndex = index; }

    // Diff
    void setDiff(const QString &title, const QString &diffText);

Q_SIGNALS:
    void titleChanged();

private:
    void applyLanguage();

    Kind m_kind;
    CodeEditor *m_editor;
    FindBar *m_findBar;
    QSyntaxHighlighter *m_highlighter = nullptr;
    QString m_path;
    QString m_diffTitle;
    bool m_crlf = false;
    int m_untitledIndex = 1;
};
