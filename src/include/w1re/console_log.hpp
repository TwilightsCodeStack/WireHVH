#pragma once

#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <chrono>

namespace ConsoleLog {
    inline std::mutex outputMutex;
    inline std::atomic<bool> loading{false};
    inline std::thread animation;
    inline std::string stage = "Starting";
    inline bool progressVisible = false;

    inline HANDLE Output() { return GetStdHandle(STD_OUTPUT_HANDLE); }

    inline void Text(const std::string& text) {
        // Write synchronously: buffered iostream text can flush after the color resets.
        const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring wide(length, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
        DWORD written = 0;
        if (!WriteConsoleW(Output(), wide.data(), static_cast<DWORD>(wide.size()), &written, nullptr))
            WriteFile(Output(), text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    }

    inline void ClearProgress() {
        if (progressVisible) {
            Text("\r                                                                              \r");
            progressVisible = false;
        }
    }

    inline void Write(const char* prefix, WORD color, const std::string& message) {
        std::lock_guard<std::mutex> lock(outputMutex);
        ClearProgress();
        SetConsoleTextAttribute(Output(), color);
        Text(std::string(prefix) + message + "\r\n");
        SetConsoleTextAttribute(Output(), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }
    inline void Info(const std::string& message) { Write("[*] ", FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY, message); }
    inline void Success(const std::string& message) { Write("[+] ", FOREGROUND_GREEN | FOREGROUND_INTENSITY, message); }
    inline void Warning(const std::string& message) { Write("[!] ", FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY, message); }
    inline void Error(const std::string& message) { Write("[x] ", FOREGROUND_RED | FOREGROUND_INTENSITY, message); }
    inline void Banner(const char* banner) {
        std::lock_guard<std::mutex> lock(outputMutex);
        ClearProgress();
        SetConsoleTextAttribute(Output(), FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
        Text(banner);
        SetConsoleTextAttribute(Output(), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }
    inline void SetStage(const std::string& value) {
        std::lock_guard<std::mutex> lock(outputMutex);
        stage = value;
    }
    inline void StartLoading() {
        loading = true;
        animation = std::thread([] {
            size_t frame = 0;
            while (loading) {
                {
                    std::lock_guard<std::mutex> lock(outputMutex);
                    ClearProgress();
                    SetConsoleTextAttribute(Output(), FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
                    Text(std::string("\r[") + "|/-\\"[frame++ % 4] + "] " + stage + "...");
                    progressVisible = true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }
    inline void StopLoading() {
        loading = false;
        if (animation.joinable()) animation.join();
        std::lock_guard<std::mutex> lock(outputMutex);
        ClearProgress();
        SetConsoleTextAttribute(Output(), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    }
}
