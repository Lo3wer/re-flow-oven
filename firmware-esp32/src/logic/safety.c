#include "safety.h"

void safety_init(safety_t *s, const safety_config_t *cfg)
{
    s->cfg = *cfg;
    safety_reset(s);
}

void safety_reset(safety_t *s)
{
    s->fault = SAFETY_OK;
    s->rate_filt = 0.0f;
    s->runaway_time = 0.0f;
    s->off_period_active = false;
    s->off_period_temp = 0.0f;
    s->bad_reads = 0;
    s->prev_temp = 0.0f;
    s->have_prev = false;
}

safety_fault_t safety_fault(const safety_t *s)
{
    return s->fault;
}

bool safety_update(safety_t *s, bool sensor_ok, float temp_c, float duty, float dt)
{
    // Latching: once a fault trips, stay off until safety_reset().
    if (s->fault != SAFETY_OK) {
        return false;
    }

    // Sensor sanity: count consecutive bad reads before declaring a fault.
    if (!sensor_ok) {
        s->bad_reads++;
        s->have_prev = false;
        s->off_period_active = false;
        if (s->bad_reads >= s->cfg.sensor_fault_count) {
            s->fault = SAFETY_SENSOR;
        }
        return s->fault == SAFETY_OK;
    }
    s->bad_reads = 0;

    // Hard over-temperature cutoff.
    if (temp_c > s->cfg.max_temp_c) {
        s->fault = SAFETY_OVERTEMP;
        return false;
    }

    // Thermal runaway: filtered rise rate sustained above the limit.
    if (s->have_prev) {
        float inst = (temp_c - s->prev_temp) / (dt > 0.0f ? dt : 1.0f);
        float a = (dt > 0.0f) ? dt / (dt + 1.0f) : 1.0f; // ~1 s low-pass
        if (a > 1.0f) {
            a = 1.0f;
        }
        s->rate_filt += (inst - s->rate_filt) * a;
    }
    s->prev_temp = temp_c;
    s->have_prev = true;

    if (s->rate_filt > s->cfg.max_rise_c_per_s) {
        s->runaway_time += dt;
        if (s->runaway_time >= s->cfg.runaway_hold_s) {
            s->fault = SAFETY_RUN_AWAY;
            return false;
        }
    } else {
        s->runaway_time = 0.0f;
    }

    // Rise while the heater is commanded off: stuck relay / heat leaking in.
    if (duty <= s->cfg.duty_off_threshold) {
        if (!s->off_period_active) {
            s->off_period_active = true;
            s->off_period_temp = temp_c;
        } else if (temp_c - s->off_period_temp > s->cfg.rise_while_off_c) {
            s->fault = SAFETY_RISE_WHILE_OFF;
            return false;
        }
    } else {
        s->off_period_active = false;
    }

    return true;
}