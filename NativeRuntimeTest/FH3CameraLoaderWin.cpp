#ifdef _WIN32
#include "RetailBuildIdentityWin.hpp"
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <cstdio>
#include <cwchar>
#include <cstdint>
#include <array>

// One-time native test loader. It DOES modify the selected FH3 process by
// loading the opt-in experimental DLL. --check is read-only.
namespace {
struct GameInfo {DWORD pid{}; std::wstring path;};
GameInfo FindGame() {
    GameInfo result{};
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snap==INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    if(Process32FirstW(snap,&entry)) do {
        if(_wcsicmp(entry.szExeFile,FH3Stage5::kGameExe)!=0) continue;
    
    // Stage 5.1: capture the result at EVERY failing API. An unsuccessful
    // Module32NextW ordinarily sets ERROR_NO_MORE_FILES (18); it cannot
    // diagnose CreateRemoteThread, LoadLibraryW, or DllMain failures.
    HANDLE process=OpenProcess(PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|
                               PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ,
                               FALSE,game.pid);
    if(!process) {
        const DWORD e=GetLastError();
        std::fwprintf(stderr,L"FAIL OpenProcess: Win32 %lu\n",e);
        return 5;
    }

    void* remote=nullptr;
    HANDLE thread=nullptr;
    bool completed=false;
    bool loaded=false;
    DWORD threadExit=STILL_ACTIVE;
    const wchar_t* failStage=nullptr;
    DWORD failError=0;

    do {
        const SIZE_T bytes=(library.size()+1)*sizeof(wchar_t);
        remote=VirtualAllocEx(process,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        if(!remote) {
            failStage=L"VirtualAllocEx";
            failError=GetLastError();
            break;
        }
        std::wprintf(L"OK Remote allocation: %zu bytes\n",static_cast<std::size_t>(bytes));

        SIZE_T written{};
        if(!WriteProcessMemory(process,remote,library.c_str(),bytes,&written)) {
            failStage=L"WriteProcessMemory";
            failError=GetLastError();
            break;
        }
        if(written!=bytes) {
            failStage=L"WriteProcessMemory (short write)";
            failError=ERROR_WRITE_FAULT;
            break;
        }
        std::wprintf(L"OK Remote DLL path written\n");

        const auto local=GetModuleHandleW(L"kernel32.dll");
        if(!local) {
            failStage=L"GetModuleHandleW(kernel32)";
            failError=GetLastError();
            break;
        }
        const auto localLoad=GetProcAddress(local,"LoadLibraryW");
        if(!localLoad) {
            failStage=L"GetProcAddress(LoadLibraryW)";
            failError=GetLastError();
            break;
        }
        MEMORY_BASIC_INFORMATION mbi{};
        if(!VirtualQuery(localLoad,&mbi,sizeof(mbi))) {
            failStage=L"VirtualQuery(LoadLibraryW)";
            failError=GetLastError();
            break;
        }
        wchar_t ownerPath[MAX_PATH]{};
        if(!GetModuleFileNameW(reinterpret_cast<HMODULE>(mbi.AllocationBase),ownerPath,MAX_PATH)) {
            failStage=L"GetModuleFileNameW(LoadLibrary owner)";
            failError=GetLastError();
            break;
        }
        std::wstring owner=ownerPath;
        const auto last=owner.find_last_of(L"\\/");
        if(last!=std::wstring::npos)owner.erase(0,last+1);
        const auto localProc=reinterpret_cast<std::uintptr_t>(localLoad);
        const auto relativeBase=reinterpret_cast<std::uintptr_t>(mbi.AllocationBase);
        if(localProc<relativeBase) {
            failStage=L"Invalid local LoadLibraryW RVA";
            break;
        }
        const auto remoteOwner=ModuleBase(game.pid,owner.c_str());
        if(!remoteOwner) {
            failStage=L"Locate remote LoadLibraryW owner";
            // ERROR_NO_MORE_FILES is an ordinary enumeration result.
            // It is NOT an injection or anti-tamper error.
            break;
        }
        const auto remoteProc=reinterpret_cast<LPTHREAD_START_ROUTINE>(
            remoteOwner+(localProc-relativeBase));
        std::wprintf(L"OK LoadLibraryW owner: %ls; remote entrypoint: 0x%llX\n",
                     owner.c_str(),static_cast<unsigned long long>(
                         reinterpret_cast<std::uintptr_t>(remoteProc)));

        thread=CreateRemoteThread(process,nullptr,0,remoteProc,remote,0,nullptr);
        if(!thread) {
            failStage=L"CreateRemoteThread";
            failError=GetLastError();
            break;
        }
        std::wprintf(L"OK Remote thread created\n");
        const DWORD wait=WaitForSingleObject(thread,15000);
        if(wait!=WAIT_OBJECT_0) {
            failStage=(wait==WAIT_TIMEOUT)?L"Remote thread timed out":L"WaitForSingleObject";
            if(wait==WAIT_FAILED)failError=GetLastError();
            break;
        }
        completed=true;
        if(!GetExitCodeThread(thread,&threadExit)) {
            failStage=L"GetExitCodeThread";
            failError=GetLastError();
            break;
        }
        std::wprintf(L"REMOTE_THREAD_EXIT: 0x%08lX (%lu)\n",threadExit,threadExit);
        std::wprintf(L"NOTE: x64 LoadLibraryW returns a 64-bit module handle; the thread exit code retains only 32 bits.\n");
        // The success criterion is target module presence, never the
        // truncated HMODULE returned by GetExitCodeThread.
        for(int attempt=0;attempt<30;++attempt) {
            if(ModuleBase(game.pid,L"FH3NativeCameraTest.dll")) {
                loaded=true;
                break;
            }
            Sleep(100);
        }
        if(!loaded) {
            failStage=L"DLL not found in target module list after remote thread exit";
            break;
        }
    } while(false);

    if(loaded) {
        std::wprintf(L"DLL_LOADED. Ctrl+Alt+F8 FreeCam; Ctrl+Alt+F9 restore. Log: %%TEMP%%\\FH3_Native_Camera_Stage5.log\n");
    } else {
        std::fwprintf(stderr,L"ACTIVATION_FAILED at: %ls\n",
                      failStage?failStage:L"unknown stage");
        if(failError!=0)
            std::fwprintf(stderr,L"FAIL Win32 code: %lu (0x%08lX)\n",failError,failError);
        else
            std::fwprintf(stderr,L"No meaningful Win32 error was returned at this stage.\n");
        if(completed && threadExit==0)
            std::fwprintf(stderr,L"LoadLibraryW may have returned NULL; the target process's actual loader error is not available from this thread exit code.\n");
        else if(completed)
            std::fwprintf(stderr,L"Remote thread finished but target DLL was not confirmed loaded.\n");
        if(failStage && std::wcscmp(failStage,L"Locate remote LoadLibraryW owner")==0)
            std::fwprintf(stderr,L"Remote owner module missing or hidden; this is NOT Win32 error 18 as an injection failure.\n");
        std::fwprintf(stderr,L"Do not assume anti-tamper until the failing operation is identified.\n");
    }

    if(thread)CloseHandle(thread);
    // A timed-out remote thread could still be reading the string.
    if(remote && (!thread || completed))VirtualFreeEx(process,remote,0,MEM_RELEASE);
    CloseHandle(process);
    return loaded ? 0 : 6;
}
#endif
