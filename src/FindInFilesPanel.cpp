#include "FindInFilesPanel.h"

#include "Theme.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QThread>
#include <QToolButton>

namespace {

constexpr int kHistorySize = 20;

QToolButton *toggle(const QString &text, const QString &tip, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setText(text);
    b->setToolTip(tip);
    b->setCheckable(true);
    b->setFixedSize(32, 26);
    b->setStyleSheet(QStringLiteral("QToolButton { padding: 0px; font-weight: bold; }"));
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

QComboBox *historyCombo(const QString &placeholder, QWidget *parent)
{
    auto *c = new QComboBox(parent);
    c->setEditable(true);
    c->setInsertPolicy(QComboBox::NoInsert);
    c->setMaxCount(kHistorySize);
    c->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    c->lineEdit()->setPlaceholderText(placeholder);
    c->lineEdit()->setClearButtonEnabled(true);
    c->setFont(Theme::editorFont(11));
    return c;
}

} // namespace

FindInFilesPanel::FindInFilesPanel(QWidget *parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette p = palette();
    p.setColor(QPalette::Window, Theme::colors().backgroundAlt);
    setPalette(p);

    m_regex = toggle(QStringLiteral(".*"), tr("Expresión regular"), this);
    m_case = toggle(QStringLiteral("Aa"), tr("Distinguir mayúsculas"), this);
    m_word = toggle(QStringLiteral("“”"), tr("Palabra completa"), this);
    m_context = toggle(QStringLiteral("≡"), tr("Mostrar contexto (líneas alrededor de cada coincidencia)"), this);
    m_newTab = toggle(QStringLiteral("⊞"), tr("Mostrar los resultados en una pestaña nueva"), this);
    m_gitignore = toggle(QStringLiteral("⎇"), tr("Omitir lo ignorado por .gitignore"), this);
    m_preserveCase = toggle(QStringLiteral("AB"), tr("Conservar mayúsculas al reemplazar"), this);

    m_find = historyCombo(QString(), this);
    m_where = historyCombo(tr("Carpetas y archivos abiertos"), this);
    m_replace = historyCombo(QString(), this);
    m_where->setToolTip(tr("Separa con comas: carpetas, archivos, filtros (*.cpp), exclusiones (-*.log, -build), "
                           "<open folders> y <open files>"));

    m_findButton = new QPushButton(tr("Buscar"), this);
    m_replaceButton = new QPushButton(tr("Reemplazar"), this);
    m_whereButton = new QToolButton(this);
    m_whereButton->setText(QStringLiteral("…"));
    m_whereButton->setToolTip(tr("Añadir carpetas, filtros o archivos abiertos"));
    m_whereButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    for (QWidget *w : {static_cast<QWidget *>(m_findButton), static_cast<QWidget *>(m_replaceButton),
                       static_cast<QWidget *>(m_whereButton)})
        w->setMinimumWidth(110);
    auto *close = new QToolButton(this);
    close->setText(QStringLiteral("✕"));
    close->setAutoRaise(true);
    close->setToolTip(tr("Cerrar (Esc)"));

    m_status = new QLabel(this);
    {
        QPalette sp = m_status->palette();
        sp.setColor(QPalette::WindowText, Theme::colors().muted);
        m_status->setPalette(sp);
    }

    auto *toggles0 = new QHBoxLayout;
    toggles0->setSpacing(2);
    for (QToolButton *t : {m_regex, m_case, m_word, m_context, m_newTab})
        toggles0->addWidget(t);
    toggles0->addStretch(1);

    auto label = [this](const QString &text) {
        auto *l = new QLabel(text, this);
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        return l;
    };

    auto *grid = new QGridLayout(this);
    grid->setContentsMargins(10, 8, 6, 6);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);
    grid->addLayout(toggles0, 0, 0);
    grid->addWidget(m_gitignore, 1, 0, Qt::AlignLeft);
    grid->addWidget(m_preserveCase, 2, 0, Qt::AlignLeft);
    grid->addWidget(label(tr("Buscar:")), 0, 1);
    grid->addWidget(label(tr("Donde:")), 1, 1);
    grid->addWidget(label(tr("Reemplazar:")), 2, 1);
    grid->addWidget(m_find, 0, 2);
    grid->addWidget(m_where, 1, 2);
    grid->addWidget(m_replace, 2, 2);
    grid->addWidget(m_findButton, 0, 3);
    grid->addWidget(m_whereButton, 1, 3);
    grid->addWidget(m_replaceButton, 2, 3);
    grid->addWidget(close, 0, 4, Qt::AlignTop);
    grid->addWidget(m_status, 3, 2, 1, 2);
    grid->setColumnStretch(2, 1);

    connect(m_findButton, &QPushButton::clicked, this, [this] { isRunning() ? cancel() : find(); });
    connect(m_replaceButton, &QPushButton::clicked, this, &FindInFilesPanel::replace);
    connect(m_whereButton, &QToolButton::clicked, this, &FindInFilesPanel::showWhereMenu);
    connect(close, &QToolButton::clicked, this, &FindInFilesPanel::closeRequested);
    for (QComboBox *c : {m_find, m_where, m_replace})
        connect(c->lineEdit(), &QLineEdit::returnPressed, this, &FindInFilesPanel::find);
    // Primero se cargan los ajustes y después se conecta el guardado (si no, el primer
    // interruptor que cambia guardaría los demás antes de leerlos)
    loadSettings();
    for (QToolButton *t : {m_regex, m_case, m_word, m_context, m_newTab, m_gitignore, m_preserveCase})
        connect(t, &QToolButton::toggled, this, &FindInFilesPanel::saveToggles);
    hide();
}

FindInFilesPanel::~FindInFilesPanel()
{
    // No dejar un hilo trabajando al cerrar la aplicación
    if (m_thread) {
        m_cancel->store(true);
        m_thread->wait(5000);
    }
}

void FindInFilesPanel::loadSettings()
{
    QSettings s;
    m_regex->setChecked(s.value(QStringLiteral("findInFiles/regex"), false).toBool());
    m_case->setChecked(s.value(QStringLiteral("findInFiles/case"), false).toBool());
    m_word->setChecked(s.value(QStringLiteral("findInFiles/word"), false).toBool());
    m_context->setChecked(s.value(QStringLiteral("findInFiles/context"), true).toBool());
    m_newTab->setChecked(s.value(QStringLiteral("findInFiles/newTab"), false).toBool());
    m_gitignore->setChecked(s.value(QStringLiteral("findInFiles/gitignore"), true).toBool());
    m_preserveCase->setChecked(s.value(QStringLiteral("findInFiles/preserveCase"), false).toBool());
    const std::pair<QComboBox *, const char *> combos[] = {
        {m_find, "findInFiles/findHistory"}, {m_where, "findInFiles/whereHistory"}, {m_replace, "findInFiles/replaceHistory"}};
    for (const auto &[combo, key] : combos) {
        combo->addItems(s.value(QLatin1String(key)).toStringList());
        combo->setCurrentIndex(-1);
        combo->setEditText(QString());
    }
}

void FindInFilesPanel::saveToggles() const
{
    QSettings s;
    s.setValue(QStringLiteral("findInFiles/regex"), m_regex->isChecked());
    s.setValue(QStringLiteral("findInFiles/case"), m_case->isChecked());
    s.setValue(QStringLiteral("findInFiles/word"), m_word->isChecked());
    s.setValue(QStringLiteral("findInFiles/context"), m_context->isChecked());
    s.setValue(QStringLiteral("findInFiles/newTab"), m_newTab->isChecked());
    s.setValue(QStringLiteral("findInFiles/gitignore"), m_gitignore->isChecked());
    s.setValue(QStringLiteral("findInFiles/preserveCase"), m_preserveCase->isChecked());
}

void FindInFilesPanel::remember(QComboBox *combo, const QString &key)
{
    const QString text = combo->currentText();
    if (text.isEmpty())
        return;
    const int existing = combo->findText(text);
    if (existing >= 0)
        combo->removeItem(existing);
    combo->insertItem(0, text);
    combo->setCurrentIndex(0);
    QStringList items;
    for (int i = 0; i < combo->count(); ++i)
        items << combo->itemText(i);
    QSettings().setValue(key, items);
}

void FindInFilesPanel::activate(const QString &initialText)
{
    show();
    if (!initialText.isEmpty())
        m_find->setEditText(initialText);
    m_find->setFocus();
    m_find->lineEdit()->selectAll();
}

bool FindInFilesPanel::resultsInNewTab() const
{
    return m_newTab->isChecked();
}

FindInFiles::Options FindInFilesPanel::currentOptions() const
{
    FindInFiles::Options o;
    o.pattern = m_find->currentText();
    o.replacement = m_replace->currentText();
    o.where = m_where->currentText();
    o.regex = m_regex->isChecked();
    o.caseSensitive = m_case->isChecked();
    o.wholeWord = m_word->isChecked();
    o.showContext = m_context->isChecked();
    o.respectGitignore = m_gitignore->isChecked();
    o.preserveCase = m_preserveCase->isChecked();
    return o;
}

void FindInFilesPanel::find()
{
    start(false);
}

void FindInFilesPanel::replace()
{
    start(true);
}

void FindInFilesPanel::cancel()
{
    if (m_cancel)
        m_cancel->store(true);
}

void FindInFilesPanel::start(bool replace)
{
    if (isRunning() || !m_provider)
        return;
    const FindInFiles::Options options = currentOptions();
    if (options.pattern.isEmpty()) {
        m_status->setText(tr("Escribe qué buscar."));
        m_find->setFocus();
        return;
    }
    remember(m_find, QStringLiteral("findInFiles/findHistory"));
    remember(m_where, QStringLiteral("findInFiles/whereHistory"));
    if (replace)
        remember(m_replace, QStringLiteral("findInFiles/replaceHistory"));

    const FindInFiles::Workspace workspace = m_provider();
    m_cancel = std::make_shared<std::atomic_bool>(false);
    const auto cancelFlag = m_cancel;
    QPointer<FindInFilesPanel> self(this);

    m_thread = QThread::create([self, options, workspace, cancelFlag, replace] {
        const FindInFiles::Results results = FindInFiles::search(options, workspace, *cancelFlag, [self](int files) {
            QMetaObject::invokeMethod(qApp, [self, files] {
                if (self && self->isRunning())
                    self->m_status->setText(tr("Buscando… %1 archivos revisados").arg(files));
            }, Qt::QueuedConnection);
        });
        QMetaObject::invokeMethod(qApp, [self, results, replace] {
            if (self)
                self->finished(results, replace);
        }, Qt::QueuedConnection);
    });
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_findButton->setText(tr("Detener"));
    m_replaceButton->setEnabled(false);
    m_status->setText(tr("Buscando…"));
    m_thread->start();
}

void FindInFilesPanel::finished(const FindInFiles::Results &results, bool replace)
{
    if (m_thread)
        m_thread->wait();
    m_thread = nullptr;
    m_findButton->setText(tr("Buscar"));
    m_replaceButton->setEnabled(true);

    if (!results.error.isEmpty()) {
        m_status->setText(results.error);
        return;
    }
    QString status = results.totalMatches == 1
        ? tr("1 coincidencia en %1 de %2 archivos").arg(results.files.size()).arg(results.filesSearched)
        : tr("%1 coincidencias en %2 de %3 archivos").arg(results.totalMatches).arg(results.files.size()).arg(results.filesSearched);
    if (results.cancelled)
        status += tr(" (detenida)");
    m_status->setText(status);
    if (replace)
        Q_EMIT replaceSearchFinished(results);
    else
        Q_EMIT searchFinished(results);
}

void FindInFilesPanel::appendWhere(const QString &entry)
{
    QString text = m_where->currentText().trimmed();
    if (!text.isEmpty() && !text.endsWith(QLatin1Char(',')))
        text += QStringLiteral(", ");
    else if (!text.isEmpty())
        text += QLatin1Char(' ');
    m_where->setEditText(text + entry);
    m_where->setFocus();
}

void FindInFilesPanel::showWhereMenu()
{
    QMenu menu(this);
    menu.addAction(tr("Añadir carpeta…"), this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Buscar en la carpeta"));
        if (!dir.isEmpty())
            appendWhere(QDir::toNativeSeparators(dir));
    });
    menu.addAction(tr("Añadir filtro de inclusión (*.cpp)"), this, [this] { appendWhere(QStringLiteral("*.cpp")); });
    menu.addAction(tr("Añadir filtro de exclusión (-*.log)"), this, [this] { appendWhere(QStringLiteral("-*.log")); });
    menu.addSeparator();
    menu.addAction(tr("Añadir carpetas abiertas"), this, [this] { appendWhere(FindInFiles::kOpenFolders); });
    menu.addAction(tr("Añadir archivos abiertos"), this, [this] { appendWhere(FindInFiles::kOpenFiles); });
    menu.addSeparator();
    menu.addAction(tr("Limpiar"), this, [this] { m_where->setEditText(QString()); });
    menu.exec(m_whereButton->mapToGlobal(QPoint(0, m_whereButton->height())));
}

void FindInFilesPanel::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (isRunning())
            cancel();
        else
            Q_EMIT closeRequested();
        return;
    }
    QWidget::keyPressEvent(event);
}
