@echo off
rem nights-nx: reruns the PC build's shader translation (RENUT_RECOMP) with BANJO_DEKO3D_SPIRV_DIR set, so Renut's
rem XenosRecomp also writes each game shader's deko3d SPIR-V (one <vs|ps>_<hash>.spv) into out\deko3d_shaders\spirv,
rem the input of tools\deko3d_shaders.py. Usage: tools\deko3d_spirv_pc.bat <log>
call "%~dp0pc_env.bat" || exit /b 1
set "ROOT=%~dp0.."
set "BANJO_DEKO3D_SPIRV_DIR=%ROOT%\out\deko3d_shaders\spirv"
if not exist "%BANJO_DEKO3D_SPIRV_DIR%" mkdir "%BANJO_DEKO3D_SPIRV_DIR%"
rem Any dump file newer than the cache makes the step run again.
for %%f in ("%ROOT%\out\renut_shader_dump\ps_*.bin") do (copy /b "%%f"+,, "%%f" >nul & goto touched)
:touched
cmake --build "%ROOT%\app\out\pc-nx" --target renut_shader_cache_gen > "%~1" 2>&1
echo exit=%ERRORLEVEL% >> "%~1"
