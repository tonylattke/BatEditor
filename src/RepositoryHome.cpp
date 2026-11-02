#include "RepositoryHome.h"

#include "GitRepository.h"
#include "Theme.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h> // CREATE_NO_WINDOW
#endif

namespace {

const char *kRecentSetting = "git/recentRepositories";
const char *kCloneDestinationSetting = "git/cloneDestination";
constexpr int kMaxRecent = 10;

QLabel *sectionTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont f = label->font();
    f.setPointSizeF(f.pointSizeF() * 1.35);
    label->setFont(f);
    return label;
}

// "https://github.com/user/proyecto.git" → "proyecto"
QString repositoryNameFromUrl(const QString &url)
{
    QString s = url.trimmed();
    while (s.endsWith(QLatin1Char('/')) || s.endsWith(QLatin1Char('\\')))
        s.chop(1);
    if (s.endsWith(QLatin1String(".git"), Qt::CaseInsensitive))
        s.chop(4);
    const int cut = s.lastIndexOf(QRegularExpression(QStringLiteral("[/\\\\:]")));
    return cut >= 0 ? s.mid(cut + 1) : s;
}

bool samePath(const QString &a, const QString &b)
{
#ifdef Q_OS_WIN
    return QDir::cleanPath(a).compare(QDir::cleanPath(b), Qt::CaseInsensitive) == 0;
#else
    return QDir::cleanPath(a) == QDir::cleanPath(b);
#endif
}

} // namespace

RepositoryHome::RepositoryHome(QWidget *parent)
    : QWidget(parent)
    , m_back(new QPushButton(tr("← Volver"), this))
    , m_recentLayout(new QVBoxLayout)
    , m_url(new QLineEdit(this))
    , m_name(new QLineEdit(this))
    , m_destination(new QLineEdit(this))
    , m_browse(new QPushButton(QStringLiteral("…"), this))
    , m_clone(new QPushButton(tr("Clonar"), this))
    , m_cloneStatus(new QLabel(this))
{
    auto *content = new QWidget(this);
    content->setMaximumWidth(640);

    // --- Repositorios locales ---
    auto *open = new QPushButton(tr("Abrir repositorio"), content);
    auto *create = new QPushButton(tr("Nuevo repositorio"), content);
    connect(open, &QPushButton::clicked, this, &RepositoryHome::browseRequested);
    connect(create, &QPushButton::clicked, this, &RepositoryHome::createRepository);
    auto *localButtons = new QHBoxLayout;
    localButtons->addWidget(open);
    localButtons->addWidget(create);

    // --- Recientes ---
    m_recentLayout->setSpacing(2);

    // --- Clonar ---
    m_url->setPlaceholderText(QStringLiteral("https://github.com/usuario/proyecto.git"));
    m_destination->setText(QDir::toNativeSeparators(
        QSettings().value(QLatin1String(kCloneDestinationSetting), QDir::homePath()).toString()));
    // El nombre sigue a la URL mientras el usuario no lo cambie
    connect(m_url, &QLineEdit::textChanged, this, [this](const QString &url) {
        if (!m_name->isModified())
            m_name->setText(repositoryNameFromUrl(url));
        updateCloneButton();
    });
    connect(m_name, &QLineEdit::textChanged, this, &RepositoryHome::updateCloneButton);
    connect(m_destination, &QLineEdit::textChanged, this, &RepositoryHome::updateCloneButton);
    connect(m_url, &QLineEdit::returnPressed, this, &RepositoryHome::clone);
    connect(m_name, &QLineEdit::returnPressed, this, &RepositoryHome::clone);
    connect(m_browse, &QPushButton::clicked, this, &RepositoryHome::browseDestination);
    connect(m_clone, &QPushButton::clicked, this, &RepositoryHome::clone);
    m_clone->setObjectName(QStringLiteral("primary"));
    m_browse->setMinimumWidth(110);
    m_clone->setMinimumWidth(110);
    m_cloneStatus->setWordWrap(true);
    m_cloneStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_cloneStatus->hide();

    auto *form = new QGridLayout;
    form->addWidget(new QLabel(tr("URL de origen"), content), 0, 0);
    form->addWidget(m_url, 0, 1, 1, 2);
    form->addWidget(new QLabel(tr("Nombre del repositorio"), content), 1, 0);
    form->addWidget(m_name, 1, 1, 1, 2);
    form->addWidget(new QLabel(tr("Carpeta de destino"), content), 2, 0);
    form->addWidget(m_destination, 2, 1);
    form->addWidget(m_browse, 2, 2);
    form->addWidget(m_clone, 3, 2);
    form->addWidget(m_cloneStatus, 4, 0, 1, 3);
    form->setColumnStretch(1, 1);

    auto *cl = new QVBoxLayout(content);
    cl->addWidget(sectionTitle(tr("Repositorios locales"), content));
    cl->addLayout(localButtons);
    cl->addSpacing(24);
    cl->addWidget(sectionTitle(tr("Repositorios recientes"), content));
    cl->addLayout(m_recentLayout);
    cl->addSpacing(24);
    cl->addWidget(sectionTitle(tr("Clonar repositorio"), content));
    cl->addLayout(form);
    cl->addStretch(1);

    auto *row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(content, 3);
    row->addStretch(1);

    connect(m_back, &QPushButton::clicked, this, &RepositoryHome::backRequested);
    auto *top = new QHBoxLayout;
    top->addWidget(m_back);
    top->addStretch(1);

    auto *inner = new QWidget;
    auto *il = new QVBoxLayout(inner);
    il->addLayout(top);
    il->addSpacing(12);
    il->addLayout(row);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(inner);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scroll);

    reloadRecent();
    updateCloneButton();
}

QStringList RepositoryHome::recentRepositories()
{
    return QSettings().value(QLatin1String(kRecentSetting)).toStringList();
}

void RepositoryHome::addRecentRepository(const QString &path)
{
    QStringList recent = recentRepositories();
    recent.erase(std::remove_if(recent.begin(), recent.end(),
                                [&](const QString &p) { return samePath(p, path); }),
                 recent.end());
    recent.prepend(QDir::cleanPath(path));
    while (recent.size() > kMaxRecent)
        recent.removeLast();
    QSettings().setValue(QLatin1String(kRecentSetting), recent);
}

void RepositoryHome::setBackVisible(bool visible)
{
    m_back->setVisible(visible);
}

void RepositoryHome::reloadRecent()
{
    while (QLayoutItem *item = m_recentLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const QIcon folder = style()->standardIcon(QStyle::SP_DirIcon);
    int shown = 0;
    for (const QString &path : recentRepositories()) {
        if (!QFileInfo::exists(path))
            continue;
        auto *item = new QToolButton(this);
        item->setIcon(folder);
        item->setText(QDir::toNativeSeparators(path));
        item->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        item->setAutoRaise(true);
        item->setCursor(Qt::PointingHandCursor);
        connect(item, &QToolButton::clicked, this, [this, path] { Q_EMIT openRequested(path); });
        m_recentLayout->addWidget(item, 0, Qt::AlignLeft);
        ++shown;
    }
    if (!shown) {
        auto *none = new QLabel(tr("Todavía no has abierto ningún repositorio."), this);
        QPalette p = none->palette();
        p.setColor(QPalette::WindowText, Theme::colors().muted);
        none->setPalette(p);
        m_recentLayout->addWidget(none);
    }
}

void RepositoryHome::createRepository()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Nuevo repositorio"), m_destination->text());
    if (dir.isEmpty())
        return;
    GitRepository existing;
    if (existing.open(dir) && samePath(existing.workdir(), dir)) {
        Q_EMIT openRequested(dir);
        return;
    }
    QString error;
    if (!GitRepository::init(dir, &error)) {
        QMessageBox::warning(this, tr("Nuevo repositorio"), error);
        return;
    }
    Q_EMIT openRequested(dir);
}

void RepositoryHome::browseDestination()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Carpeta de destino"), m_destination->text());
    if (!dir.isEmpty())
        m_destination->setText(QDir::toNativeSeparators(dir));
}

void RepositoryHome::updateCloneButton()
{
    m_clone->setEnabled(!m_process && !m_url->text().trimmed().isEmpty() && !m_name->text().trimmed().isEmpty()
                        && !m_destination->text().trimmed().isEmpty());
}

void RepositoryHome::setCloning(bool cloning)
{
    m_url->setEnabled(!cloning);
    m_name->setEnabled(!cloning);
    m_destination->setEnabled(!cloning);
    m_browse->setEnabled(!cloning);
    updateCloneButton();
}

void RepositoryHome::clone()
{
    if (m_process || !m_clone->isEnabled())
        return;
    const QString url = m_url->text().trimmed();
    const QString destination = QDir::fromNativeSeparators(m_destination->text().trimmed());
    const QString target = QDir(destination).filePath(m_name->text().trimmed());
    if (QFileInfo::exists(target) && !QDir(target).isEmpty()) {
        QMessageBox::warning(this, tr("Clonar repositorio"),
                             tr("La carpeta %1 ya existe y no está vacía.").arg(QDir::toNativeSeparators(target)));
        return;
    }
    const QString git = GitRepository::gitExecutable();
    if (git.isEmpty()) {
        QMessageBox::warning(this, tr("Git"),
                             tr("No se encontró el programa git.\n\n"
                                "Para clonar se usa tu instalación de Git y sus credenciales. "
                                "Instálalo desde https://git-scm.com y reinicia BatCode."));
        return;
    }
    if (!QDir().mkpath(destination)) {
        QMessageBox::warning(this, tr("Clonar repositorio"),
                             tr("No se pudo crear la carpeta %1.").arg(QDir::toNativeSeparators(destination)));
        return;
    }
    QSettings().setValue(QLatin1String(kCloneDestinationSetting), destination);

    m_cloneOutput.clear();
    m_process = new QProcess(this);
    m_process->setProgram(git);
    m_process->setArguments({QStringLiteral("clone"), QStringLiteral("--progress"), url, target});
    m_process->setWorkingDirectory(destination);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0")); // nunca esperar una contraseña en consola
    m_process->setProcessEnvironment(env);
#ifdef Q_OS_WIN
    m_process->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *a) { a->flags |= CREATE_NO_WINDOW; });
#endif

    auto showStatus = [this](const QString &text, const QColor &color) {
        QPalette p = m_cloneStatus->palette();
        p.setColor(QPalette::WindowText, color);
        m_cloneStatus->setPalette(p);
        m_cloneStatus->setText(text);
        m_cloneStatus->show();
    };
    showStatus(tr("Clonando…"), Theme::colors().muted);

    connect(m_process, &QProcess::readyRead, this, [this, showStatus] {
        m_cloneOutput += QString::fromUtf8(m_process->readAll());
        const QStringList lines = m_cloneOutput.split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::SkipEmptyParts);
        if (!lines.isEmpty())
            showStatus(lines.last().trimmed(), Theme::colors().muted);
    });
    auto finish = [this, target, showStatus](bool ok) {
        if (!m_process)
            return;
        m_cloneOutput += QString::fromUtf8(m_process->readAll());
        m_process->deleteLater();
        m_process = nullptr;
        setCloning(false);
        if (ok) {
            m_cloneStatus->hide();
            m_url->clear();
            m_name->clear();
            m_name->setModified(false);
            Q_EMIT openRequested(target);
            return;
        }
        // Las líneas de error de git empiezan por "fatal:" o "error:"
        QString message;
        for (const QString &l : m_cloneOutput.split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::SkipEmptyParts))
            if (l.startsWith(QLatin1String("fatal:")) || l.startsWith(QLatin1String("error:")))
                message += l.trimmed() + QLatin1Char('\n');
        if (message.isEmpty())
            message = m_cloneOutput.trimmed().section(QLatin1Char('\n'), -1);
        showStatus(tr("No se pudo clonar: %1").arg(message.trimmed()), Theme::colors().removed);
    };
    connect(m_process, &QProcess::finished, this, [finish](int code, QProcess::ExitStatus status) {
        finish(status == QProcess::NormalExit && code == 0);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this, finish](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !m_process)
            return;
        m_cloneOutput += tr("No se pudo ejecutar git: %1\n").arg(m_process->errorString());
        finish(false);
    });

    setCloning(true);
    m_process->start();
}
