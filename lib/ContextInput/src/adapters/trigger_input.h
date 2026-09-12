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
        : source_{source}, active_{initialState} {}

    [[nodiscard]] auto set(bool active) -> std::optional<InputEvent<TSourceId>> {
        if (active_ == active) {
            return std::nullopt;
        }

        active_ = active;
        return InputEvent<TSourceId>{source_, TriggerInput{active}};
    }

    [[nodiscard]] constexpr auto isActive() const noexcept -> bool { return active_; }

  private:
    TSourceId source_;
    bool active_;
};

} // namespace ContextInput
