#pragma once

#include "ModeSwitch.h"

#include <QMainWindow>

class CodeEditor;
class EditorTab;
class FolderProxyModel;
class GitWorkspace;
class QFileSystemModel;
class QLabel;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QTreeView;

// Ventana principal con dos modos, conmutables con el interruptor de la esquina:
//   - Editor: explorador de carpetas + pestañas de archivos (estilo editor de texto)
//   - Git:    una pestaña por repositorio con ubicaciones, commits y detalle
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    void openPaths(const QStringList &paths);
    bool openFile(const QString &path);
    void openFolder(const QString &path);
    void restoreSession();
    void setMode(ModeSwitch::Mode mode);
    GitWorkspace *gitWorkspace() const { return m_git; }

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createMenus();
    QWidget *createEditorPage();

    EditorTab *currentTab() const;
    CodeEditor *currentEditor() const;
    EditorTab *tabAt(int index) const;
    EditorTab *newTab();
    bool closeTab(int index);
    bool maybeSave(EditorTab *tab);
    bool saveTab(EditorTab *tab);
    bool saveTabAs(EditorTab *tab);
    void updateTabTitle(EditorTab *tab);
    void updateWindowTitle();
    void updateStatus();
    void quickOpen();
    void goToLine();
    void changeFontSize(int delta);
    void showShortcuts();
    void saveSession();

    ModeSwitch *m_modeSwitch = nullptr;
    QStackedWidget *m_pages = nullptr;

    // Modo editor
    QSplitter *m_editorSplitter = nullptr;
    QWidget *m_sidebar = nullptr;
    QTreeView *m_tree = nullptr;
    QFileSystemModel *m_fsModel = nullptr;
    FolderProxyModel *m_fsProxy = nullptr;
    QTabWidget *m_tabs = nullptr;
    QLabel *m_positionLabel = nullptr;
    QLabel *m_indentLabel = nullptr;
    QLabel *m_languageLabel = nullptr;

    // Modo Git
    GitWorkspace *m_git = nullptr;

    QString m_rootPath;
    QString m_containerPath;
    int m_untitledCounter = 0;
    int m_fontSize = 10;
    bool m_wordWrap = false;
};
