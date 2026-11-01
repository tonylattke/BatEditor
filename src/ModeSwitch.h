#pragma once

#include <QWidget>

class QToolButton;

// Interruptor segmentado de la esquina superior derecha: [ Editor | Git ]
class ModeSwitch : public QWidget
{
    Q_OBJECT
public:
    enum Mode { Editor = 0, Git = 1 };

    explicit ModeSwitch(QWidget *parent = nullptr);
    Mode mode() const { return m_mode; }

public Q_SLOTS:
    void setMode(ModeSwitch::Mode mode);
    void toggle() { setMode(m_mode == Editor ? Git : Editor); }

Q_SIGNALS:
    void modeChanged(ModeSwitch::Mode mode);

private:
    QToolButton *m_editor;
    QToolButton *m_git;
    Mode m_mode = Editor;
};
