#include "UpdateProbeCode.hpp"
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
using namespace FH3UpdateProbe;
static void Check(bool ok,const char* message) {
    if(!ok){std::cerr<<message<<'\n';std::exit(1);}
}
struct Object { std::uintptr_t table=0x1000; };
struct Fixture {
    Object object;
    std::uintptr_t global=reinterpret_cast<std::uintptr_t>(&object);
    ProbeState state{};
    Fixture() {
        state.expectedObject=global;state.globalAddress=reinterpret_cast<std::uintptr_t>(&global);state.expectedTable=object.table;
    }
};
struct RegisteredCode {
    void* address{};RUNTIME_FUNCTION* table{};std::size_t codeSize{};
    RegisteredCode(ProbeState& state,std::uintptr_t original) {
        address=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
        Check(address!=nullptr,"allocate completion code");
        const auto base=reinterpret_cast<std::uintptr_t>(address);
        const auto code=BuildCompletionProbe(reinterpret_cast<std::uintptr_t>(&state),original);
        codeSize=code.size();Check(codeSize>4 && codeSize<256,"completion code bounds");
        std::memcpy(address,code.data(),codeSize);
        table=reinterpret_cast<RUNTIME_FUNCTION*>(base+512);
        *table={0,static_cast<DWORD>(codeSize),528};
        std::memcpy(reinterpret_cast<void*>(base+528),CompletionUnwind.data(),CompletionUnwind.size());
        const auto registration=BuildRegistrationThunk(base+512,base,reinterpret_cast<std::uintptr_t>(&RtlAddFunctionTable));
        Check(!registration.empty() && registration.size()<256,"registration bounds");
        std::memcpy(reinterpret_cast<void*>(base+256),registration.data(),registration.size());
        DWORD old{};Check(VirtualProtect(address,4096,PAGE_EXECUTE_READ,&old)!=FALSE,"completion RX");
        Check(FlushInstructionCache(GetCurrentProcess(),address,4096)!=FALSE,"completion instruction cache");
        // Use a thread, the exact production bootstrap and the host exit gate.
        HANDLE worker=CreateThread(nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(base+256),nullptr,0,nullptr);
        Check(worker!=nullptr && WaitForSingleObject(worker,5000)==WAIT_OBJECT_0,"registration thread");
        DWORD result{};Check(GetExitCodeThread(worker,&result)!=FALSE && result==1,"canonical registration result");
        CloseHandle(worker);
        DWORD64 imageBase{};
        Check(RtlLookupFunctionEntry(base+4,&imageBase,nullptr)==table && imageBase==base,"registered unwind lookup");
    }
    ~RegisteredCode(){Check(RtlDeleteFunctionTable(table)!=FALSE,"remove synthetic table");VirtualFree(address,0,MEM_RELEASE);}
};
using FloatCall=std::uint64_t(*)(Object*,double,std::uint64_t,std::uint64_t);
static std::uint64_t FloatOriginal(Object* object,double delta,std::uint64_t a,std::uint64_t b) {
    Check(object && delta==0.125 && a==0x123456789abcdef0ULL && b==77,"register arguments changed");
    return 0xfedcba9876543210ULL;
}
static double DoubleOriginal(Object*,double value,double third,double fourth) {
    Check(value==0.125 && third==2.0 && fourth==3.0,"XMM arguments changed");return 9.875;
}
static std::uint64_t IntOriginal(Object*,std::uint64_t b,std::uint64_t c,std::uint64_t d) {
    Check(b==11 && c==22 && d==33,"integer arguments changed");return 42;
}
static void Forwarding() {
    Fixture f;RegisteredCode code(f.state,reinterpret_cast<std::uintptr_t>(&FloatOriginal));
    const auto call=reinterpret_cast<FloatCall>(code.address);
    Check(call(&f.object,0.125,0x123456789abcdef0ULL,77)==0xfedcba9876543210ULL,"RAX return changed");
    Check(f.state.entries==1 && f.state.returns==1 && !f.state.active && !f.state.overlappingEntries &&
        f.state.firstThread==GetCurrentThreadId(),"completion counters");
    f.global=0;call(&f.object,0.125,0x123456789abcdef0ULL,77);f.global=f.state.expectedObject;
    f.object.table=0x2000;call(&f.object,0.125,0x123456789abcdef0ULL,77);f.object.table=f.state.expectedTable;
    Object other;call(&other,0.125,0x123456789abcdef0ULL,77);
    Check(f.state.identityMismatches==3 && f.state.entries==4 && f.state.returns==4 && !f.state.active,"mismatch forwarding");
    RegisteredCode floating(f.state,reinterpret_cast<std::uintptr_t>(&DoubleOriginal));
    Check(reinterpret_cast<double(*)(Object*,double,double,double)>(floating.address)(&f.object,0.125,2.0,3.0)==9.875,"XMM0 return changed");
    RegisteredCode integers(f.state,reinterpret_cast<std::uintptr_t>(&IntOriginal));
    Check(reinterpret_cast<std::uint64_t(*)(Object*,std::uint64_t,std::uint64_t,std::uint64_t)>(integers.address)(&f.object,11,22,33)==42,"integer forwarding");
}
using SimpleCall=std::uint64_t(*)(Object*,double);
static SimpleCall recursiveCall{};
static std::uint64_t RecursiveOriginal(Object* object,double depth) {
    return depth>0?1+recursiveCall(object,depth-1):1;
}
static void Reentrancy() {
    Fixture f;RegisteredCode code(f.state,reinterpret_cast<std::uintptr_t>(&RecursiveOriginal));
    recursiveCall=reinterpret_cast<SimpleCall>(code.address);
    Check(recursiveCall(&f.object,3)==4,"recursive return");
    Check(f.state.entries==4 && f.state.returns==4 && !f.state.active && f.state.overlappingEntries==3 &&
        !f.state.differentThreadEntries,"nested calls counted");
    recursiveCall=nullptr;
}
static std::atomic<unsigned> arrived{};
static std::uint64_t BlockingOriginal(Object*,double) {
    arrived.fetch_add(1);
    const auto start=GetTickCount64();
    while(arrived.load()<4) {Check(GetTickCount64()-start<5000,"concurrent barrier timeout");std::this_thread::yield();}
    return 7;
}
static void Concurrency() {
    Fixture f;RegisteredCode code(f.state,reinterpret_cast<std::uintptr_t>(&BlockingOriginal));
    const auto call=reinterpret_cast<SimpleCall>(code.address);
    std::array<std::thread,4> workers;
    for(auto& worker:workers)worker=std::thread([&]{Check(call(&f.object,0)==7,"concurrent return");});
    for(auto& worker:workers)worker.join();
    Check(f.state.entries==4 && f.state.returns==4 && !f.state.active && f.state.overlappingEntries==3 &&
        f.state.differentThreadEntries==3,"concurrent accounting");
}
struct TestException { int value; };
static std::uint64_t ThrowingOriginal(Object*,double) {throw TestException{73};}
static void Unwinding() {
    Fixture f;RegisteredCode code(f.state,reinterpret_cast<std::uintptr_t>(&ThrowingOriginal));
    bool caught=false;
    try{reinterpret_cast<SimpleCall>(code.address)(&f.object,0);}catch(const TestException& error){caught=error.value==73;}
    Check(caught,"exception failed to unwind through generated frame");
    Check(f.state.entries==1 && f.state.returns==0 && f.state.active==1,"exception must not claim normal completion");
    // Verify both sides of the prologue and every epilogue instruction.
    const auto base=reinterpret_cast<DWORD64>(code.address);
    for(const auto offset:std::vector<std::size_t>{0,4,code.codeSize-5,code.codeSize-1}) {
        alignas(16) std::array<DWORD64,16> stack{};
        const auto entrySp=reinterpret_cast<DWORD64>(stack.data()+9);
        stack[9]=0x123456789abcULL;
        CONTEXT context{};context.Rip=base+offset;
        context.Rsp=(offset==0 || offset==code.codeSize-1)?entrySp:entrySp-40;
        PVOID handlerData{};DWORD64 frame{};
        RtlVirtualUnwind(UNW_FLAG_NHANDLER,base,context.Rip,code.table,&context,&handlerData,&frame,nullptr);
        Check(context.Rip==stack[9] && context.Rsp==entrySp+8,"virtual unwind stack/return address");
    }
}
static void ShadowSpace() {
    // Original stub checks entry alignment, writes all four home slots, and
    // returns 1. This catches a missing shadow area or corrupted wrapper return.
    const std::vector<std::uint8_t> original{
        0x48,0x89,0xE0,0x83,0xE0,0x0F,0x83,0xF8,0x08,0x75,0x1A,
        0x48,0x89,0x4C,0x24,0x08,0x48,0x89,0x54,0x24,0x10,
        0x4C,0x89,0x44,0x24,0x18,0x4C,0x89,0x4C,0x24,0x20,
        0xB8,1,0,0,0,0xC3,0x31,0xC0,0xC3};
    void* memory=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    Check(memory!=nullptr,"shadow original allocation");std::memcpy(memory,original.data(),original.size());
    DWORD old{};Check(VirtualProtect(memory,4096,PAGE_EXECUTE_READ,&old)!=FALSE,"shadow original RX");
    Check(FlushInstructionCache(GetCurrentProcess(),memory,4096)!=FALSE,"shadow original cache");
    { Fixture f;RegisteredCode code(f.state,reinterpret_cast<std::uintptr_t>(memory));
      Check(reinterpret_cast<SimpleCall>(code.address)(&f.object,0)==1,"stack alignment/shadow space"); }
    VirtualFree(memory,0,MEM_RELEASE);
}
int main() {
    Check(BuildCompletionProbe(0,1).empty() && BuildCompletionProbe(3,1).empty() &&
        BuildCompletionProbe(8,0).empty() && BuildRegistrationThunk(3,1,1).empty(),"invalid emitter arguments");
    Forwarding();Reentrancy();Concurrency();Unwinding();ShadowSpace();
    std::cout<<"Completion ABI, shadow space, nested/concurrent counts, registration and exception unwind passed\n";
}
