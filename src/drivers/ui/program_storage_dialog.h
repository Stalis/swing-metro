#pragma once

#include <array>
#include <cstdint>

#include <lvgl.h>

#include "components/ui_snapshot.h"

class ProgramStorageDialog {
  public:
    ProgramStorageDialog() = default;

    ProgramStorageDialog(const ProgramStorageDialog&) = delete;
    ProgramStorageDialog& operator=(const ProgramStorageDialog&) = delete;
    ProgramStorageDialog(ProgramStorageDialog&&) = delete;
    ProgramStorageDialog& operator=(ProgramStorageDialog&&) = delete;

    void create();
    void apply(const UiSettings::Storage& settings);

  private:
    lv_obj_t* _modal = nullptr;
    lv_obj_t* _titleLabel = nullptr;
    lv_obj_t* _valueLabel = nullptr;
    std::array<lv_obj_t*, 4> _actionLabels{};
    SwingMetro::ProgramStorageModalState _displayedState =
        SwingMetro::ProgramStorageModalState::Closed;
    SwingMetro::ProgramStorageAction _displayedAction = SwingMetro::ProgramStorageAction::Save;
    SwingMetro::ProgramStorageMenuItem _displayedSelection =
        SwingMetro::ProgramStorageMenuItem::Save;
    uint8_t _displayedSlot = UINT8_MAX;
    SwingMetro::ProgramResetChoice _displayedResetChoice = SwingMetro::ProgramResetChoice::No;
    SwingMetro::ProgramStoreStatus _displayedStatus = SwingMetro::ProgramStoreStatus::Ok;
};
