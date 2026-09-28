#pragma once
#include <imgui.h>

// Decoration only: native ImGui widgets retain their IDs, bounds and input behavior.
namespace WorkspaceStyle {
    inline void ApplyPalette(ImGuiStyle& style) {
        style.WindowRounding = 12;
        style.FrameRounding = 6;
        style.GrabRounding = 5;
        style.FrameBorderSize = 1;
        auto* c = style.Colors;
        c[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_Text] = ImVec4(.85f, .84f, .91f, 1);
        c[ImGuiCol_TextDisabled] = ImVec4(.49f, .47f, .59f, 1);
        c[ImGuiCol_Border] = ImVec4(.65f, .57f, .83f, .16f);
        c[ImGuiCol_PopupBg] = ImVec4(.065f, .058f, .09f, .98f);
        c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = ImVec4(.085f, .072f, .12f, .72f);
        c[ImGuiCol_FrameBg] = ImVec4(.14f, .12f, .20f, .60f);
        c[ImGuiCol_FrameBgHovered] = ImVec4(.23f, .18f, .33f, .72f);
        c[ImGuiCol_FrameBgActive] = ImVec4(.28f, .21f, .40f, .78f);
        c[ImGuiCol_Button] = ImVec4(.25f, .19f, .35f, .60f);
        c[ImGuiCol_ButtonHovered] = ImVec4(.37f, .27f, .53f, .80f);
        c[ImGuiCol_ButtonActive] = ImVec4(.44f, .32f, .63f, .90f);
        c[ImGuiCol_Header] = ImVec4(.40f, .29f, .57f, .35f);
        c[ImGuiCol_HeaderHovered] = ImVec4(.42f, .32f, .58f, .40f);
        c[ImGuiCol_HeaderActive] = ImVec4(.48f, .35f, .66f, .50f);
        c[ImGuiCol_CheckMark] = ImVec4(.81f, .69f, 1, 1);
        c[ImGuiCol_SliderGrab] = ImVec4(.70f, .54f, .94f, 1);
        c[ImGuiCol_SliderGrabActive] = ImVec4(.85f, .73f, 1, 1);
        c[ImGuiCol_Separator] = ImVec4(.65f, .57f, .82f, .15f);
        c[ImGuiCol_ScrollbarBg] = ImVec4(.06f, .05f, .09f, .20f);
        c[ImGuiCol_ScrollbarGrab] = ImVec4(.49f, .40f, .64f, .35f);
        c[ImGuiCol_TextSelectedBg] = ImVec4(.60f, .43f, .85f, .35f);
        c[ImGuiCol_NavCursor] = ImVec4(.78f, .65f, 1, .80f);
    }

    inline void Glow(ImDrawList* draw, ImVec2 a, ImVec2 b, float radius, float scale, float strength) {
        for (int i = 12; i > 0; --i) {
            const float spread = i * 1.7f * scale;
            draw->AddRect(ImVec2(a.x - spread, a.y - spread), ImVec2(b.x + spread, b.y + spread),
                IM_COL32(157, 112, 230, static_cast<int>((13 - i) * strength)), radius + spread, 0, 2 * scale);
        }
    }

    inline void FinishControl(float scale, bool selected = false) {
        auto* draw = ImGui::GetWindowDrawList();
        const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        const bool active = ImGui::IsItemActive(), hover = ImGui::IsItemHovered();
        if (hover || active) Glow(draw, a, b, 6 * scale, scale * .28f, active ? 1.5f : .8f);
        draw->AddRect(a, b, IM_COL32(192, 164, 239, active ? 130 : (hover ? 90 : (selected ? 65 : 27))), 6 * scale);
        draw->AddLine(ImVec2(a.x + 7 * scale, a.y + scale), ImVec2(b.x - 7 * scale, a.y + scale),
            IM_COL32(222, 205, 255, hover ? 36 : 16));
    }
}
