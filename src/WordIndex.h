#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>
#include <utility>
#include <vector>

#include <QList>
#include <QPointer>
#include <QThread>

// Índice de palabras (identificadores) del proyecto abierto para el
// autocompletado. Se construye en segundo plano recorriendo la carpeta
// (sin .git, build, node_modules ni archivos binarios o enormes).
class WordIndex : public QObject
{
    Q_OBJECT
public:
    explicit WordIndex(QObject *parent = nullptr);
    ~WordIndex() override;

    void setFolder(const QString &folder);   // vuelve a indexar en segundo plano
    void addText(const QString &text);       // añade las palabras de un texto (p. ej. al cerrar una pestaña)

    // Palabras que empiezan por `prefix` (sin distinguir mayúsculas), en orden alfabético
    QStringList wordsWithPrefix(const QString &prefix, int limit = 200) const;
    int size() const { return int(m_words.size()); }
    bool isScanning() const { return m_thread != nullptr; }

    // Extrae las palabras de un texto (letras, dígitos y '_', mínimo 3 caracteres)
    static QStringList extractWords(const QString &text);

Q_SIGNALS:
    void indexed(int words);

private:
    using Entry = std::pair<QString, QString>;   // (en minúsculas, palabra)
    void setWords(const QStringList &words);

    std::vector<Entry> m_words;                  // ordenado por la clave en minúsculas
    QThread *m_thread = nullptr;                // exploración actual
    QList<QPointer<QThread>> m_threads;         // todas las que siguen vivas (se esperan al destruir)
    std::shared_ptr<std::atomic_bool> m_cancel;
    int m_generation = 0;
};
