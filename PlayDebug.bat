@echo off
REM Dragon Valor (USA) Recompiled - debug launch.
REM Same as "Play.bat -dbg" (build-debug\, PSX_DEBUG_TOOLS=ON, TCP debug server on
REM 127.0.0.1:4370 for tools\dvdbg.py) with the FPS readout on from the start
REM (F toggles it). The window stays open on exit so the runtime log can be read.
REM Build: docs\DEVELOPMENT.md, "Build" (step 3b).
setlocal
set "PSX_FPS_TELEMETRY=1"
set "PLAY_NOPAUSE=1"
echo Dragon Valor Recompiled [DEBUG] - debug server on 127.0.0.1:4370
call "%~dp0Play.bat" -dbg %*
set "RC=%ERRORLEVEL%"
echo === exited with code %RC% ===
pause
exit /b %RC%
