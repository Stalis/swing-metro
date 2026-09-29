#pragma once

#include "engine/diagnostics_capture.h"
#include "engine/midi_clock_mode.h"
#include "engine/sequencer.h"
#include "engine/transport_controller.h"
#if defined(SWING_METRO_STAGE5_FAULT_SCENARIOS)
#include "engine/fault_midi_message_sink.h"
#include "fault_command_parser.h"
#endif
#include "serial_run_command.h"
#include <utils/counter.h>

namespace SwingMetro {

// The board adapter supplies serial bytes, time, and output. The run lifecycle
// remains independent of Arduino and can be exercised with a simulated console.
class DiagnosticConsole : public DiagnosticOutput {
  public:
    virtual int read() = 0; // -1 when no byte is available
    virtual void flush() = 0;
    virtual std::uint32_t nowMs() = 0;
    virtual std::uint32_t nowUs() = 0;
};

class SerialRunController {
  public:
    SerialRunController(DiagnosticConsole& console, Sequencer& sequencer,
                        MidiClockSettings& clockSettings, TransportController& transport,
                        Counter<std::uint8_t>& tempo, Counter<std::uint8_t>& swing,
                        RuntimeTimingDiagnostics& runtime, EncoderSampleDiagnostics& encoder,
                        DiagnosticsCapture& capture
#if defined(SWING_METRO_STAGE5_FAULT_SCENARIOS)
                        ,
                        FaultMidiMessageSink* faultSink = nullptr
#endif
                        )
        : _console(console), _sequencer(sequencer), _clockSettings(clockSettings),
          _transport(transport), _tempo(tempo), _swing(swing), _runtime(runtime), _encoder(encoder),
          _capture(capture)
#if defined(SWING_METRO_STAGE5_FAULT_SCENARIOS)
          ,
          _faultSink(faultSink)
#endif
    {
    }

    void pollSerialRunCommand();
    void updateSerialRun();
    void completeIfReady(bool internalAlarmActive);

  private:
    void announceStart();
    DiagnosticConsole& _console;
    Sequencer& _sequencer;
    MidiClockSettings& _clockSettings;
    TransportController& _transport;
    Counter<std::uint8_t>& _tempo;
    Counter<std::uint8_t>& _swing;
    RuntimeTimingDiagnostics& _runtime;
    EncoderSampleDiagnostics& _encoder;
    DiagnosticsCapture& _capture;
    SerialRunCommandParser _parser;
#if defined(SWING_METRO_STAGE5_FAULT_SCENARIOS)
    FaultMidiMessageSink* _faultSink;
    FaultCommandParser _faultParser;
    bool _faultCommand = false;
#endif
    bool _preparing = false;
    bool _active = false;
    bool _completionPending = false;
    SerialRunCommand _command;
    std::uint32_t _startedAtMs = 0;
    std::uint32_t _startSnapshotGeneration = 0;
};

} // namespace SwingMetro
