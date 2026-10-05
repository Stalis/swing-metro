#include "modal_backdrop.h"

#include <cstdint>

namespace {

// Two opaque black pixels and two transparent pixels: 1 px, 50% checkerboard.
const std::uint8_t ditherPixels[] = {
    0, 0, 0, 255, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255,
};

const lv_image_dsc_t ditherImage = {
    .header = {.cf = LV_COLOR_FORMAT_ARGB8888, .w = 2, .h = 2, .stride = 8},
    .data_size = sizeof(ditherPixels),
    .data = ditherPixels,
};

} // namespace

void ModalBackdrop::create() {
    _snapshotReady =
        lv_draw_buf_init(&_snapshot, WIDTH, HEIGHT, LV_COLOR_FORMAT_RGB565, 0,
                         _snapshotPixels.data(), _snapshotPixels.size()) == LV_RESULT_OK;
    if (_snapshotReady) {
        lv_draw_buf_set_flag(&_snapshot, LV_IMAGE_FLAGS_MODIFIABLE);
    }
    _image = lv_image_create(lv_layer_top());
    lv_image_set_src(_image, &ditherImage);
    lv_obj_set_size(_image, lv_display_get_horizontal_resolution(nullptr),
                    lv_display_get_vertical_resolution(nullptr));
    lv_obj_set_pos(_image, 0, 0);
    lv_image_set_inner_align(_image, LV_IMAGE_ALIGN_TILE);
    lv_obj_add_flag(_image, LV_OBJ_FLAG_HIDDEN);
}

bool ModalBackdrop::capture(lv_obj_t* source) {
    if (!_snapshotReady ||
        lv_snapshot_take_to_draw_buf(source, LV_COLOR_FORMAT_RGB565, &_snapshot) != LV_RESULT_OK) {
        lv_image_set_src(_image, &ditherImage);
        lv_image_set_inner_align(_image, LV_IMAGE_ALIGN_TILE);
        return false;
    }

    for (std::uint16_t y = 0; y < HEIGHT; ++y) {
        auto* row = reinterpret_cast<std::uint16_t*>(_snapshot.data + y * _snapshot.header.stride);
        for (std::uint16_t x = 0; x < WIDTH; ++x) {
            if (((x ^ y) & 1U) == 0U) {
                row[x] = 0;
            }
        }
    }
    lv_draw_buf_flush_cache(&_snapshot, nullptr);
    lv_image_set_src(_image, &_snapshot);
    lv_image_set_inner_align(_image, LV_IMAGE_ALIGN_CENTER);
    return true;
}

void ModalBackdrop::setVisible(bool visible) {
    if (visible) {
        lv_obj_remove_flag(_image, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_image, LV_OBJ_FLAG_HIDDEN);
    }
}
