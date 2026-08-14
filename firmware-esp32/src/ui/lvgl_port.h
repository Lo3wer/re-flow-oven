#pragma once

#include "lvgl.h"

// Sets up the ST7789 panel, the LVGL display driver (full-screen render) and
// the draw buffer. Call once at boot. Does NOT start the render task.
void lvgl_port_init(void);
// Starts the LVGL render task. Call only after the UI widgets are fully built:
// LVGL is not thread-safe, so nothing may touch LVGL while this task runs.
void lvgl_port_start(void);
// Register a function called from the LVGL task each loop (~5 ms); the UI uses
// it to poll the buttons and run its screen state machine.
void lvgl_port_set_poll_cb(void (*cb)(void));