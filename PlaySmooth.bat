@echo off
REM Dragon Valor (USA) Recompiled - release build with Smooth motion (frame generation).
REM The game logic stays at its NTSC 60 Hz; the OpenGL render thread draws
REM in-between frames so the display shows more than 50 per second.
REM Press F in game to see "Game 60 fps | Display N fps" in the title bar.
REM Method: redraw (default). Try PSX_FRAME_GEN_METHOD=reprojection for the cheaper warp variant.
setlocal
set "EXE=DragonValorUS_Recompiled.exe"
set "GAMEDIR=%~dp0build-release"
set "PSX_RENDER_THREAD=1"
set "PSX_FRAME_GEN=1"
set "PSX_FRAME_GEN_METHOD=redraw"
set "PSX_FPS_TELEMETRY=1"
REM Fixed internal resolution (480 lines = 2x) with dynamic resolution OFF:
REM dynamic resolution and frame generation compete for the same GPU time and
REM the generation breaker then trips intermittently, which reads as stutter.
REM Try 720 (3x) if "Display" stays steady, 240 (native) if it does not.
set "PSX_INTERNAL_RESOLUTION=480"
set "PSX_DYNRES=0"
if not exist "%GAMEDIR%\%EXE%" (
    echo %EXE% nao encontrado em "%GAMEDIR%". Rode o rebuild descrito em docs\DEVELOPMENT.md
    pause
    exit /b 1
)
cd /d "%GAMEDIR%"
"%GAMEDIR%\%EXE%"
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
    echo Saiu com codigo %RC%.
    pause
)
exit /b %RC%
