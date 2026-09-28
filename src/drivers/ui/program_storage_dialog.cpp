#include "program_storage_dialog.h"

#include "ui_theme.h"

void ProgramStorageDialog::create() {
    _modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_modal, 120, 118);
    lv_obj_align(_modal, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(_modal, lv_color_hex(UiTheme::DARK_GRAY), 0);
    lv_obj_set_style_bg_opa(_modal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(_modal, lv_color_hex(UiTheme::CYAN), 0);
    lv_obj_set_style_border_width(_modal, 2, 0);
    lv_obj_remove_flag(_modal, LV_OBJ_FLAG_SCROLLABLE);
    _titleLabel = lv_label_create(_modal);
    lv_obj_align(_titleLabel, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_text_color(_titleLabel, lv_color_hex(UiTheme::WHITE), 0);
    _valueLabel = lv_label_create(_modal);
    lv_obj_align(_valueLabel, LV_ALIGN_CENTER, 0, 12);
    lv_obj_set_style_text_color(_valueLabel, lv_color_hex(UiTheme::WHITE), 0);

    static constexpr const char* actionNames[] = {"Save", "Load", "Cancel", "Reset program"};
    for (uint8_t index = 0; index < _actionLabels.size(); ++index) {
        auto* label = lv_label_create(_modal);
        _actionLabels[index] = label;
        lv_label_set_text(label, actionNames[index]);
        lv_obj_set_width(label, 104);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(UiTheme::WHITE), 0);
        lv_obj_align(label, LV_ALIGN_TOP_MID, 0, static_cast<int16_t>(30 + index * 18));
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(_modal, LV_OBJ_FLAG_HIDDEN);
}

void ProgramStorageDialog::apply(const UiSettings::Storage& settings) {
    if (settings.state == _displayedState && settings.selection == _displayedSelection &&
        settings.action == _displayedAction && settings.slot == _displayedSlot &&
        settings.resetChoice == _displayedResetChoice && settings.status == _displayedStatus) {
        return;
    }
    _displayedState = settings.state;
    _displayedSelection = settings.selection;
    _displayedAction = settings.action;
    _displayedSlot = settings.slot;
    _displayedResetChoice = settings.resetChoice;
    _displayedStatus = settings.status;
    if (settings.state == SwingMetro::ProgramStorageModalState::Closed) {
        lv_obj_add_flag(_modal, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(_modal, LV_OBJ_FLAG_HIDDEN);
    if (settings.state == SwingMetro::ProgramStorageModalState::Action) {
        lv_label_set_text(_titleLabel, "Save / Load");
        lv_obj_add_flag(_valueLabel, LV_OBJ_FLAG_HIDDEN);
        for (uint8_t index = 0; index < _actionLabels.size(); ++index) {
            const bool selected = index == static_cast<uint8_t>(settings.selection);
            lv_obj_remove_flag(_actionLabels[index], LV_OBJ_FLAG_HIDDEN);
            UiTheme::setMenuItemStyle(_actionLabels[index], selected);
        }
    } else if (settings.state == SwingMetro::ProgramStorageModalState::Slot) {
        lv_obj_remove_flag(_valueLabel, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(_titleLabel, settings.action == SwingMetro::ProgramStorageAction::Save
                                           ? "Save slot"
                                           : "Load slot");
        if (settings.slot == SwingMetro::PROGRAM_STORAGE_CANCEL_SLOT) {
            lv_label_set_text(_valueLabel, "Cancel");
        } else {
            lv_label_set_text_fmt(_valueLabel, "Slot %u", static_cast<unsigned>(settings.slot));
        }
    } else if (settings.state == SwingMetro::ProgramStorageModalState::ResetConfirmation) {
        lv_obj_remove_flag(_valueLabel, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(_titleLabel, "Reset program?");
        lv_label_set_text(
            _valueLabel, settings.resetChoice == SwingMetro::ProgramResetChoice::No ? "No" : "Yes");
    } else if (settings.state == SwingMetro::ProgramStorageModalState::Busy) {
        lv_obj_remove_flag(_valueLabel, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(_titleLabel, "Program Storage");
        if (settings.selection == SwingMetro::ProgramStorageMenuItem::ResetProgram) {
            lv_label_set_text(_valueLabel, "Resetting...");
        } else {
            lv_label_set_text(_valueLabel, settings.action == SwingMetro::ProgramStorageAction::Save
                                               ? "Saving..."
                                               : "Loading...");
        }
    } else {
        lv_obj_remove_flag(_valueLabel, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(
            _titleLabel,
            settings.state == SwingMetro::ProgramStorageModalState::Success ? "Complete" : "Error");
        lv_label_set_text_fmt(_valueLabel, "Status %u", static_cast<unsigned>(settings.status));
    }
    if (settings.state != SwingMetro::ProgramStorageModalState::Action) {
        for (auto* label : _actionLabels) {
            lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}
