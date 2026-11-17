// SPDX-License-Identifier: LGPL-3.0-or-later
#include <assert.h>
#include <stdio.h>
#include "../examples/fbw_external_power/main/button_debounce.h"

int main(void)
{
    button_debounce_t b;
    // A held button at startup never generates a press.
    button_debounce_init(&b, 0, 0);
    for (int t = 0; t < 1000; t += 10)
        assert(!button_debounce_update(&b, 0, t));
    assert(!button_debounce_update(&b, 1, 1000));
    assert(!button_debounce_update(&b, 1, 1030));
    assert(!button_debounce_update(&b, 0, 1040));
    assert(!button_debounce_update(&b, 0, 1069));
    assert(button_debounce_update(&b, 0, 1070));
    for (int t = 1080; t < 2000; t += 10)
        assert(!button_debounce_update(&b, 0, t));

    // Short bounce in either direction must not create/rearm a press.
    assert(!button_debounce_update(&b, 1, 2000));
    assert(!button_debounce_update(&b, 0, 2010));
    assert(!button_debounce_update(&b, 0, 2050));
    assert(!button_debounce_update(&b, 1, 2060));
    assert(!button_debounce_update(&b, 1, 2090));
    assert(!button_debounce_update(&b, 0, 2100));
    assert(!button_debounce_update(&b, 1, 2110));
    assert(!button_debounce_update(&b, 0, 2120));
    assert(!button_debounce_update(&b, 0, 2149));
    assert(button_debounce_update(&b, 0, 2150));
    assert(!button_debounce_update(&b, 0, 2160));

    // Startup released also needs 30 ms stable release, then one stable press.
    button_debounce_init(&b, 1, 0);
    assert(!button_debounce_update(&b, 1, 30));
    assert(!button_debounce_update(&b, 0, 40));
    assert(button_debounce_update(&b, 0, 70));
    puts("BOOT debounce: startup-held, bounce, hold, and repeated cycles passed");
    return 0;
}
