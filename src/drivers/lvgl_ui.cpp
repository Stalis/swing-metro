#include "lvgl_ui.h"

void LVGL_Ui::setup() {
    // Init Display
    if (!_gfx.begin()) {
        Serial.println("gfx->begin() failed!");
    }
    _gfx.fillScreen(RGB565_BLACK);

    pinMode(DISPLAY_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(DISPLAY_BACKLIGHT_PIN, HIGH);

    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });

    _display = lv_display_create(_gfx.width(), _gfx.height());
    lv_display_set_user_data(_display, this);
    lv_display_set_flush_cb(_display, flush);
    lv_display_set_buffers(_display, _drawBuffer.data(), nullptr,
                           _drawBuffer.size() * sizeof(_drawBuffer[0]),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    auto* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    auto* titleLabel = lv_label_create(screen);
    lv_label_set_text(titleLabel, "Swing Metro");
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFF0000), 0);
    lv_obj_align(titleLabel, LV_ALIGN_CENTER, 0, -8);

    auto* lvglLabel = lv_label_create(screen);
    lv_label_set_text_fmt(lvglLabel, "LVGL v%d.%d.%d", lv_version_major(), lv_version_minor(),
                          lv_version_patch());
    lv_obj_set_style_text_color(lvglLabel, lv_color_hex(0x808080), 0);
    lv_obj_align(lvglLabel, LV_ALIGN_CENTER, 0, 8);

    lv_refr_now(_display);
    delay(500); // 5 seconds

    lv_subject_init_int(&_tempoSubject, 120);
    lv_subject_set_max_value_int(&_tempoSubject, 240);
    lv_subject_set_min_value_int(&_tempoSubject, 40);

    lv_subject_init_int(&_swingSubject, 50);
    lv_subject_set_min_value_int(&_swingSubject, 50);
    lv_subject_set_max_value_int(&_swingSubject, 100);

    lv_subject_init_int(&_volumeSubject, 100);
    lv_subject_set_min_value_int(&_volumeSubject, 0);
    lv_subject_set_max_value_int(&_volumeSubject, 100);

    lv_subject_init_int(&_sequencerStepsSubject, 0);

    initMainScreen();
    lv_screen_load(_mainScreen);
}

void LVGL_Ui::loop() { lv_timer_handler(); }

void LVGL_Ui::readViewModel(const UiViewModel& viewModel) {
    auto values = viewModel.read();
    setTempo(values.tempo);
    setSwing(values.swing);
    setVolume(values.volume);

    setSteps(values.notesState, values.activeNote);
}

void LVGL_Ui::setTempo(uint8_t value) { lv_subject_set_int(&_tempoSubject, value); }
void LVGL_Ui::setSwing(uint8_t value) { lv_subject_set_int(&_swingSubject, value); }
void LVGL_Ui::setVolume(uint8_t value) { lv_subject_set_int(&_volumeSubject, value); }

void LVGL_Ui::setSteps(std::bitset<SEQUENCER_STEPS_COUNT> stepsState, uint8_t activeStep) {
    lv_subject_set_int(&_sequencerStepsSubject,
                       stepsState.to_ulong() | (static_cast<uint16_t>(activeStep) << 16));
}

void LVGL_Ui::flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels) {
    auto& self = *static_cast<LVGL_Ui*>(lv_display_get_user_data(display));
    self._gfx.draw16bitRGBBitmap(static_cast<int16_t>(area->x1), static_cast<int16_t>(area->y1),
                                 reinterpret_cast<uint16_t*>(pixels),
                                 static_cast<int16_t>(lv_area_get_width(area)),
                                 static_cast<int16_t>(lv_area_get_height(area)));
    lv_display_flush_ready(display);
}

void LVGL_Ui::initMainScreen() {
    _mainScreen = lv_obj_create(nullptr);

    lv_obj_set_style_bg_color(_mainScreen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(_mainScreen, LV_OPA_COVER, 0);

    auto* tempoLabel = lv_label_create(_mainScreen);
    lv_obj_set_pos(tempoLabel, 10, 10);
    lv_obj_set_style_text_color(tempoLabel, lv_color_hex(0xFF0000), 0);
    lv_label_bind_text(tempoLabel, &_tempoSubject, "Tempo: %d");

    auto* swingLabel = lv_label_create(_mainScreen);
    lv_obj_set_pos(swingLabel, 10, 30);
    lv_obj_set_style_text_color(swingLabel, lv_color_hex(0x00FF00), 0);
    lv_label_bind_text(swingLabel, &_swingSubject, "Swing: %d");

    auto* volumeLabel = lv_label_create(_mainScreen);
    lv_obj_set_pos(volumeLabel, 10, 50);
    lv_obj_set_style_text_color(volumeLabel, lv_color_hex(0x0000FF), 0);
    lv_label_bind_text(volumeLabel, &_volumeSubject, "Volume: %d");

    lv_obj_add_event_cb(_mainScreen, onMainScreenLoaded, LV_EVENT_SCREEN_LOADED, this);

    // lv_subject_add_observer_obj(&_sequencerStepsSubject, onStepsChanged, volumeLabel, this);
    lv_subject_add_observer(&_sequencerStepsSubject, onStepsChanged, this);
}

void LVGL_Ui::drawSequencerSteps() {
    auto rawValue = static_cast<uint32_t>(lv_subject_get_int(&_sequencerStepsSubject));
    drawSequencerSteps(rawValue);
}

void LVGL_Ui::drawSequencerSteps(uint32_t rawValue) {
    constexpr int16_t stepSize = 9;
    constexpr int16_t firstStepY = 105;

    std::bitset<SEQUENCER_STEPS_COUNT> enabledSteps{static_cast<uint16_t>(rawValue)};
    uint8_t activeStep = rawValue >> 16;

    for (uint8_t stepNumber = 0; stepNumber < enabledSteps.size(); ++stepNumber) {
        const uint8_t row = stepNumber / 8;
        const uint8_t column = stepNumber % 8;

        const bool isEnabled = enabledSteps[stepNumber];
        const bool isActive = stepNumber == activeStep;

        auto* step = _stepSquares[stepNumber];

        if (step == nullptr) {
            _stepSquares[stepNumber] = lv_obj_create(_mainScreen);
            step = _stepSquares[stepNumber];
            lv_obj_set_size(step, stepSize, stepSize);
            lv_obj_set_pos(step, 1 + (column * (stepSize + 1)),
                           firstStepY + (row * (stepSize + 1)));

            lv_obj_set_style_bg_opa(step, LV_OPA_COVER, 0);

            lv_obj_set_style_border_width(step, 1, 0);
            lv_obj_set_style_radius(step, 0, 0);
            lv_obj_set_style_pad_all(step, 0, 0);
            lv_obj_remove_flag(step, LV_OBJ_FLAG_SCROLLABLE);
        }

        auto backgroundColor = isEnabled ? lv_color_hex(0xF88C00) : lv_color_black();

        lv_obj_set_style_bg_color(step, backgroundColor, 0);

        auto borderColor = isActive ? lv_color_hex(0xFF0000) : lv_color_hex(0xFFFF00);

        lv_obj_set_style_border_color(step, borderColor, 0);
    }
}

void LVGL_Ui::onMainScreenLoaded(lv_event_t* event) {
    auto& self = *static_cast<LVGL_Ui*>(lv_event_get_user_data(event));
    self.drawSequencerSteps();
}

void LVGL_Ui::onStepsChanged(lv_observer_t* observer, lv_subject_t* subject) {
    auto& self = *static_cast<LVGL_Ui*>(lv_observer_get_user_data(observer));
    auto rawValue = static_cast<uint32_t>(lv_subject_get_int(subject));
    self.drawSequencerSteps(rawValue);
}
