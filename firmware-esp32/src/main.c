#include <stdbool.h>
#include <stdint.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buttons.h"
#include "pid.h"
#include "relay.h"
#include "safety.h"
#include "thermocouple.h"
#include "ui.h"

// PID settings (tune these). Temperature is measured in deg C; the output is a
// PWM duty 0..RELAY_PWM_MAX. Setpoint is a typical reflow peak temperature.
#define PID_KP         8.0f  // duty per deg C of error
#define PID_KI         0.05f // duty per (deg C * s) of accumulated error
#define PID_KD         20.0f // duty per (deg C/s) of error change
#define PID_SETPOINT_C 245.0f

static pid_t s_pid;
static safety_t s_safety;
static volatile bool s_reset_requested;

static const char *fault_note(safety_fault_t f)
{
    switch (f) {
        case SAFETY_OVERTEMP:       return "FAULT OTEMP";
        case SAFETY_RUN_AWAY:       return "FAULT RUNAWAY";
        case SAFETY_RISE_WHILE_OFF: return "FAULT STUCK";
        case SAFETY_SENSOR:         return "FAULT SENSOR";
        case SAFETY_OK:             break;
    }
    return NULL;
}

static void ui_task(void *arg)
{
    btn_evt_t evt;
    while (1) {
        if (buttons_wait_event(&evt, portMAX_DELAY)) {
            if (safety_fault(&s_safety) != SAFETY_OK) {
                s_reset_requested = true; // any button clears a latched fault
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
    };
    safety_init(&s_safety, &scfg);
    pid_init(&s_pid, PID_KP, PID_KI, PID_KD, PID_SETPOINT_C, 0.0f, RELAY_PWM_MAX);

    int64_t last = esp_timer_get_time();
    while (1) {
        if (s_reset_requested) {
            s_reset_requested = false;
            safety_reset(&s_safety);
        }

        float dt = (esp_timer_get_time() - last) / 1e6f;
        last = esp_timer_get_time();

        int temp = tc_read_tenths();
        bool sensor_ok = temp >= 0;
        float duty = 0.0f;
        if (sensor_ok) {
            duty = pid_update(&s_pid, temp / 10.0f, dt);
        }

        bool allow = safety_update(&s_safety, sensor_ok, sensor_ok ? temp / 10.0f : 0.0f,
                                   duty / RELAY_PWM_MAX, dt);
        if (!allow) {
            pid_reset(&s_pid);
            relay_set_duty(0);
        } else {
            relay_set_duty((uint32_t)duty);
        }

        ui_draw_temp_line(temp, fault_note(safety_fault(&s_safety)));
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