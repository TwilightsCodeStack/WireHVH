#include "../console_log.hpp"
#include <iostream>
int main() {
    HANDLE original = GetStdHandle(STD_OUTPUT_HANDLE);
    const bool allocated = AllocConsole() != FALSE;
    if (allocated) ShowWindow(GetConsoleWindow(), SW_HIDE);
    HANDLE buffer = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CONSOLE_TEXTMODE_BUFFER, nullptr);
    if (buffer == INVALID_HANDLE_VALUE) return 2;
    SetStdHandle(STD_OUTPUT_HANDLE, buffer);
    ConsoleLog::Banner("W1RE\n");
    ConsoleLog::Info("Starting loader");
    ConsoleLog::Success("Ready");
    WORD banner = 0, info = 0, success = 0;
    DWORD read = 0;
    ReadConsoleOutputAttribute(buffer, &banner, 1, {0,0}, &read);
    ReadConsoleOutputAttribute(buffer, &info, 1, {5,1}, &read);
    ReadConsoleOutputAttribute(buffer, &success, 1, {5,2}, &read);
    ConsoleLog::SetStage("Loading assets");
    ConsoleLog::StartLoading();
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    ConsoleLog::Info("Concurrent renderer message");
    ConsoleLog::StopLoading();
    SetStdHandle(STD_OUTPUT_HANDLE, original);
    CloseHandle(buffer);
    if (allocated) FreeConsole();
    SetStdHandle(STD_OUTPUT_HANDLE, original);
    if (banner != 13 || info != 11 || success != 10 || ConsoleLog::animation.joinable()) return 1;
    std::cout << "PASS: banner and full log-line colors, concurrent loading animation, joined shutdown.\n";
}
