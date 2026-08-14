#include "ui.h"

#include <string.h>

#include "lvgl.h"
#include "lvgl_port.h"

#define LCD_H_RES 240
#define LCD_V_RES 135

static ui_cmd_cb_t s_cmd_cb;
static bool s_running;
static float s_sp;
static float s_duty;
static lv_obj_t *s_temp_label;
static lv_obj_t *s_state_label;
static lv_obj_t *s_meta_label;
static lv_obj_t *s_bar;
static lv_obj_t *s_start_btn;
static lv_obj_t *s_start_label;

static void cmd(ui_cmd_t c)
{
    if (s_cmd_cb) {
        s_cmd_cb(c);
    }
}

static void on_start_btn(lv_event_t *e)
{
    (void)e;
    cmd(s_running ? UI_CMD_STOP : UI_CMD_START);
}

static void on_ack_btn(lv_event_t *e)
{
    (void)e;
    cmd(UI_CMD_ACK);
}

static void update_meta(void)
{
    lv_label_set_text_fmt(s_meta_label, "SP %5.1fC   OUT %3.0f%%", s_sp, s_duty);
}

void ui_set_cmd_cb(ui_cmd_cb_t cb)
{
    s_cmd_cb = cb;
}

void ui_set_temp(float temp_c, bool sensor_open)
{
    if (sensor_open) {
        lv_label_set_text(s_temp_label, "TC OPEN");
    } else {
        lv_label_set_text_fmt(s_temp_label, "%.1f C", temp_c);
    }
}

void ui_set_state_text(const char *text)
{
    lv_label_set_text(s_state_label, text);
    uint32_t color = strncmp(text, "FAULT", 5) == 0 ? 0xFF5555 : 0xFFFFFF;
    lv_obj_set_style_text_color(s_state_label, lv_color_hex(color), 0);
}

void ui_set_running(bool running)
{
    s_running = running;
    lv_label_set_text(s_start_label, running ? "STOP" : "START");
}

void ui_set_phase(float progress01)
{
    if (progress01 < 0.0f) {
        progress01 = 0.0f;
    }
    if (progress01 > 1.0f) {
        progress01 = 1.0f;
    }
    lv_bar_set_value(s_bar, (int32_t)(progress01 * 100), LV_ANIM_OFF);
}

void ui_set_setpoint(float sp_c)
{
    s_sp = sp_c;
    update_meta();
}

void ui_set_duty_pct(float pct)
{
    s_duty = pct;
    update_meta();
}

void ui_init(void)
{
    lvgl_port_init();

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "REFLOW CTRL");
    lv_obj_set_style_text_color(title, lv_color_hex(0x9BD4FF), 0);
    lv_obj_set_pos(title, 8, 4);

    s_temp_label = lv_label_create(scr);
    lv_label_set_text(s_temp_label, "--.- C");
    lv_obj_set_style_text_font(s_temp_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_temp_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(s_temp_label, 8, 26);

    s_state_label = lv_label_create(scr);
    lv_label_set_text(s_state_label, "IDLE");
    lv_obj_set_style_text_color(s_state_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(s_state_label, 8, 60);

    s_meta_label = lv_label_create(scr);
    lv_label_set_text(s_meta_label, "SP   0.0C   OUT   0%");
    lv_obj_set_style_text_color(s_meta_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_pos(s_meta_label, 8, 76);

    s_bar = lv_bar_create(scr);
    lv_obj_set_pos(s_bar, 8, 92);
    lv_obj_set_size(s_bar, 224, 8);
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);

    s_start_btn = lv_button_create(scr);
    lv_obj_set_pos(s_start_btn, 8, 104);
    lv_obj_set_size(s_start_btn, 110, 28);
    lv_obj_add_event_cb(s_start_btn, on_start_btn, LV_EVENT_CLICKED, NULL);
    s_start_label = lv_label_create(s_start_btn);
    lv_label_set_text(s_start_label, "START");
    lv_obj_center(s_start_label);

    lv_obj_t *ack_btn = lv_button_create(scr);
    lv_obj_set_pos(ack_btn, 122, 104);
    lv_obj_set_size(ack_btn, 110, 28);
    lv_obj_add_event_cb(ack_btn, on_ack_btn, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ack_label = lv_label_create(ack_btn);
    lv_label_set_text(ack_label, "ACK FAULT");
    lv_obj_center(ack_label);

    lv_group_t *grp = lv_group_create();
    lv_group_set_default(grp);
    lv_group_add_obj(grp, s_start_btn);
    lv_group_add_obj(grp, ack_btn);
    lvgl_port_set_group(grp);
}