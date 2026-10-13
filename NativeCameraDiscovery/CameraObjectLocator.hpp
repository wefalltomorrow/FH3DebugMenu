#pragma once
// FH3 retail 1.0.125.2 camera-object collection locator.
// Mirrors instructions recovered from retail RVAs 0x1B348E0, 0x3B1480,
// 0x1DF2780. Read-only: no calls, hooks, thread manipulation or memory writes.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace FH3Native {
struct MemoryReader {
    virtual ~MemoryReader() = default;
    virtual bool Read(std::uintptr_t address, void* destination, std::size_t size) const noexcept = 0;
};

struct RetailCameraObject {
    std::uint32_t index{};
    std::uintptr_t object{};
    std::uintptr_t vtable{};
    std::uintptr_t getterMethod{};
};

enum class CameraLocateStatus {
    Success,
    WrongCForzaVtable,
    ReadFailure,
    MissingObject,
    InvalidArrayBounds,
    InvalidArrayCount
};

struct CameraLocateResult {
    CameraLocateStatus status = CameraLocateStatus::ReadFailure;
    std::uintptr_t forza{};
    std::uintptr_t gameStateHolder{};
    std::uintptr_t collectionOwner{};
    std::size_t collectionCount{};
    std::vector<RetailCameraObject> inspectedObjects;
};

inline constexpr std::uintptr_t kRetailCForzaStorageRva = 0x5CB5BF0;
inline constexpr std::uintptr_t kRetailCForzaVtableRva = 0x5423EC0;

// Locates, but does not invoke, the camera-owner getter at virtual +0x40.
// Never assumes that this getter returns an inline field or a particular class.
CameraLocateResult LocateCameraObjects(const MemoryReader& memory,
                                       std::uintptr_t imageBase,
                                       std::size_t maxObjects = 24);
} // namespace FH3Native
