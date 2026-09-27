#pragma once

#include "input/serial_run_controller.h"
#include <Arduino.h>

namespace SwingMetro {

class ArduinoDiagnosticConsole final : public DiagnosticConsole {
  public:
    void print(const char* text) override { Serial.print(text); }
    void print(std::uint32_t value) override { Serial.print(value); }
    void print(char character) override { Serial.print(character); }
    int read() override { return Serial.available() > 0 ? Serial.read() : -1; }
    void flush() override { Serial.flush(); }
    std::uint32_t nowMs() override { return millis(); }
    std::uint32_t nowUs() override { return micros(); }
};

} // namespace SwingMetro
