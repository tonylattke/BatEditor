#pragma once

#include "I18n.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QSpinBox;

// Archivo › Preferencias: idioma, editor y opciones por defecto de Git.
class PreferencesDialog : public QDialog
{
    Q_OBJECT
public:
    struct Values {
        I18n::Language language = I18n::Language::English;
        int fontSize = 10;
        int indentWidth = 4;
        bool wordWrap = false;
        bool lineNumbers = true;
        bool minimap = true;
        QString pullCommand = QStringLiteral("pull");
        QString pushCommand = QStringLiteral("push");
    };

    explicit PreferencesDialog(const Values &values, QWidget *parent = nullptr);
    Values values() const;

private:
    QComboBox *m_language;
    QSpinBox *m_fontSize;
    QSpinBox *m_indent;
    QCheckBox *m_wordWrap;
    QCheckBox *m_lineNumbers;
    QCheckBox *m_minimap;
    QComboBox *m_pull;
    QComboBox *m_push;
};
