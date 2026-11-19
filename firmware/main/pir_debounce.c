#include "pir_debounce.h"

void pir_debounce_init(pir_debounce_t *d, bool initial, uint32_t debounce_ms)
{
    d->stable = initial;
    d->last_raw = initial;
    d->raw_since_ms = 0;
    d->debounce_ms = debounce_ms;
}

bool pir_debounce_update(pir_debounce_t *d, bool raw, uint32_t now_ms)
{
    if (raw != d->last_raw) {
        d->last_raw = raw;
        d->raw_since_ms = now_ms;
    }
    if (d->last_raw != d->stable && (uint32_t)(now_ms - d->raw_since_ms) >= d->debounce_ms) {
        d->stable = d->last_raw;
        return true;
    }
    return false;
}
