#pragma once
#include <windows.h>
namespace InputCapture {
    constexpr UINT SyncMessage = WM_APP + 0x513;
    void Initialize(HWND window);
    void SetOpen(bool open);
    bool Blocking();
    bool HandleMessage(UINT message);
    void Shutdown();
}
