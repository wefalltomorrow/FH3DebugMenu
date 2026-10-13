#include "CameraModeCatalog.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <unordered_map>
using namespace FH3Native;
struct Fake final : MemoryReader {
    std::unordered_map<std::uintptr_t,std::uintptr_t> map;
    bool Read(std::uintptr_t a,void* out,std::size_t n) const noexcept override {
        auto it=map.find(a);
        if(n!=sizeof(std::uintptr_t)||it==map.end())return false;
        std::memcpy(out,&it->second,n);return true;
    }
};
int main(){
    constexpr std::uintptr_t owner=0x300000,begin=0x700000;
    Fake m;
    m.map[owner+0x1A8]=begin;
    m.map[owner+0x1B0]=begin+13*0x40;
    for(unsigned i: {0U,6U,9U,12U})m.map[begin+i*0x40+0x30]=0x900000+i;
    auto r=InspectCameraModeCatalog(m,owner);
    assert(r.status==ModeCatalogStatus::Success && r.tableSize==13);
    for(auto c:r.commands)assert(c.support==ModeSupport::Registered);
    m.map[begin+9*0x40+0x30]=0;
    r=InspectCameraModeCatalog(m,owner);
    assert(r.commands[2].support==ModeSupport::EmptySlot);
    m.map.erase(begin+12*0x40+0x30);
    r=InspectCameraModeCatalog(m,owner);
    assert(r.commands[3].support==ModeSupport::Unreadable);
    m.map[owner+0x1B0]=begin+8*0x40;
    r=InspectCameraModeCatalog(m,owner);
    assert(r.commands[2].support==ModeSupport::NotInTable);
    m.map[owner+0x1B0]=begin-8;
    assert(InspectCameraModeCatalog(m,owner).status==ModeCatalogStatus::InvalidArrayBounds);
    m.map[owner+0x1B0]=begin+1025*0x40;
    assert(InspectCameraModeCatalog(m,owner).status==ModeCatalogStatus::TooManyModes);
    m.map.erase(owner+0x1A8);
    assert(InspectCameraModeCatalog(m,owner).status==ModeCatalogStatus::ReadFailed);
    assert(InspectCameraModeCatalog(m,0).status==ModeCatalogStatus::InvalidOwner);
}
