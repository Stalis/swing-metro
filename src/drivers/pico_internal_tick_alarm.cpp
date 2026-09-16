#include "pico_internal_tick_alarm.h"

namespace SwingMetro {

auto PicoInternalTickAlarm::start(std::uint8_t bpm,
                                  InternalTickDiscardReason discardReason) noexcept -> void {
    critical_section_enter_blocking(&_criticalSection);
    if (_alarmArmed) {
        cancel_alarm(_alarmId);
    }
    _alarmArmed = false;
    _active = true;
    arm(_source.start(time_us_32(), bpm, discardReason));
    critical_section_exit(&_criticalSection);
}

auto PicoInternalTickAlarm::stop(InternalTickDiscardReason discardReason) noexcept -> void {
    critical_section_enter_blocking(&_criticalSection);
    _active = false;
    _source.stop(discardReason);
    if (_alarmArmed) {
        cancel_alarm(_alarmId);
    }
    _alarmArmed = false;
    critical_section_exit(&_criticalSection);
}

auto PicoInternalTickAlarm::setBpm(std::uint8_t bpm) noexcept -> void {
    critical_section_enter_blocking(&_criticalSection);
    _source.setBpm(bpm);
    if (_active) {
        if (_alarmArmed) {
            cancel_alarm(_alarmId);
        }
        _alarmArmed = false;
        arm(InternalTickSource::periodForBpm(bpm));
    }
    critical_section_exit(&_criticalSection);
}

auto PicoInternalTickAlarm::alarmCallback(alarm_id_t, void* userData) -> int64_t {
    auto& alarm = *static_cast<PicoInternalTickAlarm*>(userData);
    critical_section_enter_blocking(&alarm._criticalSection);
    alarm._alarmArmed = false;
    if (alarm._active) {
        const auto callbackAtUs = time_us_32();
        alarm.arm(alarm._source.onAlarm(alarm._scheduledDeadlineUs, callbackAtUs));
    }
    critical_section_exit(&alarm._criticalSection);
    return 0;
}

auto PicoInternalTickAlarm::arm(std::uint32_t delayUs) noexcept -> void {
    if (delayUs == 0) {
        return;
    }
    const auto scheduledDeadline = make_timeout_time_us(delayUs);
    _scheduledDeadlineUs = static_cast<std::uint32_t>(to_us_since_boot(scheduledDeadline));
    const auto alarmId = add_alarm_at(scheduledDeadline, alarmCallback, this, true);
    if (alarmId < 0) {
        _source.onAlarmArmFailure();
        return;
    }
    _alarmId = alarmId;
    _alarmArmed = true;
}

} // namespace SwingMetro
