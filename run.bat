@echo off
rem ===========================================================================
rem  BatCode - ejecutar en Windows
rem  Uso:   run.bat [archivos o carpeta a abrir]
rem  Compila primero (con build.bat) si el ejecutable no existe o si el codigo
rem  fuente cambio desde la ultima compilacion.
rem ===========================================================================
setlocal EnableExtensions
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "EXE=%ROOT%\build\windows\BatCode.exe"

set "NEEDS_BUILD=0"
if not exist "%EXE%" (
    set "NEEDS_BUILD=1"
) else (
    rem Hay algun archivo en src\, CMakeLists.txt o vcpkg.json mas nuevo que el .exe?
    powershell -NoProfile -ExecutionPolicy Bypass -Command "$exe = (Get-Item -LiteralPath '%EXE%').LastWriteTimeUtc; $files = @(Get-ChildItem -LiteralPath '%ROOT%\src' -Recurse -File) + @(Get-Item -LiteralPath '%ROOT%\CMakeLists.txt', '%ROOT%\vcpkg.json'); if ($files | Where-Object { $_.LastWriteTimeUtc -gt $exe }) { exit 1 } else { exit 0 }"
    if errorlevel 1 set "NEEDS_BUILD=1"
)

if "%NEEDS_BUILD%"=="1" (
    echo Compilando BatCode con build.bat...
    call "%ROOT%\build.bat" --no-pause
    if errorlevel 1 (
        echo No se pudo compilar BatCode.
        pause
        exit /b 1
    )
)

start "" "%EXE%" %*
endlocal
