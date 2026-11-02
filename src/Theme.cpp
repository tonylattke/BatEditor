#include "Theme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleFactory>
#include <QMouseEvent>
#include <QTabBar>
#include <QTabWidget>

namespace Theme {

namespace {
// Filtro sobre la barra de pestañas: cierra la pestaña si se pulsa y se suelta
// el botón central sobre ella (si se arrastra a otra, no hace nada).
class MiddleClickCloser : public QObject
{
public:
    MiddleClickCloser(QTabWidget *tabs) : QObject(tabs), m_tabs(tabs) {}

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        auto *bar = qobject_cast<QTabBar *>(watched);
        if (!bar)
            return false;
        if (event->type() == QEvent::MouseButtonPress) {
            auto *e = static_cast<QMouseEvent *>(event);
            if (e->button() == Qt::MiddleButton) {
                m_pressed = bar->tabAt(e->position().toPoint());
                return m_pressed >= 0;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto *e = static_cast<QMouseEvent *>(event);
            if (e->button() == Qt::MiddleButton) {
                const int index = bar->tabAt(e->position().toPoint());
                const bool hit = index >= 0 && index == m_pressed;
                m_pressed = -1;
                if (hit && m_tabs->tabsClosable())
                    Q_EMIT m_tabs->tabCloseRequested(index);
                return hit;
            }
        }
        return false;
    }

private:
    QTabWidget *m_tabs;
    int m_pressed = -1;
};
} // namespace

void enableMiddleClickClose(QTabWidget *tabs)
{
    tabs->tabBar()->installEventFilter(new MiddleClickCloser(tabs));
}

const Colors &colors()
{
    static const Colors c;
    return c;
}

QFont editorFont(int pointSize)
{
    // Las mismas fuentes que usa Sublime Text por defecto en cada sistema:
    // Consolas (Windows), Menlo (macOS) y DejaVu Sans Mono / "Monospace" (Linux).
    static const QString family = [] {
#if defined(Q_OS_WIN)
        const QStringList preferred = {QStringLiteral("Consolas"), QStringLiteral("Cascadia Mono"),
                                       QStringLiteral("Courier New")};
#elif defined(Q_OS_MACOS)
        const QStringList preferred = {QStringLiteral("Menlo"), QStringLiteral("Monaco")};
#else
        const QStringList preferred = {QStringLiteral("DejaVu Sans Mono"), QStringLiteral("Monospace"),
                                       QStringLiteral("Liberation Mono"), QStringLiteral("Ubuntu Mono")};
#endif
        const QStringList families = QFontDatabase::families();
        for (const QString &name : preferred)
            if (families.contains(name))
                return name;
        return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    }();
    QFont f(family, pointSize);
    f.setStyleHint(QFont::Monospace);
    f.setFixedPitch(true);
    return f;
}

QFont uiFont()
{
#if defined(Q_OS_WIN)
    // Fuente de interfaz de Windows (la que usan las barras laterales de Sublime)
    QFont f(QStringLiteral("Segoe UI"), 9);
    return f;
#else
    return QApplication::font();
#endif
}

void apply(QApplication &app)
{
    const Colors &c = colors();
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setFont(uiFont());

    QPalette p;
    p.setColor(QPalette::Window, c.panel);
    p.setColor(QPalette::WindowText, c.foreground);
    p.setColor(QPalette::Base, c.background);
    p.setColor(QPalette::AlternateBase, c.backgroundAlt);
    p.setColor(QPalette::Text, c.foreground);
    p.setColor(QPalette::Button, c.panel);
    p.setColor(QPalette::ButtonText, c.foreground);
    p.setColor(QPalette::Highlight, c.selection);
    p.setColor(QPalette::HighlightedText, c.foreground);
    p.setColor(QPalette::ToolTipBase, c.backgroundAlt);
    p.setColor(QPalette::ToolTipText, c.foreground);
    p.setColor(QPalette::PlaceholderText, c.muted);
    p.setColor(QPalette::Link, c.function);
    p.setColor(QPalette::Disabled, QPalette::Text, c.muted);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, c.muted);
    p.setColor(QPalette::Disabled, QPalette::WindowText, c.muted);
    app.setPalette(p);

    app.setStyleSheet(QStringLiteral(R"(
        QMainWindow::separator { background: %1; width: 1px; height: 1px; }
        QDockWidget { titlebar-close-icon: none; }
        QDockWidget::title { background: %2; padding: 6px 8px; color: %3; }
        QTabWidget::pane { border: none; }
        QTabBar::tab { background: %2; color: %3; padding: 6px 14px; border: none;
                       border-right: 1px solid %1; }
        QTabBar::tab:selected { background: %4; color: %5; border-top: 2px solid %6; }
        QTabBar::tab:hover:!selected { color: %5; }
        QTreeView, QListWidget, QTreeWidget { background: %2; border: none; outline: 0; }
        QTreeView::item, QListWidget::item, QTreeWidget::item { padding: 2px 0px; }
        QTreeView::item:selected, QListWidget::item:selected, QTreeWidget::item:selected {
            background: %7; color: %5; }
        QLineEdit, QPlainTextEdit#commitMessage { background: %4; border: 1px solid %1;
            border-radius: 3px; padding: 4px; color: %5; selection-background-color: %7; }
        QLineEdit:focus, QPlainTextEdit#commitMessage:focus { border: 1px solid %6; }
        QPushButton, QToolButton { background: %8; border: 1px solid %1; border-radius: 3px;
            padding: 4px 10px; color: %5; }
        QPushButton:hover, QToolButton:hover { border-color: %6; }
        QPushButton:pressed, QToolButton:checked { background: %7; }
        QPushButton#primary { background: %6; color: #1a1c22; font-weight: bold; border: none; }
        QPushButton#primary:disabled { background: %1; color: %3; }
        QStatusBar { background: %2; color: %3; }
        QStatusBar QLabel { color: %3; padding: 0px 6px; }
        QMenuBar { background: %2; color: %5; }
        QMenuBar::item:selected { background: %7; }
        QMenu { background: %8; color: %5; border: 1px solid %1; }
        QMenu::item:selected { background: %7; }
        QScrollBar:vertical { background: transparent; width: 12px; }
        QScrollBar:horizontal { background: transparent; height: 12px; }
        QScrollBar::handle { background: #3a3f4b; border-radius: 4px; min-height: 24px; min-width: 24px; margin: 2px; }
        QScrollBar::handle:hover { background: #4a505e; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: none; }
        QLabel#sectionHeader { color: %3; font-weight: bold; padding: 6px 4px 2px 4px; }
        QSplitter::handle { background: %1; }
        QLabel#sidebarHeader { color: %3; font-weight: bold; padding: 10px 14px 6px 14px; background: %2; }
        QLabel#sidebarEmpty { color: %3; background: %2; }
        QLabel#paneTitle { color: %5; padding: 7px; background: %2; border-bottom: 1px solid %1; }
        QWidget#sidebar { background: %2; }
        QGroupBox { border: 1px solid %1; border-radius: 4px; margin-top: 14px; padding: 12px 10px 8px 10px; }
        QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0px 4px; color: %3; font-weight: bold; }
        QComboBox { background: %4; border: 1px solid %1; border-radius: 3px; padding: 3px 6px; color: %5; }
        QComboBox:focus { border-color: %6; }
        QComboBox QAbstractItemView { background: %8; color: %5; selection-background-color: %7; }
    )")
        .arg(c.border.name(), c.backgroundAlt.name(), c.muted.name(), c.background.name(),
             c.foreground.name(), c.accent.name(), c.selection.name(), c.panel.name()));
}

} // namespace Theme
