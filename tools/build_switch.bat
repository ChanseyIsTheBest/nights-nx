@echo off
rem Builds the Switch NRO: app\out\switch\renut.nro
rem Usage: tools\build_switch.bat [extra cmake --build args]
rem   NVK_SDK (env) overrides the Mesa/NVK SDK folder (must contain lib\libvulkan.a).
rem Uses the VS-bundled CMake/Ninja (Windows paths) and devkitA64 GCC via the toolchain file.
set "VSROOT=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
set "PATH=C:\Python314;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
set "DEVKITPRO=C:/devkitPro"
set "ROOT=%~dp0.."
if "%NVK_SDK%"=="" set "NVK_SDK=C:/devkitPro/mesa-switch-main-nfsmw-source/mesa-switch-main/mesa-unified-install/opt/devkitpro/portlibs/switch"
if "%BUILD_DIR%"=="" set "BUILD_DIR=%ROOT%\app\out\switch"
if "%JOBS%"=="" set "JOBS=10"
if not exist "%BUILD_DIR%\CMakeCache.txt" (
  cmake -S "%ROOT%\app" -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release ^
    "-DCMAKE_TOOLCHAIN_FILE=%ROOT%\tools\switch\cmake\switch-devkitA64.cmake" ^
    "-DREXSDK_DIR=%ROOT%\sdk" "-DREXGLUE_SWITCH_NVK_SDK=%NVK_SDK%" ^
    -DPYTHON_EXECUTABLE=C:/Python314/python.exe -DPython3_EXECUTABLE=C:/Python314/python.exe %EXTRA_CMAKE% || exit /b 1
)
cmake --build "%BUILD_DIR%" -j %JOBS% %*
