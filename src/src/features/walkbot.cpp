#include <w1re/features/walkbot.hpp>
#include <w1re/config.hpp>
#include <w1re/offsets.hpp>
#include <w1re/math.hpp>
#include <w1re/features/raycasting.hpp>
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdlib>

namespace Features {
    static bool s_walkbotActive = false;
    static bool s_wPressed = false;
    static bool s_aPressed = false;
    static bool s_dPressed = false;
    static bool s_sPressed = false;
    static auto s_lastTurn = std::chrono::steady_clock::now();
    static auto s_lastProgress = std::chrono::steady_clock::now();
    static Vector3 s_lastPosition = {};
    static float s_randomYawOffset = 0.0f;
    static float s_wanderYaw = 0.0f;
    static float s_recoveryYaw = 0.0f;
    static bool s_recoveryHeadingSet = false;
    static float s_steeringOffset = 0.0f;
    static bool s_isAvoiding = false;
    static int s_avoidDirection = 1;
    static auto s_recoveryUntil = std::chrono::steady_clock::now();

    static void SetKeyState(char key, bool& state, bool pressed) {
        if (state == pressed)
            return;

        keybd_event(static_cast<BYTE>(key), 0, pressed ? 0 : KEYEVENTF_KEYUP, 0);
        state = pressed;
    }

    static void ReleaseAllKeys() {
        SetKeyState('W', s_wPressed, false);
        SetKeyState('A', s_aPressed, false);
        SetKeyState('D', s_dPressed, false);
        SetKeyState('S', s_sPressed, false);
    }

    void StopWalkbot() {
        ReleaseAllKeys();
        s_walkbotActive = false;
    }

    static void ResetNavigation(const Vector3& position) {
        ReleaseAllKeys();
        s_lastPosition = position;
        s_lastProgress = std::chrono::steady_clock::now();
        s_lastTurn = s_lastProgress;
        s_steeringOffset = 0.0f;
        s_randomYawOffset = 0.0f;
        s_wanderYaw = 0.0f;
        s_recoveryYaw = 0.0f;
        s_recoveryHeadingSet = false;
        s_isAvoiding = false;
        s_avoidDirection = 1;
        s_recoveryUntil = s_lastProgress;
    }

    static bool ReadPosition(uintptr_t pawn, Vector3& position) {
        if (!pawn)
            return false;

        __try {
            uintptr_t sceneNode = *(uintptr_t*)(pawn + Offsets::m_pGameSceneNode);
            if (!sceneNode)
                return false;

            position = *(Vector3*)(sceneNode + Offsets::m_vecAbsOrigin);
            return !position.IsZero();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static bool ReadViewAngles(uintptr_t client, QAngle& angles) {
        __try {
            float* viewAngles = reinterpret_cast<float*>(client + Offsets::dwViewAngles);
            angles.pitch = viewAngles[0];
            angles.yaw = viewAngles[1];
            angles.roll = 0.0f;
            return std::isfinite(angles.pitch) && std::isfinite(angles.yaw);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    static void WriteViewAngles(uintptr_t client, const QAngle& angles) {
        __try {
            float* viewAngles = reinterpret_cast<float*>(client + Offsets::dwViewAngles);
            viewAngles[0] = angles.pitch;
            viewAngles[1] = angles.yaw;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static bool FindTarget(uintptr_t client, uintptr_t entityList, uintptr_t localPawn,
                           uint8_t localTeam, const Vector3& eyePos,
                           Vector3& target, int& targetEntityIndex,
                           bool& targetVisible) {
        targetEntityIndex = -1;
        targetVisible = false;
        float closestDistance = 999999.0f;
        float closestVisibleDistance = 999999.0f;
        Vector3 fallbackTarget = {};
        int fallbackEntityIndex = -1;
        bool found = false;

        for (int i = 1; i < 64; ++i) {
            __try {
                uintptr_t listEntry = *(uintptr_t*)(entityList + (8LL * (i >> 9)) + 0x10);
                if (!listEntry)
                    continue;

                uintptr_t controller = *(uintptr_t*)(
                    listEntry + Offsets::ENTITY_IDENTITY_SIZE * (i & 0x1FF));
                if (!controller)
                    continue;

                uint32_t pawnHandle = *(uint32_t*)(controller + Offsets::m_hPlayerPawn);
                if (!pawnHandle || pawnHandle == 0xFFFFFFFF)
                    continue;

                uint32_t pawnIdx = pawnHandle & 0x7FFF;
                uintptr_t pawnEntry = *(uintptr_t*)(entityList + (8LL * (pawnIdx >> 9)) + 0x10);
                if (!pawnEntry)
                    continue;

                uintptr_t pawn = *(uintptr_t*)(
                    pawnEntry + Offsets::ENTITY_IDENTITY_SIZE * (pawnIdx & 0x1FF));
                if (!pawn || pawn == localPawn)
                    continue;

                int health = *(int*)(pawn + Offsets::m_iHealth);
                uint8_t team = *(uint8_t*)(pawn + Offsets::m_iTeamNum);
                if (health <= 0 || team == 0 || team == localTeam)
                    continue;

                Vector3 headPos = Math::GetBonePosition(pawn, 6);
                if (headPos.IsZero())
                    continue;

                Vector3 delta = headPos - eyePos;
                float distance = delta.Length();
                if (distance > 3000.0f)
                    continue;

                // Hidden players remain navigation targets. Visibility only
                // controls firing, otherwise boxes permanently stop pursuit.
                if (distance < closestDistance) {
                    closestDistance = distance;
                    fallbackTarget = headPos;
                    fallbackEntityIndex = static_cast<int>(pawnIdx);
                    found = true;
                }

                if (RaycastSystem::CanSeePoint(client, eyePos, headPos) &&
                    distance < closestVisibleDistance) {
                    closestVisibleDistance = distance;
                    target = headPos;
                    targetEntityIndex = static_cast<int>(pawnIdx);
                    targetVisible = true;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }
        }

        if (!targetVisible && found) {
            target = fallbackTarget;
            targetEntityIndex = fallbackEntityIndex;
        }

        return found;
    }

    static bool IsTargetUnderCrosshair(uintptr_t localPawn, int targetEntityIndex) {
        if (!localPawn || targetEntityIndex < 0)
            return false;

        __try {
            const int crosshairEntity = *(int*)(localPawn + Offsets::m_iIDEntIndex);
            return crosshairEntity == targetEntityIndex;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    void RunWalkbot() {
        if (!Config::bWalkbot) {
            if (s_walkbotActive) {
                ResetNavigation(s_lastPosition);
                s_walkbotActive = false;
            }
            return;
        }
        s_walkbotActive = true;

        uintptr_t client = (uintptr_t)GetModuleHandleA("client.dll");
        if (!client) return;

        uintptr_t localPlayerPawn = *(uintptr_t*)(client + Offsets::dwLocalPlayerPawn);
        if (!localPlayerPawn) return;

        uintptr_t entityList = *(uintptr_t*)(client + Offsets::dwEntityList);
        if (!entityList) return;

        uint8_t localTeam = 0;
        int localHealth = 0;
        Vector3 localPos = {0, 0, 0};
        
        __try {
            localTeam = *(uint8_t*)(localPlayerPawn + Offsets::m_iTeamNum);
            localHealth = *(int*)(localPlayerPawn + Offsets::m_iHealth);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}

        if (!ReadPosition(localPlayerPawn, localPos))
            return;

        if (!s_walkbotActive) {
            s_walkbotActive = true;
            ResetNavigation(localPos);
        }

        if (localHealth <= 0) {
            ResetNavigation(localPos);
            keybd_event(VK_SPACE, 0, 0, 0);
            keybd_event(VK_SPACE, 0, KEYEVENTF_KEYUP, 0);
            return;
        }

        if (localTeam <= 0) {
            ResetNavigation(localPos);
            return;
        }

        Vector3 eyePos = localPos;
        eyePos.z += 64.0f;

        const auto now = std::chrono::steady_clock::now();
        const float movedDistance = (localPos - s_lastPosition).Length2D();
        if (movedDistance > 3.0f) {
            s_lastPosition = localPos;
            s_lastProgress = now;
        }

        auto stalledFor = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - s_lastProgress).count();
        if (stalledFor > 650 && now >= s_recoveryUntil) {
            // Reverse and rotate long enough to clear a collision hull instead
            // of repeatedly pushing into the same corner.
            s_isAvoiding = true;
            s_avoidDirection = -s_avoidDirection;
            s_steeringOffset = 105.0f * static_cast<float>(s_avoidDirection);
            s_lastProgress = now;
            s_lastTurn = now;
            s_recoveryUntil = now + std::chrono::milliseconds(1050);
            s_recoveryHeadingSet = false;
        }

        Vector3 bestHeadPos = {};
        int targetEntityIndex = -1;
        bool targetVisible = false;
        bool foundTarget = FindTarget(client, entityList, localPlayerPawn, localTeam,
                                      eyePos, bestHeadPos, targetEntityIndex,
                                      targetVisible);

        QAngle currentAngles;
        if (!ReadViewAngles(client, currentAngles))
            return;

        const bool recovering = now < s_recoveryUntil;
        float desiredYaw = currentAngles.yaw;
        if (foundTarget && !recovering) {
            QAngle targetAngle = Math::CalcAngle(eyePos, bestHeadPos);
            desiredYaw = targetAngle.yaw + s_steeringOffset;
        } else {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - s_lastTurn).count();
            int wanderMs = std::max(500, Config::iWalkbotWanderMs);
            if (recovering) {
                if (!s_recoveryHeadingSet) {
                    s_recoveryYaw = Math::NormalizeYaw(
                        currentAngles.yaw + s_steeringOffset);
                    s_recoveryHeadingSet = true;
                }
                desiredYaw = s_recoveryYaw;
            } else {
                if (elapsed > wanderMs || s_wanderYaw == 0.0f) {
                    s_randomYawOffset = static_cast<float>(rand() % 120 - 60);
                    s_wanderYaw = Math::NormalizeYaw(currentAngles.yaw + s_randomYawOffset);
                    s_lastTurn = now;
                }
                desiredYaw = s_wanderYaw;
            }
        }

        desiredYaw = Math::NormalizeYaw(desiredYaw);
        const float yawDelta = Math::AngleDifference(desiredYaw, currentAngles.yaw);
        const float turnFactor = std::clamp(Config::fWalkbotAimSmooth, 0.02f, 1.0f);
        QAngle movementAngles = currentAngles;
        movementAngles.yaw = Math::NormalizeYaw(
            currentAngles.yaw + yawDelta * turnFactor);
        movementAngles.pitch = 0.0f;
        WriteViewAngles(client, movementAngles);

        const float absDelta = std::fabs(yawDelta);
        const bool hardTurn = absDelta > 145.0f;
        const bool recoveryReverse = recovering && stalledFor > 700;
        SetKeyState('W', s_wPressed, !recoveryReverse);
        SetKeyState('S', s_sPressed, recoveryReverse || hardTurn);
        SetKeyState('A', s_aPressed,
                    !s_sPressed && (yawDelta < -38.0f ||
                                    (recovering && s_avoidDirection < 0)));
        SetKeyState('D', s_dPressed,
                    !s_sPressed && (yawDelta > 38.0f ||
                                    (recovering && s_avoidDirection > 0)));

        if (!recovering && s_isAvoiding) {
            s_isAvoiding = false;
            s_steeringOffset = 0.0f;
            s_recoveryHeadingSet = false;
        }

        // Never shoot while steering around an obstacle or before the aim is
        // settled on a currently visible target.
        if (Config::bWalkbotAutoFire && foundTarget && targetVisible && !s_isAvoiding &&
            absDelta < Config::fWalkbotShootFov &&
            IsTargetUnderCrosshair(localPlayerPawn, targetEntityIndex)) {
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
        }
    }
}
