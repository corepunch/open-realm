/* Compile as a shared library to compare the same arithmetic used by Move with retail calls. */
#include "games/warcraft-3/common/wc3_math.h"

uint32_t pathing_add(uint32_t a, uint32_t b) { return wc3_add_bits(a, b); }
uint32_t pathing_subtract(uint32_t a, uint32_t b) { return wc3_add_bits(a, b ^ 0x80000000u); }
uint32_t pathing_multiply(uint32_t a, uint32_t b) { return wc3_mul_bits(a, b); }
uint32_t pathing_angle(uint32_t a) { return wc3_float_bits(wc3_angle(wc3_float(a))); }
uint32_t pathing_sin(uint32_t a) { return wc3_float_bits(wc3_sin(wc3_float(a))); }
uint32_t pathing_cos(uint32_t a) { return wc3_float_bits(wc3_cos(wc3_float(a))); }
uint32_t pathing_acos(uint32_t a) { return wc3_float_bits(wc3_acos(wc3_float(a))); }
uint32_t pathing_heading(uint32_t x, uint32_t y) {
    return wc3_float_bits(wc3_vector_heading(wc3_float(x), wc3_float(y)));
}
uint32_t pathing_heading_error(uint32_t x, uint32_t y, uint32_t heading) {
    return wc3_float_bits(wc3_heading_error(wc3_float(x), wc3_float(y), wc3_float(heading)));
}
uint32_t pathing_fractional(uint32_t a) { return wc3_float_bits(wc3_fraction(wc3_float(a))); }
uint32_t pathing_modulo(uint32_t a, uint32_t b) { return wc3_float_bits(wc3_modulo(wc3_float(a), wc3_float(b))); }
uint32_t pathing_facing_angle(uint32_t a) { return wc3_float_bits(wc3_facing_angle(wc3_float(a))); }
uint32_t pathing_velocity_heading(uint32_t x, uint32_t y, uint32_t current) {
    return wc3_float_bits(wc3_velocity_heading(wc3_float(x), wc3_float(y), wc3_float(current)));
}

uint32_t pathing_sqrt(uint32_t a) { return wc3_float_bits(wc3_sqrt(wc3_float(a))); }
uint32_t pathing_reciprocal(uint32_t a) { return wc3_float_bits(wc3_recip(wc3_float(a))); }
uint32_t pathing_divide(uint32_t a, uint32_t b) { return wc3_float_bits(wc3_div(wc3_float(a), wc3_float(b))); }

/* Inputs: speed, heading, error, increment, turn, window, stop. Outputs overwrite speed/heading only. */
void pathing_motion(uint32_t *words) {
    wc3Motion_t m = { .speed = wc3_float(words[0]), .heading = wc3_float(words[1]),
        .error = wc3_float(words[2]), .increment = wc3_float(words[3]), .turn = wc3_float(words[4]),
        .window = wc3_float(words[5]), .stop = words[6] != 0 };
    wc3_motion_update(&m);
    words[0] = wc3_float_bits(m.speed); words[1] = wc3_float_bits(m.heading);
}

/* Inputs: old velocity XY, requested speed, heading, maximum. Outputs overwrite XY only. */
void pathing_velocity(uint32_t *words) {
    wc3Velocity_t v = { .vel = {wc3_float(words[0]), wc3_float(words[1])},
        .speed = wc3_float(words[2]), .heading = wc3_float(words[3]), .limit = wc3_float(words[4]) };
    wc3_velocity_update(&v);
    words[0] = wc3_float_bits(v.vel[0]); words[1] = wc3_float_bits(v.vel[1]);
}

/* Inputs: old velocity XY, speed, heading, maximum, facing. Outputs: XY and facing. */
void pathing_velocity_commit(uint32_t *words) {
    wc3Velocity_t v = { .vel = {wc3_float(words[0]), wc3_float(words[1])},
        .speed = wc3_float(words[2]), .heading = wc3_float(words[3]), .limit = wc3_float(words[4]) };
    wc3_velocity_update(&v);
    float facing = v.speed > 0 ? wc3_velocity_heading(v.vel[0], v.vel[1], wc3_float(words[5])) : wc3_facing_angle(v.heading);
    words[0] = wc3_float_bits(v.vel[0]); words[1] = wc3_float_bits(v.vel[1]);
    words[5] = wc3_float_bits(facing);
}

/* XY, old velocity XY, previous time/epoch, current time/epoch/span, displacement XY. */
void pathing_integrate(uint32_t *words) {
    float pos[2] = {wc3_float(words[0]), wc3_float(words[1])};
    float vel[2] = {wc3_float(words[2]), wc3_float(words[3])};
    wc3Clock_t old = { .time = wc3_float(words[4]), .epoch = words[5] };
    wc3Clock_t cur = { .time = wc3_float(words[6]), .epoch = words[7], .span = wc3_float(words[8]) };
    wc3_integrate(pos, vel, wc3_elapsed(&cur, &old));
    for (unsigned i = 0; i < 2; i++) words[i] = wc3_float_bits(wc3_add(pos[i], wc3_float(words[9 + i])));
    words[4] = words[6]; words[5] = words[7];
}
