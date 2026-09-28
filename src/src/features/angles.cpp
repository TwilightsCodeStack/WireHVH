#include <w1re/features/angles.hpp>
#include <w1re/config.hpp>
#include <w1re/offsets.hpp>
#include <w1re/math.hpp>
#include <windows.h>
#include <cstdint>

namespace Features {
    namespace {
        uintptr_t g_cameraEntity = 0;
        int g_originalCameraMode = 0;
        Vector3 g_originalCameraOffset;
        uint8_t g_originalClipCameraOffset = 0;
        bool g_cameraPatched = false;

        bool ReadEntity(uintptr_t entityList, int index, uintptr_t* entity) {
            if (!entity)
                return false;
            __try {
                uintptr_t entry = *(uintptr_t*)(entityList + 0x8LL * (index >> 9) + 0x10);
                if (!entry)
                    return false;
                *entity = *(uintptr_t*)(entry + Offsets::ENTITY_IDENTITY_SIZE * (index & 0x1FF));
                return *entity != 0;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        bool WriteAngles(uintptr_t client, const QAngle& angles) {
            __try {
                float* viewAngles = reinterpret_cast<float*>(client + Offsets::dwViewAngles);
                viewAngles[0] = angles.pitch;
                viewAngles[1] = angles.yaw;
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }
    }

    void RunAngleOverrides() {
        if (!Config::bAntiAim && !Config::bSpin)
            return;

        const uintptr_t client = reinterpret_cast<uintptr_t>(GetModuleHandleA("client.dll"));
        if (!client)
            return;

        QAngle angles;
        __try {
            const float* viewAngles = reinterpret_cast<const float*>(client + Offsets::dwViewAngles);
            angles.pitch = viewAngles[0];
            angles.yaw = viewAngles[1];
            angles.roll = 0.0f;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return;
        }

        if (Config::bAntiAim) {
            angles.pitch = Config::fPitchAngle;
            angles.yaw = Config::fYawAngle;
        }

        if (Config::bSpin) {
            const double seconds = static_cast<double>(GetTickCount64()) / 1000.0;
            angles.yaw = Math::NormalizeYaw(
                static_cast<float>(seconds * Config::fSpinSpeed));
        }

        Math::ClampAngles(angles);
        if (!WriteAngles(client, angles)) {
            Config::bAntiAim = false;
            Config::bSpin = false;
        }
    }

    void ApplyThirdPerson() {
        if (!Config::bThirdPersonEnabled) {
            if (g_cameraPatched && g_cameraEntity) {
                __try {
                    *reinterpret_cast<int*>(g_cameraEntity + Offsets::m_nCameraMode) =
                        g_originalCameraMode;
                    *reinterpret_cast<Vector3*>(g_cameraEntity + Offsets::m_vecCameraOffset) =
                        g_originalCameraOffset;
                    *reinterpret_cast<uint8_t*>(g_cameraEntity + Offsets::m_bClipCameraOffset) =
                        g_originalClipCameraOffset;
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                }
                g_cameraEntity = 0;
                g_cameraPatched = false;
            }
            return;
        }

        const uintptr_t client = reinterpret_cast<uintptr_t>(GetModuleHandleA("client.dll"));
        if (!client)
            return;

        __try {
            const uintptr_t pawn = *(uintptr_t*)(client + Offsets::dwLocalPlayerPawn);
            const uintptr_t controller = *(uintptr_t*)(client + Offsets::dwLocalPlayerController);
            const uintptr_t entityList = *(uintptr_t*)(client + Offsets::dwEntityList);
            if (!pawn || !controller || !entityList)
                return;

            const uint32_t pawnHandle =
                *(uint32_t*)(controller + Offsets::m_hPlayerPawn);
            if (!pawnHandle || pawnHandle == 0xFFFFFFFFu)
                return;

            const int pawnIndex = static_cast<int>(pawnHandle & 0x7FFFu);
            for (int index = 0; index < 2048; ++index) {
                uintptr_t camera = 0;
                if (!ReadEntity(entityList, index, &camera))
                    continue;

                const uint32_t cameraPawn =
                    *(uint32_t*)(camera + Offsets::m_hPlayerPawnCamera);
                if (!cameraPawn || cameraPawn == 0xFFFFFFFFu)
                    continue;

                uintptr_t cameraPawnEntity = 0;
                if (!ReadEntity(entityList, static_cast<int>(cameraPawn & 0x7FFFu),
                                &cameraPawnEntity) ||
                    cameraPawnEntity != pawn ||
                    static_cast<int>(cameraPawn & 0x7FFFu) != pawnIndex)
                    continue;

                if (!g_cameraPatched || g_cameraEntity != camera) {
                    g_cameraEntity = camera;
                    g_originalCameraMode =
                        *reinterpret_cast<int*>(camera + Offsets::m_nCameraMode);
                    g_originalCameraOffset =
                        *reinterpret_cast<Vector3*>(camera + Offsets::m_vecCameraOffset);
                    g_originalClipCameraOffset =
                        *reinterpret_cast<uint8_t*>(camera + Offsets::m_bClipCameraOffset);
                    g_cameraPatched = true;
                }

                *reinterpret_cast<int*>(camera + Offsets::m_nCameraMode) = 1;
                *reinterpret_cast<Vector3*>(camera + Offsets::m_vecCameraOffset) =
                    Vector3(0.0f, 0.0f, Config::fThirdPersonDistance);
                *reinterpret_cast<uint8_t*>(camera + Offsets::m_bClipCameraOffset) = 1;
                return;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_cameraEntity = 0;
            g_cameraPatched = false;
            Config::bThirdPersonEnabled = false;
        }
    }
}
