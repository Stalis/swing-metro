#pragma once

#include <cstdint>
#include <limits>

namespace SwingMetro {

struct EncoderSampleWindowDiagnostics {
    std::uint32_t maxActualIntervalUs = 0;
    std::uint32_t intervalsAboveBound = 0;
};

class EncoderSampleDiagnostics {
  public:
    static constexpr std::uint32_t INTERVAL_BOUND_US = 1'250;

    auto recordSample(std::uint32_t nowUs) noexcept -> void {
        if (_hasPreviousSample) {
            const auto interval = nowUs - _previousSampleUs;
            if (interval > _maxActualIntervalUs) {
                _maxActualIntervalUs = interval;
            }
            if (interval > _window.maxActualIntervalUs) {
                _window.maxActualIntervalUs = interval;
            }
            if (interval > INTERVAL_BOUND_US) {
                _intervalsAboveBound = saturatingIncrement(_intervalsAboveBound);
                _window.intervalsAboveBound = saturatingIncrement(_window.intervalsAboveBound);
            }
        }
        _previousSampleUs = nowUs;
        _hasPreviousSample = true;
    }

    [[nodiscard]] static constexpr auto saturatingIncrement(std::uint32_t value) noexcept
        -> std::uint32_t {
        return value == std::numeric_limits<std::uint32_t>::max() ? value : value + 1U;
    }

    [[nodiscard]] auto maxActualIntervalUs() const noexcept -> std::uint32_t {
        return _maxActualIntervalUs;
    }
    [[nodiscard]] auto intervalsAboveBound() const noexcept -> std::uint32_t {
        return _intervalsAboveBound;
    }
    [[nodiscard]] auto snapshotAndResetWindow() noexcept -> EncoderSampleWindowDiagnostics {
        const auto snapshot = _window;
        _window = {};
        return snapshot;
    }

  private:
    std::uint32_t _previousSampleUs = 0;
    std::uint32_t _maxActualIntervalUs = 0;
    std::uint32_t _intervalsAboveBound = 0;
    EncoderSampleWindowDiagnostics _window{};
    bool _hasPreviousSample = false;
};

} // namespace SwingMetro
