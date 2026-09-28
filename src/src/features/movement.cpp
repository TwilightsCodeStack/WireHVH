#include <w1re/features/movement.hpp>
#include <w1re/config.hpp>
#include <w1re/offsets.hpp>
#include <windows.h>
#include <cstdint>

namespace Features {
    void RunBunnyHop() {
        const bool walkbotChain = Config::bWalkbot && Config::bWalkbotBindBhop;
        if (!Config::bBunnyHop && !walkbotChain) return;
        if (!walkbotChain && !(GetAsyncKeyState(VK_SPACE) & 0x8000)) return;

        uintptr_t client = (uintptr_t)GetModuleHandleA("client.dll");
        if (!client) return;

        uintptr_t localPlayerPawn = *(uintptr_t*)(client + Offsets::dwLocalPlayerPawn);
        if (!localPlayerPawn) return;

        uint32_t fFlags = 0;
        __try {
            fFlags = *(uint32_t*)(localPlayerPawn + Offsets::m_fFlags);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return;
        }

        constexpr uint32_t FL_ONGROUND = 1u;
        const bool onGround = (fFlags & FL_ONGROUND) != 0;

        __try {
            volatile uint32_t* forceJump =
                reinterpret_cast<volatile uint32_t*>(client + Offsets::dwForceJump);
            *forceJump = onGround ? 65537u : 256u;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return;
        }
    }
}
