param(
    [Parameter(Mandatory=$true)][string]$DumpDirectory,
    [Parameter(Mandatory=$true)][string]$GameDirectory,
    [string]$NativeProfile = (Join-Path $PSScriptRoot '..\profiles\skins-client-14183.json'),
    [switch]$Check
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$info = Get-Content -Raw (Join-Path $DumpDirectory 'info.json') | ConvertFrom-Json
$profile = Get-Content -Raw $NativeProfile | ConvertFrom-Json
$bytes = [IO.File]::ReadAllBytes((Join-Path $GameDirectory 'csgo\bin\win64\client.dll'))
$pe = [BitConverter]::ToInt32($bytes,0x3c)
$stamp = [BitConverter]::ToUInt32($bytes,$pe+8)
$size = [BitConverter]::ToUInt32($bytes,$pe+24+56)
if ($info.build_number -ne $profile.build -or $stamp -ne $profile.clientStamp -or $size -ne $profile.clientSize) {
    throw 'Game/dump differs from the reviewed client function profile. Review native calls before generating a new profile.'
}
$sectionCount = [BitConverter]::ToUInt16($bytes,$pe+6)
$sectionStart = $pe+24+[BitConverter]::ToUInt16($bytes,$pe+20)
foreach ($function in $profile.functions.PSObject.Properties) {
    $rva = [uint32]$function.Value.rva
    $fileOffset = $null
    foreach ($index in 0..($sectionCount-1)) {
        $section = $sectionStart+40*$index
        $start = [BitConverter]::ToUInt32($bytes,$section+12)
        $length = [BitConverter]::ToUInt32($bytes,$section+16)
        $flags = [BitConverter]::ToUInt32($bytes,$section+36)
        if (($flags -band 0x20000000) -and $rva -ge $start -and $rva -lt ($start+$length)) {
            $fileOffset = [BitConverter]::ToUInt32($bytes,$section+20)+$rva-$start
            break
        }
    }
    if ($null -eq $fileOffset) {throw "Non-executable function: $($function.Name)"}
    $expected = @($function.Value.prefix.Split(' ') | ForEach-Object {[Convert]::ToByte($_,16)})
    for ($index=0; $index -lt $expected.Count; $index++) {
        if ($bytes[$fileOffset+$index] -ne $expected[$index]) {throw "Native function differs: $($function.Name)"}
    }
}
if (-not $Check) {
    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add('#pragma once')
    $lines.Add('#include <cstdint>')
    $lines.Add('// Reviewed client native calls; schema fields are refreshed separately by the loader.')
    $lines.Add('namespace Skins::Profile {')
    $lines.Add("inline constexpr int Build = $($profile.build);")
    $lines.Add("inline constexpr uint32_t ClientStamp = ${stamp}u, ClientSize = ${size}u;")
    foreach ($function in $profile.functions.PSObject.Properties) {
        $lines.Add(('inline constexpr uintptr_t {0} = 0x{1:X};' -f $function.Name,[uint32]$function.Value.rva))
    }
    $lines.Add("inline constexpr unsigned FrameIndex = $($profile.frameIndex);")
    $lines.Add("inline constexpr int PostDataUpdateEnd = $($profile.postDataUpdateEnd), NetUpdateEnd = $($profile.netUpdateEnd);")
    $lines.Add('}')
    [IO.File]::WriteAllLines((Join-Path $root 'include\w1re\features\skins_profile.hpp'),$lines)
}
Write-Host "Verified client cosmetic native calls for build $($profile.build)."
