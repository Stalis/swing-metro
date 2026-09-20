#pragma once

#include <cstdint>
#include <optional>

namespace InputTiming {

class TimeDebouncer {
  public:
    using Time = std::uint32_t;

    explicit constexpr TimeDebouncer(bool stableState = false) : _stableState(stableState) {}

    void reset(bool stableState) noexcept {
        _stableState = stableState;
        _candidatePending = false;
    }

    [[nodiscard]] auto observe(bool rawState, Time now, Time duration) -> std::optional<bool> {
        if (rawState == _stableState) {
            _candidatePending = false;
            return std::nullopt;
        }

        if (!_candidatePending || rawState != _candidateState) {
            _candidateState = rawState;
            _candidateSince = now;
            _candidatePending = true;
        }

        if (now - _candidateSince < duration) {
            return std::nullopt;
        }

        _stableState = _candidateState;
        _candidatePending = false;
        return _stableState;
    }

  private:
    Time _candidateSince = 0;
    bool _stableState = false;
    bool _candidateState = false;
    bool _candidatePending = false;
};

} // namespace InputTiming
