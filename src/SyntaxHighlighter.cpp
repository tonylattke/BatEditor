#include "SyntaxHighlighter.h"

#include "Theme.h"

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
    } else if (prev == InTripleString && !m_lang.tripleQuote.isEmpty()) {
        const int end = text.indexOf(m_lang.tripleQuote);
        if (end < 0) {
            setFormat(0, len, m_stringFmt);
            setCurrentBlockState(InTripleString);
            return;
        }
        i = end + m_lang.tripleQuote.length();
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

        // Comentario de línea
        if (!m_lang.lineComment.isEmpty() && rest.startsWith(m_lang.lineComment)) {
            setFormat(i, len - i, m_commentFmt);
            return;
        }

        // Comentario de bloque
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

        // Cadena triple (Python)
        if (!m_lang.tripleQuote.isEmpty() && rest.startsWith(m_lang.tripleQuote)) {
            const int from = i + m_lang.tripleQuote.length();
            const int end = text.indexOf(m_lang.tripleQuote, from);
            if (end < 0) {
                setFormat(i, len - i, m_stringFmt);
                setCurrentBlockState(InTripleString);
                return;
            }
            const int stop = end + m_lang.tripleQuote.length();
            setFormat(i, stop - i, m_stringFmt);
            i = stop;
            continue;
        }

        // Cadenas simples
        if (m_lang.stringDelimiters.contains(ch)) {
            int j = i + 1;
            while (j < len && text.at(j) != ch) {
                if (text.at(j) == QLatin1Char('\\'))
                    ++j;
                ++j;
            }
            const int stop = qMin(j + 1, len);
            setFormat(i, stop - i, m_stringFmt);
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
