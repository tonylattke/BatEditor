#include "FindInFiles.h"

#include "GitRepository.h"
#include "Theme.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStringDecoder>
#include <QTextBlock>

#include <memory>

namespace FindInFiles {

namespace {

constexpr int kContextLines = 2;
constexpr int kMaxTotalMatches = 50000;
constexpr int kMaxFiles = 200000;
constexpr qint64 kMaxFileSize = 20 * 1024 * 1024;
constexpr int kLinePrefix = 8;   // "    67: " → 6 dígitos + marca + espacio

QString ftr(const char *text)
{
    return QCoreApplication::translate("FindInFiles", text);
}

Qt::CaseSensitivity pathCase()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

QRegularExpression wildcard(const QString &pattern)
{
    // "*.cpp" → coincide con el nombre completo del archivo o carpeta
    return QRegularExpression(QRegularExpression::wildcardToRegularExpression(pattern),
                              pathCase() == Qt::CaseInsensitive ? QRegularExpression::CaseInsensitiveOption
                                                                : QRegularExpression::NoPatternOption);
}

bool looksLikeFilter(const QString &entry)
{
    return entry.contains(QLatin1Char('*')) || entry.contains(QLatin1Char('?'));
}

bool isBinary(const QByteArray &data)
{
    return data.left(8000).contains('\0');
}

QStringList splitLines(const QString &text)
{
    QStringList lines = text.split(QLatin1Char('\n'));
    for (QString &l : lines)
        if (l.endsWith(QLatin1Char('\r')))
            l.chop(1);
    if (!lines.isEmpty() && lines.last().isEmpty() && text.endsWith(QLatin1Char('\n')))
        lines.removeLast();
    return lines;
}

// Lee un archivo como texto (UTF-8 o, si no lo es, Latin-1). Devuelve false si es binario o enorme.
bool readText(const QString &path, QString *text)
{
    QFile file(path);
    if (file.size() > kMaxFileSize || !file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = file.readAll();
    if (isBinary(data))
        return false;
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    *text = decoder.decode(data);
    if (decoder.hasError())
        *text = QString::fromLatin1(data);
    if (text->startsWith(QChar(0xFEFF)))
        text->remove(0, 1);
    return true;
}

} // namespace

QRegularExpression buildRegex(const Options &options, QString *error)
{
    QString expr = options.regex ? options.pattern : QRegularExpression::escape(options.pattern);
    if (options.wholeWord)
        expr = QStringLiteral("\\b(?:%1)\\b").arg(expr);
    QRegularExpression::PatternOptions flags = QRegularExpression::UseUnicodePropertiesOption;
    if (!options.caseSensitive)
        flags |= QRegularExpression::CaseInsensitiveOption;
    QRegularExpression re(expr, flags);
    if (!re.isValid() && error)
        *error = ftr("Expresión regular inválida: %1").arg(re.errorString());
    return re;
}

QStringList collectFiles(const Options &options, const Workspace &workspace, QString *error,
                         const std::atomic_bool *cancel)
{
    QStringList entries;
    for (const QString &raw : options.where.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString e = raw.trimmed();
        if (!e.isEmpty())
            entries << e;
    }

    QList<QRegularExpression> includes, excludes;
    QStringList roots;     // carpetas
    QStringList explicitFiles;
    bool anyLocation = false;
    for (const QString &e : std::as_const(entries)) {
        if (e == kOpenFolders) {
            roots << workspace.openFolders;
            anyLocation = true;
        } else if (e == kOpenFiles) {
            explicitFiles << workspace.openFiles;
            anyLocation = true;
        } else if (e.startsWith(QLatin1Char('-'))) {
            excludes << wildcard(e.mid(1).trimmed().remove(QLatin1Char('/')).remove(QLatin1Char('\\')));
        } else if (QFileInfo(e).isDir()) {
            roots << QFileInfo(e).absoluteFilePath();
            anyLocation = true;
        } else if (QFileInfo(e).isFile()) {
            explicitFiles << QFileInfo(e).absoluteFilePath();
            anyLocation = true;
        } else if (looksLikeFilter(e)) {
            includes << wildcard(e);
        } else if (error) {
            *error = ftr("No existe: %1").arg(e);
            return {};
        }
    }
    if (!anyLocation) {
        // Por defecto: las carpetas abiertas, o si no hay, los archivos abiertos
        roots << workspace.openFolders;
        if (roots.isEmpty())
            explicitFiles << workspace.openFiles;
    }
    if (roots.isEmpty() && explicitFiles.isEmpty()) {
        if (error)
            *error = ftr("No hay dónde buscar: abre una carpeta o escribe una ruta en «Donde».");
        return {};
    }

    auto excluded = [&](const QString &name) {
        for (const QRegularExpression &re : std::as_const(excludes))
            if (re.match(name).hasMatch())
                return true;
        return false;
    };
    auto included = [&](const QString &name) {
        if (includes.isEmpty())
            return true;
        for (const QRegularExpression &re : std::as_const(includes))
            if (re.match(name).hasMatch())
                return true;
        return false;
    };

    QStringList files;
    QSet<QString> seen;
    auto add = [&](const QString &path) {
        const QString key = pathCase() == Qt::CaseInsensitive ? path.toLower() : path;
        if (!seen.contains(key)) {
            seen.insert(key);
            files << path;
        }
    };
    for (const QString &f : std::as_const(explicitFiles)) {
        const QString name = QFileInfo(f).fileName();
        if (!excluded(name) && included(name))
            add(QDir::cleanPath(f));
    }

    for (const QString &root : std::as_const(roots)) {
        std::unique_ptr<GitRepository> repo;
        if (options.respectGitignore) {
            repo = std::make_unique<GitRepository>();
            if (!repo->open(root))
                repo.reset();
        }
        QStringList pending = {QDir::cleanPath(root)};
        while (!pending.isEmpty() && files.size() < kMaxFiles) {
            if (cancel && cancel->load())
                return files;
            const QDir dir(pending.takeLast());
            const QFileInfoList list = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                                                         QDir::Name | QDir::DirsLast);
            for (const QFileInfo &info : list) {
                const QString name = info.fileName();
                if (info.isDir()) {
                    if (name == QLatin1String(".git") || info.isSymLink() || excluded(name))
                        continue;
                    if (repo && repo->isIgnored(info.absoluteFilePath(), true))
                        continue;
                    pending << info.absoluteFilePath();
                } else {
                    if (excluded(name) || !included(name))
                        continue;
                    if (repo && repo->isIgnored(info.absoluteFilePath(), false))
                        continue;
                    add(info.absoluteFilePath());
                }
            }
        }
    }
    return files;
}

Results search(const Options &options, const Workspace &workspace, const std::atomic_bool &cancel,
               const std::function<void(int)> &progress)
{
    Results results;
    results.options = options;
    const QRegularExpression regex = buildRegex(options, &results.error);
    if (!results.error.isEmpty())
        return results;
    const QStringList files = collectFiles(options, workspace, &results.error, &cancel);
    if (!results.error.isEmpty())
        return results;

    for (const QString &path : files) {
        if (cancel.load()) {
            results.cancelled = true;
            break;
        }
        QString text;
        const auto buffer = workspace.buffers.constFind(path);
        if (buffer != workspace.buffers.constEnd())
            text = buffer.value();
        else if (!readText(path, &text))
            continue;
        ++results.filesSearched;
        if (progress && results.filesSearched % 50 == 0)
            progress(results.filesSearched);

        const QStringList lines = splitLines(text);
        FileResult fr;
        fr.path = path;
        int lastAdded = 0; // último número de línea añadido (contexto incluido)
        QList<int> matchedIndexes;
        QList<QList<QPair<int, int>>> matchRanges;
        for (int i = 0; i < lines.size(); ++i) {
            QList<QPair<int, int>> ranges;
            auto it = regex.globalMatch(lines.at(i));
            while (it.hasNext()) {
                const QRegularExpressionMatch m = it.next();
                if (m.capturedLength() > 0)
                    ranges.append({int(m.capturedStart()), int(m.capturedLength())});
            }
            if (ranges.isEmpty())
                continue;
            matchedIndexes << i;
            matchRanges << ranges;
            fr.matchCount += int(ranges.size());
        }
        if (fr.matchCount == 0)
            continue;

        for (int k = 0; k < matchedIndexes.size(); ++k) {
            const int i = matchedIndexes.at(k);
            const int from = options.showContext ? qMax(0, i - kContextLines) : i;
            for (int c = qMax(from, lastAdded); c < i; ++c)
                fr.lines.append({c + 1, lines.at(c), {}});
            if (lastAdded <= i) {
                fr.lines.append({i + 1, lines.at(i), matchRanges.at(k)});
                lastAdded = i + 1;
            }
            if (options.showContext) {
                const int nextMatch = k + 1 < matchedIndexes.size() ? matchedIndexes.at(k + 1) : int(lines.size());
                const int to = qMin(int(lines.size()), qMin(i + 1 + kContextLines, nextMatch));
                for (int c = lastAdded; c < to; ++c)
                    fr.lines.append({c + 1, lines.at(c), {}});
                lastAdded = qMax(lastAdded, to);
            }
        }
        results.totalMatches += fr.matchCount;
        results.files << fr;
        if (results.totalMatches >= kMaxTotalMatches) {
            results.truncated = true;
            break;
        }
    }
    if (progress)
        progress(results.filesSearched);
    return results;
}

QString replacementFor(const QRegularExpressionMatch &match, const Options &options)
{
    QString out;
    const QString &rep = options.replacement;
    if (!options.regex) {
        out = rep;
    } else {
        // \0..\9 y $0..$9 se sustituyen por los grupos capturados; \\ es una barra
        for (int i = 0; i < rep.size(); ++i) {
            const QChar ch = rep.at(i);
            if ((ch == QLatin1Char('\\') || ch == QLatin1Char('$')) && i + 1 < rep.size() && rep.at(i + 1).isDigit()) {
                out += match.captured(rep.at(i + 1).digitValue());
                ++i;
            } else if (ch == QLatin1Char('\\') && i + 1 < rep.size()) {
                const QChar next = rep.at(i + 1);
                out += next == QLatin1Char('n') ? QChar(QLatin1Char('\n'))
                       : next == QLatin1Char('t') ? QChar(QLatin1Char('\t')) : next;
                ++i;
            } else {
                out += ch;
            }
        }
    }
    if (options.preserveCase) {
        const QString found = match.captured(0);
        bool hasLetter = false, allUpper = true, allLower = true;
        for (const QChar c : found)
            if (c.isLetter()) {
                hasLetter = true;
                allUpper = allUpper && c.isUpper();
                allLower = allLower && c.isLower();
            }
        if (hasLetter && allUpper && found.size() > 1)
            out = out.toUpper();
        else if (hasLetter && allLower)
            out = out.toLower();
        else if (hasLetter && !found.isEmpty() && found.at(0).isUpper() && !out.isEmpty())
            out[0] = out.at(0).toUpper();
    }
    return out;
}

int replaceInText(QString &text, const QRegularExpression &regex, const Options &options)
{
    int count = 0;
    QStringList lines = text.split(QLatin1Char('\n'));
    for (QString &line : lines) {
        const bool cr = line.endsWith(QLatin1Char('\r'));
        if (cr)
            line.chop(1);
        QString out;
        int last = 0;
        auto it = regex.globalMatch(line);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            if (m.capturedLength() == 0)
                continue;
            out += line.mid(last, m.capturedStart() - last);
            out += replacementFor(m, options);
            last = int(m.capturedEnd());
            ++count;
        }
        if (last > 0) {
            out += line.mid(last);
            line = out;
        }
        if (cr)
            line += QLatin1Char('\r');
    }
    if (count > 0)
        text = lines.join(QLatin1Char('\n'));
    return count;
}

Document buildDocument(const Results &results, const QString &title)
{
    Document doc;
    QStringList out;
    out << title << QString();
    for (const FileResult &fr : results.files) {
        doc.locations.insert(int(out.size()), {fr.path, 1, 0, 0});
        out << QDir::toNativeSeparators(fr.path) + QLatin1Char(':');
        int previous = 0;
        for (const Line &line : fr.lines) {
            if (previous != 0 && line.number > previous + 1)
                out << QStringLiteral("  ..");
            previous = line.number;
            const bool isMatch = !line.matches.isEmpty();
            const int block = int(out.size());
            out << QString::number(line.number).rightJustified(kLinePrefix - 2) + (isMatch ? QLatin1Char(':') : QLatin1Char(' '))
                       + QLatin1Char(' ') + line.text;
            if (isMatch) {
                doc.locations.insert(block, {fr.path, line.number, line.matches.first().first, line.matches.first().second});
                doc.matchBlocks << block;
                for (const auto &m : line.matches)
                    doc.highlights.append({block, kLinePrefix + m.first, m.second});
            } else {
                doc.locations.insert(block, {fr.path, line.number, 0, 0});
            }
        }
        out << QString();
    }
    doc.text = out.join(QLatin1Char('\n'));
    return doc;
}

void ResultsHighlighter::highlightBlock(const QString &text)
{
    const Theme::Colors &c = Theme::colors();
    if (currentBlock().blockNumber() == 0) {
        QTextCharFormat f;
        f.setForeground(c.foreground);
        f.setFontWeight(QFont::Bold);
        setFormat(0, int(text.size()), f);
        return;
    }
    if (text.isEmpty())
        return;
    if (!text.at(0).isSpace()) {
        QTextCharFormat f;
        // Ruta del archivo ("F:\proyecto\archivo.cpp:") o línea de resumen final
        f.setForeground(text.endsWith(QLatin1Char(':')) ? c.number : c.muted);
        setFormat(0, int(text.size()), f);
        return;
    }
    if (text.trimmed() == QLatin1String("..")) {
        QTextCharFormat f;
        f.setForeground(c.muted);
        setFormat(0, int(text.size()), f);
        return;
    }
    if (text.size() >= kLinePrefix && text.at(kLinePrefix - 3).isDigit()) {
        const bool isMatch = text.at(kLinePrefix - 2) == QLatin1Char(':');
        QTextCharFormat f;
        f.setForeground(isMatch ? c.removed : c.number);
        setFormat(0, kLinePrefix - 1, f);
    }
}

} // namespace FindInFiles
