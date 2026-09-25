#pragma once

#include "engine/stage5_instrumentation.h"

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
#if SWING_METRO_STAGE5_INSTRUMENTATION
            if (interval > _window.maxActualIntervalUs) {
                _window.maxActualIntervalUs = interval;
            }
#endif
            if (interval > INTERVAL_BOUND_US) {
                _intervalsAboveBound = saturatingIncrement(_intervalsAboveBound);
#if SWING_METRO_STAGE5_INSTRUMENTATION
                _window.intervalsAboveBound = saturatingIncrement(_window.intervalsAboveBound);
#endif
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
#if SWING_METRO_STAGE5_INSTRUMENTATION
        const auto snapshot = _window;
        _window = {};
        return snapshot;
#else
        return {};
#endif
    }

  private:
    std::uint32_t _previousSampleUs = 0;
    std::uint32_t _maxActualIntervalUs = 0;
    std::uint32_t _intervalsAboveBound = 0;
#if SWING_METRO_STAGE5_INSTRUMENTATION
    EncoderSampleWindowDiagnostics _window{};
#endif
    bool _hasPreviousSample = false;
};

} // namespace SwingMetro
