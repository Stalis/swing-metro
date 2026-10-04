#include "ui_fonts.h"

#include "silkscreen_pixel_fonts.h"

namespace {

const lv_font_t* microFont = &lv_font_montserrat_8;
const lv_font_t* smallFont = &lv_font_montserrat_8;
const lv_font_t* mediumFont = &lv_font_montserrat_8;
const lv_font_t* largeFont = &lv_font_montserrat_10;

} // namespace

namespace UiFonts {

void initialize() {
    microFont = &silkscreen_flat_4;
    smallFont = &silkscreen_flat_5;
    mediumFont = &silkscreen_flat_6;
    largeFont = &silkscreen_flat_7;
}

const lv_font_t* micro() { return microFont; }
const lv_font_t* small() { return smallFont; }
const lv_font_t* medium() { return mediumFont; }
const lv_font_t* large() { return largeFont; }

} // namespace UiFonts
