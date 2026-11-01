#include "GitRepository.h"

#include <QDir>
#include <QFileInfo>

#include <git2.h>

#include <memory>
#include <vector>

namespace {

// --- RAII para objetos de libgit2 -------------------------------------------
template <typename T, void (*Free)(T *)>
struct GitDeleter {
    void operator()(T *p) const { if (p) Free(p); }
};
template <typename T, void (*Free)(T *)>
using GitPtr = std::unique_ptr<T, GitDeleter<T, Free>>;

using IndexPtr = GitPtr<git_index, git_index_free>;
using ReferencePtr = GitPtr<git_reference, git_reference_free>;
using ObjectPtr = GitPtr<git_object, git_object_free>;
using CommitPtr = GitPtr<git_commit, git_commit_free>;
using TreePtr = GitPtr<git_tree, git_tree_free>;
using DiffPtr = GitPtr<git_diff, git_diff_free>;
using StatusListPtr = GitPtr<git_status_list, git_status_list_free>;
using SignaturePtr = GitPtr<git_signature, git_signature_free>;
using RevwalkPtr = GitPtr<git_revwalk, git_revwalk_free>;

// libgit2 se inicializa una vez por proceso.
struct LibGit2Init {
    LibGit2Init() { git_libgit2_init(); }
    ~LibGit2Init() { git_libgit2_shutdown(); }
};

void ensureLibGit2()
{
    static LibGit2Init init;
    (void)init;
}

// Mantiene vivos los bytes UTF-8 de una lista de rutas para un git_strarray.
class PathSpec
{
public:
    explicit PathSpec(const QStringList &paths)
    {
        for (const QString &p : paths)
            m_bytes.push_back(p.toUtf8());
        for (QByteArray &b : m_bytes)
            m_ptrs.push_back(b.data());
        m_array.strings = m_ptrs.data();
        m_array.count = m_ptrs.size();
    }
    git_strarray *get() { return &m_array; }

private:
    std::vector<QByteArray> m_bytes;
    std::vector<char *> m_ptrs;
    git_strarray m_array{};
};

int collectDiffLine(const git_diff_delta *, const git_diff_hunk *, const git_diff_line *line, void *payload)
{
    auto *out = static_cast<QByteArray *>(payload);
    switch (line->origin) {
    case GIT_DIFF_LINE_CONTEXT:
    case GIT_DIFF_LINE_ADDITION:
    case GIT_DIFF_LINE_DELETION:
        out->append(line->origin);
        break;
    case GIT_DIFF_LINE_CONTEXT_EOFNL:
    case GIT_DIFF_LINE_ADD_EOFNL:
    case GIT_DIFF_LINE_DEL_EOFNL:
        out->append("\\ No newline at end of file\n");
        return 0;
    default:
        break;
    }
    out->append(line->content, static_cast<qsizetype>(line->content_len));
    return 0;
}

QString diffToText(git_diff *diff)
{
    QByteArray out;
    git_diff_print(diff, GIT_DIFF_FORMAT_PATCH, collectDiffLine, &out);
    return QString::fromUtf8(out);
}

QChar deltaCode(git_delta_t status)
{
    switch (status) {
    case GIT_DELTA_ADDED: return QLatin1Char('A');
    case GIT_DELTA_DELETED: return QLatin1Char('D');
    case GIT_DELTA_RENAMED: return QLatin1Char('R');
    case GIT_DELTA_TYPECHANGE: return QLatin1Char('T');
    case GIT_DELTA_UNTRACKED: return QLatin1Char('?');
    case GIT_DELTA_CONFLICTED: return QLatin1Char('U');
    default: return QLatin1Char('M');
    }
}

} // namespace

GitRepository::GitRepository()
{
    ensureLibGit2();
}

GitRepository::~GitRepository()
{
    close();
}

bool GitRepository::check(int result, const char *what) const
{
    if (result >= 0)
        return true;
    const git_error *err = git_error_last();
    m_lastError = QStringLiteral("%1: %2").arg(QString::fromUtf8(what),
                                               err && err->message ? QString::fromUtf8(err->message)
                                                                   : QStringLiteral("error %1").arg(result));
    return false;
}

bool GitRepository::open(const QString &path)
{
    close();
    git_repository *repo = nullptr;
    const QByteArray p = QDir::cleanPath(path).toUtf8();
    if (git_repository_open_ext(&repo, p.constData(), 0, nullptr) < 0) {
        m_lastError = QStringLiteral("No es un repositorio Git");
        return false;
    }
    if (git_repository_is_bare(repo)) {
        git_repository_free(repo);
        m_lastError = QStringLiteral("Los repositorios 'bare' no están soportados");
        return false;
    }
    m_repo = repo;
    m_workdir = QDir::cleanPath(QString::fromUtf8(git_repository_workdir(repo)));
    return true;
}

void GitRepository::close()
{
    if (m_repo) {
        git_repository_free(m_repo);
        m_repo = nullptr;
    }
    m_workdir.clear();
}

QString GitRepository::branchName() const
{
    if (!m_repo)
        return QString();
    git_reference *raw = nullptr;
    const int rc = git_repository_head(&raw, m_repo);
    ReferencePtr head(raw);
    if (rc == GIT_EUNBORNBRANCH || rc == GIT_ENOTFOUND) {
        // Repositorio recién creado: HEAD apunta a una rama sin commits
        git_reference *symRaw = nullptr;
        if (git_reference_lookup(&symRaw, m_repo, "HEAD") == 0) {
            ReferencePtr sym(symRaw);
            QString target = QString::fromUtf8(git_reference_symbolic_target(sym.get()));
            target.remove(QStringLiteral("refs/heads/"));
            return target;
        }
        return QStringLiteral("(sin commits)");
    }
    if (rc < 0)
        return QString();
    if (git_repository_head_detached(m_repo) == 1) {
        char buf[GIT_OID_HEXSZ + 1] = {};
        git_oid_tostr(buf, 8, git_reference_target(head.get()));
        return QStringLiteral("HEAD desacoplado (%1)").arg(QString::fromLatin1(buf));
    }
    return QString::fromUtf8(git_reference_shorthand(head.get()));
}

GitRepository::Status GitRepository::status() const
{
    Status result;
    if (!m_repo)
        return result;

    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS
        | GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX | GIT_STATUS_OPT_SORT_CASE_INSENSITIVELY;

    git_status_list *raw = nullptr;
    if (!check(git_status_list_new(&raw, m_repo, &opts), "status"))
        return result;
    StatusListPtr list(raw);

    const size_t count = git_status_list_entrycount(list.get());
    for (size_t i = 0; i < count; ++i) {
        const git_status_entry *e = git_status_byindex(list.get(), i);
        if (!e || e->status == GIT_STATUS_CURRENT || (e->status & GIT_STATUS_IGNORED))
            continue;

        if (e->status & GIT_STATUS_CONFLICTED) {
            const git_diff_delta *d = e->index_to_workdir ? e->index_to_workdir : e->head_to_index;
            if (d)
                result.unstaged.append({QString::fromUtf8(d->new_file.path), QLatin1Char('U')});
            continue;
        }
        if (e->head_to_index) {
            result.staged.append({QString::fromUtf8(e->head_to_index->new_file.path),
                                  deltaCode(e->head_to_index->status)});
        }
        if (e->index_to_workdir) {
            result.unstaged.append({QString::fromUtf8(e->index_to_workdir->new_file.path),
                                    deltaCode(e->index_to_workdir->status)});
        }
    }
    return result;
}

QString GitRepository::diff(const QString &path, bool staged) const
{
    if (!m_repo)
        return QString();

    PathSpec spec({path});
    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    opts.pathspec = *spec.get();
    opts.flags = GIT_DIFF_DISABLE_PATHSPEC_MATCH;
    opts.context_lines = 3;

    git_diff *raw = nullptr;
    if (staged) {
        // HEAD → índice (HEAD puede no existir en un repositorio sin commits)
        TreePtr headTree;
        git_object *obj = nullptr;
        if (git_revparse_single(&obj, m_repo, "HEAD^{tree}") == 0)
            headTree.reset(reinterpret_cast<git_tree *>(obj));
        git_index *idxRaw = nullptr;
        if (!check(git_repository_index(&idxRaw, m_repo), "index"))
            return m_lastError;
        IndexPtr index(idxRaw);
        if (!check(git_diff_tree_to_index(&raw, m_repo, headTree.get(), index.get(), &opts), "diff"))
            return m_lastError;
    } else {
        opts.flags |= GIT_DIFF_INCLUDE_UNTRACKED | GIT_DIFF_SHOW_UNTRACKED_CONTENT
            | GIT_DIFF_RECURSE_UNTRACKED_DIRS;
        if (!check(git_diff_index_to_workdir(&raw, m_repo, nullptr, &opts), "diff"))
            return m_lastError;
    }
    DiffPtr diff(raw);
    const QString text = diffToText(diff.get());
    return text.isEmpty() ? QStringLiteral("(sin diferencias de texto: archivo binario o solo cambio de modo)")
                          : text;
}

QString GitRepository::commitDiff(const QString &commitId) const
{
    if (!m_repo)
        return QString();
    git_oid oid;
    const QByteArray idBytes = commitId.toLatin1();
    if (!check(git_oid_fromstr(&oid, idBytes.constData()), "oid"))
        return m_lastError;
    git_commit *cRaw = nullptr;
    if (!check(git_commit_lookup(&cRaw, m_repo, &oid), "commit"))
        return m_lastError;
    CommitPtr commit(cRaw);

    git_tree *tRaw = nullptr;
    if (!check(git_commit_tree(&tRaw, commit.get()), "tree"))
        return m_lastError;
    TreePtr tree(tRaw);

    TreePtr parentTree;
    if (git_commit_parentcount(commit.get()) > 0) {
        git_commit *pRaw = nullptr;
        if (git_commit_parent(&pRaw, commit.get(), 0) == 0) {
            CommitPtr parent(pRaw);
            git_tree *ptRaw = nullptr;
            if (git_commit_tree(&ptRaw, parent.get()) == 0)
                parentTree.reset(ptRaw);
        }
    }

    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    git_diff *dRaw = nullptr;
    if (!check(git_diff_tree_to_tree(&dRaw, m_repo, parentTree.get(), tree.get(), &opts), "diff"))
        return m_lastError;
    DiffPtr diff(dRaw);

    const git_signature *author = git_commit_author(commit.get());
    QString header = QStringLiteral("commit %1\nAuthor: %2 <%3>\nDate:   %4\n\n")
                         .arg(commitId,
                              QString::fromUtf8(author->name),
                              QString::fromUtf8(author->email),
                              QDateTime::fromSecsSinceEpoch(author->when.time).toString(Qt::ISODate));
    const QString message = QString::fromUtf8(git_commit_message(commit.get()));
    for (const QString &line : message.split(QLatin1Char('\n')))
        header += QStringLiteral("    ") + line + QLatin1Char('\n');
    return header + QLatin1Char('\n') + diffToText(diff.get());
}

QList<GitRepository::CommitInfo> GitRepository::log(int maxCount, bool allRefs) const
{
    QList<CommitInfo> result;
    if (!m_repo)
        return result;
    git_revwalk *wRaw = nullptr;
    if (!check(git_revwalk_new(&wRaw, m_repo), "revwalk"))
        return result;
    RevwalkPtr walk(wRaw);
    git_revwalk_sorting(walk.get(), GIT_SORT_TOPOLOGICAL | GIT_SORT_TIME);
    const bool hasHead = git_revwalk_push_head(walk.get()) == 0;
    bool any = hasHead;
    if (allRefs) {
        for (const char *glob : {"refs/heads/*", "refs/remotes/*", "refs/tags/*"})
            if (git_revwalk_push_glob(walk.get(), glob) == 0)
                any = true;
    }
    if (!any)
        return result; // sin commits todavía

    git_oid oid;
    while (result.size() < maxCount && git_revwalk_next(&oid, walk.get()) == 0) {
        git_commit *cRaw = nullptr;
        if (git_commit_lookup(&cRaw, m_repo, &oid) < 0)
            continue;
        CommitPtr commit(cRaw);
        char full[GIT_OID_HEXSZ + 1] = {};
        git_oid_tostr(full, sizeof(full), &oid);
        CommitInfo info;
        info.id = QString::fromLatin1(full);
        info.shortId = info.id.left(7);
        info.summary = QString::fromUtf8(git_commit_summary(commit.get()));
        const git_signature *author = git_commit_author(commit.get());
        info.author = QString::fromUtf8(author->name);
        info.time = QDateTime::fromSecsSinceEpoch(author->when.time);
        info.parentCount = int(git_commit_parentcount(commit.get()));
        result << info;
    }
    return result;
}

// ---------------------------------------------------------------------------
// Ubicaciones: ramas, remotos, tags, stashes, submódulos, worktrees
// ---------------------------------------------------------------------------
namespace {

QString oidToString(const git_oid *oid)
{
    char buf[GIT_OID_HEXSZ + 1] = {};
    git_oid_tostr(buf, sizeof(buf), oid);
    return QString::fromLatin1(buf);
}

QString refTargetCommit(git_reference *ref)
{
    git_object *obj = nullptr;
    if (git_reference_peel(&obj, ref, GIT_OBJECT_COMMIT) != 0)
        return QString();
    ObjectPtr holder(obj);
    return oidToString(git_object_id(obj));
}

struct TagCollector {
    git_repository *repo;
    QList<GitRepository::RefInfo> *tags;
};

int collectTag(const char *name, git_oid *, void *payload)
{
    auto *c = static_cast<TagCollector *>(payload);
    git_object *obj = nullptr;
    const QByteArray spec = QByteArray(name) + "^{commit}";
    GitRepository::RefInfo info;
    info.name = QString::fromUtf8(name).remove(QStringLiteral("refs/tags/"));
    if (git_revparse_single(&obj, c->repo, spec.constData()) == 0) {
        info.target = oidToString(git_object_id(obj));
        git_object_free(obj);
    }
    c->tags->append(info);
    return 0;
}

int collectStash(size_t index, const char *message, const git_oid *id, void *payload)
{
    auto *list = static_cast<QList<GitRepository::RefInfo> *>(payload);
    GitRepository::RefInfo info;
    info.name = QStringLiteral("stash@{%1}").arg(index);
    info.detail = QString::fromUtf8(message);
    info.target = oidToString(id);
    list->append(info);
    return 0;
}

int collectSubmodule(git_submodule *, const char *name, void *payload)
{
    static_cast<QStringList *>(payload)->append(QString::fromUtf8(name));
    return 0;
}

// Convierte un git_diff en una lista de cambios por archivo con estadísticas y parche.
QList<GitRepository::FileChange> diffToChanges(git_diff *diff)
{
    constexpr int kMaxFiles = 500;
    constexpr int kMaxPatchLines = 4000;

    git_diff_find_options findOpts = GIT_DIFF_FIND_OPTIONS_INIT;
    findOpts.flags = GIT_DIFF_FIND_RENAMES;
    git_diff_find_similar(diff, &findOpts);

    QList<GitRepository::FileChange> result;
    const size_t count = git_diff_num_deltas(diff);
    for (size_t i = 0; i < count; ++i) {
        const git_diff_delta *delta = git_diff_get_delta(diff, i);
        GitRepository::FileChange fc;
        fc.path = QString::fromUtf8(delta->new_file.path);
        fc.oldPath = QString::fromUtf8(delta->old_file.path);
        fc.code = deltaCode(delta->status);
        fc.binary = delta->flags & GIT_DIFF_FLAG_BINARY;

        if (int(i) < kMaxFiles) {
            git_patch *patch = nullptr;
            if (git_patch_from_diff(&patch, diff, i) == 0 && patch) {
                size_t ctx = 0, add = 0, del = 0;
                git_patch_line_stats(&ctx, &add, &del, patch);
                fc.additions = int(add);
                fc.deletions = int(del);
                fc.binary = fc.binary || (git_patch_get_delta(patch)->flags & GIT_DIFF_FLAG_BINARY);
                QByteArray text;
                git_patch_print(patch, collectDiffLine, &text);
                git_patch_free(patch);

                // Solo a partir del primer bloque "@@"; la cabecera la muestra la interfaz
                QString s = QString::fromUtf8(text);
                if (!s.startsWith(QLatin1String("@@"))) {
                    const int hunk = s.indexOf(QLatin1String("\n@@"));
                    s = hunk >= 0 ? s.mid(hunk + 1) : QString();
                }
                QStringList lines = s.split(QLatin1Char('\n'));
                if (lines.size() > kMaxPatchLines) {
                    lines = lines.mid(0, kMaxPatchLines);
                    fc.truncated = true;
                }
                fc.patch = lines.join(QLatin1Char('\n'));
                if (fc.patch.endsWith(QLatin1Char('\n')))
                    fc.patch.chop(1);
            }
        } else {
            fc.truncated = true;
        }
        result << fc;
    }
    return result;
}

} // namespace

QString GitRepository::displayName() const
{
    return QFileInfo(m_workdir).fileName();
}

QString GitRepository::headCommitId() const
{
    return resolveCommit(QStringLiteral("HEAD"));
}

QString GitRepository::resolveCommit(const QString &revspec) const
{
    if (!m_repo)
        return QString();
    git_object *obj = nullptr;
    const QByteArray spec = (revspec + QStringLiteral("^{commit}")).toUtf8();
    if (git_revparse_single(&obj, m_repo, spec.constData()) != 0)
        return QString();
    ObjectPtr holder(obj);
    return oidToString(git_object_id(obj));
}

QString GitRepository::signatureText() const
{
    if (!m_repo)
        return QString();
    git_signature *raw = nullptr;
    if (git_signature_default(&raw, m_repo) != 0)
        return QString();
    SignaturePtr sig(raw);
    return QStringLiteral("%1 <%2>").arg(QString::fromUtf8(sig->name), QString::fromUtf8(sig->email));
}

GitRepository::Locations GitRepository::locations() const
{
    Locations loc;
    if (!m_repo)
        return loc;

    // Ramas locales y remotas
    for (git_branch_t type : {GIT_BRANCH_LOCAL, GIT_BRANCH_REMOTE}) {
        git_branch_iterator *it = nullptr;
        if (git_branch_iterator_new(&it, m_repo, type) != 0)
            continue;
        git_reference *ref = nullptr;
        git_branch_t got;
        while (git_branch_next(&ref, &got, it) == 0) {
            ReferencePtr holder(ref);
            if (git_reference_type(ref) == GIT_REFERENCE_SYMBOLIC)
                continue; // p. ej. origin/HEAD
            const char *name = nullptr;
            git_branch_name(&name, ref);
            RefInfo info;
            info.name = QString::fromUtf8(name);
            info.target = refTargetCommit(ref);
            if (type == GIT_BRANCH_LOCAL) {
                info.isHead = git_branch_is_head(ref) == 1;
                loc.branches << info;
            } else {
                const int slash = info.name.indexOf(QLatin1Char('/'));
                const QString remote = slash > 0 ? info.name.left(slash) : info.name;
                loc.remotes[remote] << info;
            }
        }
        git_branch_iterator_free(it);
    }
    // Remotos sin ramas descargadas
    git_strarray remotes = {nullptr, 0};
    if (git_remote_list(&remotes, m_repo) == 0) {
        for (size_t i = 0; i < remotes.count; ++i) {
            const QString r = QString::fromUtf8(remotes.strings[i]);
            if (!loc.remotes.contains(r))
                loc.remotes.insert(r, {});
        }
        git_strarray_dispose(&remotes);
    }

    TagCollector tc{m_repo, &loc.tags};
    git_tag_foreach(m_repo, collectTag, &tc);
    git_stash_foreach(m_repo, collectStash, &loc.stashes);
    git_submodule_foreach(m_repo, collectSubmodule, &loc.submodules);

    loc.worktrees << QStringLiteral("(principal)");
    git_strarray wts = {nullptr, 0};
    if (git_worktree_list(&wts, m_repo) == 0) {
        for (size_t i = 0; i < wts.count; ++i)
            loc.worktrees << QString::fromUtf8(wts.strings[i]);
        git_strarray_dispose(&wts);
    }
    return loc;
}

QHash<QString, QStringList> GitRepository::refLabels() const
{
    QHash<QString, QStringList> labels;
    const Locations loc = locations();
    for (const RefInfo &b : loc.branches)
        if (!b.target.isEmpty())
            labels[b.target] << b.name;
    for (auto it = loc.remotes.cbegin(); it != loc.remotes.cend(); ++it)
        for (const RefInfo &b : it.value())
            if (!b.target.isEmpty())
                labels[b.target] << b.name;
    for (const RefInfo &t : loc.tags)
        if (!t.target.isEmpty())
            labels[t.target] << QStringLiteral("tag:") + t.name;
    if (m_repo && git_repository_head_detached(m_repo) == 1) {
        const QString head = headCommitId();
        if (!head.isEmpty())
            labels[head].prepend(QStringLiteral("HEAD"));
    }
    return labels;
}

GitRepository::CommitDetails GitRepository::commitDetails(const QString &commitId) const
{
    CommitDetails d;
    if (!m_repo)
        return d;
    git_oid oid;
    const QByteArray idBytes = commitId.toLatin1();
    if (git_oid_fromstr(&oid, idBytes.constData()) != 0)
        return d;
    git_commit *raw = nullptr;
    if (git_commit_lookup(&raw, m_repo, &oid) != 0)
        return d;
    CommitPtr commit(raw);
    d.id = commitId;
    d.summary = QString::fromUtf8(git_commit_summary(commit.get()));
    const char *body = git_commit_body(commit.get());
    d.body = body ? QString::fromUtf8(body).trimmed() : QString();
    const git_signature *a = git_commit_author(commit.get());
    d.authorName = QString::fromUtf8(a->name);
    d.authorEmail = QString::fromUtf8(a->email);
    d.authorTime = QDateTime::fromSecsSinceEpoch(a->when.time);
    const git_signature *c = git_commit_committer(commit.get());
    d.committerName = QString::fromUtf8(c->name);
    d.commitTime = QDateTime::fromSecsSinceEpoch(c->when.time);
    for (unsigned i = 0; i < git_commit_parentcount(commit.get()); ++i)
        d.parents << oidToString(git_commit_parent_id(commit.get(), i));
    return d;
}

QList<GitRepository::FileChange> GitRepository::commitChanges(const QString &commitId) const
{
    if (!m_repo)
        return {};
    git_oid oid;
    const QByteArray idBytes = commitId.toLatin1();
    if (!check(git_oid_fromstr(&oid, idBytes.constData()), "oid"))
        return {};
    git_commit *cRaw = nullptr;
    if (!check(git_commit_lookup(&cRaw, m_repo, &oid), "commit"))
        return {};
    CommitPtr commit(cRaw);
    git_tree *tRaw = nullptr;
    if (!check(git_commit_tree(&tRaw, commit.get()), "tree"))
        return {};
    TreePtr tree(tRaw);

    TreePtr parentTree;
    if (git_commit_parentcount(commit.get()) > 0) {
        git_commit *pRaw = nullptr;
        if (git_commit_parent(&pRaw, commit.get(), 0) == 0) {
            CommitPtr parent(pRaw);
            git_tree *ptRaw = nullptr;
            if (git_commit_tree(&ptRaw, parent.get()) == 0)
                parentTree.reset(ptRaw);
        }
    }
    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    git_diff *dRaw = nullptr;
    if (!check(git_diff_tree_to_tree(&dRaw, m_repo, parentTree.get(), tree.get(), &opts), "diff"))
        return {};
    DiffPtr diff(dRaw);
    return diffToChanges(diff.get());
}

QList<GitRepository::FileChange> GitRepository::workingChanges(bool staged) const
{
    if (!m_repo)
        return {};
    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    git_diff *raw = nullptr;
    if (staged) {
        TreePtr headTree;
        git_object *obj = nullptr;
        if (git_revparse_single(&obj, m_repo, "HEAD^{tree}") == 0)
            headTree.reset(reinterpret_cast<git_tree *>(obj));
        git_index *idxRaw = nullptr;
        if (!check(git_repository_index(&idxRaw, m_repo), "index"))
            return {};
        IndexPtr index(idxRaw);
        if (!check(git_diff_tree_to_index(&raw, m_repo, headTree.get(), index.get(), &opts), "diff"))
            return {};
    } else {
        opts.flags = GIT_DIFF_INCLUDE_UNTRACKED | GIT_DIFF_SHOW_UNTRACKED_CONTENT
            | GIT_DIFF_RECURSE_UNTRACKED_DIRS;
        if (!check(git_diff_index_to_workdir(&raw, m_repo, nullptr, &opts), "diff"))
            return {};
    }
    DiffPtr diff(raw);
    return diffToChanges(diff.get());
}

bool GitRepository::checkoutBranch(const QString &branchName)
{
    if (!m_repo)
        return false;
    git_reference *raw = nullptr;
    const QByteArray name = branchName.toUtf8();
    if (!check(git_branch_lookup(&raw, m_repo, name.constData(), GIT_BRANCH_LOCAL), "rama"))
        return false;
    ReferencePtr ref(raw);
    git_object *target = nullptr;
    if (!check(git_reference_peel(&target, ref.get(), GIT_OBJECT_COMMIT), "rama"))
        return false;
    ObjectPtr holder(target);

    git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
    opts.checkout_strategy = GIT_CHECKOUT_SAFE;
    if (!check(git_checkout_tree(m_repo, target, &opts), "checkout"))
        return false;
    return check(git_repository_set_head(m_repo, git_reference_name(ref.get())), "HEAD");
}

bool GitRepository::stage(const QString &path)
{
    if (!m_repo)
        return false;
    git_index *raw = nullptr;
    if (!check(git_repository_index(&raw, m_repo), "index"))
        return false;
    IndexPtr index(raw);
    const QByteArray p = path.toUtf8();
    const bool exists = QFileInfo::exists(m_workdir + QLatin1Char('/') + path);
    const int rc = exists ? git_index_add_bypath(index.get(), p.constData())
                          : git_index_remove_bypath(index.get(), p.constData());
    if (!check(rc, "stage"))
        return false;
    return check(git_index_write(index.get()), "index write");
}

bool GitRepository::stageAll()
{
    const Status st = status();
    for (const StatusEntry &e : st.unstaged)
        if (!stage(e.path))
            return false;
    return true;
}

bool GitRepository::unstagePaths(const QStringList &paths)
{
    if (!m_repo || paths.isEmpty())
        return false;
    PathSpec spec(paths);
    ObjectPtr head;
    git_object *obj = nullptr;
    if (git_revparse_single(&obj, m_repo, "HEAD") == 0)
        head.reset(obj);
    // Con target == nullptr (sin commits) libgit2 elimina las entradas del índice
    return check(git_reset_default(m_repo, head.get(), spec.get()), "unstage");
}

bool GitRepository::unstage(const QString &path)
{
    return unstagePaths({path});
}

bool GitRepository::unstageAll()
{
    QStringList paths;
    for (const StatusEntry &e : status().staged)
        paths << e.path;
    return paths.isEmpty() || unstagePaths(paths);
}

bool GitRepository::discard(const QString &path)
{
    if (!m_repo)
        return false;
    PathSpec spec({path});
    git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
    opts.checkout_strategy = GIT_CHECKOUT_FORCE | GIT_CHECKOUT_DISABLE_PATHSPEC_MATCH;
    opts.paths = *spec.get();
    return check(git_checkout_index(m_repo, nullptr, &opts), "discard");
}

bool GitRepository::commit(const QString &message, QString *newCommitId)
{
    if (!m_repo)
        return false;
    if (message.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("El mensaje del commit está vacío");
        return false;
    }

    git_signature *sigRaw = nullptr;
    if (git_signature_default(&sigRaw, m_repo) < 0) {
        m_lastError = QStringLiteral(
            "Configura tu identidad de Git:\n"
            "  git config --global user.name \"Tu Nombre\"\n"
            "  git config --global user.email \"tu@correo\"");
        return false;
    }
    SignaturePtr sig(sigRaw);

    git_index *idxRaw = nullptr;
    if (!check(git_repository_index(&idxRaw, m_repo), "index"))
        return false;
    IndexPtr index(idxRaw);

    git_oid treeId;
    if (!check(git_index_write_tree(&treeId, index.get()), "write tree"))
        return false;
    git_tree *tRaw = nullptr;
    if (!check(git_tree_lookup(&tRaw, m_repo, &treeId), "tree"))
        return false;
    TreePtr tree(tRaw);

    CommitPtr parent;
    git_object *obj = nullptr;
    if (git_revparse_single(&obj, m_repo, "HEAD^{commit}") == 0)
        parent.reset(reinterpret_cast<git_commit *>(obj));

    if (parent) {
        const git_oid *parentTree = git_commit_tree_id(parent.get());
        if (git_oid_equal(parentTree, &treeId)) {
            m_lastError = QStringLiteral("No hay cambios preparados para el commit");
            return false;
        }
    }

    QByteArray msg = message.trimmed().toUtf8();
    msg.append('\n');

    git_oid commitId;
    const int rc = parent
        ? git_commit_create_v(&commitId, m_repo, "HEAD", sig.get(), sig.get(), nullptr,
                              msg.constData(), tree.get(), 1, parent.get())
        : git_commit_create_v(&commitId, m_repo, "HEAD", sig.get(), sig.get(), nullptr,
                              msg.constData(), tree.get(), 0);
    if (!check(rc, "commit"))
        return false;

    if (newCommitId) {
        char buf[GIT_OID_HEXSZ + 1] = {};
        git_oid_tostr(buf, sizeof(buf), &commitId);
        *newCommitId = QString::fromLatin1(buf);
    }
    return true;
}
