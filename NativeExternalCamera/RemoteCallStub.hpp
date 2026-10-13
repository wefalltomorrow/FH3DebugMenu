#pragma once
#include <cstdint>
#include <vector>

namespace FH3External {
inline constexpr std::uint32_t kSuccessMarker = 0x46483336U; // "FH36"
bool SupportedDeveloperMode(std::uint32_t mode) noexcept;
std::vector<std::uint8_t> BuildSentinelStub(std::uint32_t marker=kSuccessMarker);
std::vector<std::uint8_t> BuildSetModeStub(std::uintptr_t cameraInterface,
                                           std::uintptr_t setter,
                                           std::uint32_t mode,
                                           std::uint32_t marker=kSuccessMarker);
} // namespace FH3External
