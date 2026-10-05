#pragma once

#include "components/ui_snapshot.h"
#include "input/step_editor.h"

#include <cstdint>
#include <optional>

namespace SwingMetro {

struct AppUiSnapshotSources {
    const StepEditor& stepEditor;
    std::optional<std::uint8_t> selectedStep;
    bool shiftActive;
    bool midiClockModalOpen;
    MidiClockMode midiClockActive;
    MidiClockMenuItem midiClockSelection;
    const ProgramStorageModalSnapshot& storage;
};

class AppUiSnapshotBuilder {
  public:
    [[nodiscard]] static auto decorate(UiSettings settings, const AppUiSnapshotSources& sources)
        -> UiSettings {
        settings.page =
            sources.selectedStep.has_value() ? UiPage::StepSettings : UiPage::MainDisplay;
        settings.editor = sources.stepEditor.snapshot(sources.selectedStep, sources.shiftActive);
        settings.editor.mode = UiStepMode::Normal;
        settings.editor.repeatCount = 4;
        if (!settings.editor.transportRunning) {
            settings.main.activeNote = UINT8_MAX;
        }
        settings.main.highlightedStep = sources.selectedStep.value_or(UINT8_MAX);
        settings.main.clockMode = sources.midiClockActive;
        settings.midiClock.modalOpen = sources.midiClockModalOpen;
        settings.midiClock.active = sources.midiClockActive;
        settings.midiClock.selection = sources.midiClockSelection;
        settings.storage.state = sources.storage.state;
        settings.storage.selection = sources.storage.selection;
        settings.storage.action = sources.storage.action;
        settings.storage.slot = sources.storage.slot;
        settings.storage.status = sources.storage.status;
        settings.storage.resetChoice = sources.storage.resetChoice;
        return settings;
    }
};

} // namespace SwingMetro
