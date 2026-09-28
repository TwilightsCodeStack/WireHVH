#include <w1re/features/utility.hpp>
#include <w1re/config.hpp>
#include <w1re/offsets.hpp>
#include <windows.h>
#include <thread>
#include <cmath>

namespace Features {
    void MatchUtility::AutoJoinDeathmatch() {
        __try {
            // Click DM button in lobby
            // Typically at screen center or specific coordinates
            int screenWidth = GetSystemMetrics(SM_CXSCREEN);
            int screenHeight = GetSystemMetrics(SM_CYSCREEN);
            
            // Move mouse to typical DM button location (varies by resolution)
            int dmButtonX = screenWidth / 2;
            int dmButtonY = screenHeight / 2 + 100;
            
            mouse_event(MOUSEEVENTF_MOVE, dmButtonX, dmButtonY, 0, 0);
            Sleep(100);
            
            // Click (simulating user click on DM button)
            mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
            Sleep(50);
            mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            Sleep(500); // Wait for join animation
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void MatchUtility::AutoRespawn() {
        __try {
            uintptr_t client = (uintptr_t)GetModuleHandleA("client.dll");
            if (!client) return;

            uintptr_t localPlayerPawn = *(uintptr_t*)(client + Offsets::dwLocalPlayerPawn);
            if (!localPlayerPawn) return;

            int health = *(int*)(localPlayerPawn + Offsets::m_iHealth);
            
            // If dead, press spacebar to respawn
            if (health <= 0) {
                keybd_event(VK_SPACE, 0, 0, 0);
                Sleep(50);
                keybd_event(VK_SPACE, 0, KEYEVENTF_KEYUP, 0);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void MatchUtility::MoveTowards(float targetX, float targetY, float targetZ) {
        __try {
            uintptr_t client = (uintptr_t)GetModuleHandleA("client.dll");
            if (!client) return;

            uintptr_t localPlayerPawn = *(uintptr_t*)(client + Offsets::dwLocalPlayerPawn);
            if (!localPlayerPawn) return;

            // Get current position
            struct Vector3 {
                float x, y, z;
            };

            uintptr_t sceneNode = *(uintptr_t*)(localPlayerPawn + Offsets::m_pGameSceneNode);
            if (!sceneNode) return;

            Vector3 currentPos = *(Vector3*)(sceneNode + Offsets::m_vecAbsOrigin);

            // Calculate direction to target
            float dx = targetX - currentPos.x;
            float dy = targetY - currentPos.y;
            float distance = sqrtf(dx * dx + dy * dy);

            if (distance < 50.0f) return; // Already close

            // Normalize direction
            dx /= distance;
            dy /= distance;

            // Move towards target
            if (dx > 0.1f) {
                keybd_event('D', 0, 0, 0); // Strafe right
            } else if (dx < -0.1f) {
                keybd_event('A', 0, 0, 0); // Strafe left
            }

            if (dy > 0.1f) {
                keybd_event('W', 0, 0, 0); // Move forward
            } else if (dy < -0.1f) {
                keybd_event('S', 0, 0, 0); // Move backward
            }

            Sleep(100);

            // Release keys
            keybd_event('W', 0, KEYEVENTF_KEYUP, 0);
            keybd_event('A', 0, KEYEVENTF_KEYUP, 0);
            keybd_event('S', 0, KEYEVENTF_KEYUP, 0);
            keybd_event('D', 0, KEYEVENTF_KEYUP, 0);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}
