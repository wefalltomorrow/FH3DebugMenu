#pragma once
// FH3 retail v1.0.125.2: read-only capability check for four developer camera modes.
// No game function calls, writes, hooks, or renderer dependencies.
#include "CameraOwnerResolver.hpp"
#include <array>
#include <cstdint>
#include <cstddef>
namespace FH3Native {
inline constexpr std::uintptr_t kCameraModeTableStartOffset = 0x1A8;
inline constexpr std::uintptr_t kCameraModeTableEndOffset = 0x1B0;
inline constexpr std::uintptr_t kCameraModeEntryStride = 0x40;
inline constexpr std::uintptr_t kCameraModeEntryHandlerOffset = 0x30;
inline constexpr std::size_t kMaxCameraModes = 1024;
enum class ModeCatalogStatus {
    Success, InvalidOwner, ReadFailed, InvalidArrayBounds, TooManyModes
};
enum class ModeSupport { NotInTable, EmptySlot, Registered, Unreadable };
struct ModeEntry {
    const char* name;
    std::uint32_t mode;
    ModeSupport support=ModeSupport::Unreadable;
    std::uintptr_t handler=0;
};
struct ModeCatalog {
    ModeCatalogStatus status=ModeCatalogStatus::ReadFailed;
    std::uintptr_t arrayBegin=0;
    std::uintptr_t arrayEnd=0;
    std::size_t tableSize=0;
    std::array<ModeEntry,4> commands{{
        {"ForceFollowCam",0},{"ForceFreeCam",6},
        {"ForceWheelCam",9},{"ForceTrackSegCams",12}
    }};
};
// The owning ForzaMultiCam must already be independently validated with
// ResolveRetailCameraOwner. The table can change while sampled; capability
// presence does NOT mean the command is safe to call from an arbitrary thread.
ModeCatalog InspectCameraModeCatalog(const MemoryReader& memory,
                                     std::uintptr_t owner) noexcept;
}
