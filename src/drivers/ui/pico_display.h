#pragma once

#include <Arduino_GFX_Library.h>
#include <array>
#include <lvgl.h>

#include "engine/runtime_timing_diagnostics.h"

class PicoDisplay {
  public:
    explicit PicoDisplay(SwingMetro::RuntimeTimingDiagnostics& runtimeTimingDiagnostics)
        : _runtimeTimingDiagnostics(runtimeTimingDiagnostics) {}

    PicoDisplay(const PicoDisplay&) = delete;
    PicoDisplay& operator=(const PicoDisplay&) = delete;
    PicoDisplay(PicoDisplay&&) = delete;
    PicoDisplay& operator=(PicoDisplay&&) = delete;

    void setup();

  private:
    static constexpr uint8_t SPI_CLOCK_PIN = 10;
    static constexpr uint8_t SPI_DATA_OUT_PIN = 11;
    static constexpr uint8_t DATA_COMMAND_PIN = 12;
    static constexpr uint8_t CHIP_SELECT_PIN = 13;
    static constexpr uint8_t RESET_PIN = 14;
    static constexpr uint8_t BACKLIGHT_PIN = 15;
    static constexpr int16_t WIDTH = 128;
    static constexpr int16_t HEIGHT = 160;
    static constexpr int16_t BUFFER_ROWS = 40;
    static constexpr int16_t BUFFER_SIZE = HEIGHT * BUFFER_ROWS;

    SwingMetro::RuntimeTimingDiagnostics& _runtimeTimingDiagnostics;
    std::array<lv_color_t, BUFFER_SIZE> _drawBuffer{};
    lv_display_t* _display = nullptr;
    Arduino_RPiPicoSPI _bus{
        DATA_COMMAND_PIN, CHIP_SELECT_PIN, SPI_CLOCK_PIN, SPI_DATA_OUT_PIN, 0, spi1};
    Arduino_ST7735 _gfx{&_bus, RESET_PIN, 1U, false, WIDTH, HEIGHT, 0, 0, 0, 0, false};

    static void flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels);
    void showSplash();
};
