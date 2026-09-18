/*
 * Live-map hero walk/save/load diagnostic. Armed by wc3_hero_saveload_audit=1;
 * one isolated dedicated process per campaign map prints HERO_SAVELOAD lines.
 */
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include "g_local.h"

#define HSA_WALK_DIST 80.0f // world units; short enough for direct steering, long enough to observe motion
#define HSA_WAIT_FRAMES 150 // frames; 15s at 10 Hz for cinematic skip / scripted spawn
#define HSA_MOVE_FRAMES 40 // frames; 4s to observe a walk step before save
#define HSA_ORIGIN_EPS 0.5f // world units; restored origin must match the pre-save snapshot

typedef enum {
    HSA_IDLE,
    HSA_WAIT_HERO,
    HSA_WALK,
    HSA_DONE
} hsaPhase_t;

typedef struct {
    DWORD number, class_id, added_count;
    VECTOR3 origin;
    DWORD abilities[MAX_HERO_ABILITIES], levels[MAX_HERO_ABILITIES];
    DWORD added[MAX_ABILITIES];
    DWORD inventory[MAX_INVENTORY], charges[MAX_INVENTORY];
    char move[32];
} hsaSnap_t;

static hsaPhase_t hsa_phase;
static PATHSTR hsa_map;
static DWORD hsa_wait, hsa_walk, hsa_index;
static hsaSnap_t hsa_before;

static void hsa_fourcc(DWORD code, char out[5]) {
    memcpy(out, &code, 4);
    out[4] = '\0';
}

static void hsa_append(LPSTR out, DWORD size, LPCSTR fmt, ...) {
    va_list args;
    DWORD used = (DWORD)strlen(out);
    if (used >= size) return;
    va_start(args, fmt);
    vsnprintf(out + used, size - used, fmt, args);
    va_end(args);
}

static void hsa_capture(LPCEDICT hero, hsaSnap_t *snap) {
    memset(snap, 0, sizeof(*snap));
    if (!hero) return;
    snap->number = hero->s.number;
    snap->class_id = hero->class_id;
    snap->origin = hero->s.origin;
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        snap->abilities[i] = hero->heroabilities[i].code;
        snap->levels[i] = hero->heroabilities[i].level;
    }
    snap->added_count = ARRAY_COUNT(hero->abilities.added);
    if (snap->added_count > MAX_ABILITIES) snap->added_count = MAX_ABILITIES;
    FOR_LOOP(i, snap->added_count) snap->added[i] = hero->abilities.added[i];
    FOR_LOOP(i, MAX_INVENTORY) {
        if (!hero->inventory[i]) continue;
        snap->inventory[i] = hero->inventory[i]->class_id;
        snap->charges[i] = hero->inventory[i]->item.charges;
    }
    if (hero->currentmove && hero->currentmove->animation)
        strlcpy(snap->move, hero->currentmove->animation, sizeof(snap->move));
}

void G_FormatHeroSaveSnap(LPCEDICT hero, LPSTR out, DWORD out_size) {
    hsaSnap_t snap;
    char code[5];
    BOOL any;

    if (!out || !out_size) return;
    out[0] = '\0';
    if (!hero) {
        snprintf(out, out_size, "unit=none");
        return;
    }
    hsa_capture(hero, &snap);
    hsa_fourcc(snap.class_id, code);
    snprintf(out, out_size, "unit=%u class=%s origin=%.2f,%.2f,%.2f abilities=",
             (unsigned)snap.number, code, snap.origin.x, snap.origin.y, snap.origin.z);
    any = false;
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        if (!snap.abilities[i]) continue;
        hsa_fourcc(snap.abilities[i], code);
        hsa_append(out, out_size, "%s%s:%u", any ? "," : "", code, (unsigned)snap.levels[i]);
        any = true;
    }
    if (!any) hsa_append(out, out_size, "-");
    hsa_append(out, out_size, " added=");
    any = false;
    FOR_LOOP(i, snap.added_count) {
        if (!snap.added[i]) continue;
        hsa_fourcc(snap.added[i], code);
        hsa_append(out, out_size, "%s%s", any ? "," : "", code);
        any = true;
    }
    if (!any) hsa_append(out, out_size, "-");
    hsa_append(out, out_size, " inventory=");
    any = false;
    FOR_LOOP(i, MAX_INVENTORY) {
        if (!snap.inventory[i]) continue;
        hsa_fourcc(snap.inventory[i], code);
        hsa_append(out, out_size, "%s%s:%u", any ? "," : "", code, (unsigned)snap.charges[i]);
        any = true;
    }
    if (!any) hsa_append(out, out_size, "-");
    hsa_append(out, out_size, " move=%s", snap.move[0] ? snap.move : "none");
}

static LPEDICT hsa_find_hero(void) {
    FOR_LOOP(player, MAX_PLAYERS) {
        if (level.mapinfo && (!level.mapinfo->players[player].used ||
                level.mapinfo->players[player].playerType != kPlayerTypeHuman))
            continue;
        FOR_LOOP(i, globals.num_edicts) {
            LPEDICT ent = g_edicts + i;
            if (ent->inuse && (ent->svflags & SVF_MONSTER) && !M_IsDead(ent) &&
                    G_UnitIsHero(ent) && ent->s.player == player)
                return ent;
        }
        if (!level.mapinfo) break;
    }
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = g_edicts + i;
        if (ent->inuse && (ent->svflags & SVF_MONSTER) && !M_IsDead(ent) && G_UnitIsHero(ent))
            return ent;
    }
    return NULL;
}

static BOOL hsa_issue_walk(LPEDICT hero) {
    static VECTOR2 const dirs[] = {
        { HSA_WALK_DIST, 0 }, { -HSA_WALK_DIST, 0 }, { 0, HSA_WALK_DIST }, { 0, -HSA_WALK_DIST },
        { 56, 56 }, { -56, 56 }, { 56, -56 }, { -56, -56 }
    };
    VECTOR2 dest;
    FOR_LOOP(i, sizeof(dirs) / sizeof(dirs[0])) {
        dest.x = hero->s.origin2.x + dirs[i].x;
        dest.y = hero->s.origin2.y + dirs[i].y;
        if (unit_issueorder(hero, "move", &dest)) return true;
    }
    return false;
}

static LPCSTR hsa_compare(LPCEDICT hero, hsaSnap_t const *want) {
    hsaSnap_t got;
    if (!hero) return "fail_missing";
    hsa_capture(hero, &got);
    if (got.number != want->number || got.class_id != want->class_id) return "fail_identity";
    if (fabsf(got.origin.x - want->origin.x) > HSA_ORIGIN_EPS ||
            fabsf(got.origin.y - want->origin.y) > HSA_ORIGIN_EPS ||
            fabsf(got.origin.z - want->origin.z) > HSA_ORIGIN_EPS)
        return "fail_origin";
    FOR_LOOP(i, MAX_HERO_ABILITIES)
        if (got.abilities[i] != want->abilities[i] || got.levels[i] != want->levels[i])
            return "fail_abilities";
    if (got.added_count != want->added_count) return "fail_abilities";
    FOR_LOOP(i, want->added_count)
        if (got.added[i] != want->added[i]) return "fail_abilities";
    FOR_LOOP(i, MAX_INVENTORY)
        if (got.inventory[i] != want->inventory[i] || got.charges[i] != want->charges[i])
            return "fail_inventory";
    return "pass";
}

static void hsa_finish(LPCSTR status, LPCEDICT hero) {
    char snap[512];
    G_FormatHeroSaveSnap(hero, snap, sizeof(snap));
    fprintf(stderr, "HERO_SAVELOAD status=%s %s\n", status, snap);
    hsa_phase = HSA_DONE;
}

static void hsa_saveload(LPEDICT hero) {
    PATHSTR path;
    char snap[512];
    LPCSTR status;

    hsa_capture(hero, &hsa_before);
    G_FormatHeroSaveSnap(hero, snap, sizeof(snap));
    fprintf(stderr, "HERO_SAVELOAD snapshot=before %s\n", snap);
    gi.SavePath("hero-saveload-audit", path, sizeof(path));
    if (!WriteGame(path)) {
        hsa_finish("fail_save", hero);
        return;
    }
    if (!ReadGame(path)) {
        hsa_finish("fail_load", hero);
        return;
    }
    hero = (hsa_before.number < globals.num_edicts) ? g_edicts + hsa_before.number : NULL;
    if (!hero || !hero->inuse) {
        hsa_finish("fail_missing", NULL);
        return;
    }
    G_FormatHeroSaveSnap(hero, snap, sizeof(snap));
    fprintf(stderr, "HERO_SAVELOAD snapshot=after %s\n", snap);
    status = hsa_compare(hero, &hsa_before);
    hsa_finish(status, hero);
}

void G_HeroSaveLoadAuditFrame(void) {
    LPCSTR armed = gi.CvarString ? gi.CvarString("wc3_hero_saveload_audit", "0") : "0";
    LPEDICT hero;

    if (!armed || atoi(armed) == 0) return;
    if (!level.started || !level.map_path[0]) return;
    if (strcmp(hsa_map, level.map_path)) {
        strlcpy(hsa_map, level.map_path, sizeof(hsa_map));
        hsa_phase = HSA_WAIT_HERO;
        hsa_wait = hsa_walk = hsa_index = 0;
        memset(&hsa_before, 0, sizeof(hsa_before));
    }
    if (hsa_phase == HSA_IDLE || hsa_phase == HSA_DONE) return;
    hero = hsa_index ? g_edicts + hsa_index : hsa_find_hero();
    if (hsa_phase == HSA_WAIT_HERO) {
        if (hero) {
            hsa_index = hero->s.number;
            hsa_before.origin = hero->s.origin;
            if (!hsa_issue_walk(hero)) {
                hsa_finish("fail_order", hero);
                return;
            }
            hsa_phase = HSA_WALK;
            hsa_walk = 0;
            return;
        }
        if (++hsa_wait >= HSA_WAIT_FRAMES) hsa_finish("no_hero", NULL);
        return;
    }
    if (!hero || !hero->inuse || M_IsDead(hero) || !G_UnitIsHero(hero)) {
        hsa_finish("fail_missing", hero);
        return;
    }
    hsa_walk++;
    if (fabsf(hero->s.origin.x - hsa_before.origin.x) > 1.0f ||
            fabsf(hero->s.origin.y - hsa_before.origin.y) > 1.0f) {
        hsa_saveload(hero);
        return;
    }
    if (hsa_walk >= HSA_MOVE_FRAMES) hsa_finish("fail_move", hero);
}
