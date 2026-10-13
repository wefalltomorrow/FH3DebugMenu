#include "CameraModeBridge.hpp"
#include <array>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <iostream>

using namespace FH3Native;
namespace {
void Require(bool good, const char* message) {
    if (!good) { std::cerr << "FAIL: " << message << "\n"; std::exit(1); }
}

struct MockCameraOwner {
    std::array<std::byte, 0x1A0> prefix{};
    void** vtable{};
    std::uint32_t mode{};
};
static_assert(offsetof(MockCameraOwner, vtable)==0x1A0);
static_assert(offsetof(MockCameraOwner, mode)==0x1A8);
void Set(void* ptr, std::uint32_t value) {
    auto* owner = reinterpret_cast<MockCameraOwner*>(reinterpret_cast<std::byte*>(ptr)-0x1A0);
    owner->mode=value;
}
std::uint32_t Get(void* ptr) {
    auto* owner = reinterpret_cast<MockCameraOwner*>(reinterpret_cast<std::byte*>(ptr)-0x1A0);
    return owner->mode;
}
} // namespace
int main() {
    std::array<void*, 11> vtable{};
    vtable[0x20/sizeof(void*)] = reinterpret_cast<void*>(&Set);
    vtable[0x50/sizeof(void*)] = reinterpret_cast<void*>(&Get);
    MockCameraOwner owner{};
    owner.vtable=vtable.data();
    CameraModeBridge bridge(&owner);
    Require(bridge.CheckStructure(),"bridge valid");
    for (auto value : {CameraMode::Follow,CameraMode::Free,CameraMode::Wheel,CameraMode::TrackSegment}) {
        Require(bridge.SetMode(value),"set mode succeeded");
        Require(bridge.GetMode().has_value(),"get mode returned value");
        Require(*bridge.GetMode()==static_cast<std::uint32_t>(value),"mode readback matches");
    }
    CameraModeBridge invalid(nullptr);
    Require(!invalid.CheckStructure(),"null owner invalid");
    Require(!invalid.SetMode(CameraMode::Free),"null owner set rejected");
    Require(!invalid.GetMode(),"null owner get rejected");
    std::cout << "PASS: 4 camera modes, method-slot offsets, null owner handling\n";
}
