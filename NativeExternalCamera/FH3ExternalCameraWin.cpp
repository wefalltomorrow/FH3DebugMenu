#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>
#include <cstring>
#include "RetailBuildIdentityWin.hpp"
#include "CameraObjectLocator.hpp"
#include "CameraOwnerResolver.hpp"
#include "CameraModeCatalog.hpp"
#include "RemoteCallStub.hpp"

// Stage 6 independent external camera bridge. Does not load or map a DLL.
// Adapted from FH3DebugMenu's own read-only camera-resolver code. No source
// copied from Forza-Mods-AIO. These commands are exclusively offline tests.
namespace {
using namespace FH3Native;
using namespace FH3External;
constexpr std::uintptr_t kOwnerVtableRva = 0x511F108;
constexpr std::uintptr_t kCameraVtableRva = 0x511F428;
constexpr std::uintptr_t kSetModeRva = 0x1D63BB0;
constexpr std::uintptr_t kGetModeRva = 0x1D5DEB0;

struct GameInfo { DWORD pid{}; std::wstring path; };
GameInfo FindGame() {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    GameInfo game{};
    if(snapshot==INVALID_HANDLE_VALUE) return game;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    if(Process32FirstW(snapshot,&entry)) do {
        if(_wcsicmp(entry.szExeFile,FH3Stage5::kGameExe))continue;
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,entry.th32ProcessID);
        if(!process)continue;
        std::array<wchar_t,32768> file{};
        DWORD length=static_cast<DWORD>(file.size());
        if(QueryFullProcessImageNameW(process,0,file.data(),&length)) {
            game.pid=entry.th32ProcessID;
            game.path=file.data();
        }
        CloseHandle(process);
        if(game.pid)break;
    } while(Process32NextW(snapshot,&entry));
    CloseHandle(snapshot);
    return game;
}
std::uintptr_t ModuleBase(DWORD pid,const wchar_t* module) {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    if(snapshot==INVALID_HANDLE_VALUE)return 0;
    MODULEENTRY32W entry{}; entry.dwSize=sizeof(entry);
    std::uintptr_t base=0;
    if(Module32FirstW(snapshot,&entry)) do {
        if(!_wcsicmp(entry.szModule,module)) {
            base=reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
            break;
        }
    } while(Module32NextW(snapshot,&entry));
    CloseHandle(snapshot);
    return base;
}
class ProcessReader final : public MemoryReader {
public:
    explicit ProcessReader(HANDLE process) noexcept: process_(process) {}
    bool Read(std::uintptr_t address,void* dst,std::size_t length)const noexcept override {
        if(!address||!dst||!length)return false;
        SIZE_T actual{};
        return ReadProcessMemory(process_,reinterpret_cast<const void*>(address),
                                 dst,length,&actual)!=FALSE && actual==length;
    }
private:
    HANDLE process_;
};
struct CameraView {
    std::uintptr_t owner{},interface{},set{},get{},handler{},splitView{};
    std::size_t totalModes{};
};
bool ConfirmGetterBytes(const ProcessReader& reader,std::uintptr_t base) {
    const std::array<std::uint8_t,8> expected{0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0xC3};
    std::array<std::uint8_t,8> actual{};
    return reader.Read(base+kRetailSplitViewGetterRva,actual.data(),actual.size())
        && actual==expected;
}
bool ConfirmSetterBytes(const ProcessReader& reader,std::uintptr_t base) {
    const std::array<std::uint8_t,12> expected{
        0x48,0x81,0xC1,0x60,0xFE,0xFF,0xFF,0xE9,0x14,0xF8,0xFF,0xFF
    };
    std::array<std::uint8_t,12> actual{};
    return reader.Read(base+kSetModeRva,actual.data(),actual.size())
        && actual==expected;
}
bool SnapshotCamera(const ProcessReader& reader,std::uintptr_t base,CameraView& view) {
    if(!ConfirmGetterBytes(reader,base)||!ConfirmSetterBytes(reader,base)) {
        std::fwprintf(stderr,L"ABORT: pinned retail getter/setter code signature mismatch\n");
        return false;
    }
    auto scan=LocateCameraObjects(reader,base,8);
    if(scan.status!=CameraLocateStatus::Success || scan.collectionCount!=1) {
        std::fwprintf(stderr,L"ABORT: unexpected camera collection (%zu objects, status %d)\n",
                      scan.collectionCount,static_cast<int>(scan.status));
        return false;
    }
    for(const auto& object:scan.inspectedObjects) {
        if(object.getterMethod!=base+kRetailSplitViewGetterRva)continue;
        const auto resolution=ResolveRetailCameraOwner(reader,base,object.object);
        if(resolution.status!=CameraOwnerStatus::Success)continue;
        if(resolution.ownerVtable!=base+kOwnerVtableRva ||
           resolution.cameraVtable!=base+kCameraVtableRva ||
           resolution.setModeFunction!=base+kSetModeRva ||
           resolution.getModeFunction!=base+kGetModeRva) continue;
        const auto catalog=InspectCameraModeCatalog(reader,resolution.owner);
        if(catalog.status!=ModeCatalogStatus::Success)continue;
        bool supported=true;
        for(const auto& entry:catalog.commands) {
            if(entry.support!=ModeSupport::Registered) supported=false;
        }
        if(!supported) {
            std::fwprintf(stderr,L"ABORT: one or more original camera modes are not registered\n");
            return false;
        }
        view.owner=resolution.owner;
        view.interface=resolution.cameraInterface;
        view.set=resolution.setModeFunction;
        view.get=resolution.getModeFunction;
        view.splitView=object.object;
        view.totalModes=catalog.tableSize;
        for(const auto& entry:catalog.commands)
            if(entry.mode==6)view.handler=entry.handler;
        return true;
    }
    std::fwprintf(stderr,L"ABORT: no matching retail SplitView/ForzaMultiCam controller\n");
    return false;
}
bool SameCamera(const CameraView& a,const CameraView& b) {
    return a.owner==b.owner && a.interface==b.interface &&
           a.set==b.set && a.get==b.get && a.splitView==b.splitView &&
           a.handler==b.handler && a.totalModes==b.totalModes;
}
void PrintCamera(const CameraView& camera) {
    std::wprintf(L"CAMERA_OWNER_OK: SplitView=0x%llX ForzaMultiCam=0x%llX\n",
                 static_cast<unsigned long long>(camera.splitView),
                 static_cast<unsigned long long>(camera.owner));
    std::wprintf(L"CAMERA_INTERFACE=0x%llX SETTER=0x%llX GETTER=0x%llX\n",
                 static_cast<unsigned long long>(camera.interface),
                 static_cast<unsigned long long>(camera.set),
                 static_cast<unsigned long long>(camera.get));
    std::wprintf(L"REGISTERED_MODES: %zu; Follow=0 Free=6 Wheel=9 TrackSegment=12\n",
                 camera.totalModes);
}
enum class Result { Success=0,NoProcess=2,Unsupported=3,Access=4,Unstable=5,NoExecute=6,Thread=7,Unexpected=8 };
Result RunCode(HANDLE process,const std::vector<std::uint8_t>& code) {
    if(code.empty() || code.size()>4096)return Result::Unexpected;
    void* remote=VirtualAllocEx(process,nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!remote){
        std::fwprintf(stderr,L"VirtualAllocEx failed: %lu\n",GetLastError());
        return Result::NoExecute;
    }
    HANDLE thread=nullptr;
    bool finished=false;
    bool ok=false;
    do {
        SIZE_T written{};
        if(!WriteProcessMemory(process,remote,code.data(),code.size(),&written)||
           written!=code.size()){
            std::fwprintf(stderr,L"WriteProcessMemory failed: %lu\n",GetLastError());
            break;
        }
        DWORD old{};
        if(!VirtualProtectEx(process,remote,0x1000,PAGE_EXECUTE_READ,&old)){
            std::fwprintf(stderr,L"VirtualProtectEx PAGE_EXECUTE_READ failed: %lu\n",GetLastError());
            break;
        }
        if(!FlushInstructionCache(process,remote,code.size())){
            std::fwprintf(stderr,L"FlushInstructionCache failed: %lu\n",GetLastError());
            break;
        }
        thread=CreateRemoteThread(process,nullptr,0,
                                 reinterpret_cast<LPTHREAD_START_ROUTINE>(remote),
                                 nullptr,0,nullptr);
        if(!thread){
            std::fwprintf(stderr,L"CreateRemoteThread failed: %lu\n",GetLastError());
            break;
        }
        const DWORD wait=WaitForSingleObject(thread,10000);
        if(wait!=WAIT_OBJECT_0){
            std::fwprintf(stderr,L"Remote thread did not complete normally (wait=%lu; error=%lu)\n",
                          wait,GetLastError());
            break;
        }
        finished=true;
        DWORD exitCode{};
        if(!GetExitCodeThread(thread,&exitCode)){
            std::fwprintf(stderr,L"GetExitCodeThread failed: %lu\n",GetLastError());
            break;
        }
        std::wprintf(L"REMOTE_CODE_EXIT: 0x%08lX\n",exitCode);
        if(exitCode!=kSuccessMarker){
            std::fwprintf(stderr,L"ABORT: expected marker 0x%08X; remote code did not report completion\n",
                          kSuccessMarker);
            break;
        }
        ok=true;
    } while(false);
    if(thread)CloseHandle(thread);
    // If a thread is still running, DO NOT free its executable code out
    // from under it. The allocation will be reclaimed when the game exits.
    if(!thread || finished)VirtualFreeEx(process,remote,0,MEM_RELEASE);
    return ok?Result::Success:(thread?Result::Thread:Result::NoExecute);
}
}
int wmain(int argc,wchar_t** argv) {
    if(sizeof(void*)!=8) {
        std::fwprintf(stderr,L"64-bit executable required\n");
        return static_cast<int>(Result::Unsupported);
    }
    if(argc!=2) {
        std::wprintf(L"FH3 External Camera Stage 6: --inspect (read-only) | --probe-exec (inert executable probe)\n");
        std::wprintf(L"    --unsafe-freecam | --unsafe-followcam (experimental external-thread setter; may crash)\n");
        return 1;
    }
    const bool inspect=wcscmp(argv[1],L"--inspect")==0;
    const bool probe=wcscmp(argv[1],L"--probe-exec")==0;
    const bool freecam=wcscmp(argv[1],L"--unsafe-freecam")==0;
    const bool follow=wcscmp(argv[1],L"--unsafe-followcam")==0;
    if(!inspect&&!probe&&!freecam&&!follow){
        std::fwprintf(stderr,L"Unknown command; see usage\n");return 1;
    }
    const auto game=FindGame();
    if(!game.pid) {
        std::fwprintf(stderr,L"FH3 retail process not found\n");
        return static_cast<int>(Result::NoProcess);
    }
    if(!FH3Stage5::VerifyRetailFile(game.path)){
        std::fwprintf(stderr,L"ABORT: executable hash differs from supported retail v1.0.125.2\n");
        return static_cast<int>(Result::Unsupported);
    }
    const auto base=ModuleBase(game.pid,FH3Stage5::kGameExe);
    if(!base){
        std::fwprintf(stderr,L"ABORT: could not enumerate FH3 module base\n");
        return static_cast<int>(Result::Access);
    }
    const DWORD queryRights=PROCESS_QUERY_INFORMATION|PROCESS_VM_READ;
    const DWORD runRights=PROCESS_CREATE_THREAD|PROCESS_VM_OPERATION|PROCESS_VM_WRITE;
    HANDLE process=OpenProcess(queryRights | (inspect?0:runRights),FALSE,game.pid);
    if(!process){
        std::fwprintf(stderr,L"OpenProcess failed (error=%lu)\n",GetLastError());
        return static_cast<int>(Result::Access);
    }
    std::wprintf(L"FH3 PID %lu; SHA-256 MATCH; image base 0x%llX\n",
                 game.pid,static_cast<unsigned long long>(base));
    Result result=Result::Unexpected;
    do {
        ProcessReader reader(process);
        CameraView first{},second{};
        if(!SnapshotCamera(reader,base,first)) {
            result=Result::Unstable;break;
        }
        Sleep(250);
        if(!SnapshotCamera(reader,base,second) || !SameCamera(first,second)){
            std::fwprintf(stderr,L"ABORT: camera controller changed between two read-only snapshots\n");
            result=Result::Unstable;break;
        }
        PrintCamera(second);
        if(inspect){
            std::wprintf(L"INSPECT_OK: no game memory modified; no game functions invoked\n");
            result=Result::Success;break;
        }
        if(probe){
            std::wprintf(L"Executing a six-byte inert return-value probe; no game functions invoked\n");
            result=RunCode(process,BuildSentinelStub());
            if(result==Result::Success)
                std::wprintf(L"EXEC_PROBE_OK: DLL-free executable code ran in FH3; camera functions NOT tested\n");
            break;
        }
        const std::uint32_t mode=freecam?6:0;
        std::wprintf(L"WARNING: experiment will call retail camera setter on a NEW thread (not the game update thread).\n");
        std::wprintf(L"WARNING: possible hang, crash, ignored request or unstable camera behaviour. Offline testing only.\n");
        std::wprintf(L"Requesting camera mode %u using existing retail setter; not using LoadLibraryW\n",mode);
        // One last ownership check immediately before constructing a call.
        CameraView third{};
        if(!SnapshotCamera(reader,base,third) || !SameCamera(second,third)) {
            std::fwprintf(stderr,L"ABORT: camera changed before command dispatch\n");
            result=Result::Unstable;break;
        }
        result=RunCode(process,BuildSetModeStub(third.interface,third.set,mode));
        if(result==Result::Success)
            std::wprintf(L"SETTER_RETURNED: the game method returned. Confirm actual camera behaviour visually; this does not prove mode changed.\n");
    } while(false);
    CloseHandle(process);
    return static_cast<int>(result);
}
#endif
