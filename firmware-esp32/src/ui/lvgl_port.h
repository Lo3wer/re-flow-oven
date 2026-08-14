#pragma once

#include "lvgl.h"

// Sets up the ST7789 panel, the LVGL display driver, the keypad input device
// (driven by the 3 GPIO buttons) and the LVGL timer task. Call once at boot.
void lvgl_port_init(void);
// Assign the LVGL focus group to the keypad indev (defaults to the default group).
void lvgl_port_set_group(lv_group_t *group);