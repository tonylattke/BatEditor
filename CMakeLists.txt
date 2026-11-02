cmake_minimum_required(VERSION 3.21)

project(BatCode
    VERSION 0.2.0
    DESCRIPTION "Editor de texto con Git integrado"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de compilación" FORCE)
endif()

# --- Qt 6 -------------------------------------------------------------------
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets)

# --- libgit2 ----------------------------------------------------------------
# 1) vcpkg (Windows/Linux)  2) pkg-config (Linux)  3) búsqueda manual
set(BATCODE_GIT_TARGET "")

find_package(unofficial-libgit2 CONFIG QUIET)
if(TARGET unofficial::libgit2::libgit2)
    set(BATCODE_GIT_TARGET unofficial::libgit2::libgit2)
endif()

if(NOT BATCODE_GIT_TARGET)
    find_package(libgit2 CONFIG QUIET)
    if(TARGET libgit2::libgit2package)
        set(BATCODE_GIT_TARGET libgit2::libgit2package)
    elseif(TARGET libgit2::libgit2)
        set(BATCODE_GIT_TARGET libgit2::libgit2)
    endif()
endif()

if(NOT BATCODE_GIT_TARGET)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBGIT2 QUIET IMPORTED_TARGET libgit2)
        if(LIBGIT2_FOUND)
            set(BATCODE_GIT_TARGET PkgConfig::LIBGIT2)
        endif()
    endif()
endif()

if(NOT BATCODE_GIT_TARGET)
    find_path(LIBGIT2_INCLUDE_DIR git2.h)
    find_library(LIBGIT2_LIBRARY NAMES git2 libgit2)
    if(LIBGIT2_INCLUDE_DIR AND LIBGIT2_LIBRARY)
        add_library(batcode_libgit2 UNKNOWN IMPORTED)
        set_target_properties(batcode_libgit2 PROPERTIES
            IMPORTED_LOCATION "${LIBGIT2_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LIBGIT2_INCLUDE_DIR}")
        set(BATCODE_GIT_TARGET batcode_libgit2)
    endif()
endif()

if(NOT BATCODE_GIT_TARGET)
    message(FATAL_ERROR
        "No se encontró libgit2.\n"
        "  Linux:   sudo apt install libgit2-dev   (o el paquete de tu distribución)\n"
        "  Windows: vcpkg install libgit2  y configura con -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake")
endif()
message(STATUS "libgit2: ${BATCODE_GIT_TARGET}")

# --- Ejecutable -------------------------------------------------------------
set(BATCODE_SOURCES
    src/main.cpp
    src/Theme.h                src/Theme.cpp
    src/LanguageDefinitions.h  src/LanguageDefinitions.cpp
    src/SyntaxHighlighter.h    src/SyntaxHighlighter.cpp
    src/DiffHighlighter.h      src/DiffHighlighter.cpp
    src/CodeEditor.h           src/CodeEditor.cpp
    src/Minimap.h              src/Minimap.cpp
    src/EditorTab.h            src/EditorTab.cpp
    src/FindBar.h              src/FindBar.cpp
    src/QuickOpenDialog.h      src/QuickOpenDialog.cpp
    src/GitRepository.h        src/GitRepository.cpp
    src/FileDiffWidget.h       src/FileDiffWidget.cpp
    src/RepositoryView.h       src/RepositoryView.cpp
    src/RepositoryHome.h       src/RepositoryHome.cpp
    src/GitWorkspace.h         src/GitWorkspace.cpp
    src/GitCommandPalette.h    src/GitCommandPalette.cpp
    src/FindInFiles.h          src/FindInFiles.cpp
    src/FindInFilesPanel.h     src/FindInFilesPanel.cpp
    src/I18n.h                 src/I18n.cpp               src/I18nTable.cpp
    src/PreferencesDialog.h    src/PreferencesDialog.cpp
    src/ModeSwitch.h           src/ModeSwitch.cpp
    src/MainWindow.h           src/MainWindow.cpp
)

add_executable(BatCode ${BATCODE_SOURCES})

set_target_properties(BatCode PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON)

target_compile_definitions(BatCode PRIVATE
    BATCODE_VERSION="${PROJECT_VERSION}"
    QT_NO_KEYWORDS)

target_link_libraries(BatCode PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets
    ${BATCODE_GIT_TARGET})

if(MSVC)
    target_compile_options(BatCode PRIVATE /W4 /utf-8 /permissive-)
else()
    target_compile_options(BatCode PRIVATE -Wall -Wextra)
endif()

install(TARGETS BatCode
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .)
