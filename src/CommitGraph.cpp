#include "CommitGraph.h"

#include <algorithm>

namespace CommitGraph {

namespace {

struct Lane {
    QString expected;    // commit que continúa este carril; vacío = columna libre
    int color = 0;
    bool dashed = false;
};

int freeLane(QList<Lane> &lanes, int skip = -1)
{
    for (int i = 0; i < lanes.size(); ++i)
        if (i != skip && lanes.at(i).expected.isEmpty())
            return i;
    lanes.append(Lane());
    return int(lanes.size()) - 1;
}

} // namespace

QList<Row> layout(const QList<Input> &commits)
{
    QList<Row> rows;
    rows.reserve(commits.size());
    QList<Lane> lanes;
    int nextColor = 0;

    for (const Input &c : commits) {
        const QList<Lane> before = lanes;
        Row row;

        QList<int> incoming;
        for (int i = 0; i < before.size(); ++i)
            if (before.at(i).expected == c.id)
                incoming << i;

        // Nodo: en el primer carril que espera este commit, o en una columna nueva (punta de rama)
        if (incoming.isEmpty()) {
            row.lane = freeLane(lanes);
            row.color = nextColor++;
        } else {
            row.lane = incoming.first();
            row.color = before.at(row.lane).color;
        }

        // Mitad superior: los carriles que llegan al nodo convergen; el resto pasa de largo
        for (int i = 0; i < before.size(); ++i) {
            const Lane &l = before.at(i);
            if (l.expected.isEmpty())
                continue;
            if (incoming.contains(i))
                row.edges.append({i, Top, row.lane, Center, l.color, l.dashed});
            else
                row.edges.append({i, Top, i, Bottom, l.color, l.dashed});
        }
        for (int i : incoming)
            lanes[i] = Lane();

        // Mitad inferior: el primer padre sigue en la columna del nodo; los demás (merge)
        // se unen a su carril si ya existe, o abren uno nuevo
        if (!c.parents.isEmpty()) {
            lanes[row.lane] = {c.parents.first(), row.color, c.dashedToParents};
            row.edges.append({row.lane, Center, row.lane, Bottom, row.color, c.dashedToParents});
        }
        for (int p = 1; p < c.parents.size(); ++p) {
            const QString &parent = c.parents.at(p);
            int target = -1;
            for (int i = 0; i < lanes.size() && target < 0; ++i)
                if (i != row.lane && lanes.at(i).expected == parent)
                    target = i;
            if (target < 0) {
                target = freeLane(lanes, row.lane);
                lanes[target] = {parent, nextColor++, c.dashedToParents};
            }
            row.edges.append({row.lane, Center, target, Bottom, lanes.at(target).color, c.dashedToParents});
        }

        while (!lanes.isEmpty() && lanes.last().expected.isEmpty())
            lanes.removeLast();

        int used = row.lane + 1;
        for (const Edge &e : row.edges)
            used = std::max({used, e.fromLane + 1, e.toLane + 1});
        row.laneCount = used;
        rows << row;
    }
    return rows;
}

} // namespace CommitGraph
