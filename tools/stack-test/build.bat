@echo off
rem stack_probe.exe and fiber_probe.exe (see README.md). Built into %MG_PROBE_DIR% (default C:\b\mgl-stackprobe) with
rem the stock Ultralight runtime copied beside them, so the repo stays clean.
call "%~dp0..\desktop-harness\vcvars.bat" || exit /b 1
if not defined MG_PROBE_DIR set MG_PROBE_DIR=C:\b\mgl-stackprobe
if not exist "%MG_PROBE_DIR%" mkdir "%MG_PROBE_DIR%"
for %%P in (stack_probe fiber_probe) do (
  cl /nologo /EHsc /std:c++17 /MD /O2 /I "%~dp0..\..\extern\ultralight\include" "%~dp0%%P.cpp" /Fo:"%MG_PROBE_DIR%\\" /Fe:"%MG_PROBE_DIR%\%%P.exe" /link /LIBPATH:"%~dp0..\..\extern\ultralight\lib" AppCore.lib Ultralight.lib WebCore.lib UltralightCore.lib || exit /b 1
)
copy /y "%~dp0..\..\extern\ultralight\bin\*.dll" "%MG_PROBE_DIR%\" >nul
echo built %MG_PROBE_DIR%\stack_probe.exe and fiber_probe.exe
