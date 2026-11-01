#pragma once

#include <QDialog>
#include <QStringList>

class QLineEdit;
class QListWidget;

// "Ir a archivo" (Ctrl+P): búsqueda difusa sobre los archivos de la carpeta abierta.
class QuickOpenDialog : public QDialog
{
    Q_OBJECT
public:
    explicit QuickOpenDialog(const QString &rootPath, QWidget *parent = nullptr);

    QString selectedFile() const { return m_selected; }

    // Puntuación difusa: <0 si no coincide; mayor es mejor.
    static int fuzzyScore(const QString &pattern, const QString &candidate);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void scanFiles();
    void refilter();
    void accept() override;

    QString m_root;
    QStringList m_files; // rutas relativas a m_root
    QString m_selected;
    QLineEdit *m_input;
    QListWidget *m_list;
};
