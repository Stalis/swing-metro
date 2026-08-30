#include "7seg_disp.h"
#include <Arduino.h>

union SevenSeg {
    uint8_t bytes;
    struct {
        bool a : 1;
        bool b : 1;
        bool c : 1;
        bool d : 1;
        bool e : 1;
        bool f : 1;
        bool g : 1;
        bool dot : 1;
    } segments;
};

int int_pow(int base, int exp) {
    int res = 1;
    for (int i = 0; i < exp; ++i) {
        res *= base;
    }
    return res;
}

constexpr int DIGITS_NUM = 4;

bool isDigitsPrinted = false;

SevenSegmentDisplay::SevenSegmentDisplay(const ShiftRegisterPins pins) noexcept
    : _dataPin(pins.data), _clockPin(pins.clock), _latchPin(pins.latch), _currentDigit(0),
      _currentNumber(0) {}

void SevenSegmentDisplay::init() const {
    pinMode(_dataPin, OUTPUT);
    pinMode(_clockPin, OUTPUT);
    pinMode(_latchPin, OUTPUT);
}

void SevenSegmentDisplay::setNumber(int number) {
    _currentNumber = number;
    parseNumber(number);
}

void SevenSegmentDisplay::parseNumber(int number) {
    if (number < 0) {
        number = -number;
    }
    constexpr int BASE_VALUE = 10;
    for (int i = 0; i < DIGITS_NUM; i++) {
        _digits[DIGITS_NUM - i - 1] =
            static_cast<uint8_t>((number % int_pow(BASE_VALUE, i + 1)) / int_pow(BASE_VALUE, i));
    }
}

void SevenSegmentDisplay::update() {
    uint8_t digitPack = 1 << _currentDigit;
    // HACK: Костыль, потому что я подключил 1 разряд не к QA(15 ножка) а к QB(1 ножка)
    digitPack <<= 1;

    uint8_t decodedDigit = getDigitAs7seg();
    _currentDigit++;
    if (_currentDigit == DIGITS_NUM) {
        _currentDigit = 0;
    }

    // Serial.print("Current digit: ");
    // Serial.print(_currentDigit);
    // Serial.print("\tDigit Pack: ");
    // Serial.print(digitPack, BIN);
    // Serial.print("\tCurrent Number: ");
    // Serial.print(_currentNumber, BIN);
    // Serial.println();

    digitalWrite(_latchPin, LOW);
    shiftOut(_dataPin, _clockPin, MSBFIRST, digitPack);
    shiftOut(_dataPin, _clockPin, MSBFIRST, ~decodedDigit);
    digitalWrite(_latchPin, HIGH);

    if (!isDigitsPrinted) {
        Serial.print("Digits: [ ");
        for (int i = 0; i < DIGITS_NUM; i++) {
            Serial.print(_digits[i]);
            Serial.print(" __ ");
        }
        Serial.println(" ]");
        isDigitsPrinted = true;
    }
}

void SevenSegmentDisplay::pureDataShiftOut(uint8_t data) const {
    digitalWrite(_latchPin, LOW);
    shiftOut(_dataPin, _clockPin, MSBFIRST, data);
    digitalWrite(_latchPin, HIGH);
}

uint8_t SevenSegmentDisplay::getDigit() const { return _digits[_currentDigit]; };
uint8_t SevenSegmentDisplay::getDigitAs7seg() const {
    uint8_t digit = getDigit();

    SevenSeg impl{};

    // HACK: Неправильная коммутация сегментов!?
    // Для референсного CA56-12EWA
    // Для моего 3461BS разметки нет
    //  1 = B + C
    //  2 = A + B + G + E + D
    //  3 = A + B + G + C + D
    //  4 = F + G + B + C + D
    //  5 = A + F + G + C + D
    //
    //  |---A---|
    //  |       |
    //  F       B
    //  |       |
    //  |---G---|
    //  |       |
    //  E       C
    //  |       |
    //  |---D---|
    //

    if (digit == 1) {
        impl.segments.b = true;
        impl.segments.f = true;
    } else if (digit == 2) {
        impl.segments.a = true;
        impl.segments.b = true;
        impl.segments.d = true;
        impl.segments.e = true;
        impl.segments.g = true;
    } else if (digit == 3) {
        impl.segments.a = true;
        impl.segments.b = true;
        impl.segments.d = true;
        impl.segments.f = true;
        impl.segments.g = true;
    } else if (digit == 4) {
        // impl.segments.a = true;
        impl.segments.b = true;
        impl.segments.c = true;
        // impl.segments.d = true;
        // impl.segments.e = true;
        impl.segments.f = true;
        impl.segments.g = true;
    } else if (digit == 5) {
        impl.segments.a = true;
        // impl.segments.b = true;
        impl.segments.c = true;
        impl.segments.d = true;
        // impl.segments.e = true;
        impl.segments.f = true;
        impl.segments.g = true;
    } else if (digit == 6) {
        impl.segments.a = true;
        // impl.segments.b = true;
        impl.segments.c = true;
        impl.segments.d = true;
        impl.segments.e = true;
        impl.segments.f = true;
        impl.segments.g = true;
    } else if (digit == 7) {
        impl.segments.a = true;
        impl.segments.b = true;
        // impl.segments.c = true;
        // impl.segments.d = true;
        // impl.segments.e = true;
        impl.segments.f = true;
        // impl.segments.g = true;
    } else if (digit == 8) {
        impl.segments.a = true;
        impl.segments.b = true;
        impl.segments.c = true;
        impl.segments.d = true;
        impl.segments.e = true;
        impl.segments.f = true;
        impl.segments.g = true;
    } else if (digit == 9) {
        impl.segments.a = true;
        impl.segments.b = true;
        impl.segments.c = true;
        impl.segments.d = true;
        // impl.segments.e = true;
        impl.segments.f = true;
        impl.segments.g = true;
    } else if (digit == 0) {
        impl.segments.a = true;
        impl.segments.b = true;
        impl.segments.c = true;
        impl.segments.d = true;
        impl.segments.e = true;
        impl.segments.f = true;
    }

    // impl.bytes = 0;
    // impl.segments.a = true;
    // impl.segments.b = true;
    // impl.segments.c = true;
    // impl.segments.d = true;
    // impl.segments.e = true;
    // impl.segments.f = true;
    // impl.segments.g = true;

    return impl.bytes;
}
