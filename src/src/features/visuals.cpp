#include <w1re/features/visuals.hpp>
#include <w1re/config.hpp>
#include <w1re/offsets.hpp>
#include <windows.h>
#include <cstdint>

namespace Features {
    namespace {
        uint32_t PackColor(const float color[3]) {
            const auto channel = [](float value) -> uint32_t {
                if (value < 0.0f) value = 0.0f;
                if (value > 1.0f) value = 1.0f;
                return static_cast<uint32_t>(value * 255.0f + 0.5f);
            };
            return channel(color[0]) |
                   (channel(color[1]) << 8) |
                   (channel(color[2]) << 16) |
                   0xFF000000u;
        }

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
    }

    void ApplyVisualOverrides() {
        if (!Config::bGlowEnabled && !Config::bChamsEnabled)
            return;

        const uintptr_t client = reinterpret_cast<uintptr_t>(GetModuleHandleA("client.dll"));
        if (!client)
            return;

        __try {
            const uintptr_t entityList = *(uintptr_t*)(client + Offsets::dwEntityList);
            const uintptr_t localPawn = *(uintptr_t*)(client + Offsets::dwLocalPlayerPawn);
            if (!entityList || !localPawn)
                return;

            const uint8_t localTeam = *(uint8_t*)(localPawn + Offsets::m_iTeamNum);
            const uint32_t visibleColor = PackColor(Config::fChamsVisibleColor);
            const uint32_t hiddenColor = PackColor(Config::fChamsHiddenColor);

            for (int index = 1; index < 64; ++index) {
                uintptr_t controller = 0;
                if (!ReadEntity(entityList, index, &controller) || controller == localPawn)
                    continue;

                const uint32_t handle = *(uint32_t*)(controller + Offsets::m_hPlayerPawn);
                if (!handle || handle == 0xFFFFFFFFu)
                    continue;

                uintptr_t pawn = 0;
                if (!ReadEntity(entityList, static_cast<int>(handle & 0x7FFFu), &pawn) || !pawn)
                    continue;

                const int health = *(int*)(pawn + Offsets::m_iHealth);
                const uint8_t team = *(uint8_t*)(pawn + Offsets::m_iTeamNum);
                if (health <= 0 || team <= 0 || team == localTeam)
                    continue;

                if (Config::bGlowEnabled) {
                    *reinterpret_cast<uint8_t*>(pawn + Offsets::m_bGlowing) = 1;
                    *reinterpret_cast<uint32_t*>(pawn + Offsets::m_glowColorOverride) = visibleColor;
                }

                if (Config::bChamsEnabled) {
                    *reinterpret_cast<uint8_t*>(pawn + Offsets::m_bUseClientOverrideTint) = 1;
                    *reinterpret_cast<uint32_t*>(pawn + Offsets::m_ClientOverrideTint) =
                        Config::bChamsXQZ ? hiddenColor : visibleColor;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // The caller isolates this feature from the Present hook.
        }
    }

    bool HasCameraServices() {
        const uintptr_t client = reinterpret_cast<uintptr_t>(GetModuleHandleA("client.dll"));
        if (!client)
            return false;

        __try {
            const uintptr_t pawn = *(uintptr_t*)(client + Offsets::dwLocalPlayerPawn);
            return pawn && *(uintptr_t*)(pawn + Offsets::m_pCameraServices);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
}
