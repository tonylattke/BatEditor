#include "EditorTab.h"

#include "CodeEditor.h"
#include "DiffHighlighter.h"
#include "FindBar.h"
#include "LanguageDefinitions.h"
#include "SyntaxHighlighter.h"
#include "Theme.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QScrollBar>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextFrame>
#include <QTextTable>
#include <QTimer>
#include <QToolButton>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringDecoder>
#include <QUrl>
#include <QVBoxLayout>

EditorTab::EditorTab(Kind kind, QWidget *parent)
    : QWidget(parent)
    , m_kind(kind)
    , m_editor(new CodeEditor(this))
    , m_findBar(new FindBar(m_editor, this))
{
    m_pages = new QStackedWidget(this);
    m_pages->addWidget(m_editor);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_pages, 1);
    layout->addWidget(m_findBar);

    if (m_kind == Kind::Diff) {
        m_editor->setReadOnly(true);
        m_editor->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        m_highlighter = new DiffHighlighter(m_editor->document());
    } else if (m_kind == Kind::FindResults) {
        m_editor->setReadOnly(true);
        m_editor->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        m_highlighter = new FindInFiles::ResultsHighlighter(m_editor->document());
        m_editor->viewport()->installEventFilter(this);
        m_editor->installEventFilter(this);
    } else {
        m_highlighter = new SyntaxHighlighter(m_editor->document());
        connect(m_editor->document(), &QTextDocument::modificationChanged, this, &EditorTab::titleChanged);

        m_previewButton = new QToolButton(this);
        m_previewButton->setObjectName(QStringLiteral("previewToggle"));
        m_previewButton->setCursor(Qt::PointingHandCursor);
        m_previewButton->hide();
        connect(m_previewButton, &QToolButton::clicked, this, [this] { setPreviewing(!m_previewing); });
        m_previewTimer = new QTimer(this);
        m_previewTimer->setSingleShot(true);
        m_previewTimer->setInterval(300);
        connect(m_previewTimer, &QTimer::timeout, this, &EditorTab::refreshPreview);
        connect(m_editor->document(), &QTextDocument::contentsChanged, this, [this] {
            if (m_previewing)
                m_previewTimer->start();
        });
        m_editor->viewport()->installEventFilter(this);
    }
}

// ---------------------------------------------------------------------------
// Vista previa: JSON formateado / Markdown renderizado
// ---------------------------------------------------------------------------
namespace {

// Formatea JSON conservando el orden de las claves y los comentarios (JSONC);
// QJsonDocument ordenaría las claves y perdería los comentarios.
QString prettyJson(const QString &text, int indentWidth)
{
    const QString unit(indentWidth, QLatin1Char(' '));
    QString out;
    out.reserve(text.size() * 2);
    int depth = 0;
    bool lineStart = true;
    auto newline = [&] {
        out += QLatin1Char('\n');
        out += unit.repeated(depth);
        lineStart = true;
    };
    auto put = [&](QStringView token) {
        out += token;
        lineStart = false;
    };
    const int len = int(text.size());
    for (int i = 0; i < len; ++i) {
        const QChar ch = text.at(i);
        if (ch.isSpace())
            continue;
        if (ch == QLatin1Char('"')) {
            int j = i + 1;
            while (j < len && text.at(j) != QLatin1Char('"'))
                j += text.at(j) == QLatin1Char('\\') ? 2 : 1;
            j = qMin(j, len - 1);
            put(QStringView(text).mid(i, j - i + 1));
            i = j;
        } else if (ch == QLatin1Char('/') && i + 1 < len && text.at(i + 1) == QLatin1Char('/')) {
            int j = text.indexOf(QLatin1Char('\n'), i);
            if (j < 0)
                j = len;
            if (!lineStart)
                out += QLatin1Char(' ');
            put(QStringView(text).mid(i, j - i).trimmed());
            newline();
            i = j - 1;
        } else if (ch == QLatin1Char('/') && i + 1 < len && text.at(i + 1) == QLatin1Char('*')) {
            int j = text.indexOf(QLatin1String("*/"), i + 2);
            j = j < 0 ? len : j + 2;
            put(QStringView(text).mid(i, j - i));
            i = j - 1;
        } else if (ch == QLatin1Char('{') || ch == QLatin1Char('[')) {
            // Vacíos en la misma línea: {} y []
            int k = i + 1;
            while (k < len && text.at(k).isSpace())
                ++k;
            const QChar close = ch == QLatin1Char('{') ? QLatin1Char('}') : QLatin1Char(']');
            if (k < len && text.at(k) == close) {
                put(QString(ch) + close);
                i = k;
                continue;
            }
            put(QStringView(&ch, 1));
            ++depth;
            newline();
        } else if (ch == QLatin1Char('}') || ch == QLatin1Char(']')) {
            depth = qMax(0, depth - 1);
            while (out.endsWith(QLatin1Char(' ')))
                out.chop(1);
            if (!lineStart || !out.endsWith(QLatin1Char('\n')))
                newline();
            else
                out += unit.repeated(depth);
            put(QStringView(&ch, 1));
        } else if (ch == QLatin1Char(',')) {
            put(u",");
            newline();
        } else if (ch == QLatin1Char(':')) {
            put(u": ");
        } else {
            int j = i;
            while (j < len && !text.at(j).isSpace() && !QStringView(u"{}[],:\"/").contains(text.at(j)))
                ++j;
            put(QStringView(text).mid(i, qMax(1, j - i)));
            i = qMax(i, j - 1);
        }
    }
    // Las líneas que quedaron solo con sangría (tras un comentario final, por ejemplo)
    QStringList lines = out.split(QLatin1Char('\n'));
    for (QString &line : lines)
        if (line.trimmed().isEmpty())
            line.clear();
    lines.removeAll(QString());
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

// "línea 3, columna 5" para el desplazamiento `offset` de `text`
QString lineAndColumn(const QString &text, int offset)
{
    const QStringView before = QStringView(text).left(offset);
    const int line = int(before.count(QLatin1Char('\n'))) + 1;
    const int column = offset - int(before.lastIndexOf(QLatin1Char('\n')));
    return QCoreApplication::translate("EditorTab", "línea %1, columna %2").arg(line).arg(column);
}

} // namespace

EditorTab::PreviewKind EditorTab::previewKind() const
{
    if (m_kind != Kind::File)
        return PreviewKind::None;
    const QString name = languageName();
    if (name == QLatin1String("JSON"))
        return PreviewKind::Json;
    if (name == QLatin1String("Markdown"))
        return PreviewKind::Markdown;
    return PreviewKind::None;
}

void EditorTab::updatePreviewButton()
{
    if (!m_previewButton)
        return;
    const PreviewKind kind = previewKind();
    if (kind == PreviewKind::None && m_previewing)
        setPreviewing(false);
    m_previewButton->setVisible(kind != PreviewKind::None);
    if (kind == PreviewKind::Json) {
        m_previewButton->setText(m_previewing ? tr("Original") : tr("{ } Pretty"));
        m_previewButton->setToolTip(m_previewing ? tr("Volver al archivo tal como está")
                                                 : tr("Ver el JSON formateado (no modifica el archivo)"));
    } else if (kind == PreviewKind::Markdown) {
        m_previewButton->setText(m_previewing ? tr("Código") : tr("Vista previa"));
        m_previewButton->setToolTip(m_previewing ? tr("Volver al texto Markdown")
                                                 : tr("Ver el Markdown renderizado"));
    }
    m_previewButton->adjustSize();
    positionPreviewButton();
}

void EditorTab::positionPreviewButton()
{
    if (!m_previewButton || m_previewButton->isHidden())
        return;
    QWidget *viewport = m_editor->viewport();
    if (m_previewing && previewKind() == PreviewKind::Json && m_jsonView)
        viewport = m_jsonView->viewport();
    else if (m_previewing && previewKind() == PreviewKind::Markdown && m_markdownView)
        viewport = m_markdownView->viewport();
    const QPoint topRight = viewport->mapTo(this, viewport->rect().topRight());
    m_previewButton->move(topRight.x() - m_previewButton->width() - 10, topRight.y() + 8);
    m_previewButton->raise();
}

void EditorTab::setPreviewing(bool previewing)
{
    const PreviewKind kind = previewKind();
    if (kind == PreviewKind::None)
        previewing = false;
    m_previewing = previewing;
    if (previewing && kind == PreviewKind::Json && !m_jsonView) {
        m_jsonView = new CodeEditor(m_pages);
        m_jsonView->setReadOnly(true);
        m_jsonView->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        auto *highlighter = new SyntaxHighlighter(m_jsonView->document());
        highlighter->setLanguage(Languages::byName(QStringLiteral("JSON")));
        m_jsonView->viewport()->installEventFilter(this);
        m_pages->addWidget(m_jsonView);
    } else if (previewing && kind == PreviewKind::Markdown && !m_markdownView) {
        m_markdownView = new QTextBrowser(m_pages);
        m_markdownView->setOpenLinks(false);
        m_markdownView->setFrameShape(QFrame::NoFrame);
        connect(m_markdownView, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
            if (url.scheme().isEmpty() && url.path().isEmpty() && url.hasFragment())
                m_markdownView->scrollToAnchor(url.fragment());
            else
                QDesktopServices::openUrl(url.isRelative()
                                              ? QUrl::fromLocalFile(QFileInfo(m_path).dir().absoluteFilePath(url.path()))
                                              : url);
        });
        m_markdownView->viewport()->installEventFilter(this);
        m_pages->addWidget(m_markdownView);
    }
    if (previewing) {
        refreshPreview();
        QWidget *view = kind == PreviewKind::Json ? static_cast<QWidget *>(m_jsonView) : m_markdownView;
        m_pages->setCurrentWidget(view);
        view->setFocus();
    } else {
        m_previewTimer->stop();
        m_pages->setCurrentWidget(m_editor);
        m_editor->setFocus();
    }
    updatePreviewButton();
}

void EditorTab::refreshPreview()
{
    if (!m_previewing)
        return;
    const QString text = m_editor->toPlainText();
    if (previewKind() == PreviewKind::Json && m_jsonView) {
        m_jsonView->setFont(m_editor->font());
        QString shown = prettyJson(text, m_editor->indentWidth());
        QJsonParseError error;
        QJsonDocument::fromJson(text.toUtf8(), &error);
        const bool hasComments = text.contains(QLatin1String("//")) || text.contains(QLatin1String("/*"));
        if (error.error != QJsonParseError::NoError && !hasComments && !text.trimmed().isEmpty()) {
            const int offset = int(QString::fromUtf8(text.toUtf8().left(error.offset)).size());
            shown.prepend(tr("// JSON no válido (%1): %2\n").arg(lineAndColumn(text, offset), error.errorString()));
        }
        const int scroll = m_jsonView->verticalScrollBar()->value();
        m_jsonView->setPlainText(shown);
        m_jsonView->verticalScrollBar()->setValue(scroll);
    } else if (previewKind() == PreviewKind::Markdown && m_markdownView) {
        const Theme::Colors &c = Theme::colors();
        const int scroll = m_markdownView->verticalScrollBar()->value();
        m_markdownView->setSearchPaths({QFileInfo(m_path).absolutePath()});
        QTextDocument *doc = m_markdownView->document();
        doc->setDocumentMargin(24);
        QFont font = Theme::uiFont();
        font.setPointSizeF(font.pointSizeF() + 1);
        doc->setDefaultFont(font);
        doc->setMarkdown(text);
        // Tablas con un solo borde fino y algo de aire en las celdas
        const QList<QTextFrame *> frames = doc->rootFrame()->findChildren<QTextFrame *>();
        for (QTextFrame *frame : frames) {
            if (auto *table = qobject_cast<QTextTable *>(frame)) {
                QTextTableFormat f = table->format();
                f.setBorderCollapse(true);
                f.setBorder(1);
                f.setBorderBrush(c.border);
                f.setCellSpacing(0);
                f.setCellPadding(6);
                table->setFormat(f);
            }
        }
        // Bloques e incisos de código con fondo, como en GitHub
        QTextCursor cursor(doc);
        for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
            if (block.blockFormat().hasProperty(QTextFormat::BlockCodeFence)
                || block.charFormat().fontFixedPitch()) {
                QTextBlockFormat f;
                f.setBackground(c.backgroundAlt);
                cursor.setPosition(block.position());
                cursor.mergeBlockFormat(f);
            }
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const QTextFragment fragment = it.fragment();
                if (!fragment.charFormat().fontFixedPitch())
                    continue;
                QTextCharFormat f;
                f.setBackground(c.panel);
                f.setForeground(c.string);
                f.setFont(Theme::editorFont(m_editor->font().pointSize()));
                cursor.setPosition(fragment.position());
                cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
                cursor.mergeCharFormat(f);
            }
        }
        m_markdownView->verticalScrollBar()->setValue(scroll);
    }
}

void EditorTab::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    positionPreviewButton();
}

bool EditorTab::isModified() const
{
    return m_kind == Kind::File && m_editor->document()->isModified();
}

QString EditorTab::displayName() const
{
    if (m_kind == Kind::Diff)
        return m_diffTitle;
    if (m_kind == Kind::FindResults)
        return tr("Resultados de búsqueda");
    if (m_path.isEmpty())
        return m_untitledIndex > 1 ? tr("sin título %1").arg(m_untitledIndex) : tr("sin título");
    return QFileInfo(m_path).fileName();
}

QString EditorTab::languageName() const
{
    if (m_kind == Kind::Diff)
        return QStringLiteral("Diff");
    if (m_kind == Kind::FindResults)
        return tr("Resultados de búsqueda");
    if (auto *sh = qobject_cast<SyntaxHighlighter *>(m_highlighter))
        return sh->language().name;
    return QString();
}

void EditorTab::setFilePath(const QString &path)
{
    if (m_kind != Kind::File || path == m_path)
        return;
    m_path = path;
    applyLanguage();   // la extensión puede haber cambiado
    Q_EMIT titleChanged();
}

void EditorTab::setLanguageOverride(const QString &name)
{
    if (name == m_languageOverride)
        return;
    m_languageOverride = name;
    applyLanguage();
}

void EditorTab::applyLanguage()
{
    applyLanguage(m_editor->document()->firstBlock().text());
}

void EditorTab::applyLanguage(const QString &firstLine)
{
    if (auto *sh = qobject_cast<SyntaxHighlighter *>(m_highlighter)) {
        const LanguageDefinition &lang = m_languageOverride.isEmpty() ? Languages::forFile(m_path, firstLine)
                                                                      : Languages::byName(m_languageOverride);
        if (lang.name != sh->language().name)
            sh->setLanguage(lang);
        m_editor->setLineCommentToken(lang.lineComment.isEmpty() ? QStringLiteral("#") : lang.lineComment);
        m_editor->setAutoPairs(autoPairsFor(lang), lang.name == QLatin1String("HTML / XML"));
        m_editor->setCompletionKeywords(lang.keywords + lang.types + lang.constants);
    }
    updatePreviewButton();
}

bool EditorTab::readText(const QString &path, QString *text, QByteArray *bytes, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    *bytes = file.readAll();

    // UTF-8 por defecto; si no es UTF-8 válido, se interpreta como Latin-1
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    *text = decoder.decode(*bytes);
    if (decoder.hasError())
        *text = QString::fromLatin1(*bytes);
    if (text->startsWith(QChar(0xFEFF)))
        text->remove(0, 1);
    return true;
}

void EditorTab::rememberDisk(const QByteArray &bytes)
{
    const QFileInfo info(m_path);
    m_diskHash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha1);
    m_diskSize = info.size();
    m_diskModified = info.lastModified();
}

bool EditorTab::load(const QString &path, QString *error)
{
    QString text;
    QByteArray bytes;
    if (!readText(path, &text, &bytes, error))
        return false;

    m_crlf = text.contains(QLatin1String("\r\n"));
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));

    m_path = QFileInfo(path).absoluteFilePath();
    applyLanguage(text.left(text.indexOf(QLatin1Char('\n'))));
    m_editor->setPlainText(text);
    m_editor->document()->setModified(false);
    rememberDisk(bytes);
    Q_EMIT titleChanged();
    return true;
}

EditorTab::DiskState EditorTab::diskState() const
{
    if (m_kind != Kind::File || m_path.isEmpty() || m_diskHash.isEmpty())
        return DiskState::Unchanged;
    const QFileInfo info(m_path);
    if (!info.exists())
        return DiskState::Missing;
    if (info.size() == m_diskSize && info.lastModified() == m_diskModified)
        return DiskState::Unchanged;
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly))
        return DiskState::Unchanged;   // sin permiso o bloqueado: se reintenta en el siguiente aviso
    return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha1) == m_diskHash ? DiskState::Unchanged
                                                                                          : DiskState::Changed;
}

bool EditorTab::reloadFromDisk(QString *error)
{
    QString text;
    QByteArray bytes;
    if (!readText(m_path, &text, &bytes, error))
        return false;
    m_crlf = text.contains(QLatin1String("\r\n"));
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    rememberDisk(bytes);
    if (text == m_editor->toPlainText())
        return true;

    const QTextCursor old = m_editor->textCursor();
    const int line = old.blockNumber();
    const int column = old.positionInBlock();
    const int vscroll = m_editor->verticalScrollBar()->value();
    const int hscroll = m_editor->horizontalScrollBar()->value();

    // Un único paso de deshacer: Ctrl+Z vuelve a la versión anterior
    QTextCursor all(m_editor->document());
    all.beginEditBlock();
    all.select(QTextCursor::Document);
    all.insertText(text);
    all.endEditBlock();
    m_editor->document()->setModified(false);

    const QTextBlock block = m_editor->document()->findBlockByNumber(
        qMin(line, m_editor->document()->blockCount() - 1));
    QTextCursor c(block);
    c.setPosition(block.position() + qMin(column, block.length() - 1));
    m_editor->setTextCursor(c);
    m_editor->verticalScrollBar()->setValue(vscroll);
    m_editor->horizontalScrollBar()->setValue(hscroll);
    Q_EMIT titleChanged();
    return true;
}

bool EditorTab::save(QString *error)
{
    if (m_path.isEmpty()) {
        if (error)
            *error = tr("El archivo no tiene ruta");
        return false;
    }
    QString text = m_editor->toPlainText();
    if (m_crlf)
        text.replace(QLatin1Char('\n'), QLatin1String("\r\n"));

    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray bytes = text.toUtf8();
    file.write(bytes);
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    m_editor->document()->setModified(false);
    rememberDisk(bytes);
    Q_EMIT titleChanged();
    return true;
}

bool EditorTab::saveAs(const QString &path, QString *error)
{
    m_path = QFileInfo(path).absoluteFilePath();
    applyLanguage();
    const bool ok = save(error);
    Q_EMIT titleChanged();
    return ok;
}

void EditorTab::setDiff(const QString &title, const QString &diffText)
{
    m_diffTitle = title;
    m_editor->setPlainText(diffText);
    Q_EMIT titleChanged();
}

// ---------------------------------------------------------------------------
// Resultados de "Buscar en archivos"
// ---------------------------------------------------------------------------
void EditorTab::setFindResults(const FindInFiles::Document &document)
{
    m_results = document;
    m_editor->setPlainText(document.text);
    QList<QTextCursor> highlights;
    for (const auto &[block, column, length] : document.highlights) {
        const QTextBlock b = m_editor->document()->findBlockByNumber(block);
        if (!b.isValid())
            continue;
        QTextCursor c(b);
        c.setPosition(b.position() + column);
        c.setPosition(b.position() + column + length, QTextCursor::KeepAnchor);
        highlights << c;
    }
    m_editor->setPersistentHighlights(highlights);
    m_editor->moveCursor(QTextCursor::Start);
    Q_EMIT titleChanged();
}

QList<FindInFiles::Location> EditorTab::matchLocations() const
{
    QList<FindInFiles::Location> list;
    for (int block : m_results.matchBlocks)
        list << m_results.locations.value(block);
    return list;
}

int EditorTab::matchIndexAtCursor() const
{
    return int(m_results.matchBlocks.indexOf(m_editor->textCursor().blockNumber()));
}

void EditorTab::revealMatch(int index)
{
    if (index < 0 || index >= m_results.matchBlocks.size())
        return;
    const QTextBlock b = m_editor->document()->findBlockByNumber(m_results.matchBlocks.at(index));
    QTextCursor c(b);
    m_editor->setTextCursor(c);
    m_editor->centerCursor();
}

bool EditorTab::eventFilter(QObject *watched, QEvent *event)
{
    // El minimapa y la barra de desplazamiento cambian el ancho útil sin redimensionar la pestaña
    if (event->type() == QEvent::Resize && m_previewButton)
        positionPreviewButton();
    if (m_kind == Kind::FindResults) {
        int block = -1;
        if (watched == m_editor->viewport() && event->type() == QEvent::MouseButtonDblClick) {
            const auto *me = static_cast<QMouseEvent *>(event);
            block = m_editor->cursorForPosition(me->position().toPoint()).blockNumber();
        } else if (watched == m_editor && event->type() == QEvent::KeyPress) {
            const auto *ke = static_cast<QKeyEvent *>(event);
            if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
                block = m_editor->textCursor().blockNumber();
        }
        if (block >= 0) {
            const auto it = m_results.locations.constFind(block);
            if (it != m_results.locations.constEnd()) {
                Q_EMIT locationActivated(it.value());
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

// Parejas que se cierran solas según el lenguaje: paréntesis, corchetes y llaves
// siempre; comillas las del lenguaje (en texto y Markdown solo las dobles y `,
// para no molestar con los apóstrofos; en Rust la ' es de lifetimes)
QString EditorTab::autoPairsFor(const LanguageDefinition &lang)
{
    QString pairs = QStringLiteral("()[]{}");
    QString quotes = lang.stringDelimiters;
    if (lang.name == Languages::plainText().name || lang.name == QLatin1String("Markdown"))
        quotes = QStringLiteral("\"`");
    if (lang.name == QLatin1String("Rust"))
        quotes.remove(QLatin1Char('\''));
    for (const QChar q : quotes)
        if (!pairs.contains(q))
            pairs += QString(q) + q;
    return pairs;
}
