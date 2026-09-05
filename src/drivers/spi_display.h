#include <Arduino.h>
#include <SPI.h>

constexpr uint8_t TFT_SCK  = 10;
constexpr uint8_t TFT_MOSI = 11;
constexpr uint8_t TFT_DC   = 12;
constexpr uint8_t TFT_CS   = 13;
constexpr uint8_t TFT_RST  = 14;
constexpr uint8_t TFT_BL   = 15;

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


static void cmd(uint8_t c) {
    digitalWrite(TFT_DC, LOW);
    digitalWrite(TFT_CS, LOW);
    SPI1.transfer(c);
    digitalWrite(TFT_CS, HIGH);
}

static void data8(uint8_t d) {
    digitalWrite(TFT_DC, HIGH);
    digitalWrite(TFT_CS, LOW);
    SPI1.transfer(d);
    digitalWrite(TFT_CS, HIGH);
}

static void data16(uint16_t d) {
    digitalWrite(TFT_DC, HIGH);
    digitalWrite(TFT_CS, LOW);
    SPI1.transfer(d >> 8);
    SPI1.transfer(d & 0xFF);
    digitalWrite(TFT_CS, HIGH);
}

static void resetDisplay() {
    digitalWrite(TFT_RST, HIGH);
    delay(20);

    digitalWrite(TFT_RST, LOW);
    delay(20);

    digitalWrite(TFT_RST, HIGH);
    delay(150);
}

static void initDisplay() {
    resetDisplay();

    // Software reset
    cmd(0x01);
    delay(150);

    // Sleep out
    cmd(0x11);
    delay(150);

    // 16-bit RGB565
    cmd(0x3A);
    data8(0x05);

    // Memory access control
    cmd(0x36);
    data8(0x00);

    // Normal display mode
    cmd(0x13);
    delay(10);

    // Display on
    cmd(0x29);
    delay(100);
}

static void setWindow(
    uint16_t x0,
    uint16_t y0,
    uint16_t x1,
    uint16_t y1
) {
    cmd(0x2A);
    data16(x0);
    data16(x1);

    cmd(0x2B);
    data16(y0);
    data16(y1);

    cmd(0x2C);
}

static void fillScreen(uint16_t color) {
    setWindow(0, 0, 127, 159);

    digitalWrite(TFT_DC, HIGH);
    digitalWrite(TFT_CS, LOW);

    for (uint32_t i = 0; i < 128UL * 160UL; i++) {
        SPI1.transfer(color >> 8);
        SPI1.transfer(color & 0xFF);
    }

    digitalWrite(TFT_CS, HIGH);
}

static void drawPixel(uint16_t x, uint16_t y, uint16_t color) {
    if (x >= 128 || y >= 160) {
        return;
    }

    setWindow(x, y, x, y);
    data16(color);
}

static void drawPixel(uint16_t x, uint16_t y, RGB565_Color color) {
    drawPixel(x, y, static_cast<uint16_t>(color));
}

void display_setup() {
    pinMode(TFT_DC, OUTPUT);
    pinMode(TFT_CS, OUTPUT);
    pinMode(TFT_RST, OUTPUT);
    pinMode(TFT_BL, OUTPUT);

    digitalWrite(TFT_CS, HIGH);
    digitalWrite(TFT_DC, HIGH);
    digitalWrite(TFT_BL, HIGH);

    SPI1.setSCK(TFT_SCK);
    SPI1.setTX(TFT_MOSI);
    SPI1.begin();

    SPI1.beginTransaction(
        SPISettings(
            8000000,   // специально медленно для диагностики
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

constexpr uint8_t COUNTER_STEP = 50;
volatile static uint8_t skip_counter = COUNTER_STEP;
volatile static bool DRAW_CYCLE = true;

void draw_cycle() {
    if (DRAW_CYCLE) {
        fillScreen(static_cast<uint16_t>(RGB565_Color::Black));
    } else {
        fillScreen(static_cast<uint16_t>(RGB565_Color::White));
    }
}

void display_loop() {
    if (skip_counter == 0) {
        skip_counter = COUNTER_STEP;
        draw_cycle();
        DRAW_CYCLE = !DRAW_CYCLE;
    } else {
        skip_counter--;
    }
}