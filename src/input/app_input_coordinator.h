#pragma once

#include "app_event_handler.h"
#include "app_input_router.h"
#include "engine/transport_controller.h"
#include "midi_clock_modal.h"
#include "program/program_storage_request.h"
#include "step_editor.h"

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
          _transport{transport}, _programStorageRequest{programStorage} {}

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
            _programStorageModal.selectAction(select->delta);
        } else if (std::holds_alternative<ConfirmProgramStorageAction>(event)) {
            if (_programStorageModal.confirmAction()) {
                _inputRouter.setProgramStorageState(_programStorageModal.snapshot().state);
            }
            if (_programStorageModal.closeRequested()) {
                contextChanged = closeProgramStorage();
            }
        } else if (const auto* select = std::get_if<SelectProgramStorageSlot>(&event)) {
            _programStorageModal.selectSlot(select->delta);
        } else if (std::holds_alternative<ConfirmProgramStorageSlot>(event)) {
            if (const auto command = _programStorageModal.confirmSlot(); command.has_value()) {
                _programStorageRequest.enqueue(*command);
                _inputRouter.setProgramStorageState(_programStorageModal.snapshot().state);
            }
            if (_programStorageModal.closeRequested()) {
                contextChanged = closeProgramStorage();
            }
        } else if (const auto* select = std::get_if<SelectProgramResetChoice>(&event)) {
            _programStorageModal.selectResetChoice(select->delta);
        } else if (std::holds_alternative<ConfirmProgramReset>(event)) {
            if (_programStorageModal.snapshot().state ==
                ProgramStorageModalState::ResetConfirmation) {
                if (const auto command = _programStorageModal.confirmReset(); command.has_value()) {
                    _programStorageRequest.enqueue(*command);
                }
                _inputRouter.setProgramStorageState(_programStorageModal.snapshot().state);
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
        return _programStorageModal.isOpen();
    }

    auto processProgramStorage() -> void {
        const auto status = _programStorageRequest.process();
        if (!status.has_value()) {
            return;
        }
        _programStorageModal.complete(*status);
        _inputRouter.setProgramStorageState(_programStorageModal.snapshot().state);
    }

    [[nodiscard]] auto decorateUiSettings(UiSettings settings) const -> UiSettings {
        settings.page = currentPage();
        settings.editor = _stepEditor.snapshot(selectedStep(), isShiftActive());
        settings.midiClock.modalOpen = _midiClockModal.isOpen();
        settings.midiClock.active = _midiClock.mode();
        settings.midiClock.selection = _midiClockModal.selection();
        const auto& storage = _programStorageModal.snapshot();
        settings.storage.state = storage.state;
        settings.storage.selection = storage.selection;
        settings.storage.action = storage.action;
        settings.storage.slot = storage.slot;
        settings.storage.status = storage.status;
        settings.storage.resetChoice = storage.resetChoice;
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
        _programStorageModal.open();
        _inputRouter.setProgramStorageState(_programStorageModal.snapshot().state);
        return true;
    }

    auto closeProgramStorage() -> bool {
        if (!isProgramStorageModalOpen() ||
            _programStorageModal.snapshot().state == ProgramStorageModalState::Busy ||
            !_inputRouter.closeProgramStorage()) {
            return false;
        }
        _programStorageModal.close();
        return true;
    }

    AppEventHandler& _handler;
    Sequencer& _sequencer;
    StepEditor _stepEditor;
    MidiClockSettings& _midiClock;
    TransportController* _transport;
    ProgramStorageRequest _programStorageRequest;
    AppInputRouter<Capacity> _inputRouter;
    MidiClockModal _midiClockModal;
    ProgramStorageModal _programStorageModal;
};

} // namespace SwingMetro
