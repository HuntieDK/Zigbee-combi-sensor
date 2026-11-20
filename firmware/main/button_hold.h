#pragma once
/* Long-press detector (pure C, host-tested). */
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BUTTON_HOLD_IDLE,  /* not pressed, or already handled until release */
    BUTTON_HOLD_WARN,  /* held past warn_ms: about to trigger */
    BUTTON_HOLD_FIRE,  /* held for hold_ms: returned exactly once per press */
} button_hold_state_t;

typedef struct {
    bool     down;
    bool     fired;
    uint32_t since_ms;
    uint32_t warn_ms;
    uint32_t hold_ms;
} button_hold_t;

void button_hold_init(button_hold_t *b, uint32_t warn_ms, uint32_t hold_ms);
button_hold_state_t button_hold_update(button_hold_t *b, bool pressed, uint32_t now_ms);
