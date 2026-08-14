#pragma once

#include <stdint.h>

#define PROFILE_NAME_MAX   16
#define PROFILE_MAX_PHASES 6

typedef struct {
    char  name[8];        // e.g. "RAMP", "SOAK", "PEAK", "COOL"
    float ramp_c_per_s;   // ramp rate toward target; 0 = jump straight to target
    float target_c;
    float hold_s;         // time to hold at target after the ramp finishes
} profile_phase_t;

typedef struct {
    char name[PROFILE_NAME_MAX];
    uint8_t num_phases;
    profile_phase_t phases[PROFILE_MAX_PHASES];
} reflow_profile_t;

// Total duration in seconds of phase i (ramp time from start_temp_c + hold).
float profile_phase_duration(const reflow_profile_t *p, uint8_t i, float start_temp_c);