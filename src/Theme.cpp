#include "Theme.h"

#include <QApplication>
#include <QCursor>
#include <QFontDatabase>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyleFactory>
#include <QMouseEvent>
#include <QTimer>
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

// En X11 con monitores a distinta altura, Qt puede tomar como área útil de cada pantalla la
// de _NET_WORKAREA (una sola para todo el escritorio) y baja los menús hasta su borde superior,
// lejos de la barra de menús. Si un menú quedó justo en ese borde falso, se sube a donde toca:
// bajo su título en la barra, junto a su opción en un submenú o en el puntero en un menú contextual.
class MenuPositionFixer : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::Show)
            return false;
        auto *menu = qobject_cast<QMenu *>(watched);
        if (!menu || !menu->isWindow())
            return false;
        QScreen *screen = QGuiApplication::screenAt(menu->geometry().center());
        if (!screen)
            return false;
        const QRect full = screen->geometry();
        const QRect available = screen->availableGeometry();
        const bool clamped = available.top() > full.top() && menu->y() == available.top();

        int wantedY;
        QWidget *parent = menu->parentWidget();
        if (auto *bar = qobject_cast<QMenuBar *>(parent); bar && bar->activeAction() == menu->menuAction()) {
            // Los de la barra siempre se abren justo debajo de su título
            wantedY = bar->mapToGlobal(bar->actionGeometry(menu->menuAction()).bottomLeft()).y() + 1;
        } else if (!clamped) {
            return false;
        } else if (auto *parentMenu = qobject_cast<QMenu *>(parent); parentMenu && parentMenu->activeAction() == menu->menuAction()) {
            wantedY = parentMenu->mapToGlobal(parentMenu->actionGeometry(menu->menuAction()).topLeft()).y();
        } else {
            wantedY = QCursor::pos().y();
        }
        if (wantedY >= menu->y() || wantedY < full.top() || wantedY + menu->height() > full.bottom() + 1)
            return false;
        menu->move(menu->x(), wantedY);
        return false;
    }
};

// Ajusta cada QMessageBox justo antes de mostrarse:
//  - el botón de confirmar queda por defecto y con el foco, para aceptar con Enter
//    (si el código eligió ya otro botón de confirmar, se respeta);
//  - un texto más alto que media pantalla pasa a un panel con scroll, para que los
//    botones sigan visibles.
class MessageBoxPolisher : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Show) {
            auto *box = qobject_cast<QMessageBox *>(watched);
            if (box && !box->property("batcodePolished").toBool()) {
                box->setProperty("batcodePolished", true);
                limitTextHeight(box, QStringLiteral("qt_msgbox_label"));
                limitTextHeight(box, QStringLiteral("qt_msgbox_informativelabel"));
                focusConfirmButton(box);
            }
        }
        return false;
    }

private:
    static bool isConfirmRole(QMessageBox::ButtonRole role)
    {
        return role == QMessageBox::AcceptRole || role == QMessageBox::YesRole || role == QMessageBox::DestructiveRole;
    }

    static void focusConfirmButton(QMessageBox *box)
    {
        QPushButton *button = box->defaultButton();
        if (!button || !isConfirmRole(box->buttonRole(button))) {
            button = nullptr;
            for (QMessageBox::ButtonRole role : {QMessageBox::AcceptRole, QMessageBox::YesRole, QMessageBox::DestructiveRole}) {
                for (QAbstractButton *b : box->buttons())
                    if (box->buttonRole(b) == role && !button)
                        button = qobject_cast<QPushButton *>(b);
            }
        }
        if (!button)
            return;
        box->setDefaultButton(button);
        // QDialog reparte el foco al terminar de mostrarse: se vuelve a pedir después
        QTimer::singleShot(0, button, [button] { button->setFocus(Qt::OtherFocusReason); });
    }

    // QMessageBox calcula su tamaño fijo a partir de su propia etiqueta, así que no se
    // mueve: se oculta y se pone una copia dentro de un QScrollArea con tamaño mínimo explícito.
    static void limitTextHeight(QMessageBox *box, const QString &labelName)
    {
        auto *label = box->findChild<QLabel *>(labelName, Qt::FindDirectChildrenOnly);
        auto *grid = qobject_cast<QGridLayout *>(box->layout());
        if (!label || !grid || label->isHidden() || label->text().isEmpty())
            return;
        const int index = grid->indexOf(label);
        if (index < 0)
            return;
        const QRect screen = box->screen()->availableGeometry();
        const int maxHeight = screen.height() / 2;

        auto *copy = new QLabel;
        copy->setTextFormat(label->textFormat());
        copy->setText(label->text());
        copy->setTextInteractionFlags(label->textInteractionFlags());
        copy->setOpenExternalLinks(label->openExternalLinks());
        copy->setContentsMargins(label->contentsMargins());
        const int naturalWidth = copy->sizeHint().width();
        copy->setWordWrap(true);
        const int width = qBound(360, naturalWidth, qMin(700, screen.width() / 2));
        const int height = copy->heightForWidth(width);
        if (height <= maxHeight) {
            delete copy;
            return;
        }

        auto *scroll = new QScrollArea(box);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setWidget(copy);
        scroll->setMinimumSize(width + scroll->verticalScrollBar()->sizeHint().width(), maxHeight);
        int row, column, rowSpan, columnSpan;
        grid->getItemPosition(index, &row, &column, &rowSpan, &columnSpan);
        label->hide();
        grid->addWidget(scroll, row, column, rowSpan, columnSpan);
        scroll->show();
    }
};
} // namespace

void enableMiddleClickClose(QTabWidget *tabs)
{
    tabs->tabBar()->installEventFilter(new MiddleClickCloser(tabs));
}

void enableTabContextMenu(QTabWidget *tabs, std::function<void(QMenu *, int)> extra)
{
    QTabBar *bar = tabs->tabBar();
    bar->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(bar, &QWidget::customContextMenuRequested, tabs, [tabs, bar, extra](const QPoint &pos) {
        const int index = bar->tabAt(pos);
        if (index < 0)
            return;
        const int count = tabs->count();
        auto closeRange = [tabs](int from, int to, int keep) {
            QList<QPointer<QWidget>> widgets;
            for (int i = from; i < to; ++i)
                if (i != keep)
                    widgets << tabs->widget(i);
            for (const QPointer<QWidget> &w : std::as_const(widgets)) {
                const int i = w ? tabs->indexOf(w) : -1;
                if (i < 0)
                    continue;
                Q_EMIT tabs->tabCloseRequested(i);
                if (w && tabs->indexOf(w) >= 0)
                    return;
            }
        };
        QMenu menu(tabs);
        menu.addAction(QCoreApplication::translate("Theme", "Cerrar pestaña"), tabs,
                       [closeRange, index] { closeRange(index, index + 1, -1); });
        menu.addAction(QCoreApplication::translate("Theme", "Cerrar las demás pestañas"), tabs,
                       [closeRange, index, count] { closeRange(0, count, index); })
            ->setEnabled(count > 1);
        menu.addAction(QCoreApplication::translate("Theme", "Cerrar las pestañas de la derecha"), tabs,
                       [closeRange, index, count] { closeRange(index + 1, count, -1); })
            ->setEnabled(index < count - 1);
        menu.addAction(QCoreApplication::translate("Theme", "Cerrar las pestañas de la izquierda"), tabs,
                       [closeRange, index] { closeRange(0, index, -1); })
            ->setEnabled(index > 0);
        menu.addAction(QCoreApplication::translate("Theme", "Cerrar todas las pestañas"), tabs,
                       [closeRange, count] { closeRange(0, count, -1); });
        if (extra)
            extra(&menu, index);
        menu.exec(bar->mapToGlobal(pos));
    });
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
    app.installEventFilter(new MessageBoxPolisher(&app));
    app.installEventFilter(new MenuPositionFixer(&app));
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
        QPushButton:hover, QToolButton:hover, QPushButton:focus { border-color: %6; }
        QPushButton:pressed, QToolButton:checked { background: %7; }
        QPushButton#primary { background: %6; color: #1a1c22; font-weight: bold; border: none; }
        QPushButton#primary:disabled { background: %1; color: %3; }
        QStatusBar { background: %2; color: %3; }
        QStatusBar QLabel { color: %3; padding: 0px 6px; }
        QStatusBar QToolButton#languageButton { background: transparent; border: none; color: %3; padding: 0px 6px; }
        QStatusBar QToolButton#languageButton:hover { color: %5; }
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
