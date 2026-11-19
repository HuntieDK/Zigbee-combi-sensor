#pragma once
/* Presence hold filter (pure C, host-tested): "present" is asserted immediately,
 * and cleared only after the raw signal has been absent for off_delay_ms. */
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     present;
    bool     absent_active;   /* raw has been false since absent_since_ms */
    uint32_t absent_since_ms;
    uint32_t off_delay_ms;
} presence_filter_t;

void presence_filter_init(presence_filter_t *f, uint32_t off_delay_ms);

/* Returns true if f->present changed on this call. */
bool presence_filter_update(presence_filter_t *f, bool raw, uint32_t now_ms);
