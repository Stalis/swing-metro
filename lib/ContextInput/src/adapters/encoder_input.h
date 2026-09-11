#pragma once

#include "../input_event.h"

#include <encoder.h>
#include <optional>
#include <type_traits>

namespace ContextInput {

template <typename TSourceId>
class EncoderInputAdapter {
    static_assert(std::is_copy_constructible_v<TSourceId>,
                  "ContextInput::EncoderInputAdapter requires a copyable source ID");

  public:
    explicit constexpr EncoderInputAdapter(const TSourceId& source) : source_{source} {}

    [[nodiscard]] auto translate(EncoderDirection direction) const
        -> std::optional<InputEvent<TSourceId>> {
        switch (direction) {
        case EncoderDirection::Clockwise:
            return InputEvent<TSourceId>{source_, EncoderInput{1}};
        case EncoderDirection::CounterClockwise:
            return InputEvent<TSourceId>{source_, EncoderInput{-1}};
        case EncoderDirection::Undefined:
            return std::nullopt;
        }

        return std::nullopt;
    }

  private:
    TSourceId source_;
};

} // namespace ContextInput
