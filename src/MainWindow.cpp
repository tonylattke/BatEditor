#include "MainWindow.h"

#include "CodeEditor.h"
#include "EditorTab.h"
#include "FindBar.h"
#include "FindInFilesPanel.h"
#include "GitWorkspace.h"
#include "LanguageDefinitions.h"
#include "PreferencesDialog.h"
#include "QuickOpenDialog.h"
#include "RepositoryView.h"
#include "Theme.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QHeaderView>
#include <QFile>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QTimer>
#include <QCursor>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QSaveFile>
#include <QStringDecoder>
#include <QSettings>
#include <QShortcut>
#include <QStandardPaths>
#include <QDesktopServices>
#include <QUrl>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextEdit>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <functional>
#include <utility>

#ifdef Q_OS_WIN
#include <qt_windows.h>   // CREATE_NEW_CONSOLE
#endif

namespace {

// QMenu::addAction(texto, atajo, receptor, función) solo existe desde Qt 6.3;
// esta versión funciona también con Qt 6.2 (Ubuntu 22.04).
template <typename Context, typename Func>
QAction *addMenuAction(QMenu *menu, const QString &text, const QKeySequence &shortcut, Context *context, Func &&func)
{
    QAction *action = menu->addAction(text);
    action->setShortcut(shortcut);
    QObject::connect(action, &QAction::triggered, context, std::forward<Func>(func));
    return action;
}

} // namespace

// ---------------------------------------------------------------------------
// Modelo del explorador: muestra la carpeta abierta como nodo raíz (como
// "FOLDERS" en los editores de texto), oculta .git y ordena carpetas primero.
// ---------------------------------------------------------------------------
class FolderProxyModel : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setFolder(const QString &folder, const QString &container)
    {
        m_folder = QDir::cleanPath(folder);
        m_container = QDir::cleanPath(container);
        invalidate();
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        auto *fs = static_cast<QFileSystemModel *>(sourceModel());
        const QModelIndex idx = fs->index(row, 0, parent);
        if (idx.data().toString() == QLatin1String(".git"))
            return false;
        // En la carpeta contenedora solo se muestra la carpeta abierta
        if (!m_container.isEmpty() && m_container != m_folder && samePath(fs->filePath(parent), m_container))
            return samePath(fs->filePath(idx), m_folder);
        return true;
    }

    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override
    {
        auto *fs = static_cast<QFileSystemModel *>(sourceModel());
        const bool ld = fs->isDir(left), rd = fs->isDir(right);
        if (ld != rd)
            return ld;
        return QString::compare(left.data().toString(), right.data().toString(), Qt::CaseInsensitive) < 0;
    }

private:
    static bool samePath(const QString &a, const QString &b)
    {
#ifdef Q_OS_WIN
        return QDir::cleanPath(a).compare(b, Qt::CaseInsensitive) == 0;
#else
        return QDir::cleanPath(a) == b;
#endif
    }

    QString m_folder;
    QString m_container;

public:
    // --- Arrastrar y soltar dentro del proyecto ---
    // El modelo de archivos es de solo lectura: el proxy acepta el "drop" y avisa
    // a la ventana, que pregunta si mover o copiar. Nunca modifica nada él mismo.
    std::function<void(const QStringList &sources, const QString &targetDir)> onDrop;

    Qt::ItemFlags flags(const QModelIndex &index) const override
    {
        Qt::ItemFlags f = QSortFilterProxyModel::flags(index);
        if (!index.isValid())
            return f;
        auto *fs = static_cast<QFileSystemModel *>(sourceModel());
        const QModelIndex src = mapToSource(index);
        if (insideProject(fs->filePath(src)))
            f |= Qt::ItemIsDragEnabled;
        if (fs->isDir(src) && insideProject(fs->filePath(src), true))
            f |= Qt::ItemIsDropEnabled;
        return f;
    }

    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction | Qt::CopyAction; }
    Qt::DropActions supportedDragActions() const override { return Qt::MoveAction | Qt::CopyAction; }

    bool canDropMimeData(const QMimeData *data, Qt::DropAction, int, int, const QModelIndex &parent) const override
    {
        const QString target = targetDir(parent);
        if (!data->hasUrls() || target.isEmpty())
            return false;
        for (const QUrl &url : data->urls()) {
            const QString src = QDir::cleanPath(url.toLocalFile());
            // Solo elementos del proyecto, y nunca una carpeta dentro de sí misma
            if (!url.isLocalFile() || !insideProject(src) || samePath(src, m_folder))
                return false;
            if (samePath(src, target) || isAncestor(src, target))
                return false;
        }
        return true;
    }

    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override
    {
        if (!canDropMimeData(data, action, row, column, parent) || !onDrop)
            return false;
        QStringList sources;
        for (const QUrl &url : data->urls())
            sources << QDir::cleanPath(url.toLocalFile());
        const QString target = targetDir(parent);
        // El menú se abre cuando termina el arrastre; se devuelve false para que
        // la vista no intente quitar las filas de origen
        QTimer::singleShot(0, this, [this, sources, target] { onDrop(sources, target); });
        return false;
    }

private:
    QString targetDir(const QModelIndex &parent) const
    {
        auto *fs = static_cast<QFileSystemModel *>(sourceModel());
        if (!parent.isValid())
            return m_folder;
        // Soltar en el hueco bajo la lista (raíz de la vista = carpeta contenedora)
        // equivale a soltar en la carpeta del proyecto
        if (!m_container.isEmpty() && samePath(fs->filePath(mapToSource(parent)), m_container))
            return m_folder;
        const QModelIndex src = mapToSource(parent);
        QString dir = fs->filePath(src);
        if (!fs->isDir(src))
            dir = QFileInfo(dir).absolutePath();
        dir = QDir::cleanPath(dir);
        return insideProject(dir, true) ? dir : QString();
    }

    bool insideProject(const QString &path, bool allowRoot = false) const
    {
        if (m_folder.isEmpty())
            return false;
        const QString p = QDir::cleanPath(path);
        if (samePath(p, m_folder))
            return allowRoot;
        return isAncestor(m_folder, p);
    }

    static bool isAncestor(const QString &ancestor, const QString &path)
    {
        const QString prefix = QDir::cleanPath(ancestor) + QLatin1Char('/');
#ifdef Q_OS_WIN
        return QDir::cleanPath(path).startsWith(prefix, Qt::CaseInsensitive);
#else
        return QDir::cleanPath(path).startsWith(prefix);
#endif
    }
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    QSettings settings;
    m_fontSize = settings.value(QStringLiteral("editor/pointSize"), 10).toInt();
    m_wordWrap = settings.value(QStringLiteral("editor/wordWrap"), false).toBool();
    m_indentWidth = settings.value(QStringLiteral("editor/indentWidth"), 4).toInt();
    m_showLineNumbers = settings.value(QStringLiteral("editor/lineNumbers"), true).toBool();
    m_showMinimap = settings.value(QStringLiteral("editor/minimap"), true).toBool();

    m_git = new GitWorkspace(this);
    connect(m_git, &GitWorkspace::openFileRequested, this, [this](const QString &p) {
        setMode(ModeSwitch::Editor);
        openFile(p);
    });
    connect(m_git, &GitWorkspace::openFolderRequested, this, [this](const QString &p) {
        openFolder(p);
        setMode(ModeSwitch::Editor);
    });
    connect(m_git, &GitWorkspace::statusMessage, this, [this](const QString &m) {
        statusBar()->showMessage(m, 4000);
    });
    connect(m_git, &GitWorkspace::repositoriesChanged, this, &MainWindow::updateWindowTitle);

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(createEditorPage());
    m_pages->addWidget(m_git);
    setCentralWidget(m_pages);

    m_positionLabel = new QLabel(this);
    m_indentLabel = new QLabel(this);
    m_languageButton = new QToolButton(this);
    m_languageButton->setObjectName(QStringLiteral("languageButton"));
    m_languageButton->setAutoRaise(true);
    m_languageButton->setToolTip(tr("Lenguaje del resaltado de sintaxis"));
    connect(m_languageButton, &QToolButton::clicked, this, &MainWindow::showLanguageMenu);
    statusBar()->addWidget(m_positionLabel);
    statusBar()->addPermanentWidget(m_indentLabel);
    statusBar()->addPermanentWidget(m_languageButton);
    statusBar()->setSizeGripEnabled(false);

    // Interruptor de modo en la esquina superior derecha de la barra de menús
    m_modeSwitch = new ModeSwitch(this);
    menuBar()->setCornerWidget(m_modeSwitch, Qt::TopRightCorner);
    connect(m_modeSwitch, &ModeSwitch::modeChanged, this, [this](ModeSwitch::Mode m) { setMode(m); });

    createMenus();

    resize(1400, 860);
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());

    m_fileWatcher = new QFileSystemWatcher(this);
    m_externalTimer = new QTimer(this);
    m_externalTimer->setSingleShot(true);
    m_externalTimer->setInterval(300);
    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
        m_externalPending.insert(path);
        m_externalTimer->start();
    });
    connect(m_externalTimer, &QTimer::timeout, this, [this] {
        const QStringList paths(m_externalPending.cbegin(), m_externalPending.cend());
        m_externalPending.clear();
        checkExternalChanges(paths, true);
    });

    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState s) {
        if (s != Qt::ApplicationActive)
            return;
        checkExternalChanges({}, false);   // por si el sistema no avisó (unidades de red, etc.)
        if (m_pages->currentWidget() == m_git)
            m_git->refreshCurrent();
    });

    updateWindowTitle();
    updateStatus();
}

// ---------------------------------------------------------------------------
// Construcción de la interfaz
// ---------------------------------------------------------------------------
QWidget *MainWindow::createEditorPage()
{
    // --- Explorador de carpetas ---
    m_fsModel = new QFileSystemModel(this);
    m_fsModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    m_fsModel->setReadOnly(true);

    m_fsProxy = new FolderProxyModel(this);
    m_fsProxy->setSourceModel(m_fsModel);
    m_fsProxy->setDynamicSortFilter(true);
    m_fsProxy->setRecursiveFilteringEnabled(false);

    m_tree = new QTreeView(this);
    m_tree->setModel(m_fsProxy);
    m_tree->setHeaderHidden(true);
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(0, Qt::AscendingOrder);
    for (int c = 1; c < 4; ++c)
        m_tree->hideColumn(c);
    m_tree->setAnimated(false);
    m_tree->setIndentation(14);
    m_tree->setUniformRowHeights(true);
    // Arrastrar y soltar archivos/carpetas dentro del proyecto (pregunta mover/copiar)
    m_tree->setDragEnabled(true);
    m_tree->setAcceptDrops(true);
    m_tree->setDropIndicatorShown(true);
    m_tree->setDragDropMode(QAbstractItemView::DragDrop);
    m_tree->setDefaultDropAction(Qt::MoveAction);
    m_tree->setAutoExpandDelay(600);   // al mantener encima una carpeta, se abre
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fsProxy->onDrop = [this](const QStringList &sources, const QString &target) {
        dropIntoFolder(sources, target);
    };
    // QFileSystemModel carga en segundo plano: al cargar la carpeta contenedora
    // se expande la carpeta abierta
    connect(m_fsModel, &QFileSystemModel::directoryLoaded, this, [this](const QString &loaded) {
        if (m_rootPath.isEmpty() || QDir::cleanPath(loaded) != m_containerPath || m_containerPath == m_rootPath)
            return;
        const QModelIndex idx = m_fsProxy->mapFromSource(m_fsModel->index(m_rootPath));
        if (idx.isValid())
            m_tree->expand(idx);
    });
    connect(m_tree, &QTreeView::activated, this, [this](const QModelIndex &proxyIndex) {
        const QModelIndex idx = m_fsProxy->mapToSource(proxyIndex);
        if (!m_fsModel->isDir(idx))
            openFile(m_fsModel->filePath(idx));
    });
    // Clic derecho: renombrar, ejecutar .bat, abrir en el explorador, terminal…
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeView::customContextMenuRequested, this, &MainWindow::showFolderTreeMenu);
    auto *renameShortcut = new QShortcut(QKeySequence(Qt::Key_F2), m_tree, nullptr, nullptr, Qt::WidgetShortcut);
    connect(renameShortcut, &QShortcut::activated, this, [this] {
        const QModelIndex idx = m_fsProxy->mapToSource(m_tree->currentIndex());
        if (idx.isValid())
            renamePath(m_fsModel->filePath(idx));
    });
    auto *deleteShortcut = new QShortcut(QKeySequence::Delete, m_tree, nullptr, nullptr, Qt::WidgetShortcut);
    connect(deleteShortcut, &QShortcut::activated, this, [this] { deletePaths(selectedTreePaths(m_tree->currentIndex())); });

    auto *header = new QLabel(tr("CARPETAS"), this);
    header->setObjectName(QStringLiteral("sidebarHeader"));
    auto *empty = new QLabel(tr("Archivo › Abrir carpeta…\n(Ctrl+Shift+O)"), this);
    empty->setObjectName(QStringLiteral("sidebarEmpty"));
    empty->setAlignment(Qt::AlignCenter);

    m_sidebar = new QWidget(this);
    m_sidebar->setObjectName(QStringLiteral("sidebar"));
    auto *sl = new QVBoxLayout(m_sidebar);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->setSpacing(0);
    sl->addWidget(header);
    sl->addWidget(m_tree, 1);
    sl->addWidget(empty, 1);
    m_tree->hide();

    // --- Pestañas ---
    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->tabBar()->setExpanding(false);
    m_tabs->tabBar()->setElideMode(Qt::ElideNone);
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int i) { closeTab(i); });
    Theme::enableMiddleClickClose(m_tabs);
    Theme::enableTabContextMenu(m_tabs, [this](QMenu *menu, int index) {
        menu->addAction(tr("Cerrar las pestañas guardadas"), this, [this] {
            QList<QPointer<EditorTab>> saved;
            for (int i = 0; i < m_tabs->count(); ++i)
                if (EditorTab *t = tabAt(i); t && !t->isModified())
                    saved << t;
            for (const QPointer<EditorTab> &t : std::as_const(saved))
                if (t && m_tabs->indexOf(t) >= 0)
                    closeTab(m_tabs->indexOf(t));
        });
        EditorTab *tab = tabAt(index);
        if (!tab || tab->kind() != EditorTab::Kind::File || tab->isUntitled())
            return;
        const QString path = tab->filePath();
        menu->addSeparator();
        menu->addAction(tr("Copiar ruta"), this, [path] {
            QGuiApplication::clipboard()->setText(QDir::toNativeSeparators(path));
        });
        if (!m_rootPath.isEmpty() && QDir::cleanPath(path).startsWith(QDir::cleanPath(m_rootPath) + QLatin1Char('/'))) {
            const QString relative = QDir(m_rootPath).relativeFilePath(path);
            menu->addAction(tr("Copiar ruta relativa"), this, [relative] {
                QGuiApplication::clipboard()->setText(QDir::toNativeSeparators(relative));
            });
        }
        menu->addAction(tr("Abrir la carpeta en el explorador"), this, [this, path] { revealInExplorer(path); });
        menu->addAction(tr("Abrir terminal en la carpeta"), this,
                        [this, path] { openTerminal(QFileInfo(path).absolutePath()); });
    });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        updateWindowTitle();
        updateStatus();
    });
    // Doble clic en la zona vacía de la barra de pestañas: archivo nuevo
    connect(m_tabs->tabBar(), &QTabBar::tabBarDoubleClicked, this, [this](int index) {
        if (index < 0)
            newTab();
    });

    // --- Panel "Buscar en archivos" bajo las pestañas ---
    m_findPanel = new FindInFilesPanel(this);
    m_findPanel->setWorkspaceProvider([this] { return findWorkspace(); });
    connect(m_findPanel, &FindInFilesPanel::searchFinished, this, [this](const FindInFiles::Results &r) {
        showFindResults(r, false);
    });
    connect(m_findPanel, &FindInFilesPanel::replaceSearchFinished, this, &MainWindow::applyReplaceInFiles);
    connect(m_findPanel, &FindInFilesPanel::closeRequested, this, [this] {
        m_findPanel->hide();
        if (CodeEditor *e = currentEditor())
            e->setFocus();
    });
    auto *editorArea = new QWidget(this);
    auto *areaLayout = new QVBoxLayout(editorArea);
    areaLayout->setContentsMargins(0, 0, 0, 0);
    areaLayout->setSpacing(0);
    areaLayout->addWidget(m_tabs, 1);
    areaLayout->addWidget(m_findPanel);

    m_editorSplitter = new QSplitter(Qt::Horizontal, this);
    m_editorSplitter->addWidget(m_sidebar);
    m_editorSplitter->addWidget(editorArea);
    m_editorSplitter->setStretchFactor(0, 0);
    m_editorSplitter->setStretchFactor(1, 1);
    m_editorSplitter->setSizes({300, 1100});
    m_editorSplitter->setHandleWidth(1);
    m_editorSplitter->setChildrenCollapsible(false);
    m_editorSplitter->restoreState(QSettings().value(QStringLiteral("window/editorSplitter")).toByteArray());
    return m_editorSplitter;
}

void MainWindow::createMenus()
{
    // En modo Git el editor está oculto: sus atajos no deben tocarlo
    auto onEditor = [this](auto fn) {
        return [this, fn] {
            if (m_pages->currentWidget() == m_git)
                return;
            if (CodeEditor *e = currentEditor())
                fn(e);
        };
    };
    // Las vistas de solo lectura (diff, blame, vista previa) no reclaman Ctrl+C / Ctrl+A, así que
    // llegan aquí: se aplican a la que tiene el foco. Devuelve false si el foco no está en una.
    auto onReadOnlyView = [this](auto fn) {
        return [this, fn] {
            QWidget *focus = QApplication::focusWidget();
            if (auto *t = qobject_cast<QPlainTextEdit *>(focus); t && t->isReadOnly() && t != currentEditor()) {
                fn(t);
                return true;
            }
            if (auto *t = qobject_cast<QTextEdit *>(focus); t && t->isReadOnly()) {
                fn(t);
                return true;
            }
            return false;
        };
    };

    // --- Archivo ------------------------------------------------------------
    QMenu *file = menuBar()->addMenu(tr("&Archivo"));
    addMenuAction(file, tr("Nuevo archivo"), QKeySequence::New, this, [this] {
        setMode(ModeSwitch::Editor);
        newTab();
    });
    addMenuAction(file, tr("Abrir archivo…"), QKeySequence::Open, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Abrir archivo"), m_rootPath);
        if (!files.isEmpty())
            setMode(ModeSwitch::Editor);
        for (const QString &f : files)
            openFile(f);
    });
    addMenuAction(file, tr("Abrir carpeta…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Abrir carpeta"), m_rootPath);
        if (!dir.isEmpty()) {
            openFolder(dir);
            setMode(ModeSwitch::Editor);
        }
    });
    file->addSeparator();
    addMenuAction(file, tr("Guardar"), QKeySequence::Save, this, [this] {
        if (EditorTab *t = currentTab())
            saveTab(t);
    });
    addMenuAction(file, tr("Guardar como…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S), this, [this] {
        if (EditorTab *t = currentTab())
            saveTabAs(t);
    });
    addMenuAction(file, tr("Guardar todo"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S), this, [this] {
        for (int i = 0; i < m_tabs->count(); ++i)
            if (EditorTab *t = tabAt(i); t && t->isModified())
                saveTab(t);
    });
    file->addSeparator();
    addMenuAction(file, tr("Cerrar pestaña"), QKeySequence(Qt::CTRL | Qt::Key_W), this, [this] {
        if (m_pages->currentWidget() == m_git)
            m_git->closeCurrent();
        else if (m_tabs->currentIndex() >= 0)
            closeTab(m_tabs->currentIndex());
    });
    file->addSeparator();
    addMenuAction(file, tr("Preferencias…"), QKeySequence(Qt::CTRL | Qt::Key_Comma), this, &MainWindow::showPreferences);
    file->addSeparator();
    addMenuAction(file, tr("Salir"), QKeySequence(Qt::CTRL | Qt::Key_Q), this, &QWidget::close);

    // --- Editar -------------------------------------------------------------
    QMenu *edit = menuBar()->addMenu(tr("&Editar"));
    addMenuAction(edit, tr("Deshacer"), QKeySequence::Undo, this, onEditor([](CodeEditor *e) { e->undo(); }));
    addMenuAction(edit, tr("Rehacer"), QKeySequence(Qt::CTRL | Qt::Key_Y), this, onEditor([](CodeEditor *e) { e->redo(); }));
    edit->addSeparator();
    addMenuAction(edit, tr("Cortar"), QKeySequence::Cut, this, onEditor([](CodeEditor *e) { e->cutAll(); }));
    addMenuAction(edit, tr("Copiar"), QKeySequence::Copy, this,
                  [editor = onEditor([](CodeEditor *e) { e->copyAll(); }),
                   view = onReadOnlyView([](auto *t) { t->copy(); })] {
                      if (!view())
                          editor();
                  });
    addMenuAction(edit, tr("Pegar"), QKeySequence::Paste, this, onEditor([](CodeEditor *e) { e->pasteAll(); }));
    edit->addSeparator();
    addMenuAction(edit, tr("Comentar / descomentar líneas"), QKeySequence(Qt::CTRL | Qt::Key_Slash), this,
                    onEditor([](CodeEditor *e) { e->toggleLineComment(); }));
    addMenuAction(edit, tr("Duplicar líneas"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D), this,
                    onEditor([](CodeEditor *e) { e->duplicateLines(); }));
    addMenuAction(edit, tr("Aumentar sangría"), QKeySequence(Qt::CTRL | Qt::Key_BracketRight), this,
                    onEditor([](CodeEditor *e) { e->indentSelection(false); }));
    addMenuAction(edit, tr("Reducir sangría"), QKeySequence(Qt::CTRL | Qt::Key_BracketLeft), this,
                    onEditor([](CodeEditor *e) { e->indentSelection(true); }));

    // --- Selección ----------------------------------------------------------
    QMenu *sel = menuBar()->addMenu(tr("&Selección"));
    addMenuAction(sel, tr("Seleccionar todo"), QKeySequence::SelectAll, this,
                  [editor = onEditor([](CodeEditor *e) {
                       e->clearExtraCursors();
                       e->selectAll();
                   }),
                   view = onReadOnlyView([](auto *t) { t->selectAll(); })] {
                      if (!view())
                          editor();
                  });
    sel->addSeparator();
    addMenuAction(sel, tr("Añadir siguiente coincidencia"), QKeySequence(Qt::CTRL | Qt::Key_D), this,
                   onEditor([](CodeEditor *e) { e->addNextOccurrence(); }));
    addMenuAction(sel, tr("Seleccionar todas las coincidencias"), QKeySequence(Qt::ALT | Qt::Key_F3), this,
                   onEditor([](CodeEditor *e) { e->selectAllOccurrencesOfSelection(); }));
    addMenuAction(sel, tr("Dividir selección en líneas"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L), this,
                   onEditor([](CodeEditor *e) { e->splitSelectionIntoLines(); }));
    addMenuAction(sel, tr("Añadir cursor arriba"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Up), this,
                   onEditor([](CodeEditor *e) { e->addCursorAbove(); }));
    addMenuAction(sel, tr("Añadir cursor abajo"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Down), this,
                   onEditor([](CodeEditor *e) { e->addCursorBelow(); }));
    sel->addAction(tr("Volver a un solo cursor  (Esc)"), this,
                   onEditor([](CodeEditor *e) { e->clearExtraCursors(); }));

    // --- Buscar -------------------------------------------------------------
    QMenu *find = menuBar()->addMenu(tr("&Buscar"));
    addMenuAction(find, tr("Buscar…"), QKeySequence::Find, this, [this] {
        if (EditorTab *t = currentTab()) {
            t->setPreviewing(false);   // se busca en el texto del archivo
            t->findBar()->showFind(false);
        }
    });
    addMenuAction(find, tr("Reemplazar…"), QKeySequence(Qt::CTRL | Qt::Key_H), this, [this] {
        if (EditorTab *t = currentTab()) {
            t->setPreviewing(false);
            t->findBar()->showFind(true);
        }
    });
    addMenuAction(find, tr("Buscar siguiente"), QKeySequence(Qt::Key_F3), this,
                    onEditor([](CodeEditor *e) { e->findNext(false); }));
    addMenuAction(find, tr("Buscar anterior"), QKeySequence(Qt::SHIFT | Qt::Key_F3), this,
                    onEditor([](CodeEditor *e) { e->findNext(true); }));
    find->addSeparator();
    addMenuAction(find, tr("Buscar en archivos…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F), this,
                  &MainWindow::showFindInFiles);
    addMenuAction(find, tr("Resultado siguiente"), QKeySequence(Qt::Key_F4), this, [this] { gotoResult(+1); });
    addMenuAction(find, tr("Resultado anterior"), QKeySequence(Qt::SHIFT | Qt::Key_F4), this, [this] { gotoResult(-1); });

    // --- Ir -----------------------------------------------------------------
    QMenu *go = menuBar()->addMenu(tr("&Ir"));
    addMenuAction(go, tr("Ir a archivo…"), QKeySequence(Qt::CTRL | Qt::Key_P), this, &MainWindow::quickOpen);
    addMenuAction(go, tr("Ir a línea…"), QKeySequence(Qt::CTRL | Qt::Key_G), this, &MainWindow::goToLine);
    go->addSeparator();
    addMenuAction(go, tr("Pestaña siguiente"), QKeySequence(Qt::CTRL | Qt::Key_PageDown), this, [this] {
        if (m_tabs->count() > 0)
            m_tabs->setCurrentIndex((m_tabs->currentIndex() + 1) % m_tabs->count());
    });
    addMenuAction(go, tr("Pestaña anterior"), QKeySequence(Qt::CTRL | Qt::Key_PageUp), this, [this] {
        if (m_tabs->count() > 0)
            m_tabs->setCurrentIndex((m_tabs->currentIndex() - 1 + m_tabs->count()) % m_tabs->count());
    });

    // --- Ver ----------------------------------------------------------------
    QMenu *view = menuBar()->addMenu(tr("&Ver"));
    addMenuAction(view, tr("Modo editor"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), this,
                    [this] { setMode(ModeSwitch::Editor); });
    addMenuAction(view, tr("Modo Git"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G), this,
                    [this] { setMode(ModeSwitch::Git); });
    view->addSeparator();
    QAction *sidebar = view->addAction(tr("Barra lateral"));
    sidebar->setCheckable(true);
    sidebar->setChecked(true);
    sidebar->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    connect(sidebar, &QAction::toggled, m_sidebar, &QWidget::setVisible);
    m_minimapAction = view->addAction(tr("Mostrar minimapa"));
    m_minimapAction->setCheckable(true);
    m_minimapAction->setChecked(m_showMinimap);
    m_minimapAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    connect(m_minimapAction, &QAction::toggled, this, [this](bool on) {
        m_showMinimap = on;
        QSettings().setValue(QStringLiteral("editor/minimap"), on);
        applyEditorSettings();
    });
    m_wrapAction = view->addAction(tr("Ajuste de línea"));
    m_wrapAction->setCheckable(true);
    m_wrapAction->setChecked(m_wordWrap);
    m_wrapAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Z));
    connect(m_wrapAction, &QAction::toggled, this, [this](bool on) {
        m_wordWrap = on;
        applyEditorSettings();
    });
    addMenuAction(view, tr("Aumentar fuente"), QKeySequence(Qt::CTRL | Qt::Key_Equal), this, [this] { changeFontSize(+1); });
    addMenuAction(view, tr("Reducir fuente"), QKeySequence(Qt::CTRL | Qt::Key_Minus), this, [this] { changeFontSize(-1); });

    // --- Git ----------------------------------------------------------------
    QMenu *git = menuBar()->addMenu(tr("&Git"));
    git->addAction(tr("Abrir repositorio…"), this, [this] {
        setMode(ModeSwitch::Git);
        m_git->promptOpenRepository();
    });
    addMenuAction(git, tr("Actualizar"), QKeySequence(Qt::Key_F5), this, [this] { m_git->refreshCurrent(); });
    git->addAction(tr("Cerrar repositorio"), this, [this] { m_git->closeCurrent(); });
    git->addSeparator();
    // Operaciones remotas sobre el repositorio de la pestaña actual (cambian al modo Git)
    auto onRepo = [this](void (RepositoryView::*method)()) {
        return [this, method] {
            setMode(ModeSwitch::Git);
            if (RepositoryView *view = m_git->currentView())
                (view->*method)();
            else
                statusBar()->showMessage(tr("Abre un repositorio en el modo Git"), 3000);
        };
    };
    addMenuAction(git, tr("Pull"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P), this, onRepo(&RepositoryView::pull));
    addMenuAction(git, tr("Opciones de pull…"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_P), this,
                  onRepo(&RepositoryView::showPullOptions));
    addMenuAction(git, tr("Push"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_U), this, onRepo(&RepositoryView::push));
    addMenuAction(git, tr("Opciones de push…"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_U), this,
                  onRepo(&RepositoryView::showPushOptions));
    addMenuAction(git, tr("Fetch"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F), this, onRepo(&RepositoryView::fetch));
    addMenuAction(git, tr("Merge…"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_M), this,
                  onRepo(&RepositoryView::showMergePalette));
    git->addSeparator();
    addMenuAction(git, tr("Comandos de Git…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), this,
                  onRepo(&RepositoryView::showCommandPalette));

    // --- Ayuda --------------------------------------------------------------
    QMenu *help = menuBar()->addMenu(tr("A&yuda"));
    addMenuAction(help, tr("Atajos de teclado"), QKeySequence(Qt::Key_F1), this, &MainWindow::showShortcuts);
    help->addAction(tr("Acerca de BatCode"), this, [this] {
        QMessageBox::about(this, tr("Acerca de BatCode"),
                           tr("<h3>BatCode %1</h3>"
                              "<p>Editor de texto con Git integrado.</p>"
                              "<p>Construido con Qt %2 y libgit2.</p>")
                               .arg(QStringLiteral(BATCODE_VERSION), QString::fromLatin1(qVersion())));
    });
}

// ---------------------------------------------------------------------------
// Modos
// ---------------------------------------------------------------------------
void MainWindow::setMode(ModeSwitch::Mode mode)
{
    m_modeSwitch->setMode(mode); // sincroniza el interruptor (no reemite si no cambia)
    const bool git = mode == ModeSwitch::Git;
    QWidget *target = git ? static_cast<QWidget *>(m_git) : m_editorSplitter;
    if (m_pages->currentWidget() != target) {
        m_pages->setCurrentWidget(target);
        if (git)
            m_git->refreshCurrent();
        else if (CodeEditor *e = currentEditor())
            e->setFocus();
    }
    statusBar()->setVisible(!git);
    updateWindowTitle();
    updateStatus();
}

// ---------------------------------------------------------------------------
// Pestañas
// ---------------------------------------------------------------------------
EditorTab *MainWindow::tabAt(int index) const
{
    return qobject_cast<EditorTab *>(m_tabs->widget(index));
}

EditorTab *MainWindow::currentTab() const
{
    if (m_pages && m_pages->currentWidget() == m_git)
        return nullptr;
    return qobject_cast<EditorTab *>(m_tabs->currentWidget());
}

CodeEditor *MainWindow::currentEditor() const
{
    EditorTab *t = currentTab();
    return t ? t->editor() : nullptr;
}

EditorTab *MainWindow::newTab()
{
    auto *tab = new EditorTab(EditorTab::Kind::File, m_tabs);
    connect(tab->editor(), &CodeEditor::fontSizeStepRequested, this, &MainWindow::changeFontSize);
    tab->setUntitledIndex(++m_untitledCounter);
    applyEditorSettings(tab);

    connect(tab, &EditorTab::titleChanged, this, [this, tab] {
        updateTabTitle(tab);
        updateWindowTitle();
        updateStatus();
        syncWatchedFiles();
    });
    connect(tab->editor(), &CodeEditor::cursorsChanged, this, [this, tab] {
        if (tab == currentTab())
            updateStatus();
    });

    const int index = m_tabs->addTab(tab, tab->displayName());
    m_tabs->setCurrentIndex(index);
    updateTabTitle(tab);
    tab->editor()->setFocus();
    return tab;
}

void MainWindow::updateTabTitle(EditorTab *tab)
{
    const int index = m_tabs->indexOf(tab);
    if (index < 0)
        return;
    QString title = tab->displayName();
    if (tab->isModified())
        title += QStringLiteral("  ●");
    m_tabs->setTabText(index, title);
    m_tabs->setTabToolTip(index, tab->filePath());
}

void MainWindow::updateWindowTitle()
{
    QString title = QStringLiteral("BatCode");
    if (m_pages && m_pages->currentWidget() == m_git) {
        const QString repo = m_git->currentRepositoryPath();
        if (!repo.isEmpty())
            title = QDir::toNativeSeparators(repo) + QStringLiteral(" — Git — ") + title;
        setWindowTitle(title);
        return;
    }
    if (!m_rootPath.isEmpty())
        title = QFileInfo(m_rootPath).fileName() + QStringLiteral(" — ") + title;
    if (EditorTab *t = currentTab()) {
        const QString name = t->isUntitled() ? t->displayName() : QDir::toNativeSeparators(t->filePath());
        title = name + (t->isModified() ? QStringLiteral(" ●") : QString()) + QStringLiteral(" — ") + title;
    }
    setWindowTitle(title);
}

void MainWindow::updateStatus()
{
    CodeEditor *e = currentEditor();
    if (!e) {
        m_positionLabel->clear();
        m_indentLabel->clear();
        m_languageButton->hide();
        return;
    }
    const QTextCursor c = e->textCursor();
    QString pos = tr("Línea %1, Columna %2").arg(c.blockNumber() + 1).arg(c.positionInBlock() + 1);
    if (e->cursorCount() > 1) {
        pos += tr("  ·  %1 cursores").arg(e->cursorCount());
    } else if (c.hasSelection()) {
        const int n = c.selectionEnd() - c.selectionStart();
        pos += n == 1 ? tr("  ·  1 carácter seleccionado") : tr("  ·  %1 caracteres seleccionados").arg(n);
    }
    m_positionLabel->setText(pos);
    m_indentLabel->setText(tr("Espacios: %1").arg(e->indentWidth()));
    EditorTab *tab = currentTab();
    m_languageButton->setText(tab->languageName());
    m_languageButton->setEnabled(tab->kind() == EditorTab::Kind::File);
    m_languageButton->show();
}

void MainWindow::showLanguageMenu()
{
    EditorTab *tab = currentTab();
    if (!tab || tab->kind() != EditorTab::Kind::File)
        return;
    QMenu menu(this);
    auto *group = new QActionGroup(&menu);
    auto add = [&](const QString &text, const QString &name, bool checked) {
        QAction *a = menu.addAction(text, this, [this, tab = QPointer<EditorTab>(tab), name] {
            if (!tab)
                return;
            tab->setLanguageOverride(name);
            updateStatus();
        });
        a->setCheckable(true);
        a->setChecked(checked);
        group->addAction(a);
    };
    const QString override = tab->languageOverride();
    add(tr("Detectar automáticamente"), QString(), override.isEmpty());
    menu.addSeparator();
    add(Languages::plainText().name, Languages::plainText().name, override == Languages::plainText().name);
    QStringList names;
    for (const LanguageDefinition &d : Languages::all())
        names << d.name;
    names.sort(Qt::CaseInsensitive);
    for (const QString &name : std::as_const(names))
        add(name, name, override == name);
    menu.exec(m_languageButton->mapToGlobal(QPoint(0, 0)) - QPoint(0, menu.sizeHint().height()));
}

bool MainWindow::openFile(const QString &path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < m_tabs->count(); ++i) {
        EditorTab *t = tabAt(i);
        if (t && t->filePath() == absolute) {
            m_tabs->setCurrentIndex(i);
            t->editor()->setFocus();
            return true;
        }
    }

    // Reutiliza una pestaña "sin título" vacía y sin cambios
    EditorTab *tab = qobject_cast<EditorTab *>(m_tabs->currentWidget());
    const bool reuse = tab && tab->isUntitled() && !tab->isModified() && tab->editor()->document()->isEmpty();
    if (!reuse)
        tab = newTab();

    QString error;
    if (!tab->load(absolute, &error)) {
        if (!reuse) {
            m_tabs->removeTab(m_tabs->indexOf(tab));
            tab->deleteLater();
        }
        QMessageBox::warning(this, tr("Abrir archivo"), tr("No se pudo abrir %1:\n%2").arg(absolute, error));
        return false;
    }
    updateTabTitle(tab);
    updateWindowTitle();
    updateStatus();
    tab->editor()->setFocus();
    return true;
}

void MainWindow::openFolder(const QString &path)
{
    const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (!QFileInfo(absolute).isDir())
        return;
    m_rootPath = absolute;

    // La carpeta se muestra como nodo raíz dentro de su carpeta contenedora
    QDir parentDir(absolute);
    const bool hasParent = parentDir.cdUp() && QDir::cleanPath(parentDir.absolutePath()) != absolute;
    const QString container = hasParent ? QDir::cleanPath(parentDir.absolutePath()) : absolute;
    m_fsProxy->setFolder(absolute, container);
    const QModelIndex containerIdx = m_fsModel->setRootPath(container);
    m_fsModel->index(absolute); // fuerza la carga del nodo
    m_tree->setRootIndex(m_fsProxy->mapFromSource(containerIdx));
    m_containerPath = container;
    if (hasParent)
        m_tree->expand(m_fsProxy->mapFromSource(m_fsModel->index(absolute)));

    m_tree->show();
    if (QWidget *empty = m_sidebar->findChild<QLabel *>(QStringLiteral("sidebarEmpty")))
        empty->hide();

    // Si la carpeta es (o está dentro de) un repositorio, se añade al modo Git
    if (!m_git->hasRepositoryFor(absolute))
        m_git->openRepository(absolute, false);
    updateWindowTitle();
}

void MainWindow::openPaths(const QStringList &paths)
{
    for (const QString &p : paths) {
        const QFileInfo info(p);
        if (info.isDir())
            openFolder(p);
        else
            openFile(p);
    }
}

bool MainWindow::closeTab(int index)
{
    EditorTab *tab = tabAt(index);
    if (!tab)
        return true;
    if (!maybeSave(tab))
        return false;
    m_tabs->removeTab(index);
    tab->deleteLater();
    updateWindowTitle();
    updateStatus();
    syncWatchedFiles();
    return true;
}

void MainWindow::syncWatchedFiles()
{
    QSet<QString> wanted;
    for (int i = 0; i < m_tabs->count(); ++i)
        if (EditorTab *t = tabAt(i); t && t->kind() == EditorTab::Kind::File && !t->isUntitled())
            wanted.insert(t->filePath());
    const QStringList watched = m_fileWatcher->files();
    QStringList remove;
    for (const QString &p : watched)
        if (!wanted.contains(p))
            remove << p;
    if (!remove.isEmpty())
        m_fileWatcher->removePaths(remove);
    QStringList add;
    for (const QString &p : std::as_const(wanted))
        if (!watched.contains(p) && QFileInfo::exists(p))
            add << p;
    if (!add.isEmpty())
        m_fileWatcher->addPaths(add);
}

void MainWindow::checkExternalChanges(const QStringList &paths, bool notifyKept)
{
    QStringList reloaded, kept;
    for (int i = 0; i < m_tabs->count(); ++i) {
        EditorTab *t = tabAt(i);
        if (!t || t->kind() != EditorTab::Kind::File || t->isUntitled())
            continue;
        if (!paths.isEmpty() && !paths.contains(t->filePath()))
            continue;
        if (t->diskState() != EditorTab::DiskState::Changed)
            continue;
        const QString name = QFileInfo(t->filePath()).fileName();
        // Con cambios sin guardar no se toca nada, para no perder trabajo
        if (t->isModified()) {
            kept << name;
            continue;
        }
        QString error;
        if (t->reloadFromDisk(&error))
            reloaded << name;
    }
    // Guardar con "renombrar encima" (muchos editores, git) hace que se deje de vigilar el archivo
    syncWatchedFiles();

    if (!kept.isEmpty() && notifyKept)
        statusBar()->showMessage(tr("%1 cambió fuera del editor; no se recarga porque tiene cambios sin guardar")
                                     .arg(kept.join(QStringLiteral(", "))),
                                 8000);
    else if (!reloaded.isEmpty())
        statusBar()->showMessage(tr("Recargado %1 (cambió fuera del editor)").arg(reloaded.join(QStringLiteral(", "))),
                                 4000);
}

bool MainWindow::maybeSave(EditorTab *tab)
{
    if (!tab->isModified())
        return true;
    setMode(ModeSwitch::Editor);
    m_tabs->setCurrentWidget(tab);
    const auto answer = QMessageBox::question(
        this, tr("Cambios sin guardar"),
        tr("¿Guardar los cambios de «%1» antes de cerrar?").arg(tab->displayName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Save)
        return saveTab(tab);
    return true;
}

bool MainWindow::saveTab(EditorTab *tab)
{
    if (tab->kind() != EditorTab::Kind::File)
        return true;
    if (tab->isUntitled())
        return saveTabAs(tab);
    QString error;
    if (!tab->save(&error)) {
        QMessageBox::warning(this, tr("Guardar"), tr("No se pudo guardar:\n%1").arg(error));
        return false;
    }
    statusBar()->showMessage(tr("Guardado %1").arg(QDir::toNativeSeparators(tab->filePath())), 2500);
    m_git->refreshRepositoryContaining(tab->filePath());
    return true;
}

bool MainWindow::saveTabAs(EditorTab *tab)
{
    if (tab->kind() != EditorTab::Kind::File)
        return true;
    const QString start = tab->isUntitled() ? m_rootPath : tab->filePath();
    const QString path = QFileDialog::getSaveFileName(this, tr("Guardar como"), start);
    if (path.isEmpty())
        return false;
    QString error;
    if (!tab->saveAs(path, &error)) {
        QMessageBox::warning(this, tr("Guardar"), tr("No se pudo guardar:\n%1").arg(error));
        return false;
    }
    m_git->refreshRepositoryContaining(tab->filePath());
    return true;
}

// ---------------------------------------------------------------------------
// Diálogos y utilidades
// ---------------------------------------------------------------------------
void MainWindow::quickOpen()
{
    QString root = m_rootPath;
    if (root.isEmpty()) {
        if (EditorTab *t = currentTab(); t && !t->isUntitled())
            root = QFileInfo(t->filePath()).absolutePath();
    }
    if (root.isEmpty()) {
        statusBar()->showMessage(tr("Abre una carpeta para usar «Ir a archivo»"), 3000);
        return;
    }
    QuickOpenDialog dialog(root, this);
    const QPoint topCenter = mapToGlobal(QPoint(width() / 2, 0));
    dialog.move(topCenter.x() - dialog.minimumWidth() / 2, topCenter.y() + 60);
    if (dialog.exec() == QDialog::Accepted && !dialog.selectedFile().isEmpty()) {
        setMode(ModeSwitch::Editor);
        openFile(dialog.selectedFile());
    }
}

void MainWindow::goToLine()
{
    CodeEditor *e = currentEditor();
    if (!e)
        return;
    bool ok = false;
    const int line = QInputDialog::getInt(this, tr("Ir a línea"),
                                          tr("Línea (1 – %1):").arg(e->blockCount()),
                                          e->textCursor().blockNumber() + 1, 1, e->blockCount(), 1, &ok);
    if (ok)
        e->goToLine(line);
}

void MainWindow::changeFontSize(int delta)
{
    const int size = qBound(6, m_fontSize + delta, 48);
    if (size == m_fontSize)
        return;
    m_fontSize = size;
    const QFont f = Theme::editorFont(m_fontSize);
    for (int i = 0; i < m_tabs->count(); ++i)
        if (EditorTab *t = tabAt(i))
            t->editor()->setFont(f);
    QSettings().setValue(QStringLiteral("editor/pointSize"), m_fontSize);   // se recuerda al reiniciar
    statusBar()->showMessage(tr("Tamaño de fuente: %1 pt").arg(m_fontSize), 1500);
}

void MainWindow::showShortcuts()
{
    const QString html = tr(R"(
<h3>Atajos de BatCode</h3>
<table cellspacing="4">
<tr><td><b>Ctrl+Shift+E / Ctrl+Shift+G</b></td><td>Modo editor / Modo Git</td></tr>
<tr><td><b>Ctrl+P</b></td><td>Ir a archivo</td></tr>
<tr><td><b>Ctrl+G</b></td><td>Ir a línea</td></tr>
<tr><td><b>Ctrl+F / Ctrl+H</b></td><td>Buscar / Reemplazar (Alt+Enter: un cursor por coincidencia)</td></tr>
<tr><td><b>Ctrl+Shift+F</b></td><td>Buscar en archivos (resultados en una pestaña; Enter o doble clic abre la línea)</td></tr>
<tr><td><b>F4 / Shift+F4</b></td><td>Resultado siguiente / anterior</td></tr>
<tr><td><b>Ctrl+Clic</b></td><td>Añadir o quitar un cursor</td></tr>
<tr><td><b>Ctrl+D</b></td><td>Seleccionar palabra / añadir siguiente coincidencia</td></tr>
<tr><td><b>Alt+F3</b></td><td>Seleccionar todas las coincidencias</td></tr>
<tr><td><b>Ctrl+Shift+L</b></td><td>Dividir selección en líneas</td></tr>
<tr><td><b>Ctrl+Alt+↑ / ↓</b></td><td>Añadir cursor arriba / abajo</td></tr>
<tr><td><b>Esc</b></td><td>Volver a un solo cursor</td></tr>
<tr><td><b>Ctrl+/</b></td><td>Comentar / descomentar</td></tr>
<tr><td><b>Ctrl+rueda del ratón</b></td><td>Aumentar / reducir el tamaño de la fuente</td></tr>
<tr><td><b>Ctrl+Shift+D</b></td><td>Duplicar líneas</td></tr>
<tr><td><b>Ctrl+Shift+↑ / ↓</b></td><td>Mover la línea o las líneas seleccionadas arriba / abajo</td></tr>
<tr><td><b>Tab / Shift+Tab</b></td><td>Sangrar / quitar sangría</td></tr>
<tr><td><b>Clic central en una pestaña</b></td><td>Cerrar la pestaña (archivo o repositorio)</td></tr>
<tr><td><b>Ctrl+Shift+M</b></td><td>Mostrar / ocultar el minimapa (clic: ir a esa línea; arrastrar: desplazar)</td></tr>
<tr><td><b>Ctrl+B</b></td><td>Mostrar / ocultar el explorador de carpetas</td></tr>
<tr><td><b>F5</b></td><td>Actualizar el repositorio (modo Git)</td></tr>
<tr><td><b>Ctrl+Alt+P / Ctrl+Alt+Shift+P</b></td><td>Pull / opciones de pull</td></tr>
<tr><td><b>Ctrl+Alt+U / Ctrl+Alt+Shift+U</b></td><td>Push / opciones de push</td></tr>
<tr><td><b>Ctrl+Alt+F</b></td><td>Fetch</td></tr>
<tr><td><b>Ctrl+Alt+M</b></td><td>Merge (elegir rama)</td></tr>
<tr><td><b>Ctrl+Shift+P</b></td><td>Comandos de Git (stash, rebase, crear rama…)</td></tr>
<tr><td><b>Ctrl+Enter</b></td><td>Commit (en el mensaje)</td></tr>
<tr><td><b>Doble clic en una rama</b></td><td>Cambiar a esa rama</td></tr>
<tr><td><b>Ctrl+,</b></td><td>Preferencias (idioma, editor, Git)</td></tr>
</table>)");
    QMessageBox box(this);
    box.setWindowTitle(tr("Atajos de teclado"));
    box.setTextFormat(Qt::RichText);
    box.setText(html);
    box.exec();
}

// ---------------------------------------------------------------------------
// Sesión
// ---------------------------------------------------------------------------
void MainWindow::saveSession()
{
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/editorSplitter"), m_editorSplitter->saveState());
    settings.setValue(QStringLiteral("editor/pointSize"), m_fontSize);
    settings.setValue(QStringLiteral("editor/wordWrap"), m_wordWrap);
    settings.setValue(QStringLiteral("editor/indentWidth"), m_indentWidth);
    settings.setValue(QStringLiteral("editor/lineNumbers"), m_showLineNumbers);
    settings.setValue(QStringLiteral("session/folder"), m_rootPath);
    settings.setValue(QStringLiteral("session/mode"), int(m_modeSwitch->mode()));
    settings.setValue(QStringLiteral("session/repositories"), m_git->repositoryPaths());
    settings.setValue(QStringLiteral("session/currentRepository"), m_git->currentRepositoryPath());
    QStringList files;
    for (int i = 0; i < m_tabs->count(); ++i)
        if (EditorTab *t = tabAt(i); t && !t->isUntitled())
            files << t->filePath();
    settings.setValue(QStringLiteral("session/files"), files);
    if (auto *t = qobject_cast<EditorTab *>(m_tabs->currentWidget()))
        settings.setValue(QStringLiteral("session/current"), t->filePath());
}

void MainWindow::restoreSession()
{
    QSettings settings;
    for (const QString &repo : settings.value(QStringLiteral("session/repositories")).toStringList())
        if (QFileInfo(repo).isDir())
            m_git->openRepository(repo, false);
    const QString folder = settings.value(QStringLiteral("session/folder")).toString();
    if (!folder.isEmpty() && QFileInfo(folder).isDir())
        openFolder(folder);
    for (const QString &f : settings.value(QStringLiteral("session/files")).toStringList())
        if (QFileInfo::exists(f))
            openFile(f);
    const QString current = settings.value(QStringLiteral("session/current")).toString();
    if (!current.isEmpty() && QFileInfo::exists(current))
        openFile(current);
    m_git->setCurrentRepository(settings.value(QStringLiteral("session/currentRepository")).toString());
    setMode(ModeSwitch::Mode(settings.value(QStringLiteral("session/mode"), 0).toInt()));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        EditorTab *t = tabAt(i);
        if (t && !maybeSave(t)) {
            event->ignore();
            return;
        }
    }
    saveSession();
    event->accept();
}

// ---------------------------------------------------------------------------
// Preferencias
// ---------------------------------------------------------------------------
void MainWindow::applyEditorSettings(EditorTab *only)
{
    const QFont font = Theme::editorFont(m_fontSize);
    auto apply = [&](EditorTab *t) {
        CodeEditor *e = t->editor();
        e->setFont(font);
        e->setIndentWidth(m_indentWidth);
        e->setShowLineNumbers(m_showLineNumbers);
        e->setShowMinimap(m_showMinimap);
        e->setLineWrapMode(m_wordWrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    };
    if (only) {
        apply(only);   // pestaña nueva, quizá aún no añadida
    } else {
        for (int i = 0; i < m_tabs->count(); ++i)
            if (EditorTab *t = tabAt(i))
                apply(t);
    }
    if (m_minimapAction && m_minimapAction->isChecked() != m_showMinimap) {
        const QSignalBlocker blocker(m_minimapAction);
        m_minimapAction->setChecked(m_showMinimap);
    }
    if (m_wrapAction && m_wrapAction->isChecked() != m_wordWrap) {
        const QSignalBlocker blocker(m_wrapAction);
        m_wrapAction->setChecked(m_wordWrap);
    }
    updateStatus();
}

void MainWindow::showPreferences()
{
    QSettings settings;
    PreferencesDialog::Values current;
    current.language = I18n::currentLanguage();
    current.fontSize = m_fontSize;
    current.indentWidth = m_indentWidth;
    current.wordWrap = m_wordWrap;
    current.lineNumbers = m_showLineNumbers;
    current.minimap = m_showMinimap;
    current.pullCommand = settings.value(QStringLiteral("git/pullCommand"), QStringLiteral("pull")).toString();
    current.pushCommand = settings.value(QStringLiteral("git/pushCommand"), QStringLiteral("push")).toString();

    PreferencesDialog dialog(current, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const PreferencesDialog::Values v = dialog.values();

    m_fontSize = v.fontSize;
    m_indentWidth = v.indentWidth;
    m_wordWrap = v.wordWrap;
    m_showLineNumbers = v.lineNumbers;
    m_showMinimap = v.minimap;
    applyEditorSettings();
    settings.setValue(QStringLiteral("editor/pointSize"), m_fontSize);
    settings.setValue(QStringLiteral("editor/indentWidth"), m_indentWidth);
    settings.setValue(QStringLiteral("editor/wordWrap"), m_wordWrap);
    settings.setValue(QStringLiteral("editor/lineNumbers"), m_showLineNumbers);
    settings.setValue(QStringLiteral("editor/minimap"), m_showMinimap);
    settings.setValue(QStringLiteral("git/pullCommand"), v.pullCommand);
    settings.setValue(QStringLiteral("git/pushCommand"), v.pushCommand);
    m_git->refreshCurrent(); // actualiza los textos de ayuda de los botones Pull/Push

    if (v.language != current.language) {
        I18n::setLanguage(v.language);
        const auto answer = QMessageBox::question(
            this, tr("Cambiar idioma"),
            tr("El idioma se aplicará al reiniciar BatCode.\n\n¿Reiniciar ahora? Se conservan las carpetas, "
               "los archivos y los repositorios abiertos."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer == QMessageBox::Yes)
            restartApplication();
    }
}

void MainWindow::restartApplication()
{
    // close() pasa por closeEvent: pregunta por los cambios sin guardar y guarda la sesión
    if (!close())
        return;
    QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
    qApp->quit();
}

// ---------------------------------------------------------------------------
// Buscar en archivos
// ---------------------------------------------------------------------------
FindInFiles::Workspace MainWindow::findWorkspace() const
{
    FindInFiles::Workspace ws;
    if (!m_rootPath.isEmpty())
        ws.openFolders << m_rootPath;
    for (int i = 0; i < m_tabs->count(); ++i) {
        EditorTab *t = tabAt(i);
        if (!t || t->kind() != EditorTab::Kind::File || t->isUntitled())
            continue;
        ws.openFiles << t->filePath();
        ws.buffers.insert(QDir::cleanPath(t->filePath()), t->editor()->toPlainText());
    }
    return ws;
}

void MainWindow::showFindInFiles()
{
    setMode(ModeSwitch::Editor);
    QString initial;
    if (CodeEditor *e = currentEditor()) {
        const QString sel = e->textCursor().selectedText();
        if (!sel.isEmpty() && !sel.contains(QChar::ParagraphSeparator))
            initial = sel;
    }
    m_findPanel->activate(initial);
}

EditorTab *MainWindow::resultsTab(bool forceNew)
{
    if (!forceNew && m_resultsTab)
        return m_resultsTab;
    auto *tab = new EditorTab(EditorTab::Kind::FindResults, m_tabs);
    connect(tab->editor(), &CodeEditor::fontSizeStepRequested, this, &MainWindow::changeFontSize);
    tab->editor()->setFont(Theme::editorFont(m_fontSize));
    connect(tab, &EditorTab::locationActivated, this, [this, tab](const FindInFiles::Location &loc) {
        m_resultsTab = tab;
        m_resultIndex = tab->matchIndexAtCursor();
        openLocation(loc);
    });
    connect(tab, &EditorTab::titleChanged, this, [this, tab] { updateTabTitle(tab); });
    m_tabs->addTab(tab, tab->displayName());
    m_resultsTab = tab;
    return tab;
}

void MainWindow::showFindResults(const FindInFiles::Results &results, bool replaced, int replacedFiles)
{
    const FindInFiles::Options &o = results.options;
    QStringList flags;
    if (o.regex)
        flags << tr("expresión regular");
    if (o.caseSensitive)
        flags << tr("distingue mayúsculas");
    if (o.wholeWord)
        flags << tr("palabra completa");
    QString title = replaced
        ? tr("Reemplazado \"%1\" por \"%2\" en %3 archivos").arg(o.pattern, o.replacement).arg(replacedFiles)
        : tr("Buscando \"%1\" en %2 archivos").arg(o.pattern).arg(results.filesSearched);
    if (!flags.isEmpty())
        title += QStringLiteral(" (%1)").arg(flags.join(QStringLiteral(", ")));

    FindInFiles::Document doc = FindInFiles::buildDocument(results, title);
    QString footer = results.totalMatches == 1 ? tr("1 coincidencia en 1 archivo")
                                               : tr("%1 coincidencias en %2 archivos").arg(results.totalMatches).arg(results.files.size());
    if (results.truncated)
        footer += tr(" (límite alcanzado: la búsqueda se detuvo)");
    if (results.cancelled)
        footer += tr(" (búsqueda detenida)");
    doc.text += QLatin1Char('\n') + footer + QLatin1Char('\n');

    EditorTab *tab = resultsTab(m_findPanel->resultsInNewTab());
    tab->setFindResults(doc);
    m_tabs->setCurrentWidget(tab);
    updateTabTitle(tab);
    m_resultIndex = -1;
    statusBar()->showMessage(footer, 5000);
}

void MainWindow::openLocation(const FindInFiles::Location &loc)
{
    if (!openFile(loc.path))
        return;
    CodeEditor *e = currentEditor();
    if (!e)
        return;
    const QTextBlock block = e->document()->findBlockByNumber(qMax(0, loc.line - 1));
    if (!block.isValid())
        return;
    QTextCursor c(block);
    const int start = block.position() + qMin(loc.column, block.length() - 1);
    c.setPosition(start);
    if (loc.length > 0)
        c.setPosition(qMin(start + loc.length, block.position() + block.length() - 1), QTextCursor::KeepAnchor);
    e->clearExtraCursors();
    e->setTextCursor(c);
    e->centerCursor();
    e->setFocus();
}

void MainWindow::gotoResult(int step)
{
    if (!m_resultsTab)
        return;
    const QList<FindInFiles::Location> matches = m_resultsTab->matchLocations();
    if (matches.isEmpty())
        return;
    m_resultIndex = (m_resultIndex < 0 && step < 0) ? int(matches.size()) - 1
                                                     : (m_resultIndex + step + int(matches.size())) % int(matches.size());
    m_resultsTab->revealMatch(m_resultIndex);
    setMode(ModeSwitch::Editor);
    openLocation(matches.at(m_resultIndex));
    statusBar()->showMessage(tr("Resultado %1 de %2").arg(m_resultIndex + 1).arg(matches.size()), 3000);
}

void MainWindow::applyReplaceInFiles(const FindInFiles::Results &results)
{
    const FindInFiles::Options &o = results.options;
    if (results.totalMatches == 0) {
        QMessageBox::information(this, tr("Reemplazar en archivos"), tr("No se encontró «%1»: no hay nada que reemplazar.").arg(o.pattern));
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Reemplazar en archivos"),
        tr("¿Reemplazar %1 coincidencias de «%2» por «%3» en %4 archivos?\n\n"
           "Los archivos abiertos se cambian en el editor (sin guardar, se puede deshacer); "
           "los demás se guardan directamente en el disco.")
            .arg(results.totalMatches).arg(o.pattern, o.replacement).arg(results.files.size()),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;

    const QRegularExpression regex = FindInFiles::buildRegex(o);
    int changedFiles = 0;
    QStringList failed;
    for (const FindInFiles::FileResult &fr : results.files) {
        // ¿Está abierto? Se cambia el contenido del editor como un único paso de deshacer
        EditorTab *open = nullptr;
        for (int i = 0; i < m_tabs->count(); ++i)
            if (EditorTab *t = tabAt(i); t && t->kind() == EditorTab::Kind::File
                                          && QDir::cleanPath(t->filePath()) == QDir::cleanPath(fr.path))
                open = t;
        if (open) {
            QString text = open->editor()->toPlainText();
            if (FindInFiles::replaceInText(text, regex, o) > 0) {
                QTextCursor c(open->editor()->document());
                c.beginEditBlock();
                c.select(QTextCursor::Document);
                c.insertText(text);
                c.endEditBlock();
                ++changedFiles;
            }
            continue;
        }
        QFile file(fr.path);
        if (!file.open(QIODevice::ReadOnly)) {
            failed << fr.path;
            continue;
        }
        const QByteArray data = file.readAll();
        file.close();
        QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
        QString text = decoder.decode(data);
        if (decoder.hasError()) { // no es UTF-8: no se toca para no estropear la codificación
            failed << fr.path;
            continue;
        }
        const bool bom = text.startsWith(QChar(0xFEFF));
        if (bom)
            text.remove(0, 1);
        if (FindInFiles::replaceInText(text, regex, o) == 0)
            continue;
        QSaveFile out(fr.path);
        if (!out.open(QIODevice::WriteOnly)) {
            failed << fr.path;
            continue;
        }
        if (bom)
            out.write("\xEF\xBB\xBF");
        out.write(text.toUtf8());
        if (!out.commit())
            failed << fr.path;
        else
            ++changedFiles;
    }
    showFindResults(results, true, changedFiles);
    if (!failed.isEmpty())
        QMessageBox::warning(this, tr("Reemplazar en archivos"),
                             tr("No se pudieron cambiar estos archivos (sin permiso o no son UTF-8):\n\n%1")
                                 .arg(failed.join(QLatin1Char('\n'))));
    if (!m_rootPath.isEmpty())
        m_git->refreshRepositoryContaining(m_rootPath + QStringLiteral("/x"));
}

// ---------------------------------------------------------------------------
// Menú contextual del explorador de carpetas
// ---------------------------------------------------------------------------
namespace {
bool isRunnableScript(const QFileInfo &info)
{
    const QString ext = info.suffix().toLower();
#ifdef Q_OS_WIN
    return ext == QLatin1String("bat") || ext == QLatin1String("cmd");
#else
    return ext == QLatin1String("sh");
#endif
}

#ifdef Q_OS_WIN
// Lanza un programa de consola en una ventana propia (sin heredar la de BatCode)
bool startInNewConsole(const QString &program, const QString &nativeArgs, const QString &workdir)
{
    QProcess p;
    p.setProgram(program);
    p.setNativeArguments(nativeArgs);
    p.setWorkingDirectory(workdir);
    p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) {
        a->flags |= CREATE_NEW_CONSOLE;
        a->flags &= ~DWORD(CREATE_NO_WINDOW);
    });
    return p.startDetached();
}
#else
// Primer emulador de terminal disponible en Linux
QString linuxTerminal()
{
    const QString preferred = qEnvironmentVariable("TERMINAL");
    if (!preferred.isEmpty() && !QStandardPaths::findExecutable(preferred).isEmpty())
        return preferred;
    for (const char *t : {"x-terminal-emulator", "gnome-terminal", "konsole", "xfce4-terminal", "kitty",
                          "alacritty", "wezterm", "tilix", "xterm"})
        if (!QStandardPaths::findExecutable(QLatin1String(t)).isEmpty())
            return QLatin1String(t);
    return QString();
}
#endif
} // namespace

void MainWindow::showFolderTreeMenu(const QPoint &pos)
{
    const QModelIndex proxyIndex = m_tree->indexAt(pos);
    if (!proxyIndex.isValid()) {
        // Zona vacía del panel: crear en la carpeta abierta
        if (m_rootPath.isEmpty())
            return;
        QMenu menu(this);
        menu.addAction(tr("Nuevo archivo…"), this, [this] { createFileIn(m_rootPath); });
        menu.addAction(tr("Nueva carpeta…"), this, [this] { createFolderIn(m_rootPath); });
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
        return;
    }
    m_tree->setCurrentIndex(proxyIndex);
    const QString path = m_fsModel->filePath(m_fsProxy->mapToSource(proxyIndex));
    const QFileInfo info(path);

    QMenu menu(this);
    if (info.isDir()) {
        menu.addAction(tr("Nuevo archivo…"), this, [this, path] { createFileIn(path); });
        menu.addAction(tr("Nueva carpeta…"), this, [this, path] { createFolderIn(path); });
        menu.addSeparator();
        menu.addAction(tr("Abrir terminal aquí"), this, [this, path] { openTerminal(path); });
        menu.addAction(tr("Abrir en el explorador"), this, [this, path] { revealInExplorer(path); });
        menu.addSeparator();
        QAction *rename = menu.addAction(tr("Renombrar…"), this, [this, path] { renamePath(path); });
        rename->setShortcut(QKeySequence(Qt::Key_F2));
    } else {
        menu.addAction(tr("Abrir"), this, [this, path] { openFile(path); });
        if (isRunnableScript(info))
            menu.addAction(tr("Ejecutar"), this, [this, path] { runScript(path); });
        menu.addAction(tr("Abrir la carpeta en el explorador"), this, [this, path] { revealInExplorer(path); });
        menu.addAction(tr("Abrir terminal en la carpeta"), this,
                       [this, info] { openTerminal(info.absolutePath()); });
        menu.addAction(tr("Nuevo archivo en esta carpeta…"), this,
                       [this, info] { createFileIn(info.absolutePath()); });
        menu.addAction(tr("Nueva carpeta en esta carpeta…"), this,
                       [this, info] { createFolderIn(info.absolutePath()); });
        menu.addSeparator();
        QAction *rename = menu.addAction(tr("Renombrar…"), this, [this, path] { renamePath(path); });
        rename->setShortcut(QKeySequence(Qt::Key_F2));
    }
    const QStringList toDelete = selectedTreePaths(proxyIndex);
    QAction *remove = menu.addAction(toDelete.size() > 1 ? tr("Eliminar %1 elementos…").arg(toDelete.size())
                                                         : tr("Eliminar…"),
                                     this, [this, toDelete] { deletePaths(toDelete); });
    remove->setShortcut(QKeySequence::Delete);
    remove->setEnabled(!toDelete.isEmpty());
    menu.addSeparator();
    menu.addAction(tr("Copiar ruta"), this, [path] {
        QGuiApplication::clipboard()->setText(QDir::toNativeSeparators(path));
    });
    menu.exec(m_tree->viewport()->mapToGlobal(pos));
}

// Los elementos seleccionados si `clicked` forma parte de la selección; si no, solo `clicked`.
// Se quitan la carpeta abierta y los elementos que ya están dentro de otro seleccionado.
QStringList MainWindow::selectedTreePaths(const QModelIndex &clicked) const
{
    if (!clicked.isValid())
        return {};
    QStringList paths;
    const QModelIndexList rows = m_tree->selectionModel()->selectedRows();
    if (rows.contains(clicked.siblingAtColumn(0))) {
        for (const QModelIndex &idx : rows)
            paths << QDir::cleanPath(m_fsModel->filePath(m_fsProxy->mapToSource(idx)));
    } else {
        paths << QDir::cleanPath(m_fsModel->filePath(m_fsProxy->mapToSource(clicked)));
    }
    paths.removeAll(QDir::cleanPath(m_rootPath));
    QStringList result;
    for (const QString &p : paths) {
        bool nested = false;
        for (const QString &other : paths)
            if (other != p && p.startsWith(other + QLatin1Char('/')))
                nested = true;
        if (!nested && !result.contains(p))
            result << p;
    }
    return result;
}

void MainWindow::deletePaths(const QStringList &paths)
{
    if (paths.isEmpty())
        return;

    // Pestañas de archivos que se van a borrar (también los que están dentro de una carpeta)
    auto affected = [this](const QString &path) {
        QList<EditorTab *> tabs;
        for (int i = 0; i < m_tabs->count(); ++i) {
            EditorTab *t = tabAt(i);
            if (!t || t->isUntitled())
                continue;
            const QString file = QDir::cleanPath(t->filePath());
            if (file == path || file.startsWith(path + QLatin1Char('/')))
                tabs << t;
        }
        return tabs;
    };
    int modified = 0;
    for (const QString &p : paths)
        for (EditorTab *t : affected(p))
            if (t->isModified())
                ++modified;

    QString question;
    if (paths.size() == 1) {
        const QFileInfo info(paths.first());
        question = info.isDir() ? tr("¿Mover a la papelera la carpeta «%1» y todo su contenido?").arg(info.fileName())
                                : tr("¿Mover a la papelera «%1»?").arg(info.fileName());
    } else {
        question = tr("¿Mover a la papelera estos %1 elementos (las carpetas con todo su contenido)?").arg(paths.size());
    }
    if (modified > 0)
        question += QStringLiteral("\n\n") + tr("Se perderán los cambios sin guardar de %1 pestaña(s).").arg(modified);
    if (QMessageBox::question(this, tr("Eliminar"), question) != QMessageBox::Yes)
        return;

    auto closeTabsFor = [&](const QString &path) {
        for (EditorTab *t : affected(path)) {
            m_tabs->removeTab(m_tabs->indexOf(t));
            t->deleteLater();
        }
    };

    QStringList noTrash;
    QStringList failed;
    for (const QString &p : paths) {
        if (QFile::moveToTrash(p))
            closeTabsFor(p);
        else
            noTrash << p;
    }
    // Sin papelera disponible (p. ej. una unidad de red): borrar definitivamente, con otra confirmación
    if (!noTrash.isEmpty()) {
        QStringList names;
        for (const QString &p : noTrash)
            names << QDir::toNativeSeparators(p);
        const auto answer = QMessageBox::warning(
            this, tr("Eliminar"),
            tr("No se pudo mover a la papelera:\n\n%1\n\n¿Eliminarlo definitivamente? No se podrá recuperar.")
                .arg(names.join(QLatin1Char('\n'))),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer == QMessageBox::Yes) {
            for (const QString &p : noTrash) {
                const bool ok = QFileInfo(p).isDir() ? QDir(p).removeRecursively() : QFile::remove(p);
                if (ok)
                    closeTabsFor(p);
                else
                    failed << QDir::toNativeSeparators(p);
            }
        }
    }
    if (!failed.isEmpty())
        QMessageBox::warning(this, tr("Eliminar"),
                             tr("No se pudo eliminar:\n\n%1\n\nPuede que esté en uso o que no tengas permiso.")
                                 .arg(failed.join(QLatin1Char('\n'))));

    updateWindowTitle();
    updateStatus();
    statusBar()->showMessage(paths.size() == 1 ? tr("Eliminado %1").arg(QFileInfo(paths.first()).fileName())
                                               : tr("Eliminados %1 elementos").arg(paths.size()),
                             3000);
    for (const QString &p : paths)
        m_git->refreshRepositoryContaining(p);
}

// El nombre puede incluir subcarpetas ("src/util.cpp"): se crean si no existen.
void MainWindow::createFileIn(const QString &dir)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Nuevo archivo"),
                                               tr("Nombre del archivo en «%1»:").arg(QDir(dir).dirName()),
                                               QLineEdit::Normal, QString(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    const QString relative = QDir::fromNativeSeparators(name);
    if (QDir::isAbsolutePath(relative) || relative.split(QLatin1Char('/')).contains(QLatin1String(".."))
        || relative.endsWith(QLatin1Char('/'))) {
        QMessageBox::warning(this, tr("Nuevo archivo"),
                             tr("Escribe un nombre de archivo dentro de la carpeta (puede incluir subcarpetas)."));
        return;
    }
    const QString target = QDir::cleanPath(QDir(dir).filePath(relative));
    if (QFileInfo::exists(target)) {
        QMessageBox::warning(this, tr("Nuevo archivo"), tr("Ya existe «%1» en esa carpeta.").arg(relative));
        return;
    }
    QFile file(target);
    if (!QDir().mkpath(QFileInfo(target).absolutePath()) || !file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        QMessageBox::warning(this, tr("Nuevo archivo"),
                             tr("No se pudo crear «%1».\n\n%2").arg(relative, file.errorString()));
        return;
    }
    file.close();

    const QModelIndex parentIdx = m_fsProxy->mapFromSource(m_fsModel->index(dir));
    if (parentIdx.isValid())
        m_tree->expand(parentIdx);
    openFile(target);
    statusBar()->showMessage(tr("Creado %1").arg(QDir::toNativeSeparators(target)), 3000);
    m_git->refreshRepositoryContaining(target);
}

// Como createFileIn: el nombre puede incluir subcarpetas ("assets/icons").
void MainWindow::createFolderIn(const QString &dir)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Nueva carpeta"),
                                               tr("Nombre de la carpeta en «%1»:").arg(QDir(dir).dirName()),
                                               QLineEdit::Normal, QString(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    QString relative = QDir::fromNativeSeparators(name);
    while (relative.endsWith(QLatin1Char('/')))
        relative.chop(1);
    if (relative.isEmpty() || QDir::isAbsolutePath(relative)
        || relative.split(QLatin1Char('/')).contains(QLatin1String(".."))) {
        QMessageBox::warning(this, tr("Nueva carpeta"),
                             tr("Escribe un nombre de carpeta dentro de la carpeta (puede incluir subcarpetas)."));
        return;
    }
    const QString target = QDir::cleanPath(QDir(dir).filePath(relative));
    if (QFileInfo::exists(target)) {
        QMessageBox::warning(this, tr("Nueva carpeta"), tr("Ya existe «%1» en esa carpeta.").arg(relative));
        return;
    }
    if (!QDir().mkpath(target)) {
        QMessageBox::warning(this, tr("Nueva carpeta"),
                             tr("No se pudo crear «%1».\n\nPuede que no tengas permiso.").arg(relative));
        return;
    }

    const QModelIndex parentIdx = m_fsProxy->mapFromSource(m_fsModel->index(dir));
    if (parentIdx.isValid())
        m_tree->expand(parentIdx);
    const QModelIndex created = m_fsProxy->mapFromSource(m_fsModel->index(target));
    if (created.isValid()) {
        m_tree->setCurrentIndex(created);
        m_tree->scrollTo(created);
    }
    statusBar()->showMessage(tr("Creada %1").arg(QDir::toNativeSeparators(target)), 3000);
}

void MainWindow::renamePath(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists())
        return;
    const bool isRoot = QDir::cleanPath(path) == QDir::cleanPath(m_rootPath);
    bool ok = false;
    const QString name = QInputDialog::getText(this, info.isDir() ? tr("Renombrar carpeta") : tr("Renombrar archivo"),
                                               tr("Nuevo nombre:"), QLineEdit::Normal, info.fileName(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty() || name == info.fileName())
        return;
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        QMessageBox::warning(this, tr("Renombrar"), tr("El nombre no puede contener «/» ni «\\»."));
        return;
    }
    const QString target = info.dir().filePath(name);
    // En Windows permitir cambiar solo mayúsculas/minúsculas (a.txt -> A.txt)
    const bool onlyCase = name.compare(info.fileName(), Qt::CaseInsensitive) == 0;
    if (QFileInfo::exists(target) && !onlyCase) {
        QMessageBox::warning(this, tr("Renombrar"), tr("Ya existe «%1» en esa carpeta.").arg(name));
        return;
    }
    if (!QDir().rename(path, target)) {
        QMessageBox::warning(this, tr("Renombrar"),
                             tr("No se pudo renombrar «%1».\n\nPuede que esté en uso o que no tengas permiso.")
                                 .arg(info.fileName()));
        return;
    }

    retargetTabs(path, target);
    if (isRoot)
        openFolder(target);   // se renombró la carpeta abierta
    statusBar()->showMessage(tr("Renombrado a %1").arg(name), 3000);
    m_git->refreshRepositoryContaining(target);
}

void MainWindow::revealInExplorer(const QString &path)
{
    const QFileInfo info(path);
#ifdef Q_OS_WIN
    QProcess process;
    process.setProgram(QStringLiteral("explorer.exe"));
    if (info.isDir())
        process.setNativeArguments(QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(info.absoluteFilePath())));
    else
        process.setNativeArguments(QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(info.absoluteFilePath())));
    if (process.startDetached())
        return;
#endif
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.isDir() ? info.absoluteFilePath() : info.absolutePath()));
}

void MainWindow::runScript(const QString &path)
{
    const QFileInfo info(path);
    bool started = false;
#ifdef Q_OS_WIN
    // cmd /k: la ventana queda abierta al terminar para ver la salida
    started = startInNewConsole(QStringLiteral("cmd.exe"),
                                QStringLiteral("/k \"\"%1\"\"").arg(QDir::toNativeSeparators(info.absoluteFilePath())),
                                info.absolutePath());
#else
    const QString terminal = linuxTerminal();
    const QString command = QStringLiteral("bash '%1'; echo; read -n1 -rsp '%2'")
                                .arg(QString(info.absoluteFilePath()).replace(QLatin1Char('\''), QLatin1String("'\\''")),
                                     tr("Pulsa una tecla para cerrar…"));
    if (!terminal.isEmpty())
        started = QProcess::startDetached(terminal, {QStringLiteral("-e"), QStringLiteral("bash"), QStringLiteral("-c"), command},
                                          info.absolutePath());
#endif
    if (started)
        statusBar()->showMessage(tr("Ejecutando %1").arg(info.fileName()), 3000);
    else
        QMessageBox::warning(this, tr("Ejecutar"), tr("No se pudo ejecutar «%1».").arg(info.fileName()));
}

void MainWindow::openTerminal(const QString &directory)
{
    const QString dir = QDir::toNativeSeparators(QFileInfo(directory).absoluteFilePath());
    bool started = false;
#ifdef Q_OS_WIN
    // Windows Terminal si está instalado; si no, cmd en una consola nueva
    const QString wt = QStandardPaths::findExecutable(QStringLiteral("wt"));
    if (!wt.isEmpty())
        started = QProcess::startDetached(wt, {QStringLiteral("-d"), dir}, dir);
    if (!started)
        started = startInNewConsole(QStringLiteral("cmd.exe"), QStringLiteral("/k"), dir);
#else
    const QString terminal = linuxTerminal();
    if (!terminal.isEmpty())
        started = QProcess::startDetached(terminal, {}, dir);
#endif
    if (!started)
        QMessageBox::warning(this, tr("Abrir terminal"), tr("No se encontró ninguna terminal para abrir en «%1».").arg(dir));
}

// Las pestañas abiertas siguen al archivo (o a los archivos de la carpeta) que
// se renombró o movió
void MainWindow::retargetTabs(const QString &oldPath, const QString &newPath)
{
    const QString oldClean = QDir::cleanPath(oldPath);
    const QString oldPrefix = oldClean + QLatin1Char('/');
    for (int i = 0; i < m_tabs->count(); ++i) {
        EditorTab *t = tabAt(i);
        if (!t || t->isUntitled())
            continue;
        const QString file = QDir::cleanPath(t->filePath());
        if (file == oldClean)
            t->setFilePath(QDir::cleanPath(newPath));
        else if (file.startsWith(oldPrefix))
            t->setFilePath(QDir::cleanPath(newPath) + QLatin1Char('/') + file.mid(oldPrefix.size()));
        else
            continue;
        m_tabs->setTabToolTip(i, t->filePath());
    }
}

namespace {
// Copia recursiva de una carpeta (los archivos se copian tal cual)
bool copyRecursively(const QString &source, const QString &target)
{
    const QFileInfo info(source);
    if (!info.isDir())
        return QFile::copy(source, target);
    if (!QDir().mkpath(target))
        return false;
    const QDir dir(source);
    for (const QFileInfo &entry : dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
        if (!copyRecursively(entry.absoluteFilePath(), QDir(target).filePath(entry.fileName())))
            return false;
    }
    return true;
}

// "a.txt" -> "a - copia.txt", "a - copia (2).txt"… si ya existe en la carpeta
QString uniqueCopyName(const QString &targetDir, const QFileInfo &source)
{
    const QDir dir(targetDir);
    if (!dir.exists(source.fileName()))
        return source.fileName();
    const QString base = source.isDir() ? source.fileName() : source.completeBaseName();
    const QString ext = source.isDir() || source.suffix().isEmpty() ? QString() : QLatin1Char('.') + source.suffix();
    const QString copyWord = QCoreApplication::translate("MainWindow", "copia");  // tr("copia")
    for (int n = 1;; ++n) {
        const QString name = n == 1 ? QStringLiteral("%1 - %2%3").arg(base, copyWord, ext)
                                    : QStringLiteral("%1 - %2 (%3)%4").arg(base, copyWord).arg(n).arg(ext);
        if (!dir.exists(name))
            return name;
    }
}
} // namespace

void MainWindow::dropIntoFolder(const QStringList &sources, const QString &targetDir)
{
    if (sources.isEmpty())
        return;
    // Si se arrastra una carpeta junto con algo que contiene, basta con la carpeta
    QStringList items;
    for (const QString &src : sources) {
        bool covered = false;
        for (const QString &other : sources)
            if (other != src && QDir::cleanPath(src).startsWith(QDir::cleanPath(other) + QLatin1Char('/')))
                covered = true;
        if (!covered && !items.contains(src))
            items << src;
    }
    const QString what = items.size() == 1 ? QStringLiteral("«%1»").arg(QFileInfo(items.first()).fileName())
                                           : tr("%1 elementos").arg(items.size());
    const QString where = QFileInfo(targetDir).fileName();

    QMenu menu(this);
    QAction *header = menu.addAction(tr("%1 → %2").arg(what, where));
    header->setEnabled(false);
    menu.addSeparator();
    QAction *move = menu.addAction(tr("Mover aquí"));
    QAction *copy = menu.addAction(tr("Copiar aquí"));
    menu.addSeparator();
    menu.addAction(tr("Cancelar"));
    // Mover no tiene sentido si todo está ya en esa carpeta
    bool allHere = true;
    for (const QString &src : items)
        if (QDir::cleanPath(QFileInfo(src).absolutePath()) != QDir::cleanPath(targetDir))
            allHere = false;
    move->setEnabled(!allHere);
    menu.setActiveAction(allHere ? copy : move);
    QAction *chosen = menu.exec(QCursor::pos());
    if (chosen != move && chosen != copy)
        return;

    QStringList failed;
    QStringList skipped;
    int done = 0;
    for (const QString &src : items) {
        const QFileInfo info(src);
        if (chosen == move) {
            if (QDir::cleanPath(info.absolutePath()) == QDir::cleanPath(targetDir))
                continue;
            const QString dest = QDir(targetDir).filePath(info.fileName());
            if (QFileInfo::exists(dest)) {
                skipped << info.fileName();
                continue;
            }
            if (!QDir().rename(src, dest)) {
                failed << info.fileName();
                continue;
            }
            retargetTabs(src, dest);
            ++done;
        } else {
            const QString dest = QDir(targetDir).filePath(uniqueCopyName(targetDir, info));
            if (!copyRecursively(src, dest)) {
                failed << info.fileName();
                continue;
            }
            ++done;
        }
    }

    if (!skipped.isEmpty() || !failed.isEmpty()) {
        QString text;
        if (!skipped.isEmpty())
            text += tr("Ya existe en «%1» (no se movió):\n%2").arg(where, skipped.join(QLatin1Char('\n')));
        if (!failed.isEmpty()) {
            if (!text.isEmpty())
                text += QStringLiteral("\n\n");
            text += tr("No se pudo completar (en uso o sin permiso):\n%1").arg(failed.join(QLatin1Char('\n')));
        }
        QMessageBox::warning(this, chosen == move ? tr("Mover") : tr("Copiar"), text);
    }
    if (done > 0) {
        statusBar()->showMessage(chosen == move ? tr("%1 movido(s) a %2").arg(done).arg(where)
                                                : tr("%1 copiado(s) a %2").arg(done).arg(where),
                                 3000);
        m_git->refreshRepositoryContaining(targetDir);
    }
}
