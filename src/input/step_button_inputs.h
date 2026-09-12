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

    static constexpr Time longPressThresholdMs = 500;

    [[nodiscard]] auto onPressed(std::uint8_t step, Time now) -> Batch {
        if (step >= STEPS_COUNT) {
            return Batch{};
        }
        return adapters_[step].onPressed(now);
    }

    [[nodiscard]] auto update(std::uint8_t step, Time now) -> Batch {
        if (step >= STEPS_COUNT) {
            return Batch{};
        }
        return adapters_[step].update(now);
    }

    [[nodiscard]] auto onReleased(std::uint8_t step, Time now) -> Batch {
        if (step >= STEPS_COUNT) {
            return Batch{};
        }
        return adapters_[step].onReleased(now);
    }

  private:
    template <std::size_t... Indices>
    [[nodiscard]] static auto makeAdapters(std::index_sequence<Indices...>)
        -> std::array<Adapter, STEPS_COUNT> {
        return {{Adapter{ContextInput::ButtonInputAdapterSettings<InputId>{
            inputIdForStep(static_cast<std::uint8_t>(Indices)), longPressThresholdMs}}...}};
    }

    std::array<Adapter, STEPS_COUNT> adapters_{
        makeAdapters(std::make_index_sequence<STEPS_COUNT>{})};
};

} // namespace SwingMetro
