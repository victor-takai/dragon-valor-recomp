@echo off
REM Dragon Valor (USA) Recompiled - player launch (release build).
REM   Play.bat         -> build-release\DragonValorUS_Recompiled.exe
REM   Play.bat -dbg    -> build-debug\  (PSX_DEBUG_TOOLS=ON, TCP debug server on 127.0.0.1:4370)
REM Rebuild: python psxrecomp\psxrecomp_cli.py rebuild --config game.toml --project-root . --build-dir build-release --no-pgo
setlocal
set "EXE=DragonValorUS_Recompiled.exe"
set "GAMEDIR=%~dp0build-release"
if /I "%~1"=="-dbg" set "GAMEDIR=%~dp0build-debug"
if not exist "%GAMEDIR%\%EXE%" (
    echo %EXE% nao encontrado em "%GAMEDIR%". Rode o rebuild indicado no topo deste script.
    pause
    exit /b 1
)
REM The runtime resolves saves, settings, bios/ and keybinds.ini relative to the
REM exe directory, so start there; the exe is still invoked by full path because
REM cmd does not search the current directory when NoDefaultCurrentDirectoryInExePath is set.
cd /d "%GAMEDIR%"
"%GAMEDIR%\%EXE%"
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
    echo Saiu com codigo %RC%.
    pause
)
exit /b %RC%
