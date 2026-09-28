// Included inside loader.cpp's anonymous namespace, after its process helpers.
struct DumpDirectory {
    fs::path path;
    DumpDirectory() {
        const auto root = fs::temp_directory_path();
        for (unsigned i = 0; i < 100; ++i) {
            auto candidate = root / (L"W1RE-offsets-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(i));
            if (fs::create_directory(candidate)) { path = candidate; return; }
        }
        throw std::runtime_error("Cannot create a fresh dump directory.");
    }
    ~DumpDirectory() { std::error_code ec; fs::remove_all(path, ec); }
};

void RunDumper(const fs::path& executable, const fs::path& output, DWORD timeout = 120000) {
    if (!fs::is_regular_file(executable))
        throw std::runtime_error("cs2-dumper.exe is missing. Place it beside the loader or use --dumper <path>.");
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    Handle log(CreateFileW((output / L"dumper.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (log.value == INVALID_HANDLE_VALUE) WinError("Creating dumper log");
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (input.value == INVALID_HANDLE_VALUE) WinError("Opening dumper input");
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = startup.hStdError = log.value;
    startup.hStdInput = input.value;
    PROCESS_INFORMATION processInfo{};
    std::wstring command = L"\"" + executable.wstring() + L"\" --file-types json --output \"" +
        output.wstring() + L"\" --process-name cs2.exe --no-log-file";
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
        nullptr, output.c_str(), &startup, &processInfo)) WinError("Starting cs2-dumper");
    Handle child(processInfo.hProcess), thread(processInfo.hThread);
    const auto started = GetTickCount64();
    DWORD wait = WAIT_TIMEOUT;
    while ((wait = WaitForSingleObject(child.value, 100)) == WAIT_TIMEOUT) {
        if (GetTickCount64() - started >= timeout) break;
    }
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(child.value, 1); // Only the dumper launched by this loader.
        WaitForSingleObject(child.value, 5000);
        throw std::runtime_error("cs2-dumper timed out or could not be waited on. No offsets were applied.");
    }
    DWORD exitCode = 1;
    if (!GetExitCodeProcess(child.value, &exitCode)) WinError("Reading dumper result");
    if (exitCode) {
        std::ifstream file(output / L"dumper.log", std::ios::binary);
        std::string detail(2048, '\0');
        file.read(detail.data(), static_cast<std::streamsize>(detail.size()));
        detail.resize(static_cast<size_t>(file.gcount()));
        throw std::runtime_error("cs2-dumper failed (exit " + std::to_string(exitCode) + "). " + detail);
    }
}

OffsetPatch::Packet PreparePatch(const fs::path& dumper, HANDLE process, DWORD pid) {
    // This dumper selects by name, so an explicit PID cannot disambiguate its scan.
    if (FindGame(0) != pid) throw std::runtime_error("The selected game process changed.");
    const auto created = OffsetPatch::CreationTime(process);
    const ULONGLONG deadline = GetTickCount64() + 120000;
    OffsetPatch::Packet packet{};
    bool waitingForDumper = false;
    for (;;) {
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline)
            throw std::runtime_error("cs2-dumper could not find CS2 within two minutes. Make sure the game is fully started and run the loader with matching permissions.");
        if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT || FindGame(0) != pid)
            throw std::runtime_error("The game exited or changed while waiting for cs2-dumper. Restart the loader.");

        DumpDirectory dump;
        try {
            RunDumper(dumper, dump.path, static_cast<DWORD>(deadline - now));
            packet = OffsetPatch::ParseDump(dump.path);
            break;
        } catch (const std::exception& error) {
            const std::string detail = error.what();
            if (detail.find("oslayer: process not found") == std::string::npos) throw;
            const ULONGLONG retryAt = GetTickCount64();
            if (retryAt >= deadline)
                throw std::runtime_error("cs2-dumper could not find CS2 within two minutes. Make sure the game is fully started and run the loader with matching permissions. " + detail);
            if (!waitingForDumper) {
                std::wcout << L"[*] cs2-dumper cannot see CS2 yet; waiting for process discovery...\n" << std::flush;
                waitingForDumper = true;
            }
            const ULONGLONG remaining = deadline - retryAt;
            Sleep(static_cast<DWORD>(remaining < 1000 ? remaining : 1000));
        }
    }
    if (WaitForSingleObject(process, 0) != WAIT_TIMEOUT || FindGame(0) != pid)
        throw std::runtime_error("The game exited or changed while dumping offsets. Restart the loader.");
    packet.pid = pid;
    packet.created = created;
    DWORD engineSize = 0;
    for (const auto& module : Modules(pid)) {
        if (!_wcsicmp(module.szModule, L"client.dll")) {
            packet.clientBase = reinterpret_cast<std::uintptr_t>(module.modBaseAddr);
            packet.clientSize = module.modBaseSize;
        }
        if (!_wcsicmp(module.szModule, L"engine2.dll")) {
            packet.engineBase = reinterpret_cast<std::uintptr_t>(module.modBaseAddr);
            engineSize = module.modBaseSize;
        }
    }
    OffsetPatch::Validate(packet);
    if (engineSize < sizeof(DWORD) || packet.buildOffset > engineSize - sizeof(DWORD))
        throw std::runtime_error("Build-number offset is outside engine2.dll.");
    DWORD liveBuild = 0;
    SIZE_T bytes = 0;
    if (!ReadProcessMemory(process, reinterpret_cast<void*>(packet.engineBase + packet.buildOffset),
        &liveBuild, sizeof(liveBuild), &bytes) || bytes != sizeof(liveBuild) || liveBuild != packet.build)
        throw std::runtime_error("Dump build does not match the running game. Update cs2-dumper and retry.");
    for (size_t i = 0; i < std::size(OffsetPatch::Fields); ++i) {
        const auto source = OffsetPatch::Fields[i].source;
        if (source != OffsetPatch::Source::Global && source != OffsetPatch::Source::Button) continue;
        unsigned char probe[64]{};
        if (!ReadProcessMemory(process, reinterpret_cast<void*>(packet.clientBase + packet.values[i]),
            probe, sizeof(probe), &bytes) || bytes != sizeof(probe))
            throw std::runtime_error(std::string("Updated address is unreadable: ") + OffsetPatch::Fields[i].name);
    }
    return packet;
}

struct PatchChannel {
    HANDLE mapping = nullptr;
    OffsetPatch::Packet* shared = nullptr;
    explicit PatchChannel(const OffsetPatch::Packet& packet) {
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
            sizeof(OffsetPatch::Packet), OffsetPatch::MappingName(packet.pid).c_str());
        const DWORD error = GetLastError();
        if (!mapping) WinError("Publishing offset patch");
        if (error == ERROR_ALREADY_EXISTS) {
            CloseHandle(mapping);
            throw std::runtime_error("Another loader is already patching this game. Wait for it to finish.");
        }
        shared = static_cast<OffsetPatch::Packet*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(packet)));
        if (!shared) { CloseHandle(mapping); throw std::runtime_error("Cannot publish offset patch."); }
        *shared = packet;
        MemoryBarrier();
    }
    ~PatchChannel() { if (shared) UnmapViewOfFile(shared); if (mapping) CloseHandle(mapping); }
    PatchChannel(const PatchChannel&) = delete;
    PatchChannel& operator=(const PatchChannel&) = delete;
    void Wait(HANDLE process, DWORD timeout = 15000) {
        const auto started = GetTickCount64();
        while (GetTickCount64() - started < timeout) {
            const LONG status = InterlockedCompareExchange(&shared->status, OffsetPatch::Pending, OffsetPatch::Pending);
            if (status == OffsetPatch::Applied) return;
            if (status == OffsetPatch::Failed) {
                const auto length = strnlen_s(shared->error, sizeof(shared->error));
                throw std::runtime_error("DLL rejected the offset patch: " + std::string(shared->error, length));
            }
            if (WaitForSingleObject(process, 50) != WAIT_TIMEOUT)
                throw std::runtime_error("The game exited before confirming the offset patch.");
        }
        throw std::runtime_error("DLL did not confirm the offset patch. Rebuild loader and DLL together, then restart CS2.");
    }
};
