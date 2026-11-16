#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

// Distribución en carriles (columnas) del grafo de commits, como en Sublime Merge o "git log --graph".
// Cada fila guarda los tramos que hay que dibujar en ella; las columnas son estables: un carril no
// cambia de columna mientras sigue abierto.
namespace CommitGraph {

enum Anchor { Top, Center, Bottom };   // dónde empieza o termina un tramo dentro de la fila

struct Edge {
    int fromLane = 0;
    Anchor from = Top;
    int toLane = 0;
    Anchor to = Bottom;
    int color = 0;        // índice de color del carril
    bool dashed = false;  // tramo de los cambios actuales a HEAD
};

struct Row {
    int lane = 0;         // columna del nodo
    int color = 0;
    int laneCount = 0;    // columnas ocupadas en esta fila (para sangrar el texto)
    QList<Edge> edges;
};

struct Input {
    QString id;
    QStringList parents;
    bool dashedToParents = false;   // p. ej. la fila "cambios actuales", cuyo padre es HEAD
};

QList<Row> layout(const QList<Input> &commits);

} // namespace CommitGraph

Q_DECLARE_METATYPE(CommitGraph::Row)
