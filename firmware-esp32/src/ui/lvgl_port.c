#include "lvgl_port.h"

#include <assert.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "buttons.h"

// ---- LilyGo T-Display LCD pins ----
#define LCD_PIN_SCLK GPIO_NUM_18
#define LCD_PIN_MOSI GPIO_NUM_19
#define LCD_PIN_CS   GPIO_NUM_5
#define LCD_PIN_DC   GPIO_NUM_16
#define LCD_PIN_RST  GPIO_NUM_23
#define LCD_PIN_BL   GPIO_NUM_4

#define LCD_H_RES 240
#define LCD_V_RES 135

#define TAG "lvgl_port"

static esp_lcd_panel_handle_t s_panel;
static lv_display_t *s_disp;
static void (*s_poll_cb)(void);

static uint32_t lv_tick_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void lv_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)disp;
    (void)area;
    // FULL render mode: px_map is the whole-frame buffer. Always flush the full
    // screen - partial-region writes are unreliable on this swapped/mirrored panel.
    esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_H_RES, LCD_V_RES, px_map);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "flush failed: %s", esp_err_to_name(err));
    }
    lv_display_flush_ready(disp);
}

static void lvgl_task(void *arg)
{
    (void)arg;
    while (1) {
        lv_timer_handler();
        if (s_poll_cb) {
            s_poll_cb();
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // >= 1 tick at the 100 Hz scheduler
    }
}

void lvgl_port_set_poll_cb(void (*cb)(void))
{
    s_poll_cb = cb;
}

void lvgl_port_init(void)
{
    // Backlight: plain on/off
    const gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << LCD_PIN_BL,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&bl_cfg));
    ESP_ERROR_CHECK(gpio_set_level(LCD_PIN_BL, 1));

    // SPI bus for the LCD (write-only: no MISO)
    const spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_PIN_SCLK,
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_H_RES * 40 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle));

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

    // The T-Display's ST7789 glass is natively 135x240 but mounted landscape on
    // the board (240x135). Rotate + offset it into the correct landscape view.
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, 40, 52));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
    ESP_LOGI(TAG, "ST7789 initialized");

    buttons_init();

    lv_init();
    lv_tick_set_cb(lv_tick_ms);

    void *buf = heap_caps_malloc(LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
                                 MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf) {
        buf = heap_caps_malloc(LCD_H_RES * LCD_V_RES * sizeof(uint16_t), MALLOC_CAP_INTERNAL);
    }
    assert(buf);

    s_disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_buffers(s_disp, buf, NULL, LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(s_disp, lv_flush_cb);

    ESP_LOGI(TAG, "display + LVGL ready");
}

void lvgl_port_start(void)
{
    // Run LVGL on core 1 so a render-heavy lvgl task can't starve the control
    // / network tasks on core 0 (app_main, temp_task).
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 5, NULL, 1);
}