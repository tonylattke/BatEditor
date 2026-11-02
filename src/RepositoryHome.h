#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QProcess;
class QPushButton;
class QVBoxLayout;

// Página de inicio del modo Git: abrir o crear un repositorio local, repositorios
// recientes y clonar uno remoto.
class RepositoryHome : public QWidget
{
    Q_OBJECT
public:
    explicit RepositoryHome(QWidget *parent = nullptr);

    // Recientes, guardados en la configuración (el más reciente primero)
    static QStringList recentRepositories();
    static void addRecentRepository(const QString &path);

    void setBackVisible(bool visible);
    void reloadRecent();

Q_SIGNALS:
    void openRequested(const QString &path);  // abrir un repositorio existente
    void browseRequested();                   // mostrar el diálogo para elegir carpeta
    void backRequested();

private:
    void createRepository();
    void browseDestination();
    void clone();
    void setCloning(bool cloning);
    void updateCloneButton();

    QPushButton *m_back;
    QVBoxLayout *m_recentLayout;
    QLineEdit *m_url;
    QLineEdit *m_name;
    QLineEdit *m_destination;
    QPushButton *m_browse;
    QPushButton *m_clone;
    QLabel *m_cloneStatus;
    QProcess *m_process = nullptr;
    QString m_cloneOutput;
};
