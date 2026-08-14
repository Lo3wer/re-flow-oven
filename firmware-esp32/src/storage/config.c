#include "config.h"

#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"

static reflow_profile_t s_profiles[CONFIG_PROFILE_COUNT];
static uint8_t s_selected;

// Generic placeholders; edit values via the on-screen editor.
static const reflow_profile_t s_defaults[CONFIG_PROFILE_COUNT] = {
    {
        .name = "GENERIC", .num_phases = 5,
        .phases = {
            { "RAMP", 2.0f, 150.0f, 0.0f },
            { "SOAK", 2.0f, 200.0f, 60.0f },
            { "RAMP", 2.0f, 245.0f, 0.0f },
            { "PEAK", 0.5f, 245.0f, 30.0f },
            { "COOL", 1.0f, 80.0f, 0.0f },
        },
    },
    {
        .name = "LEADED", .num_phases = 5,
        .phases = {
            { "RAMP", 2.0f, 140.0f, 0.0f },
            { "SOAK", 1.5f, 180.0f, 60.0f },
            { "RAMP", 1.5f, 220.0f, 0.0f },
            { "PEAK", 0.5f, 220.0f, 30.0f },
            { "COOL", 1.0f, 70.0f, 0.0f },
        },
    },
    {
        .name = "FAST", .num_phases = 4,
        .phases = {
            { "RAMP", 3.0f, 150.0f, 0.0f },
            { "SOAK", 3.0f, 200.0f, 30.0f },
            { "RAMP", 3.0f, 245.0f, 0.0f },
            { "PEAK", 0.5f, 245.0f, 15.0f },
        },
    },
    {
        .name = "SLOW", .num_phases = 5,
        .phases = {
            { "RAMP", 1.0f, 150.0f, 0.0f },
            { "SOAK", 1.0f, 200.0f, 120.0f },
            { "RAMP", 1.0f, 245.0f, 0.0f },
            { "PEAK", 0.5f, 245.0f, 60.0f },
            { "COOL", 0.5f, 80.0f, 0.0f },
        },
    },
};

void config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    bool loaded = false;
    nvs_handle_t h;
    if (nvs_open("reflow", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s_profiles);
        if (nvs_get_blob(h, "profiles", s_profiles, &len) == ESP_OK &&
            len == sizeof(s_profiles)) {
            nvs_get_u8(h, "selected", &s_selected);
            loaded = true;
        }
        nvs_close(h);
    }

    if (!loaded) {
        memcpy(s_profiles, s_defaults, sizeof(s_defaults));
        s_selected = 0;
        config_save();
    }
}

void config_save(void)
{
    nvs_handle_t h;
    if (nvs_open("reflow", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "profiles", s_profiles, sizeof(s_profiles));
        nvs_set_u8(h, "selected", s_selected);
        nvs_commit(h);
        nvs_close(h);
    }
}

const reflow_profile_t *config_profiles(void)
{
    return s_profiles;
}

reflow_profile_t *config_profile(uint8_t i)
{
    if (i >= CONFIG_PROFILE_COUNT) {
        return &s_profiles[0];
    }
    return &s_profiles[i];
}

uint8_t config_selected(void)
{
    return s_selected;
}

void config_set_selected(uint8_t i)
{
    if (i < CONFIG_PROFILE_COUNT) {
        s_selected = i;
    }
}