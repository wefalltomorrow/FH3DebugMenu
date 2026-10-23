#pragma once
#include "CameraObjectLocator.hpp"
#include <array>
#include <cstdint>

namespace FH3Trace {
enum class ModeReadStatus { Consistent, ReadFailed, InvalidLayout, Empty,
    InvalidMode, HandlerMismatch, Changed };
const char* ModeReadStatusName(ModeReadStatus status) noexcept;
struct ModeObservation {
    ModeReadStatus status=ModeReadStatus::ReadFailed;
    std::uint32_t mode=0;
    std::uintptr_t owner=0, proxy=0, map=0, mapSize=0, start=0, count=0;
    std::uintptr_t modeAddress=0, handler=0, reference=0;
    std::uintptr_t tableBegin=0, tableEnd=0, tableHandler=0, tableReference=0;
};
// Caller must validate the exact retail build and owner identity before/after.
// Two equal reads detect some transitions; they are neither atomic nor a lifetime
// pin. Never use this external observation to authorize an in-game setter call.
ModeObservation ObserveCameraMode(const FH3Native::MemoryReader& reader,
                                  std::uintptr_t owner) noexcept;
} // namespace FH3Trace
