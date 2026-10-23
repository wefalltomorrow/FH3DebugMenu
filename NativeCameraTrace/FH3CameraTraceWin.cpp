#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "RetailBuildIdentityWin.hpp"
#include "CameraObjectLocator.hpp"
#include "CameraOwnerResolver.hpp"
#include "CameraModeCatalog.hpp"
#include "CameraDelta.hpp"

// All game-process permissions are query/read. Never allocate, inject, call,
// protect, suspend, patch, hook, or write to the game.
namespace {
using namespace FH3Native;
constexpr std::uintptr_t kOwnerVtableRva=0x511F108;
constexpr std::uintptr_t kCameraVtableRva=0x511F428;
constexpr std::uintptr_t kSetModeRva=0x1D63BB0;
constexpr std::uintptr_t kGetModeRva=0x1D5DEB0;
constexpr std::size_t kCaptureBytes=0x240;
constexpr int kIntervalMs=100;
struct GameInfo {DWORD pid{}; std::wstring path;};
struct CameraInfo {
    std::uintptr_t view{},owner{},control{},setter{},getter{};
    std::size_t modeEntries{};
};
GameInfo FindGame() {
    GameInfo result{};
    const HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snap==INVALID_HANDLE_VALUE)return result;
    PROCESSENTRY32W e{};e.dwSize=sizeof(e);
    if(Process32FirstW(snap,&e))do {
        if(_wcsicmp(e.szExeFile,FH3Stage5::kGameExe)!=0)continue;
        HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,e.th32ProcessID);
        if(!p)continue;
        std::array<wchar_t,32768> path{};
        DWORD chars=static_cast<DWORD>(path.size());
        if(QueryFullProcessImageNameW(p,0,path.data(),&chars)) {
            result.pid=e.th32ProcessID;
            result.path=path.data();
        }
        CloseHandle(p);
        if(result.pid)break;
    }while(Process32NextW(snap,&e));
    CloseHandle(snap);
    return result;
}
std::uintptr_t ModuleBase(DWORD pid) {
    const HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid);
    if(snap==INVALID_HANDLE_VALUE)return 0;
    MODULEENTRY32W e{};e.dwSize=sizeof(e);
    std::uintptr_t result=0;
    if(Module32FirstW(snap,&e))do {
        if(_wcsicmp(e.szModule,FH3Stage5::kGameExe)==0){
            result=reinterpret_cast<std::uintptr_t>(e.modBaseAddr);
            break;
        }
    }while(Module32NextW(snap,&e));
    CloseHandle(snap);
    return result;
}
class Reader final:public MemoryReader {
public:
    explicit Reader(HANDLE h):process_(h){}
    bool Read(std::uintptr_t at,void* dest,std::size_t bytes)const noexcept override {
        if(!at || !dest || !bytes)return false;
        SIZE_T got=0;
        return ReadProcessMemory(process_,reinterpret_cast<const void*>(at),
                                 dest,bytes,&got)!=FALSE && got==bytes;
    }
private:
    HANDLE process_;
};
bool Signature(const Reader& reader,std::uintptr_t at,const std::uint8_t* bytes,std::size_t size) {
    std::vector<std::uint8_t> actual(size);
    return reader.Read(at,actual.data(),size) &&
           std::equal(actual.begin(),actual.end(),bytes);
}
bool Resolve(const Reader& reader,std::uintptr_t base,CameraInfo& out) {
    static constexpr std::uint8_t getter[]{0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0xC3};
    static constexpr std::uint8_t setter[]{0x48,0x81,0xC1,0x60,0xFE,0xFF,0xFF,
                                             0xE9,0x14,0xF8,0xFF,0xFF};
    if(!Signature(reader,base+kRetailSplitViewGetterRva,getter,sizeof(getter))||
       !Signature(reader,base+kSetModeRva,setter,sizeof(setter)))return false;
    const auto located=LocateCameraObjects(reader,base,8);
    if(located.status!=CameraLocateStatus::Success || located.collectionCount!=1)return false;
    for(const auto& entry:located.inspectedObjects) {
        if(entry.getterMethod!=base+kRetailSplitViewGetterRva)continue;
        const auto c=ResolveRetailCameraOwner(reader,base,entry.object);
        if(c.status!=CameraOwnerStatus::Success ||
           c.ownerVtable!=base+kOwnerVtableRva ||
           c.cameraVtable!=base+kCameraVtableRva ||
           c.setModeFunction!=base+kSetModeRva ||
           c.getModeFunction!=base+kGetModeRva)continue;
        const auto table=InspectCameraModeCatalog(reader,c.owner);
        if(table.status!=ModeCatalogStatus::Success)continue;
        if(std::any_of(table.commands.begin(),table.commands.end(),[](const auto& mode) {
            return mode.support!=ModeSupport::Registered;
        }))continue;
        out={entry.object,c.owner,c.cameraInterface,c.setModeFunction,
             c.getModeFunction,table.tableSize};
        return true;
    }
    return false;
}
std::string HexBytes(const std::vector<std::uint8_t>& bytes) {
    static constexpr char digits[]="0123456789ABCDEF";
    std::string out(bytes.size()*2,'0');
    for(std::size_t i=0;i<bytes.size();++i){
        out[i*2]=digits[bytes[i]>>4];
        out[i*2+1]=digits[bytes[i]&15];
    }
    return out;
}
bool Same(const CameraInfo& a,const CameraInfo& b) {
    return a.owner==b.owner && a.view==b.view && a.control==b.control &&
           a.setter==b.setter && a.getter==b.getter &&
           a.modeEntries==b.modeEntries;
}
std::wstring Timestamp() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t buf[64]{};
    swprintf_s(buf,L"%04u%02u%02u_%02u%02u%02u",now.wYear,now.wMonth,now.wDay,
               now.wHour,now.wMinute,now.wSecond);
    return buf;
}
bool ParseSeconds(const wchar_t* text,int& seconds) {
    if(!text || !*text)return false;
    wchar_t* end=nullptr;
    const long v=std::wcstol(text,&end,10);
    if(end==text || *end!=L'\0' || v<5 || v>120)return false;
    seconds=static_cast<int>(v);return true;
}
}
int wmain(int argc,wchar_t** argv) {
    const bool inspect=argc==2 && wcscmp(argv[1],L"--inspect")==0;
    const bool record=argc==3 && wcscmp(argv[1],L"--record")==0;
    int seconds=0;
    if(!inspect && (!record || !ParseSeconds(argv[2],seconds))) {
        std::wprintf(L"FH3 native camera-state sampler (read-only)\n");
        std::wprintf(L"  --inspect       Verify the live controller (no recording)\n");
        std::wprintf(L"  --record 30     Record camera-owner bytes for 30 seconds (range: 5..120)\n");
        return 1;
    }
    const auto game=FindGame();
    if(!game.pid) {
        std::fwprintf(stderr,L"ERROR: retail FH3 process was not found\n");
        return 2;
    }
    if(!FH3Stage5::VerifyRetailFile(game.path)) {
        std::fwprintf(stderr,L"ABORT: unsupported game executable SHA-256\n");
        return 3;
    }
    const auto base=ModuleBase(game.pid);
    if(!base) {
        std::fwprintf(stderr,L"ERROR: could not enumerate FH3 module base\n");
        return 4;
    }
    HANDLE h=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,game.pid);
    if(!h) {
        std::fwprintf(stderr,L"ERROR: read-only OpenProcess failed: %lu\n",GetLastError());
        return 5;
    }
    Reader reader(h);
    CameraInfo camera{},second{};
    if(!Resolve(reader,base,camera)) {
        std::fwprintf(stderr,L"ABORT: expected SplitView/ForzaMultiCam chain is unavailable\n");
        CloseHandle(h);
        return 6;
    }
    Sleep(200);
    if(!Resolve(reader,base,second)||!Same(camera,second)) {
        std::fwprintf(stderr,L"ABORT: camera owner changed during verification\n");
        CloseHandle(h);
        return 6;
    }
    std::wprintf(L"FH3 PID %lu | executable SHA-256 MATCH | image base 0x%llX\n",
                 game.pid,static_cast<unsigned long long>(base));
    std::wprintf(L"CONTROLLER_OK: SplitView=0x%llX ForzaMultiCam=0x%llX interface=0x%llX\n",
                 static_cast<unsigned long long>(camera.view),
                 static_cast<unsigned long long>(camera.owner),
                 static_cast<unsigned long long>(camera.control));
    std::wprintf(L"Registered camera-mode entries: %zu (0, 6, 9, 12 present)\n",
                 camera.modeEntries);
    if(inspect) {
        std::wprintf(L"INSPECT_OK: read-only; no game code invoked or memory modified.\n");
        CloseHandle(h);return 0;
    }

    const std::filesystem::path output=std::filesystem::current_path();
    const std::filesystem::path csv=output/(L"FH3_Camera_Trace_"+Timestamp()+L".csv");
    const std::filesystem::path report=csv.wstring().substr(0,csv.wstring().size()-4)+L".txt";
    std::ofstream data(csv,std::ios::binary);
    if(!data) {
        std::fwprintf(stderr,L"ERROR: unable to create trace file in current directory\n");
        CloseHandle(h);return 7;
    }
    data << "elapsed_ms,owner_offset_000_to_23f_hex\n";
    FH3Trace::CameraDelta changes(kCaptureBytes);
    std::vector<std::uint8_t> capture(kCaptureBytes);
    std::size_t good=0;
    bool stopped=false;
    const auto start=std::chrono::steady_clock::now();
    const auto total=std::chrono::milliseconds(seconds*1000);
    std::wprintf(L"RECORDING %d seconds at 10 samples/sec. Switch normal in-game camera views while recording.\n",
                 seconds);
    std::wprintf(L"All reads target only the verified FH3 camera owner. No injection, writes, or hooks.\n");
    for(std::size_t sample=0;;++sample) {
        const auto target=start+std::chrono::milliseconds(static_cast<int>(sample*kIntervalMs));
        std::this_thread::sleep_until(target);
        const auto now=std::chrono::steady_clock::now();
        if(now-start>=total)break;
        CameraInfo current{};
        if(!Resolve(reader,base,current)||!Same(camera,current)) {
            std::fwprintf(stderr,L"WARNING: camera owner changed; recording stopped before next read.\n");
            stopped=true;break;
        }
        if(!reader.Read(camera.owner,capture.data(),capture.size())) {
            std::fwprintf(stderr,L"WARNING: camera owner memory became unreadable; stopping.\n");
            stopped=true;break;
        }
        const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(now-start).count();
        data << elapsed << "," << HexBytes(capture) << "\n";
        if(!data) {
            std::fwprintf(stderr,L"ERROR: trace CSV write failed; recording stopped.\n");
            CloseHandle(h);return 7;
        }
        if(!changes.Add(capture)) {
            std::fwprintf(stderr,L"ERROR: unexpected local capture size\n");
            stopped=true;break;
        }
        ++good;
    }
    data.close();
    if(!data) {
        std::fwprintf(stderr,L"ERROR: trace CSV could not be finalized.\n");
        CloseHandle(h);return 7;
    }
    std::ofstream summary(report,std::ios::binary);
    if(!summary) {
        std::fwprintf(stderr,L"ERROR: unable to create summary TXT; trace is incomplete.\n");
        CloseHandle(h);return 7;
    }
    {
        summary << "FH3 CAMERA TRACE: READ ONLY\n";
        summary << "Game SHA-256: supported retail v1.0.125.2 verified\n";
        summary << "Image base: 0x" << std::hex << base << "\n";
        summary << "Camera owner: 0x" << std::hex << camera.owner << "\n";
        summary << "Interface: 0x" << std::hex << camera.control << "\n";
        summary << std::dec;
        summary << "Samples: " << good << "\n";
        summary << "Duration requested: " << seconds << " seconds\n";
        summary << "Premature stop: " << (stopped ? "yes" : "no") << "\n";
        summary << "Capture region: camera owner +0x000 .. +0x23f\n";
        summary << "No game memory writes, DLL loading, calls or hooks.\n\n";
        summary << "4-BYTE-ALIGNED FIELDS WITH INFREQUENT TRANSITIONS (<= 30)\n";
        summary << "These are candidates, NOT proven camera-mode fields.\n";
        summary << "offset,transitions,distinct_capped9,initial_u32_hex,last_u32_hex\n";
        for(const auto& candidate:changes.SparseChanges(30)) {
            summary << "0x" << std::hex << std::setw(3) << std::setfill('0') << candidate.offset
                    << std::dec << "," << candidate.transitions << ","
                    << candidate.distinctValues << ",0x" << std::hex
                    << std::setw(8) << candidate.initial << ",0x"
                    << std::setw(8) << candidate.last << std::dec << "\n";
        }
    }
    summary.close();
    if(!summary) {
        std::fwprintf(stderr,L"ERROR: summary TXT could not be finalized.\n");
        CloseHandle(h);return 7;
    }
    CloseHandle(h);
    std::wprintf(L"Trace samples: %zu | stopped early: %ls\n",good,stopped?L"yes":L"no");
    std::wprintf(L"Trace CSV: %ls\n",csv.c_str());
    std::wprintf(L"Summary TXT: %ls\n",report.c_str());
    std::wprintf(L"UPLOAD BOTH FILES for comparison with the original camera modes.\n");
    return stopped ? 8 : 0;
}
#endif
