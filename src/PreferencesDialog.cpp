#include "PreferencesDialog.h"

#include "Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

QGroupBox *group(const QString &title, QFormLayout **form, QWidget *parent)
{
    auto *box = new QGroupBox(title, parent);
    *form = new QFormLayout(box);
    (*form)->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    (*form)->setHorizontalSpacing(14);
    (*form)->setVerticalSpacing(8);
    return box;
}

QLabel *hint(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setWordWrap(true);
    QPalette p = l->palette();
    p.setColor(QPalette::WindowText, Theme::colors().muted);
    l->setPalette(p);
    return l;
}

} // namespace

PreferencesDialog::PreferencesDialog(const Values &values, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferencias"));
    setMinimumWidth(460);

    // --- General ---
    QFormLayout *general = nullptr;
    auto *generalBox = group(tr("General"), &general, this);
    m_language = new QComboBox(generalBox);
    m_language->addItem(I18n::displayName(I18n::Language::English), int(I18n::Language::English));
    m_language->addItem(I18n::displayName(I18n::Language::Spanish), int(I18n::Language::Spanish));
    m_language->setCurrentIndex(m_language->findData(int(values.language)));
    general->addRow(tr("Idioma"), m_language);
    general->addRow(QString(), hint(tr("El cambio de idioma se aplica al reiniciar BatCode."), generalBox));

    // --- Editor ---
    QFormLayout *editor = nullptr;
    auto *editorBox = group(tr("Editor"), &editor, this);
    m_fontSize = new QSpinBox(editorBox);
    m_fontSize->setRange(6, 48);
    m_fontSize->setSuffix(tr(" pt"));
    m_fontSize->setValue(values.fontSize);
    m_indent = new QSpinBox(editorBox);
    m_indent->setRange(1, 16);
    m_indent->setSuffix(tr(" espacios"));
    m_indent->setValue(values.indentWidth);
    m_wordWrap = new QCheckBox(tr("Ajustar las líneas largas al ancho de la ventana"), editorBox);
    m_wordWrap->setChecked(values.wordWrap);
    m_lineNumbers = new QCheckBox(tr("Mostrar los números de línea"), editorBox);
    m_lineNumbers->setChecked(values.lineNumbers);
    editor->addRow(tr("Tamaño de fuente"), m_fontSize);
    editor->addRow(tr("Sangría"), m_indent);
    editor->addRow(QString(), m_wordWrap);
    editor->addRow(QString(), m_lineNumbers);

    // --- Git ---
    QFormLayout *git = nullptr;
    auto *gitBox = group(tr("Git"), &git, this);
    m_pull = new QComboBox(gitBox);
    for (const char *c : {"pull", "pull --ff-only", "pull --rebase", "pull --rebase --autostash"})
        m_pull->addItem(QStringLiteral("git ") + QLatin1String(c), QString::fromLatin1(c));
    m_pull->setCurrentIndex(qMax(0, m_pull->findData(values.pullCommand)));
    m_push = new QComboBox(gitBox);
    for (const char *c : {"push", "push --follow-tags"})
        m_push->addItem(QStringLiteral("git ") + QLatin1String(c), QString::fromLatin1(c));
    m_push->setCurrentIndex(qMax(0, m_push->findData(values.pushCommand)));
    git->addRow(tr("Botón Pull"), m_pull);
    git->addRow(tr("Botón Push"), m_push);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    layout->addWidget(generalBox);
    layout->addWidget(editorBox);
    layout->addWidget(gitBox);
    layout->addStretch(1);
    layout->addWidget(buttons);
}

PreferencesDialog::Values PreferencesDialog::values() const
{
    Values v;
    v.language = I18n::Language(m_language->currentData().toInt());
    v.fontSize = m_fontSize->value();
    v.indentWidth = m_indent->value();
    v.wordWrap = m_wordWrap->isChecked();
    v.lineNumbers = m_lineNumbers->isChecked();
    v.pullCommand = m_pull->currentData().toString();
    v.pushCommand = m_push->currentData().toString();
    return v;
}
