#ifdef _WIN32
#include "RetailBuildIdentityWin.hpp"
#include "CameraRequestState.hpp"
#include "CameraModeBridge.hpp"
#include "CameraObjectLocator.hpp"
#include "CameraOwnerResolver.hpp"
#include "CameraModeCatalog.hpp"
#include <windows.h>
#include <atomic>
#include <array>
#include <iterator>
#include <cstring>
#include <cstdio>
#include <cstdint>
#include <string>

// Experimental native retail FH3 camera-mode test. NOT the restored menu.
// Only this exact file SHA and confirmed vtables are supported.
// No RTSS dependency. A temporary, instance-specific vtable swap dispatches
// the camera command at the end of CForza's game update method.
namespace {
using namespace FH3Native;
constexpr std::uintptr_t kImageSize = 121211392;
constexpr std::uintptr_t kCForzaVtableRva = 0x5423EC0;
constexpr std::uintptr_t kCForzaColRva = 0x44268E0;
constexpr std::uintptr_t kCForzaGlobalRva = 0x5CB5BF0;
constexpr std::uintptr_t kFrameMethodRva = 0x1B3D830;
constexpr std::size_t kFrameSlot = 18;
constexpr std::size_t kVtableSlots = 31;
constexpr std::uintptr_t kOwnerVtableRva = 0x511F108;
constexpr std::uintptr_t kCameraVtableRva = 0x511F428;
constexpr std::uintptr_t kSetModeRva = 0x1D63BB0;
constexpr std::uintptr_t kGetModeRva = 0x1D5DEB0;

std::uintptr_t g_base{};
HMODULE g_self{};
void** g_originalVtable{};
void** g_ownedVtable{};      // points to first executable method
void* g_forzaObject{};
using UpdateFn = void (*)(void*, float); // MS x64: rcx=this; xmm1=float frameDelta
UpdateFn g_originalUpdate{};
CameraRequestState g_requests;
std::atomic<unsigned long long> g_frames{0};
std::atomic<int> g_result{0};
std::atomic<bool> g_installed{false};
std::atomic<bool> g_commandsActive{true};
HANDLE g_log = INVALID_HANDLE_VALUE;

void Log(const char* message) noexcept {
    if (g_log == INVALID_HANDLE_VALUE) return;
    DWORD written=0;
    WriteFile(g_log, message, static_cast<DWORD>(std::strlen(message)), &written, nullptr);
    WriteFile(g_log, "\r\n", 2, &written, nullptr);
}

bool Readable(const void* address, std::size_t bytes) noexcept {
    if(!address || !bytes) return false;
    auto at = reinterpret_cast<std::uintptr_t>(address);
    if (at > UINTPTR_MAX - bytes) return false;
    const auto end=at+bytes;
    while(at<end) {
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<const void*>(at),&info,sizeof(info)) || info.State!=MEM_COMMIT ||
           (info.Protect & (PAGE_GUARD|PAGE_NOACCESS))) return false;
        const DWORD prot=info.Protect & 0xff;
        if (prot != PAGE_READONLY && prot != PAGE_READWRITE && prot != PAGE_WRITECOPY &&
            prot != PAGE_EXECUTE_READ && prot != PAGE_EXECUTE_READWRITE && prot != PAGE_EXECUTE_WRITECOPY) return false;
        const auto next=reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize;
        if(next<=at) return false;
        at=next;
    }
    return true;
}
bool Executable(const void* address) noexcept {
    MEMORY_BASIC_INFORMATION info{};
    if (!address || !VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD|PAGE_NOACCESS))) return false;
    const DWORD p=info.Protect & 0xff;
    return p==PAGE_EXECUTE || p==PAGE_EXECUTE_READ || p==PAGE_EXECUTE_READWRITE || p==PAGE_EXECUTE_WRITECOPY;
}
class LocalReader final : public MemoryReader {
public:
    bool Read(std::uintptr_t address, void* destination, std::size_t size) const noexcept override {
        // The instance-specific frame hook swaps CForza's vtable pointer.
        // Existing Stage 2/3 locators require the original, verified pointer;
        // expose the original solely to their read-only identity check.
        if (g_installed.load(std::memory_order_acquire) &&
            address == reinterpret_cast<std::uintptr_t>(g_forzaObject) &&
            size == sizeof(std::uintptr_t) && g_originalVtable) {
            const auto original=reinterpret_cast<std::uintptr_t>(g_originalVtable);
            std::memcpy(destination,&original,sizeof(original));
            return true;
        }
        if (!Readable(reinterpret_cast<const void*>(address),size)) return false;
        std::memcpy(destination,reinterpret_cast<const void*>(address),size);
        return true;
    }
};

class LiveCamera final : public ICameraCommands {
public:
    explicit LiveCamera(const CameraOwnerResolution& info) noexcept : owner_(info.owner), camera_(reinterpret_cast<void*>(info.owner)) {}
    bool IsRegistered(std::uint32_t id) const noexcept override {
        if (id >= 1024) return false;
        LocalReader reader;
        std::uintptr_t begin{}, end{}, entry{};
        if (!reader.Read(owner_+0x1A8,&begin,sizeof(begin)) ||
            !reader.Read(owner_+0x1B0,&end,sizeof(end)) || end < begin ||
            (end-begin) % 0x40 != 0 || id >= (end-begin)/0x40) return false;
        const auto where=begin+std::uintptr_t(id)*0x40+0x30;
        return reader.Read(where,&entry,sizeof(entry)) && entry != 0;
    }
    bool ReadMode(std::uint32_t& value) const noexcept override {
        const auto mode=camera_.GetMode();
        if (!mode) return false;
        value=*mode; return true;
    }
    bool WriteMode(std::uint32_t mode) const noexcept override {
        // The original setter returns void. The actual gameplay result must
        // still be verified by observing the camera in the live game.
        return camera_.SetMode(static_cast<CameraMode>(mode));
    }
    std::uintptr_t OwnerId() const noexcept override {return owner_;}
private:
    std::uintptr_t owner_{};
    CameraModeBridge camera_;
};

CameraOwnerResolution ResolveCamera() noexcept {
    LocalReader reader;
    CameraOwnerResolution failed{};
    const auto located=LocateCameraObjects(reader,g_base,8);
    if(located.status!=CameraLocateStatus::Success) return failed;
    for(const auto& view:located.inspectedObjects) {
        if(view.getterMethod!=g_base+0x42A8A0) continue;
        const auto r=ResolveRetailCameraOwner(reader,g_base,view.object);
        if(r.status!=CameraOwnerStatus::Success) continue;
        if(r.ownerVtable!=g_base+kOwnerVtableRva || r.cameraVtable!=g_base+kCameraVtableRva ||
           r.setModeFunction!=g_base+kSetModeRva || r.getModeFunction!=g_base+kGetModeRva) continue;
        return r;
    }
    return failed;
}

void HookedUpdate(void* forza, float frameDelta) noexcept {
    // Never swallow or replace the original engine frame method.
    g_originalUpdate(forza,frameDelta);
    g_frames.fetch_add(1,std::memory_order_relaxed);
    if (!g_commandsActive.load(std::memory_order_relaxed) || !g_requests.Pending()) return;
    // Resolve again *on the game update thread*: view ownership can change.
    const auto info=ResolveCamera();
    if(info.status!=CameraOwnerStatus::Success) return;
    LiveCamera camera(info);
    const auto result=g_requests.OnGameFrame(camera);
    if(result!=CommandResult::NoRequest && result!=CommandResult::Deferred) {
        g_result.store(static_cast<int>(result),std::memory_order_release);
    }
}

bool InstallHook() noexcept {
    LocalReader reader;
    std::uintptr_t object{};
    if (!reader.Read(g_base+kCForzaGlobalRva,&object,sizeof(object)) || !object) return false;
    std::uintptr_t vtable{};
    if (!reader.Read(object,&vtable,sizeof(vtable)) || vtable!=g_base+kCForzaVtableRva) return false;
    std::uintptr_t col{};
    if (!reader.Read(vtable-sizeof(void*),&col,sizeof(col)) || col!=g_base+kCForzaColRva) return false;
    const auto old=reinterpret_cast<void**>(vtable);
    if (!Readable(old,kVtableSlots*sizeof(void*))) return false;
    // A version-specific size check prevents copying adjacent vtables.
    for(std::size_t i=0;i<kVtableSlots;++i)
        if(!Executable(old[i])) return false;
    if (old[kFrameSlot]!=reinterpret_cast<void*>(g_base+kFrameMethodRva)) return false;
    const auto allocation=VirtualAlloc(nullptr,(kVtableSlots+1)*sizeof(void*),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!allocation) return false;
    auto* copy=reinterpret_cast<void**>(allocation);
    copy[0]=reinterpret_cast<void*>(col);
    for(std::size_t i=0;i<kVtableSlots;++i) copy[i+1]=old[i];
    g_originalUpdate=reinterpret_cast<UpdateFn>(old[kFrameSlot]);
    copy[kFrameSlot+1]=reinterpret_cast<void*>(&HookedUpdate);
    DWORD oldProtection{};
    if(!VirtualProtect(allocation,(kVtableSlots+1)*sizeof(void*),PAGE_READONLY,&oldProtection)) {
        VirtualFree(allocation,0,MEM_RELEASE);return false;
    }
    g_originalVtable=old;
    g_ownedVtable=copy+1;
    g_forzaObject=reinterpret_cast<void*>(object);
    auto* field=reinterpret_cast<PVOID volatile*>(object);
    auto replacement=reinterpret_cast<PVOID>(copy+1);
    const auto previous=InterlockedCompareExchangePointer(field,replacement,reinterpret_cast<PVOID>(old));
    if(previous!=old) {
        g_originalVtable=nullptr;g_ownedVtable=nullptr;g_forzaObject=nullptr;
        VirtualFree(allocation,0,MEM_RELEASE);return false;
    }
    g_installed.store(true,std::memory_order_release);
    return true;
}

bool ForegroundIsGame() noexcept {
    HWND hwnd=GetForegroundWindow();
    if (!hwnd) return false;
    DWORD pid{};
    if (GetWindowThreadProcessId(hwnd,&pid) && pid==GetCurrentProcessId()) return true;
    // UWP host windows can own the top-level HWND while the actual game
    // owns a child. Accept a foreground tree containing a game-owned HWND.
    struct Search { DWORD target; bool found; } search{GetCurrentProcessId(),false};
    EnumChildWindows(hwnd,[](HWND child, LPARAM context)->BOOL {
        auto* data=reinterpret_cast<Search*>(context);
        DWORD childPid{};
        GetWindowThreadProcessId(child,&childPid);
        if(childPid==data->target) {data->found=true; return FALSE;}
        return TRUE;
    },reinterpret_cast<LPARAM>(&search));
    return search.found;
}
void OpenLog() noexcept {
    wchar_t temp[MAX_PATH]{};
    if(!GetTempPathW(MAX_PATH,temp))return;
    std::wstring path=std::wstring(temp)+L"FH3_Native_Camera_Stage5.log";
    g_log=CreateFileW(path.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
}
DWORD WINAPI Worker(LPVOID) {
    OpenLog();Log("Stage 5 native camera test: starting; no RTSS dependency");
    wchar_t exe[MAX_PATH*2]{};
    if(!GetModuleFileNameW(nullptr,exe,static_cast<DWORD>(std::size(exe))) || !FH3Stage5::VerifyRetailFile(exe)) {
        Log("ABORT: executable SHA-256 does not match supported retail build");return 0;
    }
    g_base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if(!g_base) {Log("ABORT: game module unavailable");return 0;}
    for(int i=0;i<120 && !InstallHook();++i) Sleep(500);
    if(!g_installed.load()) {Log("ABORT: exact 31-slot CForza vtable hook could not be installed");return 0;}
    Log("HOOK_INSTALLED. Ctrl+Alt+F8 FreeCam; Ctrl+Alt+F9 restore; Ctrl+Alt+F12 disable commands");
    constexpr UINT mods=MOD_CONTROL|MOD_ALT|MOD_NOREPEAT;
    if(!RegisterHotKey(nullptr,801,mods,VK_F8) || !RegisterHotKey(nullptr,802,mods,VK_F9) ||
       !RegisterHotKey(nullptr,803,mods,VK_F12)) {
        Log("ERROR: RegisterHotKey failed; command input unavailable");
    }
    int priorResult=0;
    unsigned long long priorFrames=0;
    ULONGLONG priorHeartbeat=GetTickCount64();
    while(g_commandsActive.load()) {
        MSG msg{};
        while(PeekMessageW(&msg,nullptr,WM_HOTKEY,WM_HOTKEY,PM_REMOVE)) {
            if(!ForegroundIsGame()) {Log("HOTKEY_IGNORED: FH3 was not foreground"); continue;}
            if(msg.wParam==801) {g_requests.Queue(NativeRequest::Free);Log("REQUEST: FreeCam mode 6");}
            else if(msg.wParam==802) {g_requests.Queue(NativeRequest::Restore);Log("REQUEST: Restore previous mode");}
            else if(msg.wParam==803) {g_commandsActive.store(false);Log("Commands disabled (DLL stays loaded until FH3 exits)");}
        }
        const int now=g_result.load();
        if(now && now!=priorResult) {
            if(now==static_cast<int>(CommandResult::Applied))Log("ENGINE_CALL: camera mode 6 sent to retail setter");
            else if(now==static_cast<int>(CommandResult::RestoreApplied))Log("ENGINE_CALL: restore mode sent to retail setter");
            else if(now==static_cast<int>(CommandResult::InvalidMode))Log("ENGINE_CALL: mode refused by validation");
            else if(now==static_cast<int>(CommandResult::CallFailed))Log("ENGINE_CALL: failed before game setter");
            priorResult=now;
        }
        const auto nowTick=GetTickCount64();
        if(nowTick-priorHeartbeat>=5000) {
            const auto frames=g_frames.load();
            char line[180]{};
            if(frames==priorFrames) {
                Log("NO_FRAME_CALLBACK: cloned CForza vtable has not received an update in 5s");
            } else {
                std::snprintf(line,sizeof(line),"FRAME_CALLBACK_ACTIVE count=%llu pending=%d",
                              frames,g_requests.Pending());
                Log(line);
            }
            priorFrames=frames;
            priorHeartbeat=nowTick;
        }
        Sleep(50);
    }
    UnregisterHotKey(nullptr,801);UnregisterHotKey(nullptr,802);UnregisterHotKey(nullptr,803);
    Log("Stage 5 input worker stopped. Native module remains loaded until game exit.");
    return 0;
}
}
extern "C" __declspec(dllexport) unsigned long long FH3Stage5FrameCount() noexcept {
    return g_frames.load(std::memory_order_relaxed);
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        g_self=module;
        DisableThreadLibraryCalls(module);
        HANDLE thread=CreateThread(nullptr,0,&Worker,nullptr,0,nullptr);
        if(thread) CloseHandle(thread);
    }
    return TRUE;
}
#endif
