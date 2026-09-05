#pragma once
#include <Arduino_GFX_Library.h>
#include <cstdint>

class MainDisplay {
public:
    MainDisplay(Arduino_GFX& gfx);

    void init();

    void updateTempo(uint8_t tempo);
    void updateSwing(uint8_t swing);
    void updateVolume(uint8_t volume);

private:
    void drawValue(uint8_t value, int16_t y, uint16_t color);

    Arduino_GFX& _gfx;
};