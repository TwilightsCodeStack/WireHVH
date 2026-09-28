param([string]$Mp3 = 'resources\music\Rental.mp3')
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$vcvars = @(
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $vcvars) { throw 'Visual Studio 2022 C++ Build Tools are required.' }
Push-Location -LiteralPath $projectDirectory
try {
    $compile = "call `"$vcvars`" && cl /EHsc /std:c++17 /FC /W3 /MT /DNOMINMAX /Iinclude /I. /Iimgui-1.92.7 tests\music_ui_preview.cpp src\features\music.cpp src\features\music_metadata.cpp src\features\music_ui.cpp imgui-1.92.7\imgui.cpp imgui-1.92.7\imgui_draw.cpp imgui-1.92.7\imgui_tables.cpp imgui-1.92.7\imgui_widgets.cpp imgui-1.92.7\backends\imgui_impl_dx11.cpp /Fe:tests\music_ui_preview.exe /Fo:tests\ /link d3d11.lib dxgi.lib d3dcompiler.lib winmm.lib ole32.lib windowscodecs.lib user32.lib"
    cmd /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'UI preview compilation failed.' }
    & '.\tests\music_ui_preview.exe' $Mp3 'tests\music-preview.png'
    if ($LASTEXITCODE -ne 0) { throw 'UI preview failed; provide an MP3 with embedded cover art.' }
}
finally {
    foreach ($artifact in @('music_ui_preview.exe', 'music_ui_preview.obj', 'music.obj', 'music_metadata.obj', 'music_ui.obj', 'imgui.obj', 'imgui_draw.obj', 'imgui_tables.obj', 'imgui_widgets.obj', 'imgui_impl_dx11.obj')) {
        Remove-Item -LiteralPath (Join-Path $PSScriptRoot $artifact) -Force -ErrorAction SilentlyContinue
    }
    Pop-Location
}
