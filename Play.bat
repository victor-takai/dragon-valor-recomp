@echo off
REM Dragon Valor (USA) Recompiled - player launch (release build).
REM   Play.bat [args]        -> build-release\DragonValorUS_Recompiled.exe [args]
REM   Play.bat -dbg [args]   -> build-debug\  (PSX_DEBUG_TOOLS=ON, TCP debug server on 127.0.0.1:4370)
REM Build: docs\DEVELOPMENT.md, "Build".
REM
REM The executable is a Windows-subsystem program (no console of its own); the
REM only console around is this script's, so the game is started detached and
REM this window closes at once. Failures are reported by the runtime in
REM psx_last_run_report.json next to the exe. Double-clicking the exe works
REM just as well. PlayDebug.bat sets PLAY_WAIT=1 to run the game in the
REM foreground instead and keep its window.
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
REM The freeze watchdog leaves a psx_freeze_dump_*.json on every boot (a false
REM positive on this title, upstream psxrecomp #646); clear them so they do
REM not pile up (35 MB each on the debug build).
del /q "%GAMEDIR%\psx_freeze_dump_*.json" >nul 2>&1
REM The runtime resolves saves, settings, bios/ and keybinds.ini relative to the
REM exe directory, so it is started there (start /D, or cd for the waiting path).
if defined PLAY_WAIT goto wait
start "" /D "%GAMEDIR%" "%GAMEDIR%\%EXE%"%ARGS%
exit /b 0
:wait
cd /d "%GAMEDIR%"
"%GAMEDIR%\%EXE%"%ARGS%
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" if not defined PLAY_NOPAUSE (
    echo Exited with code %RC%.
    pause
)
exit /b %RC%
