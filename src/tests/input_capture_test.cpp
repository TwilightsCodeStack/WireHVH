// Test capture transitions without touching the desktop cursor or a game.
#include <windows.h>
#include "../MinHook_134_bin (1)/include/MinHook.h"
#include <iostream>
#include <stdexcept>
static HWND foreground = reinterpret_cast<HWND>(1);
static bool clipped = true, captured = true, relative = true, grabbed = true;
static int warps = 0, resets = 0;
HWND WINAPI TestForeground() { return foreground; }
BOOL WINAPI TestGetClip(RECT* rect) { *rect = {0, 0, 800, 600}; return TRUE; }
BOOL WINAPI TestClip(const RECT* rect) { clipped = rect != nullptr; return TRUE; }
BOOL WINAPI TestWarp(int, int) { ++warps; return TRUE; }
HWND WINAPI TestCapture() { return captured ? reinterpret_cast<HWND>(1) : nullptr; }
BOOL WINAPI TestRelease() { captured = false; return TRUE; }
BOOL WINAPI TestPost(HWND, UINT, WPARAM, LPARAM) { return TRUE; }
#define GetForegroundWindow TestForeground
#define GetClipCursor TestGetClip
#define ClipCursor TestClip
#define GetCapture TestCapture
#define ReleaseCapture TestRelease
#define PostMessageW TestPost
#include "../input_capture.cpp"
#undef GetForegroundWindow
#undef GetClipCursor
#undef ClipCursor
#undef GetCapture
#undef ReleaseCapture
#undef PostMessageW
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID, LPVOID, LPVOID*) { return MH_ERROR_NOT_INITIALIZED; }
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) { return MH_OK; }
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) { return MH_OK; }
extern "C" MH_STATUS WINAPI MH_RemoveHook(LPVOID) { return MH_OK; }
static void* Focus() { return reinterpret_cast<void*>(2); }
static int SetRelative2(int value) { relative = value != 0; return 0; }
static int GetRelative2() { return relative; }
static void SetGrab2(void*, int value) { grabbed = value != 0; }
static int GetGrab2(void*) { return grabbed; }
static bool SetRelative3(void*, bool value) { relative = value; return true; }
static bool GetRelative3(void*) { return relative; }
static bool SetGrab3(void*, bool value) { grabbed = value; return true; }
static bool GetGrab3(void*) { return grabbed; }
static void ResetKeys() { ++resets; }
static unsigned int ReadState2(int* x, int* y) { *x = 17; *y = -5; return 1; }
static unsigned int ReadState3(float* x, float* y) { *x = 17; *y = -5; return 1; }
static void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

int main() {
    using namespace InputCapture;
    gameWindow = reinterpret_cast<HWND>(1);
    originalClip = TestClip;
    originalWarp = TestWarp;
    getMouseFocus = Focus;
    resetKeyboard = ResetKeys;
    originalRelative2 = SetRelative2; getRelative2 = GetRelative2;
    setGrab2 = SetGrab2; getGrab2 = GetGrab2; originalState2 = ReadState2;
    SetOpen(true); HandleMessage(SyncMessage);
    Require(Blocking() && !relative && !grabbed && !captured && !clipped, "Opening must release capture");
    const RECT rect{0,0,200,200};
    Clip(&rect); Warp(100,100);
    Require(!clipped && warps == 0, "Game cannot recapture or recenter while open");
    int x, y;
    Require(MouseState2(&x,&y) == 0 && !x && !y, "SDL2 relative motion must be zero");
    RelativeMode2(1);
    Require(!relative, "Relative-mode request must be deferred");
    SetOpen(false); HandleMessage(SyncMessage);
    Require(relative && grabbed && !Blocking(), "Close must restore previous modes");
    Warp(100,100);
    Require(warps == 1 && MouseState2(&x,&y) == 1 && x == 17, "Closed input passthrough");
    originalRelative2 = nullptr; getRelative2 = nullptr; setGrab2 = nullptr; getGrab2 = nullptr;
    originalRelative3 = SetRelative3; getRelative3 = GetRelative3;
    setGrab3 = SetGrab3; getGrab3 = GetGrab3; originalState3 = ReadState3;
    SetOpen(true); HandleMessage(SyncMessage);
    float fx, fy;
    Require(!relative && !grabbed && MouseState3(&fx,&fy) == 0 && !fx && !fy, "SDL3 release and zero motion");
    RelativeMode3(Focus(), false);
    foreground = reinterpret_cast<HWND>(3);
    HandleMessage(WM_KILLFOCUS);
    Require(!clipped && !Blocking(), "Alt-tab must not confine desktop cursor");
    foreground = gameWindow;
    HandleMessage(WM_SETFOCUS);
    Require(!relative && Blocking(), "Refocus must keep the open menu free");
    SetOpen(false); HandleMessage(SyncMessage);
    Require(!relative && resets == 3, "Restore latest game request and reset keys on open");
    std::cout << "PASS: SDL2/SDL3 release, restore, raw motion suppression, recenter suppression, focus transitions.\n";
}
