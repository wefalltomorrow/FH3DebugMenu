#include "CameraOwnerResolver.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <unordered_map>
using namespace FH3Native;
struct FakeReader final : MemoryReader {
    std::unordered_map<std::uintptr_t,std::uintptr_t> memory;
    bool Read(std::uintptr_t at,void* output,std::size_t bytes) const noexcept override {
        if (bytes!=sizeof(std::uintptr_t)) return false;
        const auto it=memory.find(at);
        if (it==memory.end())return false;
        std::memcpy(output,&it->second,bytes);return true;
    }
};
int main() {
    constexpr std::uintptr_t B=0x7FF700000000ULL,S=0x200000,O=0x300000;
    FakeReader m;
    m.memory[S]=B+kRetailSplitViewVtableRva;
    m.memory[B+kRetailSplitViewVtableRva+0x40]=B+kRetailSplitViewGetterRva;
    m.memory[S+0x100]=O;
    m.memory[O]=B+0x5000000;
    m.memory[O+0x1A0]=B+0x5000100;
    m.memory[B+0x5000100+0x20]=B+0x1000000;
    m.memory[B+0x5000100+0x50]=B+0x1000100;
    const auto r=ResolveRetailCameraOwner(m,B,S);
    assert(r.status==CameraOwnerStatus::Success);
    assert(r.owner==O && r.cameraInterface==O+0x1A0);
    assert(r.setModeFunction==B+0x1000000);
    assert(ResolveRetailCameraOwner(m,B,0).status==CameraOwnerStatus::InvalidSplitView);
    m.memory[S]=B+0x1000;
    assert(ResolveRetailCameraOwner(m,B,S).status==CameraOwnerStatus::UnexpectedSplitViewVtable);
    m.memory[S]=B+kRetailSplitViewVtableRva;
    m.memory[B+kRetailSplitViewVtableRva+0x40]=B+0x2000;
    assert(ResolveRetailCameraOwner(m,B,S).status==CameraOwnerStatus::UnexpectedGetter);
    m.memory[B+kRetailSplitViewVtableRva+0x40]=B+kRetailSplitViewGetterRva;
    m.memory[S+0x100]=0;
    assert(ResolveRetailCameraOwner(m,B,S).status==CameraOwnerStatus::OwnerNotReady);
    m.memory[S+0x100]=O;
    m.memory[O+0x1A0]=0;
    assert(ResolveRetailCameraOwner(m,B,S).status==CameraOwnerStatus::InterfaceNotReady);
    m.memory[O+0x1A0]=B+0x5000100;
    m.memory[B+0x5000100+0x20]=0xA00000;
    assert(ResolveRetailCameraOwner(m,B,S).status==CameraOwnerStatus::ControllerMethodOutOfImage);
    m.memory[B+0x5000100+0x20]=B+0x1000000;
    m.memory.erase(B+0x5000100+0x50);
    assert(ResolveRetailCameraOwner(m,B,S).status==CameraOwnerStatus::UnreadableMemory);
}
