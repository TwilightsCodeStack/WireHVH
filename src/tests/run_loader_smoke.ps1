$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$vcvars = @(
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $vcvars) { throw 'Visual Studio 2022 C++ Build Tools are required.' }
# Spaces and Unicode exercise full wide-character DLL paths.
$outputDirectory = Join-Path $PSScriptRoot ('loader-smoke-' + [guid]::NewGuid().ToString('N') + ' space-' + [char]0x03A9)
$null = New-Item -ItemType Directory -Path $outputDirectory
$fixtureProcess = $null
Push-Location -LiteralPath $projectDirectory
try {
    $common = '/EHsc /std:c++17 /W4 /MT /O2 /DNOMINMAX /Iinclude /I.'
    $commands = @(
        "cl $common src\app\loader.cpp /Fo:`"$outputDirectory\loader.obj`" /Fe:`"$outputDirectory\W1RE-Loader.exe`"",
        "cl $common tests\loader_fixture.cpp /Fo:`"$outputDirectory\host.obj`" /Fe:`"$outputDirectory\cs2.exe`"",
        "cl $common /LD /DLOADER_TEST_DLL tests\loader_fixture.cpp /Fo:`"$outputDirectory\fixture.obj`" /link /OUT:`"$outputDirectory\W1RE.dll`" /IMPLIB:`"$outputDirectory\fixture.lib`" /LIBPATH:`"MinHook_134_bin (1)\bin`" MinHook.x64.lib"
    )
    foreach ($command in $commands) {
        cmd /c "call `"$vcvars`" && $command"
        if ($LASTEXITCODE -ne 0) { throw 'Loader smoke-test compilation failed.' }
    }
    Copy-Item -LiteralPath (Join-Path $projectDirectory 'MinHook_134_bin (1)\bin\MinHook.x64.dll') -Destination $outputDirectory
    $loader = Join-Path $outputDirectory 'W1RE-Loader.exe'
    function Invoke-LoaderCase([string[]] $Arguments, [int] $ExpectedCode, [string] $ExpectedText) {
        $previousPreference = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try {
            $output = (& $loader @Arguments --no-pause 2>&1 | Out-String)
            $code = $LASTEXITCODE
        } finally { $ErrorActionPreference = $previousPreference }
        if ($code -ne $ExpectedCode -or $output -notmatch $ExpectedText) {
            throw "Loader case failed (exit $code): $output"
        }
        Write-Host "PASS: $ExpectedText"
    }
    Invoke-LoaderCase @('--help') 0 'validates files'
    Invoke-LoaderCase @('--pid', '-1') 1 'positive process ID'
    Invoke-LoaderCase @('--pid', '4294967296') 1 'out of range'
    Invoke-LoaderCase @('--unknown') 1 'Unknown or incomplete'
    Invoke-LoaderCase @('--pid', "$PID") 1 'not a running cs2.exe'
    $installGame = Join-Path $outputDirectory 'install-game'
    $null = New-Item -ItemType Directory -Path $installGame
    Copy-Item -LiteralPath (Join-Path $outputDirectory 'cs2.exe') -Destination (Join-Path $installGame 'cs2.exe')
    Set-Content -LiteralPath (Join-Path $outputDirectory 'cs2-dumper.exe') -Value 'fixture dumper'
    $sourceResources = Join-Path $outputDirectory 'resources'
    $null = New-Item -ItemType Directory -Path $sourceResources
    Set-Content -LiteralPath (Join-Path $sourceResources 'install-test.txt') -Value 'initial resource'
    Invoke-LoaderCase @('--install', $installGame) 0 'Installed loader bundle'
    $installedDirectory = Join-Path $installGame 'W1RE'
    foreach ($file in @('W1RE-Loader.exe', 'W1RE.dll', 'MinHook.x64.dll', 'cs2-dumper.exe', 'resources\install-test.txt')) {
        if (-not (Test-Path -LiteralPath (Join-Path $installedDirectory $file))) {
            throw "Install omitted $file."
        }
    }
    Set-Content -LiteralPath (Join-Path $installedDirectory 'resources\install-test.txt') -Value 'keep customized resource'
    Set-Content -LiteralPath (Join-Path $outputDirectory 'cs2-dumper.exe') -Value 'updated fixture dumper'
    Set-Content -LiteralPath (Join-Path $sourceResources 'install-test.txt') -Value 'updated resource'
    Invoke-LoaderCase @('--install', $installGame) 0 'Installed loader bundle'
    if ((Get-Content -LiteralPath (Join-Path $installedDirectory 'resources\install-test.txt') -Raw).Trim() -ne 'keep customized resource') {
        throw 'Reinstall overwrote an existing resource.'
    }
    if ((Get-Content -LiteralPath (Join-Path $installedDirectory 'cs2-dumper.exe') -Raw).Trim() -ne 'updated fixture dumper') {
        throw 'Reinstall did not update the runtime files.'
    }
    Remove-Item -LiteralPath (Join-Path $outputDirectory 'cs2-dumper.exe')
    Remove-Item -LiteralPath $sourceResources -Recurse -Force
    $fixtureProcess = Start-Process -FilePath (Join-Path $outputDirectory 'cs2.exe') -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 300
    $targetArgs = @('--pid', "$($fixtureProcess.Id)")
    Invoke-LoaderCase ($targetArgs + '--check') 0 'ready to load'
    $fixtureProcess.Refresh()
    if (@($fixtureProcess.Modules | Where-Object ModuleName -eq 'W1RE.dll').Count) {
        throw '--check unexpectedly loaded the fixture DLL.'
    }
    $dllPath = Join-Path $outputDirectory 'W1RE.dll'
    $backupPath = Join-Path $outputDirectory 'fixture-backup.dll'
    Move-Item -LiteralPath $dllPath -Destination $backupPath
    Invoke-LoaderCase ($targetArgs + '--check') 1 'Missing or invalid DLL'
    [System.IO.File]::WriteAllText($dllPath, 'invalid fixture')
    Invoke-LoaderCase ($targetArgs + '--check') 1 'Missing or invalid DLL'
    Remove-Item -LiteralPath $dllPath
    Move-Item -LiteralPath $backupPath -Destination $dllPath
    Invoke-LoaderCase $targetArgs 0 'W1RE.dll loaded'
    $fixtureProcess.Refresh()
    if (@($fixtureProcess.Modules | Where-Object ModuleName -eq 'W1RE.dll').Count -ne 1) {
        throw 'Fixture DLL is missing from the test host.'
    }
    Invoke-LoaderCase $targetArgs 0 'already loaded; no second copy'
    Invoke-LoaderCase ($targetArgs + '--check') 0 'already loaded'
    Write-Host 'All loader smoke tests passed. The actual game was not used.'
}
finally {
    if ($fixtureProcess -and -not $fixtureProcess.HasExited) {
        $fixtureProcess.Kill()
        $fixtureProcess.WaitForExit()
    }
    if ($fixtureProcess) { $fixtureProcess.Dispose() }
    Pop-Location
    $resolvedOutput = [System.IO.Path]::GetFullPath($outputDirectory)
    $testsRoot = [System.IO.Path]::GetFullPath($PSScriptRoot) + [System.IO.Path]::DirectorySeparatorChar
    if (-not $resolvedOutput.StartsWith($testsRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing cleanup outside the tests directory.'
    }
    Remove-Item -LiteralPath $resolvedOutput -Recurse -Force
}
