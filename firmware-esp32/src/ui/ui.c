#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_timer.h"
#include "lvgl.h"
#include "lvgl_port.h"

#include "buttons.h"
#include "config.h"

#define LCD_H_RES 240
#define LCD_V_RES 135

// Button roles: BTN3 selects/confirms; BTN1/BTN2 navigate.
#define BTN_UP   0
#define BTN_DOWN 1
#define BTN_ENTER 2

// ---------------------------------------------------------------- helpers ----
static void show(lv_obj_t *o, bool visible)
{
    if (visible) {
        lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

// ------------------------------------------------------------- widget refs ---
static lv_obj_t *s_temp_label;
static lv_obj_t *s_state_label;
static lv_obj_t *s_meta_label;
static lv_obj_t *s_bar;
static lv_obj_t *s_btn[3];       // START, ACK, PROFILES
static lv_obj_t *s_btn_label[3];
static lv_obj_t *s_prof_name_label;
static lv_obj_t *s_prof_list_label;
static lv_obj_t *s_edit_title;
static lv_obj_t *s_edit_phase;
static lv_obj_t *s_edit_vals;
static lv_obj_t *s_edit_hint;

static void update_meta(void);

// ------------------------------------------------------------- public API ---
// All ui_set_* calls just stage values; they are applied to LVGL only inside
// ui_poll(), which runs on the LVGL task. This keeps every LVGL call on one
// thread (calling lv_label_set_text() from another task while a refresh is in
// progress trips LVGL's "invalidate during rendering" assert and reboots).
static ui_cmd_cb_t s_cmd_cb;
static bool s_running;
static float s_sp;
static float s_duty;

static bool s_ui_ready;

static volatile bool s_pend_temp;
static float s_pend_temp_c;
static bool s_pend_sensor_open;
static volatile bool s_pend_state;
static char s_pend_state_text[16];
static volatile bool s_pend_running;
static bool s_pend_running_val;
static volatile bool s_pend_sp;
static float s_pend_sp_val;
static volatile bool s_pend_duty;
static float s_pend_duty_val;
static volatile bool s_pend_phase;
static float s_pend_phase_val;

static void cmd(ui_cmd_t c)
{
    if (s_cmd_cb) {
        s_cmd_cb(c);
    }
}

void ui_set_cmd_cb(ui_cmd_cb_t cb)
{
    s_cmd_cb = cb;
}

void ui_set_temp(float temp_c, bool sensor_open)
{
    s_pend_temp_c = temp_c;
    s_pend_sensor_open = sensor_open;
    s_pend_temp = true;
}

void ui_set_state_text(const char *text)
{
    strncpy(s_pend_state_text, text, sizeof(s_pend_state_text) - 1);
    s_pend_state_text[sizeof(s_pend_state_text) - 1] = '\0';
    s_pend_state = true;
}

void ui_set_running(bool running)
{
    s_pend_running_val = running;
    s_pend_running = true;
}

void ui_set_phase(float progress01)
{
    if (progress01 < 0.0f) {
        progress01 = 0.0f;
    }
    if (progress01 > 1.0f) {
        progress01 = 1.0f;
    }
    s_pend_phase_val = progress01;
    s_pend_phase = true;
}

void ui_set_setpoint(float sp_c)
{
    s_pend_sp_val = sp_c;
    s_pend_sp = true;
}

void ui_set_duty_pct(float pct)
{
    s_pend_duty_val = pct;
    s_pend_duty = true;
}

// ------------------------------------------------------------- home screen --
typedef enum { SCR_HOME, SCR_PROFILES, SCR_EDITOR } screen_t;
static screen_t s_screen = SCR_HOME;

static int s_home_focus;

static void update_meta(void)
{
    lv_label_set_text_fmt(s_meta_label, "SP %5.1fC   OUT %3.0f%%", s_sp, s_duty);
}

static void home_draw_focus(void)
{
    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_bg_color(s_btn[i], lv_color_hex(i == s_home_focus ? 0x2A6FB0 : 0x1F2937), 0);
    }
}

// ---------------------------------------------------------- profiles screen --
static int s_prof_focus;

static void prof_update_text(void)
{
    char txt[160] = "";
    char line[32];
    for (int i = 0; i < CONFIG_PROFILE_COUNT; i++) {
        const reflow_profile_t *p = config_profile(i);
        snprintf(line, sizeof(line), "%s%s%s\n",
                 s_prof_focus == i ? ">" : " ",
                 p->name,
                 config_selected() == i ? "*" : "");
        strncat(txt, line, sizeof(txt) - strlen(txt) - 1);
    }
    snprintf(line, sizeof(line), "%sBACK", s_prof_focus == CONFIG_PROFILE_COUNT ? ">" : " ");
    strncat(txt, line, sizeof(txt) - strlen(txt) - 1);
    lv_label_set_text(s_prof_list_label, txt);
}

// ------------------------------------------------------------ editor screen --
static int s_edit_slot;   // 0..n*3-1 fields, n*3 = DONE, n*3+1 = BACK
static bool s_edit_adjust;

static int edit_slot_count(void)
{
    const reflow_profile_t *p = config_profile(config_selected());
    return (int)p->num_phases * 3 + 2;
}

static void edit_update_text(void)
{
    const reflow_profile_t *p = config_profile(config_selected());
    int n = p->num_phases;
    int base = n * 3;

    lv_label_set_text_fmt(s_edit_title, "EDIT: %s", p->name);

    if (s_edit_slot >= base) {
        lv_label_set_text(s_edit_phase, "FINISH");
        bool done = s_edit_slot == base;
        lv_label_set_text_fmt(s_edit_vals, "%s[DONE]%s  %s[BACK]%s",
                              done ? "" : " ", done ? " " : "",
                              done ? " " : "", done ? "" : " ");
        lv_label_set_text(s_edit_hint, "ENTER: finish");
    } else {
        int ph = s_edit_slot / 3;
        int f = s_edit_slot % 3;
        const profile_phase_t *phd = &p->phases[ph];
        lv_label_set_text_fmt(s_edit_phase, "PH %d/%d %s", ph + 1, n, phd->name);
        lv_label_set_text_fmt(s_edit_vals,
                              "%sR %.1f%s  %sT %.0f%s  %sH %.0f%s",
                              f == 0 ? "[" : "", phd->ramp_c_per_s, f == 0 ? "]" : "",
                              f == 1 ? "[" : "", phd->target_c, f == 1 ? "]" : "",
                              f == 2 ? "[" : "", phd->hold_s, f == 2 ? "]" : "");
        lv_label_set_text(s_edit_hint, s_edit_adjust ? "ROT: value   ENTER: ok" : "ROT: field   ENTER: edit");
    }
}

static void edit_adjust(int dir)
{
    reflow_profile_t *p = config_profile(config_selected());
    int ph = s_edit_slot / 3;
    int f = s_edit_slot % 3;
    profile_phase_t *phd = &p->phases[ph];

    float *val = f == 0 ? &phd->ramp_c_per_s : f == 1 ? &phd->target_c : &phd->hold_s;
    float step = f == 0 ? 0.5f : 5.0f;
    float lo = f == 0 ? 0.1f : f == 1 ? 30.0f : 0.0f;
    float hi = f == 0 ? 20.0f : f == 1 ? 300.0f : 600.0f;

    *val += dir * step;
    if (*val < lo) {
        *val = lo;
    }
    if (*val > hi) {
        *val = hi;
    }
    edit_update_text();
}

// ------------------------------------------------------------- screen logic --
static void open_home(void)
{
    s_screen = SCR_HOME;
    show(s_temp_label, true);
    show(s_state_label, true);
    show(s_meta_label, true);
    show(s_bar, true);
    for (int i = 0; i < 3; i++) {
        show(s_btn[i], true);
    }
    show(s_prof_name_label, true);
    show(s_prof_list_label, false);
    show(s_edit_title, false);
    show(s_edit_phase, false);
    show(s_edit_vals, false);
    show(s_edit_hint, false);
    home_draw_focus();
}

static void open_profiles(void)
{
    s_screen = SCR_PROFILES;
    s_prof_focus = config_selected();
    prof_update_text();
    show(s_temp_label, false);
    show(s_state_label, false);
    show(s_meta_label, false);
    show(s_bar, false);
    for (int i = 0; i < 3; i++) {
        show(s_btn[i], false);
    }
    show(s_prof_name_label, false);
    show(s_prof_list_label, true);
    show(s_edit_title, false);
    show(s_edit_phase, false);
    show(s_edit_vals, false);
    show(s_edit_hint, false);
}

static void open_editor(void)
{
    s_screen = SCR_EDITOR;
    s_edit_slot = 0;
    s_edit_adjust = false;
    edit_update_text();
    show(s_temp_label, false);
    show(s_state_label, false);
    show(s_meta_label, false);
    show(s_bar, false);
    for (int i = 0; i < 3; i++) {
        show(s_btn[i], false);
    }
    show(s_prof_name_label, false);
    show(s_prof_list_label, false);
    show(s_edit_title, true);
    show(s_edit_phase, true);
    show(s_edit_vals, true);
    show(s_edit_hint, true);
}

// --------------------------------------------------------------- button scan --
static bool s_btn_down[3];
static uint32_t s_btn_last[3];
static uint32_t s_now;

// true on a fresh press, or on repeat while held (repeat only when enabled)
static bool btn_event(int btn, bool repeat)
{
    bool pressed = buttons_is_pressed(btn);
    if (!pressed) {
        s_btn_down[btn] = false;
        s_btn_last[btn] = 0;
        return false;
    }
    if (!s_btn_down[btn]) {
        s_btn_down[btn] = true;
        s_btn_last[btn] = s_now;
        return true;
    }
    if (repeat && (s_now - s_btn_last[btn]) >= 120) {
        s_btn_last[btn] = s_now;
        return true;
    }
    return false;
}

// Apply staged ui_set_* values to the widgets. Called from the LVGL task only.
static void apply_pending(void)
{
    if (s_pend_temp) {
        s_pend_temp = false;
        if (s_pend_sensor_open) {
            lv_label_set_text(s_temp_label, "TC OPEN");
        } else {
            lv_label_set_text_fmt(s_temp_label, "%.1f C", s_pend_temp_c);
        }
    }
    if (s_pend_state) {
        s_pend_state = false;
        lv_label_set_text(s_state_label, s_pend_state_text);
        uint32_t color = strncmp(s_pend_state_text, "FAULT", 5) == 0 ? 0xFF5555 : 0xFFFFFF;
        lv_obj_set_style_text_color(s_state_label, lv_color_hex(color), 0);
    }
    if (s_pend_running) {
        s_pend_running = false;
        s_running = s_pend_running_val;
        lv_label_set_text(s_btn_label[0], s_running ? "STOP" : "START");
    }
    if (s_pend_phase) {
        s_pend_phase = false;
        lv_bar_set_value(s_bar, (int32_t)(s_pend_phase_val * 100), LV_ANIM_OFF);
    }
    if (s_pend_sp) {
        s_pend_sp = false;
        s_sp = s_pend_sp_val;
        update_meta();
    }
    if (s_pend_duty) {
        s_pend_duty = false;
        s_duty = s_pend_duty_val;
        update_meta();
    }
}

static void ui_poll(void)
{
    if (!s_ui_ready) {
        return; // widgets not created yet
    }
    apply_pending();
    s_now = (uint32_t)(esp_timer_get_time() / 1000);

    switch (s_screen) {
        case SCR_HOME: {
            if (btn_event(BTN_UP, false)) {
                s_home_focus = (s_home_focus + 2) % 3;
                home_draw_focus();
            } else if (btn_event(BTN_DOWN, false)) {
                s_home_focus = (s_home_focus + 1) % 3;
                home_draw_focus();
            } else if (btn_event(BTN_ENTER, false)) {
                if (s_home_focus == 2) {
                    open_profiles();
                } else if (s_home_focus == 0) {
                    cmd(s_running ? UI_CMD_STOP : UI_CMD_START);
                } else {
                    cmd(UI_CMD_ACK);
                }
            }
            break;
        }

        case SCR_PROFILES: {
            int n = CONFIG_PROFILE_COUNT + 1;
            if (btn_event(BTN_UP, false)) {
                s_prof_focus = (s_prof_focus + n - 1) % n;
                prof_update_text();
            } else if (btn_event(BTN_DOWN, false)) {
                s_prof_focus = (s_prof_focus + 1) % n;
                prof_update_text();
            } else if (btn_event(BTN_ENTER, false)) {
                if (s_prof_focus == CONFIG_PROFILE_COUNT) {
                    open_home();
                } else {
                    config_set_selected((uint8_t)s_prof_focus);
                    config_save();
                    open_editor();
                }
            }
            break;
        }

        case SCR_EDITOR: {
            int n = edit_slot_count();
            if (btn_event(BTN_UP, false)) {
                if (s_edit_adjust) {
                    edit_adjust(-1);
                } else {
                    s_edit_slot = (s_edit_slot + n - 1) % n;
                    edit_update_text();
                }
            } else if (btn_event(BTN_DOWN, false)) {
                if (s_edit_adjust) {
                    edit_adjust(+1);
                } else {
                    s_edit_slot = (s_edit_slot + 1) % n;
                    edit_update_text();
                }
            } else if (btn_event(BTN_ENTER, false)) {
                int base = (int)config_profile(config_selected())->num_phases * 3;
                if (s_edit_adjust) {
                    s_edit_adjust = false;
                    edit_update_text();
                } else if (s_edit_slot == base) {
                    config_save();
                    open_home();
                } else if (s_edit_slot == base + 1) {
                    open_home();
                } else {
                    s_edit_adjust = true;
                    edit_update_text();
                }
            }
            break;
        }
    }
}

// -------------------------------------------------------------------- init ---
void ui_init(void)
{
    lvgl_port_init();
    lvgl_port_set_poll_cb(ui_poll);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "REFLOW CTRL");
    lv_obj_set_style_text_color(title, lv_color_hex(0x9BD4FF), 0);
    lv_obj_set_pos(title, 8, 4);

    s_prof_name_label = lv_label_create(scr);
    lv_label_set_text(s_prof_name_label, "");
    lv_obj_set_style_text_color(s_prof_name_label, lv_color_hex(0x9BD4FF), 0);
    lv_obj_set_pos(s_prof_name_label, 160, 4);

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

    const char *labels[3] = { "START", "ACK FAULT", "PROFILES" };
    for (int i = 0; i < 3; i++) {
        s_btn[i] = lv_button_create(scr);
        lv_obj_set_pos(s_btn[i], 8 + i * 78, 104);
        lv_obj_set_size(s_btn[i], 74, 28);
        lv_obj_set_style_bg_color(s_btn[i], lv_color_hex(0x1F2937), 0);
        s_btn_label[i] = lv_label_create(s_btn[i]);
        lv_label_set_text(s_btn_label[i], labels[i]);
        lv_obj_center(s_btn_label[i]);
    }
    home_draw_focus();

    // profiles screen
    s_prof_list_label = lv_label_create(scr);
    lv_label_set_text(s_prof_list_label, "");
    lv_obj_set_style_text_color(s_prof_list_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(s_prof_list_label, 12, 12);

    // editor screen
    s_edit_title = lv_label_create(scr);
    lv_label_set_text(s_edit_title, "");
    lv_obj_set_style_text_color(s_edit_title, lv_color_hex(0x9BD4FF), 0);
    lv_obj_set_pos(s_edit_title, 12, 10);

    s_edit_phase = lv_label_create(scr);
    lv_label_set_text(s_edit_phase, "");
    lv_obj_set_style_text_color(s_edit_phase, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(s_edit_phase, 12, 34);

    s_edit_vals = lv_label_create(scr);
    lv_label_set_text(s_edit_vals, "");
    lv_obj_set_style_text_color(s_edit_vals, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_pos(s_edit_vals, 12, 58);

    s_edit_hint = lv_label_create(scr);
    lv_label_set_text(s_edit_hint, "");
    lv_obj_set_style_text_color(s_edit_hint, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_pos(s_edit_hint, 12, 80);

    // start on the home screen
    lv_label_set_text_fmt(s_prof_name_label, "%s", config_profile(config_selected())->name);
    open_home();

    s_ui_ready = true; // from now on ui_poll may touch the widgets
    lvgl_port_start(); // start rendering only after the whole UI is built
}