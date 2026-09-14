#pragma once

#include "../event_batch.h"
#include "../input_event.h"

#include <cstdint>
#include <type_traits>

namespace ContextInput {

template <typename TSourceId>
struct ButtonInputAdapterSettings {
    TSourceId source;
    std::uint32_t longPressThreshold;
};

template <typename TSourceId>
class ButtonInputAdapter {
    static_assert(std::is_copy_constructible_v<TSourceId>,
                  "ContextInput::ButtonInputAdapter requires a copyable source ID");

  public:
    using Event = InputEvent<TSourceId>;
    using Batch = EventBatch<Event, 2>;
    using Time = std::uint32_t;

    explicit ButtonInputAdapter(ButtonInputAdapterSettings<TSourceId> settings)
        : _source{settings.source}, _longPressThreshold{settings.longPressThreshold} {}

    [[nodiscard]] auto onPressed(Time now) -> Batch {
        if (_pressed) {
            return Batch{};
        }

        _pressed = true;
        _pressedAt = now;
        _longPressSent = false;
        return Batch{makeEvent(ButtonPhase::Pressed)};
    }

    [[nodiscard]] auto update(Time now) -> Batch {
        if (!isLongPressDue(now)) {
            return Batch{};
        }

        _longPressSent = true;
        return Batch{makeEvent(ButtonPhase::LongPressed)};
    }

    [[nodiscard]] auto onReleased(Time now) -> Batch {
        if (!_pressed) {
            return Batch{};
        }

        _pressed = false;

        if (_longPressSent) {
            _longPressSent = false;
            return Batch{makeEvent(ButtonPhase::Released)};
        }

        if (elapsedSincePress(now) >= _longPressThreshold) {
            return Batch{makeEvent(ButtonPhase::LongPressed), makeEvent(ButtonPhase::Released)};
        }

        return Batch{makeEvent(ButtonPhase::Clicked), makeEvent(ButtonPhase::Released)};
    }

    [[nodiscard]] auto isPressed() const noexcept -> bool { return _pressed; }

  private:
    [[nodiscard]] auto makeEvent(ButtonPhase phase) const -> Event {
        return Event{_source, ButtonInput{phase}};
    }

    [[nodiscard]] auto elapsedSincePress(Time now) const noexcept -> Time {
        return now - _pressedAt;
    }

    [[nodiscard]] auto isLongPressDue(Time now) const noexcept -> bool {
        return _pressed && !_longPressSent && elapsedSincePress(now) >= _longPressThreshold;
    }

    TSourceId _source;
    Time _longPressThreshold;
    Time _pressedAt = 0;
    bool _pressed = false;
    bool _longPressSent = false;
};

} // namespace ContextInput
