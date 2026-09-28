#include <windows.h>

#ifdef LOADER_TEST_DLL
// Import MinHook so this fixture also tests dependency loading.
extern "C" __declspec(dllimport) int WINAPI MH_Initialize();
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) MH_Initialize();
    return TRUE;
}
#else
int wmain() {
    // The smoke-test script owns and terminates only this disposable process.
    Sleep(120000);
    return 0;
}
#endif
