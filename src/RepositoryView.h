#pragma once

#include "GitRepository.h"

#include <QHash>
#include <QSet>
#include <QPointer>
#include <QWidget>

#include <optional>

class FileDiffWidget;
class QLabel;
class QListWidget;
class QProcess;
class QListWidgetItem;
class QScrollArea;
class QTabWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

// Un repositorio abierto en el modo Git. Tres secciones:
//   1) Ubicaciones: ramas, remotos, etiquetas, stashes, submódulos, worktrees
//   2) Commits / Archivos del commit seleccionado (o de los cambios actuales)
//   3) Detalle: mensaje, autor, padres y diff de cada archivo (o el formulario de commit)
class RepositoryView : public QWidget
{
    Q_OBJECT
public:
    explicit RepositoryView(QWidget *parent = nullptr);

    bool open(const QString &path, QString *error = nullptr);
    QString path() const { return m_repo.workdir(); }
    QString title() const { return m_repo.displayName(); }
    GitRepository &repository() { return m_repo; }

    // --- Operaciones remotas (usan el programa git instalado) ---
    bool isBusy() const { return m_process != nullptr; }
    // extraEnv se aplica después de las variables por defecto (GIT_EDITOR, etc.)
    void runGit(const QStringList &args, const QString &title = QString(),
                const QHash<QString, QString> &extraEnv = {});

public Q_SLOTS:
    void reload();
    void pull();               // usa la opción de pull elegida por última vez
    void push();
    void fetch();
    void showPullOptions();
    void showPushOptions();
    void showMergePalette();
    void showCommandPalette();

Q_SIGNALS:
    void openFileRequested(const QString &absolutePath);
    void openFolderRequested(const QString &absolutePath);
    void openRepositoryRequested(const QString &absolutePath);   // p. ej. un submódulo, en otra pestaña
    void statusMessage(const QString &message);
    void commandFinished(bool ok, const QString &output);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Un archivo seleccionado en la pestaña "Archivos"
    struct FileRef {
        QString path;      // relativo al repositorio
        QString section;   // "staged", "unstaged", "untracked" o "commit"
        QChar code;
    };

    QList<FileRef> selectedFiles() const;
    void selectFileForMenu(const QString &key);
    void showFileMenu(const QPoint &globalPos);
    void toggleStageSelected();
    void stageFiles(const QList<FileRef> &files, bool stage);
    void discardFiles(const QList<FileRef> &files);
    void deleteFiles(const QList<FileRef> &files);
    // Secciones de cambios (modificados / sin seguimiento / preparados): plegar y menú
    void toggleSection(const QString &id);
    void setAllSectionsCollapsed(bool collapsed);
    void showSectionMenu(const QString &id, const QPoint &globalPos);
    void savePatch(bool staged);
    void savePatchFromGit(const QStringList &args, const QString &suggestedName);
    // Menú contextual de la lista de commits
    void showCommitMenu(const QPoint &pos);
    void checkoutCommit(const QString &sha);
    void createBranchAt(const QString &sha);
    void createTagAt(const QString &sha);
    void resetTo(const QString &sha, const QString &mode);
    void editCommitMessage(const QString &sha);
    // op: reword, squash, up, down o drop (ver RebaseHelper)
    void rewriteHistory(const QString &op, const QString &sha, const QString &message = QString());
    void showFileHistory(const QString &path);
    void showBlame(const QString &path);
    void openContainingFolder(const QString &path);
    QWidget *backHeader(const QString &title, QWidget *parent);
    QString absolutePath(const QString &path) const;

    QWidget *buildRemoteButtons(QWidget *parent);
    void updateRemoteButtons();
    void showMergeOptions(const QString &branch);
    void showSwitchPalette();
    void createBranch();
    void finishCommand(bool ok);
    // Qué hacer con los submódulos después de cambiar de rama
    enum class SubmoduleSwitch {
        None,            // no tocarlos (sin submódulos, o rama nueva desde la actual)
        FollowBranch,    // cambiar a la rama del mismo nombre los que la tengan
        RecordedCommit,  // dejarlos en el commit que registra la rama (git submodule update)
    };
    // Pregunta qué hacer con los submódulos (si hay alguno inicializado); nullopt si se cancela.
    // Sin submódulos, muestra `question` como confirmación simple si confirmWithoutSubmodules.
    std::optional<SubmoduleSwitch> askSubmoduleSwitch(const QString &branch, const QString &question,
                                                      bool confirmWithoutSubmodules);
    void applySubmoduleSwitch(SubmoduleSwitch mode, const QString &branch);
    void switchBranchWithGit(const QStringList &args, const QString &title, const QString &branch,
                             SubmoduleSwitch mode);
    QStringList initializedSubmodules() const;   // rutas absolutas
    QList<QStringList> submoduleSwitchCommands(const QString &branch) const;
    void switchSubmodulesTo(const QString &branch);
    void runNextSubmoduleSwitch();
    void showCommandOutput();
    QStringList mergeableBranches(QStringList *descriptions) const;

    void buildUi();
    void reloadLocations();
    void reloadCommits();
    void showSelection();
    void showWorkingDirectory();
    void showCommit(const QString &commitId);
    void selectCommit(const QString &commitId);
    void onLocationClicked(QTreeWidgetItem *item);
    void onLocationDoubleClicked(QTreeWidgetItem *item);
    void showLocationMenu(const QPoint &pos);
    void checkout(const QString &branch);
    void checkoutRemoteBranch(const QString &remoteBranch, const QString &remote);
    void openSubmodule(const QString &path);
    void renameBranch(const QString &branch);
    void deleteBranch(const QString &branch, const QString &target);
    void scrollToFile(const QString &key);
    // header: parte fija sobre el detalle (no se desplaza con el scroll), p. ej. el commit
    void setDetailWidget(QWidget *content, QWidget *header = nullptr);
    void report(bool ok, const QString &action);
    void doCommit();

    GitRepository m_repo;

    QWidget *m_sidebar = nullptr;
    QLabel *m_branchLabel = nullptr;
    QTreeWidget *m_locations = nullptr;
    QTabWidget *m_middleTabs = nullptr;
    QListWidget *m_commits = nullptr;
    QListWidget *m_files = nullptr;
    QScrollArea *m_detail = nullptr;
    QWidget *m_detailPane = nullptr;     // cabecera fija + m_detail
    QWidget *m_detailHeader = nullptr;

    bool m_workingSelected = true;       // "cambios actuales" seleccionado
    QString m_selectedCommit;            // si no, el commit mostrado
    QString m_commitDraft;               // mensaje de commit en edición
    bool m_filesAreWorking = false;      // la pestaña "Archivos" muestra los cambios actuales
    QHash<QString, QPointer<FileDiffWidget>> m_fileWidgets;
    QSet<QString> m_collapsedSections;   // ids de las secciones plegadas
    QHash<QString, QList<FileRef>> m_sectionFiles;  // archivos de cada sección mostrada

    // Operaciones remotas
    QProcess *m_process = nullptr;
    QString m_runningTitle;
    QString m_commandOutput;
    int m_runCounter = 0;
    // Al terminar el cambio de rama en curso: qué hacer con los submódulos y con qué rama
    SubmoduleSwitch m_pendingSubmoduleSwitch = SubmoduleSwitch::None;
    QString m_pendingSubmoduleBranch;
    QList<QStringList> m_submoduleQueue;      // "git -C <submódulo> switch …" pendientes
    bool m_submoduleQueueActive = false;
    QString m_submoduleQueueBranch;
    int m_submoduleQueueTotal = 0;
    QStringList m_submoduleFailures;
    QWidget *m_outputBar = nullptr;
    QLabel *m_outputLabel = nullptr;
    QToolButton *m_outputDetails = nullptr;
    QToolButton *m_pullButton = nullptr;
    QToolButton *m_pullMenuButton = nullptr;
    QToolButton *m_pushButton = nullptr;
    QToolButton *m_pushMenuButton = nullptr;
    QToolButton *m_fetchButton = nullptr;
    QToolButton *m_mergeButton = nullptr;
    QToolButton *m_moreButton = nullptr;
};
