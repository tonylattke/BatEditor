#!/usr/bin/env bash
# =============================================================================
#  BatCode - compilación en Linux
#
#  Uso:   ./build.sh [release|debug] [--clean] [--install-deps]
#
#    --install-deps   instala con sudo las dependencias que falten
#                     (apt, dnf, pacman o zypper)
#    --clean          borra build/linux antes de compilar
#
#  Variables opcionales:
#    QT_DIR   carpeta de una instalación de Qt 6 (p. ej. ~/Qt/6.8.3/gcc_64)
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT/build/linux"
CONFIG=Release
CLEAN=0
INSTALL_DEPS=0

for arg in "$@"; do
    case "${arg,,}" in
        release) CONFIG=Release ;;
        debug) CONFIG=Debug ;;
        --clean) CLEAN=1 ;;
        --install-deps) INSTALL_DEPS=1 ;;
        -h|--help) sed -n '2,14p' "$0"; exit 0 ;;
        *) echo "Argumento desconocido: $arg" >&2; exit 2 ;;
    esac
done

bold() { printf '\033[1m%s\033[0m\n' "$*"; }
err()  { printf '\033[31mERROR:\033[0m %s\n' "$*" >&2; }
have() { command -v "$1" >/dev/null 2>&1; }

bold "==== BatCode: compilación $CONFIG para Linux ===="

# -----------------------------------------------------------------------------
# 1) Dependencias
# -----------------------------------------------------------------------------
bold "[1/3] Comprobando dependencias..."

has_qt6() {
    [[ -n "${QT_DIR:-}" && -d "${QT_DIR}/lib/cmake/Qt6" ]] && return 0
    have pkg-config && pkg-config --exists Qt6Widgets 2>/dev/null && return 0
    for d in /usr/lib/cmake/Qt6 /usr/lib/*/cmake/Qt6 /usr/lib64/cmake/Qt6 /usr/local/lib/cmake/Qt6; do
        [[ -f "$d/Qt6Config.cmake" ]] && [[ -d "$(dirname "$d")/Qt6Widgets" ]] && return 0
    done
    return 1
}
has_libgit2() {
    have pkg-config && pkg-config --exists libgit2 2>/dev/null && return 0
    [[ -f /usr/include/git2.h || -f /usr/local/include/git2.h ]] && return 0
    return 1
}

missing=()
have cmake || missing+=(cmake)
{ have c++ || have g++ || have clang++; } || missing+=(compilador)
has_qt6 || missing+=(qt6)
has_libgit2 || missing+=(libgit2)

if ((${#missing[@]})); then
    pm=""
    if have apt-get; then
        pm=apt
        pkgs=(build-essential cmake ninja-build pkg-config qt6-base-dev libgit2-dev libgl-dev)
        cmd=(sudo apt-get install -y "${pkgs[@]}")
        pre=(sudo apt-get update)
    elif have dnf; then
        pm=dnf
        cmd=(sudo dnf install -y gcc-c++ cmake ninja-build pkgconf-pkg-config qt6-qtbase-devel libgit2-devel)
        pre=()
    elif have pacman; then
        pm=pacman
        cmd=(sudo pacman -S --needed --noconfirm base-devel cmake ninja pkgconf qt6-base libgit2)
        pre=()
    elif have zypper; then
        pm=zypper
        cmd=(sudo zypper install -y gcc-c++ cmake ninja pkg-config qt6-base-devel libgit2-devel)
        pre=()
    fi

    err "faltan dependencias: ${missing[*]}"
    if [[ -z "$pm" ]]; then
        echo "Instala con el gestor de paquetes de tu distribución: CMake, un compilador C++17,"
        echo "Qt 6 (módulo Widgets, paquete de desarrollo) y libgit2 (paquete de desarrollo)."
        exit 1
    fi
    if ((INSTALL_DEPS)); then
        bold "Instalando dependencias con $pm..."
        ((${#pre[@]})) && "${pre[@]}"
        "${cmd[@]}"
        missing=()
        has_qt6 || missing+=(qt6)
        has_libgit2 || missing+=(libgit2)
        if ((${#missing[@]})); then
            err "siguen faltando: ${missing[*]}"
            exit 1
        fi
    else
        echo "Instálalas con:"
        echo "    ${cmd[*]}"
        echo "o ejecuta:  ./build.sh --install-deps"
        exit 1
    fi
fi
echo "      CMake $(cmake --version | head -n1 | awk '{print $3}')"

# -----------------------------------------------------------------------------
# 2) Configurar
# -----------------------------------------------------------------------------
bold "[2/3] Configurando..."
if ((CLEAN)) && [[ -d "$BUILD_DIR" ]]; then
    echo "      Limpiando $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

generator=()
if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    if have ninja; then generator=(-G Ninja); else generator=(-G "Unix Makefiles"); fi
fi
cmake_args=(-S "$ROOT" -B "$BUILD_DIR" "${generator[@]}" -DCMAKE_BUILD_TYPE="$CONFIG")
[[ -n "${QT_DIR:-}" ]] && cmake_args+=(-DCMAKE_PREFIX_PATH="$QT_DIR")
cmake "${cmake_args[@]}"

# -----------------------------------------------------------------------------
# 3) Compilar
# -----------------------------------------------------------------------------
bold "[3/3] Compilando..."
jobs="$(nproc 2>/dev/null || echo 2)"
cmake --build "$BUILD_DIR" --parallel "$jobs"

bold "==== Listo: $BUILD_DIR/BatCode ===="
echo "     Ejecútalo con ./run.sh"
