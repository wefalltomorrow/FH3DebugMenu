#include "CameraObjectLocator.hpp"
#include <array>
#ifdef NDEBUG
#undef NDEBUG // Keep assertions active in Release test builds.
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <unordered_map>

using namespace FH3Native;
struct Fake final : MemoryReader {
    std::unordered_map<std::uintptr_t,std::uintptr_t> words;
    bool Read(std::uintptr_t address, void* out, std::size_t size) const noexcept override {
        if (size != sizeof(std::uintptr_t)) return false;
        auto it=words.find(address);
        if (it==words.end()) return false;
        std::memcpy(out, &it->second, size);
        return true;
    }
};
int main() {
    constexpr std::uintptr_t B=0x7FF700000000;
    Fake f;
    f.words[B+kRetailCForzaStorageRva]=0x100000;
    f.words[0x100000]=B+kRetailCForzaVtableRva;
    f.words[0x100050]=0x200000;
    f.words[0x200098]=0x300000;
    f.words[0x300080]=0x400000;
    f.words[0x300088]=0x400010;
    f.words[0x400000]=0x500000;
    f.words[0x400008]=0x600000;
    f.words[0x500000]=0x700000;
    f.words[0x600000]=0x800000;
    f.words[0x700040]=B+0x123456;
    f.words[0x800040]=B+0x234567;
    auto r=LocateCameraObjects(f,B);
    assert(r.status==CameraLocateStatus::Success);
    assert(r.collectionCount==2 && r.inspectedObjects.size()==2);
    assert(r.inspectedObjects[0].object==0x500000);
    assert(r.inspectedObjects[0].getterMethod==B+0x123456);
    assert(r.inspectedObjects[1].getterMethod==B+0x234567);
    auto one=LocateCameraObjects(f,B,1);
    assert(one.status==CameraLocateStatus::Success && one.inspectedObjects.size()==1);
    f.words[0x100000]=B+0x1234;
    assert(LocateCameraObjects(f,B).status==CameraLocateStatus::WrongCForzaVtable);
    f.words[0x100000]=B+kRetailCForzaVtableRva;
    f.words[0x300088]=0x300000;
    assert(LocateCameraObjects(f,B).status==CameraLocateStatus::InvalidArrayBounds);
    f.words[0x300088]=0x400003;
    assert(LocateCameraObjects(f,B).status==CameraLocateStatus::InvalidArrayBounds);
    f.words[0x300088]=0x400010;
    f.words.erase(0x800040);
    assert(LocateCameraObjects(f,B).status==CameraLocateStatus::ReadFailure);
    f.words[0x800040]=B+0x234567;
    f.words[0x300088]=0x400000; // empty array is legitimate
    auto empty=LocateCameraObjects(f,B);
    assert(empty.status==CameraLocateStatus::Success && empty.collectionCount==0);
}
