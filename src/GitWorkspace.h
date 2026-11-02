#pragma once

#include <QStringList>
#include <QWidget>

class QStackedWidget;
class QTabWidget;
class RepositoryHome;
class RepositoryView;

// Modo Git: un repositorio por pestaña.
class GitWorkspace : public QWidget
{
    Q_OBJECT
public:
    explicit GitWorkspace(QWidget *parent = nullptr);

    // Abre (o activa) el repositorio que contiene `path`. Devuelve false si no es un repositorio.
    bool openRepository(const QString &path, bool activate = true, QString *error = nullptr);
    bool hasRepositoryFor(const QString &path) const;
    QStringList repositoryPaths() const;
    QString currentRepositoryPath() const;
    void setCurrentRepository(const QString &path);
    void closeCurrent();
    RepositoryView *currentView() const;

public Q_SLOTS:
    void refreshCurrent();
    void refreshRepositoryContaining(const QString &filePath);
    void promptOpenRepository();
    void showHome();   // abrir, crear o clonar un repositorio

Q_SIGNALS:
    void openFileRequested(const QString &absolutePath);
    void openFolderRequested(const QString &absolutePath);
    void statusMessage(const QString &message);
    void repositoriesChanged();

private:
    RepositoryView *viewAt(int index) const;
    int indexOfPath(const QString &workdir) const;

    QStackedWidget *m_stack;
    RepositoryHome *m_home;
    QTabWidget *m_tabs;
};
