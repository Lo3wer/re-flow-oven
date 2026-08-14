#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "profile.h"

#define CONFIG_PROFILE_COUNT 4

// Call once at boot: seeds NVS with defaults on first run.
void config_init(void);
void config_save(void); // persist all profiles + selection to NVS
const reflow_profile_t *config_profiles(void); // array of CONFIG_PROFILE_COUNT
reflow_profile_t *config_profile(uint8_t i);
uint8_t config_selected(void);
void config_set_selected(uint8_t i);