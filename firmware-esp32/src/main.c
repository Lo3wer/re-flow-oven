#include <stdbool.h>
#include <stdint.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buttons.h"
#include "controller.h"
#include "history.h"
#include "profile.h"
#include "relay.h"
#include "safety.h"
#include "thermocouple.h"
#include "ui.h"

// PID settings (tune these). Temperature is measured in deg C; the output is a
// PWM duty 0..RELAY_PWM_MAX.
#define PID_KP 8.0f  // duty per deg C of error
#define PID_KI 0.05f // duty per (deg C * s) of accumulated error
#define PID_KD 20.0f // duty per (deg C/s) of error change

// Placeholder default profile; edit via the UI (Phase 2) or NVS later.
static reflow_profile_t s_profile = {
    .name = "GENERIC",
    .num_phases = 5,
    .phases = {
        { "RAMP", 2.0f, 150.0f, 0.0f },
        { "SOAK", 2.0f, 200.0f, 60.0f },
        { "RAMP", 2.0f, 245.0f, 0.0f },
        { "PEAK", 0.5f, 245.0f, 30.0f },
        { "COOL", 1.0f, 80.0f, 0.0f },
    },
};

static controller_t s_ctrl;
static history_t s_history;

static const char *fault_note(safety_fault_t f)
{
    switch (f) {
        case SAFETY_OVERTEMP:       return "FAULT OTEMP";
        case SAFETY_RUN_AWAY:       return "FAULT RUNAWAY";
        case SAFETY_RISE_WHILE_OFF: return "FAULT STUCK";
        case SAFETY_SENSOR:         return "FAULT SENSOR";
        case SAFETY_RUN_TIMEOUT:    return "FAULT TIMEOUT";
        case SAFETY_OK:             break;
    }
    return NULL;
}

static void ui_task(void *arg)
{
    btn_evt_t evt;
    while (1) {
        if (buttons_wait_event(&evt, portMAX_DELAY)) {
            switch (evt.btn) {
                case 0: ctrl_post(&s_ctrl, CTRL_CMD_START, 0); break; // run/stop
                case 1: ctrl_post(&s_ctrl, CTRL_CMD_ACK_FAULT, 0); break;
                default: break;
            }
            ui_draw_button_state(evt.btn, evt.pressed);
        }
    }
}

static void temp_task(void *arg)
{
    safety_config_t scfg = {
        .max_temp_c = 300.0f,
        .max_rise_c_per_s = 8.0f,
        .runaway_hold_s = 2.0f,
        .duty_off_threshold = 0.03f,
        .rise_while_off_c = 8.0f,
        .sensor_fault_count = 3,
        .max_run_s = 600.0f, // 10 min watchdog
    };
    ctrl_init(&s_ctrl, &scfg, &s_profile, PID_KP, PID_KI, PID_KD);
    history_init(&s_history);

    int64_t last = esp_timer_get_time();
    while (1) {
        float dt = (esp_timer_get_time() - last) / 1e6f;
        last = esp_timer_get_time();

        int temp = tc_read_tenths();
        bool sensor_ok = temp >= 0;
        float temp_c = sensor_ok ? temp / 10.0f : 0.0f;

        float duty = ctrl_tick(&s_ctrl, sensor_ok, temp_c, dt);
        relay_set_duty((uint32_t)duty);

        if (sensor_ok) {
            history_push(&s_history, (uint32_t)(esp_timer_get_time() / 1000), temp_c);
        }

        const char *note = fault_note(ctrl_fault(&s_ctrl));
        if (!note) {
            switch (ctrl_state(&s_ctrl)) {
                case CTRL_STATE_IDLE: note = "IDLE"; break;
                case CTRL_STATE_DONE: note = "DONE"; break;
                case CTRL_STATE_RUN:  note = ctrl_phase_name(&s_ctrl); break;
            }
        }

        ui_draw_temp_line(temp, note);
        ui_flush();
        vTaskDelay(pdMS_TO_TICKS(500)); // MAX6675 conversion time ~220 ms
    }
}

void app_main(void)
{
    ui_init();
    tc_init();
    buttons_init();
    relay_init();
    ui_draw_initial();
    xTaskCreate(ui_task, "ui", 4096, NULL, 5, NULL);
    xTaskCreate(temp_task, "temp", 4096, NULL, 4, NULL);
}