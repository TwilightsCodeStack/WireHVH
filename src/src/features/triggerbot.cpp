#include <w1re/offsets.hpp>
#include <w1re/config.hpp>
#include <windows.h>
#include <cstdint>
#include <chrono>

namespace Features {
    namespace {
        bool ReadTriggerbotState(uint8_t* localTeam, int* entIndex, uint8_t* entTeam, int* health) {
            __try {
                uintptr_t client = (uintptr_t)GetModuleHandleA("client.dll");
                if (!client) return false;

                uintptr_t localPlayerPawn = *(uintptr_t*)(client + Offsets::dwLocalPlayerPawn);
                if (!localPlayerPawn) return false;

                *localTeam = *(uint8_t*)(localPlayerPawn + Offsets::m_iTeamNum);
                if (*localTeam <= 0) return false;

                *entIndex = *(int*)(localPlayerPawn + Offsets::m_iIDEntIndex);
                if (*entIndex <= 0) return false;

                uintptr_t entityList = *(uintptr_t*)(client + Offsets::dwEntityList);
                if (!entityList) return false;

                uintptr_t listEntry = *(uintptr_t*)(entityList + 0x8 * (*entIndex >> 9) + 0x10);
                if (!listEntry) return false;

                uintptr_t pEntity = *(uintptr_t*)(listEntry + Offsets::ENTITY_IDENTITY_SIZE * (*entIndex & 0x1FF));
                if (!pEntity) return false;

                *entTeam = *(uint8_t*)(pEntity + Offsets::m_iTeamNum);
                *health = *(int*)(pEntity + Offsets::m_iHealth);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }
    }

    void RunTriggerbot() {
        if (!Config::bTriggerbot) return;

        static auto lastFireTime = std::chrono::steady_clock::now();
        static bool waitingDelay = false;

        uint8_t localTeam = 0, entTeam = 0;
        int entIndex = 0, health = 0;
        if (!ReadTriggerbotState(&localTeam, &entIndex, &entTeam, &health))
            return;

        if ((Config::bTriggerTeam || localTeam != entTeam) && health > 0) {
            auto now = std::chrono::steady_clock::now();

            if (!waitingDelay) {
                waitingDelay = true;
                lastFireTime = now;
                return;
            }

            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFireTime).count();
            if (elapsed < Config::iTriggerDelay)
                return;

            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            waitingDelay = false;
        } else {
            waitingDelay = false;
        }
    }
}
