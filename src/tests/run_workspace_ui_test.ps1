$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$vcvars = @(
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $vcvars) { throw 'Visual Studio 2022 C++ Build Tools are required.' }
Push-Location -LiteralPath $projectDirectory
try {
    $compile = "call `"$vcvars`" && cl /EHsc /std:c++17 /FC /W3 /MT /DNOMINMAX /Iinclude /I. /Iimgui-1.92.7 /I`"MinHook_134_bin (1)\include`" tests\workspace_ui_test.cpp src\app\hooks.cpp src\app\input_capture.cpp src\features\music.cpp src\features\music_metadata.cpp src\features\music_ui.cpp src\features\skins_data.cpp src\features\skins_http.cpp src\features\skins_ui.cpp src\features\skins_game.cpp imgui-1.92.7\imgui.cpp imgui-1.92.7\imgui_draw.cpp imgui-1.92.7\imgui_tables.cpp imgui-1.92.7\imgui_widgets.cpp imgui-1.92.7\backends\imgui_impl_dx11.cpp imgui-1.92.7\backends\imgui_impl_win32.cpp /Fe:tests\workspace_ui_test.exe /Fo:tests\ /link /LIBPATH:`"MinHook_134_bin (1)\bin`" MinHook.x64.lib d3d11.lib dxgi.lib d3dcompiler.lib winmm.lib ole32.lib windowscodecs.lib user32.lib gdi32.lib dwmapi.lib"
    cmd /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'Workspace test compilation failed.' }
    Copy-Item -LiteralPath (Join-Path $projectDirectory 'MinHook.x64.dll') -Destination (Join-Path $PSScriptRoot 'MinHook.x64.dll')
    foreach ($case in @(@('small',855,681,1,0), @('large',1631,970,1,0), @('scaled',1280,800,1.5,0), @('music',855,681,1,6), @('skins',1000,800,1,7))) {
        & '.\tests\workspace_ui_test.exe' 'resources\music\Rental.mp3' "tests\workspace-$($case[0]).png" $case[1] $case[2] $case[3] $case[4]
        if ($LASTEXITCODE -ne 0) { throw "Workspace case $($case[0]) failed." }
    }
}
finally {
    foreach ($artifact in @('skins_data.obj','skins_http.obj','skins_ui.obj','skins_game.obj')) {
        Remove-Item -LiteralPath (Join-Path $PSScriptRoot $artifact) -Force -ErrorAction SilentlyContinue
    }
    foreach ($artifact in @('workspace_ui_test.exe','workspace_ui_test.obj','hooks.obj','input_capture.obj','MinHook.x64.dll','music.obj','music_metadata.obj','music_ui.obj','imgui.obj','imgui_draw.obj','imgui_tables.obj','imgui_widgets.obj','imgui_impl_dx11.obj','imgui_impl_win32.obj')) {
        Remove-Item -LiteralPath (Join-Path $PSScriptRoot $artifact) -Force -ErrorAction SilentlyContinue
    }
    Pop-Location
}
