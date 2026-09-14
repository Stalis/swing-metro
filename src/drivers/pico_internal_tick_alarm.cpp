#include "pico_internal_tick_alarm.h"

namespace SwingMetro {

auto PicoInternalTickAlarm::start(std::uint8_t bpm) noexcept -> void {
    critical_section_enter_blocking(&_criticalSection);
    if (_alarmId != 0) {
        cancel_alarm(_alarmId);
    }
    _active = true;
    arm(_source.start(time_us_32(), bpm));
    critical_section_exit(&_criticalSection);
}

auto PicoInternalTickAlarm::stop() noexcept -> void {
    critical_section_enter_blocking(&_criticalSection);
    _active = false;
    _source.stop();
    if (_alarmId != 0) {
        cancel_alarm(_alarmId);
        _alarmId = 0;
    }
    critical_section_exit(&_criticalSection);
}

auto PicoInternalTickAlarm::setBpm(std::uint8_t bpm) noexcept -> void {
    critical_section_enter_blocking(&_criticalSection);
    _source.setBpm(bpm);
    if (_active) {
        if (_alarmId != 0) {
            cancel_alarm(_alarmId);
        }
        arm(InternalTickSource::periodForBpm(bpm));
    }
    critical_section_exit(&_criticalSection);
}

auto PicoInternalTickAlarm::alarmCallback(alarm_id_t, void* userData) -> int64_t {
    auto& alarm = *static_cast<PicoInternalTickAlarm*>(userData);
    critical_section_enter_blocking(&alarm._criticalSection);
    alarm._alarmId = 0;
    if (alarm._active) {
        alarm.arm(alarm._source.onAlarm(time_us_32()));
    }
    critical_section_exit(&alarm._criticalSection);
    return 0;
}

auto PicoInternalTickAlarm::arm(std::uint32_t delayUs) noexcept -> void {
    _alarmId = delayUs == 0 ? 0 : add_alarm_in_us(delayUs, alarmCallback, this, true);
}

} // namespace SwingMetro
