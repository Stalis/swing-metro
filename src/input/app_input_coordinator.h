#pragma once

#include "app_contexts.h"
#include "app_event_handler.h"
#include "components/ui_view_model.h"
#include "main_display_context.h"

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
    AppInputCoordinator(AppEventHandler& handler, Sequencer& sequencer)
        : handler_{handler}, sequencer_{sequencer}, shiftContext_{selectedStep_} {
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
        bool navigationChanged = false;
        if (const auto* open = std::get_if<OpenStepSettings>(&event)) {
            navigationChanged = openStepSettings(open->step);
        } else if (std::holds_alternative<CloseStepSettings>(event)) {
            navigationChanged = closeStepSettings();
        } else if (const auto* adjust = std::get_if<AdjustNote>(&event)) {
            if (selectedStep_.has_value()) {
                (void)sequencer_.adjustStepNote(*selectedStep_, adjust->delta);
            }
        } else if (const auto* adjust = std::get_if<AdjustVelocity>(&event)) {
            if (selectedStep_.has_value()) {
                (void)sequencer_.adjustStepVelocity(*selectedStep_, adjust->delta);
            }
        } else if (std::holds_alternative<ToggleTransport>(event)) {
            sequencer_.toggleRunning(nowUs);
        } else if (std::holds_alternative<ActivateShift>(event)) {
            (void)router_.addContext(shiftContext_);
        } else if (std::holds_alternative<DeactivateShift>(event)) {
            (void)router_.releaseContext(shiftContext_);
        } else {
            handler_.handle(event);
        }

        if (navigationChanged && sourceInput != nullptr) {
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

    AppEventHandler& handler_;
    Sequencer& sequencer_;
    std::optional<std::uint8_t> selectedStep_;
    GlobalContext globalContext_;
    MainDisplayContext mainContext_;
    StepSettingsContext stepContext_;
    ShiftContext shiftContext_;
    ContextInput::Router<InputEvent, AppEvent, Capacity> router_;
    std::bitset<256> capturedButtons_;
};

} // namespace SwingMetro
