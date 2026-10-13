#pragma once

// Forza Horizon 3 native camera-interface bridge, stage 1.
// Developer release/profile verified layout. Retail validation is static so far.
// This library deliberately does not inject, detour, patch, or locate a game object.
// Access must occur on a verified game thread after obtaining a valid camera component.

#include <cstddef>
#include <cstdint>
#include <optional>

namespace FH3Native {

enum class CameraMode : std::uint32_t {
    Follow = 0,
    Free = 6,
    Wheel = 9,
    TrackSegment = 12,
};

inline constexpr std::size_t kCameraControllerSubobjectOffset = 0x1A0;
inline constexpr std::size_t kSetCameraModeVtableByteOffset = 0x20;
inline constexpr std::size_t kGetCameraModeVtableByteOffset = 0x50;

// Receives the object returned by the game's camera-owner virtual method +0x40.
// It does not accept a CForza*, CRenderThread*, or Frame11*.
class CameraModeBridge final {
public:
    explicit CameraModeBridge(void* cameraOwner) noexcept : owner_(cameraOwner) {}

    // Verifies readable objects/vtable and function pointers, no camera calls.
    bool CheckStructure() const noexcept;

    // Invokes the original camera-control vtable methods.
    // Precondition: owner's provenance and game-thread context are independently verified.
    bool SetMode(CameraMode mode) const noexcept;
    std::optional<std::uint32_t> GetMode() const noexcept;

private:
    void* owner_{};
    void* ControllerSubobject() const noexcept;
    void* MethodAt(std::size_t byteOffset) const noexcept;
};

} // namespace FH3Native
