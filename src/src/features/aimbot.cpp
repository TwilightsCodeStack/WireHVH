#include <w1re/math.hpp>
#include <w1re/offsets.hpp>
#include <w1re/config.hpp>
#include <windows.h>
#include <cstdint>
#include <cmath>

namespace Features {
    void RunAimbot() {
        if (!Config::bAimAssist) return;
        if (!(GetAsyncKeyState(VK_RBUTTON) & 0x8000)) return;

        uintptr_t client = (uintptr_t)GetModuleHandleA("client.dll");
        if (!client) return;

        uintptr_t localPlayerPawn = *(uintptr_t*)(client + Offsets::dwLocalPlayerPawn);
        if (!localPlayerPawn) return;

        uintptr_t entityList = *(uintptr_t*)(client + Offsets::dwEntityList);
        if (!entityList) return;

        uint8_t localTeam = 0;
        Vector3 localPos = {0, 0, 0};
        __try {
            localTeam = *(uint8_t*)(localPlayerPawn + Offsets::m_iTeamNum);
            uintptr_t sceneNode = *(uintptr_t*)(localPlayerPawn + Offsets::m_pGameSceneNode);
            if (sceneNode) localPos = *(Vector3*)(sceneNode + Offsets::m_vecAbsOrigin);
        } __except (EXCEPTION_EXECUTE_HANDLER) { return; }

        if (localTeam <= 0) return;

        // Read current view angles as smoothing base
        QAngle currentAngles;
        __try {
            float* viewAngles = (float*)(client + Offsets::dwViewAngles);
            currentAngles.pitch = viewAngles[0];
            currentAngles.yaw = viewAngles[1];
        } __except (EXCEPTION_EXECUTE_HANDLER) { return; }

        // Add eye height offset (standing eye height ~64 units)
        Vector3 eyePos = localPos;
        eyePos.z += 64.0f;

        float closestFOV = Config::fAimFov;
        QAngle bestAngle;
        bool foundTarget = false;

        for (int i = 1; i < 64; ++i) {
            __try {
                uintptr_t listEntry = *(uintptr_t*)(entityList + (8LL * (i >> 9)) + 0x10);
                if (!listEntry) continue;

                uintptr_t controller = *(uintptr_t*)(listEntry + Offsets::ENTITY_IDENTITY_SIZE * (i & 0x1FF));
                if (!controller) continue;

                uint32_t pawnHandle = *(uint32_t*)(controller + Offsets::m_hPlayerPawn);
                if (!pawnHandle || pawnHandle == 0xFFFFFFFF) continue;

                uint32_t pawnIdx = pawnHandle & 0x7FFF;
                uintptr_t listEntry2 = *(uintptr_t*)(entityList + (8LL * (pawnIdx >> 9)) + 0x10);
                if (!listEntry2) continue;

                uintptr_t pawn = *(uintptr_t*)(listEntry2 + Offsets::ENTITY_IDENTITY_SIZE * (pawnIdx & 0x1FF));
                if (!pawn || pawn == localPlayerPawn) continue;

                int health = *(int*)(pawn + Offsets::m_iHealth);
                if (health <= 0) continue;

                uint8_t team = *(uint8_t*)(pawn + Offsets::m_iTeamNum);
                if (team <= 0 || team == localTeam) continue;

                Vector3 headPos = Math::GetBonePosition(pawn, 6);
                if (headPos.IsZero()) continue;

                // Calculate angle to target head
                QAngle targetAngle = Math::CalcAngle(eyePos, headPos);
                Math::ClampAngles(targetAngle);

                // FOV check using angular distance (degrees, not game units)
                float fov = Math::GetFovAngle(currentAngles, targetAngle);
                if (fov < closestFOV) {
                    closestFOV = fov;
                    bestAngle = targetAngle;
                    foundTarget = true;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }
        }

        if (foundTarget) {
            // Smooth aim: interpolate from current angles toward target
            float factor = 1.0f / Config::fAimSmooth;

            QAngle smoothed;
            smoothed.pitch = currentAngles.pitch + Math::AngleDifference(bestAngle.pitch, currentAngles.pitch) * factor;
            smoothed.yaw = currentAngles.yaw + Math::AngleDifference(bestAngle.yaw, currentAngles.yaw) * factor;
            smoothed.roll = 0.0f;
            Math::ClampAngles(smoothed);

            __try {
                float* viewAngles = (float*)(client + Offsets::dwViewAngles);
                viewAngles[0] = smoothed.pitch;
                viewAngles[1] = smoothed.yaw;
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
}
