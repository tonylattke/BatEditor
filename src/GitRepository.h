#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

struct git_repository;

// Envoltorio mínimo y seguro (RAII) sobre libgit2.
class GitRepository
{
public:
    struct StatusEntry {
        QString path;       // relativo a la raíz del repositorio, con '/'
        QChar code;         // 'A' nuevo, 'M' modificado, 'D' borrado, 'R' renombrado, 'T' tipo, '?' sin seguimiento, 'U' conflicto
    };
    struct Status {
        QList<StatusEntry> staged;
        QList<StatusEntry> unstaged;
    };
    struct CommitInfo {
        QString id;         // SHA completo
        QString shortId;
        QString summary;
        QString author;
        QDateTime time;
        int parentCount = 0;
    };
    struct RefInfo {
        QString name;       // nombre corto ("master", "origin/master", "v1.0", "stash@{0}")
        QString target;     // SHA del commit al que apunta
        QString detail;     // mensaje del stash, etc.
        bool isHead = false;
    };
    struct Locations {
        QList<RefInfo> branches;
        QMap<QString, QList<RefInfo>> remotes;   // remoto → ramas remotas
        QList<RefInfo> tags;
        QList<RefInfo> stashes;
        QStringList submodules;
        QStringList worktrees;
    };
    struct CommitDetails {
        QString id;
        QString summary;
        QString body;
        QString authorName;
        QString authorEmail;
        QDateTime authorTime;
        QString committerName;
        QDateTime commitTime;
        QStringList parents;
    };
    struct FileChange {
        QString path;
        QString oldPath;
        QChar code;          // como StatusEntry::code
        int additions = 0;
        int deletions = 0;
        bool binary = false;
        bool truncated = false;
        QString patch;       // solo los bloques "@@ ... @@" del diff unificado
    };

    struct BlameLine {
        QString commitId;    // vacío si la línea aún no está confirmada
        QString author;
        QDateTime time;
        QString text;
    };

    GitRepository();
    ~GitRepository();
    GitRepository(const GitRepository &) = delete;
    GitRepository &operator=(const GitRepository &) = delete;

    // Busca un repositorio en `path` o en cualquiera de sus carpetas padre.
    bool open(const QString &path);
    void close();
    bool isOpen() const { return m_repo != nullptr; }
    QString workdir() const { return m_workdir; }
    QString displayName() const;

    // --- Consultas ---
    QString branchName() const;
    QString headCommitId() const;
    QString signatureText() const;                 // "Nombre <correo>" de la configuración
    Status status() const;
    QString diff(const QString &path, bool staged) const;
    QString commitDiff(const QString &commitId) const;
    QList<CommitInfo> log(int maxCount = 100, bool allRefs = false) const;
    Locations locations() const;
    QHash<QString, QStringList> refLabels() const;  // commit → ramas y "tag:<nombre>"
    CommitDetails commitDetails(const QString &commitId) const;
    QList<FileChange> commitChanges(const QString &commitId) const;
    QList<FileChange> workingChanges(bool staged) const;
    QString resolveCommit(const QString &revspec) const;
    QList<CommitInfo> fileHistory(const QString &path, int maxCount = 500) const;
    QList<BlameLine> blame(const QString &path) const;   // usa el contenido actual del archivo
    QString upstreamName() const;                        // "origin/master" o vacío
    bool aheadBehind(int *ahead, int *behind) const;     // respecto a la rama remota
    QStringList remoteNames() const;
    bool isHeadDetached() const;
    bool isIgnored(const QString &absolutePath, bool isDirectory) const;  // según .gitignore

    // --- Acciones ---
    bool stage(const QString &path);
    bool stageAll();
    bool unstage(const QString &path);
    bool unstageAll();
    bool discard(const QString &path);
    bool commit(const QString &message, QString *newCommitId = nullptr);
    bool checkoutBranch(const QString &branchName);

    QString lastError() const { return m_lastError; }

private:
    bool check(int result, const char *what) const;
    bool unstagePaths(const QStringList &paths);

    git_repository *m_repo = nullptr;
    QString m_workdir;
    mutable QString m_lastError;
};
