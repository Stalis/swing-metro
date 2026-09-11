#pragma once

#include <cassert>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace ContextInput {

enum class DispatchStatus : std::uint8_t {
    Unhandled,
    Consumed,
    Emitted,
};

template <typename TOutputEvent>
class DispatchResult {
  public:
    [[nodiscard]] static constexpr auto pass() noexcept -> DispatchResult {
        return DispatchResult{DispatchStatus::Unhandled};
    }

    [[nodiscard]] static constexpr auto consume() noexcept -> DispatchResult {
        return DispatchResult{DispatchStatus::Consumed};
    }

    [[nodiscard]] static constexpr auto
    emit(TOutputEvent event) noexcept(std::is_nothrow_move_constructible_v<TOutputEvent>)
        -> DispatchResult {
        return DispatchResult{std::move(event)};
    }

    [[nodiscard]] constexpr auto status() const noexcept -> DispatchStatus { return status_; }

    [[nodiscard]] constexpr auto hasEvent() const noexcept -> bool { return event_.has_value(); }

    constexpr auto event() noexcept -> TOutputEvent& {
        assert(hasEvent());
        return *event_;
    }

    constexpr auto event() const noexcept -> const TOutputEvent& {
        assert(hasEvent());
        return *event_;
    }

  private:
    explicit constexpr DispatchResult(DispatchStatus status) noexcept : status_{status} {
        assert(status != DispatchStatus::Emitted);
    }

    explicit constexpr DispatchResult(TOutputEvent event) noexcept(
        std::is_nothrow_move_constructible_v<TOutputEvent>)
        : status_{DispatchStatus::Emitted}, event_{std::move(event)} {}

    DispatchStatus status_;
    std::optional<TOutputEvent> event_;
};

} // namespace ContextInput
