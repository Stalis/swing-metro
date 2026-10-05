#include "program_storage_dialog.h"

#include "input/ui_display_format.h"
#include "ui_fonts.h"
#include "ui_theme.h"

namespace {
void setHidden(lv_obj_t* object, bool hidden) {
    if (hidden) {
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
}
} // namespace

void ProgramStorageDialog::create() {
    _modal = UiTheme::createModalPanel(148, 112);
    _titleLabel = UiTheme::createLabel(_modal, "SEQUENCE", 6, 5, UiTheme::CYAN, UiFonts::small());
    _valueLabel = UiTheme::createLabel(_modal, "", 8, 48, UiTheme::WHITE, UiFonts::small());

    static constexpr const char* ACTION_NAMES[] = {"SAVE", "LOAD", "CANCEL", "RESET"};
    for (std::uint8_t index = 0; index < _actionLabels.size(); ++index) {
        _actionLabels[index] = UiTheme::createLabel(
            _modal, ACTION_NAMES[index], 9, static_cast<std::int16_t>(22 + index * 20),
            index == 3 ? UiTheme::RED : UiTheme::WHITE, UiFonts::small());
        lv_obj_set_size(_actionLabels[index], 130, 15);
        setHidden(_actionLabels[index], true);
    }

    for (std::uint8_t index = 0; index < _slotLabels.size(); ++index) {
        _slotLabels[index] =
            UiTheme::createLabel(_modal, "", 9, static_cast<std::int16_t>(23 + index * 11),
                                 UiTheme::WHITE, UiFonts::small());
        lv_obj_set_size(_slotLabels[index], 119, 11);
        setHidden(_slotLabels[index], true);
    }
    _scrollTrack = lv_obj_create(_modal);
    lv_obj_remove_style_all(_scrollTrack);
    lv_obj_set_pos(_scrollTrack, 134, 23);
    lv_obj_set_size(_scrollTrack, 1, 66);
    lv_obj_set_style_bg_color(_scrollTrack, lv_color_hex(UiTheme::GRAY), 0);
    lv_obj_set_style_bg_opa(_scrollTrack, LV_OPA_COVER, 0);
    _scrollThumb = lv_obj_create(_modal);
    lv_obj_remove_style_all(_scrollThumb);
    lv_obj_set_pos(_scrollThumb, 133, 23);
    lv_obj_set_size(_scrollThumb, 3, 23);
    lv_obj_set_style_bg_color(_scrollThumb, lv_color_hex(UiTheme::CYAN), 0);
    lv_obj_set_style_bg_opa(_scrollThumb, LV_OPA_COVER, 0);
    _slotFooter = UiTheme::createLabel(_modal, "01 / 16", 9, 98, UiTheme::CYAN, UiFonts::small());
    setHidden(_scrollTrack, true);
    setHidden(_scrollThumb, true);
    setHidden(_slotFooter, true);

    _resetPanel = UiTheme::createModalPanel(120, 58);
    UiTheme::createLabel(_resetPanel, "RESET SEQ?", 6, 5, UiTheme::RED, UiFonts::small());
    UiTheme::createLabel(_resetPanel, "CURRENT ONLY", 6, 22, UiTheme::WHITE, UiFonts::small());
    _resetChoices[0] =
        UiTheme::createLabel(_resetPanel, "NO", 9, 41, UiTheme::WHITE, UiFonts::small());
    _resetChoices[1] =
        UiTheme::createLabel(_resetPanel, "YES", 72, 41, UiTheme::WHITE, UiFonts::small());
    for (auto* choice : _resetChoices) {
        lv_obj_set_size(choice, 42, 13);
    }
    setHidden(_resetPanel, true);
    setHidden(_modal, true);
}

void ProgramStorageDialog::apply(const UiSettings::Storage& settings) {
    using State = SwingMetro::ProgramStorageModalState;
    if (settings.state == _displayedState && settings.selection == _displayedSelection &&
        settings.action == _displayedAction && settings.slot == _displayedSlot &&
        settings.resetChoice == _displayedResetChoice && settings.status == _displayedStatus) {
        return;
    }
    const bool stateChanged = settings.state != _displayedState;
    const bool selectionChanged = settings.selection != _displayedSelection;
    const bool actionChanged = settings.action != _displayedAction;
    const bool slotChanged = settings.slot != _displayedSlot;
    const bool resetChoiceChanged = settings.resetChoice != _displayedResetChoice;
    const bool statusChanged = settings.status != _displayedStatus;
    const auto previousSelection = _displayedSelection;
    const auto previousSlot = _displayedSlot;
    const auto previousResetChoice = _displayedResetChoice;
    _displayedState = settings.state;
    _displayedSelection = settings.selection;
    _displayedAction = settings.action;
    _displayedSlot = settings.slot;
    _displayedResetChoice = settings.resetChoice;
    _displayedStatus = settings.status;

    const bool reset = settings.state == State::ResetConfirmation;
    if (stateChanged) {
        setHidden(_resetPanel, !reset);
        setHidden(_modal, settings.state == State::Closed || reset);
    }
    if (settings.state == State::Closed) {
        return;
    }
    if (reset) {
        for (std::uint8_t index = 0; index < 2; ++index) {
            if (stateChanged || (resetChoiceChanged &&
                                 (index == static_cast<std::uint8_t>(previousResetChoice) ||
                                  index == static_cast<std::uint8_t>(settings.resetChoice)))) {
                UiTheme::setMenuItemStyle(_resetChoices[index],
                                          index == static_cast<std::uint8_t>(settings.resetChoice));
            }
        }
        return;
    }

    const bool action = settings.state == State::Action;
    const bool slots = settings.state == State::Slot;
    if (stateChanged) {
        for (auto* label : _actionLabels) {
            setHidden(label, !action);
        }
        for (auto* label : _slotLabels) {
            setHidden(label, !slots);
        }
        setHidden(_scrollTrack, !slots);
        setHidden(_scrollThumb, !slots);
        setHidden(_slotFooter, !slots);
        setHidden(_valueLabel, action || slots);
    }

    if (action) {
        if (stateChanged) {
            lv_label_set_text(_titleLabel, "SEQUENCE");
        }
        for (std::uint8_t index = 0; index < _actionLabels.size(); ++index) {
            if (stateChanged ||
                (selectionChanged && (index == static_cast<std::uint8_t>(previousSelection) ||
                                      index == static_cast<std::uint8_t>(settings.selection)))) {
                UiTheme::setMenuItemStyle(_actionLabels[index],
                                          index == static_cast<std::uint8_t>(settings.selection));
            }
        }
    } else if (slots) {
        if (stateChanged || actionChanged) {
            lv_label_set_text(_titleLabel, settings.action == SwingMetro::ProgramStorageAction::Save
                                               ? "SAVE SLOT"
                                               : "LOAD SLOT");
        }
        const int selected = settings.slot == SwingMetro::PROGRAM_STORAGE_CANCEL_SLOT
                                 ? -1
                                 : static_cast<int>(settings.slot);
        const int start = SwingMetro::UiDisplayFormat::slotWindowStart(settings.slot);
        const int previousSelected = previousSlot == SwingMetro::PROGRAM_STORAGE_CANCEL_SLOT
                                         ? -1
                                         : static_cast<int>(previousSlot);
        const int previousStart = SwingMetro::UiDisplayFormat::slotWindowStart(previousSlot);
        const bool windowChanged = stateChanged || start != previousStart;
        const int previousRow = previousSelected - previousStart;
        const int selectedRow = selected - start;
        for (std::uint8_t row = 0; row < _slotLabels.size(); ++row) {
            if (windowChanged) {
                const int index = start + row;
                if (index < 0) {
                    lv_label_set_text(_slotLabels[row], "CANCEL");
                } else {
                    lv_label_set_text_fmt(_slotLabels[row], "SLOT %02u",
                                          static_cast<unsigned>(index + 1));
                }
            }
            if (stateChanged || (slotChanged && (row == previousRow || row == selectedRow))) {
                UiTheme::setMenuItemStyle(_slotLabels[row], row == selectedRow);
            }
        }
        if (stateChanged || slotChanged) {
            lv_label_set_text_fmt(_slotFooter, "%02u / 16",
                                  static_cast<unsigned>(selected < 0 ? 0 : selected + 1));
        }
        if (windowChanged) {
            lv_obj_set_y(_scrollThumb, static_cast<std::int16_t>(23 + (start + 1) * 43 / 11));
        }
    } else if (settings.state == State::Busy) {
        if (stateChanged) {
            lv_label_set_text(_titleLabel, "SEQUENCE");
        }
        if (stateChanged || selectionChanged || actionChanged) {
            lv_label_set_text(_valueLabel,
                              settings.selection == SwingMetro::ProgramStorageMenuItem::ResetProgram
                                  ? "RESETTING..."
                              : settings.action == SwingMetro::ProgramStorageAction::Save
                                  ? "SAVING..."
                                  : "LOADING...");
        }
    } else {
        if (stateChanged) {
            lv_label_set_text(_titleLabel, settings.state == State::Success ? "COMPLETE" : "ERROR");
        }
        if (stateChanged || statusChanged) {
            lv_label_set_text_fmt(_valueLabel, "STATUS %u", static_cast<unsigned>(settings.status));
        }
    }
}
