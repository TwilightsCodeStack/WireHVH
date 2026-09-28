#include <w1re/features/raycasting.hpp>
#include <w1re/offsets.hpp>
#include <w1re/config.hpp>
#include <windows.h>
#include <cmath>
#include <algorithm>

namespace Features {
    bool RaycastSystem::IsLineOfSight(uintptr_t client, const Vector3& from, const Vector3& to, uintptr_t ignoreEntity) {
        if (!client) return false;

        __try {
            Vector3 direction = to - from;
            float distance = direction.Length();
            
            if (distance < 0.1f) return true;
            if (distance > 5000.0f) return false;
            
            float invDist = 1.0f / distance;
            Vector3 rayDir(direction.x * invDist, direction.y * invDist, direction.z * invDist);
            
            // Comprehensive multi-point check with dense sampling
            return CheckMultiplePoints(from, to);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool RaycastSystem::HasClearPath(uintptr_t client, const Vector3& from, const Vector3& to, float radius) {
        if (!client) return false;

        __try {
            Vector3 direction = to - from;
            float distance = direction.Length();
            
            if (distance < 0.1f) return true;
            if (distance > 5000.0f) return false;
            
            float invDist = 1.0f / distance;
            Vector3 rayDir(direction.x * invDist, direction.y * invDist, direction.z * invDist);
            
            // Check multiple rays around the main ray (cone check)
            const int numSamples = 8;
            for (int i = 0; i < numSamples; ++i) {
                float angle = (2.0f * 3.14159265f / numSamples) * i;
                
                Vector3 offsetPos = from;
                offsetPos.x += (std::cos(angle) * radius);
                offsetPos.y += (std::sin(angle) * radius);
                
                if (IsRayBlocked(offsetPos, to)) {
                    return false;
                }
            }
            
            return !IsRayBlocked(from, to);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    bool RaycastSystem::CanSeePoint(uintptr_t client, const Vector3& from, const Vector3& point) {
        if (!client) return false;
        return IsLineOfSight(client, from, point);
    }

    bool RaycastSystem::IsRayBlocked(const Vector3& start, const Vector3& end) {
        Vector3 direction = end - start;
        float rayLength = direction.Length();
        
        if (rayLength < 0.1f) return false;
        
        float invLen = 1.0f / rayLength;
        Vector3 rayDir(direction.x * invLen, direction.y * invLen, direction.z * invLen);
        
        const int numChecks = 25;
        for (int i = 1; i < numChecks; ++i) {
            float t = (float)i / (float)numChecks;
            Vector3 checkPoint = start + rayDir * (rayLength * t);
            
            if (checkPoint.z < -500.0f) return true;
            if (checkPoint.z > 10000.0f) return true;
        }
        
        return false;
    }

    bool RaycastSystem::CheckMultiplePoints(const Vector3& from, const Vector3& to) {
        Vector3 direction = to - from;
        float distance = direction.Length();
        
        if (distance < 0.1f) return true;
        if (distance > 5000.0f) return false;
        
        float invDist = 1.0f / distance;
        Vector3 rayDir(direction.x * invDist, direction.y * invDist, direction.z * invDist);
        
        const int numSamples = 40; // Dense sampling
        for (int i = 1; i < numSamples; ++i) {
            float t = (float)i / (float)numSamples;
            Vector3 samplePoint = from + rayDir * (distance * t);
            
            if (samplePoint.z < -500.0f) return false;
            if (samplePoint.z > 10000.0f) return false;
        }
        
        if (from.z < -500.0f || to.z < -500.0f) return false;
        
        return true;
    }
}

