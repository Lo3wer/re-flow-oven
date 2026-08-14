#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"

#include "pid.h"
#include "profile.h"
#include "safety.h"

typedef enum {
    CTRL_STATE_IDLE,
    CTRL_STATE_RUN,
    CTRL_STATE_DONE,
} ctrl_state_t;

typedef enum {
    CTRL_CMD_START,         // toggles run/stop; also clears a latched fault
    CTRL_CMD_STOP,
    CTRL_CMD_ACK_FAULT,     // clear a latched fault (idle only)
    CTRL_CMD_SELECT_PROFILE,
} ctrl_cmd_type_t;

typedef struct {
    ctrl_cmd_type_t type;
    int32_t arg;
} ctrl_cmd_t;

typedef struct {
    ctrl_state_t state;
    float setpoint_c;      // current target fed to the PID
    float duty;            // last commanded duty (0..RELAY_PWM_MAX)
    uint8_t active_phase;  // 0..num_phases-1
    float phase_start_temp;
    float last_temp;       // last good measurement
    uint32_t run_elapsed_ms;
    uint32_t phase_elapsed_ms;
    const reflow_profile_t *profile;
    pid_t pid;
    safety_t safety;
    QueueHandle_t cmd_queue;
    bool reset_requested;  // ack-fault pending
} controller_t;

void ctrl_init(controller_t *c, const safety_config_t *scfg,
               const reflow_profile_t *profile, float kp, float ki, float kd);
bool ctrl_post(controller_t *c, ctrl_cmd_type_t type, int32_t arg);
void ctrl_set_profile(controller_t *c, const reflow_profile_t *profile);
// One control step. sensor_ok=false means no valid temp (heater killed).
// Returns the heater duty (0..RELAY_PWM_MAX) to apply, 0 when idle/faulted.
float ctrl_tick(controller_t *c, bool sensor_ok, float temp_c, float dt);
ctrl_state_t ctrl_state(const controller_t *c);
const char *ctrl_phase_name(const controller_t *c);
safety_fault_t ctrl_fault(const controller_t *c);
float ctrl_setpoint(const controller_t *c);
float ctrl_phase_progress(const controller_t *c); // 0..1 within the current phase