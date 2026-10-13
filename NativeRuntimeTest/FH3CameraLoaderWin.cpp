#ifdef _WIN32
#include "RetailBuildIdentityWin.hpp"
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
    if(argc!=2 || (wcscmp(argv[1],L"--check")!=0 && wcscmp(argv[1],L"--activate")!=0)) {
        std::wprintf(L"FH3 Native Camera Stage 5 | --check (read-only) or --activate (inject test DLL)\n");return 1;
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
    wchar_t self[32768]{};
    if(!GetModuleFileNameW(nullptr,self,32768))return 4;
    std::wstring library=self;
    const auto slash=library.find_last_of(L"\\/");
    if(slash==std::wstring::npos)return 4;
    library.resize(slash+1);
    library+=L"FH3NativeCameraTest.dll";
    if(GetFileAttributesW(library.c_str())==INVALID_FILE_ATTRIBUTES) {
        std::fwprintf(stderr,L"DLL missing next to loader: %ls\n",library.c_str());return 4;
    }
    if(ModuleBase(game.pid,L"FH3NativeCameraTest.dll")) {
        std::wprintf(L"Native camera test already loaded.\n");return 0;
    }
    HANDLE process=OpenProcess(PROCESS_CREATE_THREAD|PROCESS_QUERY_INFORMATION|PROCESS_VM_OPERATION|PROCESS_VM_WRITE|PROCESS_VM_READ,FALSE,game.pid);
    if(!process) {std::fwprintf(stderr,L"OpenProcess failed: %lu\n",GetLastError());return 5;}
    void* remote=nullptr;
    HANDLE thread=nullptr;
    int status=6;
    do {
        const SIZE_T bytes=(library.size()+1)*sizeof(wchar_t);
        remote=VirtualAllocEx(process,nullptr,bytes,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        if(!remote)break;
        SIZE_T written{};
        if(!WriteProcessMemory(process,remote,library.c_str(),bytes,&written)||written!=bytes)break;
        // Resolve the actual export-owning DLL (often KernelBase) in the
        // target process instead of assuming local ASLR addresses match.
        const auto local=GetModuleHandleW(L"kernel32.dll");
        if(!local)break;
        const auto localLoad=GetProcAddress(local,"LoadLibraryW");
        if(!localLoad)break;
        const auto localProc=reinterpret_cast<std::uintptr_t>(localLoad);
        // On some Windows builds this export forwards to KernelBase. Abort
        // rather than using an RVA relative to the wrong DLL.
        MEMORY_BASIC_INFORMATION mbi{};
        if(!VirtualQuery(localLoad,&mbi,sizeof(mbi)))break;
        wchar_t ownerPath[MAX_PATH]{};
        if(!GetModuleFileNameW(reinterpret_cast<HMODULE>(mbi.AllocationBase),ownerPath,MAX_PATH))break;
        std::wstring owner=ownerPath;
        const auto last=owner.find_last_of(L"\\/");if(last!=std::wstring::npos)owner.erase(0,last+1);
        const auto remoteOwner=ModuleBase(game.pid,owner.c_str());
        const auto relativeBase=reinterpret_cast<std::uintptr_t>(mbi.AllocationBase);
        if(!remoteOwner || localProc<relativeBase)break;
        const auto remoteProc=reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteOwner+(localProc-relativeBase));
        thread=CreateRemoteThread(process,nullptr,0,remoteProc,remote,0,nullptr);
        if(!thread)break;
        if(WaitForSingleObject(thread,15000)!=WAIT_OBJECT_0)break;
        // GetExitCodeThread truncates HMODULE to DWORD on x64; instead inspect
        // whether our module is present in the process after LoadLibrary.
        Sleep(300);
        if(!ModuleBase(game.pid,L"FH3NativeCameraTest.dll"))break;
        status=0;
    } while(false);
    if(status)std::fwprintf(stderr,L"Native DLL loading failed; Win32 error %lu (anti-tamper or process access may prevent injection)\n",GetLastError());
    else std::wprintf(L"DLL_LOADED. Ctrl+Alt+F8 FreeCam, Ctrl+Alt+F9 restore. Log: %%TEMP%%\\FH3_Native_Camera_Stage5.log\n");
    if(thread)CloseHandle(thread);
    if(remote && ((!thread) || WaitForSingleObject(thread,0)==WAIT_OBJECT_0))
        VirtualFreeEx(process,remote,0,MEM_RELEASE);
    CloseHandle(process);
    return status;
}
#endif
