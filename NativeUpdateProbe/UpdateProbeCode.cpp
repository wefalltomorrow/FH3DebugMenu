#include "UpdateProbeCode.hpp"
#include <initializer_list>
#include <limits>
#include <stdexcept>

namespace FH3UpdateProbe {
namespace {
struct Code {
    std::vector<std::uint8_t> bytes;
    void Emit(std::initializer_list<std::uint8_t> values) { bytes.insert(bytes.end(),values); }
    void U64(std::uint64_t value) { for(unsigned i=0;i<8;++i) bytes.push_back(static_cast<std::uint8_t>(value>>(8*i))); }
    std::size_t Branch(std::uint8_t op) { Emit({op,0}); return bytes.size()-1; }
    void Target(std::size_t displacement,std::size_t target) {
        const auto delta=static_cast<std::ptrdiff_t>(target)-static_cast<std::ptrdiff_t>(displacement+1);
        if(delta<std::numeric_limits<std::int8_t>::min() || delta>std::numeric_limits<std::int8_t>::max())
            throw std::logic_error("probe branch is out of range");
        bytes.at(displacement)=static_cast<std::uint8_t>(delta);
    }
};
}
std::vector<std::uint8_t> BuildEntryProbe(std::uintptr_t state,std::uintptr_t original) {
    if(!state || !original || state%alignof(ProbeState)) return {};
    Code c;
    c.Emit({0x49,0xBA}); c.U64(state);                 // mov r10,state
    c.Emit({0xF0,0x49,0xFF,0x02});                   // lock inc qword [r10]
    c.Emit({0x65,0x4C,0x8B,0x1C,0x25,0x48,0,0,0}); // mov r11,gs:[48h] (TEB thread ID)
    c.Emit({0x31,0xC0});                            // xor eax,eax
    c.Emit({0xF0,0x4D,0x0F,0xB1,0x5A,0x08});       // lock cmpxchg [r10+8],r11
    const auto first=c.Branch(0x74);                // jz identity
    c.Emit({0x4C,0x39,0xD8});                       // cmp rax,r11
    const auto same=c.Branch(0x74);
    c.Emit({0xF0,0x49,0xFF,0x42,0x10});             // lock inc qword [r10+16]
    const auto identity=c.bytes.size();
    c.Emit({0x49,0x3B,0x4A,0x20});                 // cmp rcx,[r10+32]
    const auto wrongObject=c.Branch(0x75);
    c.Emit({0x4D,0x8B,0x5A,0x28});                 // mov r11,[r10+40]
    c.Emit({0x49,0x3B,0x0B});                       // cmp rcx,[r11]
    const auto wrongGlobal=c.Branch(0x75);
    c.Emit({0x4D,0x8B,0x5A,0x30});                 // mov r11,[r10+48]
    c.Emit({0x4C,0x39,0x19});                       // cmp [rcx],r11
    const auto valid=c.Branch(0x74);
    const auto mismatch=c.bytes.size();
    c.Emit({0xF0,0x49,0xFF,0x42,0x18});             // lock inc qword [r10+24]
    const auto forward=c.bytes.size();
    c.Emit({0x48,0xB8}); c.U64(original);            // mov rax,original
    c.Emit({0xFF,0xE0});                            // jmp rax
    c.Target(first,identity); c.Target(same,identity);
    c.Target(wrongObject,mismatch); c.Target(wrongGlobal,mismatch); c.Target(valid,forward);
    return c.bytes;
}
std::vector<std::uint8_t> BuildCompletionProbe(std::uintptr_t state,std::uintptr_t original) {
    auto entry=BuildEntryProbe(state,original);
    if(entry.empty())return {};
    // Remove only the terminal mov rax,original / jmp rax. Internal branches
    // still reach the same entry-accounting endpoint after prefix insertion.
    entry.resize(entry.size()-12);
    Code c;
    c.Emit({0x48,0x83,0xEC,0x28});                 // 32-byte shadow space + alignment
    c.bytes.insert(c.bytes.end(),entry.begin(),entry.end());
    c.Emit({0x41,0xBB,1,0,0,0});                  // mov r11d,1
    c.Emit({0xF0,0x4D,0x0F,0xC1,0x5A,0x40});     // lock xadd [r10+64],r11
    c.Emit({0x4D,0x85,0xDB});                     // test r11,r11 (previous active)
    const auto alone=c.Branch(0x74);
    c.Emit({0xF0,0x49,0xFF,0x42,0x48});           // lock inc [r10+72]
    c.Target(alone,c.bytes.size());
    c.Emit({0x48,0xB8});c.U64(original);
    c.Emit({0xFF,0xD0});                          // call rax
    c.Emit({0x49,0xBA});c.U64(state);             // original may clobber r10
    c.Emit({0xF0,0x49,0xFF,0x42,0x38});           // lock inc returns
    c.Emit({0xF0,0x49,0xFF,0x4A,0x40});           // lock dec active
    c.Emit({0x48,0x83,0xC4,0x28,0xC3});           // canonical epilogue; preserve RAX/XMM0
    return c.bytes;
}
std::vector<std::uint8_t> BuildRegistrationThunk(std::uintptr_t table,std::uintptr_t base,std::uintptr_t api) {
    if(!table || table%4 || !base || !api)return {};
    Code c;
    c.Emit({0x48,0xB9});c.U64(table);
    c.Emit({0xBA,1,0,0,0});
    c.Emit({0x49,0xB8});c.U64(base);
    c.Emit({0x48,0xB8});c.U64(api);
    c.Emit({0xFF,0xE0});
    return c.bytes;
}
std::vector<std::uint8_t> BuildSwapRoutine() {
    Code c;
    c.Emit({0x49,0x89,0xCA});                       // mov r10,rcx
    c.Emit({0x4D,0x8B,0x1A});                       // mov r11,[r10]
    c.Emit({0x4D,0x8B,0x1B});                       // mov r11,[r11]
    c.Emit({0x4D,0x3B,0x5A,0x08});                 // cmp r11,[r10+8]
    const auto globalChanged=c.Branch(0x75);
    c.Emit({0x49,0x8B,0x42,0x10});                 // mov rax,[r10+16]
    c.Emit({0x4D,0x8B,0x5A,0x18});                 // mov r11,[r10+24]
    c.Emit({0x49,0x8B,0x4A,0x08});                 // mov rcx,[r10+8]
    c.Emit({0xF0,0x4C,0x0F,0xB1,0x19});            // lock cmpxchg [rcx],r11
    c.Emit({0x49,0x89,0x42,0x28});                 // mov [r10+40],rax (preserves flags)
    const auto tableChanged=c.Branch(0x75);
    c.Emit({0xB8,1,0,0,0});
    const auto success=c.Branch(0xEB);
    const auto global=c.bytes.size(); c.Emit({0xB8,2,0,0,0});
    const auto globalDone=c.Branch(0xEB);
    const auto table=c.bytes.size(); c.Emit({0xB8,3,0,0,0});
    const auto done=c.bytes.size();
    c.Emit({0x41,0x89,0x42,0x20,0xC3});             // mov [r10+32],eax; ret
    c.Target(globalChanged,global); c.Target(tableChanged,table);
    c.Target(success,done); c.Target(globalDone,done);
    return c.bytes;
}
}
