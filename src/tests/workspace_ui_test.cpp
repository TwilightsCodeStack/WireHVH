// Offscreen UI verification; no game process, injection, or audio playback.
#include "../features/music.hpp"
#include "../imgui-1.92.7/imgui.h"
#include "../imgui-1.92.7/backends/imgui_impl_dx11.h"
#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <chrono>
#include <thread>
#include <iostream>
#include "../imgui-1.92.7/imgui_internal.h"
#include "../config.hpp"
#include "../workspace_layout.hpp"
HMODULE g_hModule = GetModuleHandleW(nullptr);
namespace Hooks {
    extern float g_InterfaceScale, g_DpiScale;
    extern int g_WorkspacePage;
    extern ID3D11Device* pDevice;
    void ApplyTheme(); void ApplyWorkspaceScale(); void RenderWorkspaceUI();
    void LoadBackground(ID3D11Device*); void CleanupBackground();
}
namespace Features {
    void RenderESP() {} void RunTriggerbot() {} void RunAimbot() {}
    void ApplyVisualOverrides() {} bool HasCameraServices() { return false; }
    void RunAngleOverrides() {} void ApplyThirdPerson() {} void RunAutoFarm() {}
    void RunWalkbot() {} void StopWalkbot() {} void RunBunnyHop() {}
}
void Require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }

using Microsoft::WRL::ComPtr;
void Check(HRESULT result) { if (FAILED(result)) throw std::runtime_error("Preview rendering failed"); }

int wmain(int argc, wchar_t** argv) {
    if (argc != 7) return 1;
    const auto track = std::filesystem::absolute(argv[1]);
    Check(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    Music::Initialize(track.parent_path());
    for (int i = 0; i < 200 && !Music::GetSnapshot()->ready; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(25));
    Music::Send(Music::Action::Inspect, track);
    for (int i = 0; i < 200 && !Music::GetSnapshot()->metadata; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(25));
    const auto metadata = Music::GetSnapshot()->metadata;
    if (!metadata || !metadata->cover) { Music::Shutdown(); return 2; }
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context));
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = _wtoi(argv[3]); desc.Height = _wtoi(argv[4]);
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> view;
    Check(device->CreateTexture2D(&desc, nullptr, &target));
    Check(device->CreateRenderTargetView(target.Get(), nullptr, &view));
    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(desc.Width), static_cast<float>(desc.Height));
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16);
    ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
    Hooks::g_InterfaceScale = static_cast<float>(_wtof(argv[5]));
    Hooks::g_DpiScale = 1;
    Hooks::g_WorkspacePage = _wtoi(argv[6]);
    Hooks::pDevice = device.Get();
    Hooks::ApplyTheme();
    Hooks::LoadBackground(device.Get());
    ImGui_ImplDX11_Init(device.Get(), context.Get());
    auto render = [&] {
        ImGui::GetIO().DeltaTime = 1.0f / 60;
        ImGui_ImplDX11_NewFrame();
        Hooks::ApplyWorkspaceScale();
        ImGui::NewFrame();
        Hooks::RenderWorkspaceUI();
        ImGui::Render();
        const float clear[4] = { 0.045f, 0.045f, 0.05f, 1 };
        auto* renderView = view.Get();
        context->OMSetRenderTargets(1, &renderView, nullptr);
        context->ClearRenderTargetView(view.Get(), clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    };
    for (int frame = 0; frame < 8; ++frame) render();
    auto* window = ImGui::FindWindowByName("W1RE##Workspace");
    Require(window && window->Pos.x >= 0 && window->Pos.y >= 0, "Window origin out of bounds");
    Require(window->Pos.x + window->Size.x <= desc.Width && window->Pos.y + window->Size.y <= desc.Height, "Window clipped by display");
    if (desc.Width >= 1200 && window->Size.x < desc.Width - 100) {
        auto& io = ImGui::GetIO();
        const ImVec2 original = window->Pos;
        io.AddMousePosEvent(original.x + 80, original.y + 12); render();
        io.AddMouseButtonEvent(0, true); render();
        io.AddMousePosEvent(original.x + 120, original.y + 42); render();
        io.AddMouseButtonEvent(0, false); render();
        Require(window->Pos.x > original.x + 20, "Title-bar dragging failed");
        const ImVec2 initialSize = window->Size;
        const ImVec2 corner(window->Pos.x + initialSize.x - 4, window->Pos.y + initialSize.y - 4);
        io.AddMousePosEvent(corner.x, corner.y); render();
        io.AddMouseButtonEvent(0, true); render();
        io.AddMousePosEvent(corner.x - 110, corner.y - 80); render();
        io.AddMouseButtonEvent(0, false); render();
        Require(window->Size.x < initialSize.x - 40, "Corner resizing failed");
    }
    for (int frame = 0; frame < 3; ++frame) render();
    desc.BindFlags = 0;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    Check(device->CreateTexture2D(&desc, nullptr, &staging));
    context->CopyResource(staging.Get(), target.Get());
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> image;
    Check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    Check(factory->CreateStream(&stream));
    Check(stream->InitializeFromFilename(argv[2], GENERIC_WRITE));
    Check(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    Check(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    Check(encoder->CreateNewFrame(&image, nullptr));
    Check(image->Initialize(nullptr));
    Check(image->SetSize(desc.Width, desc.Height));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppRGBA;
    Check(image->SetPixelFormat(&format));
    ComPtr<IWICBitmap> bitmap;
    Check(factory->CreateBitmapFromMemory(desc.Width, desc.Height, GUID_WICPixelFormat32bppRGBA,
        mapped.RowPitch, mapped.RowPitch * desc.Height, static_cast<BYTE*>(mapped.pData), &bitmap));
    Check(image->WriteSource(bitmap.Get(), nullptr));
    Check(image->Commit());
    Check(encoder->Commit());
    context->Unmap(staging.Get(), 0);
    Music::CleanupUI(); Hooks::CleanupBackground();
    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext();
    Music::Shutdown();
    std::cout << "PASS: workspace bounds, scaled rendering, title drag and corner resize (large viewport).\n";
    return 0;
}
