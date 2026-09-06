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

    for (int i = 0; i < 16; i++) {
        drawNoteState(i, false);
    }
}

void MainDisplay::drawValue(uint8_t value, int16_t y, uint16_t color) {
    // uint8_t needs at most three digits; spaces erase any trailing old digits.
    char text[4];
    snprintf(text, sizeof(text), "%-3u", static_cast<unsigned int>(value));
    _gfx.setTextColor(color, RGB565_BLACK);
    _gfx.setCursor(58, y);
    _gfx.print(text);
}

constexpr uint8_t size = (160 / 16) * 0.9;
constexpr uint8_t firstLineY = 105;
constexpr uint8_t xOffset = 1;
constexpr uint8_t xGap = 1;
constexpr uint8_t yGap = 1;
constexpr uint8_t lineCount = 8;

void MainDisplay::drawNoteState(uint8_t noteNumber, bool state) {
    const uint8_t line = noteNumber >= lineCount ? 1 : 0;
    const uint8_t column = noteNumber >= lineCount ? noteNumber - 8 : noteNumber;

    const uint8_t x1 = xOffset + (column * (size + xGap));
    const uint8_t y1 = firstLineY + (line * (size + yGap));

    _gfx.drawRect(
        x1, y1,
        size, size,
        RGB565_YELLOW
    );
    _gfx.fillRect(
        x1 + 1, y1 + 1,
        size - 2, size - 2,
        state ? RGB565_DARKORANGE : RGB565_BLACK
    );
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

void MainDisplay::updateNotesStates(std::bitset<16> notesState) {
    for (int i = 0; i < notesState.size(); i++) {
        drawNoteState(i, notesState[i]);
    }
}