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
    (void)_source.start(time_us_32(), bpm, discardReason);
    arm(_source.alarmRequest());
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
    if (_active) {
        if (_alarmArmed) {
            cancel_alarm(_alarmId);
        }
        _alarmArmed = false;
        (void)_source.setBpmAt(bpm, time_us_32());
        arm(_source.alarmRequest());
    } else {
        _source.setBpm(bpm);
    }
    critical_section_exit(&_criticalSection);
}

auto PicoInternalTickAlarm::alarmCallback(alarm_id_t id, void* userData) -> int64_t {
    auto& alarm = *static_cast<PicoInternalTickAlarm*>(userData);
    critical_section_enter_blocking(&alarm._criticalSection);
    if (alarm._active && alarm._alarmArmed && id == alarm._alarmId) {
        const auto request = alarm._request;
        alarm._alarmArmed = false;
        const auto callbackAtUs = time_us_32();
        (void)alarm._source.onAlarm(request, callbackAtUs);
        if (const auto next = alarm._source.alarmRequest(); next.valid()) {
            alarm.arm(next);
        } else {
            alarm._active = false;
        }
    } else {
        (void)alarm._source.onAlarm({}, time_us_32());
    }
    critical_section_exit(&alarm._criticalSection);
    return 0;
}

auto PicoInternalTickAlarm::arm(InternalTickAlarmRequest request) noexcept -> void {
    if (!request.valid()) {
        return;
    }
    const auto now = get_absolute_time();
    const auto nowUs = static_cast<std::uint32_t>(to_us_since_boot(now));
    const auto delayUs = request.deadlineUs - nowUs;
    if (delayUs >= TIMESTAMP_COMPARISON_HORIZON_US) {
        _source.onAlarmArmFailure(request);
        if (!_source.alarmRequest().valid()) {
            _active = false;
        }
        return;
    }
    const auto targetUs = to_us_since_boot(now) + static_cast<std::uint64_t>(delayUs);
    const auto alarmId = add_alarm_at(from_us_since_boot(targetUs), alarmCallback, this, false);
    if (alarmId <= 0) {
        _source.onAlarmArmFailure(request);
        if (!_source.alarmRequest().valid()) {
            _active = false;
        }
        return;
    }
    _alarmId = alarmId;
    _request = request;
    _alarmArmed = true;
}

} // namespace SwingMetro
