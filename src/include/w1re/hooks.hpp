#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <atomic>
#include <MinHook.h>

namespace Hooks {
    inline std::atomic<bool> RendererReady{false};
    bool Initialize();
    void Cleanup();

    // Hooked Present function
    typedef HRESULT(__stdcall* Present_t)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
    extern Present_t oPresent;

    HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);

    // WndProc hook for ImGui input
    extern LRESULT(__stdcall* oWndProc)(HWND, UINT, WPARAM, LPARAM);
    LRESULT __stdcall hkWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
}
