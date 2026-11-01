#pragma once

#include <QWidget>

class CodeEditor;
class QLabel;
class QLineEdit;
class QToolButton;
class QWidget;

// Barra inferior de buscar/reemplazar (Ctrl+F / Ctrl+H).
class FindBar : public QWidget
{
    Q_OBJECT
public:
    explicit FindBar(CodeEditor *editor, QWidget *parent = nullptr);

    void showFind(bool withReplace);
    void closeBar();

protected:
    void keyPressEvent(QKeyEvent *e) override;

private:
    void updateSearch();
    void updateCounter();

    CodeEditor *m_editor;
    QLineEdit *m_find;
    QLineEdit *m_replace;
    QWidget *m_replaceRow;
    QToolButton *m_regex;
    QToolButton *m_case;
    QToolButton *m_word;
    QLabel *m_counter;
};
