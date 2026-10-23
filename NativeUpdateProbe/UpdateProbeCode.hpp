#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <array>

namespace FH3UpdateProbe {
// Remote data layout, shared by the emitter, host and synthetic tests.
// The leaf probe uses only the first 56 bytes. Completion fields are used
// only by the separately selected, registered non-leaf observer.
struct alignas(8) ProbeState {
    std::uint64_t entries{};
    std::uint64_t firstThread{};
    std::uint64_t differentThreadEntries{};
    std::uint64_t identityMismatches{};
    std::uint64_t expectedObject{};
    std::uint64_t globalAddress{};
    std::uint64_t expectedTable{};
    std::uint64_t returns{};
    std::uint64_t active{};
    std::uint64_t overlappingEntries{};
};
enum class SwapStatus : std::uint32_t { Pending=0, Swapped=1, GlobalChanged=2, TableChanged=3 };
struct alignas(8) SwapContext {
    std::uint64_t globalAddress{};
    std::uint64_t object{};
    std::uint64_t expectedTable{};
    std::uint64_t replacementTable{};
    std::uint32_t status{};
    std::uint32_t reserved{};
    std::uint64_t observedTable{};
};
static_assert(sizeof(ProbeState)==80 && offsetof(ProbeState,expectedTable)==48);
static_assert(offsetof(ProbeState,returns)==56 && offsetof(ProbeState,active)==64 && offsetof(ProbeState,overlappingEntries)==72);
static_assert(sizeof(SwapContext)==48 && offsetof(SwapContext,observedTable)==40);

// Windows x64 leaf: preserves the incoming stack, RCX/RDX/R8/R9, XMM
// registers and all nonvolatile registers; clobbers only RAX/R10/R11/flags.
// No calls, prologue, epilogue, or camera access. Tail-jumps to original.
std::vector<std::uint8_t> BuildEntryProbe(std::uintptr_t state, std::uintptr_t original);
// Register-argument Windows x64 ABI only: no stack-argument forwarding.
// Requires registered unwind metadata BEFORE exposure to any caller.
// Preserves all incoming argument registers and RAX/XMM0 return values.
std::vector<std::uint8_t> BuildCompletionProbe(std::uintptr_t state, std::uintptr_t original);
// Version 1, four-byte prologue, one UWOP_ALLOC_SMALL for sub rsp,40.
inline constexpr std::array<std::uint8_t,8> CompletionUnwind{1,4,1,0,4,0x42,0,0};
// Leaf bootstrap tail-calls RtlAddFunctionTable(table,1,base). No hook is
// installed until registration succeeds; the host conservatively requires
// the canonical thread exit value 1 rather than accepting arbitrary high bits.
std::vector<std::uint8_t> BuildRegistrationThunk(std::uintptr_t functionTable, std::uintptr_t base, std::uintptr_t addFunctionTable);
// Windows x64 thread entry, RCX=SwapContext*. Validates global ownership
// immediately before atomically comparing/exchanging the object's vptr.
std::vector<std::uint8_t> BuildSwapRoutine();
}
