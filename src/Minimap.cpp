#include "Minimap.h"

#include "CodeEditor.h"
#include "Theme.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextLayout>
#include <QWheelEvent>

#include <vector>

Minimap::Minimap(CodeEditor *editor)
    : QWidget(editor)
    , m_editor(editor)
{
    setCursor(Qt::ArrowCursor);
    setAttribute(Qt::WA_OpaquePaintEvent);
    auto repaint = [this] { update(); };
    connect(editor->verticalScrollBar(), &QScrollBar::valueChanged, this, repaint);
    connect(editor->verticalScrollBar(), &QScrollBar::rangeChanged, this, repaint);
    connect(editor, &QPlainTextEdit::updateRequest, this, repaint);
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, repaint);
}

// ---------------------------------------------------------------------------
// Geometría
// ---------------------------------------------------------------------------
// Sin ajuste de línea el valor de la barra es el primer bloque visible; con ajuste
// es la primera línea visual, que basta como aproximación.
int Minimap::firstVisibleLine() const
{
    return m_editor->verticalScrollBar()->value();
}

int Minimap::visibleLineCount() const
{
    const int spacing = qMax(1, m_editor->fontMetrics().lineSpacing());
    return qMax(1, m_editor->viewport()->height() / spacing);
}

int Minimap::maxFirstLine() const
{
    return qMax(0, m_editor->verticalScrollBar()->maximum());
}

// Como en Sublime: si el archivo no cabe, el minimapa se desplaza en proporción
// al editor, de modo que al final del archivo se ve su última línea.
int Minimap::offset() const
{
    const int total = m_editor->blockCount() * kLineHeight;
    const int overflow = total - height();
    if (overflow <= 0 || maxFirstLine() == 0)
        return 0;
    return int(double(firstVisibleLine()) / maxFirstLine() * overflow);
}

double Minimap::boxSpeed() const
{
    const int total = m_editor->blockCount() * kLineHeight;
    const int overflow = total - height();
    if (overflow <= 0 || maxFirstLine() == 0)
        return kLineHeight;
    return kLineHeight - double(overflow) / maxFirstLine();
}

int Minimap::lineAt(int y) const
{
    return qBound(0, (y + offset()) / kLineHeight, qMax(0, m_editor->blockCount() - 1));
}

QRect Minimap::viewportRect() const
{
    const int top = firstVisibleLine() * kLineHeight - offset();
    // Al final del archivo el editor puede mostrar menos líneas que su altura
    const int lines = qMin(visibleLineCount(), m_editor->blockCount() - firstVisibleLine());
    const int h = qMax(kLineHeight * 2, lines * kLineHeight);
    return QRect(0, top, width(), h);
}

void Minimap::scrollToFirstLine(int line)
{
    m_editor->verticalScrollBar()->setValue(qBound(0, line, maxFirstLine()));
}

// La parte superior del recuadro sigue al ratón (menos el punto donde se agarró)
void Minimap::dragTo(int y)
{
    const double speed = boxSpeed();
    if (speed <= 0.0)
        return;
    scrollToFirstLine(qRound((y - m_grab) / speed));
}

// ---------------------------------------------------------------------------
// Dibujo
// ---------------------------------------------------------------------------
void Minimap::paintEvent(QPaintEvent *event)
{
    const Theme::Colors &c = Theme::colors();
    QPainter p(this);
    p.fillRect(event->rect(), c.background);

    const int off = offset();
    const int firstLine = qMax(0, (event->rect().top() + off) / kLineHeight);
    const int lastLine = (event->rect().bottom() + off) / kLineHeight;
    const int tab = qMax(1, m_editor->indentWidth());
    const int left = 4;
    const double charWidth = double(width() - left - 2) / kMaxColumns;

    // Línea del cursor
    const int cursorLine = m_editor->textCursor().blockNumber();
    if (cursorLine >= firstLine && cursorLine <= lastLine)
        p.fillRect(QRect(0, cursorLine * kLineHeight - off, width(), kLineHeight), c.currentLine.lighter(140));

    std::vector<QColor> colors;
    QTextBlock block = m_editor->document()->findBlockByNumber(firstLine);
    for (int line = firstLine; block.isValid() && line <= lastLine; block = block.next(), ++line) {
        const QString text = block.text();
        if (text.isEmpty())
            continue;
        // Color de cada carácter según el resaltado de sintaxis
        const int n = qMin<int>(text.size(), kMaxColumns * 2);
        colors.assign(size_t(n), c.foreground);
        if (QTextLayout *layout = block.layout()) {
            for (const QTextLayout::FormatRange &r : layout->formats()) {
                if (!r.format.hasProperty(QTextFormat::ForegroundBrush))
                    continue;
                const QColor fg = r.format.foreground().color();
                for (int i = qMax(0, r.start); i < qMin(n, r.start + r.length); ++i)
                    colors[size_t(i)] = fg;
            }
        }
        // Tramos de caracteres no blancos del mismo color -> un rectángulo cada uno
        const int y = line * kLineHeight - off;
        int col = 0;
        int i = 0;
        while (i < n && col < kMaxColumns) {
            const QChar ch = text.at(i);
            if (ch == QLatin1Char('\t')) {
                col += tab - col % tab;
                ++i;
                continue;
            }
            if (ch.isSpace()) {
                ++col;
                ++i;
                continue;
            }
            const QColor color = colors[size_t(i)];
            const int start = col;
            while (i < n && col < kMaxColumns && !text.at(i).isSpace() && colors[size_t(i)] == color) {
                ++col;
                ++i;
            }
            QColor dim = color;
            dim.setAlpha(170);
            p.fillRect(QRectF(left + start * charWidth, y, (col - start) * charWidth, kLineHeight - 1), dim);
        }
    }

    // Recuadro de la parte visible
    const QRect box = viewportRect();
    QColor fill = c.foreground;
    fill.setAlpha(underMouse() || m_dragging ? 34 : 20);
    p.fillRect(box, fill);
    QColor edge = c.foreground;
    edge.setAlpha(45);
    p.setPen(edge);
    p.drawRect(box.adjusted(0, 0, -1, -1));

    // Separación con el texto
    p.setPen(c.border);
    p.drawLine(0, 0, 0, height());
}

// ---------------------------------------------------------------------------
// Ratón
// ---------------------------------------------------------------------------
void Minimap::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const int y = event->position().toPoint().y();
    const QRect box = viewportRect();
    m_dragging = true;
    m_moved = false;
    m_pressY = y;
    m_pressLine = lineAt(y);
    if (box.contains(0, y)) {
        m_grab = y - box.top();               // arrastrar el recuadro desde donde se agarró
    } else {
        // Clic fuera del recuadro: centrar esa línea y seguir arrastrando desde el centro
        scrollToFirstLine(m_pressLine - visibleLineCount() / 2);
        m_grab = y - viewportRect().top();
    }
    update();
}

void Minimap::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging)
        return;
    const int y = event->position().toPoint().y();
    if (qAbs(y - m_pressY) >= QApplication::startDragDistance())
        m_moved = true;
    if (m_moved)
        dragTo(y);
}

void Minimap::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_dragging)
        return;
    m_dragging = false;
    if (!m_moved) {
        // Un clic sin arrastrar lleva el cursor a esa línea
        const int line = m_pressLine;
        const int first = m_editor->verticalScrollBar()->value();
        QTextCursor cursor(m_editor->document()->findBlockByNumber(line));
        m_editor->setTextCursor(cursor);
        scrollToFirstLine(first);              // setTextCursor no debe mover la vista
    }
    m_editor->setFocus();
    update();
}

void Minimap::wheelEvent(QWheelEvent *event)
{
    QApplication::sendEvent(m_editor->viewport(), event);
}
