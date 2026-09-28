#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdint>
#include <w1re/offset_dump.hpp>

#ifndef _WIN64
#error Build the loader for Windows x64.
#endif

namespace fs = std::filesystem;
namespace {
std::string Utf8(const std::wstring& value) {
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
    return result;
}

struct Handle {
    HANDLE value;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

[[noreturn]] void WinError(const char* operation) {
    const DWORD code = GetLastError();
    throw std::runtime_error(std::string(operation) + " failed (Windows error " +
        std::to_string(code) + ")." + (code == ERROR_ACCESS_DENIED
        ? " Access was denied; check that the loader and game have matching permissions." : ""));
}

fs::path ExecutablePath(HANDLE process = nullptr) {
    wchar_t path[32768]{};
    DWORD size = static_cast<DWORD>(std::size(path));
    if (process) {
        if (!QueryFullProcessImageNameW(process, 0, path, &size)) WinError("Reading process path");
    } else {
        size = GetModuleFileNameW(nullptr, path, size);
        if (!size || size >= std::size(path)) WinError("Reading loader path");
    }
    return fs::path(std::wstring(path, size));
}

void ValidateDll(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    IMAGE_DOS_HEADER dos{};
    DWORD signature = 0;
    IMAGE_FILE_HEADER header{};
    WORD magic = 0;
    if (!file.read(reinterpret_cast<char*>(&dos), sizeof(dos)) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < sizeof(dos))
        throw std::runtime_error("Missing or invalid DLL: " + path.filename().string());
    file.seekg(dos.e_lfanew);
    if (!file.read(reinterpret_cast<char*>(&signature), sizeof(signature)) ||
        !file.read(reinterpret_cast<char*>(&header), sizeof(header)) ||
        !file.read(reinterpret_cast<char*>(&magic), sizeof(magic)) ||
        signature != IMAGE_NT_SIGNATURE || header.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        !(header.Characteristics & IMAGE_FILE_DLL) || magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        throw std::runtime_error("Expected a Windows x64 DLL: " + path.filename().string());
}

DWORD FindGame(DWORD requested) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.value == INVALID_HANDLE_VALUE) WinError("Listing processes");
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::vector<DWORD> matches;
    if (!Process32FirstW(snapshot.value, &entry)) WinError("Reading process list");
    do {
        if (!_wcsicmp(entry.szExeFile, L"cs2.exe") && (!requested || requested == entry.th32ProcessID))
            matches.push_back(entry.th32ProcessID);
    } while (Process32NextW(snapshot.value, &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) WinError("Reading process list");
    if (matches.empty()) throw std::runtime_error(requested
        ? "The specified PID is not a running cs2.exe process."
        : "CS2 is not running. Start it, then run W1RE-Loader.exe again.");
    if (matches.size() != 1) throw std::runtime_error("Multiple CS2 processes found. Use --pid <number> to select one.");
    return matches.front();
}

std::vector<MODULEENTRY32W> Modules(DWORD pid) {
    HANDLE raw = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 10; ++attempt) {
        raw = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
        if (raw != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH) break;
        Sleep(10);
    }
    Handle snapshot(raw);
    if (snapshot.value == INVALID_HANDLE_VALUE) WinError("Listing target modules");
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Module32FirstW(snapshot.value, &entry)) WinError("Reading target modules");
    std::vector<MODULEENTRY32W> result;
    do { result.push_back(entry); } while (Module32NextW(snapshot.value, &entry));
    if (GetLastError() != ERROR_NO_MORE_FILES) WinError("Reading target modules");
    return result;
}

bool AlreadyLoaded(DWORD pid, const fs::path& path) {
    for (const auto& module : Modules(pid)) {
        if (_wcsicmp(module.szModule, path.filename().c_str())) continue;
        // Toolhelp's stored path can lose Unicode characters even with Module32FirstW.
        Handle process(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid));
        if (!process.value) WinError("Opening target to verify module path");
        wchar_t loadedPath[32768]{};
        const DWORD length = GetModuleFileNameExW(process.value, module.hModule, loadedPath,
            static_cast<DWORD>(std::size(loadedPath)));
        if (!length || length >= std::size(loadedPath)) WinError("Reading loaded DLL path");
        std::error_code ec;
        if (!fs::equivalent(path, fs::path(loadedPath), ec) || ec) {
            std::cerr << "    Expected: " << Utf8(path.wstring()) << "\n    Loaded:   " << Utf8(loadedPath)
                << "\n    Path check: " << ec.message() << '\n';
            throw std::runtime_error("A different copy of " + path.filename().string() +
                " is already loaded. Restart the game before switching copies.");
        }
        return true;
    }
    return false;
}

LPTHREAD_START_ROUTINE RemoteLoadLibrary(DWORD pid) {
    const auto proc = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    if (!proc) WinError("Resolving LoadLibraryW");
    // Resolve the actual owner: the export may be forwarded to KernelBase.
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(proc), &owner)) WinError("Resolving loader module");
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(owner, path, static_cast<DWORD>(std::size(path))))
        WinError("Reading loader module path");
    const auto name = fs::path(path).filename().wstring();
    const auto offset = reinterpret_cast<std::uintptr_t>(proc) - reinterpret_cast<std::uintptr_t>(owner);
    for (const auto& module : Modules(pid)) {
        if (!_wcsicmp(module.szModule, name.c_str()) && offset < module.modBaseSize)
            return reinterpret_cast<LPTHREAD_START_ROUTINE>(module.modBaseAddr + offset);
    }
    throw std::runtime_error("The target's Windows loader module is unavailable.");
}

void LoadDll(HANDLE process, DWORD pid, const fs::path& path) {
    if (AlreadyLoaded(pid, path)) return;
    const auto loadLibrary = RemoteLoadLibrary(pid);
    const auto text = path.wstring();
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) WinError("Allocating DLL path");
    // The path must remain valid until the remote thread has finished reading it.
    struct Allocation {
        HANDLE process;
        void* address;
        ~Allocation() { if (address) VirtualFreeEx(process, address, 0, MEM_RELEASE); }
    } allocation{process, remote};
    SIZE_T written = 0;
    if (!WriteProcessMemory(process, remote, text.c_str(), bytes, &written)) WinError("Writing DLL path");
    if (written != bytes) throw std::runtime_error("The DLL path was only partially written.");
    Handle thread(CreateRemoteThread(process, nullptr, 0, loadLibrary, remote, 0, nullptr));
    if (!thread.value) WinError("Starting DLL load");
    const DWORD wait = WaitForSingleObject(thread.value, 15000);
    if (wait != WAIT_OBJECT_0) {
        allocation.address = nullptr;
        if (wait == WAIT_FAILED) WinError("Waiting for DLL load");
        throw std::runtime_error("DLL loading timed out; it may still complete. A small path buffer was retained "
            "until the game exits. Check the game before retrying.");
    }
    // A thread's DWORD exit code cannot represent a 64-bit HMODULE. Verify the module list instead.
    if (!AlreadyLoaded(pid, path)) throw std::runtime_error("Windows did not keep " + path.filename().string() +
        " loaded. Check its dependencies and DLL initialization output.");
}

void CopyInstalledFile(const fs::path& source, const fs::path& destination) {
    if (fs::exists(destination) && fs::equivalent(source, destination)) return;
    fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
}

void CopyResources(const fs::path& source, const fs::path& destination) {
    for (const auto& entry : fs::recursive_directory_iterator(source)) {
        const auto relative = entry.path().lexically_relative(source);
        const auto target = destination / relative;
        const auto status = entry.symlink_status();
        if (fs::is_symlink(status)) continue;
        if (fs::is_directory(status)) {
            fs::create_directories(target);
        } else if (fs::is_regular_file(status)) {
            fs::create_directories(target.parent_path());
            if (!fs::exists(target)) fs::copy_file(entry.path(), target);
            else if (!fs::is_regular_file(target))
                throw std::runtime_error("Cannot install resource over a non-file: " + Utf8(target.wstring()));
        }
    }
}

void InstallBundle(const fs::path& sourceDirectory, const fs::path& gameDirectory) {
    const auto game = fs::absolute(gameDirectory);
    if (!fs::is_directory(game) || !fs::is_regular_file(game / L"cs2.exe"))
        throw std::runtime_error("The install target must be the folder containing cs2.exe.");

    const auto loader = sourceDirectory / L"W1RE-Loader.exe";
    const auto dll = sourceDirectory / L"W1RE.dll";
    const auto dependency = sourceDirectory / L"MinHook.x64.dll";
    const auto dumper = sourceDirectory / L"cs2-dumper.exe";
    const auto resources = sourceDirectory / L"resources";
    for (const auto& file : {loader, dll, dependency, dumper}) {
        if (!fs::is_regular_file(file))
            throw std::runtime_error("Missing install file: " + Utf8(file.wstring()));
    }
    if (!fs::is_directory(resources))
        throw std::runtime_error("Missing install folder: " + Utf8(resources.wstring()));
    ValidateDll(dll);
    ValidateDll(dependency);

    const auto destination = game / L"W1RE";
    fs::create_directories(destination);
    for (const auto& file : {loader, dll, dependency, dumper})
        CopyInstalledFile(file, destination / file.filename());
    CopyResources(resources, destination / L"resources");
    std::wcout << L"[+] Installed loader bundle to " << destination.wstring() << L".\n"
        L"    Run W1RE-Loader.exe from that folder each time you want to load W1RE.\n";
}

#include <w1re/loader_offset_patch.hpp>

int Run(int argc, wchar_t** argv) {
    DWORD requested = 0;
    bool check = false;
    bool install = false;
    fs::path gameDirectory;
    fs::path dumper;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--no-pause") continue;
        if (arg == L"--help" || arg == L"-h") {
            std::wcout << L"W1RE-Loader.exe [--pid <CS2 PID>] [--check] [--dumper <path>] [--no-pause]\n"
                L"Loads the adjacent W1RE.dll and MinHook.x64.dll into running CS2.\n"
                L"Automatically dumps, validates and applies current offsets before initialization.\n"
                L"--check validates files, target and fresh offsets without loading anything.\n"
                L"--dumper selects cs2-dumper.exe (default: beside this loader).\n"
                L"--install <CS2 folder> copies the runtime bundle into <CS2 folder>\\W1RE.\n"
                L"Run the installed loader manually for each game session; this does not auto-load it.\n";
            return 0;
        }
        if (arg == L"--check") { check = true; continue; }
        if (arg == L"--install" && i + 1 < argc) {
            gameDirectory = fs::absolute(argv[++i]);
            install = true;
            continue;
        }
        if (arg == L"--dumper" && i + 1 < argc) {
            dumper = fs::absolute(argv[++i]);
            continue;
        }
        if (arg == L"--pid" && i + 1 < argc) {
            const std::wstring value = argv[++i];
            if (value.empty() || value.find_first_not_of(L"0123456789") != std::wstring::npos)
                throw std::runtime_error("--pid requires a positive process ID.");
            const auto number = std::stoull(value);
            if (!number || number > MAXDWORD) throw std::runtime_error("PID is out of range.");
            requested = static_cast<DWORD>(number);
            continue;
        }
        throw std::runtime_error("Unknown or incomplete option. Use --help for usage.");
    }
    const auto directory = ExecutablePath().parent_path();
    if (install) {
        if (requested || check || !dumper.empty())
            throw std::runtime_error("--install cannot be combined with --pid, --check or --dumper.");
        InstallBundle(directory, gameDirectory);
        return 0;
    }
    if (dumper.empty()) dumper = directory / L"cs2-dumper.exe";
    const auto dll = directory / L"W1RE.dll";
    const auto dependency = directory / L"MinHook.x64.dll";
    ValidateDll(dll);
    ValidateDll(dependency);
    const DWORD pid = FindGame(requested);
    const DWORD rights = SYNCHRONIZE | PROCESS_QUERY_INFORMATION | PROCESS_VM_READ |
        (check ? 0 : PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION | PROCESS_VM_WRITE);
    Handle process(OpenProcess(rights, FALSE, pid));
    if (!process.value) WinError("Opening CS2");
    if (_wcsicmp(ExecutablePath(process.value).filename().c_str(), L"cs2.exe"))
        throw std::runtime_error("Target changed during startup. Run the loader again.");
    BOOL wow64 = FALSE;
    SYSTEM_INFO system{};
    GetNativeSystemInfo(&system);
    if (!IsWow64Process(process.value, &wow64)) WinError("Checking target architecture");
    if (wow64 || system.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_AMD64)
        throw std::runtime_error("This loader requires an x64 CS2 process on x64 Windows.");
    std::wcout << L"[*] Found CS2 (PID " << pid << L").\n";
    const bool loaded = AlreadyLoaded(pid, dll);
    AlreadyLoaded(pid, dependency); // Reject conflicting dependency copies before making changes.
    if (loaded) {
        std::wcout << L"[+] W1RE is already loaded; no second copy was loaded.\n"
            L"    Restart CS2 to apply fresh offsets. Existing offsets were not revalidated.\n";
        return 0;
    }
    std::wcout << L"[*] Auto-patching offsets with cs2-dumper (up to 2 minutes)...\n" << std::flush;
    try {
    const auto patch = PreparePatch(dumper, process.value, pid);
    std::wcout << L"[+] Validated " << std::size(OffsetPatch::Fields) << L" offsets for build " << patch.build << L".\n";
    if (check) {
        std::wcout << L"[+] Files, target and offsets checked; ready to load. No patch was applied.\n";
        return 0;
    }
    PatchChannel channel(patch);
    std::wcout << L"[*] Loading MinHook dependency...\n";
    LoadDll(process.value, pid, dependency);
    std::wcout << L"[*] Loading W1RE.dll...\n";
    LoadDll(process.value, pid, dll);
    channel.Wait(process.value);
    std::wcout << L"[+] Offset patch applied and confirmed by W1RE.dll.\n"
        L"[+] W1RE.dll loaded. Overlay initialization continues in its own console.\n"
        L"    Insert: toggle interface. End: unload overlay.\n";
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string("WARNING: Offset auto-patch/load failed. ") + error.what() +
            "\nNo success was confirmed. If the DLL was loaded, restart CS2 before retrying.");
    }
    return 0;
}
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleTitleW(L"W1RE // Loader");
    SetConsoleOutputCP(CP_UTF8);
    std::wcout << L"W1RE // Loader\n\n";
    int result = 1;
    try { result = Run(argc, argv); }
    catch (const std::exception& error) { std::cerr << "[x] " << error.what() << '\n'; }
    bool noPause = false;
    for (int i = 1; i < argc; ++i)
        if (!wcscmp(argv[i], L"--no-pause") || !wcscmp(argv[i], L"--help") || !wcscmp(argv[i], L"-h")) noPause = true;
    DWORD processes[2]{};
    DWORD mode = 0;
    if (!noPause && GetConsoleProcessList(processes, 2) == 1 &&
        GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode)) {
        std::wcout << L"\nPress Enter to close...";
        std::wstring line;
        std::getline(std::wcin, line);
    }
    return result;
}
