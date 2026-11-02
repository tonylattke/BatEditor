#!/usr/bin/env bash
# =============================================================================
#  BatCode - ejecutar en Linux
#  Uso:   ./run.sh [archivos o carpeta a abrir]
#  Compila primero si el ejecutable no existe o si el código cambió.
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EXE="$ROOT/build/linux/BatCode"

needs_build=0
if [[ ! -x "$EXE" ]]; then
    needs_build=1
elif [[ -n "$(find "$ROOT/src" "$ROOT/CMakeLists.txt" -newer "$EXE" -print -quit 2>/dev/null)" ]]; then
    needs_build=1
fi

if ((needs_build)); then
    bash "$ROOT/build.sh"
fi

exec "$EXE" "$@"
