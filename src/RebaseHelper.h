#pragma once

#include <QString>
#include <QStringList>

// Modo auxiliar sin interfaz que git invoca como editor durante un "git rebase -i"
// lanzado por BatCode:
//   BatCode --rebase-todo <reword|squash|drop|up|down> <sha> <git-rebase-todo>
//   BatCode --rebase-message <mensaje-nuevo> <COMMIT_EDITMSG>
namespace RebaseHelper {

// Devuelve el código de salida, o -1 si los argumentos no son de este modo.
int run(int argc, char *argv[]);

// Comando para GIT_SEQUENCE_EDITOR / GIT_EDITOR; git añade al final la ruta del archivo.
QString editorCommand(const QStringList &args);

} // namespace RebaseHelper
