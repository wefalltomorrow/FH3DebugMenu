#pragma once
#include <atomic>
#include <cstdint>

namespace FH3Native {
// A single request slot: the producer never invokes game functions.
enum class NativeRequest : int {
    None = 0,
    Free = 6,
    Restore = 100,
};

enum class CommandResult {
    NoRequest,
    Applied,
    RestoreApplied,
    Deferred,
    InvalidMode,
    CallFailed,
};

// Testable abstraction. Implementations MUST be invoked from an established
// game update callback, never a worker thread.
struct ICameraCommands {
    virtual ~ICameraCommands() = default;
    virtual bool IsRegistered(std::uint32_t mode) const noexcept = 0;
    virtual bool ReadMode(std::uint32_t& mode) const noexcept = 0;
    virtual bool WriteMode(std::uint32_t mode) const noexcept = 0;
    virtual std::uintptr_t OwnerId() const noexcept = 0;
};

class CameraRequestState final {
public:
    void Queue(NativeRequest request) noexcept;
    // Process only from original CForza frame update thread.
    CommandResult OnGameFrame(ICameraCommands& camera) noexcept;
    int Pending() const noexcept;
    bool HasRestoreMode() const noexcept;
    std::uint32_t SavedMode() const noexcept;
private:
    std::atomic<int> pending_{0};
    std::uintptr_t lastOwner_{};
    bool saved_{};
    std::uint32_t savedMode_{};
};
}
