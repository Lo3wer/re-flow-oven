#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buttons.h"
#include "relay.h"
#include "thermocouple.h"
#include "ui.h"

#define RELAY_ON_TENTHS 320 // relay on above 32.0 C, off otherwise

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
    while (1) {
        int temp = tc_read_tenths();

        // Simple on/off control for now; a PID will replace this later.
        relay_set_duty(temp > RELAY_ON_TENTHS ? RELAY_PWM_MAX : 0);

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