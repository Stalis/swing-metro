#pragma once

#include <Arduino_GFX_Library.h>
#include <array>
#include <lvgl.h>

constexpr const uint8_t DISPLAY_SPI_CLOCK_PIN = 10;
constexpr const uint8_t DISPLAY_SPI_DATA_OUT_PIN = 11;
constexpr const uint8_t DISPLAY_DATA_COMMAND_PIN = 12;
constexpr const uint8_t DISPLAY_CHIP_SELECT_PIN = 13;
constexpr const uint8_t DISPLAY_RESET_PIN = 14;
constexpr const uint8_t DISPLAY_BACKLIGHT_PIN = 15;

constexpr const int16_t DISPLAY_WIDTH = 128;
constexpr const int16_t DISPLAY_HEIGHT = 160;
constexpr const int16_t BUFFER_ROWS = 40;
constexpr const int16_t BUFFER_SIZE = DISPLAY_HEIGHT * BUFFER_ROWS;

class LVGL_Ui {
  public:
    void setup();
    void loop();

  private:
    std::array<lv_color_t, BUFFER_SIZE> _drawBuffer{};
    lv_display_t* _display;
    Arduino_RPiPicoSPI _bus{DISPLAY_DATA_COMMAND_PIN,
                            DISPLAY_CHIP_SELECT_PIN,
                            DISPLAY_SPI_CLOCK_PIN,
                            DISPLAY_SPI_DATA_OUT_PIN,
                            0,
                            spi1};
    Arduino_ST7735 _gfx{
        &_bus, DISPLAY_RESET_PIN, 1U, false, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0, 0, 0, false};

    static void flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels);

    // SCREENS
    lv_obj_t* _mainScreen;

    void initMainScreen();

    // DATA
    lv_obj_t* _tempo{};
    lv_obj_t* _swing{};
    lv_obj_t* _volume{};
    std::array<lv_obj_t*, 16> sequencerSteps{};
};