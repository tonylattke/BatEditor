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
    void contextMenuEvent(QContextMenuEvent *e) override;

private:
    void fitHeight();
    QString selectedCode() const;   // la selección sin las marcas +/-/espacio ni las cabeceras @@
};

// Un archivo cambiado: cabecera (plegable) con estado, ruta, estadísticas y acciones + su diff.
class FileDiffWidget : public QWidget
{
    Q_OBJECT
public:
    // Qué botones mostrar en cada bloque @@ del diff
    enum class HunkActions { None, Unstaged, Staged };

    explicit FileDiffWidget(const GitRepository::FileChange &change, QWidget *parent = nullptr,
                            HunkActions hunkActions = HunkActions::None);

    QToolButton *addAction(const QString &text, const QString &toolTip);
    void setExpanded(bool expanded);
    bool isExpanded() const { return m_expanded; }
    const GitRepository::FileChange &change() const { return m_change; }

Q_SIGNALS:
    void contextMenuRequested(const QPoint &globalPos);   // clic derecho en la cabecera
    void hunkStageRequested(int index);
    void hunkDiscardRequested(int index);
    void hunkUnstageRequested(int index);

private:
    QWidget *makeHunkBlock(const QString &hunkText, int index);
    GitRepository::FileChange m_change;
    HunkActions m_hunkActions = HunkActions::None;
    QToolButton *m_toggle;
    QHBoxLayout *m_actions;
    QWidget *m_body;
    bool m_expanded = false;
};

QColor colorForChangeCode(QChar code);
QString describeChangeCode(QChar code);
