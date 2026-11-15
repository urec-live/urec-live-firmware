@echo off
rem Builds tools\replay_reps with MSVC (VS 2022 Build Tools) and replays recorded sets through
rem gym::RepValidator. Example, from GetMeFit\:
rem   tools\replay_reps\run_replay.cmd sessions\2026-11-11_1413_sets --truth=tools\replay_reps\truth_2026-11-10.txt
setlocal
set ROOT=%~dp0..\..
set OUT=%~dp0build
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cl /nologo /std:c++17 /EHsc /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /I "%ROOT%\lib\gym_core\src" ^
  /Fo"%OUT%\\" /Fe"%OUT%\replay_reps.exe" ^
  "%~dp0replay_reps.cpp" "%ROOT%\lib\gym_core\src\rep_validator.cpp" >"%OUT%\build.log" || (type "%OUT%\build.log" & exit /b 1)
"%OUT%\replay_reps.exe" %*
