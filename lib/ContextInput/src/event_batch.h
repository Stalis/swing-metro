#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace ContextInput {

template <typename TEvent, std::size_t Capacity>
class EventBatch {
    static_assert(Capacity > 0, "ContextInput::EventBatch capacity must be greater than zero");

  public:
    constexpr EventBatch() noexcept = default;

    template <typename TFirstEvent, typename... TRestEvents,
              std::enable_if_t<std::is_constructible_v<TEvent, TFirstEvent&&> &&
                                   (std::is_constructible_v<TEvent, TRestEvents&&> && ...),
                               int> = 0>
    explicit EventBatch(TFirstEvent&& firstEvent, TRestEvents&&... restEvents) {
        static_assert(sizeof...(TRestEvents) + 1 <= Capacity,
                      "ContextInput::EventBatch cannot contain more events than its capacity");

        appendUnchecked(std::forward<TFirstEvent>(firstEvent));
        (appendUnchecked(std::forward<TRestEvents>(restEvents)), ...);
    }

    [[nodiscard]] constexpr auto empty() const noexcept -> bool { return size_ == 0; }

    [[nodiscard]] constexpr auto size() const noexcept -> std::size_t { return size_; }

    [[nodiscard]] static constexpr auto capacity() noexcept -> std::size_t { return Capacity; }

    [[nodiscard]] auto operator[](std::size_t index) const -> const TEvent& {
        // The occupied prefix is contiguous, so index < size() guarantees a value.
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        return *events_[index];
    }

  private:
    template <typename TEventValue>
    auto appendUnchecked(TEventValue&& event) -> void {
        events_[size_].emplace(std::forward<TEventValue>(event));
        ++size_;
    }

    std::array<std::optional<TEvent>, Capacity> events_{};
    std::size_t size_ = 0;
};

} // namespace ContextInput
