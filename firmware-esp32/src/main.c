#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "controller.h"
#include "history.h"
#include "net/server.h"
#include "net/wifi.h"
#include "relay.h"
#include "safety.h"
#include "storage/config.h"
#include "thermocouple.h"
#include "ui.h"

#define TAG "main"

// PID settings (tune these). Temperature is measured in deg C; the output is a
// PWM duty 0..RELAY_PWM_MAX.
#define PID_KP 8.0f  // duty per deg C of error
#define PID_KI 0.05f // duty per (deg C * s) of accumulated error
#define PID_KD 20.0f // duty per (deg C/s) of error change

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

static const char *state_text(void)
{
    safety_fault_t f = ctrl_fault(&s_ctrl);
    if (f != SAFETY_OK) {
        return fault_note(f);
    }
    switch (ctrl_state(&s_ctrl)) {
        case CTRL_STATE_IDLE: return "IDLE";
        case CTRL_STATE_DONE: return "DONE";
        case CTRL_STATE_RUN: {
            const char *ph = ctrl_phase_name(&s_ctrl);
            return ph && *ph ? ph : "RUN";
        }
    }
    return "";
}

static void on_ui_cmd(ui_cmd_t cmd)
{
    switch (cmd) {
        case UI_CMD_START: ctrl_post(&s_ctrl, CTRL_CMD_START, 0); break;
        case UI_CMD_STOP:  ctrl_post(&s_ctrl, CTRL_CMD_STOP, 0); break;
        case UI_CMD_ACK:   ctrl_post(&s_ctrl, CTRL_CMD_ACK_FAULT, 0); break;
    }
}

static void temp_task(void *arg)
{
    (void)arg;
    safety_config_t scfg = {
        .max_temp_c = 300.0f,
        .max_rise_c_per_s = 8.0f,
        .runaway_hold_s = 2.0f,
        .duty_off_threshold = 0.03f,
        .rise_while_off_c = 8.0f,
        .sensor_fault_count = 3,
        .max_run_s = 600.0f, // 10 min watchdog
    };
    ctrl_init(&s_ctrl, &scfg, &config_profiles()[config_selected()], PID_KP, PID_KI, PID_KD);
    history_init(&s_history);

    int64_t last = esp_timer_get_time();
    while (1) {
        float dt = (esp_timer_get_time() - last) / 1e6f;
        last = esp_timer_get_time();

        // Keep the controller pointed at the profile the user has selected/edited.
        ctrl_set_profile(&s_ctrl, &config_profiles()[config_selected()]);

        int temp = tc_read_tenths();
        bool sensor_ok = temp >= 0;
        float temp_c = sensor_ok ? temp / 10.0f : 0.0f;

        float duty = ctrl_tick(&s_ctrl, sensor_ok, temp_c, dt);
        relay_set_duty((uint32_t)duty);

        if (sensor_ok) {
            history_push(&s_history, (uint32_t)(esp_timer_get_time() / 1000), temp_c);
        }

        ui_set_temp(sensor_ok ? temp_c : 0.0f, !sensor_ok);
        ui_set_state_text(state_text());
        ui_set_running(ctrl_state(&s_ctrl) == CTRL_STATE_RUN);
        ui_set_setpoint(ctrl_setpoint(&s_ctrl));
        ui_set_duty_pct(duty / (float)RELAY_PWM_MAX * 100.0f);
        ui_set_phase(ctrl_phase_progress(&s_ctrl));

        vTaskDelay(pdMS_TO_TICKS(500)); // MAX6675 conversion time ~220 ms
    }
}

void app_main(void)
{
    config_init();
    ESP_LOGI(TAG, "config ok");
    ui_init();
    ESP_LOGI(TAG, "ui ok");
    tc_init();
    relay_init();
    ui_set_cmd_cb(on_ui_cmd);
    wifi_init();
    ESP_LOGI(TAG, "wifi ok");
    server_init(&s_ctrl, &s_history);
    ESP_LOGI(TAG, "server ok");
    xTaskCreate(temp_task, "temp", 8192, NULL, 4, NULL);
}