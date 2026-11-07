#include "CodeEditor.h"

#include "Minimap.h"
#include "Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>

#include <algorithm>
#include <functional>

// ---------------------------------------------------------------------------
// Margen de números de línea
// ---------------------------------------------------------------------------
class LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(CodeEditor *editor) : QWidget(editor), m_editor(editor) {}
    QSize sizeHint() const override { return {m_editor->lineNumberAreaWidth(), 0}; }

protected:
    void paintEvent(QPaintEvent *event) override { m_editor->lineNumberAreaPaintEvent(event); }

private:
    CodeEditor *m_editor;
};

namespace {

constexpr int kMaxSearchMatches = 10000;

QString leadingWhitespace(const QString &line)
{
    int i = 0;
    while (i < line.size() && (line.at(i) == QLatin1Char(' ') || line.at(i) == QLatin1Char('\t')))
        ++i;
    return line.left(i);
}

QString normalizedSelection(const QTextCursor &c)
{
    QString s = c.selectedText();
    s.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    s.replace(QChar::LineSeparator, QLatin1Char('\n'));
    return s;
}

bool cursorLess(const QTextCursor &a, const QTextCursor &b)
{
    return a.selectionStart() < b.selectionStart()
        || (a.selectionStart() == b.selectionStart() && a.selectionEnd() < b.selectionEnd());
}

} // namespace

// ---------------------------------------------------------------------------
// Construcción y configuración
// ---------------------------------------------------------------------------
CodeEditor::CodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
    , m_lineNumberArea(new LineNumberArea(this))
{
    setFont(Theme::editorFont());
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setCursorWidth(2);
    setFrameShape(QFrame::NoFrame);
    setTabChangesFocus(false);
    setCenterOnScroll(false);
    setIndentWidth(4);

    const Theme::Colors &c = Theme::colors();
    QPalette p = palette();
    p.setColor(QPalette::Base, c.background);
    p.setColor(QPalette::Text, c.foreground);
    p.setColor(QPalette::Highlight, c.selection);
    p.setColor(QPalette::HighlightedText, c.foreground);
    setPalette(p);

    connect(this, &QPlainTextEdit::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateLineNumberArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        updateExtraSelections();
        Q_EMIT cursorsChanged();
    });
    connect(this, &QPlainTextEdit::selectionChanged, this, &CodeEditor::updateExtraSelections);
    connect(document(), &QTextDocument::contentsChanged, this, [this] {
        if (m_searchActive)
            recomputeSearchMatches();
        viewport()->update();
    });

    m_minimap = new Minimap(this);
    m_minimap->hide();

    updateLineNumberAreaWidth();
    updateExtraSelections();
}

void CodeEditor::setIndentWidth(int spaces)
{
    m_indentWidth = qBound(1, spaces, 16);
    setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * m_indentWidth);
}

void CodeEditor::setPersistentHighlights(const QList<QTextCursor> &ranges)
{
    m_persistentHighlights = ranges;
    updateExtraSelections();
}

void CodeEditor::setShowMinimap(bool show)
{
    m_showMinimap = show;
    m_minimap->setVisible(show);
    updateLineNumberAreaWidth();
}

void CodeEditor::updateMinimapGeometry()
{
    if (!m_showMinimap)
        return;
    const QRect vg = viewport()->geometry();
    m_minimap->setGeometry(vg.right() + 1, vg.top(), Minimap::kWidth, vg.height());
}

void CodeEditor::setShowLineNumbers(bool show)
{
    m_showLineNumbers = show;
    m_lineNumberArea->setVisible(show);
    updateLineNumberAreaWidth();
}

// ---------------------------------------------------------------------------
// Números de línea
// ---------------------------------------------------------------------------
int CodeEditor::lineNumberAreaWidth() const
{
    if (!m_showLineNumbers)
        return 0;
    int digits = 1;
    int max = qMax(1, blockCount());
    while (max >= 10) {
        max /= 10;
        ++digits;
    }
    digits = qMax(digits, 3);
    return 20 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeEditor::updateLineNumberAreaWidth()
{
    setViewportMargins(lineNumberAreaWidth(), 0, m_showMinimap ? Minimap::kWidth : 0, 0);
    updateMinimapGeometry();
}

void CodeEditor::updateLineNumberArea(const QRect &rect, int dy)
{
    if (dy)
        m_lineNumberArea->scroll(0, dy);
    else
        m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
    if (rect.contains(viewport()->rect()))
        updateLineNumberAreaWidth();
}

void CodeEditor::resizeEvent(QResizeEvent *e)
{
    QPlainTextEdit::resizeEvent(e);
    const QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
    updateMinimapGeometry();
}

void CodeEditor::changeEvent(QEvent *e)
{
    QPlainTextEdit::changeEvent(e);
    if (e->type() == QEvent::FontChange) {
        setTabStopDistance(fontMetrics().horizontalAdvance(QLatin1Char(' ')) * m_indentWidth);
        updateLineNumberAreaWidth();
        m_lineNumberArea->update();
    }
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent *event)
{
    const Theme::Colors &colors = Theme::colors();
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), colors.background);
    painter.setFont(font());

    // Líneas que contienen algún cursor
    QSet<int> activeLines;
    for (const QTextCursor &c : allCursors())
        activeLines.insert(c.blockNumber());

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const int width = m_lineNumberArea->width() - 12;

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const bool active = activeLines.contains(blockNumber);
            painter.setPen(active ? colors.lineNumberActive : colors.lineNumber);
            painter.drawText(0, top, width, fontMetrics().height(), Qt::AlignRight,
                             QString::number(blockNumber + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

// ---------------------------------------------------------------------------
// Pintado de cursores adicionales y selecciones
// ---------------------------------------------------------------------------
void CodeEditor::paintEvent(QPaintEvent *e)
{
    QPlainTextEdit::paintEvent(e);
    if (m_extraCursors.isEmpty())
        return;
    QPainter painter(viewport());
    const QColor caret = Theme::colors().caret;
    for (const QTextCursor &c : std::as_const(m_extraCursors)) {
        const QRect r = cursorRect(c);
        painter.fillRect(QRect(r.x(), r.y(), 2, r.height()), caret);
    }
}

void CodeEditor::updateExtraSelections()
{
    const Theme::Colors &colors = Theme::colors();
    QList<QTextEdit::ExtraSelection> selections;

    // Línea actual (solo con un cursor y sin selección, como en la mayoría de editores)
    if (m_extraCursors.isEmpty() && !textCursor().hasSelection()) {
        QTextEdit::ExtraSelection line;
        line.format.setBackground(colors.currentLine);
        line.format.setProperty(QTextFormat::FullWidthSelection, true);
        line.cursor = textCursor();
        line.cursor.clearSelection();
        selections << line;
    }

    // Resaltados fijos (coincidencias en la pestaña de resultados)
    if (!m_persistentHighlights.isEmpty()) {
        QTextCharFormat fmt;
        fmt.setBackground(colors.findMatch);
        fmt.setForeground(colors.foreground);
        for (const QTextCursor &c : std::as_const(m_persistentHighlights)) {
            QTextEdit::ExtraSelection s;
            s.format = fmt;
            s.cursor = c;
            selections << s;
        }
    }

    // Coincidencias de búsqueda
    if (m_searchActive) {
        QTextCharFormat fmt;
        fmt.setBackground(colors.findMatch);
        for (const QTextCursor &c : std::as_const(m_searchMatches)) {
            QTextEdit::ExtraSelection s;
            s.format = fmt;
            s.cursor = c;
            selections << s;
        }
    }

    // Ocurrencias de la palabra seleccionada
    const QTextCursor main = textCursor();
    if (!m_searchActive && main.hasSelection() && m_extraCursors.isEmpty()) {
        const QString sel = main.selectedText();
        static const QRegularExpression wordRe(QStringLiteral("^\\w+$"));
        if (sel.size() <= 100 && wordRe.match(sel).hasMatch() && blockCount() < 50000) {
            QTextCharFormat fmt;
            fmt.setBackground(colors.currentLine.lighter(150));
            QTextCursor it(document());
            int guard = 0;
            const auto flags = QTextDocument::FindCaseSensitively | QTextDocument::FindWholeWords;
            while (guard++ < 2000) {
                it = document()->find(sel, it, flags);
                if (it.isNull())
                    break;
                if (it.selectionStart() == main.selectionStart())
                    continue;
                QTextEdit::ExtraSelection s;
                s.format = fmt;
                s.cursor = it;
                selections << s;
            }
        }
    }

    // Selecciones de los cursores adicionales
    QTextCharFormat selFmt;
    selFmt.setBackground(colors.selection);
    for (const QTextCursor &c : std::as_const(m_extraCursors)) {
        if (!c.hasSelection())
            continue;
        QTextEdit::ExtraSelection s;
        s.format = selFmt;
        s.cursor = c;
        selections << s;
    }

    setExtraSelections(selections);
    m_lineNumberArea->update();
    viewport()->update();
}

// ---------------------------------------------------------------------------
// Gestión de cursores múltiples
// ---------------------------------------------------------------------------
QList<QTextCursor> CodeEditor::allCursors() const
{
    QList<QTextCursor> list = m_extraCursors;
    list << textCursor();
    std::sort(list.begin(), list.end(), cursorLess);
    return list;
}

void CodeEditor::clearExtraCursors()
{
    if (m_extraCursors.isEmpty())
        return;
    m_extraCursors.clear();
    updateExtraSelections();
    Q_EMIT cursorsChanged();
}

void CodeEditor::setCursors(const QTextCursor &main, const QList<QTextCursor> &extras)
{
    m_extraCursors = extras;
    setTextCursor(main);
    normalizeCursors();
    updateExtraSelections();
    Q_EMIT cursorsChanged();
}

void CodeEditor::normalizeCursors()
{
    if (m_extraCursors.isEmpty())
        return;

    struct Entry {
        QTextCursor cursor;
        bool isMain;
    };
    QList<Entry> entries;
    for (const QTextCursor &c : std::as_const(m_extraCursors))
        entries.push_back({c, false});
    entries.push_back({textCursor(), true});
    std::sort(entries.begin(), entries.end(),
              [](const Entry &a, const Entry &b) { return cursorLess(a.cursor, b.cursor); });

    QList<Entry> merged;
    for (const Entry &e : entries) {
        if (!merged.isEmpty()) {
            Entry &last = merged.last();
            const bool samePos = !last.cursor.hasSelection() && !e.cursor.hasSelection()
                && last.cursor.position() == e.cursor.position();
            const bool overlap = e.cursor.selectionStart() < last.cursor.selectionEnd();
            if (samePos || overlap) {
                const int start = qMin(last.cursor.selectionStart(), e.cursor.selectionStart());
                const int end = qMax(last.cursor.selectionEnd(), e.cursor.selectionEnd());
                QTextCursor c(document());
                c.setPosition(start);
                c.setPosition(end, QTextCursor::KeepAnchor);
                last.cursor = c;
                last.isMain = last.isMain || e.isMain;
                continue;
            }
        }
        merged.push_back(e);
    }

    QList<QTextCursor> extras;
    QTextCursor main = textCursor();
    for (const Entry &e : std::as_const(merged)) {
        if (e.isMain)
            main = e.cursor;
        else
            extras << e.cursor;
    }
    m_extraCursors = extras;
    if (main != textCursor()) {
        const QSignalBlocker blocker(this);
        setTextCursor(main);
    }
}

void CodeEditor::applyToAllCursors(const CursorOp &op)
{
    QTextCursor main = textCursor();
    QList<QTextCursor> extras = m_extraCursors;

    QTextCursor group(document());
    group.beginEditBlock();
    op(main);
    for (QTextCursor &c : extras)
        op(c);
    group.endEditBlock();

    setCursors(main, extras);
    ensureCursorVisible();
}

QList<int> CodeEditor::selectedBlockNumbers() const
{
    QList<int> blocks;
    for (const QTextCursor &c : allCursors()) {
        const int first = document()->findBlock(c.selectionStart()).blockNumber();
        QTextBlock lastBlock = document()->findBlock(c.selectionEnd());
        int last = lastBlock.blockNumber();
        // Una selección que termina al inicio de una línea no incluye esa línea
        if (c.hasSelection() && last > first && c.selectionEnd() == lastBlock.position())
            --last;
        for (int b = first; b <= last; ++b)
            blocks << b;
    }
    std::sort(blocks.begin(), blocks.end());
    blocks.erase(std::unique(blocks.begin(), blocks.end()), blocks.end());
    return blocks;
}

void CodeEditor::addNextOccurrence()
{
    QTextCursor main = textCursor();
    if (!main.hasSelection()) {
        main.select(QTextCursor::WordUnderCursor);
        if (main.hasSelection()) {
            setTextCursor(main);
            updateExtraSelections();
        }
        return;
    }
    const QString text = main.selectedText();
    const auto flags = QTextDocument::FindCaseSensitively;

    int from = main.selectionEnd();
    for (int attempt = 0; attempt < 2; ++attempt) {
        QTextCursor found = document()->find(text, from, flags);
        if (found.isNull()) {
            from = 0; // volver al principio
            continue;
        }
        // ¿Ya existe un cursor sobre esa coincidencia?
        bool exists = false;
        for (const QTextCursor &c : allCursors())
            if (c.selectionStart() == found.selectionStart() && c.selectionEnd() == found.selectionEnd())
                exists = true;
        if (exists) {
            if (attempt == 0 && from != 0) {
                from = 0;
                continue;
            }
            return;
        }
        QList<QTextCursor> extras = m_extraCursors;
        extras << main;
        setCursors(found, extras);
        ensureCursorVisible();
        return;
    }
}

void CodeEditor::selectAllOccurrencesOfSelection()
{
    QTextCursor main = textCursor();
    if (!main.hasSelection())
        main.select(QTextCursor::WordUnderCursor);
    const QString text = main.selectedText();
    if (text.isEmpty())
        return;
    QList<QTextCursor> found;
    QTextCursor it(document());
    while (found.size() < kMaxSearchMatches) {
        it = document()->find(text, it, QTextDocument::FindCaseSensitively);
        if (it.isNull())
            break;
        found << it;
    }
    if (found.isEmpty())
        return;
    const QTextCursor last = found.takeLast();
    setCursors(last, found);
}

void CodeEditor::splitSelectionIntoLines()
{
    QList<QTextCursor> result;
    for (const QTextCursor &c : allCursors()) {
        if (!c.hasSelection()) {
            result << c;
            continue;
        }
        const int start = c.selectionStart();
        const int end = c.selectionEnd();
        QTextBlock block = document()->findBlock(start);
        while (block.isValid() && block.position() <= end) {
            const int lineStart = qMax(block.position(), start);
            const int lineEnd = qMin(block.position() + block.length() - 1, end);
            if (lineStart == end && block.position() == end && !result.isEmpty())
                break; // la selección acaba justo al inicio de esta línea
            QTextCursor lc(document());
            lc.setPosition(lineStart);
            lc.setPosition(lineEnd, QTextCursor::KeepAnchor);
            result << lc;
            block = block.next();
        }
    }
    if (result.isEmpty())
        return;
    const QTextCursor main = result.takeLast();
    setCursors(main, result);
}

void CodeEditor::addCursorAbove()
{
    const QList<QTextCursor> cursors = allCursors();
    QTextCursor ref = cursors.first();
    const int column = ref.position() - ref.block().position();
    const QTextBlock prev = ref.block().previous();
    if (!prev.isValid())
        return;
    QTextCursor c(document());
    c.setPosition(prev.position() + qMin(column, prev.length() - 1));
    QList<QTextCursor> extras = m_extraCursors;
    extras << textCursor();
    setCursors(c, extras);
    ensureCursorVisible();
}

void CodeEditor::addCursorBelow()
{
    const QList<QTextCursor> cursors = allCursors();
    QTextCursor ref = cursors.last();
    const int column = ref.position() - ref.block().position();
    const QTextBlock next = ref.block().next();
    if (!next.isValid())
        return;
    QTextCursor c(document());
    c.setPosition(next.position() + qMin(column, next.length() - 1));
    QList<QTextCursor> extras = m_extraCursors;
    extras << textCursor();
    setCursors(c, extras);
    ensureCursorVisible();
}

// ---------------------------------------------------------------------------
// Portapapeles
// ---------------------------------------------------------------------------
void CodeEditor::copyAll()
{
    const QList<QTextCursor> cursors = allCursors();
    if (cursors.size() == 1 && !cursors.first().hasSelection()) {
        // Sin selección: copia la línea completa
        QString line = cursors.first().block().text() + QLatin1Char('\n');
        QApplication::clipboard()->setText(line);
        return;
    }
    QStringList parts;
    for (const QTextCursor &c : cursors)
        parts << normalizedSelection(c);
    QApplication::clipboard()->setText(parts.join(QLatin1Char('\n')));
}

void CodeEditor::cutAll()
{
    if (isReadOnly()) {
        copyAll();
        return;
    }
    const QList<QTextCursor> cursors = allCursors();
    if (cursors.size() == 1 && !cursors.first().hasSelection()) {
        copyAll();
        QTextCursor c = textCursor();
        c.beginEditBlock();
        c.select(QTextCursor::BlockUnderCursor);
        if (c.block().blockNumber() == 0) {
            // primera línea: borra la línea y su salto
            c.movePosition(QTextCursor::StartOfBlock);
            c.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
            if (c.position() == c.anchor())
                c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        }
        c.removeSelectedText();
        c.endEditBlock();
        setTextCursor(c);
        return;
    }
    copyAll();
    applyToAllCursors([](QTextCursor &c) { c.removeSelectedText(); });
}

void CodeEditor::pasteAll()
{
    if (isReadOnly())
        return;
    QString text = QApplication::clipboard()->text();
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    if (text.isEmpty())
        return;

    if (m_extraCursors.isEmpty()) {
        QTextCursor c = textCursor();
        c.insertText(text);
        setTextCursor(c);
        ensureCursorVisible();
        return;
    }

    const QStringList lines = text.split(QLatin1Char('\n'));
    const QList<QTextCursor> ordered = allCursors();
    if (lines.size() == ordered.size()) {
        // Una línea por cursor, en orden de posición
        const QTextCursor main = textCursor();
        int mainIndex = ordered.size() - 1;
        for (int i = 0; i < ordered.size(); ++i)
            if (ordered.at(i).position() == main.position() && ordered.at(i).anchor() == main.anchor())
                mainIndex = i;

        QList<QTextCursor> cursors = ordered;
        QTextCursor group(document());
        group.beginEditBlock();
        for (int i = 0; i < cursors.size(); ++i)
            cursors[i].insertText(lines.at(i));
        group.endEditBlock();

        QList<QTextCursor> extras;
        for (int i = 0; i < cursors.size(); ++i)
            if (i != mainIndex)
                extras << cursors.at(i);
        setCursors(cursors.at(mainIndex), extras);
        ensureCursorVisible();
        return;
    }
    applyToAllCursors([&text](QTextCursor &c) { c.insertText(text); });
}

// ---------------------------------------------------------------------------
// Operaciones de edición
// ---------------------------------------------------------------------------
void CodeEditor::insertNewline(QTextCursor &c) const
{
    if (c.hasSelection())
        c.removeSelectedText();
    const QString line = c.block().text();
    const int col = c.positionInBlock();
    const QString indent = leadingWhitespace(line.left(col));
    const QChar before = col > 0 ? line.at(col - 1) : QChar();
    const QChar after = col < line.size() ? line.at(col) : QChar();

    const bool opens = before == QLatin1Char('{') || before == QLatin1Char('[') || before == QLatin1Char('(');
    const bool closes = (before == QLatin1Char('{') && after == QLatin1Char('}'))
        || (before == QLatin1Char('[') && after == QLatin1Char(']'))
        || (before == QLatin1Char('(') && after == QLatin1Char(')'));

    if (closes) {
        c.insertText(QLatin1Char('\n') + indent + indentUnit());
        const int pos = c.position();
        c.insertText(QLatin1Char('\n') + indent);
        c.setPosition(pos);
    } else {
        c.insertText(QLatin1Char('\n') + indent + (opens ? indentUnit() : QString()));
    }
}

void CodeEditor::smartBackspace(QTextCursor &c, bool word) const
{
    if (c.hasSelection()) {
        c.removeSelectedText();
        return;
    }
    if (word) {
        c.movePosition(QTextCursor::PreviousWord, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        return;
    }
    const QString before = c.block().text().left(c.positionInBlock());
    if (!before.isEmpty() && before.trimmed().isEmpty() && !before.contains(QLatin1Char('\t'))) {
        // Dentro de la sangría: retrocede hasta la tabulación anterior
        const int remove = ((before.size() - 1) % m_indentWidth) + 1;
        c.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, remove);
        c.removeSelectedText();
        return;
    }
    c.deletePreviousChar();
}

void CodeEditor::toggleLineComment()
{
    if (isReadOnly() || m_lineComment.isEmpty())
        return;
    const QList<int> blocks = selectedBlockNumbers();
    if (blocks.isEmpty())
        return;

    // ¿Todas las líneas no vacías están comentadas?
    bool allCommented = true;
    int minIndent = INT_MAX;
    for (int b : blocks) {
        const QString text = document()->findBlockByNumber(b).text();
        if (text.trimmed().isEmpty())
            continue;
        const QString ws = leadingWhitespace(text);
        minIndent = qMin(minIndent, int(ws.size()));
        if (!text.mid(ws.size()).startsWith(m_lineComment))
            allCommented = false;
    }
    if (minIndent == INT_MAX)
        return;

    QTextCursor group(document());
    group.beginEditBlock();
    for (int b : blocks) {
        const QTextBlock block = document()->findBlockByNumber(b);
        const QString text = block.text();
        if (text.trimmed().isEmpty())
            continue;
        QTextCursor c(block);
        if (allCommented) {
            const int ws = leadingWhitespace(text).size();
            int len = m_lineComment.size();
            if (text.mid(ws + len, 1) == QLatin1String(" "))
                ++len;
            c.setPosition(block.position() + ws);
            c.setPosition(block.position() + ws + len, QTextCursor::KeepAnchor);
            c.removeSelectedText();
        } else {
            c.setPosition(block.position() + minIndent);
            c.insertText(m_lineComment + QLatin1Char(' '));
        }
    }
    group.endEditBlock();
    updateExtraSelections();
}

void CodeEditor::duplicateLines()
{
    if (isReadOnly())
        return;
    const QList<int> blocks = selectedBlockNumbers();
    QTextCursor group(document());
    group.beginEditBlock();
    // De abajo hacia arriba para no desplazar los números de bloque pendientes
    for (int i = blocks.size() - 1; i >= 0; --i) {
        const QTextBlock block = document()->findBlockByNumber(blocks.at(i));
        QTextCursor c(block);
        c.movePosition(QTextCursor::EndOfBlock);
        c.insertText(QLatin1Char('\n') + block.text());
    }
    group.endEditBlock();
}

// Mueve la línea del cursor (o las líneas que toca la selección) una posición
// arriba o abajo, como en Sublime: la línea vecina pasa al otro lado y la
// selección se conserva. Es un único paso de deshacer.
void CodeEditor::moveLines(bool up)
{
    if (isReadOnly())
        return;
    clearExtraCursors();
    QTextCursor cur = textCursor();
    const int anchor = cur.anchor();
    const int position = cur.position();
    QTextDocument *doc = document();
    const int start = qMin(anchor, position);
    const int end = qMax(anchor, position);
    QTextBlock first = doc->findBlock(start);
    QTextBlock last = doc->findBlock(end);
    // Una selección que acaba al principio de una línea no incluye esa línea
    if (end > start && end == last.position() && last.blockNumber() > first.blockNumber())
        last = last.previous();

    const QTextBlock neighbor = up ? first.previous() : last.next();
    if (!neighbor.isValid())
        return;
    const QString text = neighbor.text();
    const int shift = int(text.size()) + 1;

    QTextCursor c(doc);
    c.beginEditBlock();
    if (up) {
        // Copiar la vecina debajo del bloque y después quitarla de arriba
        c.setPosition(last.position() + last.length() - 1);
        c.insertText(QLatin1Char('\n') + text);
        c.setPosition(neighbor.position());
        c.setPosition(first.position(), QTextCursor::KeepAnchor);
        c.removeSelectedText();
    } else {
        // Quitar la vecina de abajo y después ponerla encima del bloque
        c.setPosition(last.position() + last.length() - 1);
        c.setPosition(neighbor.position() + neighbor.length() - 1, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.setPosition(first.position());
        c.insertText(text + QLatin1Char('\n'));
    }
    c.endEditBlock();

    const int delta = up ? -shift : shift;
    cur.setPosition(anchor + delta);
    cur.setPosition(position + delta, QTextCursor::KeepAnchor);
    setTextCursor(cur);
    ensureCursorVisible();
}

void CodeEditor::indentSelection(bool unindent)
{
    if (isReadOnly())
        return;
    const QList<int> blocks = selectedBlockNumbers();
    QTextCursor group(document());
    group.beginEditBlock();
    for (int b : blocks) {
        const QTextBlock block = document()->findBlockByNumber(b);
        QTextCursor c(block);
        if (unindent) {
            const QString text = block.text();
            int remove = 0;
            if (text.startsWith(QLatin1Char('\t')))
                remove = 1;
            else
                while (remove < m_indentWidth && remove < text.size() && text.at(remove) == QLatin1Char(' '))
                    ++remove;
            if (remove > 0) {
                c.setPosition(block.position() + remove, QTextCursor::KeepAnchor);
                c.removeSelectedText();
            }
        } else if (!block.text().isEmpty() || blocks.size() == 1) {
            c.insertText(indentUnit());
        }
    }
    group.endEditBlock();
    updateExtraSelections();
}

void CodeEditor::goToLine(int line)
{
    const QTextBlock block = document()->findBlockByNumber(qBound(0, line - 1, blockCount() - 1));
    QTextCursor c(block);
    m_extraCursors.clear();
    setTextCursor(c);
    centerCursor();
    setFocus();
}

// ---------------------------------------------------------------------------
// Teclado y ratón
// ---------------------------------------------------------------------------
bool CodeEditor::isEditorCommand(QKeyEvent *e) const
{
    const auto mods = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
    const int key = e->key();
    if (mods == Qt::ControlModifier && (key == Qt::Key_D || key == Qt::Key_Slash))
        return true;
    if (mods == (Qt::ControlModifier | Qt::ShiftModifier) && (key == Qt::Key_L || key == Qt::Key_D))
        return true;
    if (mods == (Qt::ControlModifier | Qt::ShiftModifier) && (key == Qt::Key_Up || key == Qt::Key_Down))
        return true;
    if (mods == (Qt::ControlModifier | Qt::AltModifier) && (key == Qt::Key_Up || key == Qt::Key_Down))
        return true;
    if (mods == Qt::AltModifier && key == Qt::Key_F3)
        return true;
    if (e->matches(QKeySequence::Copy) || e->matches(QKeySequence::Cut) || e->matches(QKeySequence::Paste))
        return true;
    if (key == Qt::Key_Escape && !m_extraCursors.isEmpty())
        return true;
    return false;
}

bool CodeEditor::handleEditorCommand(QKeyEvent *e)
{
    const auto mods = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
    const int key = e->key();

    if (e->matches(QKeySequence::Copy)) { copyAll(); return true; }
    if (e->matches(QKeySequence::Cut)) { cutAll(); return true; }
    if (e->matches(QKeySequence::Paste)) { pasteAll(); return true; }

    if (mods == Qt::ControlModifier && key == Qt::Key_D) { addNextOccurrence(); return true; }
    if (mods == Qt::ControlModifier && key == Qt::Key_Slash) { toggleLineComment(); return true; }
    if (mods == (Qt::ControlModifier | Qt::ShiftModifier) && key == Qt::Key_L) { splitSelectionIntoLines(); return true; }
    if (mods == (Qt::ControlModifier | Qt::ShiftModifier) && key == Qt::Key_D) { duplicateLines(); return true; }
    if (mods == (Qt::ControlModifier | Qt::ShiftModifier) && key == Qt::Key_Up) { moveLines(true); return true; }
    if (mods == (Qt::ControlModifier | Qt::ShiftModifier) && key == Qt::Key_Down) { moveLines(false); return true; }
    if (mods == (Qt::ControlModifier | Qt::AltModifier) && key == Qt::Key_Up) { addCursorAbove(); return true; }
    if (mods == (Qt::ControlModifier | Qt::AltModifier) && key == Qt::Key_Down) { addCursorBelow(); return true; }
    if (mods == Qt::AltModifier && key == Qt::Key_F3) { selectAllOccurrencesOfSelection(); return true; }
    if (key == Qt::Key_Escape && !m_extraCursors.isEmpty()) { clearExtraCursors(); return true; }
    return false;
}

bool CodeEditor::event(QEvent *e)
{
    // Reclama los atajos propios antes de que los capture una QAction del menú
    if (e->type() == QEvent::ShortcutOverride) {
        auto *ke = static_cast<QKeyEvent *>(e);
        if (isEditorCommand(ke)) {
            e->accept();
            return true;
        }
    }
    return QPlainTextEdit::event(e);
}

void CodeEditor::keyPressEvent(QKeyEvent *e)
{
    if (handleEditorCommand(e))
        return;

    if (!m_extraCursors.isEmpty()) {
        if (handleMultiCursorKey(e))
            return;
        // Cualquier otra tecla (deshacer, seleccionar todo...) vuelve a un solo cursor
        m_extraCursors.clear();
        updateExtraSelections();
        Q_EMIT cursorsChanged();
    }

    if (isReadOnly()) {
        QPlainTextEdit::keyPressEvent(e);
        return;
    }

    const auto mods = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
    QTextCursor c = textCursor();

    switch (e->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (mods == Qt::NoModifier) {
            c.beginEditBlock();
            insertNewline(c);
            c.endEditBlock();
            setTextCursor(c);
            ensureCursorVisible();
            return;
        }
        break;
    case Qt::Key_Tab:
        if (mods == Qt::NoModifier) {
            if (c.hasSelection() && c.block() != document()->findBlock(c.selectionEnd())) {
                indentSelection(false);
            } else {
                const int col = c.positionInBlock();
                c.insertText(QString(m_indentWidth - (col % m_indentWidth), QLatin1Char(' ')));
                setTextCursor(c);
            }
            return;
        }
        break;
    case Qt::Key_Backtab:
        indentSelection(true);
        return;
    case Qt::Key_Backspace:
        if (mods == Qt::NoModifier || mods == Qt::ShiftModifier) {
            smartBackspace(c, false);
            setTextCursor(c);
            return;
        }
        break;
    default:
        break;
    }
    QPlainTextEdit::keyPressEvent(e);
}

bool CodeEditor::handleMultiCursorKey(QKeyEvent *e)
{
    const auto mods = e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
    const bool shift = mods & Qt::ShiftModifier;
    const bool ctrl = mods & Qt::ControlModifier;
    const bool alt = mods & Qt::AltModifier;
    const auto anchorMode = shift ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor;

    auto move = [&](QTextCursor::MoveOperation op, bool collapseToStart) {
        applyToAllCursors([&](QTextCursor &c) {
            if (!shift && c.hasSelection() && (op == QTextCursor::Left || op == QTextCursor::Right)) {
                c.setPosition(collapseToStart ? c.selectionStart() : c.selectionEnd());
                return;
            }
            c.movePosition(op, anchorMode);
        });
    };

    switch (e->key()) {
    case Qt::Key_Left:
        move(ctrl ? QTextCursor::PreviousWord : QTextCursor::Left, true);
        return true;
    case Qt::Key_Right:
        move(ctrl ? QTextCursor::NextWord : QTextCursor::Right, false);
        return true;
    case Qt::Key_Up:
        if (ctrl) return false;
        move(QTextCursor::Up, true);
        return true;
    case Qt::Key_Down:
        if (ctrl) return false;
        move(QTextCursor::Down, false);
        return true;
    case Qt::Key_Home:
        move(QTextCursor::StartOfLine, true);
        return true;
    case Qt::Key_End:
        move(QTextCursor::EndOfLine, false);
        return true;
    default:
        break;
    }

    if (isReadOnly())
        return false;

    switch (e->key()) {
    case Qt::Key_Backspace:
        applyToAllCursors([&](QTextCursor &c) { smartBackspace(c, ctrl); });
        return true;
    case Qt::Key_Delete:
        applyToAllCursors([&](QTextCursor &c) {
            if (c.hasSelection())
                c.removeSelectedText();
            else if (ctrl) {
                c.movePosition(QTextCursor::NextWord, QTextCursor::KeepAnchor);
                c.removeSelectedText();
            } else
                c.deleteChar();
        });
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        applyToAllCursors([this](QTextCursor &c) { insertNewline(c); });
        return true;
    case Qt::Key_Tab:
        applyToAllCursors([this](QTextCursor &c) {
            const int col = c.positionInBlock();
            c.insertText(QString(m_indentWidth - (col % m_indentWidth), QLatin1Char(' ')));
        });
        return true;
    case Qt::Key_Backtab:
        indentSelection(true);
        return true;
    default:
        break;
    }

    // Texto normal. Ctrl+Alt se permite porque AltGr en Windows se reporta así.
    const QString text = e->text();
    const bool plainCtrl = ctrl && !alt;
    if (!text.isEmpty() && !plainCtrl && text.at(0).isPrint()) {
        applyToAllCursors([&text](QTextCursor &c) { c.insertText(text); });
        return true;
    }
    return false;
}

void CodeEditor::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier)
        && !(e->modifiers() & Qt::ShiftModifier)) {
        // Ctrl+clic: añade (o quita) un cursor
        const QTextCursor clicked = cursorForPosition(e->position().toPoint());
        const QTextCursor main = textCursor();
        QList<QTextCursor> extras = m_extraCursors;
        for (int i = 0; i < extras.size(); ++i) {
            if (extras.at(i).position() == clicked.position()) {
                extras.removeAt(i);
                setCursors(main, extras);
                return;
            }
        }
        if (main.position() == clicked.position() && !extras.isEmpty()) {
            const QTextCursor newMain = extras.takeLast();
            setCursors(newMain, extras);
            return;
        }
        extras << main;
        setCursors(clicked, extras);
        return;
    }
    if (e->button() == Qt::LeftButton && !m_extraCursors.isEmpty())
        clearExtraCursors();
    QPlainTextEdit::mousePressEvent(e);
}

// ---------------------------------------------------------------------------
// Búsqueda y reemplazo
// ---------------------------------------------------------------------------
void CodeEditor::setSearch(const QString &pattern, const SearchOptions &options)
{
    if (pattern.isEmpty()) {
        clearSearch();
        return;
    }
    QString expr = options.regex ? pattern : QRegularExpression::escape(pattern);
    if (options.wholeWord)
        expr = QStringLiteral("\\b(?:%1)\\b").arg(expr);
    QRegularExpression::PatternOptions opts = QRegularExpression::UseUnicodePropertiesOption;
    if (!options.caseSensitive)
        opts |= QRegularExpression::CaseInsensitiveOption;
    m_searchRegex = QRegularExpression(expr, opts);
    m_searchCaseSensitive = options.caseSensitive;
    m_searchActive = m_searchRegex.isValid();
    recomputeSearchMatches();
}

void CodeEditor::clearSearch()
{
    m_searchActive = false;
    m_searchMatches.clear();
    m_searchRegex = QRegularExpression();
    updateExtraSelections();
}

void CodeEditor::recomputeSearchMatches()
{
    m_searchMatches.clear();
    if (m_searchActive) {
        // QTextDocument::find decide mayúsculas/minúsculas con su propio indicador
        const QTextDocument::FindFlags flags = m_searchCaseSensitive
            ? QTextDocument::FindCaseSensitively : QTextDocument::FindFlags();
        QTextCursor it(document());
        while (m_searchMatches.size() < kMaxSearchMatches) {
            it = document()->find(m_searchRegex, it, flags);
            if (it.isNull())
                break;
            if (!it.hasSelection()) {
                // Coincidencia vacía (p. ej. "^"): avanza para no entrar en bucle
                if (!it.movePosition(QTextCursor::NextCharacter))
                    break;
                continue;
            }
            m_searchMatches << it;
        }
    }
    updateExtraSelections();
}

int CodeEditor::currentSearchIndex() const
{
    const QTextCursor c = textCursor();
    for (int i = 0; i < m_searchMatches.size(); ++i)
        if (m_searchMatches.at(i).selectionStart() == c.selectionStart()
            && m_searchMatches.at(i).selectionEnd() == c.selectionEnd())
            return i;
    return -1;
}

bool CodeEditor::findNext(bool backward)
{
    if (!m_searchActive || m_searchMatches.isEmpty())
        return false;
    const QTextCursor c = textCursor();
    int index = -1;
    if (!backward) {
        for (int i = 0; i < m_searchMatches.size(); ++i)
            if (m_searchMatches.at(i).selectionStart() >= c.selectionEnd()
                && !(m_searchMatches.at(i).selectionStart() == c.selectionStart() && c.hasSelection())) {
                index = i;
                break;
            }
        if (index < 0)
            index = 0;
    } else {
        for (int i = m_searchMatches.size() - 1; i >= 0; --i)
            if (m_searchMatches.at(i).selectionEnd() <= c.selectionStart()) {
                index = i;
                break;
            }
        if (index < 0)
            index = m_searchMatches.size() - 1;
    }
    m_extraCursors.clear();
    setTextCursor(m_searchMatches.at(index));
    centerCursor();
    return true;
}

bool CodeEditor::replaceCurrent(const QString &replacement)
{
    if (isReadOnly() || !m_searchActive)
        return false;
    if (currentSearchIndex() < 0)
        return findNext(false);
    QTextCursor c = textCursor();
    QString text = normalizedSelection(c);
    text.replace(m_searchRegex, replacement);
    c.insertText(text);
    setTextCursor(c);
    findNext(false);
    return true;
}

int CodeEditor::replaceAll(const QString &replacement)
{
    if (isReadOnly() || !m_searchActive || m_searchMatches.isEmpty())
        return 0;
    QList<QTextCursor> matches = m_searchMatches;
    const bool wasActive = m_searchActive;
    m_searchActive = false; // evita recalcular en cada sustitución
    QTextCursor group(document());
    group.beginEditBlock();
    for (QTextCursor &c : matches) {
        QString text = normalizedSelection(c);
        text.replace(m_searchRegex, replacement);
        c.insertText(text);
    }
    group.endEditBlock();
    m_searchActive = wasActive;
    recomputeSearchMatches();
    return matches.size();
}

bool CodeEditor::selectAllMatches()
{
    if (!m_searchActive || m_searchMatches.isEmpty())
        return false;
    QList<QTextCursor> found = m_searchMatches;
    const QTextCursor main = found.takeLast();
    setCursors(main, found);
    setFocus();
    return true;
}
