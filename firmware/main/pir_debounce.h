#pragma once
/* Pure-C debouncer (no ESP-IDF dependencies, so it is unit-tested on the host).
 * A new raw level becomes the stable level only after it has been held for
 * debounce_ms without change. Safe across uint32 millisecond wrap-around. */
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     stable;        /* current debounced level */
    bool     last_raw;      /* last sampled level */
    uint32_t raw_since_ms;  /* when last_raw was first seen */
    uint32_t debounce_ms;
} pir_debounce_t;

void pir_debounce_init(pir_debounce_t *d, bool initial, uint32_t debounce_ms);

/* Feed one raw sample. Returns true if the stable level changed on this call
 * (read it from d->stable). */
bool pir_debounce_update(pir_debounce_t *d, bool raw, uint32_t now_ms);
