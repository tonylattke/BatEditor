#include "ModeSwitch.h"

#include "Theme.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QToolButton>

ModeSwitch::ModeSwitch(QWidget *parent)
    : QWidget(parent)
    , m_editor(new QToolButton(this))
    , m_git(new QToolButton(this))
{
    const Theme::Colors &c = Theme::colors();
    m_editor->setText(tr("✎  Editor"));
    m_editor->setToolTip(tr("Modo editor de texto (Ctrl+Shift+E)"));
    m_git->setText(tr("⎇  Git"));
    m_git->setToolTip(tr("Modo Git (Ctrl+Shift+G)"));

    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    for (QToolButton *b : {m_editor, m_git}) {
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setMinimumWidth(84);
        group->addButton(b);
    }
    m_editor->setChecked(true);

    // Estilo de "pastilla": el segmento activo se pinta con el color de acento
    const QString base = QStringLiteral(
        "QToolButton { background:%1; color:%2; border:1px solid %3; padding:3px 12px; }"
        "QToolButton:hover { color:%4; }"
        "QToolButton:checked { background:%5; color:%6; font-weight:bold; border-color:%5; }");
    const QString style = base.arg(c.backgroundAlt.name(), c.muted.name(), c.border.name(), c.foreground.name(),
                                   c.accent.name(), c.background.name());
    m_editor->setStyleSheet(style + QStringLiteral("QToolButton { border-top-left-radius:4px; border-bottom-left-radius:4px; border-right:none; }"));
    m_git->setStyleSheet(style + QStringLiteral("QToolButton { border-top-right-radius:4px; border-bottom-right-radius:4px; }"));

    connect(m_editor, &QToolButton::clicked, this, [this] { setMode(Editor); });
    connect(m_git, &QToolButton::clicked, this, [this] { setMode(Git); });

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 2, 8, 2);
    layout->setSpacing(0);
    layout->addWidget(m_editor);
    layout->addWidget(m_git);
}

void ModeSwitch::setMode(Mode mode)
{
    m_editor->setChecked(mode == Editor);
    m_git->setChecked(mode == Git);
    if (mode == m_mode)
        return;
    m_mode = mode;
    Q_EMIT modeChanged(mode);
}
