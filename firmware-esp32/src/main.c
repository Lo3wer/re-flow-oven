#include <stdbool.h>
#include <stdint.h>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buttons.h"
#include "pid.h"
#include "relay.h"
#include "thermocouple.h"
#include "ui.h"

// PID settings (tune these). Temperature is measured in deg C; the output is a
// PWM duty 0..RELAY_PWM_MAX. Setpoint is a typical reflow peak temperature.
#define PID_KP         8.0f  // duty per deg C of error
#define PID_KI         0.05f // duty per (deg C * s) of accumulated error
#define PID_KD         20.0f // duty per (deg C/s) of error change
#define PID_SETPOINT_C 245.0f

static pid_t s_pid;

static void ui_task(void *arg)
{
    btn_evt_t evt;
    while (1) {
        if (buttons_wait_event(&evt, portMAX_DELAY)) {
            ui_draw_button_state(evt.btn, evt.pressed);
        }
    }
}

static void temp_task(void *arg)
{
    pid_init(&s_pid, PID_KP, PID_KI, PID_KD, PID_SETPOINT_C, 0.0f, RELAY_PWM_MAX);

    int64_t last = esp_timer_get_time();
    while (1) {
        float dt = (esp_timer_get_time() - last) / 1e6f;
        last = esp_timer_get_time();

        int temp = tc_read_tenths();
        if (temp >= 0) {
            float duty = pid_update(&s_pid, temp / 10.0f, dt);
            relay_set_duty((uint32_t)duty);
        } else {
            pid_reset(&s_pid); // no valid reading: drop integrator, kill heater
            relay_set_duty(0);
        }

        ui_draw_temp_line(temp);
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