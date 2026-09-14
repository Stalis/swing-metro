#pragma once

#include "app_contexts.h"
#include "app_event_handler.h"
#include "components/ui_view_model.h"
#include "engine/transport_controller.h"
#include "main_display_context.h"
#include "program/program_storage_controller.h"

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace SwingMetro {

template <std::size_t Capacity>
class AppInputCoordinator {
    static_assert(Capacity >= 2, "AppInputCoordinator needs global and main contexts");

  public:
    AppInputCoordinator(AppEventHandler& handler, Sequencer& sequencer,
                        MidiClockSettings& midiClock, TransportController* transport = nullptr,
                        ProgramStorageController* programStorage = nullptr)
        : handler_{handler}, sequencer_{sequencer}, midiClock_{midiClock}, transport_{transport},
          programStorage_{programStorage}, shiftContext_{selectedStep_} {
        (void)router_.addContext(globalContext_);
        (void)router_.addContext(mainContext_);
    }

    auto dispatch(const InputEvent& input, std::uint32_t nowUs) -> std::optional<AppEvent> {
        const auto source = static_cast<std::uint8_t>(input.source);
        const auto* button = std::get_if<ContextInput::ButtonInput>(&input.payload);
        if (button != nullptr && capturedButtons_.test(source)) {
            if (button->phase == ContextInput::ButtonPhase::Released) {
                capturedButtons_.reset(source);
            }
            return std::nullopt;
        }

        const auto result = router_.dispatch(input);
        if (!result.hasEvent()) {
            return std::nullopt;
        }

        const auto& event = result.event();
        handleAppEvent(event, &input, nowUs);
        return event;
    }

    auto handleAppEvent(const AppEvent& event, const InputEvent* sourceInput, std::uint32_t nowUs)
        -> void {
        bool contextChanged = false;
        if (const auto* open = std::get_if<OpenStepSettings>(&event)) {
            contextChanged = openStepSettings(open->step);
        } else if (std::holds_alternative<CloseStepSettings>(event)) {
            contextChanged = closeStepSettings();
        } else if (const auto* adjust = std::get_if<AdjustNote>(&event)) {
            if (selectedStep_.has_value()) {
                (void)sequencer_.adjustStepNote(*selectedStep_, adjust->delta);
            }
        } else if (const auto* adjust = std::get_if<AdjustVelocity>(&event)) {
            if (selectedStep_.has_value()) {
                (void)sequencer_.adjustStepVelocity(*selectedStep_, adjust->delta);
            }
        } else if (std::holds_alternative<ToggleTransport>(event)) {
            if (transport_ != nullptr) {
                transport_->toggle(nowUs);
            } else if (midiClock_.mode() != MidiClockMode::External) {
                sequencer_.toggleRunning(nowUs);
            }
        } else if (std::holds_alternative<ActivateShift>(event)) {
            (void)router_.addContext(shiftContext_);
        } else if (std::holds_alternative<DeactivateShift>(event)) {
            (void)router_.releaseContext(shiftContext_);
        } else if (std::holds_alternative<OpenMidiClockSettings>(event)) {
            contextChanged = openMidiClockSettings();
        } else if (const auto* adjust = std::get_if<AdjustMidiClockPreview>(&event)) {
            adjustMidiClockPreview(adjust->delta);
        } else if (std::holds_alternative<ConfirmMidiClockSettings>(event)) {
            if (midiClockModalOpen_) {
                handleAppEvent(AppEvent{ApplyMidiClockMode{midiClockPreview_}}, nullptr, nowUs);
                contextChanged = closeMidiClockSettings();
            }
        } else if (const auto* apply = std::get_if<ApplyMidiClockMode>(&event)) {
            if (transport_ != nullptr) {
                transport_->applyMode(apply->mode);
            } else {
                midiClock_.apply(apply->mode);
            }
        } else if (std::holds_alternative<OpenProgramStorage>(event)) {
            contextChanged = openProgramStorage();
        } else if (const auto* select = std::get_if<SelectProgramStorageAction>(&event)) {
            selectProgramStorageAction(select->delta);
        } else if (std::holds_alternative<ConfirmProgramStorageAction>(event)) {
            if (_programStorageState == ProgramStorageModalState::Action) {
                _programStorageState = ProgramStorageModalState::Slot;
                programStorageContext_.setState(_programStorageState);
            }
        } else if (const auto* select = std::get_if<SelectProgramStorageSlot>(&event)) {
            selectProgramStorageSlot(select->delta);
        } else if (std::holds_alternative<ConfirmProgramStorageSlot>(event)) {
            if (_programStorageState == ProgramStorageModalState::Slot) {
                _programStorageState = ProgramStorageModalState::Busy;
                programStorageContext_.setState(_programStorageState);
                _programStoragePending = true;
            }
        } else if (std::holds_alternative<CloseProgramStorage>(event)) {
            contextChanged = closeProgramStorage();
        } else if (!std::holds_alternative<AdjustTempo>(event) ||
                   midiClock_.mode() != MidiClockMode::External) {
            handler_.handle(event);
        }

        if (contextChanged && sourceInput != nullptr) {
            const auto* button = std::get_if<ContextInput::ButtonInput>(&sourceInput->payload);
            if (button != nullptr && button->phase != ContextInput::ButtonPhase::Released) {
                capturedButtons_.set(static_cast<std::uint8_t>(sourceInput->source));
            }
        }
    }

    [[nodiscard]] auto selectedStep() const noexcept -> std::optional<std::uint8_t> {
        return selectedStep_;
    }
    [[nodiscard]] auto isShiftActive() const noexcept -> bool {
        return router_.contains(shiftContext_);
    }
    [[nodiscard]] auto hasStepSettingsContext() const noexcept -> bool {
        return router_.contains(stepContext_);
    }
    [[nodiscard]] auto stackSize() const noexcept -> std::size_t { return router_.size(); }
    [[nodiscard]] auto midiClockMode() const noexcept -> MidiClockMode { return midiClock_.mode(); }
    [[nodiscard]] auto isMidiClockModalOpen() const noexcept -> bool { return midiClockModalOpen_; }
    [[nodiscard]] auto midiClockPreviewMode() const noexcept -> MidiClockMode {
        return midiClockPreview_;
    }
    [[nodiscard]] auto isProgramStorageModalOpen() const noexcept -> bool {
        return _programStorageState != ProgramStorageModalState::Closed;
    }

    auto processProgramStorage() -> void {
        if (!_programStoragePending) {
            return;
        }
        _programStoragePending = false;
        _programStorageStatus =
            programStorage_ == nullptr
                ? ProgramStoreStatus::NotMounted
                : programStorage_->perform(_programStorageAction, _programStorageSlot);
        _programStorageState = _programStorageStatus == ProgramStoreStatus::Ok
                                   ? ProgramStorageModalState::Success
                                   : ProgramStorageModalState::Error;
        programStorageContext_.setState(_programStorageState);
    }

    [[nodiscard]] auto decorateUiSettings(UiSettings settings) const -> UiSettings {
        settings.page = selectedStep_.has_value() ? UiPage::StepSettings : UiPage::MainDisplay;
        settings.selectedStep = selectedStep_.value_or(UINT8_MAX);
        settings.selectedNote = selectedStep_.has_value()
                                    ? sequencer_.getStepMidiNote(*selectedStep_).value_or(36)
                                    : 36;
        settings.selectedVelocity = selectedStep_.has_value()
                                        ? sequencer_.getStepVelocity(*selectedStep_).value_or(127)
                                        : 127;
        settings.transportRunning = sequencer_.isRunning();
        settings.shiftActive = isShiftActive();
        settings.midiClockModalOpen = midiClockModalOpen_;
        settings.midiClockActive = midiClock_.mode();
        settings.midiClockPreview = midiClockPreview_;
        settings.programStorageState = _programStorageState;
        settings.programStorageAction = _programStorageAction;
        settings.programStorageSlot = _programStorageSlot;
        settings.programStorageStatus = _programStorageStatus;
        return settings;
    }

  private:
    [[nodiscard]] auto openStepSettings(std::uint8_t step) -> bool {
        if (step >= STEPS_COUNT) {
            return false;
        }

        if (selectedStep_.has_value()) {
            if (*selectedStep_ == step) {
                return false;
            }
            selectedStep_ = step;
            stepContext_.setSelectedStep(step);
            return true;
        }

        if (router_.size() == router_.capacity()) {
            return false;
        }

        const bool shiftWasActive = isShiftActive();
        if (shiftWasActive) {
            (void)router_.releaseContext(shiftContext_);
        }
        const auto added = router_.addContext(stepContext_);
        if (added != ContextInput::AddContextResult::Added) {
            if (shiftWasActive) {
                (void)router_.addContext(shiftContext_);
            }
            return false;
        }
        if (shiftWasActive) {
            (void)router_.addContext(shiftContext_);
        }
        selectedStep_ = step;
        stepContext_.setSelectedStep(step);
        return true;
    }

    [[nodiscard]] auto closeStepSettings() -> bool {
        if (!selectedStep_.has_value()) {
            return false;
        }
        if (router_.releaseContext(stepContext_) != ContextInput::ReleaseContextResult::Released) {
            return false;
        }
        selectedStep_.reset();
        return true;
    }

    [[nodiscard]] auto openMidiClockSettings() -> bool {
        if (midiClockModalOpen_) {
            return false;
        }

        const bool shiftWasActive = isShiftActive();
        if (shiftWasActive) {
            (void)router_.releaseContext(shiftContext_);
        } else if (router_.size() == router_.capacity()) {
            return false;
        }

        if (router_.addContext(midiClockContext_) != ContextInput::AddContextResult::Added) {
            if (shiftWasActive) {
                (void)router_.addContext(shiftContext_);
            }
            return false;
        }

        midiClockModalOpen_ = true;
        midiClockPreview_ = midiClock_.mode();
        restoreShiftAfterMidiClockModal_ = shiftWasActive;
        return true;
    }

    [[nodiscard]] auto closeMidiClockSettings() -> bool {
        if (router_.releaseContext(midiClockContext_) !=
            ContextInput::ReleaseContextResult::Released) {
            return false;
        }

        midiClockModalOpen_ = false;
        if (restoreShiftAfterMidiClockModal_) {
            (void)router_.addContext(shiftContext_);
        }
        restoreShiftAfterMidiClockModal_ = false;
        return true;
    }

    auto adjustMidiClockPreview(std::int8_t delta) noexcept -> void {
        if (!midiClockModalOpen_) {
            return;
        }

        const auto candidate = static_cast<std::int16_t>(midiClockPreview_) + delta;
        if (candidate <= static_cast<std::int16_t>(MidiClockMode::Off)) {
            midiClockPreview_ = MidiClockMode::Off;
        } else if (candidate >= static_cast<std::int16_t>(MidiClockMode::External)) {
            midiClockPreview_ = MidiClockMode::External;
        } else {
            midiClockPreview_ = static_cast<MidiClockMode>(candidate);
        }
    }

    [[nodiscard]] auto openProgramStorage() -> bool {
        if (isProgramStorageModalOpen() || router_.size() == router_.capacity()) {
            return false;
        }
        (void)router_.releaseContext(shiftContext_);
        if (router_.addContext(programStorageContext_) != ContextInput::AddContextResult::Added) {
            return false;
        }
        _programStorageState = ProgramStorageModalState::Action;
        _programStorageAction = ProgramStorageAction::Save;
        _programStorageSlot = 0;
        _programStorageStatus = ProgramStoreStatus::Ok;
        programStorageContext_.setState(_programStorageState);
        return true;
    }

    auto closeProgramStorage() -> bool {
        if (!isProgramStorageModalOpen() ||
            _programStorageState == ProgramStorageModalState::Busy ||
            router_.releaseContext(programStorageContext_) !=
                ContextInput::ReleaseContextResult::Released) {
            return false;
        }
        _programStorageState = ProgramStorageModalState::Closed;
        return true;
    }

    auto selectProgramStorageAction(std::int8_t delta) noexcept -> void {
        if (_programStorageState == ProgramStorageModalState::Action && delta != 0) {
            _programStorageAction = _programStorageAction == ProgramStorageAction::Save
                                        ? ProgramStorageAction::Load
                                        : ProgramStorageAction::Save;
        }
    }

    auto selectProgramStorageSlot(std::int8_t delta) noexcept -> void {
        if (_programStorageState != ProgramStorageModalState::Slot || delta == 0) {
            return;
        }
        const auto slot = static_cast<int>(_programStorageSlot) + delta;
        _programStorageSlot = static_cast<std::uint8_t>(
            std::clamp(slot, 0, static_cast<int>(PROGRAM_USER_SLOT_COUNT - 1)));
    }

    AppEventHandler& handler_;
    Sequencer& sequencer_;
    MidiClockSettings& midiClock_;
    TransportController* transport_;
    ProgramStorageController* programStorage_;
    std::optional<std::uint8_t> selectedStep_;
    GlobalContext globalContext_;
    MainDisplayContext mainContext_;
    StepSettingsContext stepContext_;
    ShiftContext shiftContext_;
    MidiClockSettingsContext midiClockContext_;
    ProgramStorageContext programStorageContext_;
    ContextInput::Router<InputEvent, AppEvent, Capacity> router_;
    std::bitset<256> capturedButtons_;
    MidiClockMode midiClockPreview_ = MidiClockMode::Off;
    bool midiClockModalOpen_ = false;
    bool restoreShiftAfterMidiClockModal_ = false;
    ProgramStorageModalState _programStorageState = ProgramStorageModalState::Closed;
    ProgramStorageAction _programStorageAction = ProgramStorageAction::Save;
    std::uint8_t _programStorageSlot = 0;
    ProgramStoreStatus _programStorageStatus = ProgramStoreStatus::Ok;
    bool _programStoragePending = false;
};

} // namespace SwingMetro
