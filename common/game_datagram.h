#ifndef common_game_datagram_h
#define common_game_datagram_h

#include "common/shared.h"

#define BZ_GAME_DATAGRAM_ENTITY_TINTS 0x8000u // bit mask; reserves the first weather-count bit for entity RGBA data
#define BZ_GAME_DATAGRAM_TERRAIN_MASK 0x4000u // bit mask; reserves the next weather-count bit for a synchronized terrain mask
#define BZ_GAME_DATAGRAM_LIGHTNING 0x2000u // bit mask; reserves the next weather-count bit for WC3 lightning state
#define BZ_GAME_DATAGRAM_FLAGS (BZ_GAME_DATAGRAM_ENTITY_TINTS | BZ_GAME_DATAGRAM_TERRAIN_MASK | BZ_GAME_DATAGRAM_LIGHTNING) // bits; all extension flags above the weather count
#define BZ_GAME_DATAGRAM_COUNT_MASK 0x1FFFu // bit mask; low 13 bits carry the weather effect count on the wire

#ifdef MAX_WEATHER_EFFECTS
_Static_assert(MAX_WEATHER_EFFECTS < BZ_GAME_DATAGRAM_LIGHTNING, "weather count must leave the extension bits free");
_Static_assert((MAX_WEATHER_EFFECTS & ~BZ_GAME_DATAGRAM_COUNT_MASK) == 0, "weather count must fit the count mask");
#endif

#endif
