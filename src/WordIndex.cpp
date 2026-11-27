#include "WordIndex.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QThread>

#include <algorithm>

namespace {
constexpr qint64 kMaxFileSize = 1024 * 1024;   // los archivos más grandes no se indexan
constexpr int kMaxFiles = 20000;
constexpr int kMaxWords = 300000;

bool skipDirectory(const QString &name)
{
    static const QStringList skipped = {
        QStringLiteral(".git"), QStringLiteral(".svn"), QStringLiteral(".hg"), QStringLiteral(".deps"),
        QStringLiteral(".vs"), QStringLiteral(".idea"), QStringLiteral("node_modules"),
        QStringLiteral("__pycache__"), QStringLiteral("build"), QStringLiteral("out"),
        QStringLiteral("Binaries"), QStringLiteral("Intermediate"), QStringLiteral("DerivedDataCache"),
        QStringLiteral("Saved")};
    return skipped.contains(name) || name.startsWith(QLatin1String("build-"))
        || name.startsWith(QLatin1String("cmake-build"));
}

QStringList scanFolder(const QString &folder, const std::atomic_bool &cancel)
{
    QSet<QString> words;
    int files = 0;
    // Recorrido manual para poder saltarse carpetas enteras
    QStringList pending{folder};
    while (!pending.isEmpty() && !cancel && files < kMaxFiles && words.size() < kMaxWords) {
        const QString dir = pending.takeLast();
        QDirIterator it(dir, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
        while (it.hasNext() && !cancel) {
            const QString path = it.next();
            const QFileInfo info = it.fileInfo();
            if (info.isDir()) {
                if (!info.isSymLink() && !skipDirectory(info.fileName()))
                    pending << path;
                continue;
            }
            if (info.size() > kMaxFileSize || ++files > kMaxFiles)
                continue;
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                continue;
            const QByteArray data = file.readAll();
            if (data.left(8000).contains('\0'))
                continue;   // binario
            for (const QString &w : WordIndex::extractWords(QString::fromUtf8(data)))
                words.insert(w);
        }
    }
    return QStringList(words.begin(), words.end());
}
} // namespace

WordIndex::WordIndex(QObject *parent)
    : QObject(parent)
{
}

WordIndex::~WordIndex()
{
    if (m_cancel)
        *m_cancel = true;
    for (const QPointer<QThread> &t : std::as_const(m_threads))
        if (t)
            t->wait();
}

QStringList WordIndex::extractWords(const QString &text)
{
    static const QRegularExpression word(QStringLiteral("[\\p{L}_][\\p{L}\\p{N}_]{2,}"));
    QStringList out;
    auto it = word.globalMatch(text);
    while (it.hasNext())
        out << it.next().captured(0);
    return out;
}

void WordIndex::setFolder(const QString &folder)
{
    if (m_cancel)
        *m_cancel = true;   // la exploración anterior se descarta
    auto cancel = std::make_shared<std::atomic_bool>(false);
    m_cancel = cancel;
    const int generation = ++m_generation;
    QThread *thread = QThread::create([this, folder, cancel, generation] {
        const QStringList words = scanFolder(folder, *cancel);
        if (*cancel)
            return;
        QMetaObject::invokeMethod(this, [this, words, generation] {
            if (generation != m_generation)
                return;
            setWords(words);
            Q_EMIT indexed(int(m_words.size()));
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, this, [this, thread] {
        if (m_thread == thread)
            m_thread = nullptr;
        m_threads.removeAll(QPointer<QThread>(thread));
        thread->deleteLater();
    });
    m_threads.removeAll(QPointer<QThread>());
    m_threads << thread;
    m_thread = thread;
    thread->start(QThread::LowPriority);
}

void WordIndex::setWords(const QStringList &words)
{
    m_words.clear();
    m_words.reserve(size_t(words.size()));
    for (const QString &w : words)
        m_words.emplace_back(w.toLower(), w);
    std::sort(m_words.begin(), m_words.end());
}

void WordIndex::addText(const QString &text)
{
    bool changed = false;
    for (const QString &w : extractWords(text)) {
        const Entry entry(w.toLower(), w);
        auto it = std::lower_bound(m_words.begin(), m_words.end(), entry);
        if (it != m_words.end() && *it == entry)
            continue;
        m_words.insert(it, entry);
        changed = true;
    }
    Q_UNUSED(changed);
}

QStringList WordIndex::wordsWithPrefix(const QString &prefix, int limit) const
{
    QStringList out;
    const QString key = prefix.toLower();
    auto it = std::lower_bound(m_words.begin(), m_words.end(), Entry(key, QString()));
    for (; it != m_words.end() && it->first.startsWith(key) && out.size() < limit; ++it)
        out << it->second;
    return out;
}
