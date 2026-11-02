/* Production immutable profile table, exercised by the original-code oracle. */
#include "../../games/warcraft-3/common/wc3_pathing_profile.h"

uint32_t wc3_profile_parse(char const *text) {
    return wc3_movement_profile(wc3_movement_parse(text))->bits;
}

uint32_t wc3_profile_mapping(uint32_t bits, unsigned field) {
    wc3MovementProfile_t const *profile=wc3_movement_profile_bits(bits);
    switch(field) {
    case 0: return profile->query;
    case 1: return profile->category;
    case 2: return profile->path_class;
    case 3: return wc3_movement_coarse_mask(profile->path_class);
    default: return UINT32_MAX;
    }
}
