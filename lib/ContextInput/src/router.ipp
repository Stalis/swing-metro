#pragma once

namespace ContextInput {

template <typename TInputEvent, typename TOutputEvent, std::size_t Capacity>
template <typename TContext>
auto Router<TInputEvent, TOutputEvent, Capacity>::addContext(TContext& context) noexcept
    -> AddContextResult {
    using Context = std::remove_reference_t<TContext>;
    constexpr bool isCompatible = Detail::isCompatibleContextV<Context, TInputEvent, Result>;

    static_assert(!std::is_const_v<Context>,
                  "ContextInput::Router requires a non-const context object");
    static_assert(isCompatible, "Context must provide handle(const TInputEvent&) returning exactly "
                                "ContextInput::DispatchResult<TOutputEvent>");

    if constexpr (isCompatible && !std::is_const_v<Context>) {
        if (contains(context)) {
            return AddContextResult::AlreadyPresent;
        }

        if (size_ == Capacity) {
            return AddContextResult::StackFull;
        }

        contexts_[size_] = ContextRef{
            static_cast<void*>(std::addressof(context)),
            &invokeContext<Context>,
        };
        ++size_;
        return AddContextResult::Added;
    }

    return AddContextResult::AlreadyPresent;
}

template <typename TInputEvent, typename TOutputEvent, std::size_t Capacity>
template <typename TContext>
auto Router<TInputEvent, TOutputEvent, Capacity>::releaseContext(TContext& context) noexcept
    -> ReleaseContextResult {
    const auto* identity = static_cast<const void*>(std::addressof(context));

    for (std::size_t index = 0; index < size_; ++index) {
        if (contexts_[index].context != identity) {
            continue;
        }

        for (std::size_t next = index + 1; next < size_; ++next) {
            contexts_[next - 1] = contexts_[next];
        }

        --size_;
        contexts_[size_] = ContextRef{};
        return ReleaseContextResult::Released;
    }

    return ReleaseContextResult::NotFound;
}

template <typename TInputEvent, typename TOutputEvent, std::size_t Capacity>
template <typename TContext>
auto Router<TInputEvent, TOutputEvent, Capacity>::contains(const TContext& context) const noexcept
    -> bool {
    const auto* identity = static_cast<const void*>(std::addressof(context));

    for (std::size_t index = 0; index < size_; ++index) {
        if (contexts_[index].context == identity) {
            return true;
        }
    }

    return false;
}

template <typename TInputEvent, typename TOutputEvent, std::size_t Capacity>
auto Router<TInputEvent, TOutputEvent, Capacity>::dispatch(const TInputEvent& event)
    -> DispatchResult<TOutputEvent> {
    for (std::size_t index = size_; index > 0; --index) {
        auto result = contexts_[index - 1].handle(contexts_[index - 1].context, event);
        if (result.status() != DispatchStatus::Unhandled) {
            return result;
        }
    }

    return Result::pass();
}

template <typename TInputEvent, typename TOutputEvent, std::size_t Capacity>
auto Router<TInputEvent, TOutputEvent, Capacity>::size() const noexcept -> std::size_t {
    return size_;
}

template <typename TInputEvent, typename TOutputEvent, std::size_t Capacity>
constexpr auto Router<TInputEvent, TOutputEvent, Capacity>::capacity() noexcept -> std::size_t {
    return Capacity;
}

template <typename TInputEvent, typename TOutputEvent, std::size_t Capacity>
template <typename TContext>
auto Router<TInputEvent, TOutputEvent, Capacity>::invokeContext(void* context,
                                                                const TInputEvent& event)
    -> Result {
    return static_cast<TContext*>(context)->handle(event);
}

} // namespace ContextInput
