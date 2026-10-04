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
    _image = lv_image_create(lv_layer_top());
    lv_image_set_src(_image, &ditherImage);
    lv_obj_set_size(_image, lv_display_get_horizontal_resolution(nullptr),
                    lv_display_get_vertical_resolution(nullptr));
    lv_obj_set_pos(_image, 0, 0);
    lv_image_set_inner_align(_image, LV_IMAGE_ALIGN_TILE);
    lv_obj_add_flag(_image, LV_OBJ_FLAG_HIDDEN);
}

void ModalBackdrop::setVisible(bool visible) {
    if (visible) {
        lv_obj_remove_flag(_image, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_image, LV_OBJ_FLAG_HIDDEN);
    }
}
