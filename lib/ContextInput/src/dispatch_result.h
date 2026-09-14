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

    [[nodiscard]] constexpr auto status() const noexcept -> DispatchStatus { return _status; }

    [[nodiscard]] constexpr auto hasEvent() const noexcept -> bool { return _event.has_value(); }

    constexpr auto event() noexcept -> TOutputEvent& {
        assert(hasEvent());
        return *_event;
    }

    constexpr auto event() const noexcept -> const TOutputEvent& {
        assert(hasEvent());
        return *_event;
    }

  private:
    explicit constexpr DispatchResult(DispatchStatus status) noexcept : _status{status} {
        assert(status != DispatchStatus::Emitted);
    }

    explicit constexpr DispatchResult(TOutputEvent event) noexcept(
        std::is_nothrow_move_constructible_v<TOutputEvent>)
        : _status{DispatchStatus::Emitted}, _event{std::move(event)} {}

    DispatchStatus _status;
    std::optional<TOutputEvent> _event;
};

} // namespace ContextInput
