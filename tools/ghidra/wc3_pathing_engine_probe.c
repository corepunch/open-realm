/* Compile as a shared library to compare the same arithmetic used by Move with retail calls. */
#include "games/warcraft-3/common/wc3_math.h"

uint32_t pathing_add(uint32_t a, uint32_t b) { return wc3_add_bits(a, b); }
uint32_t pathing_subtract(uint32_t a, uint32_t b) { return wc3_add_bits(a, b ^ 0x80000000u); }
uint32_t pathing_multiply(uint32_t a, uint32_t b) { return wc3_mul_bits(a, b); }
uint32_t pathing_angle(uint32_t a) { return wc3_float_bits(wc3_angle(wc3_float(a))); }

/* Inputs: speed, heading, error, increment, turn, window, stop. Outputs overwrite speed/heading only. */
void pathing_motion(uint32_t *words) {
    wc3Motion_t m = { .speed = wc3_float(words[0]), .heading = wc3_float(words[1]),
        .error = wc3_float(words[2]), .increment = wc3_float(words[3]), .turn = wc3_float(words[4]),
        .window = wc3_float(words[5]), .stop = words[6] != 0 };
    wc3_motion_update(&m);
    words[0] = wc3_float_bits(m.speed); words[1] = wc3_float_bits(m.heading);
}
