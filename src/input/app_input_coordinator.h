#pragma once

#include "app_event_handler.h"
#include "app_input_router.h"
#include "engine/transport_controller.h"
#include "midi_clock_modal.h"
#include "program/program_storage_controller.h"
#include "step_editor.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace SwingMetro {

template <std::size_t Capacity>
class AppInputCoordinator {
  public:
    AppInputCoordinator(AppEventHandler& handler, Sequencer& sequencer,
                        MidiClockSettings& midiClock, TransportController* transport = nullptr,
                        ProgramStorageController* programStorage = nullptr)
        : _handler{handler}, _sequencer{sequencer}, _stepEditor{sequencer}, _midiClock{midiClock},
          _transport{transport}, _programStorage{programStorage} {}

    auto dispatch(const InputEvent& input, std::uint32_t nowUs) -> std::optional<AppEvent> {
        const auto event = _inputRouter.dispatch(input);
        if (!event.has_value()) {
            return std::nullopt;
        }

        handleAppEvent(*event, &input, nowUs);
        return event;
    }

    auto handleAppEvent(const AppEvent& event, const InputEvent* sourceInput, std::uint32_t nowUs)
        -> void {
        bool contextChanged = false;
        if (const auto* open = std::get_if<OpenStepSettings>(&event)) {
            contextChanged = _inputRouter.openStepSettings(open->step);
        } else if (std::holds_alternative<CloseStepSettings>(event)) {
            contextChanged = _inputRouter.closeStepSettings();
        } else if (const auto* adjust = std::get_if<AdjustNote>(&event)) {
            _stepEditor.adjustNote(selectedStep(), adjust->delta);
        } else if (const auto* adjust = std::get_if<AdjustVelocity>(&event)) {
            _stepEditor.adjustVelocity(selectedStep(), adjust->delta);
        } else if (const auto* adjust = std::get_if<AdjustGate>(&event)) {
            _stepEditor.adjustGate(selectedStep(), adjust->delta);
        } else if (std::holds_alternative<ToggleTransport>(event)) {
            if (_transport != nullptr) {
                _transport->toggle(nowUs);
            } else if (_midiClock.mode() != MidiClockMode::External) {
                _sequencer.toggleRunning(nowUs);
            }
        } else if (std::holds_alternative<ActivateShift>(event)) {
            _inputRouter.activateShift();
        } else if (std::holds_alternative<DeactivateShift>(event)) {
            _inputRouter.deactivateShift();
        } else if (std::holds_alternative<OpenMidiClockSettings>(event)) {
            contextChanged = openMidiClockSettings();
        } else if (const auto* adjust = std::get_if<AdjustMidiClockPreview>(&event)) {
            _midiClockModal.adjustPreview(adjust->delta);
        } else if (std::holds_alternative<ConfirmMidiClockSettings>(event)) {
            if (_midiClockModal.isOpen()) {
                if (const auto mode = _midiClockModal.confirmedMode(); mode.has_value()) {
                    handleAppEvent(AppEvent{ApplyMidiClockMode{*mode}}, nullptr, nowUs);
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
                    _inputRouter.setProgramStorageState(_programStorageState);
                } else {
                    _programStorageAction = _programStorageSelection == ProgramStorageMenuItem::Save
                                                ? ProgramStorageAction::Save
                                                : ProgramStorageAction::Load;
                    _programStorageState = ProgramStorageModalState::Slot;
                    _programStorageSlot = 0;
                    _inputRouter.setProgramStorageState(_programStorageState);
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
                    _inputRouter.setProgramStorageState(_programStorageState);
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
                _inputRouter.setProgramStorageState(_programStorageState);
            }
        } else if (std::holds_alternative<CloseProgramStorage>(event)) {
            contextChanged = closeProgramStorage();
        } else if (!std::holds_alternative<AdjustTempo>(event) ||
                   _midiClock.mode() != MidiClockMode::External) {
            _handler.handle(event);
        }

        if (contextChanged && sourceInput != nullptr) {
            _inputRouter.captureButton(sourceInput);
        }
    }

    [[nodiscard]] auto selectedStep() const noexcept -> std::optional<std::uint8_t> {
        return _inputRouter.selectedStep();
    }
    [[nodiscard]] auto isShiftActive() const noexcept -> bool {
        return _inputRouter.isShiftActive();
    }
    [[nodiscard]] auto hasStepSettingsContext() const noexcept -> bool {
        return _inputRouter.hasStepSettingsContext();
    }
    [[nodiscard]] auto stackSize() const noexcept -> std::size_t {
        return _inputRouter.stackSize();
    }
    [[nodiscard]] auto midiClockMode() const noexcept -> MidiClockMode { return _midiClock.mode(); }
    [[nodiscard]] auto isMidiClockModalOpen() const noexcept -> bool {
        return _midiClockModal.isOpen();
    }
    [[nodiscard]] auto midiClockSelection() const noexcept -> MidiClockMenuItem {
        return _midiClockModal.selection();
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
        _inputRouter.setProgramStorageState(_programStorageState);
    }

    [[nodiscard]] auto decorateUiSettings(UiSettings settings) const -> UiSettings {
        settings.page = currentPage();
        settings.editor = _stepEditor.snapshot(selectedStep(), isShiftActive());
        settings.midiClock.modalOpen = _midiClockModal.isOpen();
        settings.midiClock.active = _midiClock.mode();
        settings.midiClock.selection = _midiClockModal.selection();
        settings.storage.state = _programStorageState;
        settings.storage.selection = _programStorageSelection;
        settings.storage.action = _programStorageAction;
        settings.storage.slot = _programStorageSlot;
        settings.storage.status = _programStorageStatus;
        settings.storage.resetChoice = _programResetChoice;
        return settings;
    }

  private:
    enum class ModalId : std::uint8_t {
        MidiClockSettings,
        ProgramStorage,
    };

    [[nodiscard]] auto currentPage() const noexcept -> UiPage {
        return selectedStep().has_value() ? UiPage::StepSettings : UiPage::MainDisplay;
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
        return _midiClockModal.isOpen() || isProgramStorageModalOpen();
    }

    [[nodiscard]] auto canOpenModal(ModalId modal) const noexcept -> bool {
        return !hasOpenModal() && currentPage() == modalHostPage(modal);
    }

    [[nodiscard]] auto openMidiClockSettings() -> bool {
        if (!canOpenModal(ModalId::MidiClockSettings)) {
            return false;
        }
        if (!_inputRouter.openMidiClockSettings()) {
            return false;
        }
        _midiClockModal.open(_midiClock.mode());
        return true;
    }

    [[nodiscard]] auto closeMidiClockSettings() -> bool {
        if (!_inputRouter.closeMidiClockSettings()) {
            return false;
        }

        _midiClockModal.close();
        return true;
    }

    [[nodiscard]] auto openProgramStorage() -> bool {
        if (!canOpenModal(ModalId::ProgramStorage) || !_inputRouter.openProgramStorage()) {
            return false;
        }
        _programStorageState = ProgramStorageModalState::Action;
        _programStorageSelection = ProgramStorageMenuItem::Save;
        _programStorageAction = ProgramStorageAction::Save;
        _programStorageSlot = 0;
        _programStorageStatus = ProgramStoreStatus::Ok;
        _programResetChoice = ProgramResetChoice::No;
        _programStorageResetPending = false;
        _inputRouter.setProgramStorageState(_programStorageState);
        return true;
    }

    auto closeProgramStorage() -> bool {
        if (!isProgramStorageModalOpen() ||
            _programStorageState == ProgramStorageModalState::Busy ||
            !_inputRouter.closeProgramStorage()) {
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
    StepEditor _stepEditor;
    MidiClockSettings& _midiClock;
    TransportController* _transport;
    ProgramStorageController* _programStorage;
    AppInputRouter<Capacity> _inputRouter;
    MidiClockModal _midiClockModal;
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
