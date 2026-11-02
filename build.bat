@echo off
rem ===========================================================================
rem  BatCode - compilacion en Windows con Visual Studio 2022
rem
rem  Uso:   build.bat [Release^|Debug] [--clean] [--no-pause]
rem
rem  Lo que hace (solo la primera vez descarga cosas):
rem    1. Carga el entorno de Visual Studio (MSVC x64) con vswhere
rem    2. Prepara vcpkg en .deps\vcpkg y compila libgit2
rem    3. Busca Qt 6 (variable QT_DIR, C:\Qt\6.x\msvc2022_64 o .deps\Qt);
rem       si no lo encuentra, lo descarga con aqtinstall en .deps\Qt
rem    4. Configura y compila con CMake + Ninja en build\windows
rem    5. Copia las DLL de Qt junto al ejecutable (windeployqt)
rem
rem  Variables opcionales:
rem    QT_DIR       carpeta de Qt para MSVC (p. ej. C:\Qt\6.8.3\msvc2022_64)
rem    QT_VERSION   version de Qt a descargar si no hay ninguna (por defecto 6.8.3)
rem ===========================================================================
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
cd /d "%ROOT%"

set "CONFIG=Release"
set "CLEAN=0"
set "PAUSE_AT_END=0"
rem Si se abrio con doble clic (cmd /c ...), espera una tecla al final para poder leer el resultado
set "BATCODE_CMDLINE=%cmdcmdline%"
if /i not "!BATCODE_CMDLINE:/c =!"=="!BATCODE_CMDLINE!" set "PAUSE_AT_END=1"
for %%A in (%*) do (
    if /i "%%~A"=="Debug"      set "CONFIG=Debug"
    if /i "%%~A"=="Release"    set "CONFIG=Release"
    if /i "%%~A"=="--clean"    set "CLEAN=1"
    if /i "%%~A"=="--no-pause" set "PAUSE_AT_END=0"
)

set "DEPS=%ROOT%\.deps"
set "BUILD_DIR=%ROOT%\build\windows"
if not defined QT_VERSION set "QT_VERSION=6.8.3"
set "QT_ARCH=win64_msvc2022_64"
set "QT_SUBDIR=msvc2022_64"

echo.
echo ==== BatCode: compilacion %CONFIG% para Windows ====
echo.

rem ---------------------------------------------------------------------------
rem 1) Visual Studio
rem ---------------------------------------------------------------------------
echo [1/5] Buscando Visual Studio...
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: no se encontro vswhere.exe. Instala Visual Studio 2022 con
    echo        la carga de trabajo "Desarrollo para el escritorio con C++".
    goto :fail
)
set "VSINSTALL="
set "VSTMP=%TEMP%\batcode_vswhere.txt"
rem Primero Visual Studio 2022; si no esta, la version mas reciente con MSVC
"%VSWHERE%" -version [17.0^,18.0^) -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath -latest > "%VSTMP%" 2>nul
set /p VSINSTALL=<"%VSTMP%"
if not defined VSINSTALL (
    "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%VSTMP%" 2>nul
    set /p VSINSTALL=<"%VSTMP%"
)
del "%VSTMP%" >nul 2>&1
if not defined VSINSTALL (
    echo ERROR: Visual Studio no tiene instalado el compilador de C++ ^(MSVC x64^).
    echo        Abre "Visual Studio Installer" y anade "Desarrollo para el escritorio con C++".
    goto :fail
)
echo       %VSINSTALL%
call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 -no_logo
if not "%ERRORLEVEL%"=="0" (
    echo ERROR: no se pudo cargar el entorno de Visual Studio.
    goto :fail
)
where cl >nul 2>&1 || (
    echo ERROR: cl.exe no esta disponible tras cargar Visual Studio.
    goto :fail
)
rem vcpkg debe usar el mismo Visual Studio (si hay varios instalados, elegiria el mas nuevo)
set "VCPKG_VISUAL_STUDIO_PATH=%VSINSTALL%"

rem ---------------------------------------------------------------------------
rem 2) vcpkg + libgit2
rem ---------------------------------------------------------------------------
echo.
echo [2/5] Preparando vcpkg...
if not exist "%DEPS%" mkdir "%DEPS%"
rem Se usa una copia propia de vcpkg (la integrada en Visual Studio exige una "baseline")
set "VCPKG_ROOT=%DEPS%\vcpkg"
if not exist "%VCPKG_ROOT%\.git" (
    where git >nul 2>&1 || (
        echo ERROR: se necesita Git para descargar vcpkg. Instalalo desde https://git-scm.com
        goto :fail
    )
    echo       Clonando vcpkg en %VCPKG_ROOT% ...
    git clone --depth 1 https://github.com/microsoft/vcpkg.git "%VCPKG_ROOT%"
    if errorlevel 1 goto :fail
)
if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    call "%VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics
    if not exist "%VCPKG_ROOT%\vcpkg.exe" (
        echo ERROR: no se pudo preparar vcpkg.
        goto :fail
    )
)
set "VCPKG_DISABLE_METRICS=1"

rem CMake y Ninja: los de Visual Studio; si faltan, los descarga vcpkg
where cmake >nul 2>&1 || (
    echo       CMake no esta en Visual Studio; descargandolo con vcpkg...
    for /f "delims=" %%P in ('"%VCPKG_ROOT%\vcpkg.exe" fetch cmake') do set "CMAKE_EXE=%%P"
    for %%D in ("!CMAKE_EXE!") do set "PATH=%%~dpD;!PATH!"
)
where ninja >nul 2>&1 || (
    echo       Ninja no esta en Visual Studio; descargandolo con vcpkg...
    for /f "delims=" %%P in ('"%VCPKG_ROOT%\vcpkg.exe" fetch ninja') do set "NINJA_EXE=%%P"
    for %%D in ("!NINJA_EXE!") do set "PATH=%%~dpD;!PATH!"
)
where cmake >nul 2>&1 || ( echo ERROR: CMake no disponible. & goto :fail )
where ninja >nul 2>&1 || ( echo ERROR: Ninja no disponible. & goto :fail )
set "NINJA_PATH="
for /f "delims=" %%N in ('where ninja 2^>nul') do if not defined NINJA_PATH set "NINJA_PATH=%%N"

rem ---------------------------------------------------------------------------
rem 3) Qt 6
rem ---------------------------------------------------------------------------
echo.
echo [3/5] Buscando Qt 6...
set "QTDIR_FOUND="
if defined QT_DIR if exist "%QT_DIR%\bin\qmake.exe" set "QTDIR_FOUND=%QT_DIR%"
if not defined QTDIR_FOUND if exist "%DEPS%\Qt\%QT_VERSION%\%QT_SUBDIR%\bin\qmake.exe" set "QTDIR_FOUND=%DEPS%\Qt\%QT_VERSION%\%QT_SUBDIR%"
if not defined QTDIR_FOUND if exist "C:\Qt" (
    rem La version mas alta de C:\Qt\6.* con compilacion MSVC 2022 o 2019
    for /f "delims=" %%V in ('dir /b /ad /o-n "C:\Qt\6.*" 2^>nul') do (
        if not defined QTDIR_FOUND if exist "C:\Qt\%%V\msvc2022_64\bin\qmake.exe" set "QTDIR_FOUND=C:\Qt\%%V\msvc2022_64"
        if not defined QTDIR_FOUND if exist "C:\Qt\%%V\msvc2019_64\bin\qmake.exe" set "QTDIR_FOUND=C:\Qt\%%V\msvc2019_64"
    )
)
if not defined QTDIR_FOUND (
    echo       No se encontro Qt. Descargando Qt %QT_VERSION% ^(%QT_ARCH%^) en %DEPS%\Qt
    echo       con aqtinstall. Son unos 300 MB y solo se hace una vez...
    if not exist "%DEPS%\aqt.exe" (
        powershell -NoProfile -ExecutionPolicy Bypass -Command "$ProgressPreference='SilentlyContinue'; [Net.ServicePointManager]::SecurityProtocol='Tls12'; try { Invoke-WebRequest -UseBasicParsing -Uri 'https://github.com/miurahr/aqtinstall/releases/latest/download/aqt.exe' -OutFile '%DEPS%\aqt.exe' } catch { Invoke-WebRequest -UseBasicParsing -Uri 'https://github.com/miurahr/aqtinstall/releases/download/Continuous/aqt.exe' -OutFile '%DEPS%\aqt.exe' }"
    )
    if not exist "%DEPS%\aqt.exe" (
        echo ERROR: no se pudo descargar aqt.exe. Instala Qt manualmente desde
        echo        https://www.qt.io/download-qt-installer ^(componente MSVC 2022 64-bit^)
        echo        y vuelve a ejecutar:  set QT_DIR=C:\Qt\6.x.x\msvc2022_64  ^&^&  build.bat
        goto :fail
    )
    "%DEPS%\aqt.exe" install-qt windows desktop %QT_VERSION% %QT_ARCH% --outputdir "%DEPS%\Qt"
    if exist "%DEPS%\Qt\%QT_VERSION%\%QT_SUBDIR%\bin\qmake.exe" set "QTDIR_FOUND=%DEPS%\Qt\%QT_VERSION%\%QT_SUBDIR%"
)
if not defined QTDIR_FOUND (
    echo ERROR: no se pudo obtener Qt 6.
    goto :fail
)
echo       %QTDIR_FOUND%

rem ---------------------------------------------------------------------------
rem 4) CMake: configurar y compilar
rem ---------------------------------------------------------------------------
echo.
echo [4/5] Compilando...
rem Windows no deja sobrescribir un .exe en uso: si BatCode esta abierto se le pide
rem que se cierre de forma normal (como al pulsar la X; pregunta si hay cambios sin guardar)
tasklist /FI "IMAGENAME eq BatCode.exe" /NH 2>nul | find /I "BatCode.exe" >nul
if not errorlevel 1 (
    echo       BatCode esta abierto: cerrandolo para poder compilar...
    taskkill /IM BatCode.exe >nul 2>&1
    for /L %%I in (1,1,15) do (
        tasklist /FI "IMAGENAME eq BatCode.exe" /NH 2>nul | find /I "BatCode.exe" >nul && ping -n 2 127.0.0.1 >nul
    )
    tasklist /FI "IMAGENAME eq BatCode.exe" /NH 2>nul | find /I "BatCode.exe" >nul
    if not errorlevel 1 (
        echo ERROR: BatCode sigue abierto. Cierralo ^(Archivo ^> Salir^) y vuelve a compilar.
        goto :fail
    )
)
if "%CLEAN%"=="1" if exist "%BUILD_DIR%" (
    echo       Limpiando %BUILD_DIR%
    rmdir /s /q "%BUILD_DIR%"
)
rem Una configuracion anterior fallida deja una cache inservible: se descarta
if exist "%BUILD_DIR%\CMakeCache.txt" if not exist "%BUILD_DIR%\build.ninja" (
    echo       Descartando una configuracion anterior incompleta...
    del /q "%BUILD_DIR%\CMakeCache.txt" >nul 2>&1
    if exist "%BUILD_DIR%\CMakeFiles" rmdir /s /q "%BUILD_DIR%\CMakeFiles"
)
cmake -S "%ROOT%" -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_C_COMPILER=cl ^
    -DCMAKE_CXX_COMPILER=cl ^
    -DCMAKE_BUILD_TYPE=%CONFIG% ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows ^
    -DVCPKG_INSTALLED_DIR="%DEPS%\vcpkg_installed" ^
    -DCMAKE_PREFIX_PATH="%QTDIR_FOUND%" ^
    -DCMAKE_MAKE_PROGRAM="%NINJA_PATH%"
if not "%ERRORLEVEL%"=="0" (
    echo ERROR: fallo la configuracion de CMake.
    goto :fail
)
cmake --build "%BUILD_DIR%" --parallel
if not "%ERRORLEVEL%"=="0" (
    echo ERROR: fallo la compilacion.
    goto :fail
)

rem ---------------------------------------------------------------------------
rem 5) DLL de Qt junto al ejecutable
rem ---------------------------------------------------------------------------
echo.
echo [5/5] Copiando las bibliotecas de Qt...
set "DEPLOY_MODE=--release"
if /i "%CONFIG%"=="Debug" set "DEPLOY_MODE=--debug"
"%QTDIR_FOUND%\bin\windeployqt.exe" %DEPLOY_MODE% --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime "%BUILD_DIR%\BatCode.exe" >nul
if not "%ERRORLEVEL%"=="0" (
    echo ERROR: windeployqt fallo.
    goto :fail
)

echo.
echo ==== Listo: %BUILD_DIR%\BatCode.exe ====
echo      Ejecutalo con run.bat
if "%PAUSE_AT_END%"=="1" pause
endlocal & exit /b 0

:fail
echo.
echo ==== La compilacion NO se completo. Revisa los mensajes de arriba. ====
if "%PAUSE_AT_END%"=="1" pause
endlocal & exit /b 1
