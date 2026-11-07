/* The page samples navigator.getGamepads() on its main thread. Emscripten
 * pthread workers cannot reliably query the browser API directly. Do not
 * replace SDL2: this is only a browser input source for the existing client. */
#include "port/web/src/web_gamepad.h"
#include <emscripten/emscripten.h>
#include <stddef.h>
#include <stdint.h>

/* Every field is one aligned i32 so JS Atomics and C atomics agree on layout. */
static webGamepadSnapshot_t web_pad;
_Static_assert(sizeof(webGamepadSnapshot_t) == 10 * sizeof(uint32_t), "web pad shared layout changed");

EMSCRIPTEN_KEEPALIVE uintptr_t Web_GamepadSharedAddress(void) { return (uintptr_t)&web_pad; }

/* Consume latched down edges exactly once. A press and release between two
 * engine frames still yields an SDL down/up pair in the input adapter. */
void Web_GamepadRead(webGamepadSnapshot_t *state) {
    if (!state) return;
    state->connected = __atomic_load_n(&web_pad.connected, __ATOMIC_ACQUIRE);
    state->buttons = __atomic_load_n(&web_pad.buttons, __ATOMIC_ACQUIRE);
    state->pressed = __atomic_exchange_n(&web_pad.pressed, 0, __ATOMIC_ACQ_REL);
    state->generation = __atomic_load_n(&web_pad.generation, __ATOMIC_ACQUIRE);
    for (int i = 0; i < BZ_WEB_GAMEPAD_AXES; i++)
        state->axes[i] = __atomic_load_n(&web_pad.axes[i], __ATOMIC_ACQUIRE);
    if (!state->connected) {
        state->buttons = state->pressed = 0;
        for (int i = 0; i < BZ_WEB_GAMEPAD_AXES; i++) state->axes[i] = 0;
    }
}
