#pragma once

#include "app_input.h"
#include "engine/sequencer.h"

#include <cstdint>
#include <utils/counter.h>

namespace SwingMetro {

struct AppEventHandlerDependencies {
    Counter<std::uint8_t>& tempo;
    Counter<std::uint8_t>& swing;
    Counter<std::uint8_t>& volume;
    Sequencer& sequencer;
};

class AppEventHandler {
  public:
    explicit AppEventHandler(AppEventHandlerDependencies dependencies) noexcept;

    auto handle(const AppEvent& event) -> void;

  private:
    auto handle(const AdjustTempo& event) -> void;
    auto handle(const AdjustSwing& event) -> void;
    auto handle(const AdjustVolume& event) -> void;
    auto handle(const ToggleStep& event) -> void;

    Counter<std::uint8_t>& tempo_;
    Counter<std::uint8_t>& swing_;
    Counter<std::uint8_t>& volume_;
    Sequencer& sequencer_;
};

} // namespace SwingMetro
