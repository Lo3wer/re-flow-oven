#include "buttons.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// ---- Buttons (active-low, edge-triggered ISR). Wiring: each button between
// the pin and GND; released = high, pressed = low.
// BTN1 = GPIO35 (right onboard button, on-board pull-up),
// BTN2 = GPIO0  (left onboard button / BOOT, internal pull-up enabled below),
// BTN3 = GPIO25 (external button, internal pull-up). ----
static const gpio_num_t s_pins[BTN_COUNT] = { GPIO_NUM_35, GPIO_NUM_0, GPIO_NUM_25 };
static bool s_last_state[BTN_COUNT] = { false, false, false };
static QueueHandle_t s_queue;

typedef struct {
    uint8_t btn;     // 0..BTN_COUNT-1
    bool    pressed; // true if the pin was low at the ISR edge
} isr_evt_t;

// Runs in ISR context: only reads the pin and posts to a queue.
static void IRAM_ATTR button_isr(void *arg)
{
    int btn = (int)(intptr_t)arg;
    isr_evt_t evt = {
        .btn = btn,
        .pressed = (gpio_get_level(s_pins[btn]) == 0),
    };
    BaseType_t hpw = pdFALSE;
    xQueueSendFromISR(s_queue, &evt, &hpw);
    portYIELD_FROM_ISR(hpw);
}

void buttons_init(void)
{
    for (int i = 0; i < BTN_COUNT; i++) {
        const gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << s_pins[i],
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_ANYEDGE,
        };
        ESP_ERROR_CHECK(gpio_config(&cfg));
    }

    s_queue = xQueueCreate(8, sizeof(isr_evt_t));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    for (int i = 0; i < BTN_COUNT; i++) {
        ESP_ERROR_CHECK(gpio_isr_handler_add(s_pins[i], button_isr, (void *)(intptr_t)i));
    }
}

bool buttons_wait_event(btn_evt_t *evt, TickType_t ticks)
{
    isr_evt_t raw;
    if (xQueueReceive(s_queue, &raw, ticks) != pdPASS) {
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(20)); // simple debounce
    bool pressed = gpio_get_level(s_pins[raw.btn]) == 0;
    if (pressed == s_last_state[raw.btn]) {
        return false; // bounce, not a real state change
    }
    s_last_state[raw.btn] = pressed;
    evt->btn = raw.btn;
    evt->pressed = pressed;
    return true;
}