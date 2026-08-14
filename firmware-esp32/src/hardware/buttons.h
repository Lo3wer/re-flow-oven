#pragma once

#include <stdbool.h>

#define BTN_COUNT 3

void buttons_init(void);
// True when the button is currently held (active-low, internal pull-up).
bool buttons_is_pressed(int btn);