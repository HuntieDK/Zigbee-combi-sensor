#include <assert.h>
#include <stdio.h>
#include "../../main/button_hold.h"

int main(void)
{
    button_hold_t b;

    /* Full long press: IDLE, WARN, then FIRE exactly once. */
    button_hold_init(&b, 2000, 5000);
    assert(button_hold_update(&b, false, 0) == BUTTON_HOLD_IDLE);
    assert(button_hold_update(&b, true, 1000) == BUTTON_HOLD_IDLE);
    assert(button_hold_update(&b, true, 2999) == BUTTON_HOLD_IDLE);
    assert(button_hold_update(&b, true, 3000) == BUTTON_HOLD_WARN);
    assert(button_hold_update(&b, true, 5999) == BUTTON_HOLD_WARN);
    assert(button_hold_update(&b, true, 6000) == BUTTON_HOLD_FIRE);
    assert(button_hold_update(&b, true, 7000) == BUTTON_HOLD_IDLE);   /* no repeat while held */
    assert(button_hold_update(&b, false, 7100) == BUTTON_HOLD_IDLE);

    /* A short press does nothing. */
    assert(button_hold_update(&b, true, 8000) == BUTTON_HOLD_IDLE);
    assert(button_hold_update(&b, true, 9000) == BUTTON_HOLD_IDLE);
    assert(button_hold_update(&b, false, 9500) == BUTTON_HOLD_IDLE);

    /* Releasing during the warning phase cancels, and a new press starts the timer again. */
    assert(button_hold_update(&b, true, 10000) == BUTTON_HOLD_IDLE);
    assert(button_hold_update(&b, true, 12500) == BUTTON_HOLD_WARN);
    assert(button_hold_update(&b, false, 13000) == BUTTON_HOLD_IDLE);
    assert(button_hold_update(&b, true, 14000) == BUTTON_HOLD_IDLE);
    assert(button_hold_update(&b, true, 18999) == BUTTON_HOLD_WARN);
    assert(button_hold_update(&b, true, 19000) == BUTTON_HOLD_FIRE);

    /* Holds across the millisecond counter wrap. */
    button_hold_init(&b, 2000, 5000);
    assert(button_hold_update(&b, true, 0xFFFFFC18u) == BUTTON_HOLD_IDLE);  /* 1000 ms before the wrap */
    assert(button_hold_update(&b, true, 0x00000BB8u) == BUTTON_HOLD_WARN);  /* 4000 ms held */
    assert(button_hold_update(&b, true, 0x00000FA0u) == BUTTON_HOLD_FIRE);  /* 5000 ms held */
    puts("button_hold: all tests passed");
    return 0;
}
