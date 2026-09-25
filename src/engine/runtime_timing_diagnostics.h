#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

namespace SwingMetro {

struct RuntimeTimingSnapshot {
    std::uint32_t lvTimerHandlerCount = 0;
    std::uint32_t lvTimerHandlerInclusiveTotalUs = 0;
    std::uint32_t lvTimerHandlerInclusiveMaxUs = 0;
    std::uint32_t displayFlushCount = 0;
    std::uint32_t displayFlushInclusiveTotalUs = 0;
    std::uint32_t displayFlushInclusiveMaxUs = 0;
};

class RuntimeTimingDiagnostics {
  public:
    [[nodiscard]] auto requestSnapshot() noexcept -> std::uint32_t {
        return _requestGeneration.fetch_add(1U, std::memory_order_release) + 1U;
    }

    [[nodiscard]] auto readSnapshot(std::uint32_t requestGeneration,
                                    RuntimeTimingSnapshot& snapshot) const noexcept -> bool {
        if (_acknowledgedGeneration.load(std::memory_order_acquire) != requestGeneration) {
            return false;
        }

        const auto before = _snapshotVersion.load(std::memory_order_acquire);
        if ((before & 1U) != 0U) {
            return false;
        }
        RuntimeTimingSnapshot candidate{
            .lvTimerHandlerCount = _lvTimerHandlerCount.load(std::memory_order_relaxed),
            .lvTimerHandlerInclusiveTotalUs =
                _lvTimerHandlerInclusiveTotalUs.load(std::memory_order_relaxed),
            .lvTimerHandlerInclusiveMaxUs =
                _lvTimerHandlerInclusiveMaxUs.load(std::memory_order_relaxed),
            .displayFlushCount = _displayFlushCount.load(std::memory_order_relaxed),
            .displayFlushInclusiveTotalUs =
                _displayFlushInclusiveTotalUs.load(std::memory_order_relaxed),
            .displayFlushInclusiveMaxUs =
                _displayFlushInclusiveMaxUs.load(std::memory_order_relaxed),
        };
        if (_snapshotVersion.load(std::memory_order_acquire) != before) {
            return false;
        }
        snapshot = candidate;
        return true;
    }

    // Core 1 only: flush durations are nested inside lv_timer_handler durations.
    auto recordLvTimerHandler(std::uint32_t durationUs) noexcept -> void {
        _local.lvTimerHandlerCount = saturatingIncrement(_local.lvTimerHandlerCount);
        _local.lvTimerHandlerInclusiveTotalUs =
            saturatingAdd(_local.lvTimerHandlerInclusiveTotalUs, durationUs);
        if (durationUs > _local.lvTimerHandlerInclusiveMaxUs) {
            _local.lvTimerHandlerInclusiveMaxUs = durationUs;
        }
    }

    // Core 1 only: this time is inclusive in the enclosing handler, not separate CPU work.
    auto recordDisplayFlush(std::uint32_t durationUs) noexcept -> void {
        _local.displayFlushCount = saturatingIncrement(_local.displayFlushCount);
        _local.displayFlushInclusiveTotalUs =
            saturatingAdd(_local.displayFlushInclusiveTotalUs, durationUs);
        if (durationUs > _local.displayFlushInclusiveMaxUs) {
            _local.displayFlushInclusiveMaxUs = durationUs;
        }
    }

    // Core 1 only: call after every complete lv_timer_handler pass.
    auto publishRequestedSnapshot() noexcept -> void {
        const auto requested = _requestGeneration.load(std::memory_order_acquire);
        if (requested == _acknowledgedGeneration.load(std::memory_order_relaxed)) {
            return;
        }

        _snapshotVersion.fetch_add(1U, std::memory_order_acq_rel);
        _lvTimerHandlerCount.store(_local.lvTimerHandlerCount, std::memory_order_relaxed);
        _lvTimerHandlerInclusiveTotalUs.store(_local.lvTimerHandlerInclusiveTotalUs,
                                              std::memory_order_relaxed);
        _lvTimerHandlerInclusiveMaxUs.store(_local.lvTimerHandlerInclusiveMaxUs,
                                            std::memory_order_relaxed);
        _displayFlushCount.store(_local.displayFlushCount, std::memory_order_relaxed);
        _displayFlushInclusiveTotalUs.store(_local.displayFlushInclusiveTotalUs,
                                            std::memory_order_relaxed);
        _displayFlushInclusiveMaxUs.store(_local.displayFlushInclusiveMaxUs,
                                          std::memory_order_relaxed);
        _snapshotVersion.fetch_add(1U, std::memory_order_release);
        _local = {};
        _acknowledgedGeneration.store(requested, std::memory_order_release);
    }

    [[nodiscard]] static constexpr auto saturatingIncrement(std::uint32_t value) noexcept
        -> std::uint32_t {
        return saturatingAdd(value, 1U);
    }

    [[nodiscard]] static constexpr auto saturatingAdd(std::uint32_t value,
                                                      std::uint32_t addend) noexcept
        -> std::uint32_t {
        constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
        return addend > maximum - value ? maximum : value + addend;
    }

  private:
    RuntimeTimingSnapshot _local{};
    std::atomic<std::uint32_t> _requestGeneration{0};
    std::atomic<std::uint32_t> _acknowledgedGeneration{0};
    std::atomic<std::uint32_t> _snapshotVersion{0};
    std::atomic<std::uint32_t> _lvTimerHandlerCount{0};
    std::atomic<std::uint32_t> _lvTimerHandlerInclusiveTotalUs{0};
    std::atomic<std::uint32_t> _lvTimerHandlerInclusiveMaxUs{0};
    std::atomic<std::uint32_t> _displayFlushCount{0};
    std::atomic<std::uint32_t> _displayFlushInclusiveTotalUs{0};
    std::atomic<std::uint32_t> _displayFlushInclusiveMaxUs{0};
};

} // namespace SwingMetro
