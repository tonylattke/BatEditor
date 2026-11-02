#pragma once

#include <QWidget>

class CodeEditor;

// Minimapa a la derecha del editor, como en Sublime Text: el archivo en miniatura
// con los colores del resaltado y un recuadro con la parte visible.
//  - Clic: va a esa línea (la centra y pone el cursor allí).
//  - Arrastrar (dentro o fuera del recuadro): desplaza el editor.
//  - Rueda: desplaza el editor.
class Minimap : public QWidget
{
public:
    explicit Minimap(CodeEditor *editor);

    static constexpr int kWidth = 120;      // ancho del minimapa en píxeles
    static constexpr int kLineHeight = 3;   // alto de cada línea en el minimapa
    static constexpr int kMaxColumns = 110; // columnas dibujadas por línea

    // Geometría (también usada por las pruebas)
    int lineAt(int y) const;                // línea (0..) bajo la coordenada y
    QRect viewportRect() const;             // recuadro de la parte visible

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    int firstVisibleLine() const;
    int visibleLineCount() const;
    int maxFirstLine() const;
    int offset() const;                     // desplazamiento del minimapa en píxeles
    double boxSpeed() const;                // píxeles del recuadro por línea del editor
    void scrollToFirstLine(int line);
    void dragTo(int y);

    CodeEditor *m_editor;
    bool m_dragging = false;
    bool m_moved = false;
    int m_grab = 0;                         // dónde se agarró el recuadro (y relativa)
    int m_pressY = 0;
    int m_pressLine = 0;                    // línea bajo el ratón al pulsar
};
