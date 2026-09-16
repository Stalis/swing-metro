#pragma once

#include "engine/internal_tick_source.h"

#include <pico/sync.h>
#include <pico/time.h>

#include <cstdint>

namespace SwingMetro {

class PicoInternalTickAlarm {
  public:
    explicit PicoInternalTickAlarm(InternalTickSource& source) noexcept : _source{source} {
        critical_section_init(&_criticalSection);
    }

    ~PicoInternalTickAlarm() { stop(); }

    auto start(std::uint8_t bpm,
               InternalTickDiscardReason discardReason = InternalTickDiscardReason::Stop) noexcept
        -> void;
    auto stop(InternalTickDiscardReason discardReason = InternalTickDiscardReason::Stop) noexcept
        -> void;
    auto setBpm(std::uint8_t bpm) noexcept -> void;

  private:
    static auto alarmCallback(alarm_id_t id, void* userData) -> int64_t;
    auto arm(std::uint32_t delayUs) noexcept -> void;

    InternalTickSource& _source;
    critical_section_t _criticalSection{};
    alarm_id_t _alarmId = 0;
    std::uint32_t _scheduledDeadlineUs = 0;
    bool _alarmArmed = false;
    bool _active = false;
};

} // namespace SwingMetro
