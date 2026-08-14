#include "controller.h"

#include <math.h>

#include "freertos/queue.h"
#include "relay.h"

void ctrl_init(controller_t *c, const safety_config_t *scfg,
               const reflow_profile_t *profile, float kp, float ki, float kd)
{
    c->state = CTRL_STATE_IDLE;
    c->setpoint_c = 25.0f;
    c->duty = 0.0f;
    c->active_phase = 0;
    c->phase_start_temp = 25.0f;
    c->last_temp = 25.0f;
    c->run_elapsed_ms = 0;
    c->phase_elapsed_ms = 0;
    c->profile = profile;
    c->reset_requested = false;
    safety_init(&c->safety, scfg);
    pid_init(&c->pid, kp, ki, kd, 25.0f, 0.0f, (float)RELAY_PWM_MAX);
    c->cmd_queue = xQueueCreate(8, sizeof(ctrl_cmd_t));
}

bool ctrl_post(controller_t *c, ctrl_cmd_type_t type, int32_t arg)
{
    ctrl_cmd_t cmd = { .type = type, .arg = arg };
    return xQueueSend(c->cmd_queue, &cmd, 0) == pdPASS;
}

void ctrl_set_profile(controller_t *c, const reflow_profile_t *profile)
{
    c->profile = profile;
}

ctrl_state_t ctrl_state(const controller_t *c)
{
    return c->state;
}

const char *ctrl_phase_name(const controller_t *c)
{
    if (c->profile && c->active_phase < c->profile->num_phases) {
        return c->profile->phases[c->active_phase].name;
    }
    return "";
}

safety_fault_t ctrl_fault(const controller_t *c)
{
    return safety_fault(&c->safety);
}

float ctrl_setpoint(const controller_t *c)
{
    return c->setpoint_c;
}

float ctrl_phase_progress(const controller_t *c)
{
    if (c->state != CTRL_STATE_RUN || !c->profile || c->active_phase >= c->profile->num_phases) {
        return 0.0f;
    }
    const profile_phase_t *ph = &c->profile->phases[c->active_phase];
    float ramp_time = (ph->ramp_c_per_s > 0.0f)
        ? fabsf(ph->target_c - c->phase_start_temp) / ph->ramp_c_per_s
        : 0.0f;
    float total = ramp_time + ph->hold_s;
    if (total <= 0.0f) {
        return 1.0f;
    }
    float t = (float)c->phase_elapsed_ms / 1000.0f;
    return t >= total ? 1.0f : t / total;
}

static void ctrl_start(controller_t *c)
{
    safety_reset(&c->safety); // START also clears a latched fault
    c->state = CTRL_STATE_RUN;
    c->active_phase = 0;
    c->phase_elapsed_ms = 0;
    c->phase_start_temp = c->last_temp;
    c->run_elapsed_ms = 0;
    c->setpoint_c = c->profile && c->profile->num_phases > 0
        ? c->profile->phases[0].target_c
        : c->last_temp;
    pid_reset(&c->pid);
}

static void ctrl_stop(controller_t *c)
{
    c->state = CTRL_STATE_IDLE;
    c->setpoint_c = 0.0f;
    c->duty = 0.0f;
    pid_reset(&c->pid);
}

static void ctrl_handle_cmd(controller_t *c, const ctrl_cmd_t *cmd)
{
    switch (cmd->type) {
        case CTRL_CMD_START:
            if (c->state == CTRL_STATE_RUN) {
                ctrl_stop(c);
            } else {
                ctrl_start(c);
            }
            break;
        case CTRL_CMD_STOP:
            ctrl_stop(c);
            break;
        case CTRL_CMD_ACK_FAULT:
            c->reset_requested = true;
            break;
        case CTRL_CMD_SELECT_PROFILE:
            break; // Phase 2 (profile storage); use ctrl_set_profile for now
        default:
            break;
    }
}

// Advance the profile state machine and update the setpoint from elapsed time.
static void ctrl_update_setpoint(controller_t *c, float temp_c)
{
    const reflow_profile_t *p = c->profile;
    if (!p || p->num_phases == 0) {
        c->setpoint_c = temp_c;
        c->state = CTRL_STATE_DONE;
        return;
    }

    float t = (float)c->phase_elapsed_ms / 1000.0f;
    while (c->active_phase < p->num_phases) {
        const profile_phase_t *ph = &p->phases[c->active_phase];
        float ramp_time = (ph->ramp_c_per_s > 0.0f)
            ? fabsf(ph->target_c - c->phase_start_temp) / ph->ramp_c_per_s
            : 0.0f;
        float total = ramp_time + ph->hold_s;

        if (t < total) {
            if (t < ramp_time && ph->ramp_c_per_s > 0.0f) {
                float dir = ph->target_c >= c->phase_start_temp ? 1.0f : -1.0f;
                c->setpoint_c = c->phase_start_temp + dir * ph->ramp_c_per_s * t;
            } else {
                c->setpoint_c = ph->target_c;
            }
            return;
        }
        // Phase finished: advance, ramp starts from the current temperature.
        c->active_phase++;
        c->phase_elapsed_ms = 0;
        c->phase_start_temp = temp_c;
        t = 0.0f;
    }

    c->state = CTRL_STATE_DONE;
    c->setpoint_c = temp_c;
    pid_reset(&c->pid);
}

float ctrl_tick(controller_t *c, bool sensor_ok, float temp_c, float dt)
{
    if (sensor_ok) {
        c->last_temp = temp_c;
    }

    ctrl_cmd_t cmd;
    while (xQueueReceive(c->cmd_queue, &cmd, 0) == pdPASS) {
        ctrl_handle_cmd(c, &cmd);
    }

    if (c->reset_requested) {
        c->reset_requested = false;
        safety_reset(&c->safety);
    }

    float duty = 0.0f;
    if (c->state == CTRL_STATE_RUN && sensor_ok) {
        c->run_elapsed_ms += (uint32_t)(dt * 1000.0f);
        c->phase_elapsed_ms += (uint32_t)(dt * 1000.0f);

        ctrl_update_setpoint(c, temp_c);

        // Max-run watchdog: the profile shouldn't legitimately take this long.
        if (c->safety.cfg.max_run_s > 0.0f &&
            c->run_elapsed_ms >= (uint32_t)(c->safety.cfg.max_run_s * 1000.0f)) {
            safety_trip(&c->safety, SAFETY_RUN_TIMEOUT);
            ctrl_stop(c);
            return 0.0f;
        }

        if (c->state == CTRL_STATE_RUN) {
            pid_set_setpoint(&c->pid, c->setpoint_c);
            duty = pid_update(&c->pid, temp_c, dt);
        }
    } else {
        pid_reset(&c->pid);
    }

    bool allow = safety_update(&c->safety, sensor_ok, temp_c,
                               duty / (float)RELAY_PWM_MAX, dt);
    if (!allow) {
        pid_reset(&c->pid);
        c->state = CTRL_STATE_IDLE;
        c->duty = 0.0f;
        return 0.0f;
    }

    c->duty = duty;
    return duty;
}