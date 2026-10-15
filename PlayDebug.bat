@echo off
REM Dragon Valor (USA) Recompiled - debug launch.
REM Same as "Play.bat -dbg" (build-debug\, PSX_DEBUG_TOOLS=ON, TCP debug server on
REM 127.0.0.1:4370 for tools\dvdbg.py) with the FPS readout on from the start
REM (F toggles it). PLAY_WAIT keeps the game in the foreground so this window
REM stays until it exits and shows the exit code (the runtime itself is a
REM Windows-subsystem program and prints nothing here; its report is
REM psx_last_run_report.json next to the exe, and tools\dvdbg.py launch
REM captures its stdout to dvdbg_4370.log).
REM Build: docs\DEVELOPMENT.md, "Build" (step 3b).
setlocal
set "PSX_FPS_TELEMETRY=1"
set "PLAY_NOPAUSE=1"
set "PLAY_WAIT=1"
echo Dragon Valor Recompiled [DEBUG] - debug server on 127.0.0.1:4370
call "%~dp0Play.bat" -dbg %*
set "RC=%ERRORLEVEL%"
echo === exited with code %RC% ===
pause
exit /b %RC%
