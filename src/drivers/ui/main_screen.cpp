#include "main_screen.h"

#include "ui_theme.h"

void MainScreen::create() {
    lv_subject_init_int(&_tempoSubject, 120);
    lv_subject_set_max_value_int(&_tempoSubject, 240);
    lv_subject_set_min_value_int(&_tempoSubject, 40);

    lv_subject_init_int(&_swingSubject, 50);
    lv_subject_set_min_value_int(&_swingSubject, 50);
    lv_subject_set_max_value_int(&_swingSubject, 100);

    lv_subject_init_int(&_volumeSubject, 100);
    lv_subject_set_min_value_int(&_volumeSubject, 0);
    lv_subject_set_max_value_int(&_volumeSubject, 100);

    _root = lv_obj_create(nullptr);
    lv_obj_set_style_text_font(_root, &lv_font_montserrat_12, 0);
    UiTheme::setScreenStyle(_root);

    auto* tempoLabel = lv_label_create(_root);
    lv_obj_set_pos(tempoLabel, 10, 10);
    lv_obj_set_style_text_color(tempoLabel, lv_color_hex(UiTheme::RED), 0);
    lv_label_bind_text(tempoLabel, &_tempoSubject, "Tempo: %d");

    auto* swingLabel = lv_label_create(_root);
    lv_obj_set_pos(swingLabel, 10, 30);
    lv_obj_set_style_text_color(swingLabel, lv_color_hex(UiTheme::GREEN), 0);
    lv_label_bind_text(swingLabel, &_swingSubject, "Swing: %d");

    auto* volumeLabel = lv_label_create(_root);
    lv_obj_set_pos(volumeLabel, 10, 50);
    lv_obj_set_style_text_color(volumeLabel, lv_color_hex(UiTheme::BLUE), 0);
    lv_label_bind_text(volumeLabel, &_volumeSubject, "Volume: %d");

    _externalClockLabel = lv_label_create(_root);
    lv_obj_set_pos(_externalClockLabel, 10, 70);
    lv_obj_set_style_text_color(_externalClockLabel, lv_color_hex(UiTheme::CYAN), 0);
    lv_label_set_text(_externalClockLabel, "Clock: Waiting");

    lv_obj_add_event_cb(_root, onScreenLoaded, LV_EVENT_SCREEN_LOADED, this);
    _stepGrid.init(_root);
}

void MainScreen::apply(const UiSettings::Main& settings) {
    lv_subject_set_int(&_tempoSubject, settings.tempo);
    lv_subject_set_int(&_swingSubject, settings.swing);
    lv_subject_set_int(&_volumeSubject, settings.volume);
    _stepGrid.setSteps(settings.notesState, settings.activeNote);
    setExternalClock(settings.externalClockStatus, settings.externalTempo);
}

void MainScreen::setExternalClock(SwingMetro::ExternalMidiClockStatus status, uint8_t tempo) {
    if (status == _displayedExternalClockStatus && tempo == _displayedExternalTempo) {
        return;
    }
    _displayedExternalClockStatus = status;
    _displayedExternalTempo = tempo;
    static constexpr const char* names[] = {"Waiting", "Locked", "Lost"};
    if (tempo == 0) {
        lv_label_set_text_fmt(_externalClockLabel, "Clock: %s",
                              names[static_cast<uint8_t>(status)]);
    } else {
        lv_label_set_text_fmt(_externalClockLabel, "Clock: %s %u BPM",
                              names[static_cast<uint8_t>(status)], static_cast<unsigned>(tempo));
    }
}

void MainScreen::onScreenLoaded(lv_event_t* event) {
    auto& self = *static_cast<MainScreen*>(lv_event_get_user_data(event));
    self._stepGrid.draw();
}
