#include "main_display.h"
#include <cstdio>

MainDisplay::MainDisplay(Arduino_GFX& gfx) : _gfx(gfx) {}

void MainDisplay::init() {
    _gfx.setFont(nullptr);
    _gfx.setTextSize(1);
    _gfx.fillScreen(RGB565_BLACK);

    _gfx.setCursor(10, 10);
    _gfx.setTextColor(RGB565_RED);
    _gfx.print("Tempo: ");

    _gfx.setCursor(10, 30);
    _gfx.setTextColor(RGB565_GREEN);
    _gfx.print("Swing: ");

    _gfx.setCursor(10, 50);
    _gfx.setTextColor(RGB565_BLUE);
    _gfx.print("Volume: ");
}

void MainDisplay::drawValue(uint8_t value, int16_t y, uint16_t color) {
    // uint8_t needs at most three digits; spaces erase any trailing old digits.
    char text[4];
    snprintf(text, sizeof(text), "%-3u", static_cast<unsigned int>(value));
    _gfx.setTextColor(color, RGB565_BLACK);
    _gfx.setCursor(58, y);
    _gfx.print(text);
}

void MainDisplay::updateTempo(uint8_t tempo) {
    drawValue(tempo, 10, RGB565_RED);
}

void MainDisplay::updateSwing(uint8_t swing) {
    drawValue(swing, 30, RGB565_GREEN);
}

void MainDisplay::updateVolume(uint8_t volume) {
    drawValue(volume, 50, RGB565_BLUE);
}
