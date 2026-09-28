param([Parameter(Mandatory=$true)][string]$DumpDirectory, [switch]$Check)
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$globals = (Get-Content -Raw (Join-Path $DumpDirectory 'offsets.json') | ConvertFrom-Json).'client.dll'
$buttons = (Get-Content -Raw (Join-Path $DumpDirectory 'buttons.json') | ConvertFrom-Json).'client.dll'
$classes = (Get-Content -Raw (Join-Path $DumpDirectory 'client_dll.json') | ConvertFrom-Json).'client.dll'.classes
$info = Get-Content -Raw (Join-Path $DumpDirectory 'info.json') | ConvertFrom-Json
$values = @{}
foreach ($name in @('dwEntityList','dwLocalPlayerController','dwLocalPlayerPawn','dwViewMatrix','dwViewAngles','dwCSGOInput','dwGlobalVars','dwGameEntitySystem_highestEntityIndex')) {
    if ($null -eq $globals.$name) { throw "Missing global $name" }
    $values[$name] = [long]$globals.$name
}
foreach ($name in @('Jump','Attack','Attack2','Forward','Back','Left','Right')) {
    $key = $name.ToLowerInvariant()
    if ($null -eq $buttons.$key) { throw "Missing button $key" }
    $values['dwForce' + $name] = [long]$buttons.$key
}
$mapping = @{
    CCSPlayerController = @('m_hPlayerPawn','m_bPawnIsAlive','m_sSanitizedPlayerName')
    C_BaseEntity = @('m_iHealth','m_lifeState','m_iTeamNum','m_pGameSceneNode','m_flSimulationTime','m_fFlags','m_nSubclassID')
    C_BasePlayerPawn = @('m_pCameraServices')
    CCSCustomPlayerCamera = @('m_nCameraMode','m_vecCameraOffset','m_bClipCameraOffset')
    C_CSPlayerPawn = @('m_iIDEntIndex')
    CGameSceneNode = @('m_vecAbsOrigin')
    CSkeletonInstance = @('m_modelState')
    C_BaseModelEntity = @('m_nRenderMode','m_clrRender','m_Glow','m_bUseClientOverrideTint','m_ClientOverrideTint')
}
$mapping['C_EconEntity'] = @($mapping['C_EconEntity']) + @('m_AttributeManager','m_nFallbackPaintKit','m_nFallbackSeed','m_flFallbackWear','m_nFallbackStatTrak') | Where-Object { $_ }
$mapping['C_AttributeContainer'] = @($mapping['C_AttributeContainer']) + @('m_Item') | Where-Object { $_ }
$mapping['C_EconItemView'] = @($mapping['C_EconItemView']) + @('m_iItemDefinitionIndex','m_iItemID','m_iItemIDHigh','m_iItemIDLow','m_iEntityQuality','m_bInitialized','m_bDisallowSOC','m_AttributeList','m_NetworkedDynamicAttributes') | Where-Object { $_ }
$mapping['C_CSPlayerPawn'] = @($mapping['C_CSPlayerPawn']) + @('m_EconGloves','m_nEconGlovesChanged','m_bNeedToReApplyGloves','m_hHudModelArms') | Where-Object { $_ }
$mapping['C_BasePlayerPawn'] = @($mapping['C_BasePlayerPawn']) + @('m_pWeaponServices') | Where-Object { $_ }
$mapping['CPlayer_WeaponServices'] = @($mapping['CPlayer_WeaponServices']) + @('m_hActiveWeapon','m_hMyWeapons') | Where-Object { $_ }
$mapping['C_BaseEntity'] = @($mapping['C_BaseEntity']) + @('m_hOwnerEntity') | Where-Object { $_ }
$mapping['CGameSceneNode'] = @($mapping['CGameSceneNode']) + @('m_pChild','m_pNextSibling','m_pOwner') | Where-Object { $_ }
$mapping['CModelState'] = @($mapping['CModelState']) + @('m_MeshGroupMask') | Where-Object { $_ }
foreach ($class in $mapping.Keys) {
    foreach ($name in $mapping[$class]) {
        $value = $classes.$class.fields.$name
        if ($null -eq $value) { throw "Missing field $class.$name" }
        $values[$name] = [long]$value
    }
}
foreach ($name in @('m_bGlowing','m_glowColorOverride')) {
    $value = $classes.CGlowProperty.fields.$name
    if ($null -eq $value) { throw "Missing glow field $name" }
    $values[$name] = $values['m_Glow'] + [long]$value
}
if ($null -eq $classes.CCSCustomPlayerCamera.fields.m_hPawn) { throw 'Missing camera pawn handle' }
$values['m_hPlayerPawnCamera'] = [long]$classes.CCSCustomPlayerCamera.fields.m_hPawn
$file = Join-Path $projectDirectory 'include\w1re\offsets.hpp'
$content = [System.IO.File]::ReadAllText($file)
$changed = 0
foreach ($name in $values.Keys) {
    $pattern = '((?:constexpr|inline) ptrdiff_t ' + [regex]::Escape($name) + ' = )0x[0-9A-Fa-f]+'
    $match = [regex]::Match($content, $pattern)
    if (-not $match.Success) { throw "Missing project constant $name" }
    $replacement = $match.Groups[1].Value + ('0x{0:X}' -f $values[$name])
    if ($match.Value -ne $replacement) { $changed++ }
    $content = [regex]::Replace($content, $pattern, $replacement)
}
if ($Check) {
    if ($changed) { throw "$changed offsets differ from the supplied dump." }
} else {
    $content = [regex]::Replace($content, '// Generated from .*', '// Generated from supplied cs2-dumper output; build ' + $info.build_number + '; ' + $info.timestamp)
    [System.IO.File]::WriteAllText($file, $content)
}
Write-Output ("Verified {0} generated offsets against build {1}; {2} changed." -f $values.Count, $info.build_number, $changed)
