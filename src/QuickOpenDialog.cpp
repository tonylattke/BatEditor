#include "QuickOpenDialog.h"

#include <QDir>
#include <QDirIterator>
#include <QKeyEvent>
#include <QLineEdit>
#include <QTimer>
#include <QListWidget>
#include <QSet>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int kMaxFiles = 50000;
constexpr int kMaxResults = 200;

bool isIgnoredDir(const QString &name)
{
    static const QSet<QString> ignored = {
        QStringLiteral(".git"), QStringLiteral(".svn"), QStringLiteral(".hg"),
        QStringLiteral("node_modules"), QStringLiteral("build"), QStringLiteral("out"),
        QStringLiteral(".vs"), QStringLiteral(".idea"), QStringLiteral("__pycache__"),
        QStringLiteral("vcpkg_installed"), QStringLiteral("target"), QStringLiteral(".cache"),
    };
    return ignored.contains(name);
}

} // namespace

QuickOpenDialog::QuickOpenDialog(const QString &rootPath, QWidget *parent)
    : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint)
    , m_root(rootPath)
    , m_input(new QLineEdit(this))
    , m_list(new QListWidget(this))
{
    setMinimumSize(620, 380);
    m_input->setPlaceholderText(tr("Ir a archivo…"));
    m_input->installEventFilter(this);
    // El campo de búsqueda es el único que recibe el teclado: ↑/↓/Enter se
    // reenvían a la lista desde eventFilter, así se puede escribir de inmediato.
    m_list->setFocusPolicy(Qt::NoFocus);
    setFocusProxy(m_input);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(m_input);
    layout->addWidget(m_list, 1);

    connect(m_input, &QLineEdit::textChanged, this, &QuickOpenDialog::refilter);
    connect(m_list, &QListWidget::itemActivated, this, [this] { accept(); });

    scanFiles();
    refilter();
}

// En Windows una ventana Qt::Popup no siempre se activa sola: se fuerza la
// activación y el foco en el campo de búsqueda al mostrarse (y otra vez cuando
// el sistema termina de mostrarla).
void QuickOpenDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    auto focusInput = [this] {
        raise();
        activateWindow();
        m_input->setFocus(Qt::PopupFocusReason);
        m_input->selectAll();
    };
    focusInput();
    QTimer::singleShot(0, this, focusInput);
}

void QuickOpenDialog::scanFiles()
{
    const QDir root(m_root);
    QList<QString> pending = {m_root};
    while (!pending.isEmpty() && m_files.size() < kMaxFiles) {
        const QString dir = pending.takeFirst();
        QDirIterator it(dir, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
        while (it.hasNext() && m_files.size() < kMaxFiles) {
            it.next();
            const QFileInfo info = it.fileInfo();
            if (info.isDir()) {
                if (!isIgnoredDir(info.fileName()) && !info.isSymLink())
                    pending << info.filePath();
            } else {
                m_files << root.relativeFilePath(info.filePath());
            }
        }
    }
    std::sort(m_files.begin(), m_files.end());
}

int QuickOpenDialog::fuzzyScore(const QString &pattern, const QString &candidate)
{
    if (pattern.isEmpty())
        return 0;
    // Cada carácter del patrón debe aparecer en orden. Se premian coincidencias
    // consecutivas, al inicio de palabra y dentro del nombre del archivo.
    const int nameStart = candidate.lastIndexOf(QLatin1Char('/')) + 1;
    int score = 0;
    int pi = 0;
    int lastMatch = -2;
    for (int ci = 0; ci < candidate.size() && pi < pattern.size(); ++ci) {
        if (candidate.at(ci).toLower() != pattern.at(pi).toLower())
            continue;
        int s = 1;
        if (ci == lastMatch + 1)
            s += 5;
        if (ci == 0 || ci == nameStart)
            s += 8;
        else {
            const QChar prev = candidate.at(ci - 1);
            if (prev == QLatin1Char('/') || prev == QLatin1Char('_') || prev == QLatin1Char('-')
                || prev == QLatin1Char('.') || (prev.isLower() && candidate.at(ci).isUpper()))
                s += 4;
        }
        if (ci >= nameStart)
            s += 2;
        score += s;
        lastMatch = ci;
        ++pi;
    }
    if (pi < pattern.size())
        return -1;
    return score * 100 - candidate.size();
}

void QuickOpenDialog::refilter()
{
    const QString pattern = m_input->text().remove(QLatin1Char(' '));
    QList<QPair<int, QString>> scored;
    for (const QString &f : std::as_const(m_files)) {
        const int s = fuzzyScore(pattern, f);
        if (s >= 0 || pattern.isEmpty())
            scored.append({s, f});
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto &a, const auto &b) { return a.first > b.first; });

    m_list->clear();
    for (int i = 0; i < scored.size() && i < kMaxResults; ++i)
        m_list->addItem(scored.at(i).second);
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
}

bool QuickOpenDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        const int row = m_list->currentRow();
        switch (ke->key()) {
        case Qt::Key_Down:
            m_list->setCurrentRow(qMin(row + 1, m_list->count() - 1));
            return true;
        case Qt::Key_Up:
            m_list->setCurrentRow(qMax(row - 1, 0));
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            accept();
            return true;
        default:
            break;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void QuickOpenDialog::accept()
{
    if (QListWidgetItem *item = m_list->currentItem())
        m_selected = QDir(m_root).absoluteFilePath(item->text());
    QDialog::accept();
}
