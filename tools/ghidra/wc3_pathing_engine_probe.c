/* Compile as a shared library to compare the same arithmetic used by Move with retail calls. */
#include "games/warcraft-3/common/wc3_math.h"

/* Three-word ABI bridges preserve output on the declared nonreturning retail power domain. */
void pathing_corelog(uint32_t a, uint32_t b, uint32_t output[2]) {
    (void)b;
    output[0] = 1;
    output[1] = wc3_float_bits(wc3_ln_core(wc3_float(a)));
}
void pathing_reducedlog(uint32_t a, uint32_t b, uint32_t output[2]) {
    (void)b;
    output[0] = 1;
    output[1] = wc3_float_bits(wc3_ln_reduced(wc3_float(a)));
}
void pathing_log(uint32_t a, uint32_t b, uint32_t output[2]) {
    (void)b;
    output[0] = 1;
    output[1] = wc3_float_bits(wc3_ln(wc3_float(a)));
}
void pathing_exp(uint32_t a, uint32_t b, uint32_t output[2]) {
    float value;
    (void)b;
    output[0] = wc3_exp(wc3_float(a), &value);
    if (output[0]) output[1] = wc3_float_bits(value);
}
void pathing_power(uint32_t a, uint32_t b, uint32_t output[2]) {
    float value;
    output[0] = wc3_pow(wc3_float(a), wc3_float(b), &value);
    if (output[0]) output[1] = wc3_float_bits(value);
}
void pathing_public_power(uint32_t a, uint32_t b, uint32_t output[2]) {
    float value;
    bool small = wc3_float(wc3_float_bits(wc3_sub(wc3_float(a), 0.0f)) & 0x7fffffffu) < wc3_float(0x3a83126f);
    bool tiny = wc3_float(wc3_float_bits(wc3_sub(wc3_float(b), 0.0f)) & 0x7fffffffu) < wc3_float(0x3a83126f);
    if (small && wc3_float(b) < 0.0f) {
        output[0] = 1;
        output[1] = 0;
    } else if (!small && tiny) {
        output[0] = 1;
        output[1] = 0x3f800000;
    } else {
        output[0] = wc3_pow(wc3_float(a), wc3_float(b), &value);
        if (output[0]) output[1] = wc3_float_bits(value);
    }
}

uint32_t pathing_add(uint32_t a, uint32_t b) { return wc3_add_bits(a, b); }
uint32_t pathing_subtract(uint32_t a, uint32_t b) { return wc3_add_bits(a, b ^ 0x80000000u); }
uint32_t pathing_multiply(uint32_t a, uint32_t b) { return wc3_mul_bits(a, b); }
uint32_t pathing_angle(uint32_t a) { return wc3_float_bits(wc3_angle(wc3_float(a))); }
uint32_t pathing_sin(uint32_t a) { return wc3_float_bits(wc3_sin(wc3_float(a))); }
uint32_t pathing_cos(uint32_t a) { return wc3_float_bits(wc3_cos(wc3_float(a))); }
void pathing_sincos(uint32_t a, uint32_t output[2]) {
    float result[2];
    wc3_sincos(wc3_float(a), &result[0], &result[1]);
    for (unsigned i = 0; i < 2; i++) output[i] = wc3_float_bits(result[i]);
}
/* Match the original pointer-alias matrix without violating C's scalar representation rules. */
void pathing_sincos_alias(uint32_t words[3], unsigned mode) {
    float slots[3];
    for (unsigned i = 0; i < 3; i++) slots[i] = wc3_float(words[i]);
    unsigned sine = mode == 1 || mode == 4 ? 0 : 1;
    unsigned cosine = mode == 2 || mode == 4 ? 0 : mode == 3 ? 1 : 2;
    wc3_sincos(slots[0], &slots[sine], &slots[cosine]);
    for (unsigned i = 0; i < 3; i++) words[i] = wc3_float_bits(slots[i]);
}
uint32_t pathing_asin(uint32_t a) { return wc3_float_bits(wc3_asin(wc3_float(a))); }
uint32_t pathing_atan(uint32_t a) { return wc3_float_bits(wc3_atan(wc3_float(a))); }
uint32_t pathing_atan2(uint32_t y, uint32_t x) { return wc3_float_bits(wc3_atan2(wc3_float(y), wc3_float(x))); }
uint32_t pathing_tan(uint32_t a) { return wc3_float_bits(wc3_tan(wc3_float(a))); }
uint32_t pathing_degrees_to_radians(uint32_t a) { return wc3_float_bits(wc3_degrees_to_radians(wc3_float(a))); }
uint32_t pathing_radians_to_degrees(uint32_t a) { return wc3_float_bits(wc3_radians_to_degrees(wc3_float(a))); }
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

uint32_t pathing_floor(uint32_t a) { return wc3_floor_bits(a); }
uint32_t pathing_ceil(uint32_t a) { return wc3_ceil_bits(a); }
uint32_t pathing_round(uint32_t a) { return wc3_round_bits(a); }
uint32_t pathing_truncate(uint32_t a) { return wc3_trunc_bits(a); }

uint32_t pathing_integer_float(uint32_t a) { return wc3_from_int(a); }
uint32_t pathing_saturating_integer(uint32_t a) { return wc3_saturating_int_bits(a); }
uint32_t pathing_decimal(char const *text) { return wc3_float_bits(wc3_decimal(text)); }

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

/* World-unit adapter of the original fine-grid commit: same six-word contract. */
void pathing_velocity_world_commit(uint32_t *words) {
    wc3Velocity_t v = { .vel = {wc3_float(words[0]), wc3_float(words[1])},
        .speed = wc3_float(words[2]), .heading = wc3_float(words[3]), .limit = wc3_float(words[4]) };
    wc3_velocity_update_world(&v);
    float x = wc3_mul(v.vel[0], wc3_float(0x3d000000));
    float y = wc3_mul(v.vel[1], wc3_float(0x3d000000));
    float facing = v.speed > 0 ? wc3_velocity_heading(x, y, wc3_float(words[5])) : wc3_facing_angle(v.heading);
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
