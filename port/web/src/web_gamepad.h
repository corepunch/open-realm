/* Browser Gamepad API -> shared-memory state for the WC3 SDL2 controller path.
 * Web-only: all words are atomically accessed across the page and engine pthread. */
#ifndef BZ_WEB_GAMEPAD_H
#define BZ_WEB_GAMEPAD_H
#include <stdint.h>

#define BZ_WEB_GAMEPAD_AXES 6 // SDL2 controller axes, including two analogue triggers
#define BZ_WEB_GAMEPAD_TRIG_LEFT (1u << 30) // pending LT press between engine ticks
#define BZ_WEB_GAMEPAD_TRIG_RIGHT (1u << 31) // pending RT press between engine ticks

typedef struct {
    uint32_t connected, buttons, pressed, generation;
    int32_t axes[BZ_WEB_GAMEPAD_AXES];
} webGamepadSnapshot_t;

void Web_GamepadRead(webGamepadSnapshot_t *state);
#endif
