@echo off
rem Builds and runs tooltip_test.exe: the host half of page tooltips (src\TooltipCore.h, 1.2.0) on
rem the desktop, no game. %1 = output folder (default C:\b\mgl-tooltiptest). Exit 0 when every check passes.
setlocal
set "OUT=%~1"
if "%OUT%"=="" set "OUT=C:\b\mgl-tooltiptest"
call "%~dp0..\desktop-harness\vcvars.bat" || exit /b 1
set "ROOT=%~dp0..\.."
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /EHsc /std:c++20 /W4 /WX /MD /O2 /I "%ROOT%\src" "%~dp0tooltip_test.cpp" /Fo"%OUT%\\" /Fe"%OUT%\tooltip_test.exe" || exit /b 1
"%OUT%\tooltip_test.exe"
exit /b %ERRORLEVEL%
