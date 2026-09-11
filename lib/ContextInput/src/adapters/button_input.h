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
        : source_{settings.source}, longPressThreshold_{settings.longPressThreshold} {}

    [[nodiscard]] auto onPressed(Time now) -> Batch {
        if (pressed_) {
            return Batch{};
        }

        pressed_ = true;
        pressedAt_ = now;
        longPressSent_ = false;
        return Batch{makeEvent(ButtonPhase::Pressed)};
    }

    [[nodiscard]] auto update(Time now) -> Batch {
        if (!isLongPressDue(now)) {
            return Batch{};
        }

        longPressSent_ = true;
        return Batch{makeEvent(ButtonPhase::LongPressed)};
    }

    [[nodiscard]] auto onReleased(Time now) -> Batch {
        if (!pressed_) {
            return Batch{};
        }

        pressed_ = false;

        if (longPressSent_) {
            longPressSent_ = false;
            return Batch{makeEvent(ButtonPhase::Released)};
        }

        if (elapsedSincePress(now) >= longPressThreshold_) {
            return Batch{makeEvent(ButtonPhase::LongPressed), makeEvent(ButtonPhase::Released)};
        }

        return Batch{makeEvent(ButtonPhase::Clicked), makeEvent(ButtonPhase::Released)};
    }

    [[nodiscard]] auto isPressed() const noexcept -> bool { return pressed_; }

  private:
    [[nodiscard]] auto makeEvent(ButtonPhase phase) const -> Event {
        return Event{source_, ButtonInput{phase}};
    }

    [[nodiscard]] auto elapsedSincePress(Time now) const noexcept -> Time {
        return now - pressedAt_;
    }

    [[nodiscard]] auto isLongPressDue(Time now) const noexcept -> bool {
        return pressed_ && !longPressSent_ && elapsedSincePress(now) >= longPressThreshold_;
    }

    TSourceId source_;
    Time longPressThreshold_;
    Time pressedAt_ = 0;
    bool pressed_ = false;
    bool longPressSent_ = false;
};

} // namespace ContextInput
