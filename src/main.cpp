#include "I18n.h"
#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("BatCode"));
    QApplication::setOrganizationName(QStringLiteral("BatCode"));
    QApplication::setApplicationVersion(QStringLiteral(BATCODE_VERSION));
    I18n::install(app); // inglés por defecto; español desde Archivo › Preferencias

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main", "BatCode: editor de texto con Git integrado"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QCoreApplication::translate("main", "rutas"),
                                 QCoreApplication::translate("main", "Archivos o carpeta a abrir."),
                                 QStringLiteral("[rutas...]"));
    parser.process(app);

    Theme::apply(app);

    MainWindow window;
    // La ventana se muestra antes de abrir nada: si una ruta falla, el aviso
    // aparece sobre ella en lugar de quedar como un diálogo suelto
    window.show();
    const QStringList paths = parser.positionalArguments();
    if (paths.isEmpty())
        window.restoreSession();
    else
        window.openPaths(paths);

    return app.exec();
}
