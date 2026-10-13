#pragma once
// FH3 v1.0.125.2: read-only, version-specific SplitView -> camera-controller locator.
// No game code calls, memory writes, injection or detours.
#include "CameraObjectLocator.hpp"
#include <cstddef>
#include <cstdint>

namespace FH3Native {
inline constexpr std::uintptr_t kRetailSplitViewVtableRva = 0x5119898;
inline constexpr std::uintptr_t kRetailSplitViewGetterRva = 0x42A8A0;
inline constexpr std::uintptr_t kRetailImageSize = 121211392;
inline constexpr std::uintptr_t kSplitViewOwnerPointerOffset = 0x100;
inline constexpr std::uintptr_t kOwnerCameraControlOffset = 0x1A0;
inline constexpr std::uintptr_t kCameraSetSlot = 0x20;
inline constexpr std::uintptr_t kCameraGetSlot = 0x50;

enum class CameraOwnerStatus {
    Success,
    InvalidSplitView,
    UnexpectedSplitViewVtable,
    UnexpectedGetter,
    UnreadableMemory,
    OwnerNotReady,
    InterfaceNotReady,
    ControllerMethodOutOfImage
};

struct CameraOwnerResolution {
    CameraOwnerStatus status = CameraOwnerStatus::UnreadableMemory;
    std::uintptr_t splitView = 0;
    std::uintptr_t owner = 0;
    std::uintptr_t ownerVtable = 0;
    std::uintptr_t cameraInterface = 0;
    std::uintptr_t cameraVtable = 0;
    std::uintptr_t setModeFunction = 0;
    std::uintptr_t getModeFunction = 0;
};

// Requires the complete SHA-256 of the expected retail executable to have
// been checked by the caller. This routine alone does NOT prove camera actions
// or camera mode IDs work in retail.
CameraOwnerResolution ResolveRetailCameraOwner(const MemoryReader& memory,
                                                std::uintptr_t imageBase,
                                                std::uintptr_t splitView) noexcept;
}
