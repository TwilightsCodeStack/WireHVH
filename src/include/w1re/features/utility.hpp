#pragma once
#include <windows.h>
#include <cstdint>

namespace Features {
    class MatchUtility {
    public:
        // Auto-join deathmatch lobby
        static void AutoJoinDeathmatch();

        // Auto-respawn when dead
        static void AutoRespawn();

        // Move towards target location (bot pathfinding)
        static void MoveTowards(float targetX, float targetY, float targetZ);
    };
}
