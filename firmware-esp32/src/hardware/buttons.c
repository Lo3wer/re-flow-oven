#include "buttons.h"

#include "driver/gpio.h"
#include "esp_err.h"

// ---- Buttons (active-low to GND, polled by the LVGL keypad indev).
// BTN1 = GPIO35 (right onboard button, on-board pull-up),
// BTN2 = GPIO0  (left onboard button / BOOT, internal pull-up enabled below),
// BTN3 = GPIO25 (external button, internal pull-up). ----
static const gpio_num_t s_pins[BTN_COUNT] = { GPIO_NUM_35, GPIO_NUM_0, GPIO_NUM_25 };

void buttons_init(void)
{
    for (int i = 0; i < BTN_COUNT; i++) {
        const gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << s_pins[i],
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&cfg));
    }
}

bool buttons_is_pressed(int btn)
{
    if (btn < 0 || btn >= BTN_COUNT) {
        return false;
    }
    return gpio_get_level(s_pins[btn]) == 0;
}