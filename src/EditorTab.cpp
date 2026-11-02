#include "EditorTab.h"

#include "CodeEditor.h"
#include "DiffHighlighter.h"
#include "FindBar.h"
#include "LanguageDefinitions.h"
#include "SyntaxHighlighter.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringDecoder>
#include <QVBoxLayout>

EditorTab::EditorTab(Kind kind, QWidget *parent)
    : QWidget(parent)
    , m_kind(kind)
    , m_editor(new CodeEditor(this))
    , m_findBar(new FindBar(m_editor, this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_editor, 1);
    layout->addWidget(m_findBar);

    if (m_kind == Kind::Diff) {
        m_editor->setReadOnly(true);
        m_editor->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        m_highlighter = new DiffHighlighter(m_editor->document());
    } else {
        m_highlighter = new SyntaxHighlighter(m_editor->document());
        connect(m_editor->document(), &QTextDocument::modificationChanged, this, &EditorTab::titleChanged);
    }
}

bool EditorTab::isModified() const
{
    return m_kind == Kind::File && m_editor->document()->isModified();
}

QString EditorTab::displayName() const
{
    if (m_kind == Kind::Diff)
        return m_diffTitle;
    if (m_path.isEmpty())
        return m_untitledIndex > 1 ? tr("sin título %1").arg(m_untitledIndex) : tr("sin título");
    return QFileInfo(m_path).fileName();
}

QString EditorTab::languageName() const
{
    if (m_kind == Kind::Diff)
        return QStringLiteral("Diff");
    if (auto *sh = qobject_cast<SyntaxHighlighter *>(m_highlighter))
        return sh->language().name;
    return QString();
}

void EditorTab::applyLanguage()
{
    if (auto *sh = qobject_cast<SyntaxHighlighter *>(m_highlighter)) {
        const LanguageDefinition &lang = Languages::forFile(m_path);
        sh->setLanguage(lang);
        m_editor->setLineCommentToken(lang.lineComment.isEmpty() ? QStringLiteral("#") : lang.lineComment);
    }
}

bool EditorTab::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray bytes = file.readAll();

    // UTF-8 por defecto; si no es UTF-8 válido, se interpreta como Latin-1
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString text = decoder.decode(bytes);
    if (decoder.hasError())
        text = QString::fromLatin1(bytes);
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);

    m_crlf = text.contains(QLatin1String("\r\n"));
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));

    m_path = QFileInfo(path).absoluteFilePath();
    applyLanguage();
    m_editor->setPlainText(text);
    m_editor->document()->setModified(false);
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
    file.write(text.toUtf8());
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    m_editor->document()->setModified(false);
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
