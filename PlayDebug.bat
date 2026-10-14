@echo off
REM Dragon Valor (USA) Recompiled - debug launch.
REM Uses build-debug\ (PSX_DEBUG_TOOLS=ON): TCP debug server on 127.0.0.1:4370
REM (psxrecomp\tools\debug_client.py or tools\dvdbg.py talk to it), FPS readout on
REM from the start (F toggles it). The window stays open on a non-zero exit so the
REM runtime log can be read.
REM Rebuild: python psxrecomp\psxrecomp_cli.py rebuild --config game.toml --project-root . --build-dir build-debug --no-pgo --cmake-extra=-DPSX_DEBUG_TOOLS=ON
setlocal
set "EXE=DragonValorUS_Recompiled.exe"
set "GAMEDIR=%~dp0build-debug"
set "PSX_DEBUG_SERVER=1"
set "PSX_FPS_TELEMETRY=1"
if not exist "%GAMEDIR%\%EXE%" (
    echo Build de debug nao encontrado em "%GAMEDIR%". Rode o rebuild indicado no topo deste script.
    pause
    exit /b 1
)
echo Dragon Valor Recompiled [DEBUG] - servidor de debug em 127.0.0.1:4370
cd /d "%GAMEDIR%"
"%GAMEDIR%\%EXE%"
set "RC=%ERRORLEVEL%"
echo === saiu com codigo %RC% ===
if not "%RC%"=="0" pause
exit /b %RC%
