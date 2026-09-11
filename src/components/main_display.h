#pragma once
#include <Arduino_GFX_Library.h>
#include <bitset>
#include <cstdint>

class MainDisplay {
  public:
    MainDisplay(Arduino_GFX& gfx);

    void init();

    void updateTempo(uint8_t tempo);
    void updateSwing(uint8_t swing);
    void updateVolume(uint8_t volume);

    void updateNotesStates(std::bitset<16> notesStates, uint8_t activeNoteNumber);

  private:
    void drawValue(uint8_t value, int16_t y, uint16_t color);
    void drawNoteState(uint8_t noteNumber, bool state, bool active);

    Arduino_GFX& _gfx;
};