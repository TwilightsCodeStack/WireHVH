#include <w1re/features/farm.hpp>
#include <w1re/offsets.hpp>
#include <w1re/config.hpp>
#include <windows.h>
#include <chrono>

namespace Features {
    namespace {
        bool ReadLocalPawnAndHealth(uintptr_t client, uintptr_t* localPlayerPawn, int* health) {
            __try {
                *localPlayerPawn = *(uintptr_t*)(client + Offsets::dwLocalPlayerPawn);
                if (!*localPlayerPawn) return false;
                *health = *(int*)(*localPlayerPawn + Offsets::m_iHealth);
                return true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        void ApplyAntiAfkForceMove(uintptr_t client, int* movePhase) {
            __try {
                uint32_t* forceLeft = (uint32_t*)(client + Offsets::dwForceLeft);
                uint32_t* forceRight = (uint32_t*)(client + Offsets::dwForceRight);

                if (*movePhase == 0) {
                    *forceLeft = 65537;
                    *movePhase = 1;
                } else {
                    *forceLeft = 256;
                    *forceRight = 65537;
                    *movePhase = 2;
                }

                if (*movePhase == 2) {
                    *forceRight = 256;
                    *movePhase = 0;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    void RunAutoFarm() {
        if (!Config::bAutoFarm) return;

        uintptr_t client = (uintptr_t)GetModuleHandleA("client.dll");
        if (!client) return;

        uintptr_t localPlayerPawn = 0;
        int health = 0;
        if (!ReadLocalPawnAndHealth(client, &localPlayerPawn, &health))
            return;

        auto now = std::chrono::steady_clock::now();

        if (Config::bAutoRespawn && health <= 0) {
            static auto lastRespawn = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - lastRespawn).count() > 2) {
                mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                lastRespawn = now;
            }
        }

        if (Config::bAntiAfk && health > 0) {
            static auto lastMove = std::chrono::steady_clock::now();
            static int movePhase = 0;
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastMove).count();

            if (elapsed > 25) {
                int phase = movePhase;
                ApplyAntiAfkForceMove(client, &phase);
                movePhase = phase;
                if (movePhase == 0)
                    lastMove = now;
            }
        }
    }
}
