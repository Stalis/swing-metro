#include "application.h"

#include <tuple>
#include <variant>

namespace SwingMetro {

Application* Application::instance_ = nullptr;

Application::Application() { instance_ = this; }

void Application::handleEncoderDirection(const ContextInput::EncoderInputAdapter<InputId>& adapter,
                                         EncoderDirection direction) {
    const auto input = adapter.translate(direction);
    if (!input.has_value()) {
        return;
    }

    appInputCoordinator_.dispatch(*input, micros());
}

void Application::handleButtonBatch(const StepButtonInputs::Batch& batch, std::uint32_t nowUs) {
    for (std::size_t index = 0; index < batch.size(); ++index) {
        const auto& input = batch[index];
        if (const auto event = appInputCoordinator_.dispatch(input, nowUs); event.has_value()) {
            handleProgramStorageEvent(*event, nowUs);
        }
    }
}

void Application::pollMatrixInputs(std::uint32_t nowMs) {
    const auto nowUs = nowMs * 1'000U;
    buttonMatrix_.readButtons(nowMs);

    for (int physicalIndex = 0; physicalIndex < STEPS_COUNT; ++physicalIndex) {
        const auto step = buttonMatrix_.getButtonId(physicalIndex);
        if (buttonMatrix_.isButtonJustPressed(physicalIndex)) {
            handleButtonBatch(stepButtonInputs_.onPressed(step, nowMs), nowUs);
        } else if (buttonMatrix_.isButtonJustReleased(physicalIndex)) {
            handleButtonBatch(stepButtonInputs_.onReleased(step, nowMs), nowUs);
        } else if (buttonMatrix_.isButtonHolding(physicalIndex)) {
            handleButtonBatch(stepButtonInputs_.update(step, nowMs), nowUs);
        }
    }

    buttonMatrix_.update();
}

void Application::pollEncoderInputs(std::uint32_t nowUs) {
    std::apply([nowUs](auto&... objects) { (objects.update(nowUs), ...); },
               std::tie(tempoEncoder_, swingEncoder_, volumeEncoder_));
    handleButtonBatch(tempoSwitchInput_.update(nowUs), nowUs);
    handleButtonBatch(volumeSwitchInput_.update(nowUs), nowUs);
}

void Application::tempoEncoderHandler(EncoderDirection direction) {
    instance_->handleEncoderDirection(instance_->tempoInput_, direction);
}

void Application::swingEncoderHandler(EncoderDirection direction) {
    instance_->handleEncoderDirection(instance_->swingInput_, direction);
}

void Application::volumeEncoderHandler(EncoderDirection direction) {
    instance_->handleEncoderDirection(instance_->volumeInput_, direction);
}

void Application::tempoEncoderSwitchHandler(std::uint32_t nowUs) {
    instance_->handleButtonBatch(instance_->tempoSwitchInput_.onPressed(nowUs), nowUs);
}

void Application::tempoEncoderSwitchReleaseHandler(std::uint32_t nowUs) {
    instance_->handleButtonBatch(instance_->tempoSwitchInput_.onReleased(nowUs), nowUs);
}

void Application::volumeEncoderSwitchHandler(std::uint32_t nowUs) {
    instance_->handleButtonBatch(instance_->volumeSwitchInput_.onPressed(nowUs), nowUs);
}

void Application::volumeEncoderSwitchReleaseHandler(std::uint32_t nowUs) {
    instance_->handleButtonBatch(instance_->volumeSwitchInput_.onReleased(nowUs), nowUs);
}

void Application::handleProgramStorageEvent(const AppEvent& event, std::uint32_t nowUs) {
    if (std::holds_alternative<OpenProgramStorage>(event) &&
        appInputCoordinator_.isProgramStorageModalOpen()) {
        session_.transport().openStorage(nowUs);
    } else if (std::holds_alternative<CloseProgramStorage>(event)) {
        session_.transport().closeStorage();
    }
}

void Application::syncInternalAlarm() {
    const bool shouldRun = session_.transport().usesInternalTiming();
    if (shouldRun && !internalAlarmActive_) {
        diagnosticsCapture_.beginWindow();
        internalTickAlarm_.start(session_.playback().sequencer().getBpm(),
                                 session_.transport().internalTickDiscardReason());
        internalAlarmActive_ = true;
        internalAlarmBpm_ = session_.playback().sequencer().getBpm();
    } else if (!shouldRun && internalAlarmActive_) {
        internalTickAlarm_.stop(session_.transport().internalTickDiscardReason());
        internalAlarmActive_ = false;
        diagnosticsCapture_.requestExport();
    } else if (shouldRun && internalAlarmBpm_ != session_.playback().sequencer().getBpm()) {
        internalTickAlarm_.setBpm(session_.playback().sequencer().getBpm());
        internalAlarmBpm_ = session_.playback().sequencer().getBpm();
    }
}

void Application::setup() {
    Serial.begin(115200);

    if (!TinyUSBDevice.isInitialized()) {
        TinyUSBDevice.begin(0);
    }

    usbMidi_.setStringDescriptor("Swing Metro MIDI");
    usbMidi_.begin();

    if (TinyUSBDevice.mounted()) {
        TinyUSBDevice.detach();
        delay(10);
        TinyUSBDevice.attach();
    }

    buttonMatrix_.init();

    tempoEncoder_.init();
    swingEncoder_.init();
    volumeEncoder_.init();
    session_.playback().sequencer().stop();
    (void)programSlotStore_.mount();
    (void)programStorageController_.restoreCurrentProgram();
    uiViewModel_.publish(
        appInputCoordinator_.decorateUiSettings({.main = {.tempo = session_.tempo().getValue(),
                                                          .swing = swingCounter_.getValue(),
                                                          .volume = volumeCounter_.getValue()}}));

    session_.playback().sequencer().sync(micros());
    matrixScanScheduler_.start(millis());
    encoderSampleScheduler_.start(micros());
}

void Application::loop() {
    serialRunController_.pollSerialRunCommand();
    midiClockReceiver_.poll([&](const MidiRealtimeEvent& event) {
        session_.transport().handleExternal(event, micros());
    });
    const auto processStartedAtUs = micros();
    session_.transport().process(processStartedAtUs, internalTicks_.ticks());
    session_.transport().recordProcessDuration(processStartedAtUs, micros());

    const auto matrixNowMs = millis();
    if (matrixScanScheduler_.poll(matrixNowMs)) {
        pollMatrixInputs(matrixNowMs);
    }

    const auto encoderNowUs = micros();
    if (encoderSampleScheduler_.poll(encoderNowUs)) {
        encoderSampleDiagnostics_.recordSample(encoderNowUs);
        pollEncoderInputs(encoderNowUs);
    }

    serialRunController_.updateSerialRun();
    syncInternalAlarm();
    diagnosticsCapture_.exportIfReady();
    serialRunController_.completeIfReady(internalAlarmActive_);

    UiSettings settings{
        .main = {.tempo = session_.tempo().getValue(),
                 .swing = swingCounter_.getValue(),
                 .volume = volumeCounter_.getValue(),
                 .activeNote =
                     session_.playback().sequencer().getDisplayStepIndex().value_or(UINT8_MAX),
                 .notesState = session_.playback().sequencer().getStepsEnabled()}};
    settings.main.externalClockStatus = session_.transport().externalStatus();
    settings.main.externalTempo = session_.transport().externalBpm();
    const auto& steps = session_.playback().sequencer().steps();
    for (std::size_t index = 0; index < steps.size(); ++index) {
        settings.main.stepNotes[index] = steps[index].note;
        settings.main.stepVelocities[index] = steps[index].velocity;
        settings.main.stepGates[index] = steps[index].gate;
    }
    uiViewModel_.publish(appInputCoordinator_.decorateUiSettings(settings));
    appInputCoordinator_.processProgramStorage();
    if (!appInputCoordinator_.hasStepSettingsContext() &&
        !appInputCoordinator_.isMidiClockModalOpen() &&
        !appInputCoordinator_.isProgramStorageModalOpen()) {
        (void)programStorageController_.syncCurrentProgramIfChanged();
    }
}

void Application::setup1() { uiProvider_.setup(); }

void Application::loop1() {
    uiProvider_.loop();
    uiProvider_.readViewModel(uiViewModel_);
}

} // namespace SwingMetro
