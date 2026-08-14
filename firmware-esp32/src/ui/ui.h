#pragma once

#include <stdbool.h>
#include <stdint.h>

#define LCD_H_RES 240
#define LCD_V_RES 135

#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF
#define COLOR_GREY  0x8410
#define COLOR_GREEN 0x07E0

void ui_init(void);
void ui_draw_initial(void);
void ui_draw_button_state(int btn, bool pressed);
void ui_draw_temp_line(int temp_tenths); // tenths of C; <0 = open/error
void ui_flush(void);