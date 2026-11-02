#include "GitCommandPalette.h"

#include "QuickOpenDialog.h"
#include "Theme.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QScreen>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>

namespace {

enum Roles { IndexRole = Qt::UserRole + 1, DescriptionRole, EnabledRole };

// Comando a la izquierda, descripción en cursiva a la derecha.
class PaletteDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        return {300, option.fontMetrics.height() + 14};
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const Theme::Colors &c = Theme::colors();
        const QRect r = option.rect.adjusted(10, 0, -10, 0);
        const bool selected = option.state & QStyle::State_Selected;
        const bool enabled = index.data(EnabledRole).toBool();
        p->save();
        if (selected)
            p->fillRect(option.rect, c.selection);
        else if (option.state & QStyle::State_MouseOver)
            p->fillRect(option.rect, c.currentLine);

        QFont commandFont = option.font;
        p->setFont(commandFont);
        p->setPen(enabled ? c.foreground : c.muted);
        const QString command = index.data(Qt::DisplayRole).toString();
        const int commandWidth = QFontMetrics(commandFont).horizontalAdvance(command);
        p->drawText(r, Qt::AlignLeft | Qt::AlignVCenter, command);

        QFont descFont = option.font;
        descFont.setItalic(true);
        descFont.setPointSizeF(option.font.pointSizeF() * 0.92);
        p->setFont(descFont);
        p->setPen(c.muted);
        const QRect descRect = r.adjusted(commandWidth + 24, 0, 0, 0);
        const QString desc = QFontMetrics(descFont).elidedText(index.data(DescriptionRole).toString(),
                                                               Qt::ElideLeft, descRect.width());
        p->drawText(descRect, Qt::AlignRight | Qt::AlignVCenter, desc);
        p->restore();
    }
};

} // namespace

GitCommandPalette::GitCommandPalette(const QString &title, const QString &placeholder,
                                     const QList<Item> &items, QWidget *parent)
    : QFrame(parent, Qt::Popup)
    , m_items(items)
    , m_title(new QLabel(title, this))
    , m_filter(new QLineEdit(this))
    , m_list(new QListWidget(this))
{
    const Theme::Colors &c = Theme::colors();
    setAttribute(Qt::WA_DeleteOnClose);
    setObjectName(QStringLiteral("gitPalette"));
    setStyleSheet(QStringLiteral(
                      "QFrame#gitPalette { background:%1; border:2px solid %2; }"
                      "QLabel#paletteTitle { background:%3; color:%4; border-radius:3px; padding:2px 8px; font-size:13pt; }"
                      "QLineEdit#paletteFilter { background:transparent; border:none; font-size:13pt; color:%4; }"
                      "QListWidget { background:%1; border:none; }")
                      .arg(c.panel.name(), c.accent.name(), c.selection.name(), c.foreground.name()));

    m_title->setObjectName(QStringLiteral("paletteTitle"));
    m_filter->setObjectName(QStringLiteral("paletteFilter"));
    m_filter->setPlaceholderText(placeholder);
    m_filter->installEventFilter(this);

    m_list->setItemDelegate(new PaletteDelegate(m_list));
    m_list->setMouseTracking(true);
    m_list->setUniformItemSizes(true);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_list, &QListWidget::itemClicked, this, [this] { activateCurrent(); });

    auto *header = new QWidget(this);
    header->setAutoFillBackground(true);
    QPalette hp = header->palette();
    hp.setColor(QPalette::Window, c.background);
    header->setPalette(hp);
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(8, 6, 8, 6);
    hl->addWidget(m_title);
    hl->addWidget(m_filter, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);
    layout->addWidget(header);
    layout->addWidget(m_list, 1);

    connect(m_filter, &QLineEdit::textChanged, this, &GitCommandPalette::refilter);
    refilter();
}

void GitCommandPalette::refilter()
{
    QString pattern = m_filter->text();
    pattern.remove(QLatin1Char(' '));
    QList<QPair<int, int>> scored; // (puntuación, índice)
    for (int i = 0; i < m_items.size(); ++i) {
        const QString haystack = m_items.at(i).command + QLatin1Char(' ') + m_items.at(i).description;
        const int s = pattern.isEmpty() ? 0 : QuickOpenDialog::fuzzyScore(pattern, haystack);
        if (s >= 0)
            scored.append({s, i});
    }
    if (!pattern.isEmpty())
        std::stable_sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) { return a.first > b.first; });

    m_list->clear();
    for (const auto &entry : std::as_const(scored)) {
        const Item &item = m_items.at(entry.second);
        auto *li = new QListWidgetItem(item.command, m_list);
        li->setData(IndexRole, entry.second);
        li->setData(DescriptionRole, item.description);
        li->setData(EnabledRole, item.enabled);
        if (!item.enabled)
            li->setFlags(li->flags() & ~Qt::ItemIsEnabled);
    }
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->flags() & Qt::ItemIsEnabled) {
            m_list->setCurrentRow(i);
            break;
        }
}

void GitCommandPalette::setCurrentCommand(const QString &command)
{
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->text() == command)
            m_list->setCurrentRow(i);
}

void GitCommandPalette::popup(QWidget *anchor, int preferredWidth)
{
    const int rowHeight = m_list->sizeHintForRow(0) > 0 ? m_list->sizeHintForRow(0) : 28;
    const int rows = qMin(int(m_items.size()), 12);
    const int width = qMin(preferredWidth, qMax(420, anchor->width() - 80));
    resize(width, 56 + rows * rowHeight + 12);

    const QPoint topCenter = anchor->mapToGlobal(QPoint(anchor->width() / 2, 0));
    move(topCenter.x() - width / 2, topCenter.y() + 4);
    show();
    m_filter->setFocus();
}

void GitCommandPalette::activateCurrent()
{
    QListWidgetItem *current = m_list->currentItem();
    if (!current || !(current->flags() & Qt::ItemIsEnabled))
        return;
    const auto action = m_items.at(current->data(IndexRole).toInt()).action;
    close();
    // La acción puede abrir otra paleta: se ejecuta cuando esta ya se cerró
    if (action)
        QTimer::singleShot(0, action);
}

bool GitCommandPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_filter && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        int row = m_list->currentRow();
        switch (ke->key()) {
        case Qt::Key_Down:
        case Qt::Key_Up: {
            const int step = ke->key() == Qt::Key_Down ? 1 : -1;
            for (int i = row + step; i >= 0 && i < m_list->count(); i += step)
                if (m_list->item(i)->flags() & Qt::ItemIsEnabled) {
                    m_list->setCurrentRow(i);
                    break;
                }
            return true;
        }
        case Qt::Key_Return:
        case Qt::Key_Enter:
            activateCurrent();
            return true;
        case Qt::Key_Escape:
            close();
            return true;
        default:
            break;
        }
    }
    return QFrame::eventFilter(watched, event);
}
