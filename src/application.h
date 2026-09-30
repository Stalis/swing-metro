#pragma once

#include "components/ui_view_model.h"
#include "drivers/diagnostics/arduino_diagnostic_console.h"
#include "drivers/littlefs_program_storage.h"
#include "drivers/lvgl_ui.h"
#include "drivers/pico_internal_tick_alarm.h"
#include "drivers/usb_midi_adapter.h"
#include "engine/diagnostics_capture.h"
#include "engine/diagnostics_serializer.h"
#include "engine/runtime_timing_diagnostics.h"
#include "engine/session.h"
#if defined(SWING_METRO_STAGE5_FAULT_SCENARIOS)
#include "engine/fault_midi_message_sink.h"
#endif
#include "input/app_event_handler.h"
#include "input/app_input.h"
#include "input/app_input_coordinator.h"
#include "input/encoder_sample_diagnostics.h"
#include "input/pad_button_ids.h"
#include "input/periodic_scheduler.h"
#include "input/serial_run_controller.h"
#include "input/step_button_inputs.h"
#include "program/program_bank.h"
#include "program/program_draft.h"
#include "program/program_slot_store.h"
#include "program/program_storage_controller.h"
#include <Adafruit_TinyUSB.h>
#include <Arduino.h>
#include <adapters/button_input.h>
#include <adapters/encoder_input.h>
#include <array>
#include <button_matrix.h>
#include <context_input.h>
#include <cstddef>
#include <cstdint>
#include <encoder.h>
#include <utils/counter.h>

namespace SwingMetro {

class Application {
  public:
    Application();

    void setup();
    void loop();
    void setup1();
    void loop1();

  private:
    static constexpr std::size_t INPUT_CONTEXT_CAPACITY = 8;
    static constexpr std::array<uint8_t, 4> INPUT_PINS = {D0, D1, D2, D3};
    static constexpr std::array<uint8_t, 4> OUTPUT_PINS = {D4, D5, D6, D7};
    static constexpr std::uint32_t MATRIX_SCAN_PERIOD_MS = 5;
    static constexpr std::uint32_t ENCODER_SAMPLE_PERIOD_US = 1'000;

    static void tempoEncoderHandler(EncoderDirection direction);
    static void tempoEncoderSwitchHandler(std::uint32_t nowUs);
    static void tempoEncoderSwitchReleaseHandler(std::uint32_t nowUs);
    static void swingEncoderHandler(EncoderDirection direction);
    static void volumeEncoderHandler(EncoderDirection direction);
    static void volumeEncoderSwitchHandler(std::uint32_t nowUs);
    static void volumeEncoderSwitchReleaseHandler(std::uint32_t nowUs);

    static constexpr EncoderSettings TEMPO_ENCODER_SETTINGS{
        .pinA = 16,
        .pinB = 17,
        .pinSwitch = 18,
        .handler = tempoEncoderHandler,
        .switchHandler = tempoEncoderSwitchHandler,
        .switchReleaseHandler = tempoEncoderSwitchReleaseHandler,
    };
    static constexpr EncoderSettings SWING_ENCODER_SETTINGS{
        .pinA = 19,
        .pinB = 20,
        .pinSwitch = 21,
        .handler = swingEncoderHandler,
    };
    static constexpr EncoderSettings VOLUME_ENCODER_SETTINGS{
        .pinA = 22,
        .pinB = 26,
        .pinSwitch = 27,
        .handler = volumeEncoderHandler,
        .switchHandler = volumeEncoderSwitchHandler,
        .switchReleaseHandler = volumeEncoderSwitchReleaseHandler,
    };

    void handleEncoderDirection(const ContextInput::EncoderInputAdapter<InputId>& adapter,
                                EncoderDirection direction);
    void handleButtonBatch(const StepButtonInputs::Batch& batch, std::uint32_t nowUs);
    void pollMatrixInputs(std::uint32_t nowMs);
    void pollEncoderInputs(std::uint32_t nowUs);
    void handleProgramStorageEvent(const AppEvent& event, std::uint32_t nowUs);
    void syncInternalAlarm();

    static Application* instance_;

    Adafruit_USBD_MIDI usbMidi_;
    UsbMidiRealtimeReceiver midiClockReceiver_{usbMidi_};
#if defined(SWING_METRO_STAGE5_FAULT_SCENARIOS)
    UsbMidiMessageSink normalMidiSink_{usbMidi_};
    FaultMidiMessageSink midiSink_{normalMidiSink_};
#else
    UsbMidiMessageSink midiSink_{usbMidi_};
#endif
    Session session_{midiSink_};
    InternalTickSource internalTicks_;
    PicoInternalTickAlarm internalTickAlarm_{internalTicks_};
    LittleFsProgramStorage programStorageBackend_;
    ProgramSlotStore programSlotStore_{programStorageBackend_};
    ProgramBank programBank_;
    ProgramDraft programDraft_;

    ContextInput::EncoderInputAdapter<InputId> tempoInput_{InputId::TempoEncoder};
    ContextInput::EncoderInputAdapter<InputId> swingInput_{InputId::SwingEncoder};
    ContextInput::EncoderInputAdapter<InputId> volumeInput_{InputId::VolumeEncoder};
    ButtonMatrix<4, 4, PadButtonIds> buttonMatrix_{INPUT_PINS, OUTPUT_PINS};
    StepButtonInputs stepButtonInputs_;
    PeriodicScheduler<MATRIX_SCAN_PERIOD_MS> matrixScanScheduler_;
    PeriodicScheduler<ENCODER_SAMPLE_PERIOD_US> encoderSampleScheduler_;
    EncoderSampleDiagnostics encoderSampleDiagnostics_;
    RuntimeTimingDiagnostics runtimeTimingDiagnostics_;
    Encoder tempoEncoder_{TEMPO_ENCODER_SETTINGS};
    Encoder swingEncoder_{SWING_ENCODER_SETTINGS};
    Counter<uint8_t> swingCounter_{{.step = 1,
                                    .value = 50,
                                    .minValue = 50,
                                    .maxValue = SWING_MAX_VALUE,
                                    .overflowBehavior = CounterOverflowBehavior::Clamp}};
    Encoder volumeEncoder_{VOLUME_ENCODER_SETTINGS};
    Counter<uint8_t> volumeCounter_{{.step = 1,
                                     .value = 100,
                                     .minValue = 0,
                                     .maxValue = 100,
                                     .overflowBehavior = CounterOverflowBehavior::Clamp}};
    AppEventHandler appEventHandler_{{.tempo = session_.tempo(),
                                      .swing = swingCounter_,
                                      .volume = volumeCounter_,
                                      .sequencer = session_.playback().sequencer()}};
    ProgramStorageController programStorageController_{
        programSlotStore_, session_, programBank_, programDraft_, swingCounter_, volumeCounter_};
    AppInputCoordinator<INPUT_CONTEXT_CAPACITY> appInputCoordinator_{
        appEventHandler_, session_.playback().sequencer(), session_.midiClock(),
        &session_.transport(), &programStorageController_};
    ContextInput::ButtonInputAdapter<InputId> tempoSwitchInput_{{InputId::TempoSwitch, 500'000}};
    ContextInput::ButtonInputAdapter<InputId> volumeSwitchInput_{{InputId::VolumeEncoder, 500'000}};
    UiViewModel uiViewModel_;
    bool internalAlarmActive_ = false;
    uint8_t internalAlarmBpm_ = 0;
    ArduinoDiagnosticConsole diagnosticConsole_;
    DiagnosticsSerializer diagnosticsSerializer_{diagnosticConsole_};
    DiagnosticsCapture diagnosticsCapture_{session_.transport(), internalTicks_,
                                           encoderSampleDiagnostics_, runtimeTimingDiagnostics_,
                                           diagnosticsSerializer_};
    SerialRunController serialRunController_{diagnosticConsole_,
                                             session_.playback().sequencer(),
                                             session_.midiClock(),
                                             session_.transport(),
                                             session_.tempo(),
                                             swingCounter_,
                                             runtimeTimingDiagnostics_,
                                             encoderSampleDiagnostics_,
                                             diagnosticsCapture_
#if defined(SWING_METRO_STAGE5_FAULT_SCENARIOS)
                                             ,
                                             &midiSink_
#endif
    };
    LvglUi uiProvider_{runtimeTimingDiagnostics_};
};

} // namespace SwingMetro
