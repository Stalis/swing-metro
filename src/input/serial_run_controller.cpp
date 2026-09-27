#include "serial_run_controller.h"

namespace SwingMetro {
#if defined(SWING_METRO_STAGE5_FAULT_SCENARIOS)
namespace {
constexpr std::array<const char*, 4> FAULT_SCENARIO_NAMES = {
    "baseline", "retry_first_clock", "sustained_backpressure", "deterministic_disconnect"};
}
#endif

void SerialRunController::announceStart() {
    _console.print("swing_metro_control_v1,run_started,");
    _console.print(_command.durationMs);
    _console.print(',');
    _console.print(static_cast<std::uint32_t>(_command.bpm));
    _console.print(',');
    _console.println(static_cast<std::uint32_t>(_command.swing));
    _console.flush();
    _startedAtMs = _console.nowMs();
    _active = true;
}

void SerialRunController::pollSerialRunCommand() {
    if (_preparing || _active || _completionPending || _capture.pending() ||
        _transport.usesInternalTiming()) {
        return;
    }
    for (int value = _console.read(); value >= 0; value = _console.read()) {
        const auto byte = static_cast<char>(value);
#if defined(SWING_METRO_STAGE5_FAULT_SCENARIOS)
        if (_faultSink != nullptr) {
            if (!_faultCommand && byte == 'F') {
                _faultCommand = true;
            }
            if (_faultCommand) {
                const auto result = _faultParser.push(byte);
                if (byte == '\n') {
                    _faultCommand = false;
                }
                if (result.status == FaultCommandStatus::Pending) {
                    continue;
                }
                if (result.status != FaultCommandStatus::Ready || _sequencer.isRunning()) {
                    _console.println("swing_metro_fault_v1,error,select_only_while_stopped");
                    continue;
                }
                _faultSink->select(result.scenario);
                _console.print("swing_metro_fault_v1,selected,");
                _console.println(FAULT_SCENARIO_NAMES[static_cast<std::size_t>(result.scenario)]);
                _console.flush();
                continue;
            }
        }
#endif
        const auto result = _parser.push(byte);
        if (result.status == SerialRunCommandStatus::Pending) {
            continue;
        }
        if (result.status == SerialRunCommandStatus::Invalid || _sequencer.isRunning()) {
            _console.println("swing_metro_control_v1,error,expected RUN or EXTERNAL_RUN "
                             "<ms> <bpm> <swing> while stopped");
            continue;
        }
        _command = result.command;
        if (_command.mode == SerialRunMode::External) {
            if (_clockSettings.mode() != MidiClockMode::External) {
                _console.println(
                    "swing_metro_control_v1,error,external_run_requires_external_mode");
                continue;
            }
            _startSnapshotGeneration = _runtime.requestSnapshot();
            _preparing = true;
            return;
        }
        _tempo.setValue(_command.bpm);
        _swing.setValue(_command.swing);
        _sequencer.setBpm(_tempo.getValue());
        _sequencer.setSwing(_swing.getValue());
        _transport.applyMode(MidiClockMode::Internal, _console.nowUs());
        announceStart();
        _transport.toggle(_console.nowUs());
        return;
    }
}

void SerialRunController::updateSerialRun() {
    if (_preparing) {
        RuntimeTimingSnapshot discarded;
        if (!_runtime.readSnapshot(_startSnapshotGeneration, discarded)) {
            return;
        }
        (void)_encoder.snapshotAndResetWindow();
        announceStart();
        _preparing = false;
        return;
    }
    if (!_active) {
        return;
    }
    if (_command.mode == SerialRunMode::Internal && !_transport.usesInternalTiming()) {
        _active = false;
        _completionPending = true;
        return;
    }
    if (_console.nowMs() - _startedAtMs < _command.durationMs) {
        return;
    }
    if (_command.mode == SerialRunMode::Internal) {
        _transport.toggle(_console.nowUs());
    } else if (_clockSettings.mode() != MidiClockMode::External) {
        _console.println("swing_metro_control_v1,error,external_mode_changed_during_run");
        _active = false;
        return;
    } else if (_transport.isRunning()) {
        _console.println("swing_metro_control_v1,error,external_transport_still_running");
        _active = false;
        return;
    } else {
        _capture.requestExport();
    }
    _active = false;
    _completionPending = true;
}

void SerialRunController::completeIfReady(bool internalAlarmActive) {
    if (_completionPending && !internalAlarmActive && !_capture.pending()) {
        _console.println("swing_metro_control_v1,run_complete");
        _completionPending = false;
    }
}

} // namespace SwingMetro
