#pragma once

#include <Arduino.h>
#include <SPI.h>

constexpr uint8_t TFT_SCK  = 10;
constexpr uint8_t TFT_MOSI = 11;
constexpr uint8_t TFT_DC   = 12;
constexpr uint8_t TFT_CS   = 13;
constexpr uint8_t TFT_RST  = 14;
constexpr uint8_t TFT_BL   = 15;

enum class ST7735_Command : uint8_t {
    SoftwareReset = 0x01,
    ExitSleepMode = 0x11,
    NormalDisplayModeOn = 0x13,
    DisplayOn = 0x29,
    SetColumnAddress = 0x2A,
    SetRowAddress = 0x2B,
    WriteDisplayMemory = 0x2C,
    SetMemoryAccessControl = 0x36,
    SetPixelFormat = 0x3A,
};

enum class RGB565_Color : uint16_t {
    Black       = 0x0000,
    White       = 0xFFFF,

    Red         = 0xF800,
    Green       = 0x07E0,
    Blue        = 0x001F,

    Cyan        = 0x07FF,
    Magenta     = 0xF81F,
    Yellow      = 0xFFE0,

    Orange      = 0xFD20,
    Purple      = 0x780F,
    Pink        = 0xF81F,

    Gray        = 0x8410,
    DarkGray    = 0x4208,
    LightGray   = 0xC618,

    DarkRed     = 0x8000,
    DarkGreen   = 0x0400,
    DarkBlue    = 0x0010,

    Navy        = 0x000F,
    Teal        = 0x0410,
    Olive       = 0x8400,
    Maroon      = 0x8000
};

struct ST7735S_DisplaySettings {
    uint8_t pinSCK;
    uint8_t pinMOSI;
    uint8_t pinDC;
    uint8_t pinCS;
    uint8_t pinRST;
    uint8_t pinBL;
};

class ST7735S_Display {
public:
    ST7735S_Display(const ST7735S_DisplaySettings& settings)
        : _spi(SPI1), _backlightPin(settings.pinBL), _resetPin(settings.pinRST),
          _chipSelectPin(settings.pinCS), _dataCommandPin(settings.pinDC) {
    }


    void init() {
        pinMode(_dataCommandPin, OUTPUT);
        pinMode(_chipSelectPin, OUTPUT);
        pinMode(_resetPin, OUTPUT);
        pinMode(_backlightPin, OUTPUT);

        digitalWrite(_chipSelectPin, HIGH);
        digitalWrite(_dataCommandPin, HIGH);
        digitalWrite(_backlightPin, HIGH);

        _spi = SPI1;

        _spi.setSCK(TFT_SCK);
        _spi.setTX(TFT_MOSI);
        _spi.begin();

        _spi.beginTransaction(
            SPISettings(
                16000000,   // специально медленно для диагностики
                MSBFIRST,
                SPI_MODE0
            )
        );

        initDisplay();

        // Полностью красный экран
        fillScreen(0xF800);

        for (int w = 0; w < 50; w++) {
            for (int h = 0; h < 30; h++) {
                drawPixel(w + 20, h+20, RGB565_Color::Cyan);
            }
        }
    }

    void resetDisplay() {
        digitalWrite(_resetPin, HIGH);
        delay(20);

        digitalWrite(_resetPin, LOW);
        delay(20);

        digitalWrite(_resetPin, HIGH);
        delay(150);
    }


    void setWindow(
        uint16_t x0,
        uint16_t y0,
        uint16_t x1,
        uint16_t y1
    ) {
        cmd(ST7735_Command::SetColumnAddress);
        data16(x0);
        data16(x1);

        cmd(ST7735_Command::SetRowAddress);
        data16(y0);
        data16(y1);

        cmd(ST7735_Command::WriteDisplayMemory);
    }

    void update() {
    }

    void drawPixel(uint16_t x, uint16_t y, RGB565_Color color) {
        drawPixel(x, y, static_cast<uint16_t>(color));
    }

    void drawPixel(uint16_t x, uint16_t y, uint16_t color) {
        if (x >= 128 || y >= 160) {
            return;
        }

        setWindow(x, y, x, y);
        data16(color);
    }

    void fillScreen(RGB565_Color color) {
        fillScreen(static_cast<uint16_t>(color));
    }

    void fillScreen(uint16_t color) {
        setWindow(0, 0, 127, 159);

        digitalWrite(_dataCommandPin, HIGH);
        digitalWrite(_chipSelectPin, LOW);

        for (uint32_t i = 0; i < 128UL * 160UL; i++) {
            SPI1.transfer(color >> 8);
            SPI1.transfer(color & 0xFF);
        }

        digitalWrite(_chipSelectPin, HIGH);
    }

    void cmd(ST7735_Command command) {
        digitalWrite(_dataCommandPin, LOW);
        digitalWrite(_chipSelectPin, LOW);
        _spi.transfer(static_cast<uint8_t>(command));
        digitalWrite(_chipSelectPin, HIGH);
    }

    void data8(uint8_t d) {
        digitalWrite(_dataCommandPin, HIGH);
        digitalWrite(_chipSelectPin, LOW);
        _spi.transfer(d);
        digitalWrite(_chipSelectPin, HIGH);
    }

    void data16(uint16_t d) {
        digitalWrite(_dataCommandPin, HIGH);
        digitalWrite(_chipSelectPin, LOW);
        _spi.transfer(d >> 8);
        _spi.transfer(d & 0xFF);
        digitalWrite(_chipSelectPin, HIGH);
    }

    void initDisplay() {
        resetDisplay();

        cmd(ST7735_Command::SoftwareReset);
        delay(150);

        cmd(ST7735_Command::ExitSleepMode);
        delay(150);

        // 16-bit RGB565
        cmd(ST7735_Command::SetPixelFormat);
        data8(0x05);

        cmd(ST7735_Command::SetMemoryAccessControl);
        data8(0x00);

        cmd(ST7735_Command::NormalDisplayModeOn);
        delay(10);

        cmd(ST7735_Command::DisplayOn);
        delay(100);
    }
private:
    SPIClassRP2040 _spi;
    uint8_t _backlightPin;
    uint8_t _resetPin;
    uint8_t _chipSelectPin;
    uint8_t _dataCommandPin;
};

ST7735S_Display display({
    .pinSCK = TFT_SCK,
    .pinMOSI = TFT_MOSI,
    .pinDC = TFT_DC,
    .pinCS = TFT_CS,
    .pinRST = TFT_RST,
    .pinBL = TFT_BL
});

void display_setup() {
    display.init();
}

constexpr uint8_t COUNTER_STEP = 50;
volatile static uint8_t skip_counter = COUNTER_STEP;
volatile static bool DRAW_CYCLE = true;

void draw_cycle() {
    if (DRAW_CYCLE) {
        display.fillScreen(RGB565_Color::Black);
    } else {
        display.fillScreen(RGB565_Color::White);
    }
}

// void display_loop() {
//     if (skip_counter == 0) {
//         skip_counter = COUNTER_STEP;
//         draw_cycle();
//         DRAW_CYCLE = !DRAW_CYCLE;
//     } else {
//         skip_counter--;
//     }
// }