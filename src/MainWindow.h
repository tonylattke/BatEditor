#pragma once

#include "FindInFiles.h"
#include "ModeSwitch.h"

#include <QMainWindow>
#include <QPointer>
#include <QSet>

class CodeEditor;
class QAction;
class QFileSystemWatcher;
class QTimer;
class EditorTab;
class FolderProxyModel;
class FindInFilesPanel;
class GitWorkspace;
class QFileSystemModel;
class QLabel;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QToolButton;
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
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void createMenus();
    QWidget *createEditorPage();

    EditorTab *currentTab() const;
    CodeEditor *currentEditor() const;
    EditorTab *tabAt(int index) const;

    // Menú contextual del explorador de carpetas
    void showFolderTreeMenu(const QPoint &pos);
    void createFileIn(const QString &dir);
    void createFolderIn(const QString &dir);
    QStringList selectedTreePaths(const QModelIndex &clicked) const;
    void deletePaths(const QStringList &paths);   // a la papelera, con confirmación
    void renamePath(const QString &path);
    void revealInExplorer(const QString &path);
    void runScript(const QString &path);
    void openTerminal(const QString &directory);
    void retargetTabs(const QString &oldPath, const QString &newPath);
    // Arrastrar y soltar en el explorador: pregunta mover / copiar / cancelar
    void dropIntoFolder(const QStringList &sources, const QString &targetDir);
    EditorTab *newTab();
    void attachTab(EditorTab *tab);   // conexiones de una pestaña de archivo con esta ventana
    // Sacar pestañas a otra ventana (arrastrándolas fuera o desde el menú de la pestaña)
    MainWindow *createDetachedWindow(const QPoint &globalPos, bool placeAtCursor);
    void moveTabToWindow(EditorTab *tab, MainWindow *target);
    void receiveTab(EditorTab *tab);
    bool closeTab(int index);
    bool maybeSave(EditorTab *tab);
    bool saveTab(EditorTab *tab);
    bool saveTabAs(EditorTab *tab);
    void updateTabTitle(EditorTab *tab);
    // Archivos abiertos modificados fuera del editor: se recargan si la pestaña no tiene cambios
    void syncWatchedFiles();
    void checkExternalChanges(const QStringList &paths, bool notifyKept);   // vacío = todas las pestañas
    void updateWindowTitle();
    void updateStatus();
    void showLanguageMenu();
    void quickOpen();
    void goToLine();
    void changeFontSize(int delta);
    void showShortcuts();
    void saveSession();
    void showPreferences();
    void applyEditorSettings(EditorTab *only = nullptr);
    void restartApplication();

    // Buscar en archivos
    FindInFiles::Workspace findWorkspace() const;
    void showFindInFiles();
    EditorTab *resultsTab(bool forceNew);
    void showFindResults(const FindInFiles::Results &results, bool replaced, int replacedFiles = 0);
    void applyReplaceInFiles(const FindInFiles::Results &results);
    void openLocation(const FindInFiles::Location &location);
    void gotoResult(int step);

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
    QToolButton *m_languageButton = nullptr;   // clic: elegir el lenguaje del resaltado

    // Modo Git
    GitWorkspace *m_git = nullptr;

    QString m_rootPath;
    QString m_containerPath;
    int m_untitledCounter = 0;
    int m_fontSize = 10;
    int m_indentWidth = 4;
    bool m_wordWrap = false;
    bool m_showLineNumbers = true;
    bool m_showMinimap = true;
    bool m_autoClose = true;   // cerrar (), [], {}, comillas y etiquetas al escribir
    bool m_wordCompletion = true;   // lista de palabras del proyecto al escribir
    class WordIndex *m_wordIndex = nullptr;
    QAction *m_minimapAction = nullptr;
    QAction *m_wrapAction = nullptr;
    FindInFilesPanel *m_findPanel = nullptr;
    QPointer<EditorTab> m_resultsTab;   // la última pestaña de resultados (F4 / Shift+F4)
    int m_resultIndex = -1;

    QFileSystemWatcher *m_fileWatcher = nullptr;
    QTimer *m_externalTimer = nullptr;   // agrupa los avisos (un guardado puede generar varios)
    QSet<QString> m_externalPending;

    // Ventanas creadas al sacar una pestaña: no guardan la sesión y se cierran al quedarse vacías
    bool m_detached = false;
    QPointer<EditorTab> m_dragTab;   // pestaña pulsada en la barra (posible arrastre fuera)
    QPoint m_dragStart;
    QPointer<QWidget> m_dragPreview; // ventana semitransparente mientras se arrastra fuera
    void updateDragPreview(const QPoint &globalPos, bool outside);
    void hideDragPreview();
};
