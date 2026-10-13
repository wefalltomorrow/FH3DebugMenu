#ifdef _WIN32
#include "RetailBuildIdentityWin.hpp"
#include "FH3ProcessDiagnosticsWin.hpp"
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <cstdio>
#include <cwchar>
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
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID);
        if(!process) continue;
        std::array<wchar_t,32768> path{};
        DWORD size=static_cast<DWORD>(path.size());
        if(QueryFullProcessImageNameW(process,0,path.data(),&size)) {
            result.pid=entry.th32ProcessID;result.path=path.data();
        }
        CloseHandle(process);
        if(result.pid) break;
    } while(Process32NextW(snap,&entry));
    CloseHandle(snap);return result;
}
std::uintptr_t ModuleBase(DWORD pid, const wchar_t* name) {
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    if(snap==INVALID_HANDLE_VALUE)return 0;
    MODULEENTRY32W e{};e.dwSize=sizeof(e);
    std::uintptr_t address{};
    if(Module32FirstW(snap,&e)) do {
        if(_wcsicmp(e.szModule,name)==0) {address=reinterpret_cast<std::uintptr_t>(e.modBaseAddr);break;}
    } while(Module32NextW(snap,&e));
    CloseHandle(snap);return address;
}
}
int wmain(int argc, wchar_t** argv) {
    if(sizeof(void*)!=8) {std::fwprintf(stderr,L"ERROR: 64-bit loader required\n");return 2;}
    if(argc!=2 || (wcscmp(argv[1],L"--check")!=0 && wcscmp(argv[1],L"--activate")!=0 &&
                   wcscmp(argv[1],L"--diagnose")!=0 && wcscmp(argv[1],L"--probe")!=0)) {
        std::wprintf(L"FH3 Native Camera Stage 5.2 | --check / --diagnose (read-only), --probe (inert DLL), --activate (camera DLL)\n");return 1;
    }
    const auto game=FindGame();
    if(!game.pid) {std::fwprintf(stderr,L"FH3 retail process not found\n");return 2;}
    if(!FH3Stage5::VerifyRetailFile(game.path)) {
        std::fwprintf(stderr,L"ABORT: unsupported game executable hash\n");return 3;
    }
    std::wprintf(L"Verified FH3 PID %lu; supported executable SHA-256 MATCH\n",game.pid);
    if(wcscmp(argv[1],L"--check")==0) {
        std::wprintf(L"CHECK_OK: no game memory changed.\n");return 0;
    }
    if(wcscmp(argv[1],L"--diagnose")==0) {
        FH3PrintProcessDiagnostics(game.pid);
        std::wprintf(L"DIAG_OK: no game memory changed.\n");
        return 0;
    }
    const bool probe=(wcscmp(argv[1],L"--probe")==0);
    const wchar_t* dllName=probe ? L"FH3NativeLoadProbe.dll" : L"FH3NativeCameraTest.dll";
    wchar_t self[32768]{};
    if(!GetModuleFileNameW(nullptr,self,32768))return 4;
    std::wstring library=self;
    const auto slash=library.find_last_of(L"\\/");
    if(slash==std::wstring::npos)return 4;
    library.resize(slash+1);
    library+=dllName;
    if(GetFileAttributesW(library.c_str())==INVALID_FILE_ATTRIBUTES) {
        std::fwprintf(stderr,L"DLL missing next to loader: %ls\n",library.c_str());return 4;
    }
    if(ModuleBase(game.pid,dllName)) {
        std::wprintf(L"%ls already loaded.\n",dllName);return 0;
    }
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
            if(ModuleBase(game.pid,dllName)) {
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
        if(probe) std::wprintf(L"PROBE_LOADED: minimal inert DLL loaded; no camera hooks or game-state changes.\n");
        else std::wprintf(L"DLL_LOADED. Ctrl+Alt+F8 FreeCam; Ctrl+Alt+F9 restore. Log: %%TEMP%%\\FH3_Native_Camera_Stage5.log\n");
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
        if(probe)std::fwprintf(stderr,L"INERT_PROBE_FAILED: target also failed to load the dependency-free probe DLL. Check --diagnose policies and process file access.\n");
        else std::fwprintf(stderr,L"CAMERA_DLL_FAILED: try --probe to distinguish a camera-specific failure from a general loading restriction.\n");
        std::fwprintf(stderr,L"Do not assume anti-tamper until the failing operation is identified.\n");
    }

    if(thread)CloseHandle(thread);
    // A timed-out remote thread could still be reading the string.
    if(remote && (!thread || completed))VirtualFreeEx(process,remote,0,MEM_RELEASE);
    CloseHandle(process);
    return loaded ? 0 : 6;
}
#endif
