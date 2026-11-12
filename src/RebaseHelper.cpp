#include "RebaseHelper.h"

#include <QCoreApplication>
#include <QFile>

namespace {

bool readFile(const QString &path, QByteArray *out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    *out = f.readAll();
    return true;
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
}

bool isCommand(const QByteArray &line)
{
    const QByteArray t = line.trimmed();
    return !t.isEmpty() && !t.startsWith('#');
}

// "pick <sha> <asunto>": ¿el sha abreviado de la línea corresponde al commit?
bool matches(const QByteArray &line, const QByteArray &sha)
{
    const QList<QByteArray> parts = line.trimmed().split(' ');
    return parts.size() >= 2 && parts.at(1).size() >= 4 && sha.startsWith(parts.at(1));
}

QByteArray withCommand(const QByteArray &line, const QByteArray &command)
{
    const QByteArray t = line.trimmed();
    const int space = t.indexOf(' ');
    return command + t.mid(space);
}

int editTodo(const QByteArray &op, const QByteArray &sha, const QString &todoPath)
{
    QByteArray data;
    if (!readFile(todoPath, &data))
        return 1;
    const bool crlf = data.contains("\r\n");
    data.replace("\r\n", "\n");
    QList<QByteArray> lines = data.split('\n');

    int target = -1;
    for (int i = 0; i < lines.size(); ++i)
        if (isCommand(lines.at(i)) && matches(lines.at(i), sha)) {
            target = i;
            break;
        }
    if (target < 0)
        return 1;

    // En el archivo de tareas el commit más antiguo va primero
    auto neighbour = [&](int step) {
        for (int i = target + step; i >= 0 && i < lines.size(); i += step)
            if (isCommand(lines.at(i)))
                return i;
        return -1;
    };

    if (op == "reword" || op == "squash" || op == "drop") {
        lines[target] = withCommand(lines.at(target), op);
    } else if (op == "up" || op == "down") {
        const int other = neighbour(op == "up" ? 1 : -1);
        if (other < 0)
            return 1;
        lines.swapItemsAt(target, other);
    } else {
        return 1;
    }

    QByteArray out = lines.join('\n');
    if (crlf)
        out.replace("\n", "\r\n");
    return writeFile(todoPath, out) ? 0 : 1;
}

QString shellQuote(QString s)
{
    s.replace(QLatin1Char('\\'), QLatin1Char('/'));
    s.replace(QLatin1String("'"), QLatin1String("'\\''"));
    return QLatin1Char('\'') + s + QLatin1Char('\'');
}

} // namespace

namespace RebaseHelper {

int run(int argc, char *argv[])
{
    if (argc < 2)
        return -1;
    const QByteArray mode = argv[1];
    if (mode == "--rebase-todo" && argc >= 5)
        return editTodo(argv[2], argv[3], QString::fromLocal8Bit(argv[4]));
    if (mode == "--rebase-message" && argc >= 4) {
        QByteArray message;
        if (!readFile(QString::fromLocal8Bit(argv[2]), &message))
            return 1;
        return writeFile(QString::fromLocal8Bit(argv[3]), message) ? 0 : 1;
    }
    if (mode == "--rebase-todo" || mode == "--rebase-message")
        return 1;
    return -1;
}

QString editorCommand(const QStringList &args)
{
    // git ejecuta el editor con sh (también en Windows): rutas con '/' y entre comillas simples
    QStringList parts{shellQuote(QCoreApplication::applicationFilePath())};
    for (const QString &a : args)
        parts << shellQuote(a);
    return parts.join(QLatin1Char(' '));
}

} // namespace RebaseHelper
