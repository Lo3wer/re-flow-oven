#include "relay.h"

#include "driver/ledc.h"
#include "esp_err.h"

// Relay/heater output driven by a PWM-capable GPIO (LEDC). GPIO32 was freed up
// when buttons moved to the onboard GPIO35/GPIO0.
#define RELAY_PWM_GPIO GPIO_NUM_32
#define RELAY_PWM_FREQ 1000 // Hz; low enough for a transistor/SSR driven relay

void relay_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT, // 0..1023
        .timer_num = LEDC_TIMER_0,
        .freq_hz = RELAY_PWM_FREQ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    const ledc_channel_config_t chan = {
        .gpio_num = RELAY_PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&chan));
}

void relay_set_duty(uint32_t duty)
{
    if (duty > RELAY_PWM_MAX) {
        duty = RELAY_PWM_MAX;
    }
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
}