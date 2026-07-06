#ifndef __DISPLAY_H
#define __DISPLAY_H

#include "lvgl.h"

class Display
{
public:
    lv_display_t* init();
    void setTouchCallback(void (*cb)());
};

#endif
