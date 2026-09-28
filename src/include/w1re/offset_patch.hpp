#pragma once
#include <windows.h>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <string>
#include <w1re/offsets.hpp>

namespace OffsetPatch {
enum class Source { Global, Button, Relative, Field, Glow };
struct Descriptor { const char* name; Source source; const char* type; const char* key; };
inline constexpr Descriptor Fields[] = {
#define OFFSET(name, source, type, key) {#name, Source::source, type, key},
#include <w1re/offset_fields.inc>
#undef OFFSET
};
inline constexpr DWORD Magic = 0x57315031;
inline constexpr DWORD Version = 1;
enum : LONG { Pending = 0, Applied = 1, Failed = 2 };
struct Packet {
    DWORD magic = Magic, version = Version, count = static_cast<DWORD>(std::size(Fields)), pid = 0;
    FILETIME created{};
    DWORD build = 0, buildOffset = 0, clientSize = 0;
    std::uint64_t clientBase = 0, engineBase = 0;
    std::int64_t values[std::size(Fields)]{};
    volatile LONG status = Pending;
    char error[256]{};
};
inline std::wstring MappingName(DWORD pid) { return L"Local\\W1RE.OffsetPatch." + std::to_wstring(pid); }
inline FILETIME CreationTime(HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user))
        throw std::runtime_error("Cannot verify target process lifetime.");
    return created;
}
inline void Validate(const Packet& packet) {
    if (packet.magic != Magic || packet.version != Version || packet.count != std::size(Fields))
        throw std::runtime_error("Offset patch format differs; rebuild loader and DLL together.");
    if (!packet.build || packet.build > 10000000 || !packet.buildOffset || !packet.clientSize ||
        !packet.clientBase || !packet.engineBase)
        throw std::runtime_error("Offset patch has invalid game metadata.");
    for (size_t i = 0; i < std::size(Fields); ++i) {
        const auto source = Fields[i].source;
        const auto limit = (source == Source::Global || source == Source::Button)
            ? static_cast<std::int64_t>(packet.clientSize) - 64 : 0x100000;
        if (packet.values[i] <= 0 || packet.values[i] >= limit)
            throw std::runtime_error(std::string("Invalid offset: ") + Fields[i].name);
    }
}
inline void Apply(const Packet& packet) {
    Validate(packet); // Validate the entire batch before changing any runtime value.
    size_t index = 0;
#define OFFSET(name, source, type, key) Offsets::name = static_cast<ptrdiff_t>(packet.values[index++]);
#include <w1re/offset_fields.inc>
#undef OFFSET
}

// Called once, before any hook or feature thread can read the offsets.
inline void Consume() {
    HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MappingName(GetCurrentProcessId()).c_str());
    if (!mapping) throw std::runtime_error("No fresh offset patch. Start W1RE with W1RE-Loader.exe.");
    auto* shared = static_cast<Packet*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Packet)));
    if (!shared) { CloseHandle(mapping); throw std::runtime_error("Cannot read loader offset patch."); }
    try {
        const Packet packet = *shared;
        Validate(packet);
        const auto created = CreationTime(GetCurrentProcess());
        if (packet.pid != GetCurrentProcessId() || CompareFileTime(&packet.created, &created) ||
            packet.clientBase != reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"client.dll")) ||
            packet.engineBase != reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"engine2.dll")))
            throw std::runtime_error("Offset patch belongs to a different game session.");
        DWORD build = 0;
        SIZE_T bytes = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(packet.engineBase + packet.buildOffset),
            &build, sizeof(build), &bytes) || bytes != sizeof(build) || build != packet.build)
            throw std::runtime_error("Offset patch does not match the running game build.");
        Apply(packet);
        InterlockedExchange(&shared->status, Applied);
    } catch (const std::exception& error) {
        strncpy_s(shared->error, error.what(), _TRUNCATE);
        InterlockedExchange(&shared->status, Failed);
        UnmapViewOfFile(shared);
        CloseHandle(mapping);
        throw;
    }
    UnmapViewOfFile(shared);
    CloseHandle(mapping);
}
}
