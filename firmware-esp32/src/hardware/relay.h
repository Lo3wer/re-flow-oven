#pragma once

#include <stdint.h>

#define RELAY_PWM_MAX 1023

void relay_init(void);
// Set PWM duty 0..RELAY_PWM_MAX (PID will later modulate this; on/off is
// full duty vs 0).
void relay_set_duty(uint32_t duty);