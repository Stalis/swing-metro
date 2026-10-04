#pragma once

#include <lvgl.h>

class ModalBackdrop {
  public:
    void create();
    void setVisible(bool visible);

  private:
    lv_obj_t* _image = nullptr;
};
