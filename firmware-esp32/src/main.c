#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"

// ---- Buttons (active-low, edge-triggered ISR). Wiring: each button between
// the pin and GND; released = high, pressed = low. GPIO 33/32/25 all support
// internal pull-ups, so no external resistors are needed. ----
#define BTN_1_GPIO   GPIO_NUM_32
#define BTN_2_GPIO   GPIO_NUM_33
#define BTN_3_GPIO   GPIO_NUM_25

// ---- LilyGo T-Display LCD pins ----
#define LCD_PIN_SCLK GPIO_NUM_18
#define LCD_PIN_MOSI GPIO_NUM_19
#define LCD_PIN_CS   GPIO_NUM_5
#define LCD_PIN_DC   GPIO_NUM_16
#define LCD_PIN_RST  GPIO_NUM_23
#define LCD_PIN_BL   GPIO_NUM_4

#define LCD_H_RES  240
#define LCD_V_RES  135

#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_GREY    0x8410
#define COLOR_GREEN   0x07E0

typedef struct {
    uint8_t btn;     // 0..2
    bool    pressed; // true if the pin was low at the ISR edge
} btn_evt_t;

static const gpio_num_t s_btn_pins[] = { BTN_1_GPIO, BTN_2_GPIO, BTN_3_GPIO };
static bool s_last_state[3] = { false, false, false };
static QueueHandle_t s_btn_evt_queue;

static esp_lcd_panel_handle_t s_panel;
static uint16_t s_fb[LCD_H_RES * LCD_V_RES];

// ---------------------------------------------------------------- buttons ----
// Runs in ISR context: only reads the pin and posts to a queue.
static void IRAM_ATTR button_isr(void *arg)
{
    int btn = (int)(intptr_t)arg;
    btn_evt_t evt = {
        .btn = btn,
        .pressed = (gpio_get_level(s_btn_pins[btn]) == 0),
    };
    BaseType_t hpw = pdFALSE;
    xQueueSendFromISR(s_btn_evt_queue, &evt, &hpw);
    portYIELD_FROM_ISR(hpw);
}

static void init_buttons(void)
{
    for (int i = 0; i < 3; i++) {
        const gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << s_btn_pins[i],
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_ANYEDGE,
        };
        ESP_ERROR_CHECK(gpio_config(&cfg));
    }

    s_btn_evt_queue = xQueueCreate(8, sizeof(btn_evt_t));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    for (int i = 0; i < 3; i++) {
        ESP_ERROR_CHECK(gpio_isr_handler_add(s_btn_pins[i], button_isr, (void *)(intptr_t)i));
    }
}

// ------------------------------------------------------------ framebuffer ---
static void fb_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    for (int r = y; r < y + h; r++) {
        if (r < 0 || r >= LCD_V_RES) {
            continue;
        }
        for (int c = x; c < x + w; c++) {
            if (c < 0 || c >= LCD_H_RES) {
                continue;
            }
            s_fb[r * LCD_H_RES + c] = color;
        }
    }
}

static void fb_flush_rect(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    if (y + h > LCD_V_RES) {
        h = LCD_V_RES - y; // never read past the framebuffer
    }
    if (x + w > LCD_H_RES) {
        w = LCD_H_RES - x;
    }
    esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + h, &s_fb[y * LCD_H_RES + x]);
}

// 5x7 font, 'A'-'Z' then '0'-'9'. Bit 0 of each column byte is the top row.
static const uint8_t s_font_5x7[36][5] = {
    {0x7C,0x12,0x11,0x12,0x7C}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x09,0x01}, // F
    {0x3E,0x41,0x41,0x51,0x32}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x04,0x08,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x3F,0x40,0x38,0x40,0x3F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}, // 9
};

static void fb_draw_char(int x, int y, char c, uint16_t color)
{
    int idx;
    if (c >= 'A' && c <= 'Z') {
        idx = c - 'A';
    } else if (c >= '0' && c <= '9') {
        idx = 26 + (c - '0');
    } else {
        return;
    }
    for (int col = 0; col < 5; col++) {
        uint8_t bits = s_font_5x7[idx][col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                fb_fill_rect(x + col, y + row, 1, 1, color);
            }
        }
    }
}

static void fb_draw_text(int x, int y, const char *s, uint16_t color)
{
    while (*s) {
        if (*s == ' ') {
            x += 4;
        } else {
            fb_draw_char(x, y, *s, color);
            x += 6;
        }
        s++;
    }
}

// -------------------------------------------------------------- UI / lcd ----
static void draw_button_state(int btn, bool pressed)
{
    int y = 12 + btn * 39; // 3 bars + title all fit within 135 rows
    int h = 36;
    fb_fill_rect(6, y, LCD_H_RES - 12, h, pressed ? COLOR_GREEN : COLOR_GREY);
    char txt[24];
    snprintf(txt, sizeof(txt), "BTN%d %s", btn + 1, pressed ? "PRESSED" : "RELEASED");
    fb_draw_text(16, y + (h - 7) / 2, txt, COLOR_BLACK);
    // Full-screen flush: partial-region (sub-window) writes corrupt on this
    // swapped/mirrored ST7789, while a full-window flush settles cleanly.
    fb_flush_rect(0, 0, LCD_H_RES, LCD_V_RES);
}

static void draw_initial_ui(void)
{
    fb_fill_rect(0, 0, LCD_H_RES, LCD_V_RES, COLOR_BLACK);
    fb_draw_text(6, 4, "BUTTON TEST", COLOR_WHITE);
    for (int i = 0; i < 3; i++) {
        draw_button_state(i, false);
    }
    fb_flush_rect(0, 0, LCD_H_RES, LCD_V_RES);
}

static void init_lcd(void)
{
    // backlight: plain on/off is enough for the test
    const gpio_config_t bl_cfg = {
        .pin_bit_mask = 1ULL << LCD_PIN_BL,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&bl_cfg));
    ESP_ERROR_CHECK(gpio_set_level(LCD_PIN_BL, 1));

    // SPI bus (display is write-only: no MISO)
    const spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_PIN_SCLK,
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_H_RES * 40 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // panel IO (4-wire SPI + DC pin)
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

    // ST7789 panel
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
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, 40, 52)); // x_gap, y_gap

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));
}

// ------------------------------------------------------------------ tasks ---
static void ui_task(void *arg)
{
    btn_evt_t evt;
    while (1) {
        if (xQueueReceive(s_btn_evt_queue, &evt, portMAX_DELAY) != pdPASS) {
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(20)); // simple debounce
        bool pressed = gpio_get_level(s_btn_pins[evt.btn]) == 0;
        if (pressed == s_last_state[evt.btn]) {
            continue; // bounce, not a real state change
        }
        s_last_state[evt.btn] = pressed;
        draw_button_state(evt.btn, pressed);
    }
}

void app_main(void)
{
    init_lcd();
    init_buttons();
    draw_initial_ui();
    xTaskCreate(ui_task, "ui", 4096, NULL, 5, NULL);
}