#pragma once

#include "GitRepository.h"

#include <QPlainTextEdit>
#include <QWidget>

class QHBoxLayout;
class QToolButton;

// Vista de solo lectura de un diff: altura ajustada al contenido para que
// el desplazamiento lo haga el panel que la contiene.
class DiffTextView : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit DiffTextView(QWidget *parent = nullptr);
    void setDiffText(const QString &text);

protected:
    void wheelEvent(QWheelEvent *e) override;

private:
    void fitHeight();
};

// Un archivo cambiado: cabecera (plegable) con estado, ruta, estadísticas y acciones + su diff.
class FileDiffWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FileDiffWidget(const GitRepository::FileChange &change, QWidget *parent = nullptr);

    QToolButton *addAction(const QString &text, const QString &toolTip);
    void setExpanded(bool expanded);
    bool isExpanded() const { return m_expanded; }
    const GitRepository::FileChange &change() const { return m_change; }

Q_SIGNALS:
    void contextMenuRequested(const QPoint &globalPos);   // clic derecho en la cabecera

private:
    GitRepository::FileChange m_change;
    QToolButton *m_toggle;
    QHBoxLayout *m_actions;
    QWidget *m_body;
    bool m_expanded = false;
};

QColor colorForChangeCode(QChar code);
QString describeChangeCode(QChar code);
