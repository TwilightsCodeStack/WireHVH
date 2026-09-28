#pragma once
#include <cstdint>
#include <cstddef>

using ptrdiff_t = std::ptrdiff_t;

namespace Offsets {
    // === Global Offsets (client.dll) ===
    // Generated from supplied cs2-dumper output; build 14183; 2026-09-24T19:49:26.254182800+00:00
    inline ptrdiff_t dwEntityList = 0x2711048;
    inline ptrdiff_t dwLocalPlayerController = 0x25334D8;
    inline ptrdiff_t dwLocalPlayerPawn = 0x255C5A8;
    inline ptrdiff_t dwViewMatrix = 0x25618F0;
    inline ptrdiff_t dwViewAngles = 0x2572118;
    inline ptrdiff_t dwCSGOInput = 0x2571A90;
    inline ptrdiff_t dwGlobalVars = 0x2227F08;
    inline ptrdiff_t dwGameEntitySystem_highestEntityIndex = 0x2120;

    // === Button offsets (client.dll) ===
    inline ptrdiff_t dwForceJump = 0x222C550;
    inline ptrdiff_t dwForceAttack = 0x222C040;
    inline ptrdiff_t dwForceAttack2 = 0x222C0D0;
    inline ptrdiff_t dwForceForward = 0x222C280;
    inline ptrdiff_t dwForceBack = 0x222C310;
    inline ptrdiff_t dwForceLeft = 0x222C3A0;
    inline ptrdiff_t dwForceRight = 0x222C430;

    // === CCSPlayerController ===
    inline ptrdiff_t m_hPlayerPawn = 0x92C;
    inline ptrdiff_t m_bPawnIsAlive = 0x934;
    inline ptrdiff_t m_sSanitizedPlayerName = 0x878;

    // === C_BaseEntity / CCSPlayerPawn ===
    inline ptrdiff_t m_iHealth = 0x34C;
    inline ptrdiff_t m_lifeState = 0x354;
    inline ptrdiff_t m_iTeamNum = 0x3E7;   // uint8
    inline ptrdiff_t m_pGameSceneNode = 0x330;
    inline ptrdiff_t m_pCameraServices = 0x1328;
    inline ptrdiff_t m_hPlayerPawnCamera = 0x600;
    inline ptrdiff_t m_nCameraMode = 0x604;
    inline ptrdiff_t m_vecCameraOffset = 0x61C;
    inline ptrdiff_t m_bClipCameraOffset = 0x628;
    inline ptrdiff_t m_iIDEntIndex = 0x36CC;
    inline ptrdiff_t m_flSimulationTime = 0x3B8;
    inline ptrdiff_t m_fFlags = 0x3F4;

    // === CGameSceneNode ===
    inline ptrdiff_t m_vecAbsOrigin = 0xC8;

    // === CSkeletonInstance ===
    inline ptrdiff_t m_modelState = 0x140;

    // === C_BaseModelEntity visual properties ===
    inline ptrdiff_t m_nRenderMode = 0xC80;
    inline ptrdiff_t m_clrRender = 0xCA0;
    inline ptrdiff_t m_Glow = 0xDE8;
    inline ptrdiff_t m_bGlowing = 0xE39; // m_Glow + CGlowProperty::m_bGlowing
    inline ptrdiff_t m_glowColorOverride = 0xE28; // m_Glow + CGlowProperty::m_glowColorOverride
    inline ptrdiff_t m_bUseClientOverrideTint = 0x104C;
    inline ptrdiff_t m_ClientOverrideTint = 0x1048;

    // === CModelState ===
    constexpr ptrdiff_t m_BoneArray = 0x80;

    // Entity stride
    constexpr ptrdiff_t ENTITY_IDENTITY_SIZE = 0x70;

    // Client cosmetic fields; refreshed by the loader from the running game.
    inline ptrdiff_t m_nSubclassID = 0x380;
    inline ptrdiff_t m_AttributeManager = 0x1290;
    inline ptrdiff_t m_nFallbackPaintKit = 0x18A8;
    inline ptrdiff_t m_nFallbackSeed = 0x18AC;
    inline ptrdiff_t m_flFallbackWear = 0x18B0;
    inline ptrdiff_t m_nFallbackStatTrak = 0x18B4;
    inline ptrdiff_t m_Item = 0x50;
    inline ptrdiff_t m_iItemDefinitionIndex = 0x1BA;
    inline ptrdiff_t m_iItemID = 0x1C8;
    inline ptrdiff_t m_iItemIDHigh = 0x1D0;
    inline ptrdiff_t m_iItemIDLow = 0x1D4;
    inline ptrdiff_t m_iEntityQuality = 0x1BC;
    inline ptrdiff_t m_bInitialized = 0x1E8;
    inline ptrdiff_t m_bDisallowSOC = 0x1E9;
    inline ptrdiff_t m_AttributeList = 0x208;
    inline ptrdiff_t m_NetworkedDynamicAttributes = 0x280;
    inline ptrdiff_t m_EconGloves = 0x1770;
    inline ptrdiff_t m_nEconGlovesChanged = 0x1D20;
    inline ptrdiff_t m_bNeedToReApplyGloves = 0x176D;
    inline ptrdiff_t m_hHudModelArms = 0x1DA8;
    inline ptrdiff_t m_pWeaponServices = 0x12F0;
    inline ptrdiff_t m_hActiveWeapon = 0x60;
    inline ptrdiff_t m_hMyWeapons = 0x48;
    inline ptrdiff_t m_hOwnerEntity = 0x520;
    inline ptrdiff_t m_pChild = 0x40;
    inline ptrdiff_t m_pNextSibling = 0x48;
    inline ptrdiff_t m_pOwner = 0x30;
    inline ptrdiff_t m_MeshGroupMask = 0x208;
}
