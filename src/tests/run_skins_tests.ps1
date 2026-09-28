$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
$vcvars = @('C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat','C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat') | Where-Object {Test-Path $_} | Select-Object -First 1
if (-not $vcvars) {throw 'Visual Studio 2022 C++ build tools are required.'}
foreach ($name in @('skins_data_test','skins_game_test')) {
    $compile = "call `"$vcvars`" && cl /EHa /std:c++17 /W4 /MT /DNOMINMAX /Iinclude /I. /I`"MinHook_134_bin (1)\include`" tests\$name.cpp src\features\skins_data.cpp /Fe:tests\$name.exe /Fo:tests\ /link /LIBPATH:`"MinHook_134_bin (1)\bin`" MinHook.x64.lib"
    cmd /c $compile
    if ($LASTEXITCODE -ne 0) {throw "Compilation failed: $name"}
    $previousPath = $env:PATH
    try {
        $env:PATH = (Get-Location).Path + ';' + $env:PATH
        & ".\tests\$name.exe"
        if ($LASTEXITCODE -ne 0) {throw "Test failed: $name"}
    } finally { $env:PATH = $previousPath }
}
