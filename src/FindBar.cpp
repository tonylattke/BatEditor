#include "FindBar.h"

#include "CodeEditor.h"
#include "Theme.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QToolButton *makeToggle(const QString &text, const QString &tip, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setText(text);
    b->setToolTip(tip);
    b->setCheckable(true);
    b->setMinimumWidth(40);
    return b;
}

} // namespace

FindBar::FindBar(CodeEditor *editor, QWidget *parent)
    : QWidget(parent)
    , m_editor(editor)
{
    setAutoFillBackground(true);
    QPalette p = palette();
    p.setColor(QPalette::Window, Theme::colors().backgroundAlt);
    setPalette(p);

    m_regex = makeToggle(QStringLiteral(".*"), tr("Expresión regular (Alt+R)"), this);
    m_case = makeToggle(QStringLiteral("Aa"), tr("Distinguir mayúsculas (Alt+C)"), this);
    m_word = makeToggle(QStringLiteral("\"\""), tr("Palabra completa (Alt+W)"), this);

    m_find = new QLineEdit(this);
    m_find->setPlaceholderText(tr("Buscar"));
    m_find->setClearButtonEnabled(true);

    m_counter = new QLabel(this);
    m_counter->setMinimumWidth(90);

    auto *prev = new QPushButton(tr("Anterior"), this);
    auto *next = new QPushButton(tr("Siguiente"), this);
    auto *all = new QPushButton(tr("Seleccionar todo"), this);
    all->setToolTip(tr("Un cursor en cada coincidencia (Alt+Enter)"));
    auto *close = new QToolButton(this);
    close->setText(QStringLiteral("✕"));
    close->setToolTip(tr("Cerrar (Esc)"));

    m_replace = new QLineEdit(this);
    m_replace->setPlaceholderText(tr("Reemplazar (\\1 para grupos)"));
    auto *replaceOne = new QPushButton(tr("Reemplazar"), this);
    auto *replaceAll = new QPushButton(tr("Reemplazar todo"), this);

    auto *findRow = new QHBoxLayout;
    findRow->setContentsMargins(0, 0, 0, 0);
    findRow->addWidget(m_regex);
    findRow->addWidget(m_case);
    findRow->addWidget(m_word);
    findRow->addWidget(m_find, 1);
    findRow->addWidget(m_counter);
    findRow->addWidget(prev);
    findRow->addWidget(next);
    findRow->addWidget(all);
    findRow->addWidget(close);

    m_replaceRow = new QWidget(this);
    auto *replaceRow = new QHBoxLayout(m_replaceRow);
    replaceRow->setContentsMargins(3 * 40 + 12, 0, 0, 0);
    replaceRow->addWidget(m_replace, 1);
    replaceRow->addWidget(replaceOne);
    replaceRow->addWidget(replaceAll);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(4);
    layout->addLayout(findRow);
    layout->addWidget(m_replaceRow);

    connect(m_find, &QLineEdit::textChanged, this, &FindBar::updateSearch);
    for (QToolButton *t : {m_regex, m_case, m_word})
        connect(t, &QToolButton::toggled, this, &FindBar::updateSearch);
    connect(m_find, &QLineEdit::returnPressed, this, [this] {
        // QLineEdit consume Enter con cualquier modificador: se decide aquí
        const auto mods = QGuiApplication::keyboardModifiers();
        if (mods & Qt::AltModifier) {
            if (m_editor->selectAllMatches())
                closeBar();
            return;
        }
        m_editor->findNext(mods & Qt::ShiftModifier);
        updateCounter();
    });
    connect(next, &QPushButton::clicked, this, [this] {
        m_editor->findNext(false);
        updateCounter();
    });
    connect(prev, &QPushButton::clicked, this, [this] {
        m_editor->findNext(true);
        updateCounter();
    });
    connect(all, &QPushButton::clicked, this, [this] {
        if (m_editor->selectAllMatches())
            closeBar();
    });
    connect(m_replace, &QLineEdit::returnPressed, this, [this] {
        m_editor->replaceCurrent(m_replace->text());
        updateCounter();
    });
    connect(replaceOne, &QPushButton::clicked, this, [this] {
        m_editor->replaceCurrent(m_replace->text());
        updateCounter();
    });
    connect(replaceAll, &QPushButton::clicked, this, [this] {
        const int n = m_editor->replaceAll(m_replace->text());
        updateCounter();
        m_counter->setText((n == 1 ? tr("1 reemplazo") : tr("%1 reemplazos").arg(n)));
    });
    connect(close, &QToolButton::clicked, this, &FindBar::closeBar);

    hide();
}

void FindBar::showFind(bool withReplace)
{
    const QTextCursor c = m_editor->textCursor();
    if (c.hasSelection() && !c.selectedText().contains(QChar::ParagraphSeparator))
        m_find->setText(c.selectedText());
    m_replaceRow->setVisible(withReplace);
    show();
    m_find->setFocus();
    m_find->selectAll();
    updateSearch();
}

void FindBar::closeBar()
{
    m_editor->clearSearch();
    hide();
    m_editor->setFocus();
}

void FindBar::updateSearch()
{
    CodeEditor::SearchOptions opts;
    opts.regex = m_regex->isChecked();
    opts.caseSensitive = m_case->isChecked();
    opts.wholeWord = m_word->isChecked();
    m_editor->setSearch(m_find->text(), opts);

    QPalette p = m_find->palette();
    const bool invalid = !m_find->text().isEmpty() && !m_editor->searchIsValid();
    p.setColor(QPalette::Text, invalid ? Theme::colors().removed : Theme::colors().foreground);
    m_find->setPalette(p);
    updateCounter();
}

void FindBar::updateCounter()
{
    if (m_find->text().isEmpty()) {
        m_counter->clear();
        return;
    }
    if (!m_editor->searchIsValid()) {
        m_counter->setText(tr("Regex inválida"));
        return;
    }
    const int total = m_editor->searchMatchCount();
    const int current = m_editor->currentSearchIndex();
    if (total == 0)
        m_counter->setText(tr("Sin resultados"));
    else if (current >= 0)
        m_counter->setText(tr("%1 de %2").arg(current + 1).arg(total));
    else
        m_counter->setText((total == 1 ? tr("1 coincidencia") : tr("%1 coincidencias").arg(total)));
}

void FindBar::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        closeBar();
        return;
    }
    if (e->modifiers() & Qt::AltModifier) {
        switch (e->key()) {
        case Qt::Key_R: m_regex->toggle(); return;
        case Qt::Key_C: m_case->toggle(); return;
        case Qt::Key_W: m_word->toggle(); return;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            if (m_editor->selectAllMatches())
                closeBar();
            return;
        default: break;
        }
    }
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && (e->modifiers() & Qt::ShiftModifier)) {
        m_editor->findNext(true);
        updateCounter();
        return;
    }
    QWidget::keyPressEvent(e);
}
