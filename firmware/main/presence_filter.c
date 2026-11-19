#include "presence_filter.h"

void presence_filter_init(presence_filter_t *f, uint32_t off_delay_ms)
{
    f->present = false;
    f->absent_active = false;
    f->absent_since_ms = 0;
    f->off_delay_ms = off_delay_ms;
}

bool presence_filter_update(presence_filter_t *f, bool raw, uint32_t now_ms)
{
    if (raw) {
        f->absent_active = false;
        if (!f->present) {
            f->present = true;
            return true;
        }
        return false;
    }
    if (!f->present) {
        return false;
    }
    if (!f->absent_active) {
        f->absent_active = true;
        f->absent_since_ms = now_ms;
    }
    if ((uint32_t)(now_ms - f->absent_since_ms) >= f->off_delay_ms) {
        f->present = false;
        f->absent_active = false;
        return true;
    }
    return false;
}
