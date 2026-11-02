#include "GitWorkspace.h"

#include "RepositoryHome.h"
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
    , m_home(new RepositoryHome(this))
    , m_tabs(new QTabWidget(this))
{
    // --- Página de inicio (sin repositorios, o con "+") ---
    connect(m_home, &RepositoryHome::browseRequested, this, &GitWorkspace::promptOpenRepository);
    connect(m_home, &RepositoryHome::openRequested, this, [this](const QString &path) {
        QString error;
        if (!openRepository(path, true, &error))
            QMessageBox::warning(this, tr("Abrir repositorio"),
                                 tr("%1 no es un repositorio Git.\n\n%2").arg(QDir::toNativeSeparators(path), error));
    });
    connect(m_home, &RepositoryHome::backRequested, this, [this] { m_stack->setCurrentWidget(m_tabs); });

    // --- Pestañas de repositorios ---
    m_tabs->setTabsClosable(true);
    Theme::enableMiddleClickClose(m_tabs);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    m_tabs->tabBar()->setExpanding(false);
    auto *add = new QToolButton(m_tabs);
    add->setText(QStringLiteral("+"));
    add->setToolTip(tr("Abrir o clonar un repositorio…"));
    add->setAutoRaise(true);
    connect(add, &QToolButton::clicked, this, &GitWorkspace::showHome);
    m_tabs->setCornerWidget(add, Qt::TopRightCorner);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int i) {
        QWidget *w = m_tabs->widget(i);
        m_tabs->removeTab(i);
        w->deleteLater();
        if (m_tabs->count())
            m_stack->setCurrentWidget(m_tabs);
        else
            showHome();
        Q_EMIT repositoriesChanged();
    });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        refreshCurrent();
        Q_EMIT repositoriesChanged();
    });

    m_home->setBackVisible(false);
    m_stack->addWidget(m_home);
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
    RepositoryHome::addRecentRepository(view->path());
    if (existing >= 0) {
        view->deleteLater();
        m_stack->setCurrentWidget(m_tabs);
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

void GitWorkspace::showHome()
{
    m_home->reloadRecent();
    m_home->setBackVisible(m_tabs->count() > 0);
    m_stack->setCurrentWidget(m_home);
}
