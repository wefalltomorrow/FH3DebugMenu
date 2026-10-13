#include "CameraObjectLocator.hpp"
#include <algorithm>
#include <limits>

namespace FH3Native {
namespace {
constexpr std::size_t kMaxPlausibleCount = 65536;

bool Add(std::uintptr_t base, std::uintptr_t offset, std::uintptr_t& out) noexcept {
    if (base > std::numeric_limits<std::uintptr_t>::max() - offset) return false;
    out = base + offset;
    return true;
}

bool Pointer(const MemoryReader& m, std::uintptr_t base, std::uintptr_t offset,
             std::uintptr_t& value) noexcept {
    std::uintptr_t where{};
    if (!Add(base, offset, where)) return false;
    value = 0;
    return m.Read(where, &value, sizeof value);
}

bool Valid(std::uintptr_t value) noexcept {
    return value >= 0x10000 && value <= 0x00007FFFFFFFFFFFULL;
}
} // namespace

CameraLocateResult LocateCameraObjects(const MemoryReader& m,
                                       std::uintptr_t imageBase,
                                       std::size_t maxObjects) {
    CameraLocateResult r;
    if (!Valid(imageBase)) {r.status = CameraLocateStatus::MissingObject; return r;}
    if (!Pointer(m, imageBase, kRetailCForzaStorageRva, r.forza)) return r;
    if (!Valid(r.forza)) {r.status = CameraLocateStatus::MissingObject; return r;}
    std::uintptr_t vtable{};
    if (!Pointer(m, r.forza, 0, vtable)) return r;
    if (vtable != imageBase + kRetailCForzaVtableRva) {
        r.status = CameraLocateStatus::WrongCForzaVtable; return r;
    }
    if (!Pointer(m, r.forza, 0x50, r.gameStateHolder)) return r;
    if (!Valid(r.gameStateHolder)) {r.status = CameraLocateStatus::MissingObject; return r;}
    if (!Pointer(m, r.gameStateHolder, 0x98, r.collectionOwner)) return r;
    if (!Valid(r.collectionOwner)) {r.status = CameraLocateStatus::MissingObject; return r;}
    std::uintptr_t begin{}, end{};
    if (!Pointer(m, r.collectionOwner, 0x80, begin) ||
        !Pointer(m, r.collectionOwner, 0x88, end)) return r;
    if (end < begin || (end-begin) % sizeof(std::uintptr_t) != 0 ||
        (end-begin) > kMaxPlausibleCount * sizeof(std::uintptr_t)) {
        r.status = CameraLocateStatus::InvalidArrayBounds;
        return r;
    }
    r.collectionCount = (end-begin)/sizeof(std::uintptr_t);
    const std::size_t count = std::min(r.collectionCount, maxObjects);
    r.inspectedObjects.reserve(count);
    for (std::size_t i=0; i<count; ++i) {
        RetailCameraObject item;
        item.index = static_cast<std::uint32_t>(i);
        if (!Pointer(m, begin, i*sizeof(std::uintptr_t), item.object)) return r;
        if (Valid(item.object)) {
            if (!Pointer(m, item.object, 0, item.vtable)) return r;
            if (Valid(item.vtable)) {
                if (!Pointer(m, item.vtable, 0x40, item.getterMethod)) return r;
            }
        }
        r.inspectedObjects.push_back(item);
    }
    r.status = CameraLocateStatus::Success;
    return r;
}
} // namespace FH3Native
