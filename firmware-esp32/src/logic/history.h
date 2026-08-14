#pragma once

#include <stdint.h>

#define HISTORY_CAPACITY 512

typedef struct {
    uint32_t t_ms;
    float temp_c;
} history_sample_t;

typedef struct {
    history_sample_t buf[HISTORY_CAPACITY];
    uint32_t head;  // next write slot
    uint32_t count; // valid samples (<= HISTORY_CAPACITY)
} history_t;

void history_init(history_t *h);
void history_push(history_t *h, uint32_t t_ms, float temp_c);
uint32_t history_count(const history_t *h);
// Oldest-first access; returns NULL if i is out of range.
const history_sample_t *history_get(const history_t *h, uint32_t i);