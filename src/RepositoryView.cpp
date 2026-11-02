#include "RepositoryView.h"

#include "FileDiffWidget.h"
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
        if (m_files->itemAt(pos) && !selectedFiles().isEmpty())
            showFileMenu(m_files->viewport()->mapToGlobal(pos));
    });
    for (int key : {int(Qt::Key_Return), int(Qt::Key_Enter)}) {
        auto *sc = new QShortcut(QKeySequence(Qt::SHIFT | key), m_files);
        sc->setContext(Qt::WidgetShortcut);
        connect(sc, &QShortcut::activated, this, &RepositoryView::toggleStageSelected);
    }
    connect(m_files, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
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

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_sidebar);
    splitter->addWidget(m_middleTabs);
    splitter->addWidget(m_detail);
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

void RepositoryView::setDetailWidget(QWidget *content)
{
    m_fileWidgets.clear();
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

    // --- Formulario de commit ---
    auto *form = new QWidget(content);
    auto *fl = new QVBoxLayout(form);
    fl->setContentsMargins(12, 12, 12, 12);
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
    layout->addWidget(form);

    // --- Lista de archivos (pestaña "Archivos") ---
    // Se conserva la selección por ruta (un archivo preparado sigue seleccionado en su nueva sección)
    QSet<QString> previouslySelected;
    if (m_filesAreWorking)
        for (const FileRef &f : selectedFiles())
            previouslySelected.insert(f.path);
    m_files->clear();
    m_filesAreWorking = true;
    auto addFileHeader = [this](const QString &text) {
        auto *h = new QListWidgetItem(text.toUpper(), m_files);
        h->setFlags(Qt::NoItemFlags);
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
        QHBoxLayout *actions = nullptr;
        layout->addWidget(sectionTitle(title, int(files.size()), content, &actions));
        auto *bulk = new QToolButton(content);
        bulk->setText(bulkText);
        actions->addWidget(bulk);
        connect(bulk, &QToolButton::clicked, this, [this, bulkAction, bulkText] {
            report(bulkAction(), bulkText.toLower());
            reload();
        });
        addFileHeader(title);

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
    addSection(QStringLiteral("staged"), tr("Preparados"), staged, tr("Quitar todo"), [this] { return m_repo.unstageAll(); });
    addSection(QStringLiteral("unstaged"), tr("Sin preparar"), unstaged, tr("Preparar todo"), [this, unstaged] {
        for (const auto &f : unstaged)
            if (!m_repo.stage(f.path))
                return false;
        return true;
    });
    addSection(QStringLiteral("untracked"), tr("Sin seguimiento"), untracked, tr("Preparar todo"), [this, untracked] {
        for (const auto &f : untracked)
            if (!m_repo.stage(f.path))
                return false;
        return true;
    });

    if (staged.isEmpty() && unstagedAll.isEmpty()) {
        auto *clean = mutedLabel(tr("No hay cambios en el directorio de trabajo."), content);
        clean->setAlignment(Qt::AlignCenter);
        clean->setContentsMargins(0, 40, 0, 0);
        layout->addWidget(clean);
    }
    layout->addStretch(1);

    setDetailWidget(content);
    m_fileWidgets = widgets;
    Q_UNUSED(c);
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
    if (kind == RefBranch)
        menu.addAction(tr("Cambiar a «%1»").arg(name), this, [this, name] { checkout(name); });
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
