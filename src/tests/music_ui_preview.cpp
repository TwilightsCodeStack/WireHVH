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

using Microsoft::WRL::ComPtr;
void Check(HRESULT result) { if (FAILED(result)) throw std::runtime_error("Preview rendering failed"); }

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 1;
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
    desc.Width = 760; desc.Height = 590;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target;
    ComPtr<ID3D11RenderTargetView> view;
    Check(device->CreateTexture2D(&desc, nullptr, &target));
    Check(device->CreateRenderTargetView(target.Get(), nullptr, &view));
    ImGui::CreateContext();
    ImGui::GetIO().DisplaySize = ImVec2(760, 590);
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(22, 18);
    style.ItemSpacing = ImVec2(10, 9);
    style.FramePadding = ImVec2(9, 5);
    style.FrameRounding = 5;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.065f, 0.045f, 0.10f, 1);
    ImGui_ImplDX11_Init(device.Get(), context.Get());
    for (int frame = 0; frame < 8; ++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        ImGui::GetIO().DeltaTime = 1.0f / 60;
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(760, 590));
        ImGui::Begin("Music preview", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
        Music::RenderUI(device.Get());
        ImGui::End();
        ImGui::Render();
        const float clear[4] = { 0, 0, 0, 1 };
        auto* renderView = view.Get();
        context->OMSetRenderTargets(1, &renderView, nullptr);
        context->ClearRenderTargetView(view.Get(), clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
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
    Music::CleanupUI();
    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext();
    Music::Shutdown();
    std::cout << "PASS: embedded cover uploaded and music UI rendered offscreen.\n";
    return 0;
}
