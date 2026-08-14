#pragma once

#include <stdbool.h>

typedef enum {
    UI_CMD_START,
    UI_CMD_STOP,
    UI_CMD_ACK,
} ui_cmd_t;

typedef void (*ui_cmd_cb_t)(ui_cmd_t cmd);

void ui_init(void);
void ui_set_cmd_cb(ui_cmd_cb_t cb);
void ui_set_temp(float temp_c, bool sensor_open);
void ui_set_state_text(const char *text); // "IDLE"/"RUN ..."/"DONE"/"FAULT ..."
void ui_set_running(bool running);
void ui_set_phase(float progress01); // 0..1 profile progress
void ui_set_setpoint(float sp_c);
void ui_set_duty_pct(float pct);