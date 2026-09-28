@echo off
cd /d "%~dp0"
set "VS_BASE="
if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VS_BASE=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
if "%VS_BASE%"=="" if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VS_BASE=C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
if "%VS_BASE%"=="" (
    echo [ERROR] vcvars64.bat not found. Install the "Desktop development with C++" workload for Visual Studio 2022 Build Tools and rerun this script.
    exit /b 1
)
call "%VS_BASE%\VC\Auxiliary\Build\vcvars64.bat"

set "DLL_NAME=W1RE.dll"
set "IMGUI_DIR=imgui-1.92.7"
set "MINHOOK_DIR=MinHook_134_bin (1)"

del /q "*.obj" 2>nul
if exist "%DLL_NAME%" del /q "%DLL_NAME%"

cl /experimental:deterministic "/pathmap:%CD%=." /LD /EHa /std:c++17 /FC /W3 /MT /O2 /DNOMINMAX ^
    /Iinclude /I. /I"%IMGUI_DIR%" /I"%IMGUI_DIR%\backends" /I"%MINHOOK_DIR%\include" ^
    src\app\dllmain.cpp src\app\hooks.cpp src\app\input_capture.cpp ^
    src\features\music.cpp src\features\music_metadata.cpp src\features\music_ui.cpp ^
    src\features\skins_data.cpp src\features\skins_http.cpp src\features\skins_ui.cpp src\features\skins_game.cpp ^
    src\features\esp.cpp src\features\triggerbot.cpp src\features\aimbot.cpp ^
    src\features\farm.cpp src\features\raycasting.cpp src\features\utility.cpp ^
    src\features\visuals.cpp src\features\angles.cpp ^
    src\features\walkbot.cpp src\features\movement.cpp ^
    "%IMGUI_DIR%\imgui.cpp" "%IMGUI_DIR%\imgui_draw.cpp" ^
    "%IMGUI_DIR%\imgui_tables.cpp" "%IMGUI_DIR%\imgui_widgets.cpp" ^
    "%IMGUI_DIR%\imgui_demo.cpp" ^
    "%IMGUI_DIR%\backends\imgui_impl_dx11.cpp" ^
    "%IMGUI_DIR%\backends\imgui_impl_win32.cpp" ^
    /link /OUT:%DLL_NAME% /LIBPATH:"%MINHOOK_DIR%\bin" ^
    MinHook.x64.lib d3d11.lib dxgi.lib user32.lib gdi32.lib dwmapi.lib winmm.lib ole32.lib windowscodecs.lib
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Compilation failed.
    exit /b %ERRORLEVEL%
)
cl /experimental:deterministic "/pathmap:%CD%=." /EHsc /std:c++17 /FC /W4 /MT /O2 /DNOMINMAX /Iinclude /I. src\app\loader.cpp /Fe:W1RE-Loader.exe /link /MACHINE:X64
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Loader compilation failed.
    exit /b %ERRORLEVEL%
)
copy /y "%MINHOOK_DIR%\bin\MinHook.x64.dll" "MinHook.x64.dll" >nul
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%
echo [SUCCESS] %DLL_NAME% and W1RE-Loader.exe built successfully.
