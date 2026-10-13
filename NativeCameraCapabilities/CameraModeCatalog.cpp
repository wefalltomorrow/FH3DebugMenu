#include "CameraModeCatalog.hpp"
#include <limits>
namespace FH3Native {
namespace {
bool Valid(std::uintptr_t p) noexcept {
    return p>=0x10000 && p<=0x00007FFFFFFFFFFFULL;
}
bool Read(const MemoryReader& memory,std::uintptr_t address,
          std::uintptr_t& out) noexcept {
    out=0;
    if (!Valid(address)) return false;
    return memory.Read(address,&out,sizeof(out));
}
bool Add(std::uintptr_t a,std::uintptr_t b,std::uintptr_t& sum) noexcept {
    if(a>std::numeric_limits<std::uintptr_t>::max()-b) return false;
    sum=a+b;return true;
}
}
ModeCatalog InspectCameraModeCatalog(const MemoryReader& memory,
                                     std::uintptr_t owner) noexcept {
    ModeCatalog result{};
    if (!Valid(owner)) {result.status=ModeCatalogStatus::InvalidOwner;return result;}
    std::uintptr_t atBegin{},atEnd{};
    if(!Add(owner,kCameraModeTableStartOffset,atBegin)||
       !Add(owner,kCameraModeTableEndOffset,atEnd)||
       !Read(memory,atBegin,result.arrayBegin)||
       !Read(memory,atEnd,result.arrayEnd)) {
        result.status=ModeCatalogStatus::ReadFailed;return result;
    }
    if(!Valid(result.arrayBegin)||result.arrayEnd<result.arrayBegin||
       (result.arrayEnd-result.arrayBegin)%kCameraModeEntryStride!=0) {
        result.status=ModeCatalogStatus::InvalidArrayBounds;return result;
    }
    result.tableSize=(result.arrayEnd-result.arrayBegin)/kCameraModeEntryStride;
    if(result.tableSize>kMaxCameraModes){result.status=ModeCatalogStatus::TooManyModes;return result;}
    result.status=ModeCatalogStatus::Success;
    for(auto& c:result.commands){
        if(c.mode>=result.tableSize){c.support=ModeSupport::NotInTable;continue;}
        std::uintptr_t address{},offset{};
        if(!Add(static_cast<std::uintptr_t>(c.mode)*kCameraModeEntryStride,
                kCameraModeEntryHandlerOffset,offset)||
           !Add(result.arrayBegin,offset,address)||
           !Read(memory,address,c.handler)){
            c.support=ModeSupport::Unreadable;continue;
        }
        c.support=c.handler ? ModeSupport::Registered : ModeSupport::EmptySlot;
    }
    return result;
}
}
