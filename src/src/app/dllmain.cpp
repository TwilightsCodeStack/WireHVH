#include <windows.h>
#include <iostream>
#include <thread>
#include <w1re/hooks.hpp>
#include <w1re/console_log.hpp>
#include <w1re/features/music.hpp>
#include <w1re/offset_patch.hpp>

// Global variables
HMODULE g_hModule = nullptr;

// Main thread for initialization
void Initialize(HMODULE hModule) {
    // Allocate console for diagnostic logging
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    SetConsoleTitleA("W1RE // Inject CLI");
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    ConsoleLog::Banner(
        " █     █░ ██▓ ██▀███  ▓█████ \n"
        "▓█░ █ ░█░▓██▒▓██ ▒ ██▒▓█   ▀ \n"
        "▒█░ █ ░█ ▒██▒▓██ ░▄█ ▒▒███   \n"
        "░█░ █ ░█ ░██░▒██▀▀█▄  ▒▓█  ▄ \n"
        "░░██▒██▓ ░██░░██▓ ▒██▒░▒████▒\n"
        "░ ▓░▒ ▒  ░▓  ░ ▒▓ ░▒▓░░ ▒░ ░\n"
        "  ▒ ░ ░   ▒ ░  ░▒ ░ ▒░░ ░  ░\n"
        "  ░   ░   ▒ ░  ░░   ░   ░   \n"
        "    ░     ░     ░        ░  ░\n"
        "          \n"
        "[W1RE] in inject cli\n\n");
    ConsoleLog::Info("Starting loader...");
    ConsoleLog::SetStage("Waiting for the game");
    ConsoleLog::StartLoading();

    // Wait until game modules are loaded (client.dll)
    bool waitingForClient = true;
    while (!GetModuleHandleA("client.dll")) {
        if (GetAsyncKeyState(VK_END) & 0x8000) {
            ConsoleLog::StopLoading();
            FreeLibraryAndExitThread(hModule, 0);
        }
        if (waitingForClient) {
            ConsoleLog::Info("Waiting for client.dll...");
            waitingForClient = false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    ConsoleLog::Success("client.dll detected.");

    ConsoleLog::SetStage("Applying current offsets");
    try {
        OffsetPatch::Consume();
        ConsoleLog::Success("Fresh offsets applied and confirmed to the loader.");
    } catch (const std::exception& error) {
        ConsoleLog::StopLoading();
        ConsoleLog::Error("WARNING: Offset patch failed. Features will not start.");
        ConsoleLog::Error(error.what());
        FreeLibraryAndExitThread(hModule, 1);
        return;
    }

    // Initialize MinHook, ImGui, and Hooks
    ConsoleLog::SetStage("Preparing audio and hooks");
    wchar_t modulePath[32768] = {};
    const DWORD pathLength = GetModuleFileNameW(hModule, modulePath, 32768);
    const auto moduleDirectory = pathLength > 0 && pathLength < 32768
        ? std::filesystem::path(modulePath).parent_path() : std::filesystem::path{};
    Music::Initialize(Music::ResolveDirectory(moduleDirectory));
    if (!Hooks::Initialize()) {
        ConsoleLog::StopLoading();
        Music::Shutdown();
        ConsoleLog::Error("Hook initialization failed.");
        FreeLibraryAndExitThread(hModule, 0);
        return;
    }
    if (!Hooks::RendererReady) ConsoleLog::SetStage("Waiting for the renderer");

    while (!GetAsyncKeyState(VK_END)) {
        if (Hooks::RendererReady && ConsoleLog::loading) ConsoleLog::StopLoading();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Cleanup and eject
    ConsoleLog::StopLoading();
    Music::Shutdown();
    Hooks::Cleanup();
#ifdef _DEBUG
    if (f) fclose(f);
    FreeConsole();
#endif
    FreeLibraryAndExitThread(hModule, 0);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        if (HANDLE hThread = CreateThread(nullptr, 0, (LPTHREAD_START_ROUTINE)Initialize, hModule, 0, nullptr)) {
            CloseHandle(hThread);
        }
        break;
    case DLL_PROCESS_DETACH:
        // Cleanup logic here
        break;
    }
    return TRUE;
}
