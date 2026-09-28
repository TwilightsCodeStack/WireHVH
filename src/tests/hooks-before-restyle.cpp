#include "hooks.hpp"
#include "console_log.hpp"
#include <iostream>
#include "stdexcept"
#include <intrin.h>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "imgui-1.92.7/imgui.h"
#include "imgui-1.92.7/imgui_internal.h"
#include "imgui-1.92.7/backends/imgui_impl_dx11.h"
#include "imgui-1.92.7/backends/imgui_impl_win32.h"
#include "config.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "features/farm.hpp"
#include "features/walkbot.hpp"
#include "features/movement.hpp"
#include "features/music.hpp"
#include "features/skins.hpp"
#include "workspace_layout.hpp"
#include "input_capture.hpp"

extern HMODULE g_hModule;

// Custom toggle switch widget
namespace {
    bool Toggle(const char* label, bool* v) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;

        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id = window->GetID(label);
        const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);

        float height = ImGui::GetFrameHeight();
        const ImVec2 pos = window->DC.CursorPos;
        
        float width = height * 1.55f;
        float radius = height * 0.50f;

        const ImRect total_bb(pos, ImVec2(pos.x + width + (label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f), pos.y + label_size.y + style.FramePadding.y * 2.0f));

        ImGui::ItemSize(total_bb, style.FramePadding.y);
        if (!ImGui::ItemAdd(total_bb, id)) return false;

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held);
        if (pressed) {
            *v = !*v;
            ImGui::MarkItemEdited(id);
        }

        float t = *v ? 1.0f : 0.0f;
        
        if (g.LastActiveId == id) {
            float t_anim = ImSaturate(g.LastActiveIdTimer / 0.16f);
            t = *v ? (t_anim) : (1.0f - t_anim);
        }

        // Off: dark gray, On: purple accent
        ImU32 col_bg;
        if (hovered)
            col_bg = ImGui::GetColorU32(ImLerp(ImVec4(0.25f, 0.25f, 0.30f, 1.0f), ImVec4(0.55f, 0.25f, 0.90f, 1.0f), t));
        else
            col_bg = ImGui::GetColorU32(ImLerp(ImVec4(0.18f, 0.18f, 0.22f, 1.0f), ImVec4(0.50f, 0.20f, 0.85f, 1.0f), t));

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), col_bg, height * 0.5f);
        draw_list->AddCircleFilled(ImVec2(pos.x + radius + t * (width - radius * 2.0f), pos.y + radius), radius - 1.5f, IM_COL32(255, 255, 255, 255));

        if (label_size.x > 0.0f) {
            ImGui::RenderText(ImVec2(pos.x + width + style.ItemInnerSpacing.x, pos.y + style.FramePadding.y), label);
        }

        return pressed;
    }
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Features {
    extern void RenderESP();
    extern void RunTriggerbot();
    extern void RunAimbot();
    extern void ApplyVisualOverrides();
    extern bool HasCameraServices();
    extern void RunAngleOverrides();
    extern void ApplyThirdPerson();
}

namespace Hooks {
    using FeatureProc = void(*)();

    bool InvokeFeatureSafely(FeatureProc proc) {
        __try {
            proc();
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    void RunFeatureSafely(const char* name, FeatureProc proc, bool* enabled) {
        if (!proc || !enabled || !*enabled)
            return;

        if (!InvokeFeatureSafely(proc)) {
            *enabled = false;
            if (name[0] == 'E')
                ConsoleLog::Error("ESP disabled after a protected memory access fault.");
            else if (name[0] == 'T')
                ConsoleLog::Error("Triggerbot disabled after a protected memory access fault.");
            else if (name[0] == 'A' && name[1] == 'i')
                ConsoleLog::Error("Aimbot disabled after a protected memory access fault.");
            else if (name[0] == 'A')
                ConsoleLog::Error("Auto farm disabled after a protected memory access fault.");
            else
                ConsoleLog::Error("Walkbot disabled after a protected memory access fault.");
        }
    }

    Present_t oPresent = nullptr;
    ID3D11Device* pDevice = nullptr;
    ID3D11DeviceContext* pContext = nullptr;
    ID3D11RenderTargetView* pRenderTargetView = nullptr;
    ID3D11Texture2D* g_LiveSceneTexture = nullptr;
    ID3D11ShaderResourceView* g_LiveSceneView = nullptr;
    UINT g_LiveSceneWidth = 0;
    UINT g_LiveSceneHeight = 0;
    UINT g_RenderTargetWidth = 0;
    UINT g_RenderTargetHeight = 0;
    HWND hWnd = nullptr;
    WNDPROC oWndProc = nullptr;

    bool bInitialized = false;
    bool bStyleInitialized = false;
    float g_DpiScale = 1.0f;
    std::vector<ID3D11ShaderResourceView*> g_BackgroundFrames;
    std::vector<int> g_BackgroundDelays;
    std::chrono::steady_clock::time_point g_BackgroundStarted;
    int g_BackgroundWidth = 0;
    int g_BackgroundHeight = 0;
    float g_InterfaceScale = 1.0f;
    int g_ThemeIndex = 0;
    bool g_InterfaceAnimations = true;
    int g_WorkspacePage = 0;
    bool g_WorkspacePositionInitialized = false;
    char g_ProfileName[64] = "default";
    std::string g_ProfileStatus;
    ImGuiStyle g_BaseStyle;
    const auto g_SessionStarted = std::chrono::steady_clock::now();

    std::filesystem::path GetProfileDirectory() {
        char modulePath[MAX_PATH] = {};
        GetModuleFileNameA(g_hModule, modulePath, MAX_PATH);
        std::filesystem::path directory(modulePath);
        return directory.parent_path() / "profiles";
    }

    std::filesystem::path GetProfilePath() {
        std::string name(g_ProfileName);
        if (name.empty())
            name = "default";
        for (char& character : name) {
            if (character == '\\' || character == '/' || character == ':' ||
                character == '*' || character == '?' || character == '"' ||
                character == '<' || character == '>' || character == '|')
                character = '_';
        }
        return GetProfileDirectory() / (name + ".cfg");
    }

    void SaveCurrentProfile() {
        std::error_code error;
        std::filesystem::create_directories(GetProfileDirectory(), error);
        if (!error && Config::Save(GetProfilePath().string()))
            g_ProfileStatus = "Saved " + std::string(g_ProfileName) + ".cfg";
        else
            g_ProfileStatus = "Could not save profile";
    }

    void LoadCurrentProfile() {
        if (Config::Load(GetProfilePath().string()))
            g_ProfileStatus = "Loaded " + std::string(g_ProfileName) + ".cfg";
        else
            g_ProfileStatus = "Profile not found";
    }

    void DeleteCurrentProfile() {
        std::error_code error;
        const bool removed = std::filesystem::remove(GetProfilePath(), error);
        g_ProfileStatus = removed && !error ? "Deleted profile" : "Profile not found";
    }

    void CleanupLiveScene() {
        if (g_LiveSceneView) {
            g_LiveSceneView->Release();
            g_LiveSceneView = nullptr;
        }
        if (g_LiveSceneTexture) {
            g_LiveSceneTexture->Release();
            g_LiveSceneTexture = nullptr;
        }
        g_LiveSceneWidth = 0;
        g_LiveSceneHeight = 0;
    }

    bool CaptureLiveScene(IDXGISwapChain* swapChain) {
        if (!pDevice || !pContext || !swapChain)
            return false;

        ID3D11Texture2D* backBuffer = nullptr;
        if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                        reinterpret_cast<void**>(&backBuffer))))
            return false;

        D3D11_TEXTURE2D_DESC sourceDesc = {};
        backBuffer->GetDesc(&sourceDesc);
        if (sourceDesc.Width == 0 || sourceDesc.Height == 0 ||
            sourceDesc.SampleDesc.Count != 1 ||
            sourceDesc.Format == DXGI_FORMAT_UNKNOWN) {
            backBuffer->Release();
            return false;
        }

        if (!g_LiveSceneTexture ||
            g_LiveSceneWidth != sourceDesc.Width ||
            g_LiveSceneHeight != sourceDesc.Height) {
            CleanupLiveScene();

            D3D11_TEXTURE2D_DESC captureDesc = sourceDesc;
            captureDesc.Usage = D3D11_USAGE_DEFAULT;
            captureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            captureDesc.CPUAccessFlags = 0;
            captureDesc.MiscFlags = 0;
            captureDesc.MipLevels = 1;
            captureDesc.ArraySize = 1;

            if (FAILED(pDevice->CreateTexture2D(&captureDesc, nullptr, &g_LiveSceneTexture)) ||
                FAILED(pDevice->CreateShaderResourceView(g_LiveSceneTexture, nullptr,
                                                         &g_LiveSceneView))) {
                backBuffer->Release();
                CleanupLiveScene();
                return false;
            }

            g_LiveSceneWidth = sourceDesc.Width;
            g_LiveSceneHeight = sourceDesc.Height;
        }

        pContext->CopyResource(g_LiveSceneTexture, backBuffer);
        backBuffer->Release();
        return true;
    }

    bool EnsureRenderTarget(IDXGISwapChain* swapChain) {
        if (!pDevice || !swapChain)
            return false;

        DXGI_SWAP_CHAIN_DESC description = {};
        if (FAILED(swapChain->GetDesc(&description)))
            return false;

        ID3D11Texture2D* backBuffer = nullptr;
        if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                        reinterpret_cast<void**>(&backBuffer))) ||
            !backBuffer)
            return false;

        D3D11_TEXTURE2D_DESC textureDescription = {};
        backBuffer->GetDesc(&textureDescription);
        const bool needsRebuild =
            !pRenderTargetView ||
            g_RenderTargetWidth != textureDescription.Width ||
            g_RenderTargetHeight != textureDescription.Height;

        if (needsRebuild) {
            ID3D11RenderTargetView* newTarget = nullptr;
            const HRESULT result = pDevice->CreateRenderTargetView(
                backBuffer, nullptr, &newTarget);
            if (FAILED(result) || !newTarget) {
                backBuffer->Release();
                return false;
            }

            if (pRenderTargetView)
                pRenderTargetView->Release();
            pRenderTargetView = newTarget;
            g_RenderTargetWidth = textureDescription.Width;
            g_RenderTargetHeight = textureDescription.Height;
        }

        backBuffer->Release();
        return pRenderTargetView != nullptr;
    }

    std::string GetBackgroundPath() {
        char modulePath[MAX_PATH] = {};
        GetModuleFileNameA(g_hModule, modulePath, MAX_PATH);
        std::string path(modulePath);
        const size_t separator = path.find_last_of("\\/");
        if (separator != std::string::npos)
            path.resize(separator + 1);
        return path + "resources\\bg.gif";
    }

    std::vector<std::string> GetBackgroundCandidates() {
        std::vector<std::string> paths;

        char modulePath[MAX_PATH] = {};
        GetModuleFileNameA(g_hModule, modulePath, MAX_PATH);
        std::string module(modulePath);
        const size_t separator = module.find_last_of("\\/");
        if (separator != std::string::npos)
            module.resize(separator + 1);

        // Prefer the user's original animated background before scene captures.
        paths.push_back(GetBackgroundPath());
        paths.push_back(module + "resources\\cs2-home-menu.gif");
        paths.push_back(module + "resources\\cs2-home-menu.png");
        paths.push_back(module + "resources\\w1re-background.gif");
        paths.push_back(module + "resources\\background.gif");
        paths.push_back(module + "resources\\background.png");

        char currentPath[MAX_PATH] = {};
        GetCurrentDirectoryA(MAX_PATH, currentPath);
        std::string current(currentPath);
        paths.insert(paths.begin() + 1, current + "\\resources\\bg.gif");
        paths.push_back(current + "\\resources\\w1re-background.gif");
        paths.push_back(current + "\\resources\\cs2-home-menu.gif");
        paths.push_back(current + "\\resources\\cs2-home-menu.png");
        paths.push_back(current + "\\resources\\background.gif");
        paths.push_back(current + "\\resources\\background.png");
        return paths;
    }

    void CleanupBackground() {
        for (ID3D11ShaderResourceView* frame : g_BackgroundFrames) {
            if (frame)
                frame->Release();
        }
        g_BackgroundFrames.clear();
        g_BackgroundDelays.clear();
        g_BackgroundWidth = 0;
        g_BackgroundHeight = 0;
    }

    void LoadBackground(ID3D11Device* device) {
        CleanupBackground();

        std::string backgroundPath;
        std::vector<unsigned char> bytes;
        ConsoleLog::Info("Searching for background assets...");
        for (const std::string& candidate : GetBackgroundCandidates()) {
            std::ifstream file(candidate, std::ios::binary);
            if (!file)
                continue;
            bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            if (!bytes.empty()) {
                backgroundPath = candidate;
                break;
            }
        }
        if (bytes.empty()) {
            char modulePath[MAX_PATH] = {};
            GetModuleFileNameA(g_hModule, modulePath, MAX_PATH);
            ConsoleLog::Warning(std::string("No background asset found beside injected module: ") + modulePath);
            return;
        }

        int* delays = nullptr;
        int frameCount = 0;
        int channels = 0;
        unsigned char* pixels = nullptr;
        if (backgroundPath.size() >= 4 &&
            backgroundPath.substr(backgroundPath.size() - 4) == ".gif") {
            pixels = stbi_load_gif_from_memory(
                bytes.data(), static_cast<int>(bytes.size()), &delays,
                &g_BackgroundWidth, &g_BackgroundHeight, &frameCount, &channels, 4);
        } else {
            pixels = stbi_load_from_memory(
                bytes.data(), static_cast<int>(bytes.size()),
                &g_BackgroundWidth, &g_BackgroundHeight, &channels, 4);
            frameCount = pixels ? 1 : 0;
        }
        if (!pixels || frameCount <= 0) {
            ConsoleLog::Error(std::string("Background decode failed: ") + backgroundPath);
            if (pixels)
                stbi_image_free(pixels);
            if (delays)
                stbi_image_free(delays);
            return;
        }

        const size_t frameBytes = static_cast<size_t>(g_BackgroundWidth) *
                                  static_cast<size_t>(g_BackgroundHeight) * 4u;
        for (int frame = 0; frame < frameCount; ++frame) {
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = static_cast<UINT>(g_BackgroundWidth);
            desc.Height = static_cast<UINT>(g_BackgroundHeight);
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA data = {};
            data.pSysMem = pixels + frameBytes * static_cast<size_t>(frame);
            data.SysMemPitch = static_cast<UINT>(g_BackgroundWidth * 4);

            ID3D11Texture2D* texture = nullptr;
            ID3D11ShaderResourceView* view = nullptr;
            if (SUCCEEDED(device->CreateTexture2D(&desc, &data, &texture)) &&
                SUCCEEDED(device->CreateShaderResourceView(texture, nullptr, &view))) {
                g_BackgroundFrames.push_back(view);
                g_BackgroundDelays.push_back(delays ? (delays[frame] > 50 ? delays[frame] : 50) : 100);
            }
            if (texture)
                texture->Release();
        }

        stbi_image_free(pixels);
        if (delays)
            stbi_image_free(delays);
        g_BackgroundStarted = std::chrono::steady_clock::now();
        const bool animated = backgroundPath.size() >= 4 &&
                              backgroundPath.substr(backgroundPath.size() - 4) == ".gif";
        ConsoleLog::Success(std::string(animated ? "Animated home scene loaded: " : "Home scene loaded: ") +
                            std::to_string(frameCount) + " frame" + (frameCount == 1 ? "" : "s") + " (" +
                            std::to_string(g_BackgroundWidth) + "x" +
                            std::to_string(g_BackgroundHeight) + ").");
    }

    void RenderBackground(const ImVec2& min, const ImVec2& max) {
        if (g_BackgroundFrames.empty())
            return;

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - g_BackgroundStarted).count();
        int totalDuration = 0;
        for (int delay : g_BackgroundDelays)
            totalDuration += delay;
        const int position = totalDuration > 0 ? static_cast<int>(elapsed % totalDuration) : 0;

        if (g_BackgroundFrames.empty() || g_BackgroundDelays.size() != g_BackgroundFrames.size())
            return;

        int frame = 0;
        int accumulated = 0;
        for (; frame + 1 < static_cast<int>(g_BackgroundFrames.size()); ++frame) {
            accumulated += g_BackgroundDelays[frame];
            if (position < accumulated)
                break;
        }

        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddImage(ImTextureRef(reinterpret_cast<ImTextureID>(g_BackgroundFrames[frame])),
                       min, max, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255));
        draw->AddRectFilled(min, max, IM_COL32(11, 8, 22, 108), 10.0f);
        draw->AddRectFilledMultiColor(
            min, max,
            IM_COL32(20, 15, 34, 104),
            IM_COL32(30, 17, 46, 118),
            IM_COL32(23, 18, 38, 112),
            IM_COL32(15, 11, 27, 100));
    }

    // Check if game window is focused
    inline bool IsGameFocused() {
        if (!hWnd) return false;
        return GetForegroundWindow() == hWnd;
    }

    // Apply W1RE dark purple theme once
    void ApplyTheme() {
        if (bStyleInitialized) return;

        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 10.0f;
        style.FrameRounding = 6.0f;
        style.GrabRounding = 6.0f;
        style.PopupRounding = 8.0f;
        style.ScrollbarRounding = 6.0f;
        style.TabRounding = 6.0f;
        style.ChildRounding = 8.0f;
        style.ItemSpacing = ImVec2(10, 10);
        style.FramePadding = ImVec2(10, 7);
        style.WindowPadding = ImVec2(0, 0);
        style.ScrollbarSize = 10.0f;
        style.WindowBorderSize = 0.0f;
        style.ChildBorderSize = 0.0f;
        style.PopupBorderSize = 0.0f;
        style.ItemInnerSpacing = ImVec2(8, 6);
        style.IndentSpacing = 18.0f;

        ImVec4* colors = style.Colors;

        // Outer shell / dark blue canvas like the reference
        colors[ImGuiCol_WindowBg]           = ImVec4(0.075f, 0.050f, 0.115f, 1.00f);
        colors[ImGuiCol_ChildBg]            = ImVec4(0.075f, 0.050f, 0.115f, 0.78f);
        colors[ImGuiCol_PopupBg]            = ImVec4(0.11f, 0.08f, 0.16f, 0.98f);
        colors[ImGuiCol_Border]             = ImVec4(0.82f, 0.62f, 1.00f, 0.18f);
        colors[ImGuiCol_BorderShadow]       = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

        // Text
        colors[ImGuiCol_Text]               = ImVec4(0.94f, 0.90f, 1.00f, 1.00f);
        colors[ImGuiCol_TextDisabled]       = ImVec4(0.55f, 0.50f, 0.70f, 1.00f);

        // Panels and cards
        colors[ImGuiCol_FrameBg]            = ImVec4(0.12f, 0.075f, 0.19f, 0.94f);
        colors[ImGuiCol_FrameBgHovered]     = ImVec4(0.20f, 0.12f, 0.30f, 0.98f);
        colors[ImGuiCol_FrameBgActive]      = ImVec4(0.27f, 0.16f, 0.40f, 1.00f);
        colors[ImGuiCol_TitleBg]            = ImVec4(0.08f, 0.06f, 0.12f, 1.00f);
        colors[ImGuiCol_TitleBgActive]      = ImVec4(0.12f, 0.09f, 0.18f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]   = ImVec4(0.08f, 0.06f, 0.12f, 0.80f);

        // Buttons / tabs
        colors[ImGuiCol_Button]             = ImVec4(0.27f, 0.18f, 0.43f, 0.98f);
        colors[ImGuiCol_ButtonHovered]      = ImVec4(0.42f, 0.27f, 0.64f, 1.00f);
        colors[ImGuiCol_ButtonActive]       = ImVec4(0.54f, 0.34f, 0.78f, 1.00f);
        colors[ImGuiCol_Header]             = ImVec4(0.29f, 0.17f, 0.42f, 1.00f);
        colors[ImGuiCol_HeaderHovered]      = ImVec4(0.44f, 0.25f, 0.67f, 1.00f);
        colors[ImGuiCol_HeaderActive]       = ImVec4(0.52f, 0.28f, 0.79f, 1.00f);
        colors[ImGuiCol_Tab]                = ImVec4(0.16f, 0.11f, 0.22f, 1.00f);
        colors[ImGuiCol_TabHovered]         = ImVec4(0.35f, 0.22f, 0.56f, 1.00f);
        colors[ImGuiCol_TabSelected]        = ImVec4(0.46f, 0.27f, 0.71f, 1.00f);

        // Inputs / checks / sliders
        colors[ImGuiCol_CheckMark]          = ImVec4(0.82f, 0.62f, 1.00f, 1.00f);
        colors[ImGuiCol_SliderGrab]         = ImVec4(0.70f, 0.47f, 1.00f, 1.00f);
        colors[ImGuiCol_SliderGrabActive]   = ImVec4(0.82f, 0.62f, 1.00f, 1.00f);

        // Scrollbars and separators
        colors[ImGuiCol_ScrollbarBg]        = ImVec4(0.09f, 0.09f, 0.12f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab]      = ImVec4(0.34f, 0.28f, 0.42f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.46f, 0.36f, 0.58f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.56f, 0.42f, 0.74f, 1.00f);
        colors[ImGuiCol_Separator]          = ImVec4(0.70f, 0.52f, 0.94f, 0.28f);
        colors[ImGuiCol_SeparatorHovered]   = ImVec4(0.70f, 0.52f, 0.94f, 0.48f);
        colors[ImGuiCol_SeparatorActive]    = ImVec4(0.70f, 0.52f, 0.94f, 0.72f);

        colors[ImGuiCol_ResizeGrip]         = ImVec4(0.50f, 0.25f, 0.80f, 0.25f);
        colors[ImGuiCol_ResizeGripHovered]  = ImVec4(0.50f, 0.25f, 0.80f, 0.60f);
        colors[ImGuiCol_ResizeGripActive]   = ImVec4(0.65f, 0.40f, 1.00f, 0.90f);

        bStyleInitialized = true;
        g_BaseStyle = style;
    }

    LRESULT __stdcall hkWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (InputCapture::HandleMessage(uMsg)) return 0;
        if (uMsg == WM_KEYDOWN && wParam == VK_INSERT) {
            if (!(lParam & (1LL << 30))) {
                Config::bShowMenu = !Config::bShowMenu;
                InputCapture::SetOpen(Config::bShowMenu);
            }
            return 0;
        }
        if (uMsg == WM_KEYUP && wParam == VK_INSERT) return 0;
        if (ImGui::GetCurrentContext()) ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
        if (InputCapture::Blocking()) {
            if (uMsg == WM_INPUT) return DefWindowProcW(hWnd, uMsg, wParam, lParam);
            if (uMsg == WM_SYSKEYDOWN || uMsg == WM_SYSKEYUP) return DefWindowProcW(hWnd, uMsg, wParam, lParam);
            if (uMsg == WM_SETCURSOR) { SetCursor(nullptr); return TRUE; }
            if ((uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST) ||
                uMsg == WM_KEYDOWN || uMsg == WM_KEYUP || uMsg == WM_CHAR ||
                uMsg == WM_SYSKEYDOWN || uMsg == WM_SYSKEYUP || uMsg == WM_SYSCHAR)
                return 0;
        }

        return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
    }

    void RenderFeatureControls() {
                if (g_WorkspacePage == 1) {
                ImGui::TextColored(ImVec4(0.74f, 0.58f, 0.94f, 1.0f), "COMBAT");
                    ImGui::Separator();
                Toggle("Aim assist##section", &Config::bAimAssist);
                ImGui::SliderFloat("Aim FOV##section", &Config::fAimFov, 1.0f, 45.0f, "%.1f deg");
                ImGui::SliderFloat("Aim smoothing##section", &Config::fAimSmooth, 1.0f, 20.0f, "%.1f");
                Toggle("Triggerbot##section", &Config::bTriggerbot);
                Toggle("Anti-aim##section", &Config::bAntiAim);
                if (Config::bAntiAim) {
                    ImGui::SliderFloat("Anti-aim pitch##section", &Config::fPitchAngle,
                                       -89.0f, 89.0f, "%.1f deg");
                    ImGui::SliderFloat("Anti-aim yaw##section", &Config::fYawAngle,
                                       -180.0f, 180.0f, "%.1f deg");
                }
                Toggle("Spin##section", &Config::bSpin);
                if (Config::bSpin)
                    ImGui::SliderFloat("Spin speed##section", &Config::fSpinSpeed,
                                       1.0f, 1440.0f, "%.0f deg/s");
            } else if (g_WorkspacePage == 2) {
                ImGui::TextColored(ImVec4(0.74f, 0.58f, 0.94f, 1.0f), "VISUALS");
                ImGui::Separator();
                Toggle("Enable ESP##workspace", &Config::bEsp);
                Toggle("Bounding Box##workspace", &Config::bEspBox);
                Toggle("Skeleton##workspace", &Config::bEspSkeleton);
                Toggle("Health Bar##workspace", &Config::bEspHealth);
                Toggle("Player Name##workspace", &Config::bEspName);
                Toggle("Distance##workspace", &Config::bEspDistance);
                Toggle("Snaplines##workspace", &Config::bEspSnaplines);
                Toggle("FOV circle##workspace", &Config::bDrawFov);
                Toggle("Glow##workspace", &Config::bGlowEnabled);
                Toggle("Chams##workspace", &Config::bChamsEnabled);
                if (Config::bChamsEnabled) {
                    Toggle("Chams through walls##workspace", &Config::bChamsXQZ);
                    ImGui::TextColored(ImVec4(0.62f, 0.55f, 0.72f, 1.0f),
                                       "Visible color");
                    ImGui::ColorEdit3("##ChamsVisibleColor", Config::fChamsVisibleColor,
                                      ImGuiColorEditFlags_NoInputs);
                    if (Config::bChamsXQZ) {
                        ImGui::TextColored(ImVec4(0.62f, 0.55f, 0.72f, 1.0f),
                                           "Hidden color");
                        ImGui::ColorEdit3("##ChamsHiddenColor", Config::fChamsHiddenColor,
                                          ImGuiColorEditFlags_NoInputs);
                    }
                }
                Toggle("Third person##workspace", &Config::bThirdPersonEnabled);
                if (Config::bThirdPersonEnabled)
                    ImGui::SliderFloat("Camera distance##workspace", &Config::fThirdPersonDistance,
                                       32.0f, 300.0f, "%.0f units");
            } else if (g_WorkspacePage == 3) {
                ImGui::TextColored(ImVec4(0.74f, 0.58f, 0.94f, 1.0f), "MOVEMENT");
                ImGui::Separator();
                Toggle("Bunny hop##workspace", &Config::bBunnyHop);
                Toggle("Walkbot##workspace", &Config::bWalkbot);
                Toggle("Walkbot auto fire##workspace", &Config::bWalkbotAutoFire);
                } else if (g_WorkspacePage == 4) {
                ImGui::TextColored(ImVec4(0.74f, 0.58f, 0.94f, 1.0f), "UTILITY");
                    ImGui::Separator();
                Toggle("Auto farm##utility", &Config::bAutoFarm);
                Toggle("Auto respawn##utility", &Config::bAutoRespawn);
                Toggle("Anti-AFK##utility", &Config::bAntiAfk);
                } else if (g_WorkspacePage == 6) {
                    Music::RenderUI(pDevice);
                } else if (g_WorkspacePage == 7) {
                    Skins::RenderUI();
                } else {
                ImGui::TextColored(ImVec4(0.74f, 0.58f, 0.94f, 1.0f), "DIAGNOSTICS");
                    ImGui::Separator();
                ImGui::Text("Renderer: DirectX 11");
                ImGui::Text("Overlay: initialized");
                ImGui::Text("Camera services: %s", Features::HasCameraServices() ? "available" : "unavailable");
                ImGui::Text("Live scene: %s", g_LiveSceneView ? "available" : "unavailable");
                ImGui::Text("Background frames: %d", static_cast<int>(g_BackgroundFrames.size()));
                }
    }

    void ApplyWorkspaceScale() {
        if (hWnd) {
            const UINT dpi = GetDpiForWindow(hWnd);
            if (dpi) g_DpiScale = static_cast<float>(dpi) / 96.0f;
        }
        const auto layout = WorkspaceLayout::Calculate(ImGui::GetIO().DisplaySize, g_InterfaceScale, g_DpiScale);
        ImGui::GetStyle() = g_BaseStyle;
        ImGui::GetStyle().ScaleAllSizes(layout.scale);
        ImGui::GetStyle().FontScaleMain = layout.scale;
        ImGui::GetStyle().FontScaleDpi = 1.0f;
        ImGui::GetStyle().WindowPadding = ImVec2(16 * layout.scale, 14 * layout.scale);
    }

    void RenderDashboard(float scale) {
        ImGui::TextColored(ImVec4(0.78f, 0.63f, 0.98f, 1), "SESSION ONLINE");
        ImGui::Text("Profile: %s", g_ProfileName);
        const float buttonWidth = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3;
        if (ImGui::Button("Save profile", ImVec2(buttonWidth, 32 * scale))) SaveCurrentProfile();
        ImGui::SameLine();
        if (ImGui::Button("Load profile", ImVec2(buttonWidth, 32 * scale))) LoadCurrentProfile();
        ImGui::SameLine();
        if (ImGui::Button("Delete profile", ImVec2(buttonWidth, 32 * scale))) DeleteCurrentProfile();
        if (!g_ProfileStatus.empty()) ImGui::TextWrapped("%s", g_ProfileStatus.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - g_SessionStarted).count();
        ImGui::Text("Ready  |  DirectX 11  |  Session %lld:%02lld", seconds / 60, seconds % 60);
        ImGui::Spacing();
        ImGui::TextUnformatted("Interface settings");
        if (ImGui::BeginTable("##Settings", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 0.42f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.58f);
            const auto row = [](const char* name) {
                ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(name); ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1);
            };
            row("UI scale");
            ImGui::SliderFloat("##UIScale", &g_InterfaceScale, 0.75f, 1.5f, "%.2fx");
            row("Interface animations");
            ImGui::Checkbox("##Animations", &g_InterfaceAnimations);
            row("Live game background");
            ImGui::Checkbox("##LiveScene", &Config::bLiveSceneBackground);
            row("Profile name");
            ImGui::InputText("##Profile", g_ProfileName, sizeof(g_ProfileName));
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::TextWrapped("Drag the title bar to move. Drag any edge or the bottom-right corner to resize.");
    }

    void RenderWorkspaceUI() {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        if (display.x < 64 || display.y < 64) return;
        const auto layout = WorkspaceLayout::Calculate(display, g_InterfaceScale, g_DpiScale);
        const float scale = layout.scale;
        const char* pages[] = { "Dashboard", "Combat", "Visuals", "Movement", "Utility", "Diagnostics", "Music", "Skins" };
        g_WorkspacePage = std::clamp(g_WorkspacePage, 0, IM_ARRAYSIZE(pages) - 1);
        if (!g_WorkspacePositionInitialized) {
            ImGui::SetNextWindowPos(ImVec2((display.x - layout.initial.x) / 2, (display.y - layout.initial.y) / 2));
            ImGui::SetNextWindowSize(layout.initial);
            g_WorkspacePositionInitialized = true;
        }
        if (auto* previous = ImGui::FindWindowByName("W1RE##Workspace")) {
            const ImVec2 size(std::clamp(previous->Size.x, layout.minimum.x, layout.maximum.x),
                              std::clamp(previous->Size.y, layout.minimum.y, layout.maximum.y));
            ImGui::SetNextWindowPos(WorkspaceLayout::KeepVisible(previous->Pos, size, display));
        }
        ImGui::SetNextWindowSizeConstraints(layout.minimum, layout.maximum);
        const auto flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        if (!ImGui::Begin("W1RE##Workspace", &Config::bShowMenu, flags)) { ImGui::End(); return; }
        // Keep title bar and resize borders clear; the backdrop belongs to the content area.
        const auto pos = ImGui::GetWindowPos();
        const auto size = ImGui::GetWindowSize();
        const ImVec2 backgroundMin(pos.x + 2, pos.y + ImGui::GetFrameHeight() + 2);
        const ImVec2 backgroundMax(pos.x + size.x - 12 * scale, pos.y + size.y - 16 * scale);
        if (Config::bLiveSceneBackground && g_LiveSceneView) {
            ImGui::GetWindowDrawList()->AddImage(ImTextureRef(reinterpret_cast<ImTextureID>(g_LiveSceneView)), backgroundMin, backgroundMax);
            ImGui::GetWindowDrawList()->AddRectFilled(backgroundMin, backgroundMax, IM_COL32(11, 8, 22, 140));
        } else RenderBackground(backgroundMin, backgroundMax);
        const float footerHeight = ImGui::GetTextLineHeightWithSpacing() + 8 * scale;
        const float bodyHeight = std::max(1.0f, ImGui::GetContentRegionAvail().y - footerHeight);
        ImGui::BeginChild("##Sidebar", ImVec2(180 * scale, bodyHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::TextColored(ImVec4(0.82f, 0.62f, 1, 1), "W1RE INTERNAL");
        ImGui::Separator();
        for (int i = 0; i < IM_ARRAYSIZE(pages); ++i) {
            if (ImGui::Selectable(pages[i], g_WorkspacePage == i, 0, ImVec2(0, 35 * scale))) g_WorkspacePage = i;
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##Content", ImVec2(0, bodyHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::TextColored(ImVec4(0.89f, 0.80f, 1, 1), "%s", pages[g_WorkspacePage]);
        ImGui::Separator();
        if (g_WorkspacePage == 0) RenderDashboard(scale);
        else {
            ImGui::PushItemWidth(std::max(100 * scale, ImGui::GetContentRegionAvail().x * 0.58f));
            RenderFeatureControls();
            ImGui::PopItemWidth();
        }
        ImGui::EndChild();
        ImGui::TextDisabled("Insert: menu   |   End: unload");
        ImGui::End();
    }

    HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
        if (!bInitialized) {
            ConsoleLog::SetStage("Preparing the renderer");
            ConsoleLog::Info("Initializing ImGui and DXGI...");
            if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice))) {
                pDevice->GetImmediateContext(&pContext);
                DXGI_SWAP_CHAIN_DESC sd;
                pSwapChain->GetDesc(&sd);
                
                hWnd = FindWindowA("SDL_app", "Counter-Strike 2");
                if (!hWnd) {
                    ConsoleLog::Warning("Game window lookup fell back to the swap-chain output window.");
                    hWnd = sd.OutputWindow;
                }

                ID3D11Texture2D* pBackBuffer = nullptr;
                if (FAILED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                                 reinterpret_cast<void**>(&pBackBuffer))) ||
                    !pBackBuffer ||
                    FAILED(pDevice->CreateRenderTargetView(pBackBuffer, nullptr,
                                                           &pRenderTargetView))) {
                    if (pBackBuffer)
                        pBackBuffer->Release();
                    ConsoleLog::Error("Failed to create the game render target.");
                    return oPresent(pSwapChain, SyncInterval, Flags);
                }
                pBackBuffer->Release();

                oWndProc = (WNDPROC)SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);

                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
                
                // DPI-aware scaling (prevents clipped UI on 125%/150%/200%)
                g_DpiScale = 1.0f;
                if (hWnd) {
                    HDC dc = GetDC(hWnd);
                    const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
                    if (dc)
                        ReleaseDC(hWnd, dc);
                    if (dpi >= 96)
                        g_DpiScale = static_cast<float>(dpi) / 96.0f;
                }

                // Load font (scaled)
                ImFont* mainFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f);
                if (!mainFont) {
                    io.Fonts->AddFontDefault();
                }

                if (!ImGui_ImplWin32_Init(hWnd) ||
                    !ImGui_ImplDX11_Init(pDevice, pContext)) {
                    ConsoleLog::Error("ImGui backend initialization failed.");
                    return oPresent(pSwapChain, SyncInterval, Flags);
                }

                ApplyTheme();
                ConsoleLog::SetStage("Loading background assets");
                LoadBackground(pDevice);
                InputCapture::Initialize(hWnd);
                InputCapture::SetOpen(Config::bShowMenu);

                bInitialized = true;
                RendererReady = true;
                ConsoleLog::Success("Renderer and interface ready.");
            }
            else return oPresent(pSwapChain, SyncInterval, Flags);
        }

        // No double-toggle — INSERT is handled only in WndProc
        if (!EnsureRenderTarget(pSwapChain))
            return oPresent(pSwapChain, SyncInterval, Flags);

        // Live backbuffer capture is opt-in because some swap-chain formats
        // and game transitions do not support CopyResource safely.
        if (Config::bLiveSceneBackground)
            CaptureLiveScene(pSwapChain);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        InputCapture::SetOpen(Config::bShowMenu);
        ImGui::GetIO().MouseDrawCursor = InputCapture::Blocking();
        ApplyWorkspaceScale();
        ImGui::NewFrame();

        if (Config::bShowMenu)
            RenderWorkspaceUI();



        // ========================= FEATURES =========================
        uintptr_t client = (uintptr_t)GetModuleHandleA("client.dll");
        if (!Config::bWalkbot || Config::bShowMenu || !IsGameFocused()) Features::StopWalkbot();
        if (client && IsGameFocused()) {
            RunFeatureSafely("ESP", Features::RenderESP, &Config::bEsp);
            if (!Config::bShowMenu) {
            RunFeatureSafely("Triggerbot", Features::RunTriggerbot, &Config::bTriggerbot);
            RunFeatureSafely("Aimbot", Features::RunAimbot, &Config::bAimAssist);
            RunFeatureSafely("Auto farm", Features::RunAutoFarm, &Config::bAutoFarm);
            RunFeatureSafely("Walkbot", Features::RunWalkbot, &Config::bWalkbot);
            if (Config::bAntiAim || Config::bSpin) {
                if (!InvokeFeatureSafely(Features::RunAngleOverrides)) {
                    Config::bAntiAim = false;
                    Config::bSpin = false;
                }
            }
            }
            if (!InvokeFeatureSafely(Features::ApplyThirdPerson))
                Config::bThirdPersonEnabled = false;
            if (Config::bGlowEnabled || Config::bChamsEnabled) {
                if (!InvokeFeatureSafely(Features::ApplyVisualOverrides)) {
                    Config::bGlowEnabled = false;
                    Config::bChamsEnabled = false;
                    ConsoleLog::Error("Visual overrides disabled after a protected memory access fault.");
                }
            }
            if (!Config::bShowMenu && (Config::bBunnyHop || (Config::bWalkbot && Config::bWalkbotBindBhop))) {
                if (!InvokeFeatureSafely(Features::RunBunnyHop)) {
                    Config::bBunnyHop = false;
                    Config::bWalkbotBindBhop = false;
                    ConsoleLog::Error("Bunny hop disabled after a protected memory access fault.");
                }
            }
        }

        if (!pContext || !pRenderTargetView)
            return oPresent(pSwapChain, SyncInterval, Flags);

        ImGui::Render();
        pContext->OMSetRenderTargets(1, &pRenderTargetView, NULL);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        return oPresent(pSwapChain, SyncInterval, Flags);
    }

    bool Initialize() {
        ConsoleLog::Info("Initializing MinHook...");
        const MH_STATUS minHookStatus = MH_Initialize();
        if (minHookStatus != MH_OK) {
            ConsoleLog::Error("MinHook initialization failed (status " +
                              std::to_string(static_cast<int>(minHookStatus)) + ").");
            return false;
        }
        ConsoleLog::Success("MinHook initialized.");

        ConsoleLog::Info("Creating DX11 device...");
        HWND hWndDummy = CreateWindowA("BUTTON", "Dummy", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, GetModuleHandle(NULL), NULL);
        if (!hWndDummy) {
            ConsoleLog::Error("Failed to create the DX11 bootstrap window.");
            MH_Uninitialize();
            return false;
        }

        D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
        DXGI_SWAP_CHAIN_DESC sd = { 0 };
        sd.BufferCount = 1;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hWndDummy;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        ID3D11Device* pDummyDevice = NULL;
        ID3D11DeviceContext* pDummyContext = NULL;
        IDXGISwapChain* pDummySwapChain = NULL;

        HRESULT hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, &featureLevel, 1, D3D11_SDK_VERSION, &sd, &pDummySwapChain, &pDummyDevice, NULL, &pDummyContext);
        if (FAILED(hr)) {
            ConsoleLog::Error("D3D11 device creation failed (HRESULT 0x" +
                              std::to_string(static_cast<unsigned long>(hr)) + ").");
            DestroyWindow(hWndDummy);
            MH_Uninitialize();
            return false;
        }

        void** pVTable = *(void***)pDummySwapChain;
        void* pPresent = pVTable[8];

        if (MH_CreateHook(pPresent, reinterpret_cast<LPVOID>(&hkPresent),
                          reinterpret_cast<LPVOID*>(&oPresent)) != MH_OK) {
            ConsoleLog::Error("Failed to create the Present hook.");
            pDummySwapChain->Release();
            pDummyDevice->Release();
            pDummyContext->Release();
            DestroyWindow(hWndDummy);
            MH_Uninitialize();
            return false;
        }

        if (MH_EnableHook(pPresent) != MH_OK) {
            ConsoleLog::Error("Failed to enable the Present hook.");
            MH_RemoveHook(pPresent);
            pDummySwapChain->Release();
            pDummyDevice->Release();
            pDummyContext->Release();
            DestroyWindow(hWndDummy);
            MH_Uninitialize();
            return false;
        }
        ConsoleLog::Success("Present hook enabled.");

        pDummySwapChain->Release();
        pDummyDevice->Release();
        pDummyContext->Release();
        DestroyWindow(hWndDummy);
        return true; 
    }

    void Cleanup() {
        Skins::Shutdown();
        Features::StopWalkbot();
        InputCapture::Shutdown();
        RendererReady = false;
        MH_DisableHook(MH_ALL_HOOKS);
        Music::CleanupUI();
        if (bInitialized) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            bInitialized = false;
        }
        CleanupBackground();
        CleanupLiveScene();
        if (pRenderTargetView) {
            pRenderTargetView->Release();
            pRenderTargetView = nullptr;
        }
        g_RenderTargetWidth = 0;
        g_RenderTargetHeight = 0;
        if (pContext) {
            pContext->Release();
            pContext = nullptr;
        }
        if (pDevice) {
            pDevice->Release();
            pDevice = nullptr;
        }
        MH_Uninitialize();
        if (hWnd && oWndProc) {
            SetWindowLongPtrW(hWnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
        }
    }
}
