#include "DiffHighlighter.h"

#include "Theme.h"

DiffHighlighter::DiffHighlighter(QTextDocument *document)
    : QSyntaxHighlighter(document)
{
    const Theme::Colors &c = Theme::colors();
    m_added.setForeground(c.added);
    m_added.setBackground(c.addedBg);
    m_removed.setForeground(c.removed);
    m_removed.setBackground(c.removedBg);
    m_hunk.setForeground(c.hunk);
    m_header.setForeground(c.muted);
    m_header.setFontWeight(QFont::Bold);
}

void DiffHighlighter::highlightBlock(const QString &text)
{
    if (text.startsWith(QLatin1String("+++")) || text.startsWith(QLatin1String("---"))
        || text.startsWith(QLatin1String("diff ")) || text.startsWith(QLatin1String("index "))
        || text.startsWith(QLatin1String("new file")) || text.startsWith(QLatin1String("deleted file"))) {
        setFormat(0, text.length(), m_header);
    } else if (text.startsWith(QLatin1String("@@"))) {
        setFormat(0, text.length(), m_hunk);
    } else if (text.startsWith(QLatin1Char('+'))) {
        setFormat(0, text.length(), m_added);
    } else if (text.startsWith(QLatin1Char('-'))) {
        setFormat(0, text.length(), m_removed);
    }
}
