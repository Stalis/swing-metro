#pragma once

#include <Arduino.h>
#include <optional>

struct EncoderSettings {
    uint8_t pinA;
    uint8_t pinB;
    std::optional<uint8_t> pinSwitch;
};

class Encoder {
public:
    Encoder(EncoderSettings& settings);

private:
    uint8_t pinA;
    uint8_t pinB;
    std::optional<uint8_t> pinSwitch;
};