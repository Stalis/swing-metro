#include "app_event_handler.h"

#include <type_traits>

namespace {

auto applyDelta(Counter<std::uint8_t>& counter, std::int8_t remaining) -> void {
    while (remaining > 0) {
        counter.stepUp();
        --remaining;
    }

    while (remaining < 0) {
        counter.stepDown();
        ++remaining;
    }
}

} // namespace

namespace SwingMetro {

AppEventHandler::AppEventHandler(AppEventHandlerDependencies dependencies) noexcept
    : _tempo{dependencies.tempo}, _swing{dependencies.swing}, _volume{dependencies.volume},
      _sequencer{dependencies.sequencer} {}

auto AppEventHandler::handle(const AppEvent& event) -> void {
    std::visit(
        [this](const auto& concreteEvent) -> void {
            using TEvent = std::decay_t<decltype(concreteEvent)>;
            if constexpr (std::is_same_v<TEvent, AdjustTempo> ||
                          std::is_same_v<TEvent, AdjustSwing> ||
                          std::is_same_v<TEvent, AdjustVolume> ||
                          std::is_same_v<TEvent, ToggleStep>) {
                handle(concreteEvent);
            }
        },
        event);
}

auto AppEventHandler::handle(const AdjustTempo& event) -> void {
    applyDelta(_tempo, event.delta);
    _sequencer.setBpm(_tempo.getValue());
}

auto AppEventHandler::handle(const AdjustSwing& event) -> void {
    applyDelta(_swing, event.delta);
    _sequencer.setSwing(_swing.getValue());
}

auto AppEventHandler::handle(const AdjustVolume& event) -> void {
    applyDelta(_volume, event.delta);
}

auto AppEventHandler::handle(const ToggleStep& event) -> void {
    if (event.step < STEPS_COUNT) {
        _sequencer.toggleStep(event.step);
    }
}

} // namespace SwingMetro
