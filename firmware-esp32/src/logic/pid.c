#include "pid.h"

void pid_init(pid_controller_t *pid, float kp, float ki, float kd, float setpoint,
              float out_min, float out_max)
{
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->setpoint = setpoint;
    pid->out_min = out_min;
    pid->out_max = out_max;
    pid_reset(pid);
}

void pid_reset(pid_controller_t *pid)
{
    pid->integral = 0.0f;
    pid->prev_error = 0.0f;
    pid->initialized = false;
}

void pid_set_setpoint(pid_controller_t *pid, float setpoint)
{
    pid->setpoint = setpoint;
}

float pid_update(pid_controller_t *pid, float measurement, float dt)
{
    float error = pid->setpoint - measurement;
    if (!pid->initialized) {
        pid->initialized = true;
        pid->prev_error = error; // no derivative on the first step
    }

    pid->integral += error * dt;

    float derivative = 0.0f;
    if (dt > 0.0f) {
        derivative = (error - pid->prev_error) / dt;
    }

    float output = pid->kp * error + pid->ki * pid->integral + pid->kd * derivative;

    // Anti-windup: if the output saturates, back off the integrator so it
    // doesn't keep accumulating while the heater can't respond faster.
    if (output > pid->out_max) {
        output = pid->out_max;
        pid->integral -= error * dt;
    } else if (output < pid->out_min) {
        output = pid->out_min;
        pid->integral -= error * dt;
    }

    pid->prev_error = error;
    return output;
}