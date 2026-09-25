#ifdef WC3_SC2API

#include "sc2api_compat.h"

wc3Sc2Race_t WC3_SC2API_RaceFromPlayerRace(playerRace_t race) {
    switch (race) {
        case kPlayerRaceHuman: return WC3_SC2_RACE_HUMAN;
        case kPlayerRaceOrc: return WC3_SC2_RACE_ORC;
        case kPlayerRaceUndead: return WC3_SC2_RACE_UNDEAD;
        case kPlayerRaceNightElf: return WC3_SC2_RACE_NIGHT_ELF;
        default: return WC3_SC2_RACE_NONE;
    }
}

wc3Sc2Race_t WC3_SC2API_RequestedRaceFromPlayerRace(playerRace_t race) {
    if (race == kPlayerRaceNone) return WC3_SC2_RACE_RANDOM;
    return WC3_SC2API_RaceFromPlayerRace(race);
}

LPGAMECLIENT WC3_SC2API_PlayerClient(DWORD player) {
    if (player >= MAX_PLAYERS) return NULL;
    FOR_LOOP(i, game.max_clients) {
        LPGAMECLIENT client = &game.clients[i];
        if (client->ps.number == player) return client;
    }
    return NULL;
}

BOOL WC3_SC2API_FillPlayerCommon(DWORD player, wc3Sc2PlayerCommon_t *out) {
    LPGAMECLIENT client;

    if (!out || !(client = WC3_SC2API_PlayerClient(player))) return false;
    memset(out, 0, sizeof(*out));
    /* SC2API player ids are externally one-based; OpenRealm's player slots are zero-based. */
    out->player_id = player + 1;
    out->minerals = client->ps.stats[PLAYERSTATE_RESOURCE_LUMBER];
    out->vespene = client->ps.stats[PLAYERSTATE_RESOURCE_GOLD];
    out->food_cap = client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_CAP];
    out->food_used = client->ps.stats[PLAYERSTATE_RESOURCE_FOOD_USED];

    /* SC2 splits food and counts into worker/army buckets. Warcraft does not
     * maintain those aggregates, so derive them from the authoritative owned
     * unit set. Hidden workers inside a mine still count; units that are dead,
     * still training, or buildings do not. */
    FILTER_EDICTS(ent, ent->inuse && (ent->svflags & SVF_MONSTER) &&
                  ent->s.player == player && ent->data.UnitBalance &&
                  !ent->training && !M_IsDead(ent) && !G_UnitIsBuilding(ent->class_id)) {
        DWORD const food = (DWORD)MAX(0, ent->food.used);
        if (G_UnitIsWorker(ent)) {
            out->food_workers += food;
            if (G_UnitIsIdleWorker(ent)) out->idle_worker_count++;
        } else {
            out->food_army += food;
            out->army_count++;
        }
    }
    return true;
}

static DWORD sc2_tag_source_spawn_times[MAX_ENTITIES];
static DWORD sc2_tag_generations[MAX_ENTITIES];
static DWORD sc2_next_tag_generation = 1;

static DWORD sc2_new_tag_generation(void) {
    DWORD generation = sc2_next_tag_generation++;
    if (!generation) generation = sc2_next_tag_generation++;
    if (!sc2_next_tag_generation) sc2_next_tag_generation = 1;
    return generation;
}

static DWORD sc2_tag_generation(LPCEDICT ent) {
    DWORD number;
    if (!ent || !ent->inuse || ent->s.number < 0 || ent->s.number >= MAX_ENTITIES) return 0;
    number = (DWORD)ent->s.number;
    if (sc2_tag_source_spawn_times[number] != ent->spawn_time || !sc2_tag_generations[number]) {
        sc2_tag_source_spawn_times[number] = ent->spawn_time;
        sc2_tag_generations[number] = sc2_new_tag_generation();
    }
    return sc2_tag_generations[number];
}

uint64_t WC3_SC2API_UnitTag(LPCEDICT ent) {
    DWORD const generation = sc2_tag_generation(ent);
    if (!generation) return 0;
    return ((uint64_t)generation << 32) | (uint32_t)ent->s.number;
}

LPEDICT WC3_SC2API_ResolveUnitTag(uint64_t tag) {
    DWORD const number = (DWORD)(tag & UINT32_MAX);
    LPEDICT ent;

    if (!tag || number >= globals.num_edicts) return NULL;
    ent = &g_edicts[number];
    if (!ent->inuse || ent->s.number != (LONG)number || WC3_SC2API_UnitTag(ent) != tag) return NULL;
    return ent;
}

void WC3_SC2API_AdvanceUnitTagLifetime(LPEDICT ent) {
    DWORD number;
    if (!ent || !ent->inuse || ent->s.number < 0 || ent->s.number >= MAX_ENTITIES) return;
    number = (DWORD)ent->s.number;
    /* Ensure the dying incarnation has a generation even if it was never observed,
     * then retire it. A later WC3 resurrection on the same edict receives a new
     * opaque SC2 tag while the JASS handle and spawn_time remain untouched. */
    (void)sc2_tag_generation(ent);
    sc2_tag_source_spawn_times[number] = ent->spawn_time;
    sc2_tag_generations[number] = sc2_new_tag_generation();
}

void WC3_SC2API_ResetUnitTags(void) {
    memset(sc2_tag_source_spawn_times, 0, sizeof(sc2_tag_source_spawn_times));
    memset(sc2_tag_generations, 0, sizeof(sc2_tag_generations));
    sc2_next_tag_generation = 1;
}

#endif
