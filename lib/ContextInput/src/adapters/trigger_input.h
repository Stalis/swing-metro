#pragma once

#include "../input_event.h"

#include <optional>
#include <type_traits>

namespace ContextInput {

template <typename TSourceId>
class TriggerInputAdapter {
    static_assert(std::is_copy_constructible_v<TSourceId>,
                  "ContextInput::TriggerInputAdapter requires a copyable source ID");

  public:
    explicit constexpr TriggerInputAdapter(const TSourceId& source, bool initialState = false)
        : _source{source}, _active{initialState} {}

    [[nodiscard]] auto set(bool active) -> std::optional<InputEvent<TSourceId>> {
        if (_active == active) {
            return std::nullopt;
        }

        _active = active;
        return InputEvent<TSourceId>{_source, TriggerInput{active}};
    }

    [[nodiscard]] constexpr auto isActive() const noexcept -> bool { return _active; }

  private:
    TSourceId _source;
    bool _active;
};

} // namespace ContextInput
