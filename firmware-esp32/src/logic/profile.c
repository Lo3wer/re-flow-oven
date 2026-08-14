#include "profile.h"

#include <math.h>

float profile_phase_duration(const reflow_profile_t *p, uint8_t i, float start_temp_c)
{
    const profile_phase_t *ph = &p->phases[i];
    float ramp_time = (ph->ramp_c_per_s > 0.0f)
        ? fabsf(ph->target_c - start_temp_c) / ph->ramp_c_per_s
        : 0.0f;
    return ramp_time + ph->hold_s;
}