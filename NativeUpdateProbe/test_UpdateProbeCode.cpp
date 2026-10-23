#include "UpdateProbeCode.hpp"
#include <cstdlib>
#include <iostream>
#include <cstring>
#include <thread>
#include <array>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
using namespace FH3UpdateProbe;
static void Check(bool ok,const char* message) {
    if(!ok) { std::cerr << message << '\n'; std::exit(1); }
}
#ifdef _WIN32
struct Executable {
    void* address{};
    explicit Executable(const std::vector<std::uint8_t>& code) {
        address=VirtualAlloc(nullptr,code.size(),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        Check(address!=nullptr,"allocation");
        std::memcpy(address,code.data(),code.size());
        DWORD old{};
        Check(VirtualProtect(address,code.size(),PAGE_EXECUTE_READ,&old)!=FALSE,"RX protection");
        Check(FlushInstructionCache(GetCurrentProcess(),address,code.size())!=FALSE,"instruction cache");
    }
    ~Executable() { VirtualFree(address,0,MEM_RELEASE); }
};
struct Object { std::uintptr_t table; };
using FloatCall=std::uint64_t(*)(Object*,double,std::uint64_t,std::uint64_t,std::uint64_t);
static std::uint64_t FloatOriginal(Object* object,double delta,std::uint64_t a,std::uint64_t b,std::uint64_t stack) {
    Check(object!=nullptr && delta==0.125 && a==0x123456789abcdef0ULL && b==77 && stack==99,"float/register/stack arguments changed");
    return 0xfedcba9876543210ULL;
}
static std::uint64_t IntOriginal(Object* object,std::uint64_t b,std::uint64_t c,std::uint64_t d) {
    Check(object!=nullptr && b==11 && c==22 && d==33,"integer arguments changed");
    return 42;
}
static void WindowsTests() {
    Object object{0x1000};
    auto global=reinterpret_cast<std::uintptr_t>(&object);
    ProbeState state{};
    state.expectedObject=global; state.globalAddress=reinterpret_cast<std::uintptr_t>(&global); state.expectedTable=object.table;
    Executable code(BuildEntryProbe(reinterpret_cast<std::uintptr_t>(&state),reinterpret_cast<std::uintptr_t>(&FloatOriginal)));
    const auto call=reinterpret_cast<FloatCall>(code.address);
    Check(call(&object,0.125,0x123456789abcdef0ULL,77,99)==0xfedcba9876543210ULL,"return changed");
    Check(state.entries==1 && state.firstThread==GetCurrentThreadId() && state.identityMismatches==0,"first entry");
    global=0; call(&object,0.125,0x123456789abcdef0ULL,77,99); global=state.expectedObject;
    object.table=0x2000; call(&object,0.125,0x123456789abcdef0ULL,77,99); object.table=state.expectedTable;
    Object other{object.table}; call(&other,0.125,0x123456789abcdef0ULL,77,99);
    Check(state.identityMismatches==3 && state.entries==4,"identity failures must still forward");
    std::array<std::thread,4> workers;
    for(auto& worker:workers) worker=std::thread([&]{for(int i=0;i<1000;++i) call(&object,0.125,0x123456789abcdef0ULL,77,99);});
    for(auto& worker:workers) worker.join();
    Check(state.entries==4004 && state.differentThreadEntries==4000 && state.identityMismatches==3,"concurrent entry accounting");
    Executable ints(BuildEntryProbe(reinterpret_cast<std::uintptr_t>(&state),reinterpret_cast<std::uintptr_t>(&IntOriginal)));
    using IntCall=std::uint64_t(*)(Object*,std::uint64_t,std::uint64_t,std::uint64_t);
    Check(reinterpret_cast<IntCall>(ints.address)(&object,11,22,33)==42,"integer forwarding");
    Executable swap(BuildSwapRoutine());
    auto exchange=reinterpret_cast<DWORD(WINAPI*)(void*)>(swap.address);
    SwapContext context{reinterpret_cast<std::uintptr_t>(&global),global,object.table,0x3000};
    Check(exchange(&context)==1 && context.status==1 && context.observedTable==0x1000 && object.table==0x3000,"install CAS");
    context.status=0;
    Check(exchange(&context)==3 && context.status==3 && context.observedTable==0x3000 && object.table==0x3000,"foreign table not overwritten");
    context.expectedTable=0x3000; context.replacementTable=0x1000;
    Check(exchange(&context)==1 && object.table==0x1000,"restore CAS");
    global=0; context.object=1;
    Check(exchange(&context)==2 && context.status==2 && object.table==0x1000,"changed global must not dereference old object");
}
#endif
int main() {
    Check(BuildEntryProbe(0,1).empty() && BuildEntryProbe(8,0).empty() && BuildEntryProbe(3,1).empty(),"invalid emitter inputs");
    Check(!BuildEntryProbe(8,16).empty() && !BuildSwapRoutine().empty(),"emission");
#ifdef _WIN32
    WindowsTests();
    std::cout << "Windows x64 emitted-code execution, ABI forwarding, concurrent counts and ownership CAS passed\n";
#else
    std::cout << "Emission checks only; Windows execution tests unavailable\n";
#endif
}
