#include "ui_fonts.h"

#include <cstddef>
#include <cstdint>

namespace {

// Silkscreen Flat adds the lowercase b used for flat notes. See OFL-Silkscreen.txt.
const std::uint8_t silkscreenFlatTtf[] = {
#include "silkscreen_flat_font_bytes.inc"
};

const lv_font_t* microFont = &lv_font_montserrat_8;
const lv_font_t* smallFont = &lv_font_montserrat_8;
const lv_font_t* mediumFont = &lv_font_montserrat_8;
const lv_font_t* largeFont = &lv_font_montserrat_10;

} // namespace

namespace UiFonts {

void initialize() {
    if (auto* font = lv_tiny_ttf_create_data(silkscreenFlatTtf, sizeof(silkscreenFlatTtf), 4)) {
        microFont = font;
    }
    if (auto* font = lv_tiny_ttf_create_data(silkscreenFlatTtf, sizeof(silkscreenFlatTtf), 5)) {
        smallFont = font;
    }
    if (auto* font = lv_tiny_ttf_create_data(silkscreenFlatTtf, sizeof(silkscreenFlatTtf), 6)) {
        mediumFont = font;
    }
    if (auto* font = lv_tiny_ttf_create_data(silkscreenFlatTtf, sizeof(silkscreenFlatTtf), 7)) {
        largeFont = font;
    }
}

const lv_font_t* micro() { return microFont; }
const lv_font_t* small() { return smallFont; }
const lv_font_t* medium() { return mediumFont; }
const lv_font_t* large() { return largeFont; }

} // namespace UiFonts
