#include <w1re/input_capture.hpp>
#include <w1re/console_log.hpp>
#include <MinHook.h>
#include <atomic>
#include <vector>

namespace InputCapture {
namespace {
    HWND gameWindow = nullptr;
    std::atomic<bool> requested{false};
    bool applied = false;
    bool relativeWanted = false, grabWanted = false;
    RECT previousClip = {};
    void* mouseWindow = nullptr;
    thread_local bool bypass = false;
    std::vector<void*> installed;
    using ClipProc = BOOL(WINAPI*)(const RECT*);
    using WarpProc = BOOL(WINAPI*)(int, int);
    ClipProc originalClip = nullptr;
    WarpProc originalWarp = nullptr;
    using FocusProc = void* (__cdecl*)();
    FocusProc getMouseFocus = nullptr;
    void(__cdecl* resetKeyboard)() = nullptr;
    using Relative2 = int(__cdecl*)(int);
    using Query2 = int(__cdecl*)();
    using Grab2 = void(__cdecl*)(void*, int);
    using QueryGrab2 = int(__cdecl*)(void*);
    Relative2 originalRelative2 = nullptr;
    Query2 getRelative2 = nullptr;
    Grab2 setGrab2 = nullptr;
    QueryGrab2 getGrab2 = nullptr;
    using Relative3 = bool(__cdecl*)(void*, bool);
    using Query3 = bool(__cdecl*)(void*);
    Relative3 originalRelative3 = nullptr, setGrab3 = nullptr;
    Query3 getRelative3 = nullptr, getGrab3 = nullptr;
    using State2 = unsigned int(__cdecl*)(int*, int*);
    using State3 = unsigned int(__cdecl*)(float*, float*);
    State2 originalState2 = nullptr;
    State3 originalState3 = nullptr;

    BOOL WINAPI Clip(const RECT* rect) {
        return !bypass && Blocking() ? TRUE : originalClip(rect);
    }
    BOOL WINAPI Warp(int x, int y) {
        return !bypass && Blocking() ? TRUE : originalWarp(x, y);
    }
    int __cdecl RelativeMode2(int enabled) {
        if (!bypass && Blocking()) { relativeWanted = enabled != 0; return originalRelative2(0); }
        return originalRelative2(enabled);
    }
    bool __cdecl RelativeMode3(void* window, bool enabled) {
        if (!bypass && Blocking()) { relativeWanted = enabled; mouseWindow = window; return originalRelative3(window, false); }
        return originalRelative3(window, enabled);
    }
    unsigned int __cdecl MouseState2(int* x, int* y) {
        const auto buttons = originalState2(x, y);
        if (!Blocking()) return buttons;
        if (x) *x = 0; if (y) *y = 0;
        return 0;
    }
    unsigned int __cdecl MouseState3(float* x, float* y) {
        const auto buttons = originalState3(x, y);
        if (!Blocking()) return buttons;
        if (x) *x = 0; if (y) *y = 0;
        return 0;
    }
    template<class T> T Symbol(HMODULE module, const char* name) {
        return reinterpret_cast<T>(GetProcAddress(module, name));
    }
    template<class T> bool Hook(void* target, void* replacement, T* original) {
        if (!target || MH_CreateHook(target, replacement, reinterpret_cast<void**>(original)) != MH_OK) return false;
        if (MH_EnableHook(target) != MH_OK) { MH_RemoveHook(target); return false; }
        installed.push_back(target);
        return true;
    }

    // Executed by the game's WndProc, keeping SDL operations on the window thread.
    void Apply(bool open) {
        if (open == applied) return;
        bypass = true;
        if (open) {
            GetClipCursor(&previousClip);
            mouseWindow = getMouseFocus ? getMouseFocus() : nullptr;
            relativeWanted = getRelative2 ? getRelative2() != 0 : (mouseWindow && getRelative3 ? getRelative3(mouseWindow) : false);
            grabWanted = mouseWindow && (getGrab2 ? getGrab2(mouseWindow) != 0 : (getGrab3 ? getGrab3(mouseWindow) : false));
            if (originalRelative2) originalRelative2(0);
            if (originalRelative3 && mouseWindow) originalRelative3(mouseWindow, false);
            if (setGrab2 && mouseWindow) setGrab2(mouseWindow, 0);
            if (setGrab3 && mouseWindow) setGrab3(mouseWindow, false);
            if (resetKeyboard) resetKeyboard();
            ClipCursor(nullptr);
            if (GetCapture() == gameWindow) ReleaseCapture();
        } else {
            if (setGrab2 && mouseWindow) setGrab2(mouseWindow, grabWanted ? 1 : 0);
            if (setGrab3 && mouseWindow) setGrab3(mouseWindow, grabWanted);
            if (originalRelative2) originalRelative2(relativeWanted ? 1 : 0);
            if (originalRelative3 && mouseWindow) originalRelative3(mouseWindow, relativeWanted);
            // Never confine the cursor while another application is foreground.
            if (GetForegroundWindow() != gameWindow) ClipCursor(nullptr);
            else if (!originalRelative2 && !originalRelative3) ClipCursor(&previousClip);
        }
        applied = open;
        bypass = false;
    }
}
    bool Blocking() { return requested.load() && gameWindow && GetForegroundWindow() == gameWindow; }
    void Initialize(HWND window) {
        gameWindow = window;
        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        Hook(reinterpret_cast<void*>(GetProcAddress(user32, "ClipCursor")), reinterpret_cast<void*>(&Clip), &originalClip);
        Hook(reinterpret_cast<void*>(GetProcAddress(user32, "SetCursorPos")), reinterpret_cast<void*>(&Warp), &originalWarp);
        HMODULE sdl = GetModuleHandleW(L"SDL3.dll");
        if (!sdl) sdl = GetModuleHandleW(L"SDL2.dll");
        if (sdl) {
            getMouseFocus = Symbol<FocusProc>(sdl, "SDL_GetMouseFocus");
            resetKeyboard = Symbol<void(__cdecl*)()>(sdl, "SDL_ResetKeyboard");
            if (GetProcAddress(sdl, "SDL_SetWindowRelativeMouseMode")) {
                getRelative3 = Symbol<Query3>(sdl, "SDL_GetWindowRelativeMouseMode");
                getGrab3 = Symbol<Query3>(sdl, "SDL_GetWindowMouseGrab");
                setGrab3 = Symbol<Relative3>(sdl, "SDL_SetWindowMouseGrab");
                Hook(reinterpret_cast<void*>(GetProcAddress(sdl, "SDL_SetWindowRelativeMouseMode")), reinterpret_cast<void*>(&RelativeMode3), &originalRelative3);
                Hook(reinterpret_cast<void*>(GetProcAddress(sdl, "SDL_GetRelativeMouseState")), reinterpret_cast<void*>(&MouseState3), &originalState3);
            } else {
                getRelative2 = Symbol<Query2>(sdl, "SDL_GetRelativeMouseMode");
                getGrab2 = Symbol<QueryGrab2>(sdl, "SDL_GetWindowGrab");
                setGrab2 = Symbol<Grab2>(sdl, "SDL_SetWindowGrab");
                Hook(reinterpret_cast<void*>(GetProcAddress(sdl, "SDL_SetRelativeMouseMode")), reinterpret_cast<void*>(&RelativeMode2), &originalRelative2);
                Hook(reinterpret_cast<void*>(GetProcAddress(sdl, "SDL_GetRelativeMouseState")), reinterpret_cast<void*>(&MouseState2), &originalState2);
            }
        }
        ConsoleLog::Info(originalRelative2 || originalRelative3 ? "SDL mouse release ready." : "Windows mouse release ready (SDL relative-mode export unavailable).");
    }
    void SetOpen(bool open) {
        if (requested.exchange(open) != open && gameWindow) PostMessageW(gameWindow, SyncMessage, 0, 0);
    }
    bool HandleMessage(UINT message) {
        if (message == SyncMessage) { Apply(Blocking()); return true; }
        if (message == WM_SETFOCUS) Apply(requested);
        if (message == WM_KILLFOCUS) Apply(false);
        return false;
    }
    void Shutdown() {
        requested = false;
        if (gameWindow) {
            DWORD_PTR ignored = 0;
            SendMessageTimeoutW(gameWindow, SyncMessage, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &ignored);
        }
        for (auto* target : installed) { MH_DisableHook(target); MH_RemoveHook(target); }
        installed.clear();
        gameWindow = nullptr;
    }
}
