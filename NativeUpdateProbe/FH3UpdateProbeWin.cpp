#include "UpdateProbeCode.hpp"
#include "RetailBuildIdentityWin.hpp"
#include <windows.h>
#include <tlhelp32.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <fstream>
#include <string>

// Experimental observation of the already mapped CForza virtual update entry.
// No game-function calls from the installer thread, no camera access.
// Snapshot checks and CAS do NOT pin engine object lifetime. Offline free roam
// only; game transitions/teardown remain an unverified race.
namespace {
using namespace FH3UpdateProbe;
constexpr std::uintptr_t GlobalRva=0x5CB5BF0, TableRva=0x5423EC0, ColRva=0x44268E0, UpdateRva=0x1B3D830;
constexpr DWORD ImageSize=121211392;
constexpr std::size_t Slots=31, UpdateSlot=18, Page=4096;
constexpr std::array<unsigned char,28> Prologue{
    0x48,0x8B,0xC4,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
    0x48,0x8D,0xA8,0x08,0xF9,0xFF,0xFF,0x48,0x81,0xEC,0xC0,0x07,0,0};
struct Handle {
    HANDLE value{};
    explicit Handle(HANDLE v=nullptr):value(v) {}
    ~Handle(){if(value && value!=INVALID_HANDLE_VALUE) CloseHandle(value);}
    Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
    explicit operator bool() const {return value && value!=INVALID_HANDLE_VALUE;}
};
struct RemoteBlock {
    HANDLE process; void* address; bool exposed{};
    explicit RemoteBlock(HANDLE h):process(h),address(VirtualAllocEx(h,nullptr,4*Page,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE)) {}
    ~RemoteBlock(){if(address && !exposed) VirtualFreeEx(process,address,0,MEM_RELEASE);}
};
std::atomic<bool> stop{};
BOOL WINAPI ConsoleControl(DWORD event) {
    if(event==CTRL_C_EVENT || event==CTRL_BREAK_EVENT) {stop.store(true);return TRUE;}
    return FALSE; // Closing the console cannot promise cleanup; retained code remains valid.
}
bool Read(HANDLE h,std::uintptr_t address,void* out,std::size_t size) {
    SIZE_T actual{};
    return address && ReadProcessMemory(h,reinterpret_cast<void*>(address),out,size,&actual) && actual==size;
}
template<class T> bool Read(HANDLE h,std::uintptr_t address,T& out){return Read(h,address,&out,sizeof(out));}
bool Write(HANDLE h,std::uintptr_t address,const void* data,std::size_t size) {
    SIZE_T actual{};
    return WriteProcessMemory(h,reinterpret_cast<void*>(address),data,size,&actual) && actual==size;
}
bool Protect(HANDLE h,std::uintptr_t address,DWORD protection) {
    DWORD old{};return VirtualProtectEx(h,reinterpret_cast<void*>(address),Page,protection,&old)!=FALSE;
}
DWORD FindGame() {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));
    if(!snapshot)return 0;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry); DWORD found{};
    if(Process32FirstW(snapshot.value,&entry)) do {
        if(_wcsicmp(entry.szExeFile,FH3Stage5::kGameExe))continue;
        if(found) {std::puts("ABORT: multiple FH3 processes; selection is ambiguous");return 0;}
        found=entry.th32ProcessID;
    }while(Process32NextW(snapshot.value,&entry));
    return found;
}
std::uintptr_t Module(DWORD pid) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid));
    if(!snapshot)return 0;
    MODULEENTRY32W entry{};entry.dwSize=sizeof(entry);
    if(Module32FirstW(snapshot.value,&entry)) do {
        if(!_wcsicmp(entry.szModule,FH3Stage5::kGameExe) && entry.modBaseSize==ImageSize)
            return reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
    }while(Module32NextW(snapshot.value,&entry));
    return 0;
}
bool ExecutableImage(HANDLE h,std::uintptr_t address,std::uintptr_t base) {
    if(address<base || address-base>=ImageSize)return false;
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQueryEx(h,reinterpret_cast<void*>(address),&info,sizeof(info)) ||
       info.State!=MEM_COMMIT || info.Type!=MEM_IMAGE ||
       reinterpret_cast<std::uintptr_t>(info.AllocationBase)!=base || (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const auto p=info.Protect&0xff;
    return p==PAGE_EXECUTE || p==PAGE_EXECUTE_READ || p==PAGE_EXECUTE_READWRITE || p==PAGE_EXECUTE_WRITECOPY;
}
struct Snapshot { std::uintptr_t object{}; std::array<std::uintptr_t,Slots+1> table{}; };
bool Inspect(HANDLE h,std::uintptr_t base,Snapshot& snapshot) {
    std::uintptr_t table{};
    if(!Read(h,base+GlobalRva,snapshot.object) || !snapshot.object || snapshot.object%8 ||
       !Read(h,snapshot.object,table) || table!=base+TableRva ||
       !Read(h,table-8,snapshot.table) || snapshot.table[0]!=base+ColRva ||
       snapshot.table[UpdateSlot+1]!=base+UpdateRva)return false;
    for(std::size_t i=1;i<snapshot.table.size();++i)
        if(!ExecutableImage(h,snapshot.table[i],base))return false;
    std::array<unsigned char,Prologue.size()> bytes{};
    return Read(h,base+UpdateRva,bytes) && bytes==Prologue;
}
bool Owned(HANDLE h,std::uintptr_t global,std::uintptr_t object,std::uintptr_t table) {
    std::uintptr_t current{},vptr{};
    return Read(h,global,current) && current==object && Read(h,object,vptr) && vptr==table;
}
// A timeout is indeterminate. Never retry it, terminate the thread, free its
// arguments, or report success. Separate contexts prevent install/restore races.
bool Exchange(HANDLE h,std::uintptr_t code,std::uintptr_t data,SwapContext& context,RemoteBlock& block) {
    if(!Write(h,data,&context,sizeof(context)))return false;
    block.exposed=true;
    Handle thread(CreateRemoteThread(h,nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(code),reinterpret_cast<void*>(data),0,nullptr));
    if(!thread){std::printf("REMOTE_THREAD_FAILED: %lu\n",GetLastError());return false;}
    if(WaitForSingleObject(thread.value,5000)!=WAIT_OBJECT_0) {
        std::puts("REMOTE_THREAD_INDETERMINATE: memory retained; restart FH3 before another experiment");return false;
    }
    DWORD exit{};
    if(!GetExitCodeThread(thread.value,&exit) || !Read(h,data,context) || exit!=context.status ||
       context.status<1 || context.status>3) {std::puts("REMOTE_THREAD_RESULT_INVALID");return false;}
    return true;
}
int Probe(HANDLE h,std::uintptr_t base,const Snapshot& snapshot,unsigned seconds,std::ofstream& report) {
    RemoteBlock block(h);
    if(!block.address){std::puts("ABORT: allocation failed");return 4;}
    const auto allocation=reinterpret_cast<std::uintptr_t>(block.address);
    const auto code=allocation, swapCode=allocation+256, table=allocation+Page+8;
    const auto data=allocation+2*Page, installData=allocation+3*Page, restoreData=installData+256;
    ProbeState state{};state.expectedObject=snapshot.object;state.globalAddress=base+GlobalRva;state.expectedTable=table;
    auto clone=snapshot.table;clone[UpdateSlot+1]=code;
    const auto entry=BuildEntryProbe(data,base+UpdateRva), swap=BuildSwapRoutine();
    if(entry.empty() || entry.size()>256 || swap.size()>Page-256 ||
       !Write(h,code,entry.data(),entry.size()) || !Write(h,swapCode,swap.data(),swap.size()) ||
       !Write(h,table-8,clone.data(),sizeof(clone)) || !Write(h,data,&state,sizeof(state)) ||
       !Protect(h,code,PAGE_EXECUTE_READ) || !Protect(h,table-8,PAGE_READONLY) ||
       !FlushInstructionCache(h,reinterpret_cast<void*>(code),Page)) {std::puts("ABORT: preparing probe failed");return 4;}
    Snapshot final{};
    if(!Inspect(h,base,final) || final.object!=snapshot.object || final.table!=snapshot.table) {
        std::puts("ABORT: identity changed before installation");return 4;
    }
    report << "Allocation=0x" << std::hex << allocation << " Clone=0x" << table << std::dec << '\n' << std::flush;
    if(!report){std::puts("ABORT: report write failed before installation");return 7;}
    SwapContext install{base+GlobalRva,snapshot.object,base+TableRva,table};
    if(!Exchange(h,swapCode,installData,install,block)) {
        report << "INSTALL_INDETERMINATE; restart FH3. Remote allocation retained.\n";return 5;
    }
    report << "InstallStatus=" << install.status << '\n' << std::flush;
    if(install.status!=static_cast<unsigned>(SwapStatus::Swapped)) {
        std::puts("INSTALL_REJECTED: global or vtable changed; no replacement performed");return 4;
    }
    std::printf("PROBE_INSTALLED: observing up to %u seconds; Ctrl+C requests restoration\n",seconds);
    bool observed=false,interrupted=false;
    const auto start=GetTickCount64();
    while(GetTickCount64()-start<seconds*1000ULL && !stop.load()) {
        const auto wait=WaitForSingleObject(h,100);
        if(wait!=WAIT_TIMEOUT || !Owned(h,base+GlobalRva,snapshot.object,table) || !Read(h,data,state)) {interrupted=true;break;}
        observed=true;
        if(state.identityMismatches || state.differentThreadEntries || !report) {interrupted=true;break;}
    }
    // Restore only our exact table on the still-current instance. CAS rejects
    // a competing replacement; no original bytes or shared vtables are patched.
    bool restored=false;
    if(WaitForSingleObject(h,0)==WAIT_TIMEOUT && Owned(h,base+GlobalRva,snapshot.object,table)) {
        SwapContext restore{base+GlobalRva,snapshot.object,table,base+TableRva};
        restored=Exchange(h,swapCode,restoreData,restore,block) && restore.status==static_cast<unsigned>(SwapStatus::Swapped) &&
            Owned(h,base+GlobalRva,snapshot.object,base+TableRva);
        report << "RestoreStatus=" << restore.status << '\n';
    }
    ProbeState finalState{};
    if(Read(h,data,finalState)) {state=finalState;observed=true;}
    report << "Entries=" << state.entries << " FirstThreadId=" << state.firstThread
           << " OtherThreadEntries=" << state.differentThreadEntries << " IdentityMismatches=" << state.identityMismatches
           << " Interrupted=" << interrupted << " CancelRequested=" << stop.load() << " Restored=" << restored << '\n'
           << "16 KiB retained until FH3 exits: stale dispatch may still reference cloned table/code.\n"
           << "Counts prove entry only. They do not prove original completion, a safe camera command point, or FreeCam.\n" << std::flush;
    std::printf("ENTRIES=%llu FIRST_THREAD=%llu OTHER_THREAD_ENTRIES=%llu IDENTITY_MISMATCHES=%llu\n",
        state.entries,state.firstThread,state.differentThreadEntries,state.identityMismatches);
    std::puts(restored?"RESTORE_OK: original instance table observed":"RESTORE_NOT_CONFIRMED: restart FH3 before further experiments");
    std::puts("PROBE_MEMORY_RETAINED: 16 KiB until game exit; do not free remotely");
    if(!restored)return 5;
    if(!report)return 7;
    if(interrupted || !observed || !state.entries || !state.firstThread || state.differentThreadEntries || state.identityMismatches)return 6;
    std::puts("UPDATE_ENTRY_OBSERVED: original completion and camera safety NOT established");
    return 0;
}
}
int wmain(int argc,wchar_t** argv) {
    static_assert(sizeof(void*)==8,"Windows x64 required");
    const bool inspect=argc==2 && !wcscmp(argv[1],L"--inspect");
    const bool probe=(argc==2 || argc==3) && !wcscmp(argv[1],L"--probe-update");
    unsigned seconds=10;
    if(probe && argc==3) {
        wchar_t* end{};const auto value=wcstoul(argv[2],&end,10);
        if(end==argv[2] || *end || value<1 || value>30){std::puts("Duration must be 1..30 seconds");return 2;}
        seconds=static_cast<unsigned>(value);
    }
    if(!inspect && !probe){std::puts("FH3UpdateProbe --inspect | --probe-update [1..30 seconds]\nExperimental offline callback test; no camera commands.");return 2;}
    const DWORD pid=FindGame();
    if(!pid){std::puts("ERROR: one running FH3 retail process is required");return 3;}
    const DWORD rights=PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE|
        (probe?(PROCESS_VM_WRITE|PROCESS_VM_OPERATION|PROCESS_CREATE_THREAD):0);
    Handle process(OpenProcess(rights,FALSE,pid));
    if(!process){std::printf("ERROR: OpenProcess %lu\n",GetLastError());return 3;}
    std::array<wchar_t,32768> path{};DWORD length=static_cast<DWORD>(path.size());
    if(!QueryFullProcessImageNameW(process.value,0,path.data(),&length) || !FH3Stage5::VerifyRetailFile(path.data())) {
        std::puts("ABORT: supported retail executable SHA-256 did not match");return 3;
    }
    const auto base=Module(pid);Snapshot first{},second{};
    if(!base || !Inspect(process.value,base,first)) {std::puts("ABORT: retail CForza/RTTI/vtable/update signature mismatch");return 3;}
    if(WaitForSingleObject(process.value,100)!=WAIT_TIMEOUT || !Inspect(process.value,base,second) ||
       first.object!=second.object || first.table!=second.table) {std::puts("ABORT: unstable CForza identity");return 3;}
    std::printf("SHA256_MATCH PID=%lu BASE=0x%llX CFORZA=0x%llX UPDATE=0x%llX SLOT=18\n",pid,base,second.object,base+UpdateRva);
    if(inspect){std::puts("INSPECT_OK: read only; update dispatch not tested");return 0;}
    PROCESS_MITIGATION_DYNAMIC_CODE_POLICY dynamicPolicy{};
    if(!GetProcessMitigationPolicy(process.value,ProcessDynamicCodePolicy,&dynamicPolicy,sizeof(dynamicPolicy)) || dynamicPolicy.ProhibitDynamicCode) {
        std::puts("ABORT: dynamic-code policy unavailable or prohibits probe");return 3;
    }
    wchar_t mutexName[96]{};swprintf_s(mutexName,L"Local\\FH3UpdateProbe-%lu",pid);
    Handle mutex(CreateMutexW(nullptr,FALSE,mutexName));
    if(!mutex || GetLastError()==ERROR_ALREADY_EXISTS){std::puts("ABORT: another update probe is active");return 3;}
    SYSTEMTIME now{};GetLocalTime(&now);wchar_t filename[160]{};
    swprintf_s(filename,L"FH3_Update_Probe_%04u%02u%02u_%02u%02u%02u_%03u_%lu.txt",
        now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,GetCurrentProcessId());
    std::ofstream report(filename,std::ios::binary);
    report << "FH3 experimental update-entry probe, Stage 8\nSupported retail SHA-256 verified\nPID=" << pid
           << " Base=0x" << std::hex << base << " CForza=0x" << second.object << std::dec << '\n' << std::flush;
    if(!report){std::puts("ABORT: cannot create report");return 7;}
    std::wprintf(L"Report: %ls\n",filename);
    if(!SetConsoleCtrlHandler(ConsoleControl,TRUE)){std::puts("ABORT: cannot install cancellation handler");return 3;}
    const int result=Probe(process.value,base,second,seconds,report);
    SetConsoleCtrlHandler(ConsoleControl,FALSE);
    report << "ExitCode=" << result << '\n';report.close();
    return report?result:7;
}
