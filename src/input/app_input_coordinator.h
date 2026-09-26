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
        : _handler{handler}, _sequencer{sequencer}, _midiClock{midiClock}, _transport{transport},
          _programStorage{programStorage}, _shiftContext{_selectedStep} {
        (void)_router.addContext(_globalContext);
        (void)_router.addContext(_mainContext);
    }

    auto dispatch(const InputEvent& input, std::uint32_t nowUs) -> std::optional<AppEvent> {
        const auto source = static_cast<std::uint8_t>(input.source);
        const auto* button = std::get_if<ContextInput::ButtonInput>(&input.payload);
        if (button != nullptr && _capturedButtons.test(source)) {
            if (button->phase == ContextInput::ButtonPhase::Released) {
                _capturedButtons.reset(source);
            }
            return std::nullopt;
        }

        const auto result = _router.dispatch(input);
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
            if (_selectedStep.has_value()) {
                (void)_sequencer.adjustStepNote(*_selectedStep, adjust->delta);
            }
        } else if (const auto* adjust = std::get_if<AdjustVelocity>(&event)) {
            if (_selectedStep.has_value()) {
                (void)_sequencer.adjustStepVelocity(*_selectedStep, adjust->delta);
            }
        } else if (const auto* adjust = std::get_if<AdjustGate>(&event)) {
            if (_selectedStep.has_value()) {
                (void)_sequencer.adjustStepGate(*_selectedStep, adjust->delta);
            }
        } else if (std::holds_alternative<ToggleTransport>(event)) {
            if (_transport != nullptr) {
                _transport->toggle(nowUs);
            } else if (_midiClock.mode() != MidiClockMode::External) {
                _sequencer.toggleRunning(nowUs);
            }
        } else if (std::holds_alternative<ActivateShift>(event)) {
            (void)_router.addContext(_shiftContext);
        } else if (std::holds_alternative<DeactivateShift>(event)) {
            (void)_router.releaseContext(_shiftContext);
        } else if (std::holds_alternative<OpenMidiClockSettings>(event)) {
            contextChanged = openMidiClockSettings();
        } else if (const auto* adjust = std::get_if<AdjustMidiClockPreview>(&event)) {
            adjustMidiClockPreview(adjust->delta);
        } else if (std::holds_alternative<ConfirmMidiClockSettings>(event)) {
            if (_midiClockModalOpen) {
                if (_midiClockSelection != MidiClockMenuItem::Cancel) {
                    handleAppEvent(AppEvent{ApplyMidiClockMode{
                                       static_cast<MidiClockMode>(_midiClockSelection)}},
                                   nullptr, nowUs);
                }
                contextChanged = closeMidiClockSettings();
            }
        } else if (const auto* apply = std::get_if<ApplyMidiClockMode>(&event)) {
            if (_transport != nullptr) {
                _transport->applyMode(apply->mode, nowUs);
            } else {
                _midiClock.apply(apply->mode);
            }
        } else if (std::holds_alternative<OpenProgramStorage>(event)) {
            contextChanged = openProgramStorage();
        } else if (const auto* select = std::get_if<SelectProgramStorageAction>(&event)) {
            selectProgramStorageAction(select->delta);
        } else if (std::holds_alternative<ConfirmProgramStorageAction>(event)) {
            if (_programStorageState == ProgramStorageModalState::Action) {
                if (_programStorageSelection == ProgramStorageMenuItem::Cancel) {
                    contextChanged = closeProgramStorage();
                } else if (_programStorageSelection == ProgramStorageMenuItem::ResetProgram) {
                    _programResetChoice = ProgramResetChoice::No;
                    _programStorageState = ProgramStorageModalState::ResetConfirmation;
                    _programStorageContext.setState(_programStorageState);
                } else {
                    _programStorageAction = _programStorageSelection == ProgramStorageMenuItem::Save
                                                ? ProgramStorageAction::Save
                                                : ProgramStorageAction::Load;
                    _programStorageState = ProgramStorageModalState::Slot;
                    _programStorageSlot = 0;
                    _programStorageContext.setState(_programStorageState);
                }
            }
        } else if (const auto* select = std::get_if<SelectProgramStorageSlot>(&event)) {
            selectProgramStorageSlot(select->delta);
        } else if (std::holds_alternative<ConfirmProgramStorageSlot>(event)) {
            if (_programStorageState == ProgramStorageModalState::Slot) {
                if (_programStorageSlot == PROGRAM_STORAGE_CANCEL_SLOT) {
                    contextChanged = closeProgramStorage();
                } else {
                    _programStorageState = ProgramStorageModalState::Busy;
                    _programStorageContext.setState(_programStorageState);
                    _programStoragePending = true;
                }
            }
        } else if (const auto* select = std::get_if<SelectProgramResetChoice>(&event)) {
            selectProgramResetChoice(select->delta);
        } else if (std::holds_alternative<ConfirmProgramReset>(event)) {
            if (_programStorageState == ProgramStorageModalState::ResetConfirmation) {
                if (_programResetChoice == ProgramResetChoice::No) {
                    _programStorageState = ProgramStorageModalState::Action;
                } else {
                    _programStorageState = ProgramStorageModalState::Busy;
                    _programStorageResetPending = true;
                    _programStoragePending = true;
                }
                _programStorageContext.setState(_programStorageState);
            }
        } else if (std::holds_alternative<CloseProgramStorage>(event)) {
            contextChanged = closeProgramStorage();
        } else if (!std::holds_alternative<AdjustTempo>(event) ||
                   _midiClock.mode() != MidiClockMode::External) {
            _handler.handle(event);
        }

        if (contextChanged && sourceInput != nullptr) {
            const auto* button = std::get_if<ContextInput::ButtonInput>(&sourceInput->payload);
            if (button != nullptr && button->phase != ContextInput::ButtonPhase::Released) {
                _capturedButtons.set(static_cast<std::uint8_t>(sourceInput->source));
            }
        }
    }

    [[nodiscard]] auto selectedStep() const noexcept -> std::optional<std::uint8_t> {
        return _selectedStep;
    }
    [[nodiscard]] auto isShiftActive() const noexcept -> bool {
        return _router.contains(_shiftContext);
    }
    [[nodiscard]] auto hasStepSettingsContext() const noexcept -> bool {
        return _router.contains(_stepContext);
    }
    [[nodiscard]] auto stackSize() const noexcept -> std::size_t { return _router.size(); }
    [[nodiscard]] auto midiClockMode() const noexcept -> MidiClockMode { return _midiClock.mode(); }
    [[nodiscard]] auto isMidiClockModalOpen() const noexcept -> bool { return _midiClockModalOpen; }
    [[nodiscard]] auto midiClockSelection() const noexcept -> MidiClockMenuItem {
        return _midiClockSelection;
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
            _programStorage == nullptr ? ProgramStoreStatus::NotMounted
            : _programStorageResetPending
                ? _programStorage->resetCurrentProgram()
                : _programStorage->perform(_programStorageAction, _programStorageSlot);
        _programStorageResetPending = false;
        _programStorageState = _programStorageStatus == ProgramStoreStatus::Ok
                                   ? ProgramStorageModalState::Success
                                   : ProgramStorageModalState::Error;
        _programStorageContext.setState(_programStorageState);
    }

    [[nodiscard]] auto decorateUiSettings(UiSettings settings) const -> UiSettings {
        settings.page = currentPage();
        settings.selectedStep = _selectedStep.value_or(UINT8_MAX);
        settings.selectedNote = _selectedStep.has_value()
                                    ? _sequencer.getStepMidiNote(*_selectedStep).value_or(36)
                                    : 36;
        settings.selectedVelocity = _selectedStep.has_value()
                                        ? _sequencer.getStepVelocity(*_selectedStep).value_or(127)
                                        : 127;
        settings.selectedGate =
            _selectedStep.has_value()
                ? _sequencer.getStepGate(*_selectedStep).value_or(STEP_DEFAULT_GATE)
                : STEP_DEFAULT_GATE;
        settings.transportRunning = _sequencer.isRunning();
        settings.shiftActive = isShiftActive();
        settings.midiClockModalOpen = _midiClockModalOpen;
        settings.midiClockActive = _midiClock.mode();
        settings.midiClockSelection = _midiClockSelection;
        settings.programStorageState = _programStorageState;
        settings.programStorageSelection = _programStorageSelection;
        settings.programStorageAction = _programStorageAction;
        settings.programStorageSlot = _programStorageSlot;
        settings.programStorageStatus = _programStorageStatus;
        settings.programResetChoice = _programResetChoice;
        return settings;
    }

  private:
    enum class ModalId : std::uint8_t {
        MidiClockSettings,
        ProgramStorage,
    };

    [[nodiscard]] auto currentPage() const noexcept -> UiPage {
        return _selectedStep.has_value() ? UiPage::StepSettings : UiPage::MainDisplay;
    }

    [[nodiscard]] static constexpr auto modalHostPage(ModalId modal) noexcept -> UiPage {
        switch (modal) {
        case ModalId::MidiClockSettings:
        case ModalId::ProgramStorage:
            return UiPage::MainDisplay;
        }
        return UiPage::MainDisplay;
    }

    [[nodiscard]] auto hasOpenModal() const noexcept -> bool {
        return _midiClockModalOpen || isProgramStorageModalOpen();
    }

    [[nodiscard]] auto canOpenModal(ModalId modal) const noexcept -> bool {
        return !hasOpenModal() && currentPage() == modalHostPage(modal);
    }

    [[nodiscard]] auto openStepSettings(std::uint8_t step) -> bool {
        if (step >= STEPS_COUNT) {
            return false;
        }

        if (_selectedStep.has_value()) {
            if (*_selectedStep == step) {
                return false;
            }
            _selectedStep = step;
            _stepContext.setSelectedStep(step);
            return true;
        }

        if (_router.size() == _router.capacity()) {
            return false;
        }

        const bool shiftWasActive = isShiftActive();
        if (shiftWasActive) {
            (void)_router.releaseContext(_shiftContext);
        }
        const auto added = _router.addContext(_stepContext);
        if (added != ContextInput::AddContextResult::Added) {
            if (shiftWasActive) {
                (void)_router.addContext(_shiftContext);
            }
            return false;
        }
        if (shiftWasActive) {
            (void)_router.addContext(_shiftContext);
        }
        _selectedStep = step;
        _stepContext.setSelectedStep(step);
        return true;
    }

    [[nodiscard]] auto closeStepSettings() -> bool {
        if (!_selectedStep.has_value()) {
            return false;
        }
        if (_router.releaseContext(_stepContext) != ContextInput::ReleaseContextResult::Released) {
            return false;
        }
        _selectedStep.reset();
        return true;
    }

    [[nodiscard]] auto openMidiClockSettings() -> bool {
        if (!canOpenModal(ModalId::MidiClockSettings)) {
            return false;
        }

        const bool shiftWasActive = isShiftActive();
        if (shiftWasActive) {
            (void)_router.releaseContext(_shiftContext);
        } else if (_router.size() == _router.capacity()) {
            return false;
        }

        if (_router.addContext(_midiClockContext) != ContextInput::AddContextResult::Added) {
            if (shiftWasActive) {
                (void)_router.addContext(_shiftContext);
            }
            return false;
        }

        _midiClockModalOpen = true;
        _midiClockSelection = static_cast<MidiClockMenuItem>(_midiClock.mode());
        _restoreShiftAfterMidiClockModal = shiftWasActive;
        return true;
    }

    [[nodiscard]] auto closeMidiClockSettings() -> bool {
        if (_router.releaseContext(_midiClockContext) !=
            ContextInput::ReleaseContextResult::Released) {
            return false;
        }

        _midiClockModalOpen = false;
        if (_restoreShiftAfterMidiClockModal) {
            (void)_router.addContext(_shiftContext);
        }
        _restoreShiftAfterMidiClockModal = false;
        return true;
    }

    auto adjustMidiClockPreview(std::int8_t delta) noexcept -> void {
        if (!_midiClockModalOpen) {
            return;
        }

        const auto candidate = static_cast<std::int16_t>(_midiClockSelection) + delta;
        if (candidate <= static_cast<std::int16_t>(MidiClockMenuItem::Off)) {
            _midiClockSelection = MidiClockMenuItem::Off;
        } else if (candidate >= static_cast<std::int16_t>(MidiClockMenuItem::Cancel)) {
            _midiClockSelection = MidiClockMenuItem::Cancel;
        } else {
            _midiClockSelection = static_cast<MidiClockMenuItem>(candidate);
        }
    }

    [[nodiscard]] auto openProgramStorage() -> bool {
        if (!canOpenModal(ModalId::ProgramStorage) || _router.size() == _router.capacity()) {
            return false;
        }
        (void)_router.releaseContext(_shiftContext);
        if (_router.addContext(_programStorageContext) != ContextInput::AddContextResult::Added) {
            return false;
        }
        _programStorageState = ProgramStorageModalState::Action;
        _programStorageSelection = ProgramStorageMenuItem::Save;
        _programStorageAction = ProgramStorageAction::Save;
        _programStorageSlot = 0;
        _programStorageStatus = ProgramStoreStatus::Ok;
        _programResetChoice = ProgramResetChoice::No;
        _programStorageResetPending = false;
        _programStorageContext.setState(_programStorageState);
        return true;
    }

    auto closeProgramStorage() -> bool {
        if (!isProgramStorageModalOpen() ||
            _programStorageState == ProgramStorageModalState::Busy ||
            _router.releaseContext(_programStorageContext) !=
                ContextInput::ReleaseContextResult::Released) {
            return false;
        }
        _programStorageState = ProgramStorageModalState::Closed;
        return true;
    }

    auto selectProgramStorageAction(std::int8_t delta) noexcept -> void {
        if (_programStorageState == ProgramStorageModalState::Action && delta != 0) {
            const auto candidate = static_cast<int>(_programStorageSelection) + delta;
            _programStorageSelection = static_cast<ProgramStorageMenuItem>(
                std::clamp(candidate, static_cast<int>(ProgramStorageMenuItem::Save),
                           static_cast<int>(ProgramStorageMenuItem::ResetProgram)));
        }
    }

    auto selectProgramResetChoice(std::int8_t delta) noexcept -> void {
        if (_programStorageState != ProgramStorageModalState::ResetConfirmation || delta == 0) {
            return;
        }
        const auto candidate = static_cast<int>(_programResetChoice) + delta;
        _programResetChoice = static_cast<ProgramResetChoice>(
            std::clamp(candidate, static_cast<int>(ProgramResetChoice::No),
                       static_cast<int>(ProgramResetChoice::Yes)));
    }

    auto selectProgramStorageSlot(std::int8_t delta) noexcept -> void {
        if (_programStorageState != ProgramStorageModalState::Slot || delta == 0) {
            return;
        }
        const auto current = _programStorageSlot == PROGRAM_STORAGE_CANCEL_SLOT
                                 ? -1
                                 : static_cast<int>(_programStorageSlot);
        const auto selected =
            std::clamp(current + delta, -1, static_cast<int>(PROGRAM_USER_SLOT_COUNT - 1));
        _programStorageSlot =
            selected < 0 ? PROGRAM_STORAGE_CANCEL_SLOT : static_cast<std::uint8_t>(selected);
    }

    AppEventHandler& _handler;
    Sequencer& _sequencer;
    MidiClockSettings& _midiClock;
    TransportController* _transport;
    ProgramStorageController* _programStorage;
    std::optional<std::uint8_t> _selectedStep;
    GlobalContext _globalContext;
    MainDisplayContext _mainContext;
    StepSettingsContext _stepContext;
    ShiftContext _shiftContext;
    MidiClockSettingsContext _midiClockContext;
    ProgramStorageContext _programStorageContext;
    ContextInput::Router<InputEvent, AppEvent, Capacity> _router;
    std::bitset<256> _capturedButtons;
    MidiClockMenuItem _midiClockSelection = MidiClockMenuItem::Off;
    bool _midiClockModalOpen = false;
    bool _restoreShiftAfterMidiClockModal = false;
    ProgramStorageModalState _programStorageState = ProgramStorageModalState::Closed;
    ProgramStorageMenuItem _programStorageSelection = ProgramStorageMenuItem::Save;
    ProgramStorageAction _programStorageAction = ProgramStorageAction::Save;
    std::uint8_t _programStorageSlot = 0;
    ProgramStoreStatus _programStorageStatus = ProgramStoreStatus::Ok;
    ProgramResetChoice _programResetChoice = ProgramResetChoice::No;
    bool _programStoragePending = false;
    bool _programStorageResetPending = false;
};

} // namespace SwingMetro
