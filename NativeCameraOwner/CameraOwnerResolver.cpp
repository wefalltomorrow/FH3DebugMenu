#include "CameraOwnerResolver.hpp"
#include <limits>

namespace FH3Native {
namespace {
constexpr bool Valid(std::uintptr_t p) noexcept {
    return p >= 0x10000 && p <= 0x00007FFFFFFFFFFFULL;
}
constexpr bool Add(std::uintptr_t base,std::uintptr_t offset,std::uintptr_t &out) noexcept {
    if (base>std::numeric_limits<std::uintptr_t>::max()-offset) return false;
    out=base+offset;return true;
}
bool ReadPtr(const MemoryReader& memory,std::uintptr_t base,std::uintptr_t offset,
             std::uintptr_t& value) noexcept {
    std::uintptr_t at;
    if (!Add(base,offset,at)) return false;
    value=0;
    return memory.Read(at,&value,sizeof value);
}
bool InImage(std::uintptr_t address,std::uintptr_t base) noexcept {
    return address>=base && address-base<kRetailImageSize;
}
}
CameraOwnerResolution ResolveRetailCameraOwner(const MemoryReader& m,
                                                std::uintptr_t imageBase,
                                                std::uintptr_t splitView) noexcept {
    CameraOwnerResolution r{};
    r.splitView=splitView;
    if (!Valid(imageBase)||!Valid(splitView)) {
        r.status=CameraOwnerStatus::InvalidSplitView;return r;
    }
    std::uintptr_t vt{};
    if(!Add(imageBase,kRetailSplitViewVtableRva,vt))return r;
    // Check against the version-pinned class, not a generic pointer.
    std::uintptr_t actualVtable{};
    if (!ReadPtr(m,splitView,0,actualVtable))return r;
    if(actualVtable!=vt) {
        r.status=CameraOwnerStatus::UnexpectedSplitViewVtable;return r;
    }
    std::uintptr_t getter{};
    if(!ReadPtr(m,actualVtable,0x40,getter))return r;
    if(getter!=imageBase+kRetailSplitViewGetterRva){
        r.status=CameraOwnerStatus::UnexpectedGetter;return r;
    }
    if(!ReadPtr(m,splitView,kSplitViewOwnerPointerOffset,r.owner))return r;
    if(!Valid(r.owner)) {
        r.status=CameraOwnerStatus::OwnerNotReady;return r;
    }
    if(!ReadPtr(m,r.owner,0,r.ownerVtable))return r;
    if(!Valid(r.ownerVtable)) {
        r.status=CameraOwnerStatus::OwnerNotReady;return r;
    }
    if(!Add(r.owner,kOwnerCameraControlOffset,r.cameraInterface))return r;
    if(!ReadPtr(m,r.cameraInterface,0,r.cameraVtable))return r;
    if(!Valid(r.cameraVtable)) {
        r.status=CameraOwnerStatus::InterfaceNotReady;return r;
    }
    if(!ReadPtr(m,r.cameraVtable,kCameraSetSlot,r.setModeFunction) ||
       !ReadPtr(m,r.cameraVtable,kCameraGetSlot,r.getModeFunction)) return r;
    if(!InImage(r.setModeFunction,imageBase) || !InImage(r.getModeFunction,imageBase)) {
        r.status=CameraOwnerStatus::ControllerMethodOutOfImage;return r;
    }
    r.status=CameraOwnerStatus::Success;
    return r;
}
}
