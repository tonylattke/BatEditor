#pragma once

#include "GitRepository.h"

#include <QHash>
#include <QPointer>
#include <QWidget>

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
    void runGit(const QStringList &args, const QString &title = QString());

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
    void scrollToFile(const QString &key);
    void setDetailWidget(QWidget *content);
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

    bool m_workingSelected = true;       // "cambios actuales" seleccionado
    QString m_selectedCommit;            // si no, el commit mostrado
    QString m_commitDraft;               // mensaje de commit en edición
    bool m_filesAreWorking = false;      // la pestaña "Archivos" muestra los cambios actuales
    QHash<QString, QPointer<FileDiffWidget>> m_fileWidgets;

    // Operaciones remotas
    QProcess *m_process = nullptr;
    QString m_runningTitle;
    QString m_commandOutput;
    int m_runCounter = 0;
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
