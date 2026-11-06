@echo off
rem ------------------------------------------------------------------
rem  Genere MiniCalc.exe a partir de minicalc.c (double-clic suffit).
rem  Cherche d'abord le compilateur de Microsoft (Visual Studio ou
rem  Build Tools), a defaut MinGW (gcc) s'il est installe.
rem  Parametre facultatif "nopause" : ne pas attendre une touche a la fin.
rem ------------------------------------------------------------------
setlocal
cd /d "%~dp0"
set "PAUSE_CMD=pause"
if /i "%~1"=="nopause" set "PAUSE_CMD=rem"

set "VSDIR="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if defined VSDIR goto msvc

where gcc >nul 2>nul
if not errorlevel 1 goto mingw

echo Aucun compilateur C trouve.
echo Installez Visual Studio ou les Build Tools avec la charge de travail
echo "Developpement Desktop en C++", puis relancez ce fichier.
%PAUSE_CMD%
exit /b 1

:msvc
echo Compilation avec Visual C++ ...
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul
if errorlevel 1 goto erreur
rc /nologo minicalc.rc
if errorlevel 1 goto erreur
cl /nologo /O1 /MT /D_CRT_SECURE_NO_WARNINGS minicalc.c minicalc.res /Fe:MiniCalc.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib
if errorlevel 1 goto erreur
del minicalc.obj minicalc.res 2>nul
goto ok

:mingw
echo Compilation avec MinGW ...
windres minicalc.rc -O coff -o minicalc_res.o
if errorlevel 1 goto erreur
gcc -Os -s -static -mwindows minicalc.c minicalc_res.o -o MiniCalc.exe -lgdi32 -luser32 -lm
if errorlevel 1 goto erreur
del minicalc_res.o 2>nul
goto ok

:ok
echo.
echo REUSSI : MiniCalc.exe est dans %~dp0
%PAUSE_CMD%
exit /b 0

:erreur
echo.
echo ERREUR : lisez les messages ci-dessus.
%PAUSE_CMD%
exit /b 1
