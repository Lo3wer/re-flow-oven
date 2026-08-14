#include "history.h"

#include <stddef.h>

void history_init(history_t *h)
{
    h->head = 0;
    h->count = 0;
}

void history_push(history_t *h, uint32_t t_ms, float temp_c)
{
    h->buf[h->head].t_ms = t_ms;
    h->buf[h->head].temp_c = temp_c;
    h->head = (h->head + 1) % HISTORY_CAPACITY;
    if (h->count < HISTORY_CAPACITY) {
        h->count++;
    }
}

uint32_t history_count(const history_t *h)
{
    return h->count;
}

const history_sample_t *history_get(const history_t *h, uint32_t i)
{
    if (i >= h->count) {
        return NULL;
    }
    uint32_t idx = (h->head + HISTORY_CAPACITY - h->count + i) % HISTORY_CAPACITY;
    return &h->buf[idx];
}