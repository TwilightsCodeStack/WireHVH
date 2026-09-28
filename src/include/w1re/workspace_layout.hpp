#pragma once
#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace WorkspaceLayout {
    struct Metrics { float scale; ImVec2 minimum, maximum, initial; };
    inline Metrics Calculate(ImVec2 display, float preference, float dpi) {
        const float requested = std::clamp(std::isfinite(preference) ? preference : 1.0f, 0.75f, 1.5f);
        dpi = std::clamp(std::isfinite(dpi) ? dpi : 1.0f, 1.0f, 2.5f);
        ImVec2 maximum(std::max(1.0f, display.x - 24), std::max(1.0f, display.y - 24));
        const float scale = std::min({requested * dpi, maximum.x / 680, maximum.y / 440});
        return {scale, ImVec2(680 * scale, 440 * scale), maximum,
                ImVec2(std::min(1100 * scale, maximum.x), std::min(740 * scale, maximum.y))};
    }
    inline ImVec2 KeepVisible(ImVec2 pos, ImVec2 size, ImVec2 display) {
        return ImVec2(std::clamp(pos.x, 0.0f, std::max(0.0f, display.x - size.x)),
                      std::clamp(pos.y, 0.0f, std::max(0.0f, display.y - size.y)));
    }
}
