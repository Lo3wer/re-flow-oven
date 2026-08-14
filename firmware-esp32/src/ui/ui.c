#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"

// ---- LilyGo T-Display LCD pins ----
#define LCD_PIN_SCLK GPIO_NUM_18
#define LCD_PIN_MOSI GPIO_NUM_19
#define LCD_PIN_CS   GPIO_NUM_5
#define LCD_PIN_DC   GPIO_NUM_16
#define LCD_PIN_RST  GPIO_NUM_23
#define LCD_PIN_BL   GPIO_NUM_4

#define TEMP_Y  11
#define BAR_Y(i) (22 + (i) * 38) // 3 bars (22/60/98) fit under title + temp line

static esp_lcd_panel_handle_t s_panel;
static uint16_t s_fb[LCD_H_RES * LCD_V_RES];

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
    if (c == '.') {
        fb_fill_rect(x + 2, y + 5, 1, 2, color);
        return;
    }
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

// -------------------------------------------------------------- drawing -----
static void draw_temp_line(int temp_tenths, const char *note)
{
    fb_fill_rect(6, TEMP_Y, LCD_H_RES - 12, 7, COLOR_BLACK);
    char txt[40];
    if (temp_tenths < 0) {
        snprintf(txt, sizeof(txt), "TEMP TC OPEN");
    } else {
        int frac = temp_tenths % 10;
        if (frac < 0) {
            frac = -frac;
        }
        snprintf(txt, sizeof(txt), "TEMP %d.%dC", temp_tenths / 10, frac);
    }
    if (note) {
        strncat(txt, " ", sizeof(txt) - strlen(txt) - 1);
        strncat(txt, note, sizeof(txt) - strlen(txt) - 1);
    }
    fb_draw_text(6, TEMP_Y, txt, COLOR_WHITE);
}

static void draw_button_state(int btn, bool pressed)
{
    int y = BAR_Y(btn);
    int h = 36;
    fb_fill_rect(6, y, LCD_H_RES - 12, h, pressed ? COLOR_GREEN : COLOR_GREY);
    char txt[24];
    snprintf(txt, sizeof(txt), "BTN%d %s", btn + 1, pressed ? "PRESSED" : "RELEASED");
    fb_draw_text(16, y + (h - 7) / 2, txt, COLOR_BLACK);
    // Full-screen flush: partial-region (sub-window) writes corrupt on this
    // swapped/mirrored ST7789, while a full-window flush settles cleanly.
    fb_flush_rect(0, 0, LCD_H_RES, LCD_V_RES);
}

// -------------------------------------------------------------------- API ---
void ui_draw_temp_line(int temp_tenths, const char *note)
{
    draw_temp_line(temp_tenths, note);
}

void ui_draw_button_state(int btn, bool pressed)
{
    draw_button_state(btn, pressed);
}

void ui_flush(void)
{
    fb_flush_rect(0, 0, LCD_H_RES, LCD_V_RES);
}

void ui_draw_initial(void)
{
    fb_fill_rect(0, 0, LCD_H_RES, LCD_V_RES, COLOR_BLACK);
    fb_draw_text(6, 2, "REFLOW TEST", COLOR_WHITE);
    draw_temp_line(-1, NULL); // "TEMP TC OPEN" until the first real read
    for (int i = 0; i < 3; i++) {
        draw_button_state(i, false);
    }
    fb_flush_rect(0, 0, LCD_H_RES, LCD_V_RES);
}

void ui_init(void)
{
    // backlight: plain on/off is enough for the test
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