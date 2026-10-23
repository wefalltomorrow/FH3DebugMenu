#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace FH3UpdateProbe {
// Remote data layout, shared by the emitter, host and synthetic tests.
// Counts describe callback ENTRY, never original-function completion.
struct alignas(8) ProbeState {
    std::uint64_t entries{};
    std::uint64_t firstThread{};
    std::uint64_t differentThreadEntries{};
    std::uint64_t identityMismatches{};
    std::uint64_t expectedObject{};
    std::uint64_t globalAddress{};
    std::uint64_t expectedTable{};
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
static_assert(sizeof(ProbeState)==56 && offsetof(ProbeState,expectedTable)==48);
static_assert(sizeof(SwapContext)==48 && offsetof(SwapContext,observedTable)==40);

// Windows x64 leaf: preserves the incoming stack, RCX/RDX/R8/R9, XMM
// registers and all nonvolatile registers; clobbers only RAX/R10/R11/flags.
// No calls, prologue, epilogue, or camera access. Tail-jumps to original.
std::vector<std::uint8_t> BuildEntryProbe(std::uintptr_t state, std::uintptr_t original);
// Windows x64 thread entry, RCX=SwapContext*. Validates global ownership
// immediately before atomically comparing/exchanging the object's vptr.
std::vector<std::uint8_t> BuildSwapRoutine();
}
