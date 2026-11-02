#pragma once

#include <QList>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QTextCursor>

#include <functional>

class LineNumberArea;
class Minimap;

// Editor de código con:
//  - números de línea y resaltado de la línea actual
//  - cursores múltiples (Ctrl+clic, Ctrl+D, Ctrl+Shift+L, Ctrl+Alt+↑/↓)
//  - autoindentación, sangría por bloques y comentar/descomentar
//  - búsqueda y reemplazo (texto o expresión regular)
class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit CodeEditor(QWidget *parent = nullptr);

    // --- Configuración ---
    void setIndentWidth(int spaces);
    int indentWidth() const { return m_indentWidth; }
    void setLineCommentToken(const QString &token) { m_lineComment = token; }
    void setShowLineNumbers(bool show);
    void setShowMinimap(bool show);                 // miniatura del archivo a la derecha
    bool showMinimap() const { return m_showMinimap; }
    Minimap *minimap() const { return m_minimap; }
    void setPersistentHighlights(const QList<QTextCursor> &ranges);  // resaltados fijos (p. ej. resultados)

    // --- Cursores múltiples ---
    int cursorCount() const { return m_extraCursors.size() + 1; }
    QList<QTextCursor> allCursors() const;          // ordenados por posición
    void clearExtraCursors();
    void addNextOccurrence();                       // Ctrl+D
    void selectAllOccurrencesOfSelection();         // Alt+F3
    void splitSelectionIntoLines();                 // Ctrl+Shift+L
    void addCursorAbove();                          // Ctrl+Alt+↑
    void addCursorBelow();                          // Ctrl+Alt+↓

    // --- Edición ---
    void copyAll();
    void cutAll();
    void pasteAll();
    void toggleLineComment();                       // Ctrl+/
    void duplicateLines();                          // Ctrl+Shift+D
    void indentSelection(bool unindent);
    void goToLine(int line);

    // --- Búsqueda ---
    struct SearchOptions {
        bool regex = false;
        bool caseSensitive = false;
        bool wholeWord = false;
    };
    void setSearch(const QString &pattern, const SearchOptions &options);
    void clearSearch();
    int searchMatchCount() const { return m_searchMatches.size(); }
    int currentSearchIndex() const;
    bool searchIsValid() const { return m_searchRegex.isValid(); }
    bool findNext(bool backward = false);
    bool replaceCurrent(const QString &replacement);
    int replaceAll(const QString &replacement);
    bool selectAllMatches();

    // --- Usado por LineNumberArea ---
    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent *event);

Q_SIGNALS:
    void cursorsChanged();

protected:
    bool event(QEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void changeEvent(QEvent *e) override;

private:
    using CursorOp = std::function<void(QTextCursor &)>;

    bool isEditorCommand(QKeyEvent *e) const;
    bool handleEditorCommand(QKeyEvent *e);
    bool handleMultiCursorKey(QKeyEvent *e);

    void applyToAllCursors(const CursorOp &op);
    void setCursors(const QTextCursor &main, const QList<QTextCursor> &extras);
    void normalizeCursors();
    QList<int> selectedBlockNumbers() const;

    void insertNewline(QTextCursor &c) const;
    void smartBackspace(QTextCursor &c, bool word) const;
    QString indentUnit() const { return QString(m_indentWidth, QLatin1Char(' ')); }

    void updateLineNumberAreaWidth();
    void updateLineNumberArea(const QRect &rect, int dy);
    void updateExtraSelections();
    void recomputeSearchMatches();

    LineNumberArea *m_lineNumberArea = nullptr;
    Minimap *m_minimap = nullptr;
    bool m_showMinimap = false;
    void updateMinimapGeometry();
    QList<QTextCursor> m_extraCursors;
    int m_indentWidth = 4;
    bool m_showLineNumbers = true;
    QString m_lineComment = QStringLiteral("//");

    QRegularExpression m_searchRegex;
    bool m_searchActive = false;
    bool m_searchCaseSensitive = false;
    QList<QTextCursor> m_searchMatches;
    QList<QTextCursor> m_persistentHighlights;
};
