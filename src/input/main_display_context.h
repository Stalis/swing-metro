#pragma once

#include "app_input.h"

namespace SwingMetro {

class MainDisplayContext {
  public:
    [[nodiscard]] auto handle(const InputEvent& event) const
        -> ContextInput::DispatchResult<AppEvent>;
};

} // namespace SwingMetro
