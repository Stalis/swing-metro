#pragma once

#include <cstdint>

namespace SwingMetro {

// Text output only: serialization does not know about USB, Arduino, or clocks.
class DiagnosticOutput {
  public:
    virtual ~DiagnosticOutput() = default;
    virtual void print(const char* text) = 0;
    virtual void print(std::uint32_t value) = 0;
    virtual void print(char character) = 0;
    void println() { print("\r\n"); }
    void println(const char* text) {
        print(text);
        println();
    }
    void println(std::uint32_t value) {
        print(value);
        println();
    }
};

} // namespace SwingMetro
