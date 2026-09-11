#pragma once

#include <Arduino.h>

struct ShiftRegisterPins {
    uint8_t data;
    uint8_t clock;
    uint8_t latch;
};

class SevenSegmentDisplay {
  public:
    SevenSegmentDisplay(const ShiftRegisterPins pins) noexcept;
    void init() const;
    void setNumber(int number);
    void update();
    void pureDataShiftOut(uint8_t data) const;

  private:
    uint8_t _dataPin;
    uint8_t _clockPin;
    uint8_t _latchPin;
    uint8_t _currentDigit;
    uint8_t _digits[4];
    int _currentNumber;

    void parseNumber(int number);
    uint8_t getDigit() const;
    uint8_t getDigitAs7seg() const;
};
