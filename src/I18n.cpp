#include "I18n.h"

#include <QCoreApplication>
#include <QHash>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

// Tablas definidas en I18nTable.cpp
extern const char *const kSpanishToEnglish[][2];
extern const int kSpanishToEnglishCount;
extern const char *const kQtToSpanish[][2];
extern const int kQtToSpanishCount;

namespace {

const char *kLanguageKey = "ui/language";

// Traductor que busca por el texto original, sin importar el contexto.
class TableTranslator : public QTranslator
{
public:
    TableTranslator(const char *const table[][2], int count, QObject *parent)
        : QTranslator(parent)
    {
        m_map.reserve(count);
        for (int i = 0; i < count; ++i)
            m_map.insert(QString::fromUtf8(table[i][0]), QString::fromUtf8(table[i][1]));
    }

    QString translate(const char *, const char *sourceText, const char *, int) const override
    {
        if (!sourceText)
            return QString();
        return m_map.value(QString::fromUtf8(sourceText));
    }

    bool isEmpty() const override { return m_map.isEmpty(); }

private:
    QHash<QString, QString> m_map;
};

} // namespace

namespace I18n {

Language currentLanguage()
{
    const QString code = QSettings().value(QLatin1String(kLanguageKey), QStringLiteral("en")).toString();
    return code == QLatin1String("es") ? Language::Spanish : Language::English;
}

void setLanguage(Language language)
{
    QSettings().setValue(QLatin1String(kLanguageKey),
                         language == Language::Spanish ? QStringLiteral("es") : QStringLiteral("en"));
}

QString displayName(Language language)
{
    // Cada idioma con su propio nombre, para reconocerlo aunque no se entienda el otro
    return language == Language::Spanish ? QStringLiteral("Español") : QStringLiteral("English");
}

void install(QCoreApplication &app)
{
    if (currentLanguage() == Language::English) {
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
        app.installTranslator(new TableTranslator(kSpanishToEnglish, kSpanishToEnglishCount, &app));
    } else {
        QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Spain));
        // Los textos propios de Qt (botones de diálogos, menú contextual de edición) vienen en inglés
        app.installTranslator(new TableTranslator(kQtToSpanish, kQtToSpanishCount, &app));
    }
}

} // namespace I18n
