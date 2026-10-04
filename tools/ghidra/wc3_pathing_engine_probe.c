#include "games/warcraft-3/common/wc3_pathing_coordinates.h"
/* Compile as a shared library to compare the same arithmetic used by Move with retail calls. */
#include "games/warcraft-3/common/wc3_math.h"
#include "games/warcraft-3/common/wc3_pathing_masks.h"
#include "games/warcraft-3/common/wc3_pathing_arrival.h"
#include "games/warcraft-3/common/wc3_pathing_speed.h"
#include "games/warcraft-3/common/wc3_pathing_formation.h"
#include "games/warcraft-3/common/wc3_pathing_placement.h"
#include "games/warcraft-3/common/wc3_pathing_random.h"
#include "games/warcraft-3/common/wc3_pathing_repulsion.h"
#include "games/warcraft-3/common/wc3_pathing_adaptive.h"
#include "games/warcraft-3/common/wc3_pathing_yield.h"
#include "games/warcraft-3/common/wc3_pathing_retry.h"
#include "games/warcraft-3/common/wc3_pathing_widget.h"

void pathing_widget_snap(uint32_t const input[5], uint32_t output[2]) {
    float point[]={wc3_float(input[0]),wc3_float(input[1])};
    wc3_widget_snap(point,input[input[4]&1 ? 3 : 2],input[input[4]&1 ? 2 : 3]);
    output[0]=wc3_float_bits(point[0]); output[1]=wc3_float_bits(point[1]);
}

void pathing_widget_clamp(uint32_t const input[6], uint32_t output[2]) {
    for(unsigned k=0;k<2;k++)
        output[k]=wc3_float_bits(wc3_widget_clamp_axis(wc3_float(input[k]),wc3_float(input[2+k]),wc3_float(input[4+k])));
}

/* Original05b970 world bounds clipping and subsequent fixed32-cell input. */
void pathing_point_order_clip(uint32_t const input[7], uint32_t output[4]) {
    for (unsigned k=0;k<2;k++) {
        float point=wc3_point_order_coordinate(wc3_float(input[k]),wc3_float(input[2+k]),wc3_float(input[4+k]),wc3_float(input[6]));
        output[k]=wc3_float_bits(point);
        output[2+k]=wc3_float_bits(wc3_grid_coordinate(point,wc3_float(input[2+k]),32));
    }
}

/* Complete retry inputs retain native source/adjusted goal and owner words. */
void pathing_retry_init(uint32_t const input[7], uint32_t output[3]) {
    wc3RetryInput_t in={{wc3_float(input[0]),wc3_float(input[1])},
        {wc3_float(input[2]),wc3_float(input[3])},input[4]};
    wc3Random_t random={input[5],input[6]};
    output[0]=wc3_retry_init(&in,&random); output[1]=random.sum; output[2]=random.index;
}

void pathing_retry_advance(uint32_t const input[8], uint32_t output[4]) {
    wc3RetryInput_t in={{wc3_float(input[1]),wc3_float(input[2])},
        {wc3_float(input[3]),wc3_float(input[4])},input[5]};
    wc3Random_t random={input[6],input[7]};
    uint32_t count=input[0];
    output[0]=wc3_retry_advance(&count,&in,&random); output[1]=count;
    output[2]=random.sum; output[3]=random.index;
}

/* Four supplied hierarchy levels, exact source/goal words and ordinary size1/2 route policy. */
static wc3AccSearch_t adaptive_probe;
void pathing_adaptive_route(uint32_t const input[8], uint8_t const *classes, uint32_t *output) {
    static wc3FineVector_t points[BZ_WC3_ACC_ROUTE_NODES];
    uint32_t offset=0;
    for (unsigned level=0; level<4; level++) {
        uint32_t width=input[0]>>level, height=input[1]>>level;
        adaptive_probe.maps[level]=(wc3AccMap_t){width,height,classes+offset,malloc(width*height*sizeof(int))};
        assert(adaptive_probe.maps[level].indices); offset+=width*height;
    }
    wc3AccRequest_t req={{wc3_float(input[4]),wc3_float(input[5])},{wc3_float(input[6]),wc3_float(input[7])},1u<<input[2],input[3]};
    uint32_t result=wc3_acc_route(&adaptive_probe,&req,points), count=result&0x7fffffffu;
    output[0]=!(result&0x80000000u); output[1]=adaptive_probe.work.pops; output[2]=adaptive_probe.work.count; output[3]=count;
    for (uint32_t i=0; i<count; i++) {
        output[4+i*2]=wc3_float_bits(points[i].x); output[5+i*2]=wc3_float_bits(points[i].y);
    }
    for (unsigned level=0; level<4; level++) free(adaptive_probe.maps[level].indices);
}

/* Read retained production backing and reset for an independent constructor control. */
void pathing_adaptive_storage(uint32_t reset, uint32_t out[3]) {
    if (reset) wc3_acc_free(&adaptive_probe);
    out[0] = adaptive_probe.work.node_capacity;
    out[1] = adaptive_probe.work.heap_capacity;
    out[2] = adaptive_probe.work.count;
}

/* Explicit legal enqueue/pop prefix over nodes from the most recent search;
 * exercise retained backing growth independently from gameplay reachability. */
void pathing_adaptive_heap_prefix(uint32_t count, uint32_t *out) {
    wc3FineSearch_t *work = &adaptive_probe.work;
    uint32_t initial = work->queued;
    assert(count >= initial && work->count);
    for (uint32_t i=0; i<count-initial; i++) wc3_fine_enqueue(work,i%work->count);
    out[0] = work->heap_capacity;
    for (uint32_t i=0; i<count; i++) {
        wc3FineEntry_t entry = wc3_fine_pop(work);
        out[1+3*i]=entry.key; out[2+3*i]=entry.node; out[3+3*i]=entry.gen;
    }
}

/* Supplied ordinary hierarchy, query size/budget and exact coarse endpoints. */
void pathing_adaptive_distance(uint32_t const input[8], uint8_t const *classes, uint32_t output[3]) {
    static wc3AccSearch_t search;
    uint32_t offset=0;
    for (unsigned level=0; level<4; level++) {
        uint32_t width=input[0]>>level, height=input[1]>>level;
        search.maps[level]=(wc3AccMap_t){width,height,classes+offset,malloc(width*height*sizeof(int))};
        assert(search.maps[level].indices); offset+=width*height;
    }
    wc3AccRequest_t req={{wc3_float(input[4]),wc3_float(input[5])},{wc3_float(input[6]),wc3_float(input[7])},1u<<input[2],input[3]};
    wc3FineVector_t endpoint;
    output[0]=wc3_acc_query_distance(&search,&req,&endpoint);
    output[1]=wc3_float_bits(endpoint.x); output[2]=wc3_float_bits(endpoint.y);
    for (unsigned level=0; level<4; level++) free(search.maps[level].indices);
}

/* Complete public terrain native inputs: XY/origin words, type, seed, passable. */
void pathing_terrain_native(uint32_t const input[7], uint32_t output[4]) {
    uint32_t x = wc3_int_bits(wc3_floor_bits(wc3_float_bits(
        wc3_grid_coordinate(wc3_float(input[0]),wc3_float(input[2]),32))));
    uint32_t y = wc3_int_bits(wc3_floor_bits(wc3_float_bits(
        wc3_grid_coordinate(wc3_float(input[1]),wc3_float(input[3]),32))));
    uint8_t mask = wc3_pathingtype_mask(input[4]), flags = input[5];
    bool outside = x >= 16 || y >= 16;
    output[0] = mask;
    output[1] = outside || (flags & mask) != 0;
    if (!outside) flags = wc3_terrain_pathing_edit(flags,mask,!input[6]);
    output[2] = outside || (flags & mask) != 0;
    output[3] = flags;
}

/* Existing mover image70..8c, current clock, new cap and fine flags. */
void pathing_speed_cap(uint32_t const input[13], uint32_t output[10]) {
    for (unsigned i = 0; i < 8; i++) output[i] = input[i];
    output[6] = input[11]; output[8] = input[12];
    wc3Velocity_t v = { .vel = {wc3_float(input[4]), wc3_float(input[5])}, .limit = wc3_float(input[11]) };
    output[9] = wc3_velocity_cap(&v);
    if (!output[9]) return;
    wc3Clock_t old = {wc3_float(input[0]), input[1], wc3_float(input[10])};
    wc3Clock_t current = {wc3_float(input[8]), input[9], wc3_float(input[10])};
    float pos[2] = {wc3_float(input[2]), wc3_float(input[3])};
    float velocity[2] = {wc3_float(input[4]), wc3_float(input[5])};
    wc3_integrate(pos, velocity, wc3_elapsed(&current, &old));
    output[0] = input[8]; output[1] = input[9];
    for (unsigned i = 0; i < 2; i++) {
        output[2 + i] = wc3_float_bits(pos[i]); output[4 + i] = wc3_float_bits(v.vel[i]);
    }
    if ((v.vel[0] != 0 || v.vel[1] != 0) && v.limit > 0) output[8] |= 0x20000000u;
    else output[8] &= ~0x20000000u;
}

void pathing_speed_cap_world(uint32_t const input[3], uint32_t output[3]) {
    wc3Velocity_t v = { .vel = {wc3_float(input[0]), wc3_float(input[1])}, .limit = wc3_float(input[2]) };
    output[2] = wc3_velocity_cap_world(&v);
    output[0] = wc3_float_bits(v.vel[0]); output[1] = wc3_float_bits(v.vel[1]);
}

/* Count/heading then eleven words per member: pose, velocity, old/current
 * clock words and span, radius and authored rank. Inputs remain untouched. */
void pathing_formation(uint32_t const *input, uint32_t *output) {
    wc3FormationMember_t members[WC3_FORMATION_MEMBERS] = {0};
    wc3Formation_t f = { .members = members, .count = input[0], .heading = wc3_float(input[1]) };
    if (f.count > WC3_FORMATION_MEMBERS) { output[0] = 0; return; }
    for (unsigned i = 0; i < f.count; i++) {
        uint32_t const *row = input + 2 + i * 11;
        wc3Clock_t old = { wc3_float(row[4]), row[5], wc3_float(row[8]) };
        wc3Clock_t current = { wc3_float(row[6]), row[7], wc3_float(row[8]) };
        members[i].position[0] = wc3_float(row[0]); members[i].position[1] = wc3_float(row[1]);
        float velocity[2] = {wc3_float(row[2]), wc3_float(row[3])};
        wc3_integrate(members[i].position, velocity, wc3_elapsed(&current, &old));
        members[i].radius = wc3_float(row[9]); members[i].rank = row[10];
    }
    output[0] = wc3_formation_layout(&f);
    for (unsigned i = 0; i < f.count; i++)
        for (unsigned k = 0; k < 2; k++) output[1 + i * 2 + k] = wc3_float_bits(members[i].offset[k]);
}

/* Base, two attached bonuses, multiplier, authored/default profile bounds. */
void pathing_speed_bonus(uint32_t const input[8], uint32_t output[3]) {
    float bonus = wc3_speed_bonus_max(0, wc3_float(input[1]));
    bonus = wc3_speed_bonus_max(bonus, wc3_float(input[2]));
    float raw = wc3_mul(wc3_add(wc3_float(input[0]), bonus), wc3_float(input[3]));
    wc3SpeedLimit_t s = { .value = raw, .minimum = wc3_float(input[4]), .maximum = wc3_float(input[5]),
        .default_minimum = wc3_float(input[6]), .default_maximum = wc3_float(input[7]) };
    output[0] = wc3_float_bits(bonus); output[1] = wc3_float_bits(raw);
    output[2] = wc3_float_bits(wc3_speed_limit_update(&s));
}

void pathing_speed_limits(uint32_t const input[6], uint32_t output[3]) {
    wc3SpeedLimit_t s = { .value = wc3_float(input[0]), .minimum = wc3_float(input[1]),
        .maximum = wc3_float(input[2]), .default_minimum = wc3_float(input[3]),
        .default_maximum = wc3_float(input[4]), .disabled = input[5] != 0 };
    output[0] = wc3_float_bits(wc3_speed_limit_update(&s));
    output[1] = wc3_float_bits(s.minimum);
    output[2] = wc3_float_bits(s.maximum);
}

/* Inputs: source XY, target XY, heading, range, flags. Outputs: distance,
 * signed heading error, in-range, reached; inputs remain untouched. */
void pathing_arrival(uint32_t const input[7], uint32_t output[4]) {
    wc3Arrival_t a = { .source = {wc3_float(input[0]), wc3_float(input[1])},
        .target = {wc3_float(input[2]), wc3_float(input[3])},
        .heading = wc3_float(input[4]), .range = wc3_float(input[5]), .flags = input[6] };
    output[3] = wc3_arrival_update(&a);
    output[0] = wc3_float_bits(a.distance);
    output[1] = wc3_float_bits(a.error);
    output[2] = a.in_range;
}

uint32_t pathing_wpm_flags(uint32_t flags) { return wc3_wpm_movement_flags(flags); }

uint32_t pathing_vector_heading(uint32_t x, uint32_t y) {
    return wc3_float_bits(wc3_vector_heading(wc3_float(x), wc3_float(y)));
}

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

/* Compiled source tokens have a distinct producer from public S2R strings. */
uint32_t pathing_literal(char const *text) { return wc3_float_bits(wc3_literal(text)); }

/* Source integer words precede the public truncating I2R conversion. */
uint32_t pathing_integer_literal(char const *text) { return wc3_integer_literal_bits(text); }

#include "games/warcraft-3/common/wc3_pathing_fine.h"
typedef struct { uint32_t width; uint8_t const *edges; } fineProbeGraph_t;
static wc3FineSearch_t fine_probe;
static uint8_t fine_probe_edges(void const *data, wc3FinePoint_t pos) {
    fineProbeGraph_t const *graph = data;
    return graph->edges[(uint32_t)pos.y * graph->width + (uint32_t)pos.x];
}

/* Input: width,height,start XY,goal XY,budget. Output: cost,pops,nodes,path count,
 * reopens,stale, then start-to-goal cell pairs. Uses the production search. */
void pathing_fine_grid(uint32_t const input[7], uint8_t const *edges, int32_t *out) {
    fineProbeGraph_t graph = { input[0], edges };
    wc3FineRequest_t req = { .start = {(int)input[2], (int)input[3]}, .goal = {(int)input[4], (int)input[5]},
        .width = input[0], .height = input[1], .budget = input[6], .edges = fine_probe_edges, .data = &graph };
    int at = wc3_fine_search(&fine_probe, &req), length = 0;
    out[0] = at < 0 ? -1 : (int32_t)fine_probe.nodes[at].g;
    out[1] = (int32_t)fine_probe.pops; out[2] = (int32_t)fine_probe.count;
    out[4] = (int32_t)fine_probe.reopens; out[5] = (int32_t)fine_probe.stale;
    for (int node = at; node >= 0; node = fine_probe.nodes[node].parent) length++;
    out[3] = length;
    for (int i = length - 1; i >= 0; i--, at = fine_probe.nodes[at].parent) {
        out[6 + i * 2] = fine_probe.nodes[at].pos.x; out[7 + i * 2] = fine_probe.nodes[at].pos.y;
    }
}

/* Original queue-oracle witness: node(2,5), goal(0,0), parent g=100, step=21. */
void pathing_fine_relax(uint32_t state, uint32_t old_cost, uint32_t out[7]) {
    wc3_fine_reserve(&fine_probe, 1, 2);
    fine_probe.queued = fine_probe.reopens = 0;
    fine_probe.nodes[0] = (wc3FineNode_t){ .pos = {2,5}, .parent = 9, .g = old_cost,
        .h = 83, .gen = 10, .state = (wc3FineState_t)state };
    if (state == WC3_FINE_OPEN) {
        fine_probe.queued = 1;
        fine_probe.heap[1] = (wc3FineEntry_t){ old_cost + 83, 0, 10 };
    }
    wc3_fine_relax(&fine_probe, (wc3FinePoint_t){0,0}, (wc3FineStep_t){0,3,121});
    wc3FineNode_t const *node = &fine_probe.nodes[0];
    out[0] = node->gen; out[1] = node->g; out[2] = node->h; out[3] = (uint32_t)node->parent;
    out[4] = (uint32_t)node->state; out[5] = fine_probe.queued; out[6] = fine_probe.reopens;
}

void pathing_fine_heap_ties(uint32_t out[8]) {
    wc3_fine_reserve(&fine_probe, 8, 0);
    fine_probe.queued = 0;
    for (uint32_t i = 0; i < 8; i++) {
        fine_probe.nodes[i] = (wc3FineNode_t){0};
        wc3_fine_enqueue(&fine_probe, i);
    }
    for (int i = 0; i < 8; i++) out[i] = wc3_fine_pop(&fine_probe).node;
}

/* Storage diagnostics consume the same production search, not a second
 * capacity model. Reset permits a fresh-control run in the same library. */
void pathing_fine_storage(uint32_t reset, uint32_t out[3]) {
    if (reset) wc3_fine_free(&fine_probe);
    out[0] = fine_probe.node_capacity; out[1] = fine_probe.heap_capacity;
    out[2] = fine_probe.count;
}

/* Static 16ee80 geometry: raw radius/XY, dimensions and terrain-query bits. */
uint32_t pathing_footprint(uint32_t const input[6], uint8_t const *cells) {
    wc3FinePoint_t pos = { (int)wc3_float(wc3_floor_bits(input[1])), (int)wc3_float(wc3_floor_bits(input[2])) };
    wc3FineBox_t box = wc3_fine_cover(wc3_fine_class(wc3_float(input[0])), pos);
    if (box.min.x < 0 || box.min.y < 0 || (uint32_t)box.max.x > input[3] || (uint32_t)box.max.y > input[4]) return 0;
    for (int y = box.min.y; y < box.max.y; y++) for (int x = box.min.x; x < box.max.x; x++)
        if (cells[(uint32_t)y * input[3] + (uint32_t)x] & input[5]) return 0;
    return 1;
}

#include "games/warcraft-3/common/wc3_pathing_route.h"

void pathing_acc_selection(uint32_t const input[2], uint32_t const *words, uint32_t output[2]) {
    wc3FineVector_t points[13];
    for (unsigned i=0;i<13;i++) points[i]=(wc3FineVector_t){wc3_float(words[2*i]),wc3_float(words[2*i+1])};
    wc3AccSelection_t result=wc3_acc_select((wc3FineRoute_t){points,input[0]},input[1]!=0);
    output[0]=result.index; output[1]=result.gate;
}

void pathing_fine_reconstruct(uint32_t const *input, int32_t const *cells, uint32_t *out) {
    wc3FineNode_t nodes[64];
    wc3FineVector_t points[64];
    assert(input[0] > 0 && input[0] <= 64);
    for (uint32_t i = 0; i < input[0]; i++) nodes[i] = (wc3FineNode_t){.pos = {cells[2*i], cells[2*i+1]}, .parent = (int)i - 1};
    wc3FineReconstruct_t query = {nodes, input[0], (int)input[0] - 1,
        {wc3_float(input[1]), wc3_float(input[2])}, {wc3_float(input[3]), wc3_float(input[4])}};
    out[0] = wc3_fine_reconstruct(&query, points, 64);
    for (uint32_t i = 0; i < out[0]; i++) {
        out[1+2*i] = wc3_float_bits(points[i].x);
        out[2+2*i] = wc3_float_bits(points[i].y);
    }
}

typedef struct {
    uint32_t const *query, *objects;
    uint8_t const *cells;
    bool *target_hit;
} fineObjectProbe_t;

/* Use production eligibility before constructing edges, rather than supplying
 * a graph with objects already flattened by the Python reference. */
static bool fine_object_cell(void const *data, wc3FinePoint_t pos) {
    fineObjectProbe_t const *probe = data;
    uint32_t const *q = probe->query;
    if ((uint32_t)pos.x >= q[0] || (uint32_t)pos.y >= q[1] ||
        (probe->cells[(uint32_t)pos.y * q[0] + (uint32_t)pos.x] & (q[8] >> 24))) return false;
    /* Original cell records are prepended; the fixture inserts ascending IDs. */
    for (uint32_t remaining = q[10]; remaining; remaining--) {
        uint32_t i = remaining - 1;
        uint32_t const *obj = probe->objects + 7 * i;
        if (pos.x < (int32_t)obj[0] || pos.y < (int32_t)obj[1] ||
            pos.x >= (int32_t)obj[2] || pos.y >= (int32_t)obj[3]) continue;
        if (probe->target_hit && i == q[11] && obj[6] && (obj[4] & 0x01000000)) *probe->target_hit = true;
        if (wc3_fine_object_blocks((wc3FineObject_t){obj[4], obj[5], obj[6]}, q[8], q[9])) return false;
    }
    return true;
}

static uint8_t fine_object_edges(void const *data, wc3FinePoint_t pos) {
    fineObjectProbe_t const *probe = data;
    wc3FineSegment_t query = { .cls = probe->query[7], .cell = fine_object_cell, .data = probe };
    return wc3_fine_cell_edges(&query, pos);
}

typedef struct { uint8_t const *cells; uint32_t const *objects; } fineObjectInput_t;

/* Query: grid input7,class,mask,endpoint-mode,object count. Objects: half-open
 * bounds4,category,flags,linked. This exercises the actual C search policy. */
void pathing_fine_objects(uint32_t const *input, fineObjectInput_t const *data, int32_t *out) {
    fineObjectProbe_t graph = { .query = input, .objects = data->objects, .cells = data->cells };
    wc3FineRequest_t req = { .start = {(int)input[2], (int)input[3]}, .goal = {(int)input[4], (int)input[5]},
        .width = input[0], .height = input[1], .budget = input[6], .edges = fine_object_edges, .data = &graph };
    int at = wc3_fine_search(&fine_probe, &req), length = 0;
    out[0] = at < 0 ? -1 : (int32_t)fine_probe.nodes[at].g;
    out[1] = (int32_t)fine_probe.pops; out[2] = (int32_t)fine_probe.count;
    out[4] = (int32_t)fine_probe.reopens; out[5] = (int32_t)fine_probe.stale;
    for (int node = at; node >= 0; node = fine_probe.nodes[node].parent) length++;
    out[3] = length;
    for (int i = length - 1; i >= 0; i--, at = fine_probe.nodes[at].parent) {
        out[6 + i * 2] = fine_probe.nodes[at].pos.x; out[7 + i * 2] = fine_probe.nodes[at].pos.y;
    }
}

uint32_t pathing_fine_obstruction(uint32_t const *input, fineObjectInput_t const *data) {
    int32_t out[6+2*BZ_WC3_FINE_NODES];
    pathing_fine_objects(input,data,out);
    return fine_probe.observed_obstruction;
}

/* Complete fine request with native fractional source/goal, including the
 * nearest partial reconstruction and original initial-index producer. */
void pathing_fine_request_words(uint32_t const *input, fineObjectInput_t const *data, uint32_t *out) {
    int32_t searched[6+2*BZ_WC3_FINE_NODES];
    wc3FineVector_t points[BZ_WC3_FINE_NODES];
    pathing_fine_objects(input,data,searched);
    bool complete=searched[0]>=0;
    int at=complete ? wc3_fine_node(&fine_probe,&(wc3FineRequest_t){.width=input[0],.height=input[1]},
        (wc3FinePoint_t){(int)input[4],(int)input[5]}) : (int)fine_probe.nearest;
    wc3FineReconstruct_t query={fine_probe.nodes,fine_probe.count,at,{wc3_float(input[11]),wc3_float(input[12])},
        complete ? (wc3FineVector_t){wc3_float(input[13]),wc3_float(input[14])} : wc3_route_center(fine_probe.nodes[at].pos)};
    uint32_t count=wc3_fine_reconstruct(&query,points,BZ_WC3_FINE_NODES);
    out[0]=complete; out[1]=fine_probe.pops; out[2]=fine_probe.count; out[3]=count;
    out[4]=fine_probe.observed_obstruction && count>1 ? count-2 : 0; out[5]=fine_probe.observed_obstruction;
    for(uint32_t i=0;i<count;i++) { out[6+2*i]=wc3_float_bits(points[i].x); out[7+2*i]=wc3_float_bits(points[i].y); }
}

/* A fresh original constructor/control has no preceding obstruction latch. */
void pathing_fine_result_reset(void) {
    wc3_fine_free(&fine_probe); memset(&fine_probe,0,sizeof(fine_probe));
}

/* Complete fine caller: width,height,startXY,goalXY,budget,class,mask,
 * endpoint-mode,object-count,target-index,source words2,goal words2. */
void pathing_fine_result_words(uint32_t const *input, fineObjectInput_t const *data, uint32_t *out) {
    bool hit=false,complete;
    fineObjectProbe_t graph={.query=input,.objects=data->objects,.cells=data->cells,.target_hit=&hit};
    wc3FineRequest_t request={.start={(int)input[2],(int)input[3]},.goal={(int)input[4],(int)input[5]},
        .width=input[0],.height=input[1],.budget=input[6],.edges=fine_object_edges,.data=&graph,.target_hit=&hit};
    wc3FineVector_t points[BZ_WC3_FINE_NODES],source={wc3_float(input[12]),wc3_float(input[13])},
        goal={wc3_float(input[14]),wc3_float(input[15])};
    uint32_t count=wc3_fine_build_route(&fine_probe,&request,source,goal,points,BZ_WC3_FINE_NODES,&complete);
    out[0]=complete;out[1]=fine_probe.pops;out[2]=fine_probe.count;out[3]=count;
    out[4]=fine_probe.observed_obstruction && count>1 ? count-2 : 0;out[5]=fine_probe.observed_obstruction;
    out[6]=count && (points[0].x!=goal.x || points[0].y!=goal.y);
    for(uint32_t i=0;i<count;i++) {out[7+2*i]=wc3_float_bits(points[i].x);out[8+2*i]=wc3_float_bits(points[i].y);}
}

/* Full request failure retains the closest admitted node, even if its goal
 * entry has not yet been popped. Output: result,pops,nodes,count,nearXY,dist2,
 * then the nearest start-to-end parent chain. */
void pathing_fine_partial(uint32_t const *input, fineObjectInput_t const *data, int32_t *out) {
    pathing_fine_objects(input, data, out);
    out[0] = out[0] >= 0;
    int at = (int)fine_probe.nearest, length = 0;
    out[4] = fine_probe.nodes[at].pos.x; out[5] = fine_probe.nodes[at].pos.y;
    out[6] = (int32_t)fine_probe.dist2;
    for (int node = at; node >= 0; node = fine_probe.nodes[node].parent) length++;
    out[3] = length;
    for (int i = length - 1; i >= 0; i--, at = fine_probe.nodes[at].parent) {
        out[7 + i * 2] = fine_probe.nodes[at].pos.x; out[8 + i * 2] = fine_probe.nodes[at].pos.y;
    }
}

/* Target request adds the object identity after the ordinary query words.
 * Emit either the successful chain or the retained nearest chain. */
void pathing_fine_target(uint32_t const *input, fineObjectInput_t const *data, int32_t *out) {
    bool target_hit = false;
    fineObjectProbe_t graph = { .query = input, .objects = data->objects, .cells = data->cells, .target_hit = &target_hit };
    wc3FineRequest_t req = { .start = {(int)input[2], (int)input[3]}, .goal = {(int)input[4], (int)input[5]},
        .width = input[0], .height = input[1], .budget = input[6], .edges = fine_object_edges, .data = &graph, .target_hit = &target_hit };
    int at = wc3_fine_search(&fine_probe, &req), length = 0;
    out[0] = at < 0 ? -1 : (int32_t)fine_probe.nodes[at].g;
    out[1] = (int32_t)fine_probe.pops; out[2] = (int32_t)fine_probe.count;
    out[4] = at >= 0 && (fine_probe.nodes[at].pos.x != req.goal.x || fine_probe.nodes[at].pos.y != req.goal.y);
    if (at < 0) at = (int)fine_probe.nearest;
    out[5] = fine_probe.nodes[at].pos.x; out[6] = fine_probe.nodes[at].pos.y;
    for (int node = at; node >= 0; node = fine_probe.nodes[node].parent) length++;
    out[3] = length;
    for (int i = length - 1; i >= 0; i--, at = fine_probe.nodes[at].parent) {
        out[7 + i * 2] = fine_probe.nodes[at].pos.x; out[8 + i * 2] = fine_probe.nodes[at].pos.y;
    }
}

typedef struct {
    uint32_t width, height, mask, count;
    uint8_t const *cells;
    int32_t *out;
} segmentProbe_t;
static bool segment_probe_cell(void const *data, wc3FinePoint_t pos) {
    segmentProbe_t *probe = (segmentProbe_t *)data;
    if (probe->out) {
        assert(probe->count < 512);
        probe->out[2 + 2 * probe->count] = pos.x;
        probe->out[3 + 2 * probe->count++] = pos.y;
    }
    return pos.x >= 0 && pos.y >= 0 && pos.x < (int)probe->width && pos.y < (int)probe->height &&
           !(probe->cells[pos.y * probe->width + pos.x] & probe->mask);
}
void pathing_segment(uint32_t const *input, uint8_t const *cells, int32_t *out) {
    segmentProbe_t probe = {input[6], input[7], input[8], 0, cells, out};
    wc3FineSegment_t query = { {wc3_float(input[0]), wc3_float(input[1])},
                              {wc3_float(input[2]), wc3_float(input[3])}, wc3_float(input[4]),
                              input[5], segment_probe_cell, &probe };
    out[0] = wc3_segment_test(&query);
    out[1] = probe.count;
}
void pathing_segment_normalize(uint32_t const *input, uint32_t *out) {
    float dir[] = {wc3_float(input[0]), wc3_float(input[1])};
    out[0] = wc3_float_bits(wc3_segment_normalize(dir));
    out[1] = wc3_float_bits(dir[0]); out[2] = wc3_float_bits(dir[1]);
}
uint32_t pathing_segment_waypoint(uint32_t const *input, uint8_t const *cells, uint32_t const *words) {
    segmentProbe_t probe = {input[4], input[5], input[6], 0, cells, NULL};
    wc3FineVector_t points[128];
    assert(input[3] < 128);
    for (uint32_t i = 0; i <= input[3]; i++)
        points[i] = (wc3FineVector_t){wc3_float(words[2*i]), wc3_float(words[2*i + 1])};
    wc3FineSegment_t query = { .start = {wc3_float(input[0]), wc3_float(input[1])},
        .cls = input[2], .cell = segment_probe_cell, .data = &probe };
    return wc3_segment_waypoint(&query, (wc3FineRoute_t){points, input[3]});
}

/* World XY, origin XY, cell dimensions XY: fine words, integer cells and the
 * composed original scalar inverse. Caller metadata is supplied separately. */
void pathing_world_grid(uint32_t const *input, uint32_t *out) {
    for (unsigned k = 0; k < 2; k++) {
        float grid = wc3_grid_coordinate(wc3_float(input[k]), wc3_float(input[k+2]), wc3_float(input[k+4]));
        out[k] = wc3_float_bits(grid);
        out[k+2] = wc3_int_bits(wc3_floor_bits(out[k]));
        out[k+4] = wc3_float_bits(wc3_world_coordinate(grid, wc3_float(input[k+2]), wc3_float(input[k+4])));
    }
}

/* Native fine XY, world velocity XY, world origin XY, elapsed. */
void pathing_native_pose(uint32_t const *input, uint32_t *out) {
    wc3GridPose_t pose = { .grid = {wc3_float(input[0]), wc3_float(input[1])},
        .origin = {wc3_float(input[4]), wc3_float(input[5])} };
    float velocity[2] = {wc3_float(input[2]), wc3_float(input[3])};
    wc3_grid_step(&pose, velocity, wc3_float(input[6]));
    for (unsigned k = 0; k < 2; k++) {
        out[k] = wc3_float_bits(pose.grid[k]); out[k+2] = wc3_float_bits(pose.world[k]);
    }
}

/* Native fine source2, map origin2, admitted fine point2 -> fine2/world2. */
void pathing_fine_pose_write(uint32_t const *input, uint32_t *out) {
    wc3GridPose_t pose={.grid={wc3_float(input[0]),wc3_float(input[1])},
        .origin={wc3_float(input[2]),wc3_float(input[3])}};
    float point[2]={wc3_float(input[4]),wc3_float(input[5])};
    wc3_grid_place_fine(&pose,point);
    for (unsigned k=0;k<2;k++) { out[k]=wc3_float_bits(pose.grid[k]); out[k+2]=wc3_float_bits(pose.world[k]); }
}

/* input: fine pose2, world velocity2, map origin2, requested world2, elapsed. */
void pathing_pose_write(uint32_t const *input, uint32_t *out) {
    wc3GridPose_t pose = { .grid = {wc3_float(input[0]), wc3_float(input[1])},
        .origin = {wc3_float(input[4]), wc3_float(input[5])} };
    float velocity[2] = {wc3_float(input[2]), wc3_float(input[3])};
    float point[2] = {wc3_float(input[6]), wc3_float(input[7])};
    wc3_grid_step(&pose, velocity, wc3_float(input[8]));
    wc3_grid_place(&pose, point);
    for (unsigned k = 0; k < 2; k++) { out[k] = wc3_float_bits(pose.grid[k]); out[k + 2] = wc3_float_bits(pose.world[k]); }
}

/* Original public spawn: origin2, admitted world2 -> retained fine2, world2. */
void pathing_spawn_position(uint32_t const *input, uint32_t *out) {
    wc3GridPose_t pose = {.origin={wc3_float(input[0]),wc3_float(input[1])}};
    float point[2] = {wc3_float(input[2]),wc3_float(input[3])};
    wc3_grid_spawn_place(&pose,point);
    for (unsigned k=0;k<2;k++) {
        out[k]=wc3_float_bits(pose.grid[k]); out[k+2]=wc3_float_bits(pose.world[k]);
    }
}

/* Live bridge input: mover time/epoch/pose/velocity/cap/facing8, clock3, origin2, point2.
 * Output: committed mover8, published world2, queried predicted world2. */
void pathing_position_bridge(uint32_t const *input, uint32_t *out) {
    wc3Clock_t old = { .time = wc3_float(input[0]), .epoch = input[1] };
    wc3Clock_t cur = { .time = wc3_float(input[8]), .epoch = input[9], .span = wc3_float(input[10]) };
    wc3GridPose_t pose = { .grid = {wc3_float(input[2]), wc3_float(input[3])},
        .origin = {wc3_float(input[11]), wc3_float(input[12])} };
    float velocity[2], point[2] = {wc3_float(input[13]), wc3_float(input[14])};
    for (unsigned k = 0; k < 2; k++) velocity[k] = wc3_mul(wc3_float(input[4 + k]), 32);
    wc3_grid_step(&pose, velocity, wc3_elapsed(&cur, &old));
    for (unsigned k = 0; k < 2; k++) out[10 + k] = wc3_float_bits(pose.world[k]);
    wc3_grid_place(&pose, point);
    for (unsigned k = 0; k < 8; k++) out[k] = input[k];
    out[0] = input[8]; out[1] = input[9];
    for (unsigned k = 0; k < 2; k++) {
        out[2 + k] = wc3_float_bits(pose.grid[k]); out[8 + k] = wc3_float_bits(pose.world[k]);
    }
}

/* Time/epoch/span, flags and increment -> advanced time/epoch/span/wrap. */
void pathing_clock_advance(uint32_t const *input, uint32_t *out) {
    wc3Clock_t clock = {wc3_float(input[0]), input[1], wc3_float(input[2])};
    bool wrapped = wc3_clock_advance(&clock, wc3_float(input[4]), input[3]);
    out[0] = wc3_float_bits(clock.time); out[1] = clock.epoch;
    out[2] = wc3_float_bits(clock.span); out[3] = wrapped;
}


/* Public policy2 point admission; actual terrain/object cell data is supplied. */
void pathing_fine_placement(uint32_t const *input, uint8_t const *cells, uint32_t *out) {
    uint32_t query[12] = {input[0],input[1],0,0,0,0,0,input[5],input[6],1,0,0};
    fineObjectProbe_t graph = {query,NULL,cells,NULL};
    wc3FinePlacement_t request = {.point = {wc3_float(input[2]),wc3_float(input[3])},
        .limit = input[4], .footprint = {.cls = input[5], .cell = fine_object_cell, .data = &graph},
        .integer_result = input[7] != 0};
    float point[2]; out[0] = wc3_fine_place(&request,point);
    out[1] = wc3_float_bits(point[0]); out[2] = wc3_float_bits(point[1]);
}

/* The same owner state consumed by the public JASS natives and overlap directions. */
void pathing_random_seed(uint32_t seed, uint32_t output[2]) {
    wc3Random_t state;
    wc3_random_seed(&state, seed); wc3_random_next(&state);
    output[0] = state.sum; output[1] = state.index;
}
void pathing_random_query(uint32_t const input[3], uint32_t words[2], uint32_t output[2]) {
    wc3Random_t state = {.sum = words[0], .index = words[1]};
    float dir[2];
    switch (input[0]) {
    case 0: output[0] = wc3_random_next(&state); break;
    case 1: output[0] = (uint32_t)wc3_random_int(&state, (int32_t)input[1], (int32_t)input[2]); break;
    case 2: output[0] = wc3_float_bits(wc3_random_real(&state, wc3_float(input[1]), wc3_float(input[2]))); break;
    case 3:
        wc3_random_direction(&state, dir);
        for (unsigned i = 0; i < 2; i++) output[i] = wc3_float_bits(dir[i]);
        break;
    }
    words[0] = state.sum; words[1] = state.index;
}

/* Supplied original pair locals, exact state/result words; same kernel used by Move. */
void pathing_repulsion_pair(uint32_t const input[13], uint32_t output[4]) {
    wc3Random_t random = {input[0],input[1]};
    wc3Repulse_t state = {{wc3_float(input[11]),wc3_float(input[12])},0};
    wc3RepulsePair_t pair = {.source={wc3_float(input[7]),wc3_float(input[8])},
        .other={wc3_float(input[9]),wc3_float(input[10])},
        .config={wc3_float(input[2]),wc3_float(input[3]),wc3_float(input[4]),wc3_float(input[5]),wc3_float(input[6])},
        .random=&random};
    wc3_repulse_pair(&state,&pair);
    output[0]=random.sum; output[1]=random.index;
    for (unsigned i=0;i<2;i++) output[i+2]=wc3_float_bits(state.vector[i]);
}
void pathing_repulsion_tail(uint32_t const input[8], uint32_t output[3]) {
    wc3Repulse_t state = {{wc3_float(input[0]),wc3_float(input[1])},input[7]};
    wc3RepulseConfig_t config = {wc3_float(input[2]),wc3_float(input[3]),wc3_float(input[4]),
        wc3_float(input[5]),wc3_float(input[6])};
    wc3_repulse_tail(&state,&config);
    for (unsigned i=0;i<2;i++) output[i]=wc3_float_bits(state.vector[i]);
    output[2]=state.packed;
}

/* Current velocity/player then peer velocity/player/flags/group/identity state. */
uint32_t pathing_yield_decision(uint32_t const input[10]) {
    float velocity[]={wc3_float(input[0]),wc3_float(input[1])};
    wc3YieldPeer_t peer={{wc3_float(input[3]),wc3_float(input[4])},input[5],input[6],input[7],input[8],input[9]};
    return wc3_yield_decide(velocity,input[2],&peer);
}
uint32_t pathing_yield_advance(uint32_t *delay, uint32_t disabled) {
    return wc3_yield_advance(delay,disabled!=0);
}

#ifdef BZ_WC3_FINE_TRACE
void pathing_adaptive_node_state(uint32_t *out) {
    out[0]=adaptive_probe.work.count;
    for(uint32_t i=0;i<adaptive_probe.work.count;i++) {
        wc3FineNode_t const *n=adaptive_probe.work.nodes+i;
        uint32_t words[]={n->pos.x,n->pos.y,n->g,n->h,n->gen,(uint32_t)n->parent,n->state,adaptive_probe.levels[i]};
        memcpy(out+1+8*i,words,sizeof(words));
    }
}
static void fine_probe_pop_trace(void *data, uint32_t const words[10]) {
    uint32_t *out=data;
    uint32_t at=out[0]++;
    assert(at<BZ_WC3_FINE_WORK);
    memcpy(out+1+10*at,words,10*sizeof(*words));
}
/* Semantic final node state after the most recent complete request. */
void pathing_fine_node_state(uint32_t *out) {
    out[0]=fine_probe.count;
    for(uint32_t i=0;i<fine_probe.count;i++) {
        wc3FineNode_t const *n=fine_probe.nodes+i;
        uint32_t words[]={n->pos.x,n->pos.y,n->g,n->h,n->gen,(uint32_t)n->parent,n->state};
        memcpy(out+1+7*i,words,sizeof(words));
    }
}
void pathing_fine_queue_trace(uint32_t const *input,fineObjectInput_t const *data,uint32_t *out) {
    int32_t result[6+2*BZ_WC3_FINE_NODES];
    out[0]=0; fine_probe.pop_trace=fine_probe_pop_trace; fine_probe.trace_data=out;
    pathing_fine_objects(input,data,result);
    fine_probe.pop_trace=NULL; fine_probe.trace_data=NULL;
}
#endif

/* The same active fine-chain producer used by the game map owner. */
#include "wc3_spatial_engine_probe.c"

#include "games/warcraft-3/common/wc3_pathing_gate.h"
uint32_t pathing_waygate_id_allocate(uint8_t used[BZ_WC3_GATE_RECORDS]) { return wc3_gate_allocate(used); }

/* Diagnostic owner of the same marker plane/reducer used by g_world.c.
 * State is four lane arrays per level, followed by base marker bytes. */
void pathing_waygate_publish(uint32_t const q[8],uint32_t id,uint8_t *state) {
    uint32_t widths[4],heights[4];uint8_t *classes[4][4],*cursor=state;
    if(id>=BZ_WC3_GATE_RECORDS)return;
    for(unsigned level=0;level<4;level++) {
        widths[level]=((q[0]+16)/2+1)>>level;heights[level]=((q[1]+16)/2+1)>>level;
        for(unsigned lane=0;lane<4;lane++){classes[level][lane]=cursor;cursor+=widths[level]*heights[level];}
    }
    float ax=wc3_float(q[2]),ay=wc3_float(q[3]),bx=wc3_float(q[4]),by=wc3_float(q[5]);
    float minx=wc3_grid_coordinate(ax<bx?ax:bx,wc3_float(q[6]),32),miny=wc3_grid_coordinate(ay<by?ay:by,wc3_float(q[7]),32);
    float maxx=wc3_grid_coordinate(ax>bx?ax:bx,wc3_float(q[6]),32),maxy=wc3_grid_coordinate(ay>by?ay:by,wc3_float(q[7]),32);
    int x0=(int)wc3_int_bits(wc3_floor_bits(wc3_float_bits(minx))),y0=(int)wc3_int_bits(wc3_floor_bits(wc3_float_bits(miny)));
    int x1=(int)(wc3_int_bits(wc3_floor_bits(wc3_float_bits(maxx)))+1u),y1=(int)(wc3_int_bits(wc3_floor_bits(wc3_float_bits(maxy)))+1u);
    if(x0<0)x0=0;
    if(y0<0)y0=0;
    if(x1>(int)q[0])x1=q[0];
    if(y1>(int)q[1])y1=q[1];
    if(x0>=x1 || y0>=y1)return;
    wc3_gate_stamp(cursor,widths[0],heights[0],x0,y0,x1,y1,(uint8_t)id);
    for(unsigned level=1;level<4;level++) {
        unsigned scale=2u<<level,right=(unsigned)x1/scale+1,bottom=(unsigned)y1/scale+1;
        if(right>widths[level])right=widths[level];
        if(bottom>heights[level])bottom=heights[level];
        for(unsigned lane=0;lane<4;lane++)for(unsigned y=(unsigned)y0/scale;y<bottom;y++)for(unsigned x=(unsigned)x0/scale;x<right;x++)
            classes[level][lane][y*widths[level]+x]=wc3_gate_parent(classes[level-1][lane],level==1?cursor:NULL,widths[level-1],heights[level-1],x*2,y*2);
    }
}

uint32_t pathing_gate_parent(uint32_t width,uint32_t height,uint8_t const *classes,uint8_t const *markers) {
    return wc3_gate_parent(classes,markers,width,height,0,0);
}

/* Original special-edge requests use the production adaptive kernel and its
 * separate source/incoming bytes. Standalone ordinary callers stay disabled. */
void pathing_adaptive_special_route(uint32_t const *q,uint8_t const *classes,
        uint8_t const *markers,wc3AccGate_t const *gates,uint32_t *out) {
    adaptive_probe.markers=markers;adaptive_probe.gates=gates;adaptive_probe.warp=q[8];
    pathing_adaptive_route(q,classes,out);
    memmove(out+5,out+4,(size_t)out[3]*8);out[4]=adaptive_probe.warps;
    adaptive_probe.warp=false;adaptive_probe.markers=NULL;adaptive_probe.gates=NULL;
}
void pathing_adaptive_special_node_state(uint32_t *out) {
    out[0]=adaptive_probe.work.count;
    for(uint32_t i=0;i<adaptive_probe.work.count;i++) {
        wc3FineNode_t const *n=adaptive_probe.work.nodes+i;
        uint32_t words[]={n->pos.x,n->pos.y,n->g,n->h,n->gen,(uint32_t)n->parent,n->state,
            adaptive_probe.levels[i],adaptive_probe.source_ids[i],adaptive_probe.gate_ids[i]};
        memcpy(out+1+10*i,words,sizeof(words));
    }
}
void pathing_adaptive_special_distance(uint32_t const *q,uint8_t const *classes,
        uint8_t const *markers,wc3AccGate_t const *gates,uint32_t *out) {
    static wc3AccSearch_t search;
    search.markers=markers;search.gates=gates;search.warp=q[8];
    uint32_t offset=0;
    for(unsigned level=0;level<4;level++) {
        uint32_t width=q[0]>>level,height=q[1]>>level;
        search.maps[level]=(wc3AccMap_t){width,height,classes+offset,malloc((size_t)width*height*sizeof(int))};
        assert(search.maps[level].indices);offset+=width*height;
    }
    wc3AccRequest_t req={{wc3_float(q[4]),wc3_float(q[5])},{wc3_float(q[6]),wc3_float(q[7])},1u<<q[2],q[3]};
    wc3FineVector_t endpoint;out[0]=wc3_acc_query_distance(&search,&req,&endpoint);
    out[1]=wc3_float_bits(endpoint.x);out[2]=wc3_float_bits(endpoint.y);out[3]=search.warps;
    out[4]=search.work.pops;out[5]=search.work.count;
    for(unsigned level=0;level<4;level++)free(search.maps[level].indices);
}

/* 165d10 controlled consumer scope: the owner-active lookup and physical
 * placement outcome are supplied; public engine trajectories prove placement. */
typedef struct {uint32_t active,placement,calls,point[2];} gateConsumerProbe_t;
static bool gate_probe_active(void const *data,uint8_t id) {
    (void)id;return ((gateConsumerProbe_t const *)data)->active&1;
}
static bool gate_probe_place(void *data,wc3FineVector_t point) {
    gateConsumerProbe_t *probe=data;probe->calls++;
    probe->point[0]=wc3_float_bits(point.x);probe->point[1]=wc3_float_bits(point.y);
    return probe->placement;
}
void pathing_adaptive_gate_consumer(uint32_t const *q,uint32_t const *words,uint32_t *out) {
    wc3FineVector_t points[8];assert(q[0]<=8);
    for(uint32_t i=0;i<q[0];i++)points[i]=(wc3FineVector_t){wc3_float(words[2*i]),wc3_float(words[2*i+1])};
    gateConsumerProbe_t probe={q[3],q[4],0,{0,0}};wc3FineRoute_t route={points,q[1]};bool warped;
    out[0]=wc3_acc_advance(&route,q[2],gate_probe_active,gate_probe_place,&probe,&warped);
    out[1]=route.index;out[2]=warped;out[3]=probe.calls;out[4]=probe.point[0];out[5]=probe.point[1];
}

/* Original165f10: controlled force0/1 and supplied placement result. */
void pathing_adaptive_gate_threshold(uint32_t const *q,uint32_t const *words,uint32_t *out) {
    wc3FineVector_t points[8];assert(q[0]<=8 && q[1]<q[0]);
    for(uint32_t i=0;i<q[0];i++)points[i]=(wc3FineVector_t){wc3_float(words[2*i]),wc3_float(words[2*i+1])};
    gateConsumerProbe_t probe={q[3],q[4],0,{0,0}};wc3FineRoute_t route={points,q[1]};bool warped;
    out[0]=0;out[2]=q[7];out[3]=q[8];
    if(route.index && (q[2] || wc3_acc_in_range((wc3FineVector_t){wc3_float(q[5]),wc3_float(q[6])},points[route.index]))) {
        if(!wc3_acc_advance(&route,true,gate_probe_active,gate_probe_place,&probe,&warped)) {
            out[0]=2;if(out[3]<20)out[3]=20;
        } else {out[0]=warped;out[2]=UINT32_MAX;}
    }
    out[1]=route.index;out[4]=probe.calls;out[5]=probe.point[0];out[6]=probe.point[1];
}
