#pragma once

#include <stdbool.h>

typedef struct {
    float kp;
    float ki;
    float kd;
    float setpoint;   // target, same units as measurement (deg C)
    float integral;
    float prev_error;
    bool  initialized;
    float out_min;
    float out_max;
} pid_t;

void pid_init(pid_t *pid, float kp, float ki, float kd, float setpoint,
              float out_min, float out_max);
void pid_reset(pid_t *pid);
void pid_set_setpoint(pid_t *pid, float setpoint);
// One control step. measurement and setpoint in deg C, dt in seconds.
// Returns the clamped output (e.g. a PWM duty for the relay).
float pid_update(pid_t *pid, float measurement, float dt);