#include "CameraRequestState.hpp"
namespace FH3Native {
void CameraRequestState::Queue(NativeRequest request) noexcept {
    pending_.store(static_cast<int>(request), std::memory_order_release);
}
int CameraRequestState::Pending() const noexcept {
    return pending_.load(std::memory_order_acquire);
}
bool CameraRequestState::HasRestoreMode() const noexcept { return saved_; }
std::uint32_t CameraRequestState::SavedMode() const noexcept { return savedMode_; }
CommandResult CameraRequestState::OnGameFrame(ICameraCommands& camera) noexcept {
    const int request = pending_.load(std::memory_order_acquire);
    if (!request) return CommandResult::NoRequest;
    if (camera.OwnerId() == 0) return CommandResult::Deferred;
    // A newly created view may own a different camera. Never apply a previously
    // captured mode to a different owner.
    if (lastOwner_ && lastOwner_ != camera.OwnerId()) saved_ = false;
    lastOwner_ = camera.OwnerId();
    std::uint32_t requestedMode{};
    if (request == static_cast<int>(NativeRequest::Free)) requestedMode = 6;
    else if (request == static_cast<int>(NativeRequest::Restore)) {
        if (!saved_) {
            int expected = request;
            pending_.compare_exchange_strong(expected, 0);
            return CommandResult::InvalidMode;
        }
        requestedMode = savedMode_;
    } else {
        int expected = request;
        pending_.compare_exchange_strong(expected, 0);
        return CommandResult::InvalidMode;
    }
    if (!camera.IsRegistered(requestedMode)) {
        int expected = request; pending_.compare_exchange_strong(expected, 0);
        return CommandResult::InvalidMode;
    }
    std::uint32_t oldMode{};
    if (!camera.ReadMode(oldMode)) return CommandResult::Deferred;
    if (request == static_cast<int>(NativeRequest::Free) && !saved_) {
        savedMode_ = oldMode;
        saved_ = true;
    }
    if (!camera.WriteMode(requestedMode)) return CommandResult::CallFailed;
    int expected = request;
    pending_.compare_exchange_strong(expected, 0);
    if (request == static_cast<int>(NativeRequest::Restore)) saved_ = false;
    return request == static_cast<int>(NativeRequest::Restore) ?
           CommandResult::RestoreApplied : CommandResult::Applied;
}
}
