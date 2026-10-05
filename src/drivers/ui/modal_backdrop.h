#pragma once

#include <array>
#include <cstdint>
#include <lvgl.h>

class ModalBackdrop {
  public:
    void create();
    bool capture(lv_obj_t* source);
    void setVisible(bool visible);

  private:
    static constexpr std::uint16_t WIDTH = 160;
    static constexpr std::uint16_t HEIGHT = 128;

    alignas(LV_DRAW_BUF_ALIGN) std::array<
        std::uint8_t, LV_DRAW_BUF_SIZE(WIDTH, HEIGHT, LV_COLOR_FORMAT_RGB565)> _snapshotPixels{};
    lv_draw_buf_t _snapshot{};
    bool _snapshotReady = false;
    lv_obj_t* _image = nullptr;
};
