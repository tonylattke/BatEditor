#include "RepositoryView.h"

#include "CommitGraph.h"
#include "FileDiffWidget.h"
#include "GitCommandPalette.h"
#include "RebaseHelper.h"
#include "Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDir>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
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
#include <QTreeWidgetItemIterator>
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

#include <algorithm>
#include <functional>
#include <utility>

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
    RemoteRole,   // Ubicaciones: remoto de una rama remota ("origin")
    GraphRole,    // Commits: CommitGraph::Row de la fila
};

enum RefKind { RefNone, RefBranch, RefRemoteBranch, RefTag, RefStash, RefSubmodule, RefFolder, RefWorktree,
               RefOther };

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

        const int cy = r.top() + r.height() / 2;
        const bool merge = index.data(MergeRole).toBool();
        const QVariant graphData = index.data(GraphRole);
        QColor nodeColor = c.accent;
        int x0 = r.left() + 34;
        if (graphData.isValid()) {
            const auto g = graphData.value<CommitGraph::Row>();
            nodeColor = laneColor(g.color);
            x0 = paintGraph(p, r, g, working, merge);
        } else {
            // Sin grafo (historial de un archivo): una sola línea
            const int gx = r.left() + 16;
            p->setPen(QPen(c.accent.darker(150), 2));
            p->drawLine(gx, row == 0 ? cy : r.top(), gx, row == rows - 1 ? cy : r.bottom() + 1);
            p->setPen(Qt::NoPen);
            p->setBrush(c.accent);
            p->drawEllipse(QPoint(gx, cy), merge ? 6 : 4, merge ? 6 : 4);
        }

        QFont base = option.font;
        const QFontMetrics fm(base);
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
            QColor bg = isTag ? c.preproc.darker(170) : (isRemote ? c.border.lighter(130) : nodeColor.darker(150));
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

private:
    static constexpr int kGraphMargin = 6;
    static constexpr int kLaneWidth = 14;
    static constexpr int kMaxLanes = 12;   // más carriles se recortan para no tapar el texto

    static QColor laneColor(int index)
    {
        const Theme::Colors &c = Theme::colors();
        const QColor palette[] = {c.function, c.preproc, c.keyword, c.string, c.type, c.number, c.accent, c.removed};
        return palette[index % int(std::size(palette))];
    }

    // Dibuja los carriles y el nodo de la fila; devuelve la x donde empieza el texto
    static int paintGraph(QPainter *p, const QRect &r, const CommitGraph::Row &g, bool working, bool merge)
    {
        const Theme::Colors &c = Theme::colors();
        const int lanes = std::min(g.laneCount, kMaxLanes);
        const QRect area(r.left(), r.top(), kGraphMargin + lanes * kLaneWidth, r.height());
        auto laneX = [&](int lane) { return qreal(r.left() + kGraphMargin + lane * kLaneWidth + kLaneWidth / 2); };
        auto anchorY = [&](CommitGraph::Anchor a) {
            return a == CommitGraph::Top ? qreal(r.top())
                                         : a == CommitGraph::Center ? r.top() + r.height() / 2.0 : qreal(r.bottom() + 1);
        };

        p->save();
        p->setClipRect(area);
        p->setBrush(Qt::NoBrush);
        for (const CommitGraph::Edge &e : g.edges) {
            QPen pen(laneColor(e.color), 2);
            if (e.dashed)
                pen.setStyle(Qt::DashLine);
            p->setPen(pen);
            const QPointF a(laneX(e.fromLane), anchorY(e.from));
            const QPointF b(laneX(e.toLane), anchorY(e.to));
            if (e.fromLane == e.toLane) {
                p->drawLine(a, b);
            } else {
                // Curva suave entre carriles, vertical en los extremos
                QPainterPath path(a);
                const qreal midY = (a.y() + b.y()) / 2;
                path.cubicTo(a.x(), midY, b.x(), midY, b.x(), b.y());
                p->drawPath(path);
            }
        }

        const QPointF node(laneX(g.lane), r.top() + r.height() / 2.0);
        const QColor color = laneColor(g.color);
        if (working) {
            p->setPen(QPen(color, 2));
            p->setBrush(c.background);
            p->drawRoundedRect(QRectF(node.x() - 5, node.y() - 5, 10, 10), 2, 2);
        } else if (merge) {
            p->setPen(QPen(color, 2));
            p->setBrush(c.background);
            p->drawEllipse(node, 4.5, 4.5);
        } else {
            p->setPen(Qt::NoPen);
            p->setBrush(color);
            p->drawEllipse(node, 5, 5);
        }
        p->restore();
        return area.right() + 8;
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

    auto *openInTerminal = new QToolButton(this);
    openInTerminal->setText(tr("Abrir en terminal"));
    openInTerminal->setToolTip(tr("Abre una terminal en la carpeta del repositorio"));
    connect(openInTerminal, &QToolButton::clicked, this, [this] { Q_EMIT openTerminalRequested(m_repo.workdir()); });

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
    tl->addWidget(openInTerminal);
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
    m_commits->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_commits, &QListWidget::customContextMenuRequested, this, &RepositoryView::showCommitMenu);
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
    // Con las flechas el detalle sigue al archivo actual (los clics ya los atiende itemClicked)
    connect(m_files, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (!item || !m_files->hasFocus() || QApplication::mouseButtons() != Qt::NoButton)
            return;
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
    // Recuerda qué grupos y carpetas de ramas estaban expandidos
    QSet<QString> expanded;
    QSet<QString> knownFolders;
    for (QTreeWidgetItemIterator it(m_locations); *it; ++it) {
        const QString id = (*it)->data(0, IdRole).toString();
        if ((*it)->data(0, RefKindRole).toInt() == RefFolder)
            knownFolders.insert(id);
        if ((*it)->isExpanded())
            expanded.insert(id);
    }
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

    // "feature/login" se muestra como "login" dentro de la carpeta "feature". Las carpetas nuevas
    // empiezan plegadas salvo la que contiene la rama actual.
    auto sortedRefs = [](QList<GitRepository::RefInfo> refs) {
        std::sort(refs.begin(), refs.end(), [](const auto &a, const auto &b) {
            return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
        });
        return refs;
    };
    auto placeInFolders = [&](QTreeWidgetItem *root, const QString &rootId, const QString &relativeName,
                              bool containsHead) {
        QStringList parts = relativeName.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        parts.removeLast();
        QTreeWidgetItem *parent = root;
        QString prefix;
        for (const QString &part : parts) {
            prefix += part + QLatin1Char('/');
            const QString id = rootId + QLatin1Char(':') + prefix;
            QTreeWidgetItem *folder = nullptr;
            for (int i = 0; i < parent->childCount() && !folder; ++i)
                if (parent->child(i)->data(0, IdRole).toString() == id)
                    folder = parent->child(i);
            if (!folder) {
                folder = child(parent, part, QString(), RefFolder);
                folder->setData(0, IdRole, id);
                folder->setExpanded(knownFolders.contains(id) ? expanded.contains(id) : false);
            }
            if (containsHead && !knownFolders.contains(id))
                folder->setExpanded(true);
            parent = folder;
        }
        return parent;
    };
    auto leafName = [](const QString &name) { return name.section(QLatin1Char('/'), -1); };

    QTreeWidgetItem *branches = group(QStringLiteral("branches"), tr("Ramas"), loc.branches.size());
    for (const auto &b : sortedRefs(loc.branches)) {
        QTreeWidgetItem *parent = placeInFolders(branches, QStringLiteral("branches"), b.name, b.isHead);
        QTreeWidgetItem *it = child(parent, leafName(b.name), b.target, RefBranch);
        it->setData(0, IdRole, b.name);
        if (b.isHead) {
            QFont f = it->font(0);
            f.setBold(true);
            it->setFont(0, f);
            it->setText(0, QStringLiteral("● ") + leafName(b.name));
            it->setForeground(0, Theme::colors().accent);
        }
        it->setToolTip(0, b.name + QLatin1Char('\n') + tr("Doble clic para cambiar a esta rama"));
    }

    int remoteCount = 0;
    for (auto it = loc.remotes.cbegin(); it != loc.remotes.cend(); ++it)
        remoteCount += 1;
    QTreeWidgetItem *remotes = group(QStringLiteral("remotes"), tr("Remotos"), remoteCount);
    for (auto it = loc.remotes.cbegin(); it != loc.remotes.cend(); ++it) {
        QTreeWidgetItem *r = child(remotes, it.key(), QString(), RefOther);
        r->setExpanded(true);
        for (const auto &b : sortedRefs(it.value())) {
            const QString relative = b.name.mid(it.key().size() + 1);
            QTreeWidgetItem *parent = placeInFolders(r, QStringLiteral("remote:") + it.key(), relative, false);
            QTreeWidgetItem *rb = child(parent, leafName(relative), b.target, RefRemoteBranch);
            rb->setData(0, IdRole, b.name);
            rb->setData(0, RemoteRole, it.key());
            rb->setToolTip(0, b.name + QLatin1Char('\n') + tr("Doble clic para trabajar en esta rama localmente"));
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
        child(subs, s, QString(), RefSubmodule)->setToolTip(0, tr("Doble clic para abrir el submódulo en una pestaña"));

    QTreeWidgetItem *wts = group(QStringLiteral("worktrees"), tr("Worktrees"), loc.worktrees.size());
    for (const auto &w : loc.worktrees) {
        QString label = QFileInfo(w.path).fileName();
        if (w.isMain)
            label = tr("%1 (principal)").arg(label.isEmpty() ? QDir(w.path).dirName() : label);
        if (w.locked)
            label += QStringLiteral(" \u{1F512}");
        if (!w.valid)
            label += QStringLiteral(" \u{26A0}");
        QTreeWidgetItem *it = child(wts, label, w.path, RefWorktree);
        it->setData(0, IdRole, w.name);
        QString tip = w.path;
        if (!w.branch.isEmpty())
            tip += QLatin1Char('\n') + w.branch;
        if (w.locked)
            tip += QLatin1Char('\n')
                + (w.lockReason.isEmpty() ? tr("Worktree bloqueado") : tr("Bloqueado: %1").arg(w.lockReason));
        if (!w.valid)
            tip += QLatin1Char('\n') + tr("El directorio de trabajo no es válido; «Reparar» o «Poda» puede ayudar.");
        if (w.prunable)
            tip += QLatin1Char('\n') + tr("Se puede podar (enlace huérfano en .git).");
        it->setToolTip(0, tip + QLatin1Char('\n') + tr("Doble clic para abrir en una pestaña Git"));
        if (!w.branch.isEmpty())
            it->setText(0, label + QLatin1Char('\n') + w.branch);
        if (w.isCurrent) {
            QFont f = it->font(0);
            f.setBold(true);
            it->setFont(0, f);
            it->setForeground(0, Theme::colors().accent);
        }
    }
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
    const QList<GitRepository::CommitInfo> history = m_repo.log(kMaxCommits, true);

    // Grafo: los cambios actuales son una fila más cuyo padre es HEAD (línea discontinua)
    QList<CommitGraph::Input> graphInput;
    graphInput.reserve(history.size() + 1);
    graphInput.append({QStringLiteral("<working>"), head.isEmpty() ? QStringList() : QStringList{head}, true});
    for (const auto &ci : history)
        graphInput.append({ci.id, ci.parents, false});
    const QList<CommitGraph::Row> graph = CommitGraph::layout(graphInput);
    working->setData(GraphRole, QVariant::fromValue(graph.first()));

    for (int i = 0; i < history.size(); ++i) {
        const GitRepository::CommitInfo &ci = history.at(i);
        auto *item = new QListWidgetItem(m_commits);
        item->setData(GraphRole, QVariant::fromValue(graph.at(i + 1)));
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

        const auto hunkActionsFor = [](const QString &section) {
            if (section == QLatin1String("unstaged"))
                return FileDiffWidget::HunkActions::Unstaged;
            if (section == QLatin1String("staged"))
                return FileDiffWidget::HunkActions::Staged;
            return FileDiffWidget::HunkActions::None;
        };
        for (const auto &f : files) {
            auto *w = new FileDiffWidget(f, content, hunkActionsFor(id));
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
            if (id == QLatin1String("unstaged")) {
                connect(w, &FileDiffWidget::hunkStageRequested, this,
                        [this, f](int index) { stageHunk(f, index); });
                connect(w, &FileDiffWidget::hunkDiscardRequested, this,
                        [this, f](int index) { discardUnstagedHunk(f, index); });
            } else if (id == QLatin1String("staged")) {
                connect(w, &FileDiffWidget::hunkUnstageRequested, this,
                        [this, f](int index) { unstageHunk(f, index); });
                connect(w, &FileDiffWidget::hunkDiscardRequested, this,
                        [this, f](int index) { discardStagedHunk(f, index); });
            }
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
    if (!item)
        return;
    const int kind = item->data(0, RefKindRole).toInt();
    if (kind == RefBranch)
        checkout(item->data(0, IdRole).toString());
    else if (kind == RefRemoteBranch)
        checkoutRemoteBranch(item->data(0, IdRole).toString(), item->data(0, RemoteRole).toString());
    else if (kind == RefSubmodule)
        openSubmodule(item->data(0, IdRole).toString());
    else if (kind == RefWorktree)
        openWorktreePath(item->data(0, TargetRole).toString());
}

// "origin/feature/x": si ya hay una rama local "feature/x" se cambia a ella; si no, se crea
// enlazada a la remota (git switch --track) y se cambia a ella.
void RepositoryView::checkoutRemoteBranch(const QString &remoteBranch, const QString &remote)
{
    const QString local = remoteBranch.mid(remote.size() + 1);
    if (local.isEmpty())
        return;
    for (const auto &b : m_repo.locations().branches)
        if (b.name == local) {
            checkout(local);
            return;
        }
    const auto mode = askSubmoduleSwitch(
        local, tr("¿Crear la rama local «%1» desde «%2» y cambiar a ella?").arg(local, remoteBranch), true);
    if (!mode)
        return;
    m_workingSelected = true;
    switchBranchWithGit({QStringLiteral("switch"), QStringLiteral("--track"), remoteBranch},
                        QStringLiteral("git switch --track ") + remoteBranch, local, *mode);
}

namespace {

#ifndef Q_OS_WIN
QString terminalExecutable()
{
    const QString preferred = qEnvironmentVariable("TERMINAL");
    if (!preferred.isEmpty() && !QStandardPaths::findExecutable(preferred).isEmpty())
        return preferred;
    for (const char *t : {"x-terminal-emulator", "gnome-terminal", "konsole", "xfce4-terminal", "kitty",
                          "alacritty", "wezterm", "tilix", "xterm"}) {
        if (!QStandardPaths::findExecutable(QLatin1String(t)).isEmpty())
            return QLatin1String(t);
    }
    return QString();
}
#endif

} // namespace

GitRepository::Locations::WorktreeInfo RepositoryView::worktreeFromItem(QTreeWidgetItem *item) const
{
    const QString path = QDir::cleanPath(item->data(0, TargetRole).toString());
    for (const auto &w : m_repo.locations().worktrees)
        if (QDir::cleanPath(w.path) == path)
            return w;
    return {};
}

void RepositoryView::openWorktreePath(const QString &path)
{
    const QString abs = QDir::cleanPath(path);
    if (abs == QDir::cleanPath(m_repo.workdir()))
        Q_EMIT statusMessage(tr("Ya estás en este worktree"));
    else
        Q_EMIT openRepositoryRequested(abs);
}

void RepositoryView::openWorktreeTerminal(const QString &path)
{
    const QString dir = QDir::cleanPath(path);
#ifndef Q_OS_WIN
    const QString terminal = terminalExecutable();
    if (!terminal.isEmpty()) {
        if (QProcess::startDetached(terminal, {}, dir))
            return;
    }
    QMessageBox::warning(this, tr("Abrir terminal"), tr("No se encontró ninguna terminal para abrir en «%1».").arg(dir));
#else
    QProcess::startDetached(QStringLiteral("cmd.exe"), {QStringLiteral("/k"), QStringLiteral("cd /d"), dir}, dir);
#endif
}

void RepositoryView::showWorktreeGroupMenu(const QPoint &pos)
{
    QMenu menu(this);
    QAction *add = menu.addAction(tr("Añadir worktree…"), this, &RepositoryView::addWorktree);
    add->setEnabled(!isBusy());
    QAction *prune = menu.addAction(tr("Podar worktrees huérfanos…"), this, &RepositoryView::pruneWorktrees);
    prune->setEnabled(!isBusy());
    QAction *repair = menu.addAction(tr("Reparar todos los worktrees"), this, [this] {
        runGit({QStringLiteral("worktree"), QStringLiteral("repair")}, QStringLiteral("git worktree repair"));
    });
    repair->setEnabled(!isBusy());
    menu.exec(m_locations->viewport()->mapToGlobal(pos));
}

void RepositoryView::addWorktree()
{
    const GitRepository::Locations loc = m_repo.locations();
    QStringList branches;
    for (const auto &b : loc.branches)
        branches << b.name;
    if (branches.isEmpty()) {
        QMessageBox::information(this, tr("Añadir worktree"), tr("No hay ramas locales para enlazar."));
        return;
    }
    bool ok = false;
    const QString branch = QInputDialog::getItem(this, tr("Añadir worktree"), tr("Rama a comprobar:"), branches, 0,
                                                 false, &ok);
    if (!ok || branch.isEmpty())
        return;
    const QString suggested =
        QDir(m_repo.workdir()).filePath(QStringLiteral("../") + QFileInfo(m_repo.workdir()).fileName() + QLatin1Char('-')
                                     + branch.section(QLatin1Char('/'), -1));
    const QString path = QInputDialog::getText(this, tr("Añadir worktree"),
                                               tr("Carpeta del nuevo worktree:\n(rama «%1»)").arg(branch),
                                               QLineEdit::Normal, QDir::cleanPath(suggested), &ok)
                             .trimmed();
    if (!ok || path.isEmpty())
        return;
    runGit({QStringLiteral("worktree"), QStringLiteral("add"), QDir::cleanPath(path), branch},
           QStringLiteral("git worktree add ") + path + QLatin1Char(' ') + branch);
}

void RepositoryView::removeWorktree(const GitRepository::Locations::WorktreeInfo &wt, bool force)
{
    if (wt.isMain) {
        QMessageBox::information(this, tr("Worktree"), tr("No se puede eliminar el worktree principal."));
        return;
    }
    QString question =
        force ? tr("¿Eliminar el worktree en «%1» a la fuerza (puede perder cambios sin guardar)?").arg(wt.path)
              : tr("¿Eliminar el worktree en «%1»?").arg(wt.path);
    if (wt.locked)
        question += QLatin1Char('\n') + tr("Está bloqueado; quizá necesites «Eliminar a la fuerza».");
    const auto answer =
        QMessageBox::warning(this, tr("Eliminar worktree"), question,
                             force ? QMessageBox::Yes | QMessageBox::Cancel : QMessageBox::Yes | QMessageBox::Cancel,
                             QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;
    QStringList args{QStringLiteral("worktree"), QStringLiteral("remove")};
    if (force)
        args << QStringLiteral("--force");
    args << wt.path;
    runGit(args, QStringLiteral("git worktree remove") + (force ? QStringLiteral(" --force ") : QStringLiteral(" "))
             + wt.path);
}

void RepositoryView::lockWorktree(const GitRepository::Locations::WorktreeInfo &wt)
{
    bool ok = false;
    const QString reason = QInputDialog::getText(this, tr("Bloquear worktree"),
                                                 tr("Motivo del bloqueo (opcional) para «%1»:").arg(wt.path),
                                                 QLineEdit::Normal, QString(), &ok)
                               .trimmed();
    if (!ok)
        return;
    QStringList args{QStringLiteral("worktree"), QStringLiteral("lock")};
    if (!reason.isEmpty())
        args << QStringLiteral("--reason") << reason;
    args << wt.path;
    runGit(args, QStringLiteral("git worktree lock ") + wt.path);
}

void RepositoryView::unlockWorktree(const GitRepository::Locations::WorktreeInfo &wt)
{
    runGit({QStringLiteral("worktree"), QStringLiteral("unlock"), wt.path},
           QStringLiteral("git worktree unlock ") + wt.path);
}

void RepositoryView::pruneWorktrees()
{
    const auto answer = QMessageBox::question(
        this, tr("Podar worktrees"),
        tr("¿Eliminar de .git los enlaces de worktrees cuyo directorio ya no existe en disco?"));
    if (answer != QMessageBox::Yes)
        return;
    runGit({QStringLiteral("worktree"), QStringLiteral("prune")}, QStringLiteral("git worktree prune"));
}

void RepositoryView::repairWorktree(const QString &path)
{
    runGit({QStringLiteral("worktree"), QStringLiteral("repair"), QDir::cleanPath(path)},
           QStringLiteral("git worktree repair ") + path);
}

void RepositoryView::openSubmodule(const QString &path)
{
    // Sin .git propio, la carpeta pertenece al repositorio padre y abrirla volvería a este
    const QString dir = absolutePath(path);
    if (!QFileInfo::exists(QDir(dir).filePath(QStringLiteral(".git")))) {
        const auto answer = QMessageBox::question(
            this, tr("Submódulo sin inicializar"),
            tr("El submódulo «%1» todavía no está descargado.\n\n¿Inicializarlo ahora (git submodule update --init)?")
                .arg(path));
        if (answer == QMessageBox::Yes)
            runGit({QStringLiteral("submodule"), QStringLiteral("update"), QStringLiteral("--init"),
                    QStringLiteral("--progress"), QStringLiteral("--"), path},
                   QStringLiteral("git submodule update --init ") + path);
        return;
    }
    Q_EMIT openRepositoryRequested(dir);
}

void RepositoryView::showLocationMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_locations->itemAt(pos);
    if (!item)
        return;
    if (!item->parent()) {
        if (item->data(0, IdRole).toString() == QLatin1String("worktrees"))
            showWorktreeGroupMenu(pos);
        return;
    }
    const int kind = item->data(0, RefKindRole).toInt();
    const QString name = item->data(0, IdRole).toString();
    const QString target = item->data(0, TargetRole).toString();
    QMenu menu(this);
    const QString current = m_repo.branchName();
    if (kind == RefBranch)
        menu.addAction(tr("Cambiar a «%1»").arg(name), this, [this, name] { checkout(name); });
    if (kind == RefRemoteBranch) {
        const QString remote = item->data(0, RemoteRole).toString();
        QAction *a = menu.addAction(tr("Cambiar a «%1» (rama local)").arg(name.mid(remote.size() + 1)), this,
                                    [this, name, remote] { checkoutRemoteBranch(name, remote); });
        a->setEnabled(!isBusy());
    }
    if ((kind == RefBranch || kind == RefRemoteBranch) && name != current && !m_repo.isHeadDetached())
        menu.addAction(tr("Fusionar «%1» en «%2»…").arg(name, current), this, [this, name] { showMergeOptions(name); });
    if (kind == RefOther && item->parent()->data(0, IdRole).toString() == QLatin1String("remotes")) {
        menu.addAction(tr("Fetch de «%1»").arg(name), this,
                       [this, name] { runGit({QStringLiteral("fetch"), QStringLiteral("--progress"), name}); });
        menu.addAction(tr("Fetch de «%1» con --prune").arg(name), this, [this, name] {
            runGit({QStringLiteral("fetch"), QStringLiteral("--progress"), QStringLiteral("--prune"), name});
        });
    }
    if (kind == RefSubmodule) {
        menu.addAction(tr("Abrir submódulo en una pestaña"), this, [this, name] { openSubmodule(name); });
        menu.addAction(tr("Abrir en el editor"), this, [this, name] { Q_EMIT openFolderRequested(absolutePath(name)); });
    }
    if (kind == RefWorktree) {
        const GitRepository::Locations::WorktreeInfo wt = worktreeFromItem(item);
        const QString path = wt.path.isEmpty() ? target : wt.path;
        const bool here = QDir::cleanPath(path) == QDir::cleanPath(m_repo.workdir());
        const bool busy = isBusy();
        if (!here)
            menu.addAction(tr("Abrir en una pestaña Git"), this, [this, path] { openWorktreePath(path); });
        menu.addAction(tr("Abrir en el editor"), this, [this, path] { Q_EMIT openFolderRequested(path); });
        menu.addAction(tr("Abrir en el explorador"), this, [path] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        });
        menu.addAction(tr("Abrir terminal aquí"), this, [this, path] { openWorktreeTerminal(path); });
        menu.addSeparator();
        if (!wt.isMain) {
            QAction *remove = menu.addAction(tr("Eliminar worktree…"), this, [this, wt] { removeWorktree(wt, false); });
            remove->setEnabled(!busy);
            QAction *force = menu.addAction(tr("Eliminar worktree a la fuerza…"),
                                          this, [this, wt] { removeWorktree(wt, true); });
            force->setEnabled(!busy);
            if (wt.locked)
                menu.addAction(tr("Desbloquear worktree"), this, [this, wt] { unlockWorktree(wt); })->setEnabled(!busy);
            else
                menu.addAction(tr("Bloquear worktree…"), this, [this, wt] { lockWorktree(wt); })->setEnabled(!busy);
            if (!wt.valid || wt.prunable) {
                menu.addAction(tr("Reparar worktree"), this, [this, path] { repairWorktree(path); })->setEnabled(!busy);
                if (wt.prunable)
                    menu.addAction(tr("Podar enlace administrativo"), this, [this, wt] {
                        runGit({QStringLiteral("worktree"), QStringLiteral("prune")},
                               QStringLiteral("git worktree prune"));
                    })->setEnabled(!busy);
            }
        }
        menu.addAction(tr("Copiar ruta"), this, [path] { QApplication::clipboard()->setText(path); });
    }
    if (kind == RefBranch) {
        menu.addSeparator();
        QAction *rename = menu.addAction(tr("Renombrar «%1»…").arg(name), this, [this, name] { renameBranch(name); });
        QAction *remove = menu.addAction(tr("Eliminar «%1»…").arg(name), this,
                                         [this, name, target] { deleteBranch(name, target); });
        rename->setEnabled(!isBusy());
        remove->setEnabled(!isBusy() && !(name == current && !m_repo.isHeadDetached()));
        if (!remove->isEnabled() && !isBusy()) {
            menu.setToolTipsVisible(true);
            remove->setToolTip(tr("No se puede eliminar la rama actual: cambia antes a otra."));
        }
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

void RepositoryView::renameBranch(const QString &branch)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Renombrar rama"),
                                               tr("Nuevo nombre para «%1»:").arg(branch),
                                               QLineEdit::Normal, branch, &ok).trimmed();
    if (ok && !name.isEmpty() && name != branch)
        runGit({QStringLiteral("branch"), QStringLiteral("-m"), branch, name},
               QStringLiteral("git branch -m %1 %2").arg(branch, name));
}

void RepositoryView::deleteBranch(const QString &branch, const QString &target)
{
    // "git branch -d" se niega si la rama tiene commits que no están en HEAD; entonces se avisa y se usa -D
    const bool merged = m_repo.isAncestor(target, m_repo.headCommitId());
    QMessageBox::StandardButton answer;
    if (merged)
        answer = QMessageBox::question(this, tr("Eliminar rama"), tr("¿Eliminar la rama local «%1»?").arg(branch));
    else
        answer = QMessageBox::warning(
            this, tr("Eliminar rama"),
            tr("La rama «%1» tiene commits que no están en la rama actual. Si la eliminas, esos commits "
               "dejarán de verse en el historial.\n\n¿Eliminarla de todos modos?").arg(branch),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes)
        return;
    const QString flag = merged ? QStringLiteral("-d") : QStringLiteral("-D");
    runGit({QStringLiteral("branch"), flag, branch}, QStringLiteral("git branch %1 %2").arg(flag, branch));
}

void RepositoryView::checkout(const QString &branch)
{
    // Ya en esa rama: solo se alinean los submódulos
    if (branch == m_repo.branchName()) {
        const auto mode = askSubmoduleSwitch(
            branch, tr("Ya estás en la rama «%1». ¿Qué hago con los submódulos?").arg(branch), false);
        if (mode)
            applySubmoduleSwitch(*mode, branch);
        return;
    }
    const auto mode = askSubmoduleSwitch(branch, tr("¿Cambiar a la rama «%1»?").arg(branch), true);
    if (!mode)
        return;
    const bool ok = m_repo.checkoutBranch(branch);
    report(ok, tr("cambiar de rama (revisa que no haya cambios que entren en conflicto)"));
    if (ok)
        Q_EMIT statusMessage(tr("Ahora en la rama %1").arg(branch));
    m_workingSelected = true;
    reload();
    if (ok)
        applySubmoduleSwitch(*mode, branch);
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

void RepositoryView::stageHunk(const GitRepository::FileChange &file, int index)
{
    const QStringList hunks = GitRepository::splitPatchHunks(file.patch);
    if (index < 0 || index >= hunks.size())
        return;
    report(m_repo.applyHunk(file, hunks.at(index), true, false), tr("preparar bloque"));
    m_workingSelected = true;
    reload();
}

void RepositoryView::discardUnstagedHunk(const GitRepository::FileChange &file, int index)
{
    const QStringList hunks = GitRepository::splitPatchHunks(file.patch);
    if (index < 0 || index >= hunks.size())
        return;
    report(m_repo.applyHunk(file, hunks.at(index), false, true), tr("descartar bloque"));
    m_workingSelected = true;
    reload();
}

void RepositoryView::unstageHunk(const GitRepository::FileChange &file, int index)
{
    const QStringList hunks = GitRepository::splitPatchHunks(file.patch);
    if (index < 0 || index >= hunks.size())
        return;
    report(m_repo.applyHunk(file, hunks.at(index), true, true), tr("quitar bloque de preparados"));
    m_workingSelected = true;
    reload();
}

void RepositoryView::discardStagedHunk(const GitRepository::FileChange &file, int index)
{
    const QStringList hunks = GitRepository::splitPatchHunks(file.patch);
    if (index < 0 || index >= hunks.size())
        return;
    const QString hunk = hunks.at(index);
    bool ok = m_repo.applyHunk(file, hunk, true, true);
    if (ok)
        ok = m_repo.applyHunk(file, hunk, false, true);
    report(ok, tr("descartar bloque"));
    m_workingSelected = true;
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

void RepositoryView::runGit(const QStringList &args, const QString &title, const QHash<QString, QString> &extraEnv)
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
        m_pendingSubmoduleSwitch = SubmoduleSwitch::None;
        m_submoduleQueue.clear();
        m_submoduleQueueActive = false;
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
    for (auto it = extraEnv.cbegin(); it != extraEnv.cend(); ++it)
        env.insert(it.key(), it.value());
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

    // Cambio de rama terminado: ahora los submódulos, según lo elegido
    const SubmoduleSwitch mode = std::exchange(m_pendingSubmoduleSwitch, SubmoduleSwitch::None);
    const QString switchedTo = std::exchange(m_pendingSubmoduleBranch, QString());
    if (ok && mode != SubmoduleSwitch::None) {
        applySubmoduleSwitch(mode, switchedTo);
        return;
    }
    if (m_submoduleQueueActive) {
        if (!ok)
            m_submoduleFailures << QStringLiteral("%1: %2").arg(m_runningTitle, errorLine(m_commandOutput));
        QTimer::singleShot(0, this, &RepositoryView::runNextSubmoduleSwitch);
    }
}

QStringList RepositoryView::initializedSubmodules() const
{
    QStringList dirs;
    for (const QString &path : m_repo.locations().submodules) {
        const QString dir = absolutePath(path);
        if (QFileInfo::exists(QDir(dir).filePath(QStringLiteral(".git"))))
            dirs << dir;
    }
    return dirs;
}

std::optional<RepositoryView::SubmoduleSwitch>
RepositoryView::askSubmoduleSwitch(const QString &branch, const QString &question, bool confirmWithoutSubmodules)
{
    const QStringList submodules = initializedSubmodules();
    if (submodules.isEmpty()) {
        if (confirmWithoutSubmodules
            && QMessageBox::question(this, tr("Cambiar de rama"), question) != QMessageBox::Yes)
            return std::nullopt;
        return SubmoduleSwitch::None;
    }

    QStringList following;
    for (const QStringList &args : submoduleSwitchCommands(branch))
        following << QDir(m_repo.workdir()).relativeFilePath(args.at(1));

    QMessageBox box(QMessageBox::Question, tr("Cambiar de rama"), question, QMessageBox::Cancel, this);
    box.setInformativeText(
        tr("Este repositorio tiene %1 submódulo(s).\n\n"
           "• Cambiar también los submódulos: los que tienen una rama «%2» cambian a ella (%3); "
           "el resto se queda como está.\n\n"
           "• Solo el proyecto: los submódulos se ponen en el commit que registra «%2» "
           "(git submodule update).")
            .arg(submodules.size())
            .arg(branch, following.isEmpty() ? tr("ahora mismo ninguno") : following.join(QStringLiteral(", "))));
    QPushButton *recursive = box.addButton(tr("Cambiar también los submódulos"), QMessageBox::AcceptRole);
    QPushButton *projectOnly = box.addButton(tr("Solo el proyecto"), QMessageBox::AcceptRole);
    box.setDefaultButton(following.isEmpty() ? projectOnly : recursive);
    box.exec();
    if (box.clickedButton() == recursive)
        return SubmoduleSwitch::FollowBranch;
    if (box.clickedButton() == projectOnly)
        return SubmoduleSwitch::RecordedCommit;
    return std::nullopt;
}

void RepositoryView::applySubmoduleSwitch(SubmoduleSwitch mode, const QString &branch)
{
    if (mode == SubmoduleSwitch::FollowBranch)
        switchSubmodulesTo(branch);
    else if (mode == SubmoduleSwitch::RecordedCommit && !isBusy())
        runGit({QStringLiteral("submodule"), QStringLiteral("update"), QStringLiteral("--recursive")},
               QStringLiteral("git submodule update --recursive"));
}

// Submódulos con una rama llamada `branch` (local, o en su remoto) que aún no están en ella
QList<QStringList> RepositoryView::submoduleSwitchCommands(const QString &branch) const
{
    QList<QStringList> commands;
    for (const QString &dir : initializedSubmodules()) {
        GitRepository sub;
        if (!sub.open(dir) || QDir::cleanPath(sub.workdir()) != QDir::cleanPath(dir))
            continue;
        if (!sub.isHeadDetached() && sub.branchName() == branch)
            continue;
        if (!sub.resolveCommit(QStringLiteral("refs/heads/") + branch).isEmpty()) {
            commands << QStringList{QStringLiteral("-C"), dir, QStringLiteral("switch"), branch};
            continue;
        }
        for (const QString &remote : sub.remoteNames())
            if (!sub.resolveCommit(QStringLiteral("refs/remotes/%1/%2").arg(remote, branch)).isEmpty()) {
                commands << QStringList{QStringLiteral("-C"), dir, QStringLiteral("switch"), QStringLiteral("--track"),
                                        remote + QLatin1Char('/') + branch};
                break;
            }
    }
    return commands;
}

void RepositoryView::switchSubmodulesTo(const QString &branch)
{
    if (isBusy() || m_submoduleQueueActive)
        return;
    m_submoduleQueue = submoduleSwitchCommands(branch);
    if (m_submoduleQueue.isEmpty())
        return;
    m_submoduleQueueActive = true;
    m_submoduleQueueBranch = branch;
    m_submoduleQueueTotal = int(m_submoduleQueue.size());
    m_submoduleFailures.clear();
    runNextSubmoduleSwitch();
}

void RepositoryView::switchBranchWithGit(const QStringList &args, const QString &title, const QString &branch,
                                         SubmoduleSwitch mode)
{
    if (!isBusy()) {
        m_pendingSubmoduleSwitch = mode;
        m_pendingSubmoduleBranch = branch;
    }
    runGit(args, title);
}

void RepositoryView::runNextSubmoduleSwitch()
{
    if (m_submoduleQueue.isEmpty()) {
        m_submoduleQueueActive = false;
        const int done = m_submoduleQueueTotal - int(m_submoduleFailures.size());
        Q_EMIT statusMessage(tr("Submódulos en «%1»: %2 de %3").arg(m_submoduleQueueBranch).arg(done).arg(m_submoduleQueueTotal));
        if (!m_submoduleFailures.isEmpty())
            QMessageBox::warning(this, tr("Submódulos"),
                                 tr("Algunos submódulos no pudieron cambiar a «%1» y se quedaron como estaban:\n\n%2")
                                     .arg(m_submoduleQueueBranch, m_submoduleFailures.join(QLatin1Char('\n'))));
        return;
    }
    const QStringList args = m_submoduleQueue.takeFirst();
    const QString path = QDir(m_repo.workdir()).relativeFilePath(args.at(1));
    runGit(args, tr("Submódulo %1: git %2").arg(path, args.mid(2).join(QLatin1Char(' '))));
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
                          const auto mode = askSubmoduleSwitch(name, tr("¿Cambiar a la rama «%1»?").arg(name), false);
                          if (mode)
                              switchBranchWithGit({QStringLiteral("switch"), name}, QStringLiteral("git switch ") + name,
                                                  name, *mode);
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
    if (!ok || name.isEmpty())
        return;
    const auto mode = askSubmoduleSwitch(name, tr("¿Crear la rama «%1» y cambiar a ella?").arg(name), false);
    if (mode)
        switchBranchWithGit({QStringLiteral("switch"), QStringLiteral("-c"), name}, QStringLiteral("git switch -c ") + name,
                            name, *mode);
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
    QStringList args{QStringLiteral("diff"), QStringLiteral("--binary")};
    if (staged)
        args << QStringLiteral("--cached");
    savePatchFromGit(args, QFileInfo(QDir::cleanPath(m_repo.workdir())).fileName()
                               + (staged ? QStringLiteral("-staged.patch") : QStringLiteral(".patch")));
}

void RepositoryView::savePatchFromGit(const QStringList &args, const QString &suggestedName)
{
    const QString suggested = QDir(m_repo.workdir()).absoluteFilePath(suggestedName);
    const QString path = QFileDialog::getSaveFileName(this, tr("Guardar parche"), suggested,
                                                      tr("Parches (*.patch *.diff);;Todos los archivos (*)"));
    if (path.isEmpty())
        return;
    QProcess git;
    git.setWorkingDirectory(m_repo.workdir());
    const QString program = gitExecutable();
    git.start(program.isEmpty() ? QStringLiteral("git") : program, args);
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

// ---------------------------------------------------------------------------
// Menú contextual de la lista de commits
// ---------------------------------------------------------------------------
void RepositoryView::showWorkingMenu(const QPoint &globalPos)
{
    const GitRepository::Status st = m_repo.status();
    const bool busy = isBusy();
    QMenu menu(this);
    QAction *stage = menu.addAction(tr("Preparar todo (stage all)"));
    stage->setEnabled(!busy && !st.unstaged.isEmpty());
    connect(stage, &QAction::triggered, this, &RepositoryView::stageAllChanges);
    QAction *unstage = menu.addAction(tr("Quitar todo de preparados"));
    unstage->setEnabled(!busy && !st.staged.isEmpty());
    connect(unstage, &QAction::triggered, this, [this] {
        report(m_repo.unstageAll(), tr("quitar de preparados"));
        reload();
    });
    menu.addSeparator();
    QAction *discard = menu.addAction(tr("Descartar todos los cambios (discard all)…"));
    discard->setEnabled(!busy && !(st.staged.isEmpty() && st.unstaged.isEmpty()));
    connect(discard, &QAction::triggered, this, &RepositoryView::discardAllChanges);
    menu.exec(globalPos);
}

void RepositoryView::stageAllChanges()
{
    const bool ok = m_repo.stageAll();
    report(ok, tr("preparar"));
    if (ok)
        Q_EMIT statusMessage(tr("Todos los cambios preparados"));
    m_workingSelected = true;
    reload();
}

void RepositoryView::discardAllChanges()
{
    const GitRepository::Status st = m_repo.status();
    QStringList untracked, added;
    int tracked = 0;
    for (const auto &e : st.unstaged) {
        if (e.code == QLatin1Char('?'))
            untracked << e.path;
        else
            ++tracked;
    }
    for (const auto &e : st.staged)
        if (e.code == QLatin1Char('A'))
            added << e.path;   // no están en HEAD: el reset los borraría, mejor a la papelera
    tracked += int(st.staged.size() - added.size());

    QMessageBox box(QMessageBox::Warning, tr("Descartar todos los cambios"),
                    tr("¿Descartar todos los cambios, preparados y sin preparar?"), QMessageBox::Cancel, this);
    QStringList details;
    if (tracked > 0)
        details << tr("%1 cambio(s) en archivos con seguimiento vuelven a como están en HEAD.").arg(tracked);
    if (!added.isEmpty())
        details << tr("%1 archivo(s) nuevo(s) preparado(s) se mueven a la papelera.").arg(added.size());
    details << tr("Esta acción no se puede deshacer.");
    box.setInformativeText(details.join(QStringLiteral("\n\n")));
    QCheckBox *withUntracked = nullptr;
    if (!untracked.isEmpty()) {
        withUntracked = new QCheckBox(
            tr("Mover también a la papelera los %1 archivo(s) sin seguimiento").arg(untracked.size()), &box);
        box.setCheckBox(withUntracked);
    }
    QPushButton *discard = box.addButton(tr("Descartar todo"), QMessageBox::DestructiveRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != discard)
        return;

    QStringList toTrash = added;
    if (withUntracked && withUntracked->isChecked())
        toTrash << untracked;
    QStringList failed;
    for (const QString &path : std::as_const(toTrash))
        if (QFileInfo::exists(absolutePath(path)) && !QFile::moveToTrash(absolutePath(path)))
            failed << path;
    bool ok = true;
    if (m_repo.headCommitId().isEmpty())
        ok = m_repo.unstageAll();   // sin commits todavía: no hay HEAD al que volver
    else if (tracked > 0 || !added.isEmpty())
        ok = m_repo.discardAll();
    report(ok, tr("descartar"));
    if (!failed.isEmpty())
        QMessageBox::warning(this, tr("Descartar todos los cambios"),
                             tr("No se pudieron mover a la papelera:\n\n%1").arg(failed.join(QLatin1Char('\n'))));
    else if (ok)
        Q_EMIT statusMessage(tr("Cambios descartados"));
    m_workingSelected = true;
    reload();
}

void RepositoryView::showCommitMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_commits->itemAt(pos);
    if (!item)
        return;
    if (item->data(KindRole).toInt() == 0) {
        m_commits->setCurrentItem(item);
        showWorkingMenu(m_commits->viewport()->mapToGlobal(pos));
        return;
    }
    if (item->data(KindRole).toInt() != 1)
        return;
    m_commits->setCurrentItem(item);
    const QString sha = item->data(IdRole).toString();
    const QString shortSha = sha.left(7);
    const GitRepository::CommitDetails d = m_repo.commitDetails(sha);
    const bool isMerge = d.parents.size() > 1;
    const bool detached = m_repo.isHeadDetached();
    const QString head = m_repo.headCommitId();
    const QString branch = m_repo.branchName();
    const bool inHead = m_repo.isAncestor(sha, head);
    const bool busy = isBusy();

    QMenu menu(this);
    auto add = [](QMenu *m, const QString &text, bool enabled, auto &&slot) {
        QAction *a = m->addAction(text);
        a->setEnabled(enabled);
        QObject::connect(a, &QAction::triggered, m, slot);
        return a;
    };
    auto withMainline = [isMerge](QStringList args, const QString &sha) {
        if (isMerge)
            args << QStringLiteral("-m") << QStringLiteral("1");
        return args << sha;
    };

    add(&menu, tr("Checkout del commit (HEAD desacoplado)…"), !busy && !(detached && sha == head),
        [this, sha] { checkoutCommit(sha); });
    add(&menu, tr("Revert %1").arg(shortSha), !busy, [this, sha, withMainline] {
        const QStringList args = withMainline({QStringLiteral("revert"), QStringLiteral("--no-edit")}, sha);
        runGit(args, QStringLiteral("git ") + args.join(QLatin1Char(' ')));
    });
    add(&menu, tr("Cherry-pick %1").arg(shortSha), !busy && !inHead, [this, sha, withMainline] {
        const QStringList args = withMainline({QStringLiteral("cherry-pick")}, sha);
        runGit(args, QStringLiteral("git ") + args.join(QLatin1Char(' ')));
    });
    add(&menu, tr("Copiar SHA"), true, [sha] { QApplication::clipboard()->setText(sha); });
    add(&menu, tr("Copiar mensaje"), true, [d] {
        QApplication::clipboard()->setText(d.body.isEmpty() ? d.summary : d.summary + QStringLiteral("\n\n") + d.body);
    });
    menu.addSeparator();

    QMenu *create = menu.addMenu(tr("Crear"));
    add(create, tr("Rama en este commit…"), !busy, [this, sha] { createBranchAt(sha); });
    add(create, tr("Etiqueta en este commit…"), !busy, [this, sha] { createTagAt(sha); });
    add(create, tr("Guardar parche…"), true, [this, sha] {
        savePatchFromGit({QStringLiteral("format-patch"), QStringLiteral("-1"), QStringLiteral("--binary"),
                          QStringLiteral("--stdout"), sha},
                         sha.left(10) + QStringLiteral(".patch"));
    });

    // Reescribir historial: solo commits de la rama actual sin merges hasta HEAD
    const QStringList chain = detached ? QStringList() : m_repo.firstParentChain(sha);
    const bool linear = !busy && !chain.isEmpty();
    const bool isRoot = d.parents.isEmpty();
    QMenu *edit = menu.addMenu(tr("Editar"));
    add(edit, tr("Editar mensaje del commit…"), linear, [this, sha] { editCommitMessage(sha); });
    add(edit, tr("Squash con el padre"), linear && !isRoot,
        [this, sha] { rewriteHistory(QStringLiteral("squash"), sha); });
    add(edit, tr("Mover arriba (más reciente)"), linear && sha != head,
        [this, sha] { rewriteHistory(QStringLiteral("up"), sha); });
    add(edit, tr("Mover abajo (más antiguo)"), linear && !isRoot,
        [this, sha] { rewriteHistory(QStringLiteral("down"), sha); });
    add(edit, tr("Eliminar commit…"), linear, [this, sha] { rewriteHistory(QStringLiteral("drop"), sha); });
    if (chain.isEmpty()) {
        edit->setToolTipsVisible(true);
        for (QAction *a : edit->actions())
            a->setToolTip(detached ? tr("No hay ninguna rama activa (HEAD desacoplado).")
                                   : tr("Solo commits de la rama actual, sin merges entre el commit y HEAD."));
    }

    QMenu *reset = menu.addMenu(detached ? tr("Reset de HEAD a este commit")
                                         : tr("Reset de «%1» a este commit").arg(branch));
    add(reset, tr("Soft (conserva los cambios preparados)"), !busy,
        [this, sha] { resetTo(sha, QStringLiteral("--soft")); });
    add(reset, tr("Mixed (conserva los cambios sin preparar)"), !busy,
        [this, sha] { resetTo(sha, QStringLiteral("--mixed")); });
    add(reset, tr("Hard (descarta los cambios)…"), !busy, [this, sha] { resetTo(sha, QStringLiteral("--hard")); });

    menu.exec(m_commits->viewport()->mapToGlobal(pos));
}

void RepositoryView::checkoutCommit(const QString &sha)
{
    const auto answer = QMessageBox::question(
        this, tr("Checkout del commit"),
        tr("¿Cambiar al commit %1?\n\nHEAD quedará desacoplado: los commits nuevos no pertenecerán a ninguna rama "
           "hasta que crees una.").arg(sha.left(7)));
    if (answer == QMessageBox::Yes)
        runGit({QStringLiteral("checkout"), QStringLiteral("--detach"), sha},
               QStringLiteral("git checkout --detach ") + sha.left(7));
}

void RepositoryView::createBranchAt(const QString &sha)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Crear rama"),
                                               tr("Nombre de la nueva rama (se crea en el commit %1 y se cambia a ella):")
                                                   .arg(sha.left(7)),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    const auto mode = askSubmoduleSwitch(name, tr("¿Crear la rama «%1» y cambiar a ella?").arg(name), false);
    if (mode)
        switchBranchWithGit({QStringLiteral("switch"), QStringLiteral("-c"), name, sha},
                            QStringLiteral("git switch -c %1 %2").arg(name, sha.left(7)), name, *mode);
}

void RepositoryView::createTagAt(const QString &sha)
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Crear etiqueta"));
    auto *name = new QLineEdit(&dialog);
    auto *message = new QPlainTextEdit(&dialog);
    message->setPlaceholderText(tr("Opcional: con mensaje se crea una etiqueta anotada"));
    message->setTabChangesFocus(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel(tr("Nombre de la etiqueta (en el commit %1):").arg(sha.left(7)), &dialog));
    layout->addWidget(name);
    layout->addWidget(new QLabel(tr("Mensaje"), &dialog));
    layout->addWidget(message, 1);
    layout->addWidget(buttons);
    dialog.resize(460, 260);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString tag = name->text().trimmed();
    const QString text = message->toPlainText().trimmed();
    if (tag.isEmpty())
        return;
    QStringList args{QStringLiteral("tag")};
    if (!text.isEmpty())
        args << QStringLiteral("-a") << QStringLiteral("-m") << text;
    args << tag << sha;
    runGit(args, QStringLiteral("git tag %1 %2").arg(tag, sha.left(7)));
}

void RepositoryView::resetTo(const QString &sha, const QString &mode)
{
    if (mode == QLatin1String("--hard")) {
        const auto answer = QMessageBox::warning(
            this, tr("Reset hard"),
            tr("«git reset --hard %1» descarta todos los cambios sin confirmar y los commits posteriores "
               "de la rama actual.\n\n¿Continuar?").arg(sha.left(7)),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes)
            return;
    }
    runGit({QStringLiteral("reset"), mode, sha}, QStringLiteral("git reset %1 %2").arg(mode, sha.left(7)));
}

void RepositoryView::editCommitMessage(const QString &sha)
{
    const GitRepository::CommitDetails d = m_repo.commitDetails(sha);
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Editar mensaje del commit %1").arg(sha.left(7)));
    auto *text = new QPlainTextEdit(&dialog);
    text->setFont(Theme::editorFont());
    const QString original = d.body.isEmpty() ? d.summary : d.summary + QStringLiteral("\n\n") + d.body;
    text->setPlainText(original);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(text, 1);
    layout->addWidget(buttons);
    dialog.resize(620, 300);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const QString message = text->toPlainText().trimmed();
    if (message.isEmpty() || message == original.trimmed())
        return;
    rewriteHistory(QStringLiteral("reword"), sha, message);
}

// Reescribe el historial con "git rebase -i" sin intervención: BatCode hace de editor
// de la lista de tareas (y del mensaje, en reword) a través de RebaseHelper.
void RepositoryView::rewriteHistory(const QString &op, const QString &sha, const QString &message)
{
    if (isBusy()) {
        QMessageBox::information(this, tr("Git"), tr("Espera a que termine «%1».").arg(m_runningTitle));
        return;
    }
    const QString upstream = m_repo.upstreamCommitId();
    if (!upstream.isEmpty() && m_repo.isAncestor(sha, upstream)) {
        const auto answer = QMessageBox::warning(
            this, tr("Reescribir historial"),
            tr("El commit %1 ya está en «%2».\n\nDespués tendrás que hacer un push forzado y quien ya lo haya "
               "descargado tendrá que reajustar su rama. ¿Continuar?").arg(sha.left(7), m_repo.upstreamName()),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes)
            return;
    } else if (op == QLatin1String("drop")) {
        const auto answer = QMessageBox::question(this, tr("Eliminar commit"),
                                                  tr("¿Eliminar el commit %1 de la rama «%2»?")
                                                      .arg(sha.left(7), m_repo.branchName()));
        if (answer != QMessageBox::Yes)
            return;
    }

    // Base del rebase: el padre (o el abuelo si la operación involucra al padre)
    const bool needsParent = op == QLatin1String("squash") || op == QLatin1String("down");
    QString base = m_repo.commitDetails(sha).parents.value(0);
    if (needsParent && !base.isEmpty())
        base = m_repo.commitDetails(base).parents.value(0);

    QHash<QString, QString> env;
    env.insert(QStringLiteral("GIT_SEQUENCE_EDITOR"),
               RebaseHelper::editorCommand({QStringLiteral("--rebase-todo"), op, sha}));
    QString messageFile;
    if (op == QLatin1String("reword")) {
        messageFile = QDir(QDir::tempPath()).absoluteFilePath(
            QStringLiteral("batcode-reword-%1-%2.txt").arg(QCoreApplication::applicationPid()).arg(m_runCounter + 1));
        QFile f(messageFile);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::warning(this, tr("Git"), tr("No se pudo crear un archivo temporal.\n\n%1").arg(f.errorString()));
            return;
        }
        f.write(message.toUtf8() + '\n');
        f.close();
        env.insert(QStringLiteral("GIT_EDITOR"),
                   RebaseHelper::editorCommand({QStringLiteral("--rebase-message"), messageFile}));
        connect(this, &RepositoryView::commandFinished, this, [messageFile] { QFile::remove(messageFile); },
                Qt::SingleShotConnection);
    }

    QStringList args{QStringLiteral("rebase"), QStringLiteral("-i"), QStringLiteral("--autostash")};
    if (base.isEmpty())
        args << QStringLiteral("--root");
    else
        args << base;

    QString title;
    if (op == QLatin1String("reword"))
        title = tr("Editar mensaje de %1");
    else if (op == QLatin1String("squash"))
        title = tr("Squash de %1 con su padre");
    else if (op == QLatin1String("up"))
        title = tr("Mover %1 arriba");
    else if (op == QLatin1String("down"))
        title = tr("Mover %1 abajo");
    else
        title = tr("Eliminar %1");
    runGit(args, title.arg(sha.left(7)), env);
}
