@echo off
rem nights-nx: tools\build_pc.bat with its output in a log, for unattended builds.
rem Usage: tools\pc_build_bg.bat <log> [extra cmake --build args]   (EXTRA_CMAKE / BUILD_DIR as for build_pc.bat)
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
set "HERE=%~dp0"
set "LOG=%~1"
call "%HERE%build_pc.bat" %2 %3 %4 %5 > "%LOG%" 2>&1
echo exit=%ERRORLEVEL% >> "%LOG%"
