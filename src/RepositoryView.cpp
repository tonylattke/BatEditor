#include "RepositoryView.h"

#include "FileDiffWidget.h"
#include "GitCommandPalette.h"
#include "Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QFileDialog>
#include <QMenu>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSyntaxHighlighter>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QUrl>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h> // CREATE_NO_WINDOW
#endif

#include <functional>

namespace {

constexpr int kMaxCommits = 1000;

enum Roles {
    KindRole = Qt::UserRole + 1, // 0 = cambios actuales, 1 = commit
    IdRole,
    SummaryRole,
    AuthorRole,
    DateRole,
    LabelsRole,
    MergeRole,
    HeadRole,
    KeyRole,      // Archivos: clave del widget de diff
    TargetRole,   // Ubicaciones: commit destino
    RefKindRole,  // Ubicaciones: tipo de referencia
    FilePathRole, // Archivos: ruta relativa
    SectionRole,  // Archivos: staged / unstaged / untracked / commit
    SectionHeaderRole, // cabecera de sección (id) en la pestaña Archivos
    CodeRole,     // Archivos: código de estado
};

enum RefKind { RefNone, RefBranch, RefRemoteBranch, RefTag, RefStash, RefOther };

QString formatDate(const QDateTime &dt)
{
    return QLocale().toString(dt, QStringLiteral("ddd d MMM yyyy, HH:mm"));
}

// ---------------------------------------------------------------------------
// Delegado de la lista de commits: línea de grafo, dos líneas de texto y etiquetas
// ---------------------------------------------------------------------------
class CommitDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        return {200, option.fontMetrics.height() * 2 + 16};
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const Theme::Colors &c = Theme::colors();
        const QRect r = option.rect;
        const bool selected = option.state & QStyle::State_Selected;
        const bool hover = option.state & QStyle::State_MouseOver;
        const bool working = index.data(KindRole).toInt() == 0;
        const int row = index.row();
        const int rows = index.model()->rowCount();

        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        if (selected)
            p->fillRect(r, c.selection);
        else if (hover)
            p->fillRect(r, c.currentLine);

        // Línea vertical y nodo
        const int gx = r.left() + 16;
        const int cy = r.top() + r.height() / 2;
        QPen linePen(c.accent.darker(150), 2);
        p->setPen(linePen);
        p->drawLine(gx, row == 0 ? cy : r.top(), gx, row == rows - 1 ? cy : r.bottom() + 1);
        if (working) {
            p->setPen(QPen(c.accent, 2));
            p->setBrush(c.background);
            p->drawEllipse(QPoint(gx, cy), 5, 5);
        } else {
            const bool merge = index.data(MergeRole).toBool();
            p->setPen(Qt::NoPen);
            p->setBrush(c.accent);
            p->drawEllipse(QPoint(gx, cy), merge ? 6 : 4, merge ? 6 : 4);
        }

        QFont base = option.font;
        const QFontMetrics fm(base);
        const int x0 = r.left() + 34;
        const int right = r.right() - 10;
        const int line1 = r.top() + 7;
        const int line2 = line1 + fm.height() + 2;

        // Línea 1: resumen + fecha
        const QString date = index.data(DateRole).toString();
        QFont small = base;
        small.setPointSizeF(base.pointSizeF() * 0.92);
        const QFontMetrics sfm(small);
        const int dateW = sfm.horizontalAdvance(date);
        QFont summaryFont = base;
        if (index.data(HeadRole).toBool() || working)
            summaryFont.setBold(true);
        p->setFont(summaryFont);
        p->setPen(c.foreground);
        const QString summary = QFontMetrics(summaryFont)
                                    .elidedText(index.data(SummaryRole).toString(), Qt::ElideRight,
                                                right - x0 - dateW - 12);
        p->drawText(QRect(x0, line1, right - x0, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, summary);
        p->setFont(small);
        p->setPen(c.muted);
        p->drawText(QRect(x0, line1, right - x0, fm.height()), Qt::AlignRight | Qt::AlignVCenter, date);

        // Línea 2: etiquetas a la derecha, autor a la izquierda
        int badgeX = right;
        const QStringList labels = index.data(LabelsRole).toStringList();
        for (int i = labels.size() - 1; i >= 0; --i) {
            const bool isTag = labels.at(i).startsWith(QLatin1String("tag:"));
            const QString label = isTag ? QStringLiteral("◆ ") + labels.at(i).mid(4) : labels.at(i);
            const int w = sfm.horizontalAdvance(label) + 12;
            if (badgeX - w < x0 + 60)
                break;
            const QRect br(badgeX - w, line2 + 1, w, sfm.height() + 2);
            const bool isRemote = label.contains(QLatin1Char('/'));
            QColor bg = isTag ? c.preproc.darker(170) : (isRemote ? c.border.lighter(130) : c.function.darker(140));
            if (label == QLatin1String("HEAD"))
                bg = c.removed.darker(150);
            QPainterPath path;
            path.addRoundedRect(br, 3, 3);
            p->fillPath(path, bg);
            p->setPen(c.foreground);
            p->drawText(br, Qt::AlignCenter, label);
            badgeX -= w + 4;
        }
        QFont authorFont = small;
        authorFont.setItalic(true);
        p->setFont(authorFont);
        p->setPen(c.muted);
        const QString author = QFontMetrics(authorFont)
                                   .elidedText(index.data(AuthorRole).toString(), Qt::ElideRight, badgeX - x0 - 6);
        p->drawText(QRect(x0, line2, badgeX - x0, fm.height()), Qt::AlignLeft | Qt::AlignVCenter, author);
        p->restore();
    }
};

QLabel *mutedLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    QPalette p = l->palette();
    p.setColor(QPalette::WindowText, Theme::colors().muted);
    l->setPalette(p);
    return l;
}

QWidget *sectionTitle(const QString &text, int count, QWidget *parent, QHBoxLayout **actions)
{
    auto *w = new QWidget(parent);
    w->setAutoFillBackground(true);
    QPalette p = w->palette();
    p.setColor(QPalette::Window, Theme::colors().backgroundAlt);
    w->setPalette(p);
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(10, 6, 10, 6);
    QString html = QStringLiteral("<b>%1</b>").arg(text.toHtmlEscaped());
    if (count >= 0)
        html += QStringLiteral("&nbsp;&nbsp;<span style='color:%1'>%2</span>").arg(Theme::colors().muted.name()).arg(count);
    auto *title = new QLabel(html, w);
    title->setTextFormat(Qt::RichText);
    l->addWidget(title, 1);
    *actions = l;
    return w;
}

// Cabecera de una sección de cambios: ▼/▶ + título + número; clic pliega o
// despliega y clic derecho abre el menú de la sección (como en Sublime Merge).
class SectionHeader : public QWidget
{
public:
    SectionHeader(const QString &text, int count, bool collapsed, QWidget *parent)
        : QWidget(parent)
    {
        setAutoFillBackground(true);
        QPalette p = palette();
        p.setColor(QPalette::Window, Theme::colors().backgroundAlt);
        setPalette(p);
        setCursor(Qt::PointingHandCursor);
        setObjectName(QStringLiteral("sectionHeader"));
        layout = new QHBoxLayout(this);
        layout->setContentsMargins(10, 6, 10, 6);
        auto *title = new QLabel(QStringLiteral("<span style='color:%1'>%2</span>&nbsp;&nbsp;<b>%3</b>"
                                                "&nbsp;&nbsp;<span style='color:%1'>(%4)</span>")
                                     .arg(Theme::colors().muted.name(),
                                          collapsed ? QStringLiteral("&#9654;") : QStringLiteral("&#9660;"),
                                          text.toUpper().toHtmlEscaped())
                                     .arg(count),
                                 this);
        title->setTextFormat(Qt::RichText);
        layout->addWidget(title, 1);
    }

    QHBoxLayout *layout = nullptr;
    std::function<void()> onToggle;
    std::function<void(const QPoint &)> onMenu;

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && onToggle)
            onToggle();
        else if (e->button() == Qt::RightButton && onMenu)
            onMenu(e->globalPosition().toPoint());
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Construcción
// ---------------------------------------------------------------------------
RepositoryView::RepositoryView(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

void RepositoryView::buildUi()
{
    const Theme::Colors &c = Theme::colors();

    // --- Barra superior -------------------------------------------------------
    auto *toggleSidebar = new QToolButton(this);
    toggleSidebar->setText(QStringLiteral("◧"));
    toggleSidebar->setToolTip(tr("Mostrar u ocultar ubicaciones"));
    toggleSidebar->setCheckable(true);
    toggleSidebar->setChecked(true);

    m_branchLabel = new QLabel(this);
    m_branchLabel->setAlignment(Qt::AlignCenter);
    m_branchLabel->setMinimumWidth(260);
    m_branchLabel->setStyleSheet(QStringLiteral("background:%1; border:1px solid %2; border-radius:3px; padding:4px 16px;")
                                     .arg(c.background.name(), c.border.name()));

    auto *openInEditor = new QToolButton(this);
    openInEditor->setText(tr("Abrir en el editor"));
    openInEditor->setToolTip(tr("Abre la carpeta del repositorio en el modo editor"));
    connect(openInEditor, &QToolButton::clicked, this, [this] { Q_EMIT openFolderRequested(m_repo.workdir()); });

    auto *refresh = new QToolButton(this);
    refresh->setText(QStringLiteral("⟳"));
    refresh->setToolTip(tr("Actualizar (F5)"));
    connect(refresh, &QToolButton::clicked, this, &RepositoryView::reload);

    auto *toolbar = new QWidget(this);
    toolbar->setAutoFillBackground(true);
    {
        QPalette p = toolbar->palette();
        p.setColor(QPalette::Window, c.panel);
        toolbar->setPalette(p);
    }
    auto *tl = new QHBoxLayout(toolbar);
    tl->setContentsMargins(8, 6, 8, 6);
    tl->addWidget(toggleSidebar);
    tl->addStretch(1);
    tl->addWidget(m_branchLabel);
    tl->addStretch(1);
    tl->addWidget(openInEditor);
    tl->addWidget(refresh);
    tl->addSpacing(12);
    tl->addWidget(buildRemoteButtons(toolbar));

    // --- Barra de progreso / resultado de las operaciones remotas ---
    m_outputBar = new QWidget(this);
    m_outputBar->setAutoFillBackground(true);
    m_outputLabel = new QLabel(m_outputBar);
    m_outputLabel->setTextFormat(Qt::RichText);
    m_outputLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_outputDetails = new QToolButton(m_outputBar);
    m_outputDetails->setText(tr("Ver salida"));
    connect(m_outputDetails, &QToolButton::clicked, this, &RepositoryView::showCommandOutput);
    auto *closeOutput = new QToolButton(m_outputBar);
    closeOutput->setText(QStringLiteral("✕"));
    closeOutput->setAutoRaise(true);
    connect(closeOutput, &QToolButton::clicked, m_outputBar, &QWidget::hide);
    auto *ol = new QHBoxLayout(m_outputBar);
    ol->setContentsMargins(12, 5, 8, 5);
    ol->addWidget(m_outputLabel, 1);
    ol->addWidget(m_outputDetails);
    ol->addWidget(closeOutput);
    m_outputBar->hide();

    // --- 1) Ubicaciones -------------------------------------------------------
    m_locations = new QTreeWidget(this);
    m_locations->setHeaderHidden(true);
    m_locations->setColumnCount(1);
    m_locations->setIndentation(12);
    m_locations->setUniformRowHeights(true);
    m_locations->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_locations, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *item, int) { onLocationClicked(item); });
    connect(m_locations, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) { onLocationDoubleClicked(item); });
    connect(m_locations, &QTreeWidget::customContextMenuRequested, this, &RepositoryView::showLocationMenu);

    m_sidebar = new QWidget(this);
    auto *sl = new QVBoxLayout(m_sidebar);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->setSpacing(0);
    auto *locTitle = new QLabel(tr("Ubicaciones"), m_sidebar);
    locTitle->setAlignment(Qt::AlignCenter);
    locTitle->setObjectName(QStringLiteral("paneTitle"));
    sl->addWidget(locTitle);
    sl->addWidget(m_locations, 1);
    connect(toggleSidebar, &QToolButton::toggled, m_sidebar, &QWidget::setVisible);

    // --- 2) Commits / Archivos -------------------------------------------------
    m_commits = new QListWidget(this);
    m_commits->setObjectName(QStringLiteral("commitList"));
    m_commits->setItemDelegate(new CommitDelegate(m_commits));
    m_commits->setMouseTracking(true);
    m_commits->setUniformItemSizes(true);
    m_commits->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    connect(m_commits, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (!item)
            return;
        m_workingSelected = item->data(KindRole).toInt() == 0;
        m_selectedCommit = item->data(IdRole).toString();
        showSelection();
    });

    m_files = new QListWidget(this);
    m_files->setObjectName(QStringLiteral("fileList"));
    m_files->setUniformItemSizes(true);
    m_files->setSelectionMode(QAbstractItemView::ExtendedSelection); // Ctrl+clic y Shift+clic
    m_files->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_files, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *item = m_files->itemAt(pos);
        const QString section = item ? item->data(SectionHeaderRole).toString() : QString();
        if (!section.isEmpty()) {
            showSectionMenu(section, m_files->viewport()->mapToGlobal(pos));
            return;
        }
        if (item && !selectedFiles().isEmpty())
            showFileMenu(m_files->viewport()->mapToGlobal(pos));
    });
    for (int key : {int(Qt::Key_Return), int(Qt::Key_Enter)}) {
        auto *sc = new QShortcut(QKeySequence(Qt::SHIFT | key), m_files);
        sc->setContext(Qt::WidgetShortcut);
        connect(sc, &QShortcut::activated, this, &RepositoryView::toggleStageSelected);
    }
    connect(m_files, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const QString section = item->data(SectionHeaderRole).toString();
        if (!section.isEmpty()) {
            toggleSection(section);
            return;
        }
        const QString key = item->data(KeyRole).toString();
        if (!key.isEmpty())
            scrollToFile(key);
    });

    m_middleTabs = new QTabWidget(this);
    m_middleTabs->setDocumentMode(true);
    m_middleTabs->tabBar()->setExpanding(true);
    m_middleTabs->addTab(m_commits, tr("Commits"));
    m_middleTabs->addTab(m_files, tr("Archivos"));

    // --- 3) Detalle ------------------------------------------------------------
    m_detail = new QScrollArea(this);
    m_detail->setWidgetResizable(true);
    m_detail->setFrameShape(QFrame::NoFrame);
    m_detail->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_detailPane = new QWidget(this);
    auto *paneLayout = new QVBoxLayout(m_detailPane);
    paneLayout->setContentsMargins(0, 0, 0, 0);
    paneLayout->setSpacing(0);
    paneLayout->addWidget(m_detail, 1);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_sidebar);
    splitter->addWidget(m_middleTabs);
    splitter->addWidget(m_detailPane);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 0);
    splitter->setStretchFactor(2, 1);
    splitter->setSizes({230, 400, 900});
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(toolbar);
    layout->addWidget(m_outputBar);
    layout->addWidget(splitter, 1);
}

bool RepositoryView::open(const QString &path, QString *error)
{
    if (!m_repo.open(path)) {
        if (error)
            *error = m_repo.lastError();
        return false;
    }
    reload();
    return true;
}

// ---------------------------------------------------------------------------
// Carga de datos
// ---------------------------------------------------------------------------
void RepositoryView::reload()
{
    if (!m_repo.isOpen())
        return;
    m_branchLabel->setText(QStringLiteral("⎇  ") + m_repo.branchName());
    m_branchLabel->setToolTip(m_repo.workdir());
    reloadLocations();
    reloadCommits();
    updateRemoteButtons();
}

void RepositoryView::reloadLocations()
{
    // Recuerda qué grupos estaban expandidos
    QSet<QString> expanded;
    for (int i = 0; i < m_locations->topLevelItemCount(); ++i)
        if (m_locations->topLevelItem(i)->isExpanded())
            expanded.insert(m_locations->topLevelItem(i)->data(0, IdRole).toString());
    const bool first = m_locations->topLevelItemCount() == 0;

    m_locations->clear();
    const GitRepository::Locations loc = m_repo.locations();

    auto group = [&](const QString &id, const QString &title, int count) {
        auto *g = new QTreeWidgetItem(m_locations, {QStringLiteral("%1 (%2)").arg(title.toUpper()).arg(count)});
        g->setData(0, IdRole, id);
        QFont f = g->font(0);
        f.setBold(true);
        g->setFont(0, f);
        g->setForeground(0, Theme::colors().muted);
        g->setExpanded(first ? id == QLatin1String("branches") : expanded.contains(id));
        return g;
    };
    auto child = [](QTreeWidgetItem *parent, const QString &text, const QString &target, RefKind kind) {
        auto *it = new QTreeWidgetItem(parent, {text});
        it->setData(0, TargetRole, target);
        it->setData(0, RefKindRole, int(kind));
        it->setData(0, IdRole, text);
        return it;
    };

    QTreeWidgetItem *branches = group(QStringLiteral("branches"), tr("Ramas"), loc.branches.size());
    for (const auto &b : loc.branches) {
        QTreeWidgetItem *it = child(branches, b.name, b.target, RefBranch);
        if (b.isHead) {
            QFont f = it->font(0);
            f.setBold(true);
            it->setFont(0, f);
            it->setText(0, QStringLiteral("● ") + b.name);
            it->setForeground(0, Theme::colors().accent);
        }
        it->setToolTip(0, tr("Doble clic para cambiar a esta rama"));
    }

    int remoteCount = 0;
    for (auto it = loc.remotes.cbegin(); it != loc.remotes.cend(); ++it)
        remoteCount += 1;
    QTreeWidgetItem *remotes = group(QStringLiteral("remotes"), tr("Remotos"), remoteCount);
    for (auto it = loc.remotes.cbegin(); it != loc.remotes.cend(); ++it) {
        QTreeWidgetItem *r = child(remotes, it.key(), QString(), RefOther);
        r->setExpanded(true);
        for (const auto &b : it.value()) {
            QTreeWidgetItem *rb = child(r, b.name.mid(it.key().size() + 1), b.target, RefRemoteBranch);
            rb->setData(0, IdRole, b.name);
        }
    }

    QTreeWidgetItem *tags = group(QStringLiteral("tags"), tr("Etiquetas"), loc.tags.size());
    for (const auto &t : loc.tags)
        child(tags, t.name, t.target, RefTag);

    QTreeWidgetItem *stashes = group(QStringLiteral("stashes"), tr("Stashes"), loc.stashes.size());
    for (const auto &s : loc.stashes) {
        QTreeWidgetItem *it = child(stashes, s.detail.isEmpty() ? s.name : s.detail, s.target, RefStash);
        it->setToolTip(0, s.name);
    }

    QTreeWidgetItem *subs = group(QStringLiteral("submodules"), tr("Submódulos"), loc.submodules.size());
    for (const QString &s : loc.submodules)
        child(subs, s, QString(), RefOther);

    QTreeWidgetItem *wts = group(QStringLiteral("worktrees"), tr("Worktrees"), loc.worktrees.size());
    for (const QString &w : loc.worktrees)
        child(wts, w, QString(), RefOther);
}

void RepositoryView::reloadCommits()
{
    const bool wasWorking = m_workingSelected;
    const QString selected = m_selectedCommit;

    const QSignalBlocker blocker(m_commits);
    m_commits->clear();

    // Elemento "cambios actuales"
    const GitRepository::Status st = m_repo.status();
    int untracked = 0;
    for (const auto &e : st.unstaged)
        if (e.code == QLatin1Char('?'))
            ++untracked;
    const int unstaged = int(st.unstaged.size()) - untracked;
    QStringList parts;
    if (!st.staged.isEmpty())
        parts << tr("%1 preparado(s)").arg(st.staged.size());
    if (unstaged > 0)
        parts << tr("%1 sin preparar").arg(unstaged);
    if (untracked > 0)
        parts << tr("%1 sin seguimiento").arg(untracked);

    auto *working = new QListWidgetItem(m_commits);
    working->setData(KindRole, 0);
    working->setData(SummaryRole, parts.isEmpty() ? tr("Sin cambios") : parts.join(QStringLiteral(", ")));
    working->setData(AuthorRole, parts.isEmpty() ? tr("Directorio de trabajo limpio") : tr("Confirmar cambios"));

    // Historial (todas las ramas)
    const QHash<QString, QStringList> labels = m_repo.refLabels();
    const QString head = m_repo.headCommitId();
    QListWidgetItem *toSelect = wasWorking ? working : nullptr;
    for (const auto &ci : m_repo.log(kMaxCommits, true)) {
        auto *item = new QListWidgetItem(m_commits);
        item->setData(KindRole, 1);
        item->setData(IdRole, ci.id);
        item->setData(SummaryRole, ci.summary);
        item->setData(AuthorRole, ci.author);
        item->setData(DateRole, formatDate(ci.time));
        item->setData(LabelsRole, labels.value(ci.id));
        item->setData(MergeRole, ci.parentCount > 1);
        item->setData(HeadRole, ci.id == head);
        item->setToolTip(QStringLiteral("%1\n%2").arg(ci.id, ci.summary));
        if (!wasWorking && ci.id == selected)
            toSelect = item;
    }

    if (!toSelect)
        toSelect = working;
    m_commits->setCurrentItem(toSelect);
    m_workingSelected = toSelect == working;
    m_selectedCommit = toSelect->data(IdRole).toString();
    showSelection();
}

void RepositoryView::showSelection()
{
    if (m_workingSelected)
        showWorkingDirectory();
    else
        showCommit(m_selectedCommit);
}

void RepositoryView::setDetailWidget(QWidget *content, QWidget *header)
{
    m_fileWidgets.clear();
    if (m_detailHeader) {
        m_detailHeader->hide();
        m_detailHeader->deleteLater();
        m_detailHeader = nullptr;
    }
    if (header) {
        static_cast<QVBoxLayout *>(m_detailPane->layout())->insertWidget(0, header);
        m_detailHeader = header;
    }
    // El contenido anterior se borra después: puede que estemos dentro de una
    // señal de uno de sus botones (Preparar, Quitar, Commit...)
    if (QWidget *old = m_detail->takeWidget()) {
        old->hide();
        old->deleteLater();
    }
    m_detail->setWidget(content);
}

void RepositoryView::scrollToFile(const QString &key)
{
    FileDiffWidget *w = m_fileWidgets.value(key);
    if (!w)
        return;
    w->setExpanded(true);
    QTimer::singleShot(0, this, [this, w = QPointer<FileDiffWidget>(w)] {
        if (w)
            m_detail->verticalScrollBar()->setValue(w->y());
    });
}

// ---------------------------------------------------------------------------
// Detalle: cambios actuales (formulario de commit)
// ---------------------------------------------------------------------------
void RepositoryView::showWorkingDirectory()
{
    const Theme::Colors &c = Theme::colors();
    const int scroll = m_detail->verticalScrollBar()->value();

    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    const QList<GitRepository::FileChange> staged = m_repo.workingChanges(true);
    const QList<GitRepository::FileChange> unstagedAll = m_repo.workingChanges(false);
    QList<GitRepository::FileChange> unstaged, untracked;
    for (const auto &f : unstagedAll)
        (f.code == QLatin1Char('?') ? untracked : unstaged) << f;

    // --- Formulario de commit (fijo arriba: no se desplaza con la lista) ---
    auto *form = new QWidget;
    form->setAutoFillBackground(true);
    {
        QPalette fp = form->palette();
        fp.setColor(QPalette::Window, c.background);
        form->setPalette(fp);
    }
    auto *fl = new QVBoxLayout(form);
    fl->setContentsMargins(12, 12, 12, 0);
    auto *message = new QPlainTextEdit(form);
    message->setObjectName(QStringLiteral("commitMessage"));
    message->setPlaceholderText(tr("Mensaje del commit"));
    message->setFont(Theme::editorFont());
    message->setFixedHeight(96);
    message->setTabChangesFocus(true);
    message->setPlainText(m_commitDraft);
    connect(message, &QPlainTextEdit::textChanged, this, [this, message] { m_commitDraft = message->toPlainText(); });
    message->installEventFilter(this);
    message->setProperty("batcodeCommitBox", true);

    const QString sig = m_repo.signatureText();
    auto *who = mutedLabel(sig.isEmpty() ? tr("Configura user.name y user.email en Git") : sig, form);
    auto *commitBtn = new QPushButton(staged.isEmpty() ? tr("Nada que confirmar")
                                                       : (staged.size() == 1 ? tr("Commit 1 archivo")
                                                                             : tr("Commit %1 archivos").arg(staged.size())),
                                      form);
    commitBtn->setObjectName(QStringLiteral("primary"));
    commitBtn->setEnabled(!staged.isEmpty());
    commitBtn->setMinimumWidth(220);
    commitBtn->setToolTip(tr("Ctrl+Enter en el mensaje"));
    connect(commitBtn, &QPushButton::clicked, this, &RepositoryView::doCommit);

    auto *row = new QHBoxLayout;
    row->addWidget(who, 1);
    row->addWidget(commitBtn);
    fl->addWidget(message);
    fl->addLayout(row);
    auto *formLine = new QFrame(form);   // separación con la lista de archivos
    formLine->setFrameShape(QFrame::HLine);
    formLine->setFixedHeight(1);
    formLine->setStyleSheet(QStringLiteral("background:%1; border:none;").arg(c.border.name()));
    fl->addWidget(formLine);

    // --- Lista de archivos (pestaña "Archivos") ---
    // Se conserva la selección por ruta (un archivo preparado sigue seleccionado en su nueva sección)
    QSet<QString> previouslySelected;
    if (m_filesAreWorking)
        for (const FileRef &f : selectedFiles())
            previouslySelected.insert(f.path);
    m_files->clear();
    m_filesAreWorking = true;
    m_sectionFiles.clear();
    auto addFileHeader = [this](const QString &id, const QString &text, int count) {
        const bool collapsed = m_collapsedSections.contains(id);
        auto *h = new QListWidgetItem(QStringLiteral("%1  %2 (%3)")
                                          .arg(collapsed ? QChar(0x25B6) : QChar(0x25BC))
                                          .arg(text.toUpper())
                                          .arg(count),
                                      m_files);
        h->setFlags(Qt::ItemIsEnabled);   // no seleccionable: clic = plegar/desplegar
        h->setData(SectionHeaderRole, id);
        QFont f = h->font();
        f.setBold(true);
        h->setFont(f);
        h->setForeground(Theme::colors().muted);
    };

    QHash<QString, QPointer<FileDiffWidget>> widgets;
    auto addSection = [&](const QString &id, const QString &title, const QList<GitRepository::FileChange> &files,
                          const QString &bulkText, std::function<bool()> bulkAction) {
        if (files.isEmpty())
            return;
        QList<FileRef> &refs = m_sectionFiles[id];
        for (const auto &f : files)
            refs.append({f.path, id, f.code});
        const bool collapsed = m_collapsedSections.contains(id);
        auto *header = new SectionHeader(title, int(files.size()), collapsed, content);
        header->onToggle = [this, id] { toggleSection(id); };
        header->onMenu = [this, id](const QPoint &pos) { showSectionMenu(id, pos); };
        layout->addWidget(header);
        auto *bulk = new QToolButton(header);
        bulk->setText(bulkText);
        header->layout->addWidget(bulk);
        connect(bulk, &QToolButton::clicked, this, [this, bulkAction, bulkText] {
            report(bulkAction(), bulkText.toLower());
            reload();
        });
        addFileHeader(id, title, int(files.size()));
        if (collapsed)
            return;   // plegada: ni diffs ni archivos en la lista

        for (const auto &f : files) {
            auto *w = new FileDiffWidget(f, content);
            const QString key = id + QLatin1Char(':') + f.path;
            widgets.insert(key, w);
            const QString abs = QDir(m_repo.workdir()).absoluteFilePath(f.path);
            if (id == QLatin1String("staged")) {
                connect(w->addAction(tr("Quitar"), tr("Quitar de preparados")), &QToolButton::clicked, this,
                        [this, f] { report(m_repo.unstage(f.path), tr("quitar de preparados")); reload(); });
            } else {
                connect(w->addAction(tr("Preparar"), tr("Añadir al próximo commit")), &QToolButton::clicked, this,
                        [this, f] { report(m_repo.stage(f.path), tr("preparar")); reload(); });
                if (id == QLatin1String("unstaged")) {
                    connect(w->addAction(tr("Descartar"), tr("Descartar los cambios de este archivo")),
                            &QToolButton::clicked, this, [this, f] {
                                const auto answer = QMessageBox::warning(
                                    this, tr("Descartar cambios"),
                                    tr("¿Descartar los cambios sin preparar de\n%1?\n\nEsta acción no se puede deshacer.")
                                        .arg(f.path),
                                    QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
                                if (answer != QMessageBox::Discard)
                                    return;
                                report(m_repo.discard(f.path), tr("descartar"));
                                reload();
                            });
                }
            }
            if (f.code != QLatin1Char('D')) {
                connect(w->addAction(tr("Abrir"), tr("Abrir en el editor")), &QToolButton::clicked, this,
                        [this, abs] { Q_EMIT openFileRequested(abs); });
            }
            layout->addWidget(w);

            auto *fi = new QListWidgetItem(QStringLiteral("%1   %2").arg(f.code == QLatin1Char('?') ? QLatin1Char('U') : f.code).arg(f.path), m_files);
            fi->setForeground(colorForChangeCode(f.code));
            fi->setData(KeyRole, key);
            fi->setData(FilePathRole, f.path);
            fi->setData(SectionRole, id);
            fi->setData(CodeRole, QString(f.code));
            fi->setToolTip(describeChangeCode(f.code));
            if (previouslySelected.contains(f.path))
                fi->setSelected(true);
            connect(w, &FileDiffWidget::contextMenuRequested, this, [this, key](const QPoint &pos) {
                selectFileForMenu(key);
                showFileMenu(pos);
            });
        }
    };
    // Mismo orden que Sublime Merge: modificados, sin seguimiento y preparados
    addSection(QStringLiteral("unstaged"), tr("Archivos modificados"), unstaged, tr("Preparar todo"), [this, unstaged] {
        for (const auto &f : unstaged)
            if (!m_repo.stage(f.path))
                return false;
        return true;
    });
    addSection(QStringLiteral("untracked"), tr("Archivos sin seguimiento"), untracked, tr("Preparar todo"), [this, untracked] {
        for (const auto &f : untracked)
            if (!m_repo.stage(f.path))
                return false;
        return true;
    });
    addSection(QStringLiteral("staged"), tr("Archivos preparados"), staged, tr("Quitar todo"), [this] { return m_repo.unstageAll(); });

    if (staged.isEmpty() && unstagedAll.isEmpty()) {
        auto *clean = mutedLabel(tr("No hay cambios en el directorio de trabajo."), content);
        clean->setAlignment(Qt::AlignCenter);
        clean->setContentsMargins(0, 40, 0, 0);
        layout->addWidget(clean);
    }
    layout->addStretch(1);

    setDetailWidget(content, form);
    m_fileWidgets = widgets;
    QTimer::singleShot(0, this, [this, scroll] { m_detail->verticalScrollBar()->setValue(scroll); });
}

void RepositoryView::doCommit()
{
    QString id;
    const bool ok = m_repo.commit(m_commitDraft, &id);
    report(ok, tr("hacer commit"));
    if (!ok)
        return;
    m_commitDraft.clear();
    Q_EMIT statusMessage(tr("Commit %1 creado").arg(id.left(7)));
    reload();
}

// ---------------------------------------------------------------------------
// Detalle: un commit
// ---------------------------------------------------------------------------
void RepositoryView::showCommit(const QString &commitId)
{
    const Theme::Colors &c = Theme::colors();
    const GitRepository::CommitDetails d = m_repo.commitDetails(commitId);
    const QList<GitRepository::FileChange> changes = m_repo.commitChanges(commitId);

    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // --- Cabecera ---
    auto *headerBox = new QWidget(content);
    auto *hl = new QVBoxLayout(headerBox);
    hl->setContentsMargins(16, 14, 16, 14);
    hl->setSpacing(8);

    auto *summary = new QLabel(d.summary, headerBox);
    QFont sf = summary->font();
    sf.setPointSizeF(sf.pointSizeF() * 1.35);
    sf.setBold(true);
    summary->setFont(sf);
    summary->setWordWrap(true);
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    hl->addWidget(summary);

    if (!d.body.isEmpty()) {
        auto *body = new QLabel(d.body, headerBox);
        body->setWordWrap(true);
        body->setFont(Theme::editorFont());
        body->setTextInteractionFlags(Qt::TextSelectableByMouse);
        hl->addWidget(body);
    }

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(4);
    int row = 0;
    auto addRow = [&](const QString &name, QWidget *value) {
        grid->addWidget(mutedLabel(name, headerBox), row, 0, Qt::AlignTop);
        grid->addWidget(value, row, 1);
        ++row;
    };
    auto selectable = [&](const QString &text) {
        auto *l = new QLabel(text, headerBox);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return l;
    };
    addRow(tr("Autor"), selectable(QStringLiteral("%1 <%2>").arg(d.authorName, d.authorEmail)));
    addRow(tr("Fecha"), selectable(formatDate(d.authorTime)));
    if (d.committerName != d.authorName)
        addRow(tr("Confirmado por"), selectable(QStringLiteral("%1, %2").arg(d.committerName, formatDate(d.commitTime))));
    auto *sha = selectable(d.id);
    sha->setFont(Theme::editorFont());
    addRow(tr("Commit"), sha);
    if (!d.parents.isEmpty()) {
        QStringList links;
        for (const QString &p : d.parents)
            links << QStringLiteral("<a href='%1' style='color:%2'>%3</a>").arg(p, c.function.name(), p.left(10));
        auto *parents = new QLabel(links.join(QStringLiteral(", ")), headerBox);
        parents->setTextFormat(Qt::RichText);
        parents->setFont(Theme::editorFont());
        connect(parents, &QLabel::linkActivated, this, &RepositoryView::selectCommit);
        addRow(d.parents.size() > 1 ? tr("Padres") : tr("Padre"), parents);
    }
    grid->setColumnStretch(1, 1);
    hl->addLayout(grid);
    layout->addWidget(headerBox);

    // --- Archivos ---
    int adds = 0, dels = 0;
    for (const auto &f : changes) {
        adds += f.additions;
        dels += f.deletions;
    }
    QHBoxLayout *actions = nullptr;
    layout->addWidget(sectionTitle(changes.size() == 1 ? tr("1 archivo cambiado") : tr("%1 archivos cambiados").arg(changes.size()),
                                   -1, content, &actions));
    auto *totals = new QLabel(QStringLiteral("<span style='color:%1'>+%2</span>&nbsp;&nbsp;<span style='color:%3'>−%4</span>")
                                  .arg(c.added.name()).arg(adds).arg(c.removed.name()).arg(dels),
                              content);
    actions->addWidget(totals);

    m_files->clear();
    m_filesAreWorking = false;
    QHash<QString, QPointer<FileDiffWidget>> widgets;
    const bool many = changes.size() > 40;
    for (const auto &f : changes) {
        auto *w = new FileDiffWidget(f, content);
        if (many)
            w->setExpanded(false);
        const QString key = QStringLiteral("commit:") + f.path;
        widgets.insert(key, w);
        if (f.code != QLatin1Char('D')) {
            const QString abs = QDir(m_repo.workdir()).absoluteFilePath(f.path);
            connect(w->addAction(tr("Abrir"), tr("Abrir la versión actual en el editor")), &QToolButton::clicked, this,
                    [this, abs] { Q_EMIT openFileRequested(abs); });
        }
        layout->addWidget(w);

        auto *fi = new QListWidgetItem(QStringLiteral("%1   %2").arg(f.code).arg(f.path), m_files);
        fi->setForeground(colorForChangeCode(f.code));
        fi->setData(KeyRole, key);
        fi->setData(FilePathRole, f.path);
        fi->setData(SectionRole, QStringLiteral("commit"));
        fi->setData(CodeRole, QString(f.code));
        connect(w, &FileDiffWidget::contextMenuRequested, this, [this, key](const QPoint &pos) {
            selectFileForMenu(key);
            showFileMenu(pos);
        });
        fi->setToolTip(QStringLiteral("%1  (+%2 −%3)").arg(describeChangeCode(f.code)).arg(f.additions).arg(f.deletions));
    }
    layout->addStretch(1);

    setDetailWidget(content);
    m_fileWidgets = widgets;
    m_detail->verticalScrollBar()->setValue(0);
}

void RepositoryView::selectCommit(const QString &commitId)
{
    for (int i = 0; i < m_commits->count(); ++i) {
        QListWidgetItem *item = m_commits->item(i);
        if (item->data(IdRole).toString() == commitId) {
            m_middleTabs->setCurrentWidget(m_commits);
            m_commits->setCurrentItem(item);
            m_commits->scrollToItem(item, QAbstractItemView::PositionAtCenter);
            return;
        }
    }
    // Fuera del historial cargado (p. ej. un stash): se muestra igualmente
    const QSignalBlocker blocker(m_commits);
    m_commits->clearSelection();
    m_commits->setCurrentItem(nullptr);
    m_workingSelected = false;
    m_selectedCommit = commitId;
    showCommit(commitId);
}

// ---------------------------------------------------------------------------
// Ubicaciones
// ---------------------------------------------------------------------------
void RepositoryView::onLocationClicked(QTreeWidgetItem *item)
{
    if (!item || !item->parent())
        return;
    const QString target = item->data(0, TargetRole).toString();
    if (!target.isEmpty())
        selectCommit(target);
}

void RepositoryView::onLocationDoubleClicked(QTreeWidgetItem *item)
{
    if (!item || item->data(0, RefKindRole).toInt() != RefBranch)
        return;
    checkout(item->data(0, IdRole).toString());
}

void RepositoryView::showLocationMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_locations->itemAt(pos);
    if (!item || !item->parent())
        return;
    const int kind = item->data(0, RefKindRole).toInt();
    const QString name = item->data(0, IdRole).toString();
    const QString target = item->data(0, TargetRole).toString();
    QMenu menu(this);
    const QString current = m_repo.branchName();
    if (kind == RefBranch)
        menu.addAction(tr("Cambiar a «%1»").arg(name), this, [this, name] { checkout(name); });
    if ((kind == RefBranch || kind == RefRemoteBranch) && name != current && !m_repo.isHeadDetached())
        menu.addAction(tr("Fusionar «%1» en «%2»…").arg(name, current), this, [this, name] { showMergeOptions(name); });
    if (kind == RefOther && item->parent()->data(0, IdRole).toString() == QLatin1String("remotes")) {
        menu.addAction(tr("Fetch de «%1»").arg(name), this,
                       [this, name] { runGit({QStringLiteral("fetch"), QStringLiteral("--progress"), name}); });
        menu.addAction(tr("Fetch de «%1» con --prune").arg(name), this, [this, name] {
            runGit({QStringLiteral("fetch"), QStringLiteral("--progress"), QStringLiteral("--prune"), name});
        });
    }
    if (!menu.isEmpty())
        menu.addSeparator();
    if (!target.isEmpty()) {
        menu.addAction(tr("Mostrar commit"), this, [this, target] { selectCommit(target); });
        menu.addAction(tr("Copiar SHA"), this, [target] { QApplication::clipboard()->setText(target); });
    }
    if (!menu.isEmpty())
        menu.exec(m_locations->viewport()->mapToGlobal(pos));
}

void RepositoryView::checkout(const QString &branch)
{
    if (branch == m_repo.branchName())
        return;
    const auto answer = QMessageBox::question(this, tr("Cambiar de rama"),
                                              tr("¿Cambiar a la rama «%1»?").arg(branch));
    if (answer != QMessageBox::Yes)
        return;
    const bool ok = m_repo.checkoutBranch(branch);
    report(ok, tr("cambiar de rama (revisa que no haya cambios que entren en conflicto)"));
    if (ok)
        Q_EMIT statusMessage(tr("Ahora en la rama %1").arg(branch));
    m_workingSelected = true;
    reload();
}

void RepositoryView::report(bool ok, const QString &action)
{
    if (!ok)
        QMessageBox::warning(this, tr("Git"), tr("No se pudo %1.\n\n%2").arg(action, m_repo.lastError()));
}

bool RepositoryView::eventFilter(QObject *watched, QEvent *event)
{
    // Ctrl+Enter en el mensaje confirma el commit
    if (event->type() == QEvent::KeyPress && watched->property("batcodeCommitBox").toBool()) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) && (ke->modifiers() & Qt::ControlModifier)) {
            QTimer::singleShot(0, this, &RepositoryView::doCommit);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// Archivos: selección múltiple y menú contextual
// ---------------------------------------------------------------------------
namespace {

constexpr QChar kBlameSeparator(0x2502); // │

// Colorea la columna de blame (commit, autor, fecha, línea) de una vista de blame.
class BlameHighlighter : public QSyntaxHighlighter
{
public:
    using QSyntaxHighlighter::QSyntaxHighlighter;

protected:
    void highlightBlock(const QString &text) override
    {
        const Theme::Colors &c = Theme::colors();
        const int first = text.indexOf(kBlameSeparator);
        const int second = first >= 0 ? text.indexOf(kBlameSeparator, first + 1) : -1;
        if (second < 0)
            return;
        QTextCharFormat muted;
        muted.setForeground(c.muted);
        setFormat(0, second + 1, muted);
        QTextCharFormat sha;
        sha.setForeground(c.function);
        const int space = text.indexOf(QLatin1Char(' '));
        if (space > 0 && !text.startsWith(QLatin1Char(' ')))
            setFormat(0, space, sha);
    }
};

// Vista de solo lectura que avisa del doble clic en una línea.
class BlameView : public QPlainTextEdit
{
public:
    explicit BlameView(std::function<void(int)> onDoubleClick, QWidget *parent)
        : QPlainTextEdit(parent), m_onDoubleClick(std::move(onDoubleClick))
    {
        setReadOnly(true);
        setLineWrapMode(QPlainTextEdit::NoWrap);
        setFont(Theme::editorFont());
        setFrameShape(QFrame::NoFrame);
        QPalette p = palette();
        p.setColor(QPalette::Base, Theme::colors().background);
        setPalette(p);
    }

protected:
    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        m_onDoubleClick(cursorForPosition(e->position().toPoint()).blockNumber());
    }

private:
    std::function<void(int)> m_onDoubleClick;
};

QString plural(int n, const QString &one, const QString &many)
{
    return n == 1 ? one : many.arg(n);
}

} // namespace

QString RepositoryView::absolutePath(const QString &path) const
{
    return QDir(m_repo.workdir()).absoluteFilePath(path);
}

QList<RepositoryView::FileRef> RepositoryView::selectedFiles() const
{
    QList<FileRef> refs;
    for (int i = 0; i < m_files->count(); ++i) {
        const QListWidgetItem *item = m_files->item(i);
        if (!item->isSelected() || item->data(FilePathRole).toString().isEmpty())
            continue;
        const QString code = item->data(CodeRole).toString();
        refs.append({item->data(FilePathRole).toString(), item->data(SectionRole).toString(),
                     code.isEmpty() ? QChar() : code.at(0)});
    }
    return refs;
}

void RepositoryView::selectFileForMenu(const QString &key)
{
    // Clic derecho en un archivo del detalle: si ya forma parte de la selección se
    // conserva (para actuar sobre varios); si no, pasa a ser el único seleccionado.
    for (int i = 0; i < m_files->count(); ++i) {
        QListWidgetItem *item = m_files->item(i);
        if (item->data(KeyRole).toString() != key)
            continue;
        if (!item->isSelected()) {
            m_files->clearSelection();
            item->setSelected(true);
        }
        m_files->setCurrentItem(item, QItemSelectionModel::NoUpdate);
        return;
    }
}

void RepositoryView::showFileMenu(const QPoint &globalPos)
{
    const QList<FileRef> files = selectedFiles();
    if (files.isEmpty())
        return;
    const int n = int(files.size());
    const bool single = n == 1;
    const FileRef first = files.first();

    QList<FileRef> stageable, staged, discardable, onDisk;
    for (const FileRef &f : files) {
        if (f.section == QLatin1String("staged"))
            staged << f;
        else if (f.section == QLatin1String("unstaged") || f.section == QLatin1String("untracked"))
            stageable << f;
        if (f.section == QLatin1String("unstaged") && f.code != QLatin1Char('?'))
            discardable << f;
        if (QFileInfo::exists(absolutePath(f.path)))
            onDisk << f;
    }

    QMenu menu(this);
    const QKeySequence stageKey(Qt::SHIFT | Qt::Key_Enter); // se muestra como "Shift+Enter"

    // --- Preparar / quitar / descartar / borrar ---
    if (!stageable.isEmpty()) {
        QAction *a = menu.addAction(plural(int(stageable.size()), tr("Preparar archivo"), tr("Preparar %1 archivos")),
                                    this, [this, stageable] { stageFiles(stageable, true); });
        a->setShortcut(stageKey);
    }
    if (!staged.isEmpty()) {
        QAction *a = menu.addAction(plural(int(staged.size()), tr("Quitar de preparados"),
                                           tr("Quitar %1 archivos de preparados")),
                                    this, [this, staged] { stageFiles(staged, false); });
        if (stageable.isEmpty())
            a->setShortcut(stageKey);
    }
    if (!discardable.isEmpty())
        menu.addAction(plural(int(discardable.size()), tr("Descartar cambios…"), tr("Descartar cambios de %1 archivos…")),
                       this, [this, discardable] { discardFiles(discardable); });
    if (first.section != QLatin1String("commit") && !onDisk.isEmpty())
        menu.addAction(plural(int(onDisk.size()), tr("Borrar archivo…"), tr("Borrar %1 archivos…")),
                       this, [this, onDisk] { deleteFiles(onDisk); });
    if (!menu.isEmpty())
        menu.addSeparator();

    // --- Historial y blame (un solo archivo) ---
    QAction *history = menu.addAction(tr("Historial del archivo"), this, [this, first] { showFileHistory(first.path); });
    history->setEnabled(single && first.code != QLatin1Char('?'));
    QAction *blame = menu.addAction(tr("Blame"), this, [this, first] { showBlame(first.path); });
    const bool hasHistory = first.code != QLatin1Char('?')
        && !(first.section == QLatin1String("staged") && first.code == QLatin1Char('A'));
    blame->setEnabled(single && hasHistory && onDisk.size() == 1);
    menu.addSeparator();

    // --- Abrir / copiar ---
    QAction *open = menu.addAction(plural(int(onDisk.size()), tr("Abrir en el editor…"), tr("Abrir %1 archivos en el editor…")),
                                   this, [this, onDisk] {
                                       for (const FileRef &f : onDisk)
                                           Q_EMIT openFileRequested(absolutePath(f.path));
                                   });
    open->setEnabled(!onDisk.isEmpty());
    menu.addAction(tr("Abrir carpeta contenedora…"), this, [this, first] { openContainingFolder(first.path); });
    menu.addAction(plural(n, tr("Copiar ruta del archivo"), tr("Copiar rutas de los %1 archivos")), this, [this, files] {
        QStringList paths;
        for (const FileRef &f : files)
            paths << QDir::toNativeSeparators(absolutePath(f.path));
        QApplication::clipboard()->setText(paths.join(QLatin1Char('\n')));
        Q_EMIT statusMessage(tr("Ruta copiada al portapapeles"));
    });
    menu.addAction(plural(n, tr("Copiar ruta relativa"), tr("Copiar rutas relativas")), this, [this, files] {
        QStringList paths;
        for (const FileRef &f : files)
            paths << f.path;
        QApplication::clipboard()->setText(paths.join(QLatin1Char('\n')));
        Q_EMIT statusMessage(tr("Ruta copiada al portapapeles"));
    });

    menu.exec(globalPos);
}

void RepositoryView::toggleStageSelected()
{
    QList<FileRef> toStage, toUnstage;
    for (const FileRef &f : selectedFiles()) {
        if (f.section == QLatin1String("staged"))
            toUnstage << f;
        else if (f.section != QLatin1String("commit"))
            toStage << f;
    }
    if (toStage.isEmpty() && toUnstage.isEmpty())
        return;
    bool ok = true;
    for (const FileRef &f : toStage)
        ok = m_repo.stage(f.path) && ok;
    for (const FileRef &f : toUnstage)
        ok = m_repo.unstage(f.path) && ok;
    report(ok, tr("preparar o quitar los archivos"));
    reload();
}

void RepositoryView::stageFiles(const QList<FileRef> &files, bool stage)
{
    bool ok = true;
    for (const FileRef &f : files)
        ok = (stage ? m_repo.stage(f.path) : m_repo.unstage(f.path)) && ok;
    report(ok, stage ? tr("preparar") : tr("quitar de preparados"));
    reload();
}

void RepositoryView::discardFiles(const QList<FileRef> &files)
{
    QStringList names;
    for (const FileRef &f : files)
        names << f.path;
    const auto answer = QMessageBox::warning(
        this, tr("Descartar cambios"),
        tr("¿Descartar los cambios sin preparar de estos archivos?\n\n%1\n\nEsta acción no se puede deshacer.")
            .arg(names.join(QLatin1Char('\n'))),
        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Discard)
        return;
    bool ok = true;
    for (const FileRef &f : files)
        ok = m_repo.discard(f.path) && ok;
    report(ok, tr("descartar"));
    reload();
}

void RepositoryView::deleteFiles(const QList<FileRef> &files)
{
    QStringList names;
    for (const FileRef &f : files)
        names << f.path;
    const auto answer = QMessageBox::question(
        this, tr("Borrar archivos"),
        tr("¿Mover a la papelera estos archivos?\n\n%1").arg(names.join(QLatin1Char('\n'))),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;
    QStringList failed;
    for (const FileRef &f : files)
        if (!QFile::moveToTrash(absolutePath(f.path)))
            failed << f.path;
    if (!failed.isEmpty())
        QMessageBox::warning(this, tr("Borrar archivos"),
                             tr("No se pudieron mover a la papelera:\n\n%1").arg(failed.join(QLatin1Char('\n'))));
    else
        Q_EMIT statusMessage(plural(int(files.size()), tr("Archivo movido a la papelera"),
                                    tr("%1 archivos movidos a la papelera")));
    reload();
}

void RepositoryView::openContainingFolder(const QString &path)
{
    const QString abs = absolutePath(path);
    const QFileInfo info(abs);
#ifdef Q_OS_WIN
    if (info.exists()) {
        // Abre el Explorador con el archivo seleccionado
        QProcess process;
        process.setProgram(QStringLiteral("explorer.exe"));
        process.setNativeArguments(QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(abs)));
        if (process.startDetached())
            return;
    }
#endif
    QString dir = info.absolutePath();
    while (!QFileInfo(dir).isDir() && dir.size() > m_repo.workdir().size())
        dir = QFileInfo(dir).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

QWidget *RepositoryView::backHeader(const QString &title, QWidget *parent)
{
    auto *bar = new QWidget(parent);
    bar->setAutoFillBackground(true);
    QPalette p = bar->palette();
    p.setColor(QPalette::Window, Theme::colors().backgroundAlt);
    bar->setPalette(p);
    auto *l = new QHBoxLayout(bar);
    l->setContentsMargins(10, 6, 10, 6);
    auto *back = new QToolButton(bar);
    back->setText(tr("← Volver"));
    connect(back, &QToolButton::clicked, this, &RepositoryView::showSelection);
    auto *label = new QLabel(title, bar);
    label->setTextFormat(Qt::RichText);
    l->addWidget(back);
    l->addSpacing(8);
    l->addWidget(label, 1);
    return bar;
}

void RepositoryView::showFileHistory(const QString &path)
{
    const QList<GitRepository::CommitInfo> history = m_repo.fileHistory(path);
    const QHash<QString, QStringList> labels = m_repo.refLabels();
    const QString head = m_repo.headCommitId();

    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(backHeader(tr("<b>Historial</b> de %1 &nbsp;<span style='color:%2'>%3</span>")
                                     .arg(path.toHtmlEscaped(), Theme::colors().muted.name(),
                                          plural(int(history.size()), tr("1 commit"), tr("%1 commits"))),
                                 content));

    if (history.isEmpty()) {
        auto *empty = mutedLabel(tr("Este archivo no aparece en ningún commit de la rama actual."), content);
        empty->setAlignment(Qt::AlignCenter);
        empty->setContentsMargins(0, 40, 0, 0);
        layout->addWidget(empty);
        layout->addStretch(1);
    } else {
        auto *list = new QListWidget(content);
        list->setItemDelegate(new CommitDelegate(list));
        list->setMouseTracking(true);
        list->setUniformItemSizes(true);
        list->setFrameShape(QFrame::NoFrame);
        for (const auto &ci : history) {
            auto *item = new QListWidgetItem(list);
            item->setData(KindRole, 1);
            item->setData(IdRole, ci.id);
            item->setData(SummaryRole, ci.summary);
            item->setData(AuthorRole, ci.author);
            item->setData(DateRole, formatDate(ci.time));
            item->setData(LabelsRole, labels.value(ci.id));
            item->setData(MergeRole, ci.parentCount > 1);
            item->setData(HeadRole, ci.id == head);
            item->setToolTip(tr("Clic para ver el commit completo"));
        }
        connect(list, &QListWidget::itemClicked, this,
                [this](QListWidgetItem *item) { selectCommit(item->data(IdRole).toString()); });
        layout->addWidget(list, 1);
    }
    setDetailWidget(content);
}

void RepositoryView::showBlame(const QString &path)
{
    const QList<GitRepository::BlameLine> lines = m_repo.blame(path);
    if (lines.isEmpty() && QFileInfo(absolutePath(path)).size() > 0) {
        report(false, tr("calcular el blame de %1").arg(path));
        return;
    }

    // Columna: SHA corto, autor, fecha y número de línea; solo al empezar cada bloque
    constexpr int kAuthorWidth = 16;
    const int numberWidth = int(QString::number(lines.size()).size());
    QStringList out;
    QStringList commitPerLine;
    QString previous = QStringLiteral("-");
    for (int i = 0; i < lines.size(); ++i) {
        const GitRepository::BlameLine &l = lines.at(i);
        QString info;
        if (l.commitId != previous) {
            if (l.commitId.isEmpty()) {
                info = tr("·······  sin confirmar");
            } else {
                QString author = l.author.left(kAuthorWidth);
                info = QStringLiteral("%1  %2  %3").arg(l.commitId.left(7), author.leftJustified(kAuthorWidth),
                                                        l.time.toString(QStringLiteral("yyyy-MM-dd")));
            }
        }
        const int infoWidth = 7 + 2 + kAuthorWidth + 2 + 10;
        out << QStringLiteral("%1 %2 %3 %4 %5")
                   .arg(info.leftJustified(infoWidth, QLatin1Char(' '), true), QString(kBlameSeparator),
                        QString::number(i + 1).rightJustified(numberWidth), QString(kBlameSeparator), l.text);
        commitPerLine << l.commitId;
        previous = l.commitId;
    }

    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(backHeader(tr("<b>Blame</b> de %1 &nbsp;<span style='color:%2'>doble clic en una línea para ver su commit</span>")
                                     .arg(path.toHtmlEscaped(), Theme::colors().muted.name()),
                                 content));
    auto *view = new BlameView([this, commitPerLine](int line) {
        if (line >= 0 && line < commitPerLine.size() && !commitPerLine.at(line).isEmpty())
            selectCommit(commitPerLine.at(line));
    }, content);
    new BlameHighlighter(view->document());
    view->setPlainText(out.join(QLatin1Char('\n')));
    layout->addWidget(view, 1);
    setDetailWidget(content);
}

// ---------------------------------------------------------------------------
// Operaciones remotas: pull, push, fetch, merge y otros comandos
// ---------------------------------------------------------------------------
namespace {

const char *kPullSetting = "git/pullCommand";
const char *kPushSetting = "git/pushCommand";

QString gitExecutable()
{
    QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
#ifdef Q_OS_WIN
    if (git.isEmpty()) {
        const QStringList candidates = {
            QStringLiteral("C:/Program Files/Git/cmd/git.exe"),
            QStringLiteral("C:/Program Files (x86)/Git/cmd/git.exe"),
            QDir::homePath() + QStringLiteral("/AppData/Local/Programs/Git/cmd/git.exe"),
            QDir::homePath() + QStringLiteral("/scoop/shims/git.exe"),
        };
        for (const QString &c : candidates)
            if (QFileInfo::exists(c))
                return c;
    }
#endif
    return git;
}

// Última línea útil de la salida de git (las de progreso terminan en \r)
QString lastLine(const QString &output)
{
    const QStringList lines = output.split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::SkipEmptyParts);
    for (int i = lines.size() - 1; i >= 0; --i) {
        const QString l = lines.at(i).trimmed();
        if (!l.isEmpty() && !l.startsWith(QLatin1String("$ ")))
            return l;
    }
    return QString();
}

QToolButton *toolbarButton(const QString &text, const QString &tip, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setText(text);
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

// Para el aviso de error: la primera línea fatal:/error:/CONFLICT/rechazo, o la última
QString errorLine(const QString &output)
{
    const QStringList lines = output.split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::SkipEmptyParts);
    for (const QString &raw : lines) {
        const QString l = raw.trimmed();
        if (l.startsWith(QLatin1String("fatal:")) || l.startsWith(QLatin1String("error:"))
            || l.startsWith(QLatin1String("CONFLICT")) || l.contains(QLatin1String("[rejected]")))
            return l;
    }
    return lastLine(output);
}

} // namespace

QWidget *RepositoryView::buildRemoteButtons(QWidget *parent)
{
    auto *box = new QWidget(parent);
    auto *l = new QHBoxLayout(box);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);

    const QString joinedLeft = QStringLiteral("QToolButton { border-top-right-radius:0; border-bottom-right-radius:0; }");
    const QString joinedRight = QStringLiteral(
        "QToolButton { border-top-left-radius:0; border-bottom-left-radius:0; border-left:none; padding:4px 5px; }");

    m_mergeButton = toolbarButton(tr("⇄ Merge"), tr("Fusionar otra rama en la actual (Ctrl+Alt+M)"), box);
    m_fetchButton = toolbarButton(tr("⟲ Fetch"), tr("Descargar cambios sin tocar tu rama (Ctrl+Alt+F)"), box);
    m_pullButton = toolbarButton(tr("↓ Pull"), QString(), box);
    m_pullMenuButton = toolbarButton(QStringLiteral("▾"), tr("Opciones de pull (Ctrl+Alt+Shift+P)"), box);
    m_pushButton = toolbarButton(tr("↑ Push"), QString(), box);
    m_pushMenuButton = toolbarButton(QStringLiteral("▾"), tr("Opciones de push (Ctrl+Alt+Shift+U)"), box);
    m_moreButton = toolbarButton(QStringLiteral("⋯"), tr("Más comandos de Git (Ctrl+Shift+P)"), box);
    m_pullButton->setStyleSheet(joinedLeft);
    m_pushButton->setStyleSheet(joinedLeft);
    m_pullMenuButton->setStyleSheet(joinedRight);
    m_pushMenuButton->setStyleSheet(joinedRight);

    connect(m_mergeButton, &QToolButton::clicked, this, &RepositoryView::showMergePalette);
    connect(m_fetchButton, &QToolButton::clicked, this, &RepositoryView::fetch);
    connect(m_pullButton, &QToolButton::clicked, this, &RepositoryView::pull);
    connect(m_pullMenuButton, &QToolButton::clicked, this, &RepositoryView::showPullOptions);
    connect(m_pushButton, &QToolButton::clicked, this, &RepositoryView::push);
    connect(m_pushMenuButton, &QToolButton::clicked, this, &RepositoryView::showPushOptions);
    connect(m_moreButton, &QToolButton::clicked, this, &RepositoryView::showCommandPalette);

    l->addWidget(m_mergeButton);
    l->addSpacing(6);
    l->addWidget(m_fetchButton);
    l->addSpacing(6);
    l->addWidget(m_pullButton);
    l->addWidget(m_pullMenuButton);
    l->addSpacing(6);
    l->addWidget(m_pushButton);
    l->addWidget(m_pushMenuButton);
    l->addSpacing(6);
    l->addWidget(m_moreButton);
    return box;
}

void RepositoryView::updateRemoteButtons()
{
    if (!m_pullButton)
        return;
    const bool busy = isBusy();
    const bool hasRemotes = !m_repo.remoteNames().isEmpty();
    int ahead = 0, behind = 0;
    const bool tracking = m_repo.aheadBehind(&ahead, &behind);

    m_pullButton->setText(behind > 0 ? tr("↓ Pull  %1").arg(behind) : tr("↓ Pull"));
    m_pushButton->setText(ahead > 0 ? tr("↑ Push  %1").arg(ahead) : tr("↑ Push"));
    const QSettings settings;
    const QString pullCmd = settings.value(QLatin1String(kPullSetting), QStringLiteral("pull")).toString();
    const QString pushCmd = settings.value(QLatin1String(kPushSetting), QStringLiteral("push")).toString();
    QString state;
    if (tracking)
        state = tr("\nRama remota: %1 · %2 por bajar, %3 por subir").arg(m_repo.upstreamName()).arg(behind).arg(ahead);
    else if (hasRemotes)
        state = tr("\nEsta rama aún no tiene rama remota");
    m_pullButton->setToolTip(tr("git %1 (Ctrl+Alt+P)").arg(pullCmd) + state);
    m_pushButton->setToolTip(tr("git %1 (Ctrl+Alt+U)").arg(pushCmd) + state);

    for (QToolButton *b : {m_pullButton, m_pullMenuButton, m_pushButton, m_pushMenuButton, m_fetchButton})
        b->setEnabled(!busy && hasRemotes);
    m_mergeButton->setEnabled(!busy);
    m_moreButton->setEnabled(!busy);
    // Que el texto (con los contadores) nunca se recorte
    for (QToolButton *b : {m_mergeButton, m_fetchButton, m_pullButton, m_pushButton})
        b->setMinimumWidth(b->sizeHint().width());
}

void RepositoryView::runGit(const QStringList &args, const QString &title)
{
    if (m_process) {
        QMessageBox::information(this, tr("Git"), tr("Espera a que termine «%1».").arg(m_runningTitle));
        return;
    }
    const QString git = gitExecutable();
    if (git.isEmpty()) {
        QMessageBox::warning(this, tr("Git"),
                             tr("No se encontró el programa git.\n\n"
                                "Las operaciones remotas (pull, push, fetch…) usan tu instalación de Git "
                                "y sus credenciales. Instálalo desde https://git-scm.com y reinicia BatCode."));
        return;
    }

    const int run = ++m_runCounter;
    m_runningTitle = title.isEmpty() ? QStringLiteral("git ") + args.join(QLatin1Char(' ')) : title;
    m_commandOutput = QStringLiteral("$ git %1\n").arg(args.join(QLatin1Char(' ')));

    m_process = new QProcess(this);
    m_process->setProgram(git);
    m_process->setArguments(args);
    m_process->setWorkingDirectory(m_repo.workdir());
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0")); // nunca esperar una contraseña en consola
    env.insert(QStringLiteral("GIT_MERGE_AUTOEDIT"), QStringLiteral("no"));  // merge sin abrir un editor
    env.insert(QStringLiteral("GIT_EDITOR"), QStringLiteral(":"));            // rebase --continue, etc. sin editor
    m_process->setProcessEnvironment(env);
#ifdef Q_OS_WIN
    // Sin ventana de consola
    m_process->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *a) { a->flags |= CREATE_NO_WINDOW; });
#endif

    const Theme::Colors &c = Theme::colors();
    QPalette p = m_outputBar->palette();
    p.setColor(QPalette::Window, c.panel);
    m_outputBar->setPalette(p);
    m_outputLabel->setText(QStringLiteral("<span style='color:%1'>⟳</span>&nbsp; <b>%2</b>…")
                               .arg(c.accent.name(), m_runningTitle.toHtmlEscaped()));
    m_outputDetails->setVisible(false);
    m_outputBar->show();

    connect(m_process, &QProcess::readyRead, this, [this] {
        m_commandOutput += QString::fromUtf8(m_process->readAll());
        const QString progress = lastLine(m_commandOutput);
        m_outputLabel->setText(QStringLiteral("<span style='color:%1'>⟳</span>&nbsp; <b>%2</b> &nbsp;<span style='color:%3'>%4</span>")
                                   .arg(Theme::colors().accent.name(), m_runningTitle.toHtmlEscaped(),
                                        Theme::colors().muted.name(), progress.toHtmlEscaped()));
    });
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        m_commandOutput += QString::fromUtf8(m_process->readAll());
        finishCommand(status == QProcess::NormalExit && code == 0);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this, run](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || run != m_runCounter || !m_process)
            return;
        m_commandOutput += tr("No se pudo ejecutar git: %1\n").arg(m_process->errorString());
        finishCommand(false);
    });

    updateRemoteButtons();
    m_process->start();
}

void RepositoryView::finishCommand(bool ok)
{
    if (!m_process)
        return;
    m_process->deleteLater();
    m_process = nullptr;

    const Theme::Colors &c = Theme::colors();
    const QString summary = ok ? lastLine(m_commandOutput) : errorLine(m_commandOutput);
    QPalette p = m_outputBar->palette();
    p.setColor(QPalette::Window, ok ? c.addedBg : c.removedBg);
    m_outputBar->setPalette(p);
    m_outputLabel->setText(QStringLiteral("<span style='color:%1'>%2</span>&nbsp; <b>%3</b> &nbsp;%4")
                               .arg((ok ? c.added : c.removed).name(), ok ? QStringLiteral("✓") : QStringLiteral("✗"),
                                    m_runningTitle.toHtmlEscaped(),
                                    (ok ? (summary.isEmpty() ? tr("completado") : summary)
                                        : (summary.isEmpty() ? tr("falló") : summary))
                                        .toHtmlEscaped()));
    m_outputDetails->setVisible(true);
    m_outputBar->show();
    if (ok) {
        // El aviso de éxito desaparece solo; el de error se queda hasta cerrarlo
        const int run = m_runCounter;
        QTimer::singleShot(8000, this, [this, run] {
            if (run == m_runCounter && !m_process)
                m_outputBar->hide();
        });
    }
    Q_EMIT statusMessage(ok ? tr("%1: completado").arg(m_runningTitle) : tr("%1: falló").arg(m_runningTitle));
    reload();
    Q_EMIT commandFinished(ok, m_commandOutput);
}

void RepositoryView::showCommandOutput()
{
    QDialog dialog(this);
    dialog.setWindowTitle(m_runningTitle);
    dialog.resize(820, 460);
    auto *text = new QPlainTextEdit(&dialog);
    text->setReadOnly(true);
    text->setFont(Theme::editorFont());
    QString cleaned = m_commandOutput;
    cleaned.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    // De las líneas de progreso (separadas por \r) se conserva solo la última
    QStringList lines;
    for (const QString &line : cleaned.split(QLatin1Char('\n')))
        lines << line.section(QLatin1Char('\r'), -1);
    text->setPlainText(lines.join(QLatin1Char('\n')));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(text, 1);
    layout->addWidget(buttons);
    dialog.exec();
}

void RepositoryView::pull()
{
    if (m_repo.upstreamName().isEmpty()) {
        QMessageBox::information(this, tr("Pull"),
                                 tr("La rama «%1» no tiene rama remota.\n\nUsa Push para publicarla, "
                                    "o elige un fetch en las opciones de pull.").arg(m_repo.branchName()));
        return;
    }
    const QString command = QSettings().value(QLatin1String(kPullSetting), QStringLiteral("pull")).toString();
    QStringList args = command.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    args.insert(1, QStringLiteral("--progress"));
    runGit(args, QStringLiteral("git ") + command);
}

void RepositoryView::fetch()
{
    runGit({QStringLiteral("fetch"), QStringLiteral("--progress")}, QStringLiteral("git fetch"));
}

void RepositoryView::push()
{
    if (m_repo.isHeadDetached()) {
        QMessageBox::information(this, tr("Push"), tr("No hay ninguna rama activa (HEAD desacoplado)."));
        return;
    }
    const QString branch = m_repo.branchName();
    if (m_repo.upstreamName().isEmpty()) {
        const QStringList remotes = m_repo.remoteNames();
        if (remotes.isEmpty())
            return;
        const QString remote = remotes.contains(QLatin1String("origin")) ? QStringLiteral("origin") : remotes.first();
        const auto answer = QMessageBox::question(
            this, tr("Publicar rama"),
            tr("La rama «%1» todavía no existe en el remoto.\n\n¿Publicarla como «%2/%1»?").arg(branch, remote));
        if (answer != QMessageBox::Yes)
            return;
        runGit({QStringLiteral("push"), QStringLiteral("--progress"), QStringLiteral("--set-upstream"), remote, branch},
               QStringLiteral("git push --set-upstream %1 %2").arg(remote, branch));
        return;
    }
    const QString command = QSettings().value(QLatin1String(kPushSetting), QStringLiteral("push")).toString();
    if (command.contains(QLatin1String("--force"))) {
        const auto answer = QMessageBox::warning(
            this, tr("Push forzado"),
            tr("El botón Push está configurado como «git %1».\n\nPuede sobrescribir commits del remoto. ¿Continuar?")
                .arg(command),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes)
            return;
    }
    QStringList args = command.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    args.insert(1, QStringLiteral("--progress"));
    runGit(args, QStringLiteral("git ") + command);
}

void RepositoryView::showPullOptions()
{
    const bool tracking = !m_repo.upstreamName().isEmpty();
    struct Option {
        const char *command;
        QString description;
        bool isPull;
    };
    const QList<Option> options = {
        {"pull", tr("Descarga los cambios del remoto y los fusiona en la rama actual"), true},
        {"pull --ff-only", tr("Descarga los cambios y solo avanza la rama si no hace falta fusionar"), true},
        {"pull --rebase", tr("Descarga los cambios y reaplica tus commits encima"), true},
        {"pull --rebase --autostash", tr("Guarda los cambios sin confirmar antes de pull --rebase y los recupera después"), true},
        {"fetch", tr("Descarga los cambios del remoto sin tocar tu rama"), false},
        {"fetch --tags", tr("Descarga los cambios del remoto, incluidas todas las etiquetas"), false},
        {"fetch --prune", tr("Descarga los cambios y borra las ramas remotas que ya no existen"), false},
        {"fetch --all", tr("Descarga los cambios de todos los remotos"), false},
    };
    QList<GitCommandPalette::Item> items;
    for (const Option &o : options) {
        const QString command = QString::fromLatin1(o.command);
        GitCommandPalette::Item item;
        item.command = command;
        item.description = o.description;
        item.enabled = !o.isPull || tracking;
        const bool isPull = o.isPull;
        item.action = [this, command, isPull] {
            if (isPull) // la opción elegida pasa a ser la del botón Pull
                QSettings().setValue(QLatin1String(kPullSetting), command);
            QStringList args = command.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            args.insert(1, QStringLiteral("--progress"));
            runGit(args, QStringLiteral("git ") + command);
            updateRemoteButtons();
        };
        items << item;
    }
    auto *palette = new GitCommandPalette(tr("Pull"), tr("Opciones de pull (la elegida queda como la del botón)"), items, this);
    palette->setCurrentCommand(QSettings().value(QLatin1String(kPullSetting), QStringLiteral("pull")).toString());
    palette->popup(this);
}

void RepositoryView::showPushOptions()
{
    const bool detached = m_repo.isHeadDetached();
    const bool tracking = !m_repo.upstreamName().isEmpty();
    const QString branch = m_repo.branchName();

    QList<GitCommandPalette::Item> items;
    auto add = [&](const QString &command, const QString &description, bool enabled, bool remember, bool confirmForce) {
        GitCommandPalette::Item item;
        item.command = command;
        item.description = description;
        item.enabled = enabled && !detached;
        item.action = [this, command, remember, confirmForce] {
            if (confirmForce) {
                const auto answer = QMessageBox::warning(
                    this, tr("Push forzado"),
                    tr("«git %1» puede sobrescribir commits del remoto si alguien más los subió.\n\n¿Continuar?")
                        .arg(command),
                    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
                if (answer != QMessageBox::Yes)
                    return;
            }
            if (remember)
                QSettings().setValue(QLatin1String(kPushSetting), command);
            QStringList args = command.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            args.insert(1, QStringLiteral("--progress"));
            runGit(args, QStringLiteral("git ") + command);
            updateRemoteButtons();
        };
        items << item;
    };
    add(QStringLiteral("push"), tr("Envía los commits de la rama actual a su rama remota"), tracking, true, false);
    for (const QString &remote : m_repo.remoteNames())
        add(QStringLiteral("push --set-upstream %1 %2").arg(remote, branch),
            tr("Publica la rama en «%1» y la enlaza como su rama remota").arg(remote), true, false, false);
    add(QStringLiteral("push --force-with-lease"),
        tr("Sobrescribe la rama remota solo si nadie más la ha cambiado (tras un rebase)"), tracking, false, true);
    add(QStringLiteral("push --follow-tags"), tr("Envía los commits y las etiquetas anotadas que apuntan a ellos"),
        tracking, true, false);
    add(QStringLiteral("push --tags"), tr("Envía todas las etiquetas locales"), true, false, false);

    auto *palette = new GitCommandPalette(tr("Push"), tr("Opciones de push"), items, this);
    palette->setCurrentCommand(QSettings().value(QLatin1String(kPushSetting), QStringLiteral("push")).toString());
    palette->popup(this);
}

QStringList RepositoryView::mergeableBranches(QStringList *descriptions) const
{
    QStringList names;
    const QString current = m_repo.branchName();
    const GitRepository::Locations loc = m_repo.locations();
    for (const auto &b : loc.branches) {
        if (b.name == current)
            continue;
        names << b.name;
        descriptions->append(tr("rama local"));
    }
    for (auto it = loc.remotes.cbegin(); it != loc.remotes.cend(); ++it)
        for (const auto &b : it.value()) {
            names << b.name;
            descriptions->append(tr("rama remota de «%1»").arg(it.key()));
        }
    return names;
}

void RepositoryView::showMergePalette()
{
    if (m_repo.isHeadDetached()) {
        QMessageBox::information(this, tr("Merge"), tr("No hay ninguna rama activa (HEAD desacoplado)."));
        return;
    }
    QStringList descriptions;
    const QStringList branches = mergeableBranches(&descriptions);
    QList<GitCommandPalette::Item> items;
    for (int i = 0; i < branches.size(); ++i) {
        const QString branch = branches.at(i);
        items.append({branch, descriptions.at(i), [this, branch] { showMergeOptions(branch); }, true});
    }
    if (items.isEmpty()) {
        QMessageBox::information(this, tr("Merge"), tr("No hay otras ramas que fusionar."));
        return;
    }
    auto *palette = new GitCommandPalette(tr("Merge"), tr("Rama que se fusionará en «%1»").arg(m_repo.branchName()),
                                          items, this);
    palette->popup(this);
}

void RepositoryView::showMergeOptions(const QString &branch)
{
    const QString current = m_repo.branchName();
    QList<GitCommandPalette::Item> items;
    auto add = [&](const QStringList &flags, const QString &description) {
        QStringList args = {QStringLiteral("merge")};
        args << flags;
        const QString shown = (args + QStringList{branch}).join(QLatin1Char(' '));
        if (!flags.contains(QLatin1String("--squash")))
            args << QStringLiteral("--no-edit");
        args << branch;
        items.append({shown, description, [this, args, shown] { runGit(args, QStringLiteral("git ") + shown); }, true});
    };
    add({}, tr("Fusiona «%1» en «%2» (avance rápido si es posible)").arg(branch, current));
    add({QStringLiteral("--no-ff")}, tr("Fusiona creando siempre un commit de merge"));
    add({QStringLiteral("--ff-only")}, tr("Solo avanza «%1» si no hace falta fusionar").arg(current));
    add({QStringLiteral("--squash")}, tr("Deja los cambios de «%1» preparados, sin hacer commit").arg(branch));
    auto *palette = new GitCommandPalette(tr("Merge"), tr("Opciones para fusionar «%1» en «%2»").arg(branch, current),
                                          items, this);
    palette->popup(this);
}

void RepositoryView::showSwitchPalette()
{
    const QString current = m_repo.branchName();
    QList<GitCommandPalette::Item> items;
    for (const auto &b : m_repo.locations().branches) {
        if (b.name == current)
            continue;
        const QString name = b.name;
        items.append({name, tr("rama local"), [this, name] {
                          runGit({QStringLiteral("switch"), name}, QStringLiteral("git switch ") + name);
                      }, true});
    }
    if (items.isEmpty()) {
        QMessageBox::information(this, tr("Cambiar de rama"), tr("No hay otras ramas locales."));
        return;
    }
    auto *palette = new GitCommandPalette(tr("Switch"), tr("Rama a la que cambiar"), items, this);
    palette->popup(this);
}

void RepositoryView::createBranch()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Crear rama"),
                                               tr("Nombre de la nueva rama (se crea desde «%1» y se cambia a ella):")
                                                   .arg(m_repo.branchName()),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (ok && !name.isEmpty())
        runGit({QStringLiteral("switch"), QStringLiteral("-c"), name}, QStringLiteral("git switch -c ") + name);
}

void RepositoryView::showCommandPalette()
{
    const bool hasRemotes = !m_repo.remoteNames().isEmpty();
    const bool tracking = !m_repo.upstreamName().isEmpty();
    const bool detached = m_repo.isHeadDetached();
    auto run = [this](const QStringList &args) {
        return [this, args] { runGit(args); };
    };
    const QList<GitCommandPalette::Item> items = {
        {tr("Pull"), tr("Ejecuta la opción de pull del botón (Ctrl+Alt+P)"), [this] { pull(); }, tracking},
        {tr("Pull…"), tr("Elegir una opción de pull o fetch (Ctrl+Alt+Shift+P)"), [this] { showPullOptions(); }, hasRemotes},
        {tr("Push"), tr("Envía los commits a la rama remota (Ctrl+Alt+U)"), [this] { push(); }, hasRemotes && !detached},
        {tr("Push…"), tr("Elegir una opción de push (Ctrl+Alt+Shift+U)"), [this] { showPushOptions(); }, hasRemotes},
        {QStringLiteral("fetch"), tr("Descarga los cambios del remoto (Ctrl+Alt+F)"), [this] { fetch(); }, hasRemotes},
        {QStringLiteral("fetch --all --prune"), tr("Descarga de todos los remotos y borra las ramas remotas eliminadas"),
         run({QStringLiteral("fetch"), QStringLiteral("--progress"), QStringLiteral("--all"), QStringLiteral("--prune")}), hasRemotes},
        {tr("Merge…"), tr("Fusionar otra rama en la actual (Ctrl+Alt+M)"), [this] { showMergePalette(); }, !detached},
        {tr("Cambiar de rama…"), tr("git switch a otra rama local"), [this] { showSwitchPalette(); }, true},
        {tr("Crear rama…"), tr("git switch -c: nueva rama desde la actual"), [this] { createBranch(); }, true},
        {QStringLiteral("stash"), tr("Guarda los cambios sin confirmar (incluidos los nuevos) y limpia el directorio"),
         run({QStringLiteral("stash"), QStringLiteral("push"), QStringLiteral("--include-untracked")}), true},
        {QStringLiteral("stash pop"), tr("Recupera el último stash y lo elimina"), run({QStringLiteral("stash"), QStringLiteral("pop")}), true},
        {QStringLiteral("merge --abort"), tr("Cancela un merge con conflictos y vuelve al estado anterior"),
         run({QStringLiteral("merge"), QStringLiteral("--abort")}), true},
        {QStringLiteral("rebase --continue"), tr("Continúa un rebase después de resolver los conflictos"),
         run({QStringLiteral("rebase"), QStringLiteral("--continue")}), true},
        {QStringLiteral("rebase --abort"), tr("Cancela el rebase en curso"), run({QStringLiteral("rebase"), QStringLiteral("--abort")}), true},
    };
    auto *palette = new GitCommandPalette(tr("Git"), tr("Buscar un comando…"), items, this);
    palette->popup(this);
}

// ---------------------------------------------------------------------------
// Secciones de cambios: plegar / desplegar y menú contextual
// ---------------------------------------------------------------------------
void RepositoryView::toggleSection(const QString &id)
{
    if (!m_collapsedSections.remove(id))
        m_collapsedSections.insert(id);
    showWorkingDirectory();
}

void RepositoryView::setAllSectionsCollapsed(bool collapsed)
{
    m_collapsedSections.clear();
    if (collapsed)
        for (auto it = m_sectionFiles.cbegin(); it != m_sectionFiles.cend(); ++it)
            m_collapsedSections.insert(it.key());
    showWorkingDirectory();
}

void RepositoryView::showSectionMenu(const QString &id, const QPoint &globalPos)
{
    const QList<FileRef> files = m_sectionFiles.value(id);
    if (files.isEmpty())
        return;
    const int n = int(files.size());
    QMenu menu(this);
    if (id == QLatin1String("unstaged")) {
        menu.addAction(plural(n, tr("Preparar 1 archivo modificado"), tr("Preparar %1 archivos modificados")),
                       this, [this, files] { stageFiles(files, true); });
        menu.addAction(plural(n, tr("Descartar cambios de 1 archivo…"), tr("Descartar cambios de %1 archivos…")),
                       this, [this, files] { discardFiles(files); });
        menu.addSeparator();
        menu.addAction(tr("Guardar parche…"), this, [this] { savePatch(false); });
    } else if (id == QLatin1String("untracked")) {
        menu.addAction(plural(n, tr("Preparar 1 archivo sin seguimiento"), tr("Preparar %1 archivos sin seguimiento")),
                       this, [this, files] { stageFiles(files, true); });
        menu.addAction(plural(n, tr("Borrar 1 archivo sin seguimiento…"), tr("Borrar %1 archivos sin seguimiento…")),
                       this, [this, files] { deleteFiles(files); });
    } else if (id == QLatin1String("staged")) {
        menu.addAction(plural(n, tr("Quitar 1 archivo de preparados"), tr("Quitar %1 archivos de preparados")),
                       this, [this, files] { stageFiles(files, false); });
        menu.addSeparator();
        menu.addAction(tr("Guardar parche…"), this, [this] { savePatch(true); });
    }
    menu.addSeparator();
    const bool collapsed = m_collapsedSections.contains(id);
    menu.addAction(collapsed ? tr("Desplegar") : tr("Plegar"), this, [this, id] { toggleSection(id); });
    menu.addAction(tr("Desplegar todo"), this, [this] { setAllSectionsCollapsed(false); });
    menu.addAction(tr("Plegar todo"), this, [this] { setAllSectionsCollapsed(true); });
    menu.exec(globalPos);
}

// Guarda en un archivo .patch el diff de los cambios sin preparar o preparados
// (git diff [--cached] --binary), listo para "git apply".
void RepositoryView::savePatch(bool staged)
{
    const QString suggested = QDir(m_repo.workdir()).absoluteFilePath(
        QFileInfo(QDir::cleanPath(m_repo.workdir())).fileName() + (staged ? QStringLiteral("-staged.patch")
                                                                          : QStringLiteral(".patch")));
    const QString path = QFileDialog::getSaveFileName(this, tr("Guardar parche"), suggested,
                                                      tr("Parches (*.patch *.diff);;Todos los archivos (*)"));
    if (path.isEmpty())
        return;
    QProcess git;
    git.setWorkingDirectory(m_repo.workdir());
    QStringList args{QStringLiteral("diff"), QStringLiteral("--binary")};
    if (staged)
        args << QStringLiteral("--cached");
    git.start(QStringLiteral("git"), args);
    const bool ran = git.waitForFinished(60000) && git.exitStatus() == QProcess::NormalExit && git.exitCode() == 0;
    QFile out(path);
    if (!ran || !out.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Guardar parche"),
                             tr("No se pudo guardar el parche.\n\n%1")
                                 .arg(ran ? out.errorString() : QString::fromLocal8Bit(git.readAllStandardError())));
        return;
    }
    out.write(git.readAllStandardOutput());
    Q_EMIT statusMessage(tr("Parche guardado en %1").arg(QDir::toNativeSeparators(path)));
}
