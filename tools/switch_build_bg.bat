@echo off
rem nights-nx: tools\build_switch.bat with its output in a log, for unattended builds.
rem Usage: tools\switch_build_bg.bat <log> [extra cmake --build args]   (BUILD_DIR / JOBS / EXTRA_CMAKE as below)
rem EXTRA_CMAKE: extra configure options for a new folder (e.g. -DBANJO_PGO=generate), or applied to an existing one.
set "HERE=%~dp0"
set "LOG=%~1"
if not "%EXTRA_CMAKE%"=="" if exist "%BUILD_DIR%\CMakeCache.txt" (
  set "PATH=C:\Python314;C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
  cmake -S "%HERE%..\app" -B "%BUILD_DIR%" %EXTRA_CMAKE% > "%LOG%.configure" 2>&1
)
call "%HERE%build_switch.bat" %2 %3 %4 %5 > "%LOG%" 2>&1
echo exit=%ERRORLEVEL% >> "%LOG%"
