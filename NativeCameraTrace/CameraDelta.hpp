#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace FH3Trace {
struct FieldChange {
    std::size_t offset = 0;
    std::size_t transitions = 0;
    std::size_t distinctValues = 0; // capped at 9, meaning 9 or more
    std::uint32_t initial = 0;
    std::uint32_t last = 0;
};
class CameraDelta {
public:
    explicit CameraDelta(std::size_t bytes);
    bool Add(const std::vector<std::uint8_t>& snapshot);
    std::size_t Samples() const noexcept { return samples_; }
    std::vector<FieldChange> SparseChanges(std::size_t maxTransitions) const;
private:
    std::size_t bytes_;
    std::size_t samples_{};
    std::vector<FieldChange> fields_;
    std::vector<std::vector<std::uint32_t>> distinct_;
};
} // namespace FH3Trace
