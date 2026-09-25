#ifndef WC3_SC2API_COMPAT_H
#define WC3_SC2API_COMPAT_H

#include "../g_local.h"

/* Blizzard SC2 Race values 0-4 stay unchanged. OpenRealm extends the enum
 * with Warcraft III races so existing wire values remain compatible. */
#define WC3_SC2_DESTRUCTABLE_TYPE_FLAG 0x80000000u

/* Warcraft unit and destructable rawcodes live in separate object-data namespaces
 * and may collide. SC2 Unit.unit_type has one uint32 namespace, so reserve the
 * high bit for destructable catalog ids; standard FOURCC bytes leave that bit clear. */
static inline DWORD WC3_SC2API_DestructableTypeId(DWORD rawcode) {
    return rawcode | WC3_SC2_DESTRUCTABLE_TYPE_FLAG;
}

typedef enum {
    WC3_SC2_RACE_NONE = 0,
    WC3_SC2_RACE_TERRAN = 1,
    WC3_SC2_RACE_ZERG = 2,
    WC3_SC2_RACE_PROTOSS = 3,
    WC3_SC2_RACE_RANDOM = 4,
    WC3_SC2_RACE_HUMAN = 5,
    WC3_SC2_RACE_ORC = 6,
    WC3_SC2_RACE_UNDEAD = 7,
    WC3_SC2_RACE_NIGHT_ELF = 8,
} wc3Sc2Race_t;

/* Protocol-independent subset of SC2API PlayerCommon. The names deliberately
 * match the SC2 wire schema; OpenRealm's mapping is minerals=lumber and
 * vespene=gold. */
typedef struct {
    DWORD player_id;
    DWORD minerals;
    DWORD vespene;
    DWORD food_cap;
    DWORD food_used;
    DWORD food_army;
    DWORD food_workers;
    DWORD idle_worker_count;
    DWORD army_count;
} wc3Sc2PlayerCommon_t;

wc3Sc2Race_t WC3_SC2API_RaceFromPlayerRace(playerRace_t race);
/* Warcraft uses kPlayerRaceNone for a Random race preference in map/lobby
 * configuration. Requested race therefore needs a context-specific mapping,
 * while an unresolved/unknown actual race remains SC2 NoRace. */
wc3Sc2Race_t WC3_SC2API_RequestedRaceFromPlayerRace(playerRace_t race);
/* Agent-facing player arguments are Warcraft player numbers, not indexes into
 * game.clients. Maps may assign a client slot to a different map-player id. */
LPGAMECLIENT WC3_SC2API_PlayerClient(DWORD player);
BOOL WC3_SC2API_FillPlayerCommon(DWORD player, wc3Sc2PlayerCommon_t *out);
uint64_t WC3_SC2API_UnitTag(LPCEDICT ent);
LPEDICT WC3_SC2API_ResolveUnitTag(uint64_t tag);
/* Retire the current SC2-visible incarnation without changing Warcraft's edict/JASS handle lifetime.
 * Used when a WC3 unit/destructable dies but may later be restored on the same edict. */
void WC3_SC2API_AdvanceUnitTagLifetime(LPEDICT ent);
void WC3_SC2API_ResetUnitTags(void);

#endif
