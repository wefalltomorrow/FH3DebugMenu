#include "CameraModeBridge.hpp"

#include <cstring>
#include <limits>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace FH3Native {
namespace {

bool IsMemoryRangeReadable(const void* address, std::size_t bytes) noexcept {
    if (!address || !bytes) return false;
    const auto p = reinterpret_cast<std::uintptr_t>(address);
    if (p > std::numeric_limits<std::uintptr_t>::max() - (bytes - 1)) return false;
#ifdef _WIN32
    std::uintptr_t cursor = p;
    const std::uintptr_t end = p + bytes;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (!VirtualQuery(reinterpret_cast<LPCVOID>(cursor), &mbi, sizeof(mbi))) return false;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD) || (mbi.Protect & PAGE_NOACCESS)) return false;
        const DWORD mode = mbi.Protect & 0xff;
        const bool readable = mode == PAGE_READONLY || mode == PAGE_READWRITE || mode == PAGE_WRITECOPY ||
                              mode == PAGE_EXECUTE_READ || mode == PAGE_EXECUTE_READWRITE || mode == PAGE_EXECUTE_WRITECOPY;
        if (!readable) return false;
        const auto base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
        const auto next = base + mbi.RegionSize;
        if (next <= cursor) return false;
        cursor = next;
    }
#endif
    return true;
}

bool IsExecutable(const void* address) noexcept {
    if (!address) return false;
#ifdef _WIN32
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(address, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & PAGE_GUARD)) return false;
    const DWORD mode = mbi.Protect & 0xff;
    return mode == PAGE_EXECUTE || mode == PAGE_EXECUTE_READ || mode == PAGE_EXECUTE_READWRITE ||
           mode == PAGE_EXECUTE_WRITECOPY;
#else
    return true; // Unit tests use actual native functions; no page metadata needed.
#endif
}

template <typename T>
bool TryRead(const void* address, T& value) noexcept {
    if (!IsMemoryRangeReadable(address, sizeof(T))) return false;
    std::memcpy(&value, address, sizeof(T));
    return true;
}

} // namespace

void* CameraModeBridge::ControllerSubobject() const noexcept {
    if (!owner_) return nullptr;
    const auto base = reinterpret_cast<std::uintptr_t>(owner_);
    if (base > std::numeric_limits<std::uintptr_t>::max() - kCameraControllerSubobjectOffset) return nullptr;
    return reinterpret_cast<void*>(base + kCameraControllerSubobjectOffset);
}

void* CameraModeBridge::MethodAt(std::size_t byteOffset) const noexcept {
    auto* sub = ControllerSubobject();
    void* vtable = nullptr;
    if (!TryRead(sub, vtable) || !vtable) return nullptr;
    auto p = reinterpret_cast<std::uintptr_t>(vtable);
    if (p > std::numeric_limits<std::uintptr_t>::max() - byteOffset) return nullptr;
    void* method = nullptr;
    if (!TryRead(reinterpret_cast<const void*>(p + byteOffset), method)) return nullptr;
    return IsExecutable(method) ? method : nullptr;
}

bool CameraModeBridge::CheckStructure() const noexcept {
    return MethodAt(kSetCameraModeVtableByteOffset) && MethodAt(kGetCameraModeVtableByteOffset);
}

bool CameraModeBridge::SetMode(CameraMode mode) const noexcept {
    // Do not invoke arbitrary pointers on structurally invalid objects.
    if (!CheckStructure()) return false;
    using SetFn = void (*)(void*, std::uint32_t);
    const auto method = reinterpret_cast<SetFn>(MethodAt(kSetCameraModeVtableByteOffset));
    if (!method) return false;
    method(ControllerSubobject(), static_cast<std::uint32_t>(mode));
    return true;
}

std::optional<std::uint32_t> CameraModeBridge::GetMode() const noexcept {
    if (!CheckStructure()) return std::nullopt;
    using GetFn = std::uint32_t (*)(void*);
    const auto method = reinterpret_cast<GetFn>(MethodAt(kGetCameraModeVtableByteOffset));
    if (!method) return std::nullopt;
    return method(ControllerSubobject());
}

} // namespace FH3Native
