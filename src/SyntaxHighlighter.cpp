#include "SyntaxHighlighter.h"

#include "Theme.h"

#include <QRegularExpression>

namespace {

bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

} // namespace

SyntaxHighlighter::SyntaxHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
    , m_lang(Languages::plainText())
{
    const Theme::Colors &c = Theme::colors();
    m_keywordFmt.setForeground(c.keyword);
    m_typeFmt.setForeground(c.type);
    m_constantFmt.setForeground(c.constant);
    m_stringFmt.setForeground(c.string);
    m_numberFmt.setForeground(c.number);
    m_commentFmt.setForeground(c.comment);
    m_commentFmt.setFontItalic(true);
    m_functionFmt.setForeground(c.function);
    m_preprocFmt.setForeground(c.preproc);
    m_headingFmt.setForeground(c.keyword);
    m_headingFmt.setFontWeight(QFont::Bold);
    m_keyFmt.setForeground(c.function);
    m_variableFmt.setForeground(c.type);
}

QString SyntaxHighlighter::normalize(const QString &word) const
{
    return m_lang.caseInsensitiveKeywords ? word.toLower() : word;
}

void SyntaxHighlighter::setLanguage(const LanguageDefinition &language)
{
    m_lang = language;
    auto toSet = [this](const QStringList &list) {
        QSet<QString> s;
        for (const QString &w : list)
            s.insert(normalize(w));
        return s;
    };
    m_keywords = toSet(m_lang.keywords);
    m_types = toSet(m_lang.types);
    m_constants = toSet(m_lang.constants);
    rehighlight();
}

int SyntaxHighlighter::variableLength(const QString &text, int i) const
{
    const int len = text.length();
    if (m_lang.variablePrefixes.isEmpty() || !m_lang.variablePrefixes.contains(text.at(i)) || i + 1 >= len)
        return 0;
    const QChar next = text.at(i + 1);
    if (next == QLatin1Char('{') || next == QLatin1Char('(')) {   // ${VAR} / $(VAR) en Makefile
        const QChar close = next == QLatin1Char('{') ? QLatin1Char('}') : QLatin1Char(')');
        const int end = text.indexOf(close, i + 2);
        return end < 0 ? len - i : end - i + 1;
    }
    if (next.isLetter() || next == QLatin1Char('_')) {
        int j = i + 2;
        while (j < len && isWordChar(text.at(j)))
            ++j;
        return j - i;
    }
    if (next.isDigit() || QStringView(u"#?@*!$-").contains(next))   // $1, $#, $?...
        return 2;
    return 0;
}

void SyntaxHighlighter::highlightVariables(const QString &text, int from, int to)
{
    for (int k = from; k < to; ++k) {
        if (text.at(k) == QLatin1Char('\\')) {
            ++k;
            continue;
        }
        if (const int n = variableLength(text, k); n > 0) {
            setFormat(k, qMin(n, to - k), m_variableFmt);
            k += n - 1;
        }
    }
}

bool SyntaxHighlighter::lineCommentAt(const QString &text, int i, int firstNonSpace) const
{
    const QStringView rest = QStringView(text).mid(i);
    if (!m_lang.lineComment.isEmpty() && rest.startsWith(m_lang.lineComment))
        return true;
    for (const QString &token : m_lang.extraLineComments) {
        if (!rest.startsWith(token, m_lang.caseInsensitiveKeywords ? Qt::CaseInsensitive : Qt::CaseSensitive))
            continue;
        // "REM" solo como palabra al principio de la línea; ";" o "#" en cualquier sitio
        if (!token.back().isLetter())
            return true;
        const int after = i + int(token.size());
        if (i == firstNonSpace && (after >= text.length() || !isWordChar(text.at(after))))
            return true;
    }
    return false;
}

void SyntaxHighlighter::highlightBlock(const QString &text)
{
    const int len = text.length();
    int i = 0;
    setCurrentBlockState(Normal);

    // --- Continuación de construcciones multilínea --------------------------
    const int prev = previousBlockState();
    if (prev == InBlockComment && !m_lang.blockCommentEnd.isEmpty()) {
        const int end = text.indexOf(m_lang.blockCommentEnd);
        if (end < 0) {
            setFormat(0, len, m_commentFmt);
            setCurrentBlockState(InBlockComment);
            return;
        }
        i = end + m_lang.blockCommentEnd.length();
        setFormat(0, i, m_commentFmt);
    } else if (prev >= InTripleString && prev - InTripleString < m_lang.tripleQuotes.size()) {
        const QString &quote = m_lang.tripleQuotes.at(prev - InTripleString);
        const int end = text.indexOf(quote);
        if (end < 0) {
            setFormat(0, len, m_stringFmt);
            setCurrentBlockState(prev);
            return;
        }
        i = end + quote.length();
        setFormat(0, i, m_stringFmt);
    }

    // --- Casos especiales por lenguaje --------------------------------------
    if (m_lang.name == QLatin1String("Markdown") && text.startsWith(QLatin1Char('#'))) {
        setFormat(0, len, m_headingFmt);
        return;
    }
    const bool isMarkup = m_lang.name.startsWith(QLatin1String("HTML"));

    int firstNonSpace = 0;
    while (firstNonSpace < len && text.at(firstNonSpace).isSpace())
        ++firstNonSpace;

    if (i == 0 && m_lang.sectionHeaders && firstNonSpace < len && text.at(firstNonSpace) == QLatin1Char('[')) {
        const int close = text.lastIndexOf(QLatin1Char(']'));
        if (close > firstNonSpace) {
            setFormat(firstNonSpace, close - firstNonSpace + 1, m_headingFmt);
            i = close + 1;
        }
    }
    // Clave sin comillas al principio de la línea: "name:", "- name:", "key =", "color:", "CC :="
    if (i == 0 && m_lang.keyValue) {
        static const QRegularExpression key(
            QStringLiteral(R"(^\s*(?:-\s+)?([A-Za-z_$\-][\w$.\-/ ]*?)\s*(?::(?=\s|$)|[:?+]?=))"));
        const QRegularExpressionMatch m = key.match(text);
        if (m.hasMatch() && !lineCommentAt(text, m.capturedStart(1), firstNonSpace)) {
            setFormat(m.capturedStart(1), m.capturedLength(1), m_keyFmt);
            i = m.capturedEnd(1);
        }
    }

    while (i < len) {
        const QChar ch = text.at(i);
        const QStringView rest = QStringView(text).mid(i);

        // Preprocesador (#include, #define...)
        if (m_lang.preprocessor && i == firstNonSpace && ch == QLatin1Char('#')) {
            int end = m_lang.lineComment.isEmpty() ? -1 : text.indexOf(m_lang.lineComment, i);
            if (end < 0)
                end = len;
            // Resalta también la cadena <...> o "..." de un #include
            setFormat(i, end - i, m_preprocFmt);
            const int lt = text.indexOf(QLatin1Char('<'), i);
            const int gt = lt >= 0 ? text.indexOf(QLatin1Char('>'), lt) : -1;
            if (lt >= 0 && gt > lt && gt < end && text.mid(i, lt - i).contains(QLatin1String("include")))
                setFormat(lt, gt - lt + 1, m_stringFmt);
            i = end;
            continue;
        }

        // Variables ($HOME, ${x}, $#): antes que los comentarios para que "$#" no lo parezca
        if (const int n = variableLength(text, i); n > 0) {
            setFormat(i, n, m_variableFmt);
            i += n;
            continue;
        }

        // Comentario de bloque (antes que el de línea: "--[[" en Lua empieza por "--")
        if (!m_lang.blockCommentStart.isEmpty() && rest.startsWith(m_lang.blockCommentStart)) {
            const int from = i + m_lang.blockCommentStart.length();
            const int end = text.indexOf(m_lang.blockCommentEnd, from);
            if (end < 0) {
                setFormat(i, len - i, m_commentFmt);
                setCurrentBlockState(InBlockComment);
                return;
            }
            const int stop = end + m_lang.blockCommentEnd.length();
            setFormat(i, stop - i, m_commentFmt);
            i = stop;
            continue;
        }

        // Comentario de línea
        if (lineCommentAt(text, i, firstNonSpace)) {
            setFormat(i, len - i, m_commentFmt);
            return;
        }

        // Cadenas multilínea (""" y ''' en Python, text blocks de Java...)
        bool triple = false;
        for (int k = 0; k < m_lang.tripleQuotes.size() && !triple; ++k) {
            const QString &quote = m_lang.tripleQuotes.at(k);
            if (!rest.startsWith(quote))
                continue;
            triple = true;
            const int from = i + quote.length();
            const int end = text.indexOf(quote, from);
            if (end < 0) {
                setFormat(i, len - i, m_stringFmt);
                setCurrentBlockState(InTripleString + k);
                return;
            }
            const int stop = end + quote.length();
            setFormat(i, stop - i, m_stringFmt);
            i = stop;
        }
        if (triple)
            continue;

        // Anotaciones y decoradores (@Override, @dataclass, @media)
        if (m_lang.annotations && ch == QLatin1Char('@') && i + 1 < len
            && (text.at(i + 1).isLetter() || text.at(i + 1) == QLatin1Char('_'))
            && (i == 0 || !isWordChar(text.at(i - 1)))) {
            int j = i + 1;
            while (j < len && (isWordChar(text.at(j)) || text.at(j) == QLatin1Char('.') || text.at(j) == QLatin1Char('-')))
                ++j;
            setFormat(i, j - i, m_preprocFmt);
            i = j;
            continue;
        }

        // Cadenas simples (una clave si va seguida de ':', como en JSON)
        if (m_lang.stringDelimiters.contains(ch)) {
            int j = i + 1;
            while (j < len && text.at(j) != ch) {
                if (text.at(j) == QLatin1Char('\\'))
                    ++j;
                ++j;
            }
            const int stop = qMin(j + 1, len);
            int after = stop;
            while (after < len && text.at(after).isSpace())
                ++after;
            const bool isKey = m_lang.keyValue && after < len && text.at(after) == QLatin1Char(':');
            setFormat(i, stop - i, isKey ? m_keyFmt : m_stringFmt);
            if (!isKey && ch != QLatin1Char('\''))
                highlightVariables(text, i + 1, stop);
            i = stop;
            continue;
        }

        // Etiquetas HTML/XML
        if (isMarkup && ch == QLatin1Char('<')) {
            int j = i + 1;
            if (j < len && (text.at(j) == QLatin1Char('/') || text.at(j) == QLatin1Char('?')))
                ++j;
            const int start = j;
            while (j < len && (isWordChar(text.at(j)) || text.at(j) == QLatin1Char('-')
                               || text.at(j) == QLatin1Char(':')))
                ++j;
            if (j > start)
                setFormat(start, j - start, m_keywordFmt);
            i = qMax(j, i + 1);
            continue;
        }

        // Números
        if (ch.isDigit() && (i == 0 || !isWordChar(text.at(i - 1)))) {
            int j = i + 1;
            while (j < len && (isWordChar(text.at(j)) || text.at(j) == QLatin1Char('.')
                               || text.at(j) == QLatin1Char('\'')))
                ++j;
            setFormat(i, j - i, m_numberFmt);
            i = j;
            continue;
        }

        // Identificadores
        if (ch.isLetter() || ch == QLatin1Char('_')) {
            int j = i + 1;
            while (j < len && isWordChar(text.at(j)))
                ++j;
            const QString word = normalize(text.mid(i, j - i));
            if (m_keywords.contains(word)) {
                setFormat(i, j - i, m_keywordFmt);
            } else if (m_types.contains(word)) {
                setFormat(i, j - i, m_typeFmt);
            } else if (m_constants.contains(word)) {
                setFormat(i, j - i, m_constantFmt);
            } else if (m_lang.highlightFunctions) {
                int k = j;
                while (k < len && text.at(k) == QLatin1Char(' '))
                    ++k;
                if (k < len && text.at(k) == QLatin1Char('('))
                    setFormat(i, j - i, m_functionFmt);
            }
            i = j;
            continue;
        }

        ++i;
    }
}
