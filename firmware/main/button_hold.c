#include "button_hold.h"

void button_hold_init(button_hold_t *b, uint32_t warn_ms, uint32_t hold_ms)
{
    b->down = false;
    b->fired = false;
    b->since_ms = 0;
    b->warn_ms = warn_ms;
    b->hold_ms = hold_ms;
}

button_hold_state_t button_hold_update(button_hold_t *b, bool pressed, uint32_t now_ms)
{
    if (!pressed) {
        b->down = false;
        b->fired = false;
        return BUTTON_HOLD_IDLE;
    }
    if (!b->down) {
        b->down = true;
        b->since_ms = now_ms;
    }
    if (b->fired) {
        return BUTTON_HOLD_IDLE; /* wait for release before arming again */
    }
    uint32_t held = (uint32_t)(now_ms - b->since_ms);
    if (held >= b->hold_ms) {
        b->fired = true;
        return BUTTON_HOLD_FIRE;
    }
    return held >= b->warn_ms ? BUTTON_HOLD_WARN : BUTTON_HOLD_IDLE;
}
