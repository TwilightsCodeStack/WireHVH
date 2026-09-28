#include <w1re/math.hpp>
#include <w1re/offsets.hpp>
#include <imgui.h>
#include <w1re/config.hpp>
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cmath>

namespace Features {
    void RenderESP() {
        if (!Config::bEsp) return;

        ImGuiIO& io = ImGui::GetIO();
        int width = (int)io.DisplaySize.x;
        int height = (int)io.DisplaySize.y;

        uintptr_t client = (uintptr_t)GetModuleHandleA("client.dll");
        if (!client) return;

        uintptr_t entityList = *(uintptr_t*)(client + Offsets::dwEntityList);
        if (!entityList) return;

        uintptr_t localPlayerPawn = *(uintptr_t*)(client + Offsets::dwLocalPlayerPawn);
        if (!localPlayerPawn) return;

        uint8_t localTeam = 0;
        Vector3 localPos = {0, 0, 0};
        __try {
            localTeam = *(uint8_t*)(localPlayerPawn + Offsets::m_iTeamNum);
            uintptr_t sn = *(uintptr_t*)(localPlayerPawn + Offsets::m_pGameSceneNode);
            if (sn) localPos = *(Vector3*)(sn + Offsets::m_vecAbsOrigin);
        } __except (EXCEPTION_EXECUTE_HANDLER) { return; }

        if (localTeam <= 0) return;

        Matrix4x4 viewMatrix;
        __try {
            viewMatrix = *(Matrix4x4*)(client + Offsets::dwViewMatrix);
        } __except (EXCEPTION_EXECUTE_HANDLER) { return; }

        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        if (!draw) return;

        BoneConnection skeletonBones[] = {
            {6, 5}, {5, 4}, {4, 0},       // head → neck → spine → pelvis
            {5, 8}, {8, 9}, {9, 10},       // left arm
            {5, 13}, {13, 14}, {14, 15},   // right arm
            {0, 22}, {22, 23}, {23, 24},   // left leg
            {0, 25}, {25, 26}, {26, 27}    // right leg
        };
        int boneCount = 15;

        // FOV circle
        if (Config::bDrawFov && Config::bAimAssist) {
            float cx = width / 2.0f;
            float cy = height / 2.0f;
            // Approximate pixel radius from FOV angle
            float fovPixels = (Config::fAimFov / 90.0f) * (width / 2.0f);
            draw->AddCircle(ImVec2(cx, cy), fovPixels, IM_COL32(160, 80, 255, 100), 64, 1.5f);
        }

        for (int i = 1; i <= 64; ++i) {
            __try {
                uintptr_t listEntry = *(uintptr_t*)(entityList + (8LL * (i >> 9)) + 0x10);
                if (!listEntry) continue;

                uintptr_t controller = *(uintptr_t*)(listEntry + Offsets::ENTITY_IDENTITY_SIZE * (i & 0x1FF));
                if (!controller) continue;

                uint32_t pawnHandle = *(uint32_t*)(controller + Offsets::m_hPlayerPawn);
                if (!pawnHandle || pawnHandle == 0xFFFFFFFF) continue;

                uint32_t pawnIdx = pawnHandle & 0x7FFF;
                uintptr_t listEntry2 = *(uintptr_t*)(entityList + (8LL * (pawnIdx >> 9)) + 0x10);
                if (!listEntry2) continue;

                uintptr_t pawn = *(uintptr_t*)(listEntry2 + Offsets::ENTITY_IDENTITY_SIZE * (pawnIdx & 0x1FF));
                if (!pawn || pawn == localPlayerPawn) continue;

                int health = *(int*)(pawn + Offsets::m_iHealth);
                if (health <= 0 || health > 200) continue;

                uint8_t team = *(uint8_t*)(pawn + Offsets::m_iTeamNum);
                if (team <= 0 || team == localTeam) continue;

                uintptr_t sceneNode = *(uintptr_t*)(pawn + Offsets::m_pGameSceneNode);
                if (!sceneNode) continue;

                Vector3 absOrigin = *(Vector3*)(sceneNode + Offsets::m_vecAbsOrigin);
                Vector3 headPos = Math::GetBonePosition(pawn, 6);
                if (headPos.IsZero()) continue;

                // Add some height above head for the top of the box
                Vector3 headTop = headPos;
                headTop.z += 8.0f;

                Vector3 screenPos, screenHead;
                if (!Math::WorldToScreen(absOrigin, screenPos, viewMatrix, width, height)) continue;
                if (!Math::WorldToScreen(headTop, screenHead, viewMatrix, width, height)) continue;

                float h = std::abs(screenPos.y - screenHead.y);
                float w = h / 2.2f;
                float x = screenHead.x - w / 2.0f;
                float y = screenHead.y;

                // Color based on health
                int r = (int)((1.0f - health / 100.0f) * 255);
                int g = (int)((health / 100.0f) * 255);
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                ImU32 healthColor = IM_COL32(r, g, 0, 255);

                // Box ESP
                if (Config::bEspBox) {
                    // Outline
                    draw->AddRect(ImVec2(x - 1, y - 1), ImVec2(x + w + 1, y + h + 1), IM_COL32(0, 0, 0, 180), 0.0f, 0, 3.0f);
                    // Inner box — purple accent
                    draw->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(160, 80, 255, 255), 0.0f, 0, 1.5f);
                }

                // Health bar (left side)
                if (Config::bEspHealth) {
                    float healthFrac = health / 100.0f;
                    if (healthFrac > 1.0f) healthFrac = 1.0f;
                    
                    float barX = x - 7.0f;
                    // Background
                    draw->AddRectFilled(ImVec2(barX - 1, y - 1), ImVec2(barX + 3, y + h + 1), IM_COL32(0, 0, 0, 180));
                    // Health fill
                    draw->AddRectFilled(
                        ImVec2(barX, y + h - (h * healthFrac)),
                        ImVec2(barX + 2, y + h),
                        healthColor
                    );
                }

                // Skeleton ESP
                if (Config::bEspSkeleton) {
                    for (int b = 0; b < boneCount; ++b) {
                        BoneConnection bc = skeletonBones[b];
                        Vector3 bone1Pos = Math::GetBonePosition(pawn, bc.bone1);
                        Vector3 bone2Pos = Math::GetBonePosition(pawn, bc.bone2);
                        if (bone1Pos.IsZero() || bone2Pos.IsZero()) continue;

                        Vector3 s1, s2;
                        if (Math::WorldToScreen(bone1Pos, s1, viewMatrix, width, height) &&
                            Math::WorldToScreen(bone2Pos, s2, viewMatrix, width, height)) {
                            draw->AddLine(ImVec2(s1.x, s1.y), ImVec2(s2.x, s2.y), IM_COL32(200, 160, 255, 200), 1.5f);
                        }
                    }
                }

                // Player name
                if (Config::bEspName) {
                    char nameBuf[128] = "???";
                    __try {
                        uintptr_t namePtr = *(uintptr_t*)(controller + Offsets::m_sSanitizedPlayerName);
                        if (namePtr) {
                            // Read up to 31 chars, null-terminate
                            for (int c = 0; c < 31; ++c) {
                                nameBuf[c] = *(char*)(namePtr + c);
                                if (nameBuf[c] == '\0') break;
                            }
                            nameBuf[31] = '\0';
                        }
                    } __except (EXCEPTION_EXECUTE_HANDLER) {
                        snprintf(nameBuf, sizeof(nameBuf), "Player %d", i);
                    }

                    ImVec2 textSize = ImGui::CalcTextSize(nameBuf);
                    float nameX = screenHead.x - textSize.x / 2.0f;
                    draw->AddText(ImVec2(nameX + 1, y - 16.0f), IM_COL32(0, 0, 0, 200), nameBuf);
                    draw->AddText(ImVec2(nameX, y - 17.0f), IM_COL32(255, 255, 255, 255), nameBuf);
                }

                // Distance
                if (Config::bEspDistance) {
                    Vector3 diff = absOrigin - localPos;
                    float dist = diff.Length() / 100.0f; // Convert to approximate meters
                    char distBuf[32];
                    snprintf(distBuf, sizeof(distBuf), "%.0fm", dist);
                    ImVec2 textSize = ImGui::CalcTextSize(distBuf);
                    float distX = screenHead.x - textSize.x / 2.0f;
                    draw->AddText(ImVec2(distX, y + h + 4.0f), IM_COL32(200, 200, 200, 200), distBuf);
                }

                // Snaplines
                if (Config::bEspSnaplines) {
                    draw->AddLine(
                        ImVec2((float)(width / 2), (float)height),
                        ImVec2(screenPos.x, screenPos.y),
                        IM_COL32(160, 80, 255, 80), 1.0f
                    );
                }

            } __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }
        }
    }
}
