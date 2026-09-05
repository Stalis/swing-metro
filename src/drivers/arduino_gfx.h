#pragma once

#include <Arduino_GFX_Library.h>

constexpr const uint8_t DISPLAY_SPI_CLOCK_PIN    = 10;
constexpr const uint8_t DISPLAY_SPI_DATA_OUT_PIN = 11;
constexpr const uint8_t DISPLAY_DATA_COMMAND_PIN = 12;
constexpr const uint8_t DISPLAY_CHIP_SELECT_PIN  = 13;
constexpr const uint8_t DISPLAY_RESET_PIN        = 14;
constexpr const uint8_t DISPLAY_BACKLIGHT_PIN    = 15;

constexpr const int16_t DISPLAY_WIDTH  = 128;
constexpr const int16_t DISPLAY_HEIGHT = 160;

Arduino_DataBus *bus = new Arduino_RPiPicoSPI(
    DISPLAY_DATA_COMMAND_PIN,
    DISPLAY_CHIP_SELECT_PIN,
    DISPLAY_SPI_CLOCK_PIN,
    DISPLAY_SPI_DATA_OUT_PIN,
    0, 
    spi1); // Constructor

Arduino_GFX *gfx = new Arduino_ST7735(bus, DISPLAY_RESET_PIN, 1U, false, DISPLAY_WIDTH, DISPLAY_HEIGHT); // Constructor

#define GFX_BL DISPLAY_BACKLIGHT_PIN

void display_setup(void)
{
#ifdef DEV_DEVICE_INIT
  DEV_DEVICE_INIT();
#endif

  Serial.begin(115200);
  // Serial.setDebugOutput(true);
  // while(!Serial);
  Serial.println("Arduino_GFX Hello World example");

  // Init Display
  if (!gfx->begin())
  {
    Serial.println("gfx->begin() failed!");
  }
  gfx->fillScreen(RGB565_BLACK);

#ifdef GFX_BL
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
#endif

  gfx->setCursor(10, 10);
  gfx->setTextColor(RGB565_RED);
  gfx->println("Hello World!");

  delay(5000); // 5 seconds
}

void display_loop()
{
  gfx->setCursor(random(gfx->width()), random(gfx->height()));
  gfx->setTextColor(random(0xffff), random(0xffff));
  gfx->setTextSize(random(6) + 1 /* x scale */, random(6) + 1 /* y scale */, random(2) /* pixel_margin */);
  gfx->println("Hello World!");

}