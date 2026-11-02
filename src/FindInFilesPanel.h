#pragma once

#include "FindInFiles.h"

#include <QPointer>
#include <QThread>
#include <QWidget>

#include <functional>
#include <memory>

class QComboBox;
class QLabel;
class QPushButton;

class QToolButton;

// Panel inferior "Buscar en archivos" (Ctrl+Shift+F): Buscar / Donde / Reemplazar.
class FindInFilesPanel : public QWidget
{
    Q_OBJECT
public:
    explicit FindInFilesPanel(QWidget *parent = nullptr);
    ~FindInFilesPanel() override;

    // Qué hay abierto en el editor (carpetas, archivos y su contenido actual)
    void setWorkspaceProvider(std::function<FindInFiles::Workspace()> provider) { m_provider = std::move(provider); }

    void activate(const QString &initialText = QString());
    bool resultsInNewTab() const;
    bool isRunning() const { return m_thread != nullptr; }

public Q_SLOTS:
    void find();
    void replace();
    void cancel();

Q_SIGNALS:
    void searchFinished(const FindInFiles::Results &results);
    void replaceSearchFinished(const FindInFiles::Results &results);  // falta confirmar y aplicar
    void closeRequested();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    FindInFiles::Options currentOptions() const;
    void start(bool replace);
    void finished(const FindInFiles::Results &results, bool replace);
    void remember(QComboBox *combo, const QString &key);
    void loadSettings();
    void saveToggles() const;
    void showWhereMenu();
    void appendWhere(const QString &entry);

    std::function<FindInFiles::Workspace()> m_provider;
    QToolButton *m_regex, *m_case, *m_word, *m_context, *m_newTab, *m_gitignore, *m_preserveCase;
    QComboBox *m_find, *m_where, *m_replace;
    QPushButton *m_findButton, *m_replaceButton;
    QToolButton *m_whereButton;
    QLabel *m_status;

    QPointer<QThread> m_thread;
    std::shared_ptr<std::atomic_bool> m_cancel;
};
