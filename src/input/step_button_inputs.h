#pragma once

#include "app_input.h"

#include <adapters/button_input.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace SwingMetro {

class StepButtonInputs {
  public:
    using Adapter = ContextInput::ButtonInputAdapter<InputId>;
    using Batch = Adapter::Batch;
    using Time = Adapter::Time;

    static constexpr Time LONG_PRESS_THRESHOLD_MS = 500;

    [[nodiscard]] auto onPressed(std::uint8_t step, Time now) -> Batch {
        if (step >= STEPS_COUNT) {
            return Batch{};
        }
        return _adapters[step].onPressed(now);
    }

    [[nodiscard]] auto update(std::uint8_t step, Time now) -> Batch {
        if (step >= STEPS_COUNT) {
            return Batch{};
        }
        return _adapters[step].update(now);
    }

    [[nodiscard]] auto onReleased(std::uint8_t step, Time now) -> Batch {
        if (step >= STEPS_COUNT) {
            return Batch{};
        }
        return _adapters[step].onReleased(now);
    }

  private:
    template <std::size_t... Indices>
    [[nodiscard]] static auto makeAdapters(std::index_sequence<Indices...>)
        -> std::array<Adapter, STEPS_COUNT> {
        return {{Adapter{ContextInput::ButtonInputAdapterSettings<InputId>{
            inputIdForStep(static_cast<std::uint8_t>(Indices)), LONG_PRESS_THRESHOLD_MS}}...}};
    }

    std::array<Adapter, STEPS_COUNT> _adapters{
        makeAdapters(std::make_index_sequence<STEPS_COUNT>{})};
};

} // namespace SwingMetro
