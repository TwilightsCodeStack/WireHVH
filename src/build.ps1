param([switch]$DllOnly)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$imguiDir = 'imgui-1.92.7'
$minHookDir = 'MinHook_134_bin (1)'
$sources = @(
    'src\app\dllmain.cpp', 'src\app\hooks.cpp', 'src\app\input_capture.cpp',
    'src\features\music.cpp', 'src\features\music_metadata.cpp', 'src\features\music_ui.cpp',
    'src\features\skins_data.cpp', 'src\features\skins_http.cpp', 'src\features\skins_ui.cpp', 'src\features\skins_game.cpp',
    'src\features\esp.cpp', 'src\features\triggerbot.cpp', 'src\features\aimbot.cpp',
    'src\features\farm.cpp', 'src\features\raycasting.cpp', 'src\features\utility.cpp',
    'src\features\visuals.cpp', 'src\features\angles.cpp',
    'src\features\walkbot.cpp', 'src\features\movement.cpp',
    "$imguiDir\imgui.cpp", "$imguiDir\imgui_draw.cpp",
    "$imguiDir\imgui_tables.cpp", "$imguiDir\imgui_widgets.cpp",
    "$imguiDir\imgui_demo.cpp",
    "$imguiDir\backends\imgui_impl_dx11.cpp",
    "$imguiDir\backends\imgui_impl_win32.cpp"
)
$pathMap = "/experimental:deterministic /pathmap:`"$PSScriptRoot=.`""

Remove-Item -Force -ErrorAction SilentlyContinue *.obj, W1RE.dll
$vcvarsCandidates = @(
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
)
$vcvars = $vcvarsCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vcvars) {
    Write-Error 'vcvars64.bat not found. Install the "Desktop development with C++" workload for Visual Studio 2022 Build Tools and rerun this script.'
    exit 1
}
$sourceArgs = ($sources | ForEach-Object { "`"$_`"" }) -join ' '
$cmd = "call `"$vcvars`" && cl $pathMap /LD /EHa /std:c++17 /FC /W3 /MT /O2 /DNOMINMAX /Iinclude /I. /I`"$imguiDir`" /I`"$imguiDir\backends`" /I`"$minHookDir\include`" $sourceArgs /link /OUT:W1RE.dll /LIBPATH:`"$minHookDir\bin`" MinHook.x64.lib d3d11.lib dxgi.lib user32.lib gdi32.lib dwmapi.lib winmm.lib ole32.lib windowscodecs.lib"
cmd /c $cmd
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if (-not $DllOnly) {
    $loaderCommand = "call `"$vcvars`" && cl $pathMap /EHsc /std:c++17 /FC /W4 /MT /O2 /DNOMINMAX /Iinclude /I. src\app\loader.cpp /Fe:W1RE-Loader.exe /link /MACHINE:X64"
    cmd /c $loaderCommand
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
Copy-Item -LiteralPath "$minHookDir\bin\MinHook.x64.dll" -Destination 'MinHook.x64.dll' -Force
Remove-Item -Force -ErrorAction SilentlyContinue *.obj
if ($DllOnly) { Write-Host '[SUCCESS] W1RE.dll built successfully.' }
else { Write-Host '[SUCCESS] W1RE.dll and W1RE-Loader.exe built successfully.' }
