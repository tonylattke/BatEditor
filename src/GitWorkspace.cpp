#include "GitWorkspace.h"

#include "RepositoryView.h"
#include "Theme.h"

#include <QDir>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

bool samePath(const QString &a, const QString &b)
{
#ifdef Q_OS_WIN
    return QDir::cleanPath(a).compare(QDir::cleanPath(b), Qt::CaseInsensitive) == 0;
#else
    return QDir::cleanPath(a) == QDir::cleanPath(b);
#endif
}

bool isInside(const QString &file, const QString &dir)
{
    const QString f = QDir::cleanPath(file);
    const QString d = QDir::cleanPath(dir) + QLatin1Char('/');
#ifdef Q_OS_WIN
    return f.startsWith(d, Qt::CaseInsensitive);
#else
    return f.startsWith(d);
#endif
}

} // namespace

GitWorkspace::GitWorkspace(QWidget *parent)
    : QWidget(parent)
    , m_stack(new QStackedWidget(this))
    , m_empty(new QWidget(this))
    , m_tabs(new QTabWidget(this))
{
    // --- Estado vacío ---
    auto *title = new QLabel(tr("Ningún repositorio abierto"), m_empty);
    QFont f = title->font();
    f.setPointSizeF(f.pointSizeF() * 1.4);
    title->setFont(f);
    title->setAlignment(Qt::AlignCenter);
    auto *hint = new QLabel(tr("Abre la carpeta de un repositorio Git para ver sus ramas, commits y cambios."), m_empty);
    hint->setAlignment(Qt::AlignCenter);
    {
        QPalette p = hint->palette();
        p.setColor(QPalette::WindowText, Theme::colors().muted);
        hint->setPalette(p);
    }
    auto *open = new QPushButton(tr("Abrir repositorio…"), m_empty);
    open->setObjectName(QStringLiteral("primary"));
    connect(open, &QPushButton::clicked, this, &GitWorkspace::promptOpenRepository);
    auto *el = new QVBoxLayout(m_empty);
    el->addStretch(1);
    el->addWidget(title);
    el->addWidget(hint);
    el->addSpacing(12);
    el->addWidget(open, 0, Qt::AlignHCenter);
    el->addStretch(2);

    // --- Pestañas de repositorios ---
    m_tabs->setTabsClosable(true);
    Theme::enableMiddleClickClose(m_tabs);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    m_tabs->tabBar()->setExpanding(false);
    auto *add = new QToolButton(m_tabs);
    add->setText(QStringLiteral("+"));
    add->setToolTip(tr("Abrir repositorio…"));
    add->setAutoRaise(true);
    connect(add, &QToolButton::clicked, this, &GitWorkspace::promptOpenRepository);
    m_tabs->setCornerWidget(add, Qt::TopRightCorner);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int i) {
        QWidget *w = m_tabs->widget(i);
        m_tabs->removeTab(i);
        w->deleteLater();
        m_stack->setCurrentWidget(m_tabs->count() ? static_cast<QWidget *>(m_tabs) : m_empty);
        Q_EMIT repositoriesChanged();
    });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        refreshCurrent();
        Q_EMIT repositoriesChanged();
    });

    m_stack->addWidget(m_empty);
    m_stack->addWidget(m_tabs);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_stack);
}

RepositoryView *GitWorkspace::viewAt(int index) const
{
    return qobject_cast<RepositoryView *>(m_tabs->widget(index));
}

int GitWorkspace::indexOfPath(const QString &workdir) const
{
    for (int i = 0; i < m_tabs->count(); ++i)
        if (RepositoryView *v = viewAt(i); v && samePath(v->path(), workdir))
            return i;
    return -1;
}

bool GitWorkspace::hasRepositoryFor(const QString &path) const
{
    for (int i = 0; i < m_tabs->count(); ++i)
        if (RepositoryView *v = viewAt(i); v && (samePath(v->path(), path) || isInside(path, v->path())))
            return true;
    return false;
}

bool GitWorkspace::openRepository(const QString &path, bool activate, QString *error)
{
    auto *view = new RepositoryView(m_tabs);
    if (!view->open(path, error)) {
        view->deleteLater();
        return false;
    }
    const int existing = indexOfPath(view->path());
    if (existing >= 0) {
        view->deleteLater();
        if (activate)
            m_tabs->setCurrentIndex(existing);
        return true;
    }
    connect(view, &RepositoryView::openFileRequested, this, &GitWorkspace::openFileRequested);
    connect(view, &RepositoryView::openFolderRequested, this, &GitWorkspace::openFolderRequested);
    connect(view, &RepositoryView::statusMessage, this, &GitWorkspace::statusMessage);

    const QSignalBlocker blocker(m_tabs);
    const int index = m_tabs->addTab(view, view->title());
    m_tabs->setTabToolTip(index, view->path());
    if (activate || m_tabs->count() == 1)
        m_tabs->setCurrentIndex(index);
    m_stack->setCurrentWidget(m_tabs);
    Q_EMIT repositoriesChanged();
    return true;
}

QStringList GitWorkspace::repositoryPaths() const
{
    QStringList paths;
    for (int i = 0; i < m_tabs->count(); ++i)
        if (RepositoryView *v = viewAt(i))
            paths << v->path();
    return paths;
}

QString GitWorkspace::currentRepositoryPath() const
{
    RepositoryView *v = viewAt(m_tabs->currentIndex());
    return v ? v->path() : QString();
}

void GitWorkspace::setCurrentRepository(const QString &path)
{
    const int i = indexOfPath(path);
    if (i >= 0)
        m_tabs->setCurrentIndex(i);
}

void GitWorkspace::closeCurrent()
{
    if (m_tabs->currentIndex() >= 0)
        Q_EMIT m_tabs->tabCloseRequested(m_tabs->currentIndex());
}

void GitWorkspace::refreshCurrent()
{
    if (RepositoryView *v = viewAt(m_tabs->currentIndex()))
        v->reload();
}

void GitWorkspace::refreshRepositoryContaining(const QString &filePath)
{
    // Solo el visible: los demás se actualizan al seleccionarlos
    RepositoryView *v = viewAt(m_tabs->currentIndex());
    if (v && isVisible() && isInside(filePath, v->path()))
        v->reload();
}

void GitWorkspace::promptOpenRepository()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Abrir repositorio"), currentRepositoryPath());
    if (dir.isEmpty())
        return;
    QString error;
    if (!openRepository(dir, true, &error))
        QMessageBox::warning(this, tr("Abrir repositorio"),
                             tr("%1 no es un repositorio Git.\n\n%2").arg(QDir::toNativeSeparators(dir), error));
}

RepositoryView *GitWorkspace::currentView() const
{
    return viewAt(m_tabs->currentIndex());
}
