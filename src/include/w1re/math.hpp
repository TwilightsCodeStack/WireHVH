#pragma once
#include <windows.h>
#include <cmath>
#include <w1re/offsets.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct Vector3 {
    float x, y, z;
    
    Vector3() : x(0), y(0), z(0) {}
    Vector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}
    
    Vector3 operator-(const Vector3& o) const { return Vector3(x - o.x, y - o.y, z - o.z); }
    Vector3 operator+(const Vector3& o) const { return Vector3(x + o.x, y + o.y, z + o.z); }
    Vector3 operator*(float s) const { return Vector3(x * s, y * s, z * s); }
    
    float Length() const { return std::sqrtf(x * x + y * y + z * z); }
    float Length2D() const { return std::sqrtf(x * x + y * y); }
    bool IsZero() const { return x == 0.f && y == 0.f && z == 0.f; }
};

struct Vector4 {
    float x, y, z, w;
};

struct Matrix4x4 {
    float m[4][4];
};

struct BoneEntry {
    float px, py, pz, pad;
    float qx, qy, qz, qw;
};

struct BoneConnection {
    int bone1, bone2;
};

struct QAngle {
    float pitch, yaw, roll;
    QAngle() : pitch(0), yaw(0), roll(0) {}
    QAngle(float p, float y, float r) : pitch(p), yaw(y), roll(r) {}
};

namespace Math {
    inline Vector3 GetBonePosition(uintptr_t pawn, int boneId) {
        if (!pawn) return Vector3(0, 0, 0);

        __try {
            uintptr_t sceneNode = *(uintptr_t*)(pawn + Offsets::m_pGameSceneNode);
            if (!sceneNode) return Vector3(0, 0, 0);

            uintptr_t boneArray = *(uintptr_t*)(sceneNode + Offsets::m_modelState + Offsets::m_BoneArray);
            if (!boneArray) return Vector3(0, 0, 0);

            BoneEntry bone = *(BoneEntry*)(boneArray + (boneId * sizeof(BoneEntry)));
            return Vector3(bone.px, bone.py, bone.pz);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return Vector3(0, 0, 0);
        }
    }

    inline bool WorldToScreen(const Vector3& pos, Vector3& screen, const Matrix4x4& viewMatrix, int width, int height) {
        float w = viewMatrix.m[3][0] * pos.x + viewMatrix.m[3][1] * pos.y + viewMatrix.m[3][2] * pos.z + viewMatrix.m[3][3];

        if (w < 0.01f)
            return false;

        float invW = 1.0f / w;
        screen.x = (viewMatrix.m[0][0] * pos.x + viewMatrix.m[0][1] * pos.y + viewMatrix.m[0][2] * pos.z + viewMatrix.m[0][3]) * invW;
        screen.y = (viewMatrix.m[1][0] * pos.x + viewMatrix.m[1][1] * pos.y + viewMatrix.m[1][2] * pos.z + viewMatrix.m[1][3]) * invW;

        screen.x = (width / 2.0f) + (screen.x * width / 2.0f);
        screen.y = (height / 2.0f) - (screen.y * height / 2.0f);

        return true;
    }

    inline QAngle CalcAngle(const Vector3& src, const Vector3& dst) {
        Vector3 d = dst - src;
        float hyp = std::sqrt(d.x * d.x + d.y * d.y);
        QAngle angle;
        angle.pitch = -std::atan2f(d.z, hyp) * (180.0f / (float)M_PI);
        angle.yaw = std::atan2f(d.y, d.x) * (180.0f / (float)M_PI);
        angle.roll = 0.0f;
        return angle;
    }

    inline void ClampAngles(QAngle& a) {
        if (a.pitch > 89.f) a.pitch = 89.f;
        if (a.pitch < -89.f) a.pitch = -89.f;
        while (a.yaw > 180.f) a.yaw -= 360.f;
        while (a.yaw < -180.f) a.yaw += 360.f;
        a.roll = 0.f;
    }

    inline float NormalizeYaw(float yaw) {
        while (yaw > 180.f) yaw -= 360.f;
        while (yaw < -180.f) yaw += 360.f;
        return yaw;
    }

    inline float AngleDifference(float a, float b) {
        float diff = a - b;
        while (diff > 180.f) diff -= 360.f;
        while (diff < -180.f) diff += 360.f;
        return diff;
    }

    // Get angular FOV distance between current view and a target angle
    inline float GetFovAngle(const QAngle& viewAngle, const QAngle& targetAngle) {
        float pitchDiff = AngleDifference(targetAngle.pitch, viewAngle.pitch);
        float yawDiff = AngleDifference(targetAngle.yaw, viewAngle.yaw);
        return std::sqrtf(pitchDiff * pitchDiff + yawDiff * yawDiff);
    }
}
