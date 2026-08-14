#pragma once

#include "lvgl.h"

// Sets up the ST7789 panel, the LVGL display driver (full-screen render) and
// the LVGL timer task. Call once at boot.
void lvgl_port_init(void);
// Register a function called from the LVGL task each loop (~5 ms); the UI uses
// it to poll the buttons and run its screen state machine.
void lvgl_port_set_poll_cb(void (*cb)(void));