#ifndef BZ_WC3_PATHING_PROFILE_H
#define BZ_WC3_PATHING_PROFILE_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    UNIT_MOVE_UNSPECIFIED, UNIT_MOVE_FOOT, UNIT_MOVE_HORSE, UNIT_MOVE_FLY,
    UNIT_MOVE_HOVER, UNIT_MOVE_FLOAT, UNIT_MOVE_AMPH, UNIT_MOVE_NONE, UNIT_MOVE_UNBUILD
} unitMovementType_t;

typedef enum {
    WC3_SUPPORT_TERRAIN, WC3_SUPPORT_WATER_MAX, WC3_SUPPORT_DEEP_WATER, WC3_SUPPORT_FLIGHT
} wc3SupportPolicy_t;

/* Unit280 bits published by684480 after66d780 has selected support. */
enum { WC3_SUPPORT_ON_DECK = 0x02, WC3_SUPPORT_IN_DEEP_WATER = 0x20 };

typedef struct {
    char const *name;
    uint8_t bits, query, category, path_class;
    wc3SupportPolicy_t support;
} wc3MovementProfile_t;

/*685340/685e30/685db0: authored movement bits, fine query, occupied
 * category and coarse class are independent. Unknown names publish zeros;
 * none of these fields determines whether the unit has a usable Move owner. */
static wc3MovementProfile_t const wc3_movement_profiles[]={
    {"",       0x00,0x00,0x00,0,WC3_SUPPORT_TERRAIN},
    {"foot",   0x01,0x02,0xca,0,WC3_SUPPORT_TERRAIN},
    {"horse",  0x04,0x02,0xca,0,WC3_SUPPORT_TERRAIN},
    {"fly",    0x02,0x04,0x00,3,WC3_SUPPORT_FLIGHT},
    {"hover",  0x08,0x02,0xca,0,WC3_SUPPORT_WATER_MAX},
    {"float",  0x10,0x40,0xca,2,WC3_SUPPORT_WATER_MAX},
    {"amph",   0x20,0x80,0xca,1,WC3_SUPPORT_DEEP_WATER},
    {NULL,     0x00,0x00,0x00,0,WC3_SUPPORT_TERRAIN},
    {"unbuild",0x40,0x00,0x08,0,WC3_SUPPORT_TERRAIN},
};

static inline unsigned wc3_movement_fold(unsigned c) {
    return c>='A' && c<='Z' ? c+'a'-'A' : c;
}

static inline unitMovementType_t wc3_movement_parse(char const *name) {
    if(!name || !*name)return UNIT_MOVE_UNSPECIFIED;
    for(unsigned type=UNIT_MOVE_FOOT;type<=UNIT_MOVE_UNBUILD;type++) {
        char const *candidate=wc3_movement_profiles[type].name;
        if(!candidate)continue;
        unsigned i=0;
        while(candidate[i] && wc3_movement_fold((uint8_t)name[i])==(unsigned)candidate[i])i++;
        if(!candidate[i] && !name[i])return (unitMovementType_t)type;
    }
    return UNIT_MOVE_NONE;
}

static inline wc3MovementProfile_t const *wc3_movement_profile(unitMovementType_t type) {
    return wc3_movement_profiles+((unsigned)type<=UNIT_MOVE_UNBUILD ? type : UNIT_MOVE_UNSPECIFIED);
}

static inline wc3MovementProfile_t const *wc3_movement_profile_bits(uint32_t bits) {
    for(unsigned type=UNIT_MOVE_FOOT;type<=UNIT_MOVE_UNBUILD;type++)
        if(bits==wc3_movement_profiles[type].bits)return wc3_movement_profiles+type;
    return wc3_movement_profiles;
}

static inline uint8_t wc3_movement_coarse_mask(unsigned path_class) {
    static uint8_t const lanes[]={2,0x80,0x40,4};
    return lanes[path_class&3];
}

#endif
