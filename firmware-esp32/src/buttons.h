#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"

#define BTN_COUNT 3

typedef struct {
    uint8_t btn;     // 0..BTN_COUNT-1
    bool    pressed; // true if the pin is low (active-low)
} btn_evt_t;

void buttons_init(void);
// Block up to `ticks`, return true on a debounced, state-changing press/release.
bool buttons_wait_event(btn_evt_t *evt, TickType_t ticks);