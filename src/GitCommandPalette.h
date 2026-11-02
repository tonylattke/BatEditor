#pragma once

#include <QFrame>
#include <QList>

#include <functional>

class QLabel;
class QLineEdit;
class QListWidget;

// Paleta emergente de comandos (estilo "Pull Options"): una etiqueta con el
// nombre de la operación, un campo para filtrar y la lista de comandos con su
// descripción a la derecha. Se navega con ↑/↓, Enter ejecuta y Esc cierra.
class GitCommandPalette : public QFrame
{
    Q_OBJECT
public:
    struct Item {
        QString command;       // texto principal ("pull --rebase")
        QString description;   // explicación a la derecha
        std::function<void()> action;
        bool enabled = true;
    };

    GitCommandPalette(const QString &title, const QString &placeholder, const QList<Item> &items,
                      QWidget *parent);

    // Muestra la paleta centrada en la parte superior de `anchor` y la borra al cerrarse.
    void popup(QWidget *anchor, int preferredWidth = 1000);

    // Selecciona la fila que muestra exactamente `command` (para destacar la opción por defecto).
    void setCurrentCommand(const QString &command);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refilter();
    void activateCurrent();

    QList<Item> m_items;
    QLabel *m_title;
    QLineEdit *m_filter;
    QListWidget *m_list;
};
