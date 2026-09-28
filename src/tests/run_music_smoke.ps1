param([string]$Mp3 = '')
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$vcvarsCandidates = @(
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
)
$vcvars = $vcvarsCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $vcvars) {
    throw 'Install Visual Studio 2022 Build Tools with Desktop development with C++.'
}

Push-Location -LiteralPath $projectDirectory
try {
    $compile = "call `"$vcvars`" && cl /EHsc /std:c++17 /FC /W4 /MT /DNOMINMAX /Iinclude /I. tests\music_smoke.cpp src\features\music.cpp src\features\music_metadata.cpp /Fe:tests\music_smoke.exe /Fo:tests\ /link winmm.lib ole32.lib windowscodecs.lib user32.lib"
    cmd /c $compile
    if ($LASTEXITCODE -ne 0) { throw 'Music test compilation failed.' }
    if ($Mp3) { & '.\tests\music_smoke.exe' $Mp3 }
    else { & '.\tests\music_smoke.exe' }
    if ($LASTEXITCODE -ne 0) { throw 'Music integration test failed.' }
}
finally {
    foreach ($artifact in @('music_smoke.exe', 'music_smoke.obj', 'music.obj', 'music_metadata.obj')) {
        Remove-Item -LiteralPath (Join-Path $PSScriptRoot $artifact) -Force -ErrorAction SilentlyContinue
    }
    Pop-Location
}
