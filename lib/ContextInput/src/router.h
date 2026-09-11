#pragma once

#include "dispatch_result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

namespace ContextInput {

enum class AddContextResult : std::uint8_t {
    Added,
    AlreadyPresent,
    StackFull,
};

enum class ReleaseContextResult : std::uint8_t {
    Released,
    NotFound,
};

namespace Detail {

template <typename TContext, typename TInputEvent, typename TResult, typename TVoid = void>
struct IsCompatibleContext : std::false_type {};

template <typename TContext, typename TInputEvent, typename TResult>
struct IsCompatibleContext<
    TContext, TInputEvent, TResult,
    std::void_t<decltype(std::declval<TContext&>().handle(std::declval<const TInputEvent&>()))>>
    : std::is_same<decltype(std::declval<TContext&>().handle(std::declval<const TInputEvent&>())),
                   TResult> {};

template <typename TContext, typename TInputEvent, typename TResult>
inline constexpr bool isCompatibleContextV =
    IsCompatibleContext<TContext, TInputEvent, TResult>::value;

} // namespace Detail

template <typename TInputEvent, typename TOutputEvent, std::size_t Capacity>
class Router {
    static_assert(Capacity > 0, "ContextInput::Router capacity must be greater than zero");

  public:
    template <typename TContext>
    [[nodiscard]] auto addContext(TContext& context) noexcept -> AddContextResult;

    template <typename TContext>
    [[nodiscard]] auto releaseContext(TContext& context) noexcept -> ReleaseContextResult;

    template <typename TContext>
    [[nodiscard]] auto contains(const TContext& context) const noexcept -> bool;

    [[nodiscard]] auto dispatch(const TInputEvent& event) -> DispatchResult<TOutputEvent>;

    [[nodiscard]] auto size() const noexcept -> std::size_t;

    [[nodiscard]] static constexpr auto capacity() noexcept -> std::size_t;

  private:
    using Result = DispatchResult<TOutputEvent>;
    using Handler = Result (*)(void*, const TInputEvent&);

    struct ContextRef {
        void* context = nullptr;
        Handler handle = nullptr;
    };

    template <typename TContext>
    static auto invokeContext(void* context, const TInputEvent& event) -> Result;

    std::array<ContextRef, Capacity> contexts_{};
    std::size_t size_ = 0;
};

} // namespace ContextInput

#include "router.ipp"
