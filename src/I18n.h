#pragma once

#include <QString>

class QCoreApplication;

// Idioma de la interfaz. Los textos del código fuente están en español; en inglés
// se traducen con una tabla compilada en el programa (I18nTable.cpp), así que no
// hacen falta archivos .qm ni las herramientas de Qt Linguist.
namespace I18n {

enum class Language { English, Spanish };

Language currentLanguage();          // el guardado en la configuración (por defecto, inglés)
void setLanguage(Language language);  // se aplica al reiniciar
QString displayName(Language language);

// Instala la traducción y el formato regional (fechas). Llamar una vez, antes de crear ventanas.
void install(QCoreApplication &app);

} // namespace I18n
