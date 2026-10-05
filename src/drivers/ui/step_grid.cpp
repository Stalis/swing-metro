#include "step_grid.h"

#include "input/ui_step_display_state.h"
#include "ui_fonts.h"
#include "ui_note_name.h"
#include "ui_theme.h"

void StepGrid::init(lv_obj_t* parent) {
    constexpr std::int16_t cellWidth = 36;
    constexpr std::int16_t cellHeight = 23;
    constexpr std::int16_t pitchX = 39;
    constexpr std::int16_t pitchY = 26;

    for (std::uint8_t index = 0; index < _cells.size(); ++index) {
        const std::int16_t x = 3 + (index % 4) * pitchX;
        const std::int16_t y = 24 + (index / 4) * pitchY;
        auto& cell = _cells[index];
        cell.frame = lv_obj_create(parent);
        lv_obj_remove_style_all(cell.frame);
        lv_obj_set_pos(cell.frame, x, y);
        lv_obj_set_size(cell.frame, cellWidth, cellHeight);
        lv_obj_set_style_bg_opa(cell.frame, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(cell.frame, 1, 0);
        lv_obj_set_style_radius(cell.frame, 0, 0);
        lv_obj_set_style_pad_all(cell.frame, 0, 0);
        lv_obj_remove_flag(cell.frame, LV_OBJ_FLAG_SCROLLABLE);

        cell.number =
            UiTheme::createLabel(cell.frame, "01", 2, 1, UiTheme::CYAN, UiFonts::medium());
        cell.note = UiTheme::createLabel(cell.frame, "C0", 15, 1, UiTheme::WHITE, UiFonts::large());
        cell.velocityLabel =
            UiTheme::createLabel(cell.frame, "VEL", 2, 10, UiTheme::CYAN, UiFonts::small());
        cell.velocity =
            UiTheme::createLabel(cell.frame, "127", 24, 10, UiTheme::WHITE, UiFonts::small());
        cell.gateLabel =
            UiTheme::createLabel(cell.frame, "GATE", 2, 16, UiTheme::CYAN, UiFonts::small());
        cell.gate =
            UiTheme::createLabel(cell.frame, "100%", 20, 16, UiTheme::WHITE, UiFonts::small());
        cell.off =
            UiTheme::createLabel(cell.frame, "OFF", 8, 9, UiTheme::YELLOW, UiFonts::medium());
    }
}

void StepGrid::apply(const UiSettings::Main& settings) {
    for (std::uint8_t index = 0; index < _cells.size(); ++index) {
        const bool enabled = settings.notesState[index];
        const bool withinLength = index < settings.sequenceLength;
        const bool active = index == settings.activeNote;
        const auto visualState = SwingMetro::uiStepDisplayState(enabled, active, withinLength);
        const std::uint64_t renderKey =
            static_cast<std::uint64_t>(settings.stepNotes[index]) |
            static_cast<std::uint64_t>(settings.stepVelocities[index]) << 8 |
            static_cast<std::uint64_t>(settings.stepGates[index]) << 16 |
            static_cast<std::uint64_t>(visualState) << 24;
        auto& cell = _cells[index];
        if (cell.renderKey == renderKey) {
            continue;
        }
        cell.renderKey = renderKey;

        const bool showEnabled = visualState == SwingMetro::UiStepDisplayState::Enabled ||
                                 visualState == SwingMetro::UiStepDisplayState::EnabledActive;
        const std::uint32_t border =
            visualState == SwingMetro::UiStepDisplayState::EnabledActive ? UiTheme::RED
            : visualState == SwingMetro::UiStepDisplayState::Enabled     ? UiTheme::CYAN
            : visualState == SwingMetro::UiStepDisplayState::Off         ? UiTheme::YELLOW
                                                                         : UiTheme::GRAY;
        lv_obj_set_style_border_color(cell.frame, lv_color_hex(border), 0);
        lv_obj_set_style_bg_color(
            cell.frame, lv_color_hex(showEnabled ? UiTheme::DARK_TEAL : UiTheme::BLACK), 0);

        lv_label_set_text_fmt(cell.number, "%02u", static_cast<unsigned>(index + 1));
        lv_obj_set_style_text_color(
            cell.number,
            lv_color_hex(visualState != SwingMetro::UiStepDisplayState::Disabled ? UiTheme::CYAN
                                                                                 : UiTheme::GRAY),
            0);
        auto setHidden = [](lv_obj_t* object, bool hidden) {
            if (hidden) {
                lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_remove_flag(object, LV_OBJ_FLAG_HIDDEN);
            }
        };
        setHidden(cell.note, !showEnabled);
        setHidden(cell.velocityLabel, !showEnabled);
        setHidden(cell.velocity, !showEnabled);
        setHidden(cell.gateLabel, !showEnabled);
        setHidden(cell.gate, !showEnabled);
        setHidden(cell.off, showEnabled || !withinLength);
        if (showEnabled) {
            char note[8]{};
            UiNoteName::format(settings.stepNotes[index], note, sizeof(note));
            lv_label_set_text(cell.note, note);
            lv_label_set_text_fmt(cell.velocity, "%03u",
                                  static_cast<unsigned>(settings.stepVelocities[index]));
            lv_label_set_text_fmt(cell.gate, "%u%%",
                                  static_cast<unsigned>(settings.stepGates[index]));
            lv_obj_align(cell.note, LV_ALIGN_TOP_RIGHT, -2, 1);
            lv_obj_align(cell.velocity, LV_ALIGN_TOP_RIGHT, -2, 10);
            lv_obj_align(cell.gate, LV_ALIGN_TOP_RIGHT, -2, 16);
        } else if (!withinLength) {
            lv_label_set_text(cell.off, "--");
            lv_obj_set_style_text_color(cell.off, lv_color_hex(UiTheme::GRAY), 0);
            lv_obj_remove_flag(cell.off, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(cell.off, "OFF");
            lv_obj_set_style_text_color(cell.off, lv_color_hex(UiTheme::YELLOW), 0);
        }
    }
}
