#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Deliberately minimal inert DLL to distinguish target-process DLL-loading
// restrictions from dependencies or initialization failures in the camera DLL.
// It installs no hooks, creates no threads, and changes no game state.
extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) noexcept {
    return TRUE;
}
#endif
