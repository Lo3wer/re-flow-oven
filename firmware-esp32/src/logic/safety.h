#pragma once

#include <stdbool.h>
#include <stddef.h>

// Safety interlock for the heater. Feed it every control step; when it returns
// false the heater must stay off. Faults latch until safety_reset().
typedef enum {
    SAFETY_OK = 0,
    SAFETY_OVERTEMP,       // temp exceeded max_temp_c
    SAFETY_RUN_AWAY,       // temp rose too fast for too long
    SAFETY_RISE_WHILE_OFF, // temp kept rising while the heater was commanded off
    SAFETY_SENSOR,         // too many consecutive invalid readings
} safety_fault_t;

typedef struct {
    float max_temp_c;          // hard over-temperature cutoff, e.g. 300 C
    float max_rise_c_per_s;    // runaway rise-rate limit, e.g. 8 C/s
    float runaway_hold_s;      // time above the rate limit before tripping, e.g. 2 s
    float duty_off_threshold;  // duty fraction at/below this counts as "heater off", e.g. 0.03
    float rise_while_off_c;    // allowed rise while the heater is off, e.g. 8 C
    size_t sensor_fault_count; // consecutive bad reads before a sensor fault, e.g. 3
} safety_config_t;

typedef struct {
    safety_config_t cfg;
    safety_fault_t fault; // latched fault, SAFETY_OK while running
    float rate_filt;      // filtered rise rate in C/s (diagnostics)
    float runaway_time;   // seconds spent above the rate limit
    bool  off_period_active;
    float off_period_temp; // temp captured when the heater turned off
    size_t bad_reads;
    float prev_temp;
    bool  have_prev;
} safety_t;

void safety_init(safety_t *s, const safety_config_t *cfg);
// sensor_ok: false if the thermocouple read failed. temp_c: measured temp in C
// (ignored when sensor_ok is false). duty: commanded heater duty 0..1. dt:
// seconds since the previous step. Returns true if the heater may run.
bool safety_update(safety_t *s, bool sensor_ok, float temp_c, float duty, float dt);
safety_fault_t safety_fault(const safety_t *s);
void safety_reset(safety_t *s);