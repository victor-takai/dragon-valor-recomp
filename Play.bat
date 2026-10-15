@echo off
REM Dragon Valor (USA) Recompiled - player launch (release build).
REM   Play.bat [args]        -> build-release\DragonValorUS_Recompiled.exe [args]
REM   Play.bat -dbg [args]   -> build-debug\  (PSX_DEBUG_TOOLS=ON, TCP debug server on 127.0.0.1:4370)
REM Build: docs\DEVELOPMENT.md, "Build".
setlocal
set "EXE=DragonValorUS_Recompiled.exe"
set "GAMEDIR=%~dp0build-release"
if /I "%~1"=="-dbg" (
    set "GAMEDIR=%~dp0build-debug"
    shift
)
REM Remaining arguments go to the exe (e.g. --no-launcher); %* would still
REM include -dbg, so collect them by hand.
set "ARGS="
:collect
if "%~1"=="" goto run
set ARGS=%ARGS% %1
shift
goto collect
:run
if not exist "%GAMEDIR%\%EXE%" (
    echo %EXE% not found in "%GAMEDIR%". Build it first: docs\DEVELOPMENT.md, "Build".
    pause
    exit /b 1
)
REM The runtime resolves saves, settings, bios/ and keybinds.ini relative to the
REM exe directory, so start there; the exe is still invoked by full path because
REM cmd does not search the current directory when NoDefaultCurrentDirectoryInExePath is set.
cd /d "%GAMEDIR%"
"%GAMEDIR%\%EXE%"%ARGS%
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" if not defined PLAY_NOPAUSE (
    echo Exited with code %RC%.
    pause
)
exit /b %RC%
