/* Host-side unit test. Run: ./test/host/run.sh */
#include <assert.h>
#include <stdio.h>
#include "../../main/pir_debounce.h"

int main(void)
{
    pir_debounce_t d;

    /* Rising edge is accepted only after being held for debounce_ms. */
    pir_debounce_init(&d, false, 100);
    assert(!pir_debounce_update(&d, true, 1000));
    assert(!pir_debounce_update(&d, true, 1099));
    assert(pir_debounce_update(&d, true, 1100) && d.stable);
    assert(!pir_debounce_update(&d, true, 1200)); /* no repeat event */

    /* A glitch shorter than debounce_ms is ignored. */
    pir_debounce_init(&d, false, 100);
    assert(!pir_debounce_update(&d, true, 0));
    assert(!pir_debounce_update(&d, false, 50)); /* back before 100 ms */
    assert(!pir_debounce_update(&d, false, 500));
    assert(!d.stable);

    /* Falling edge also needs to be held. */
    pir_debounce_init(&d, true, 100);
    assert(!pir_debounce_update(&d, false, 10));
    assert(pir_debounce_update(&d, false, 110) && !d.stable);

    /* Bouncing restarts the timer. */
    pir_debounce_init(&d, false, 100);
    assert(!pir_debounce_update(&d, true, 0));
    assert(!pir_debounce_update(&d, false, 60));
    assert(!pir_debounce_update(&d, true, 70));
    assert(!pir_debounce_update(&d, true, 169)); /* 99 ms since 70 */
    assert(pir_debounce_update(&d, true, 170));

    /* uint32 millisecond wrap-around. */
    pir_debounce_init(&d, false, 100);
    assert(!pir_debounce_update(&d, true, 0xFFFFFFF0u));
    assert(!pir_debounce_update(&d, true, 0x0000004Fu)); /* 95 ms later */
    assert(pir_debounce_update(&d, true, 0x00000054u));  /* 100 ms later */

    puts("pir_debounce: all tests passed");
    return 0;
}
