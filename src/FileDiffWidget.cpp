#include "FileDiffWidget.h"

#include "DiffHighlighter.h"
#include "Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <functional>

QColor colorForChangeCode(QChar code)
{
    const Theme::Colors &c = Theme::colors();
    switch (code.toLatin1()) {
    case 'A':
    case '?': return c.added;
    case 'D':
    case 'U': return c.removed;
    case 'R': return c.type;
    default: return c.modified;
    }
}

QString describeChangeCode(QChar code)
{
    switch (code.toLatin1()) {
    case 'A': return QObject::tr("nuevo");
    case '?': return QObject::tr("sin seguimiento");
    case 'D': return QObject::tr("borrado");
    case 'R': return QObject::tr("renombrado");
    case 'T': return QObject::tr("cambio de tipo");
    case 'U': return QObject::tr("conflicto");
    default: return QObject::tr("modificado");
    }
}

// ---------------------------------------------------------------------------
// DiffTextView
// ---------------------------------------------------------------------------
DiffTextView::DiffTextView(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setReadOnly(true);
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFrameShape(QFrame::NoFrame);
    setFont(Theme::editorFont());
    new DiffHighlighter(document());

    QPalette p = palette();
    p.setColor(QPalette::Base, Theme::colors().background);
    setPalette(p);
}

void DiffTextView::setDiffText(const QString &text)
{
    setPlainText(text);
    fitHeight();
}

void DiffTextView::fitHeight()
{
    const int lines = qMax(1, document()->blockCount());
    const int h = lines * fontMetrics().lineSpacing() + int(document()->documentMargin() * 2) + 4
        + horizontalScrollBar()->sizeHint().height();
    setFixedHeight(h);
}

void DiffTextView::wheelEvent(QWheelEvent *e)
{
    // El desplazamiento vertical lo hace el panel exterior
    if (!(e->modifiers() & Qt::ShiftModifier) && e->angleDelta().x() == 0) {
        e->ignore();
        return;
    }
    QPlainTextEdit::wheelEvent(e);
}

// ---------------------------------------------------------------------------
// FileDiffWidget
// ---------------------------------------------------------------------------
namespace {

class ClickableHeader : public QWidget
{
public:
    explicit ClickableHeader(std::function<void()> onClick, QWidget *parent)
        : QWidget(parent), m_onClick(std::move(onClick))
    {
        setCursor(Qt::PointingHandCursor);
        setAutoFillBackground(true);
        QPalette p = palette();
        p.setColor(QPalette::Window, Theme::colors().panel);
        setPalette(p);
    }

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton)
            m_onClick();
    }

private:
    std::function<void()> m_onClick;
};

} // namespace

FileDiffWidget::FileDiffWidget(const GitRepository::FileChange &change, QWidget *parent)
    : QWidget(parent)
    , m_change(change)
{
    const Theme::Colors &c = Theme::colors();

    auto *header = new ClickableHeader([this] { setExpanded(!m_expanded); }, this);
    m_toggle = new QToolButton(header);
    m_toggle->setAutoRaise(true);
    m_toggle->setFixedWidth(22);
    m_toggle->setStyleSheet(QStringLiteral("QToolButton { padding:0px; border:none; background:transparent; color:%1; }")
                                .arg(Theme::colors().muted.name()));
    connect(m_toggle, &QToolButton::clicked, this, [this] { setExpanded(!m_expanded); });

    auto *code = new QLabel(QString(change.code == QLatin1Char('?') ? QLatin1Char('U') : change.code), header);
    code->setToolTip(describeChangeCode(change.code));
    code->setAlignment(Qt::AlignCenter);
    code->setFixedWidth(20);
    code->setStyleSheet(QStringLiteral("color:%1; background:%2; border-radius:3px; font-weight:bold;")
                            .arg(c.background.name(), colorForChangeCode(change.code).name()));

    QString pathText = change.path.toHtmlEscaped();
    const int slash = change.path.lastIndexOf(QLatin1Char('/'));
    if (slash > 0) {
        pathText = QStringLiteral("<span style='color:%1'>%2/</span>%3")
                       .arg(c.muted.name(), change.path.left(slash).toHtmlEscaped(),
                            change.path.mid(slash + 1).toHtmlEscaped());
    }
    if (change.code == QLatin1Char('R') && change.oldPath != change.path)
        pathText = QStringLiteral("<span style='color:%1'>%2 → </span>%3")
                       .arg(c.muted.name(), change.oldPath.toHtmlEscaped(), pathText);
    auto *path = new QLabel(pathText, header);
    path->setTextFormat(Qt::RichText);

    auto *stats = new QLabel(header);
    stats->setTextFormat(Qt::RichText);
    if (change.binary)
        stats->setText(tr("binario"));
    else
        stats->setText(QStringLiteral("<span style='color:%1'>+%2</span>&nbsp;&nbsp;<span style='color:%3'>−%4</span>")
                           .arg(c.added.name()).arg(change.additions).arg(c.removed.name()).arg(change.deletions));

    m_actions = new QHBoxLayout;
    m_actions->setSpacing(4);

    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(4, 4, 8, 4);
    hl->addWidget(m_toggle);
    hl->addWidget(code);
    hl->addSpacing(6);
    hl->addWidget(path, 1);
    hl->addWidget(stats);
    hl->addSpacing(8);
    hl->addLayout(m_actions);

    // Cuerpo
    m_body = new QWidget(this);
    auto *bl = new QVBoxLayout(m_body);
    bl->setContentsMargins(0, 0, 0, 0);
    if (change.binary || change.patch.isEmpty()) {
        auto *msg = new QLabel(change.binary ? tr("Archivo binario: no se muestra el contenido.")
                                             : (change.truncated ? tr("Demasiados archivos: diff omitido.")
                                                                 : tr("Sin cambios de contenido.")),
                               m_body);
        msg->setContentsMargins(36, 8, 8, 8);
        QPalette p = msg->palette();
        p.setColor(QPalette::WindowText, c.muted);
        msg->setPalette(p);
        bl->addWidget(msg);
    } else {
        auto *view = new DiffTextView(m_body);
        view->setDiffText(change.patch);
        bl->addWidget(view);
        if (change.truncated) {
            auto *more = new QLabel(tr("… diff recortado (archivo muy grande)"), m_body);
            more->setContentsMargins(36, 4, 8, 4);
            bl->addWidget(more);
        }
    }

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addWidget(m_body);

    const int patchLines = int(change.patch.count(QLatin1Char('\n')));
    setExpanded(patchLines < 400);
}

QToolButton *FileDiffWidget::addAction(const QString &text, const QString &toolTip)
{
    auto *b = new QToolButton(this);
    b->setText(text);
    b->setToolTip(toolTip);
    m_actions->addWidget(b);
    return b;
}

void FileDiffWidget::setExpanded(bool expanded)
{
    m_expanded = expanded;
    m_body->setVisible(expanded);
    m_toggle->setText(expanded ? QStringLiteral("▾") : QStringLiteral("▸"));
}
