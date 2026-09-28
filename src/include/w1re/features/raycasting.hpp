#pragma once
#include <cstdint>
#include <w1re/math.hpp>

namespace Features {
    // Trace result structure
    struct TraceResult {
        bool hit;
        Vector3 hitPos;
        uintptr_t hitEntity;
        float distance;
    };

    // Line-of-sight check using raycasting
    class RaycastSystem {
    public:
        // Check if there's a clear line of sight between two points
        // Uses multiple checks to ensure NO obstacles block the path
        static bool IsLineOfSight(uintptr_t client, const Vector3& from, const Vector3& to, uintptr_t ignoreEntity = 0);

        // Comprehensive check: tests multiple points along ray
        // Returns true ONLY if path is completely clear
        static bool HasClearPath(uintptr_t client, const Vector3& from, const Vector3& to, float radius = 5.0f);

        // Check specific point visibility with entity filtering
        static bool CanSeePoint(uintptr_t client, const Vector3& from, const Vector3& point);

    private:
        // Test if a ray segment intersects with any solid entity
        static bool IsRayBlocked(const Vector3& start, const Vector3& end);

        // Multiple detailed checks along the path
        static bool CheckMultiplePoints(const Vector3& from, const Vector3& to);
    };
}
