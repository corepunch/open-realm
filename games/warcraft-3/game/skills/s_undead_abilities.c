#include "s_skills.h"

#define BZ_UPGRADE_RAISE_DEAD_LIFE MAKEFOURCC('r','r','a','i')
#define RAISE_DEAD_SUMMON_LIMIT 25u

#define UNDEAD_AUTOCAST_RADIUS 900.0f // world units; fallback acquisition radius when the spell range is zero
#define BZ_AMS_SHIELD MAKEFOURCC('B', 'a', 'm', '2') // rawcode; Bam2 DataC spell-damage absorption

static LPCSTR undead_buff(abilityitem_t const *spell, DWORD level) {
    LPCSTR buff = G_AbilityLevel(spell->code, level)->buffID;
    return buff && strlen(buff) >= 4 ? buff : NULL;
}

/* BuffID is "Bams,Bam2"; DataC selects the token. Index 0 is Bams, index 1 is Bam2. */
static LPCSTR ams_buff_token(LPCSTR list, DWORD index) {
    DWORD i = 0;
    if (!list) return NULL;
    for (;;) {
        if (strlen(list) < 4) return NULL;
        if (i == index) return list;
        list = strchr(list, ',');
        if (!list) return NULL;
        list++; i++;
    }
}

/* DataC > 0 is the TFT melee shield (Aam2); empty DataC is ROC-style targeting immunity (Aams/ACam). */
static void anti_magic_shell_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT absorb = S_SpellData(spell->code, level, 3);
    LPCSTR list = G_AbilityLevel(spell->code, level)->buffID;
    LPCSTR buff = ams_buff_token(list, absorb > 0.0f ? 1 : 0);
    heroabilitystatus_t *slot;
    (void)caster;
    if (!st.entity) return;
    /* ROC AbilityData omits BuffID; UndeadAbilityStrings still names Bams as the shell buff. */
    if (!buff) buff = absorb > 0.0f ? "Bam2" : "Bams";
    unit_addtimedstatus(st.entity, buff, level, S_SpellDuration(spell->code, level, G_UnitIsHero(st.entity)));
    if (absorb > 0.0f) {
        FOR_LOOP(i, MAX_UNIT_STATUSES) {
            slot = st.entity->abilstatus + i;
            if (slot->level && slot->code == *((DWORD const *)buff)) { slot->data = (DWORD)absorb; break; }
        }
    }
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, st.entity, NULL, true);
}

/* Name=Anti-magic Shell
 * Ubertip="Creates a barrier that stops spells from affecting a target unit. |nLasts <Aams,Dur1> seconds."
 * Aam2 Ubertip="Creates a barrier that stops <Aam2,DataC1> points of spell damage from affecting a target unit."
 */
BZ_SIMPLE_SPELL_PROC(AbilityAntiMagicShell) { anti_magic_shell_execute(caster, st, spell); }

/* Item Instant AMS (Aami/AIxs): same Bams/Bam2 DataC path; distinct TFT class, not an Aams alias. */
BZ_SIMPLE_SPELL_PROC(AbilityAntiMagicShellInstant) { anti_magic_shell_execute(caster, st, spell); }

/* Bam2 is not magic-immune: spells may target the unit, but S_SpellDamage consumes the authored pool first. */
int S_AntiMagicShellAbsorb(LPEDICT target, int damage) {
    heroabilitystatus_t *slot = NULL;
    if (!target || damage <= 0) return damage;
    FOR_LOOP(i, MAX_UNIT_STATUSES)
        if (target->abilstatus[i].level && target->abilstatus[i].code == BZ_AMS_SHIELD &&
            (!target->abilstatus[i].timestamp || target->abilstatus[i].timestamp > G_Time())) {
            slot = target->abilstatus + i; break;
        }
    if (!slot) return damage;
    if (slot->data >= (DWORD)damage) { slot->data -= (DWORD)damage; return 0; }
    damage -= (int)slot->data;
    memset(slot, 0, sizeof(*slot));
    return damage;
}

/* Shared unit-target autocast acquire: friendly wounded targets for replenish. */
static BOOL undead_unit_autocast_acquire(LPEDICT caster, DWORD code, BOOL wounded) {
    LPEDICT best = NULL;
    FLOAT range = S_SpellRange(code, S_SpellLevel(caster, code));
    FLOAT best_distance = FLT_MAX;
    if (range <= 0.0f) range = UNDEAD_AUTOCAST_RADIUS;
    FILTER_EDICTS(target, target != caster && S_SpellIsAliveTarget(target) && S_SpellIsFriend(caster, target)) {
        FLOAT distance;
        if (wounded && target->health.value >= target->health.max_value) continue;
        if (!S_SpellAllowsTarget(code, caster, target)) continue;
        distance = Vector2_distance(&target->s.origin2, &caster->s.origin2);
        if (distance <= range && distance < best_distance) { best = target; best_distance = distance; }
    }
    return best && S_CastUnitTargetSpell(caster, code, best);
}

/* Shared area autocast acquire: cast self-spell if a worthy friendly exists in area. */
static BOOL undead_area_autocast_acquire(LPEDICT caster, DWORD code, BOOL needs_hp, BOOL needs_mana) {
    DWORD level = S_SpellLevel(caster, code);
    FLOAT area = S_SpellNumber(code, ABILITY_NUMBER_AREA, level);
    if (area <= 0.0f) area = UNDEAD_AUTOCAST_RADIUS;
    FILTER_EDICTS(target, target != caster && S_SpellIsAliveTarget(target) && S_SpellIsFriend(caster, target) &&
                  Vector2_distance(&target->s.origin2, &caster->s.origin2) <= area) {
        if (needs_hp && target->health.value < target->health.max_value) return S_CastNoTargetSpell(caster, code);
        if (needs_mana && target->mana.max_value > 0 && target->mana.value < target->mana.max_value)
            return S_CastNoTargetSpell(caster, code);
    }
    return false;
}

/* ---- Replenish (Arpb) --------------------------------------------------------
 * Name=Replenish
 * Ubertip="Replenish the life and mana of a target friendly unit."
 * Untip="Right-click to activate auto-casting."
 * DataA = HP restored, DataB = mana restored. BuffID = Brpb.
 */
static BOOL replenish_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)spell;
    return st.entity && S_SpellIsAliveTarget(st.entity) && S_SpellIsFriend(caster, st.entity) &&
           !G_UnitIsHero(st.entity);
}

static void replenish_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    LPEDICT target = st.entity;
    LPCSTR buff = undead_buff(spell, level);
    if (!target) return;
    S_SpellHeal(target, S_SpellData(spell->code, level, 1));
    target->mana.value = MIN(target->mana.max_value, target->mana.value + S_SpellData(spell->code, level, 2));
    if (buff) G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, target, NULL, true);
}

BZ_ABILITY_PROC(CAbilityReplenish) {
    spellTarget_t target = (msg == A_VALIDATE || msg == A_EXECUTE) && call && call->target ?
        *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
    DWORD code = call && call->item ? call->item->code : 0;
    switch (msg) {
    case A_VALIDATE: return replenish_validate(ent, target, call ? call->item : NULL);
    case A_EXECUTE: replenish_execute(ent, target, call ? call->item : NULL); return true;
    case A_AUTOCAST_ON: return ent && ent->autocast_code == code;
    case A_AUTOCAST_SET: return true;
    case A_AUTOCAST_ACQUIRE: return undead_unit_autocast_acquire(ent, code, true);
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}

/* ---- Essence of Blight (Arpl) ------------------------------------------------
 * Name=Essence of Blight
 * Ubertip="Restores DataA1 hit points to nearby friendly units."
 * Untip="Right-click to activate auto-casting."
 * DataA = HP restored per unit in area.
 */
static void replenish_life_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    FLOAT amount = S_SpellData(spell->code, level, 1);
    (void)st;
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsFriend(caster, target) &&
                  Vector2_distance(&target->s.origin2, &caster->s.origin2) <= area)
        S_SpellHeal(target, amount);
}

BZ_ABILITY_PROC(CAbilityReplenishLife) {
    DWORD code = call && call->item ? call->item->code : 0;
    switch (msg) {
    case A_EXECUTE: replenish_life_execute(ent, MAKE(spellTarget_t, .type = SPELL_TARGET_NONE), call ? call->item : NULL); return true;
    case A_AUTOCAST_ON: return ent && ent->autocast_code == code;
    case A_AUTOCAST_SET: return true;
    case A_AUTOCAST_ACQUIRE: return undead_area_autocast_acquire(ent, code, true, false);
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}

/* ---- Spirit Touch (Arpm) -----------------------------------------------------
 * Name=Spirit Touch
 * Ubertip="Restores DataB1 mana to nearby friendly units."
 * Untip="Right-click to activate auto-casting."
 * DataB = mana restored per unit in area.
 */
static void replenish_mana_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT area = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    FLOAT amount = S_SpellData(spell->code, level, 2);
    (void)st;
    FILTER_EDICTS(target, S_SpellIsAliveTarget(target) && S_SpellIsFriend(caster, target) &&
                  Vector2_distance(&target->s.origin2, &caster->s.origin2) <= area)
        target->mana.value = MIN(target->mana.max_value, target->mana.value + amount);
}

BZ_ABILITY_PROC(CAbilityReplenishMana) {
    DWORD code = call && call->item ? call->item->code : 0;
    switch (msg) {
    case A_EXECUTE: replenish_mana_execute(ent, MAKE(spellTarget_t, .type = SPELL_TARGET_NONE), call ? call->item : NULL); return true;
    case A_AUTOCAST_ON: return ent && ent->autocast_code == code;
    case A_AUTOCAST_SET: return true;
    case A_AUTOCAST_ACQUIRE: return undead_area_autocast_acquire(ent, code, false, true);
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}

/* ---- Graveyard Create Corpse (Agyd) -----------------------------------------
 * DataA/Gyd1 = maximum corpses, DataB/Gyd2 = gravestone/spawn radius,
 * DataC/Gyd3 = corpse-count radius, UnitID/Gydu = corpse type, Cool = interval.
 * Blizzard documents stock Graveyards as one Ghoul corpse every 15 seconds,
 * capped at five nearby corpses.
 */
#define ID_GRAVEYARD_CORPSE MAKEFOURCC('A','g','y','d')

static LPEDICT graveyard_find_thinker(LPEDICT graveyard) {
    FILTER_EDICTS(ent, ent->inuse && ent->owner == graveyard && ent->think == graveyard_think &&
                  ent->class_id == ID_GRAVEYARD_CORPSE) return ent;
    return NULL;
}

static DWORD graveyard_corpse_count(LPEDICT graveyard, DWORD unit_id, FLOAT radius) {
    DWORD count = 0;
    if (!graveyard || !unit_id || radius < 0.0f) return 0;
    FILTER_EDICTS(ent, ent->inuse && ent->class_id == unit_id && M_IsDead(ent) &&
                  (ent->svflags & SVF_DEADMONSTER)) {
        VECTOR2 position;
        if (S_CorpseCargoPosition(ent, &position) &&
            Vector2_distance(&position, &graveyard->s.origin2) <= radius) count++;
    }
    return count;
}

static void graveyard_spawn_corpse(LPEDICT graveyard, DWORD unit_id, FLOAT radius, DWORD ordinal) {
    VECTOR2 point;
    FLOAT angle;
    LPEDICT corpse;

    if (!graveyard || !unit_id) return;
    angle = fmodf((FLOAT)ordinal * 2.3999632297f, 2.0f * (FLOAT)M_PI);
    point = graveyard->s.origin2;
    point.x += cosf(angle) * MAX(0.0f, radius);
    point.y += sinf(angle) * MAX(0.0f, radius);
    corpse = SP_SpawnAtLocationNoBirth(unit_id, graveyard->s.player, &point);
    if (!corpse) return;
    corpse->health.value = 0.0f;
    corpse->svflags |= SVF_DEADMONSTER;
    corpse->s.flags |= EF_NOT_SELECTABLE;
    corpse->aiflags |= AI_HOLD_FRAME;
    unit_begin_decay(corpse);
}

void graveyard_think(LPEDICT thinker) {
    LPEDICT graveyard = thinker ? thinker->owner : NULL;
    DWORD level, unit_id, cap, count;
    FLOAT interval, spawn_radius, corpse_radius;

    if (!thinker || !graveyard || !graveyard->inuse || M_IsDead(graveyard) ||
        !(level = G_UnitAbilityLevel(graveyard, ID_GRAVEYARD_CORPSE))) {
        if (thinker) G_FreeEdict(thinker);
        return;
    }
    if (G_Time() < thinker->freetime) return;
    interval = S_SpellNumber(ID_GRAVEYARD_CORPSE, ABILITY_NUMBER_COOLDOWN, level);
    if (interval <= 0.0f) { G_FreeEdict(thinker); return; }
    unit_id = S_SpellUnitId(ID_GRAVEYARD_CORPSE, level);
    cap = (DWORD)MAX(0.0f, S_SpellData(ID_GRAVEYARD_CORPSE, level, 1));
    spawn_radius = MAX(0.0f, S_SpellData(ID_GRAVEYARD_CORPSE, level, 2));
    corpse_radius = MAX(0.0f, S_SpellData(ID_GRAVEYARD_CORPSE, level, 3));
    count = graveyard_corpse_count(graveyard, unit_id, corpse_radius);
    if (unit_id && count < cap) graveyard_spawn_corpse(graveyard, unit_id, spawn_radius, count);
    thinker->freetime = G_Time() + (DWORD)(interval * 1000.0f);
}

static void graveyard_ensure(LPEDICT graveyard) {
    DWORD level;
    FLOAT interval;
    LPEDICT thinker;

    if (!graveyard || M_IsDead(graveyard) || graveyard_find_thinker(graveyard) ||
        !(level = G_UnitAbilityLevel(graveyard, ID_GRAVEYARD_CORPSE))) return;
    interval = S_SpellNumber(ID_GRAVEYARD_CORPSE, ABILITY_NUMBER_COOLDOWN, level);
    if (interval <= 0.0f) return;
    thinker = G_Spawn();
    if (!thinker) return;
    thinker->owner = graveyard;
    thinker->class_id = ID_GRAVEYARD_CORPSE;
    thinker->think = graveyard_think;
    thinker->freetime = G_Time() + (DWORD)(interval * 1000.0f);
}

BZ_ABILITY_PROC(CAbilityGraveyard) {
    if (msg == A_UPDATE) { graveyard_ensure(ent); return true; }
    return false;
}

/* ---- Cannibalize (Acan) -----------------------------------------------------
 * Name=Cannibalize
 * Ubertip="Consumes a nearby corpse to restore hit points over time."
 * DataA = HP restored per second, DataB = corpse acquisition radius, Dur = channel duration.
 */
static void corpse_remove_status(LPEDICT corpse, DWORD code) {
    if (!corpse || !code) return;
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        if (corpse->abilstatus[i].level && corpse->abilstatus[i].code == code) {
            memset(corpse->abilstatus + i, 0, sizeof(corpse->abilstatus[i]));
            G_InvalidateUnitInfoPanel(corpse);
            return;
        }
    }
}

static void show_no_usable_corpse(LPEDICT caster) {
    if (!caster) return;
    G_ShowCommandErrorKey(G_GetPlayerEntityByNumber(caster->s.player),
                          "Cantfindcorpse", "There are no usable corpses nearby.");
}

static LPEDICT cannibalize_corpse(LPEDICT caster, abilityitem_t const *spell, LPEDICT requested) {
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT range = S_SpellData(spell->code, level, 2), best = FLT_MAX;
    LPEDICT corpse = NULL;

    if (requested) {
        if (!G_UnitIsHero(requested) && S_SpellAllowsCorpseTarget(spell->code, caster, requested) &&
            !G_UnitStatusLevel(requested, spell->code) &&
            Vector2_distance(&requested->s.origin2, &caster->s.origin2) <= range)
            return requested;
        return NULL;
    }

    FILTER_EDICTS(unit, !G_UnitIsHero(unit) && S_SpellAllowsCorpseTarget(spell->code, caster, unit) &&
                  !G_UnitStatusLevel(unit, spell->code)) {
        FLOAT const distance = Vector2_distance(&unit->s.origin2, &caster->s.origin2);
        if (distance <= range && distance < best) { corpse = unit; best = distance; }
    }
    FILTER_EDICTS(transport, S_CargoIsCorpseHolder(transport) &&
                  transport->s.player == caster->s.player) {
        FOR_LOOP(i, transport->cargo.count) {
            LPEDICT unit = S_CargoUnitAt(transport, i);
            VECTOR2 position;
            FLOAT distance;
            if (!unit || !S_CorpseCargoIsStored(unit) || G_UnitIsHero(unit) ||
                !S_SpellAllowsStoredCorpseTarget(spell->code, caster, unit) ||
                G_UnitStatusLevel(unit, spell->code) || !S_CorpseCargoPosition(unit, &position)) continue;
            distance = Vector2_distance(&position, &caster->s.origin2);
            if (distance <= range && distance < best) { corpse = unit; best = distance; }
        }
    }
    return corpse;
}

static BOOL cannibalize_reserved_corpse_valid(LPCEDICT thinker, LPCEDICT corpse) {
    return thinker && corpse && corpse->inuse &&
        corpse->spawn_time == thinker->channel.target_spawn_time &&
        (corpse->svflags & SVF_DEADMONSTER) && M_IsDead(corpse);
}

static void cannibalize_finish(LPEDICT thinker) {
    LPEDICT corpse = thinker ? thinker->goalentity : NULL;
    DWORD code = thinker ? thinker->class_id : 0;

    if (cannibalize_reserved_corpse_valid(thinker, corpse)) {
        corpse->aiflags &= ~AI_CORPSE_RESERVED;
        corpse_remove_status(corpse, code);
        G_FreeEdict(corpse);
    }
    if (thinker) S_SpellEndChannel(thinker);
}

/* Warsmash models DataA as an HP-regeneration stat buff.  OpenRealm applies the
 * same continuous HP/sec amount on the simulation cadence while the channel is
 * active, which preserves fractional healing and ends immediately at full HP. */
void cannibalize_think(LPEDICT thinker) {
    LPEDICT caster = thinker ? thinker->owner : NULL;
    LPEDICT corpse = thinker ? thinker->goalentity : NULL;
    DWORD const now = G_Time();

    if (!thinker) return;
    if (!S_SpellChannelActive(thinker)) { cannibalize_finish(thinker); return; }
    if (!cannibalize_reserved_corpse_valid(thinker, corpse)) { S_SpellEndChannel(thinker); return; }
    if (!caster || caster->health.value >= caster->health.max_value || now >= thinker->freetime) {
        cannibalize_finish(thinker);
        return;
    }
    S_SpellHeal(caster, thinker->velocity * ((FLOAT)FRAMETIME / 1000.0f));
    if (caster->health.value >= caster->health.max_value) cannibalize_finish(thinker);
}

static BOOL cannibalize_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    if (caster && spell && cannibalize_corpse(caster, spell, st.entity)) return true;
    if (caster && spell && !st.entity) show_no_usable_corpse(caster);
    return false;
}

BZ_ABILITY_PROC(CAbilityCannibalize) {
    abilityitem_t const *spell = call ? call->item : NULL;
    spellTarget_t target = (msg == A_VALIDATE || msg == A_EXECUTE) && call && call->target ?
        *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
    switch (msg) {
    case A_VALIDATE:
        return spell && G_UnitAbilityResearchAvailable(ent, spell->code) && cannibalize_validate(ent, target, spell);
    case A_EXECUTE: {
        DWORD level;
        LPEDICT corpse, thinker;
        if (!ent || !spell) return false;
        level = S_SpellLevel(ent, spell->code);
        corpse = cannibalize_corpse(ent, spell, target.entity);
        if (!corpse) { S_SpellCancelChannel(ent); return false; }
        corpse->aiflags |= AI_CORPSE_RESERVED;
        unit_addstatus(corpse, GetClassName(spell->code), level);
        thinker = S_SpellChannelThinker(ent, spell->code);
        thinker->goalentity = corpse;
        thinker->channel.target_spawn_time = corpse->spawn_time;
        thinker->velocity = MAX(0.0f, S_SpellData(spell->code, level, 1));
        thinker->freetime = G_Time() + (DWORD)(MAX(0.0f, S_SpellDuration(spell->code, level, false)) * 1000.0f);
        thinker->think = cannibalize_think;
        return true;
    }
    default:
        return CAbilitySimpleSpell(ent, msg, call);
    }
}

/* ---- Raise Dead (Arai/ACrd/AIrd) --------------------------------------------
 * The Raise Dead object-data family authors two summon groups:
 * DataA x DataC and DataB x DataD.  BuffID is the summoned timed-life buff.
 */
static FLOAT raise_dead_search_range(LPCEDICT caster) {
    return caster ? MAX(0.0f, caster->runtime.acquisition_range) : 0.0f;
}

static LONG raise_dead_corpse_rank(LPCEDICT unit) {
    UnitBalance_t const *balance = unit ? unit->data.UnitBalance : NULL;
    if (!balance && unit) balance = G_UnitBalance(unit->class_id);
    return balance ? balance->level : 0;
}

/* Retail Raise Dead preserves more valuable corpses by preferring the lowest-
 * ranked eligible corpse; distance is only the tie-breaker.  UnitBalance.level
 * is already the engine's corpse-power rank for Resurrection/Animate Dead. */
static LPEDICT raise_dead_corpse(LPEDICT caster, DWORD code, FLOAT range) {
    FLOAT best_distance = FLT_MAX;
    LONG best_rank = 0;
    LPEDICT corpse = NULL;

    FILTER_EDICTS(unit, !G_UnitIsHero(unit) && S_SpellAllowsCorpseTarget(code, caster, unit)) {
        FLOAT const distance = Vector2_distance(&unit->s.origin2, &caster->s.origin2);
        LONG const rank = raise_dead_corpse_rank(unit);
        if (distance > range) continue;
        if (!corpse || rank < best_rank || (rank == best_rank && distance < best_distance)) {
            corpse = unit; best_rank = rank; best_distance = distance;
        }
    }
    FILTER_EDICTS(transport, S_CargoIsCorpseHolder(transport) &&
                  transport->s.player == caster->s.player) {
        FOR_LOOP(i, transport->cargo.count) {
            LPEDICT unit = S_CargoUnitAt(transport, i);
            VECTOR2 position;
            FLOAT distance;
            LONG rank;
            if (!unit || !S_CorpseCargoIsStored(unit) || G_UnitIsHero(unit) ||
                !S_SpellAllowsStoredCorpseTarget(code, caster, unit) ||
                !S_CorpseCargoPosition(unit, &position)) continue;
            distance = Vector2_distance(&position, &caster->s.origin2);
            rank = raise_dead_corpse_rank(unit);
            if (distance > range) continue;
            if (!corpse || rank < best_rank || (rank == best_rank && distance < best_distance)) {
                corpse = unit; best_rank = rank; best_distance = distance;
            }
        }
    }
    return corpse;
}

static BOOL raise_dead_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    FLOAT range;
    (void)st;
    if (!caster || !spell) return false;
    range = raise_dead_search_range(caster);
    if (raise_dead_corpse(caster, spell->code, range)) return true;
    show_no_usable_corpse(caster);
    return false;
}

static void raise_dead_add_authored_buff(LPEDICT summon, LPCSTR buff_list, DWORD level) {
    char buff[5] = {0};
    if (!summon || !buff_list || strlen(buff_list) < 4) return;
    memcpy(buff, buff_list, 4);
    unit_addstatus(summon, buff, level);
}

static void raise_dead_spawn_group(LPEDICT caster, abilityitem_t const *spell, LPEDICT corpse,
                                   DWORD level, DWORD unit_id, DWORD count, FLOAT duration,
                                   LPCSTR buff) {
    VECTOR2 position;

    if (!unit_id || !count || !corpse || !S_CorpseCargoPosition(corpse, &position)) return;
    FOR_LOOP(i, count) {
        LPEDICT summon = S_SummonAt(caster, unit_id, &position, duration);
        if (!summon) continue;
        summon->s.angle = corpse->s.angle;
        summon->summon_ability = spell->code;
        raise_dead_add_authored_buff(summon, buff, level);
        G_SpawnAbilityEffectAtPoint(spell->code, WC3_EFFECT_EFFECT, 0, &summon->s.origin2, true);
        gi.LinkEntity(summon);
    }
}

static void raise_dead_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level, count_a, count_b, unit_a, unit_b;
    FLOAT duration, range;
    LPCSTR buff;
    LPEDICT corpse;
    (void)st;

    if (!caster || !spell) return;
    level = S_SpellLevel(caster, spell->code);
    range = raise_dead_search_range(caster);
    corpse = raise_dead_corpse(caster, spell->code, range);
    if (!corpse) return;

    count_a = (DWORD)MAX(0.0f, S_SpellData(spell->code, level, 1));
    count_b = (DWORD)MAX(0.0f, S_SpellData(spell->code, level, 2));
    unit_a = S_SpellDataId(spell->code, level, 3);
    unit_b = S_SpellDataId(spell->code, level, 4);
    duration = S_SpellDuration(spell->code, level, false) +
        G_UnitUpgradeEffectBonus(caster, BZ_UPGRADE_RAISE_DEAD_LIFE);
    buff = G_AbilityLevel(spell->code, level)->buffID;

    raise_dead_spawn_group(caster, spell, corpse, level, unit_a, count_a, duration, buff);
    raise_dead_spawn_group(caster, spell, corpse, level, unit_b, count_b, duration, buff);
    /* Raiu/UnitID is specifically the unit type used for Raise Dead's fixed
     * retail summon-limit check.  A blank custom-map field disables the cap. */
    S_EnforceSummonedUnitTypeLimit(caster, S_SpellUnitId(spell->code, level), RAISE_DEAD_SUMMON_LIMIT);
    G_FreeEdict(corpse);
}

static BOOL raise_dead_autocast_acquire(LPEDICT caster, DWORD code) {
    FLOAT const range = raise_dead_search_range(caster);
    return raise_dead_corpse(caster, code, range) && S_CastNoTargetSpell(caster, code);
}

BZ_ABILITY_PROC(CAbilityRaiseDead) {
    spellTarget_t target = (msg == A_VALIDATE || msg == A_EXECUTE) && call && call->target ?
        *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
    DWORD code = call && call->item ? call->item->code : 0;
    switch (msg) {
    case A_VALIDATE: return raise_dead_validate(ent, target, call ? call->item : NULL);
    case A_EXECUTE: raise_dead_execute(ent, target, call ? call->item : NULL); return true;
    case A_AUTOCAST_ON: return ent && ent->autocast_code == code;
    case A_AUTOCAST_SET: return true;
    case A_AUTOCAST_ACQUIRE: return raise_dead_autocast_acquire(ent, code);
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}

/* ---- Possession (Apos / ACps instant; Aps2 channeled) -------------------------
 * Not Charm. Instant rows destroy the caster immediately; Aps2 locks for Dur then
 * transfers. Bpoc must not set stunned or spell_run_frame cancels the channel.
 */
#define BZ_BPOS MAKEFOURCC('B', 'p', 'o', 's') // rawcode; target Possession stun
#define BZ_BPOC MAKEFOURCC('B', 'p', 'o', 'c') // rawcode; caster Possession damage amp
#define BZ_POS_MAGIC_IMMUNE 1u // Bpos.data bit; authored DataD > 0 during channel

static BOOL possession_is_neutral(LPCEDICT target) {
    return target && target->s.player < MAX_PLAYERS && level.mapinfo &&
        level.mapinfo->players[target->s.player].playerType == kPlayerTypeNeutral;
}

static BOOL possession_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    LPEDICT target = st.entity;
    DWORD level = S_SpellLevel(caster, spell->code);
    DWORD max_level = (DWORD)S_SpellData(spell->code, level, 1);
    if (!target || !target->data.UnitBalance) return false;
    if (G_UnitIsHero(target)) return false;
    if (!S_SpellIsEnemy(caster, target) && !possession_is_neutral(target)) return false;
    if (max_level && (DWORD)target->data.UnitBalance->level > max_level) return false;
    return true;
}

static void possession_clear_status(LPEDICT ent, DWORD code) {
    if (!ent) return;
    FOR_LOOP(i, MAX_UNIT_STATUSES)
        if (ent->abilstatus[i].level && ent->abilstatus[i].code == code)
            memset(ent->abilstatus + i, 0, sizeof(ent->abilstatus[i]));
}

/* Keep stunned in sync after stripping Bpos without waiting for a later status tick. */
static void possession_refresh_stun(LPEDICT ent) {
    if (!ent) return;
    ent->stunned = false;
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        DWORD c = ent->abilstatus[i].code;
        if (!ent->abilstatus[i].level) continue;
        if (c == MAKEFOURCC('B', 's', 't', 'u') || c == MAKEFOURCC('B', 'U', 's', 'l') || c == BZ_BPOS)
            ent->stunned = true;
    }
}

static void possession_takeover(LPEDICT caster, LPEDICT target) {
    G_SetUnitPlayer(target, caster->s.player);
    target->owner = NULL;
    target->combatentity = NULL;
    if (target->stand) target->stand(target);
    G_SetHealth(caster, 0);
    if (caster->die) caster->die(caster, caster);
    else unit_die(caster, caster);
}

static void possession_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)spell;
    if (!st.entity) return;
    possession_takeover(caster, st.entity);
}

static void possession_strip_channel(LPEDICT thinker) {
    LPEDICT caster = thinker->owner, target = thinker->goalentity;
    if (target && target->inuse && target->spawn_time == thinker->channel.target_spawn_time) {
        possession_clear_status(target, BZ_BPOS);
        possession_refresh_stun(target);
        if (thinker->damage) target->invulnerable = thinker->invulnerable;
    }
    if (caster && caster->inuse && caster->spawn_time == thinker->channel.owner_spawn_time)
        possession_clear_status(caster, BZ_BPOC);
}

void possession_two_think(LPEDICT thinker) {
    LPEDICT caster = thinker->owner, target = thinker->goalentity;
    if (!S_SpellChannelActive(thinker) || !S_SpellIsAliveTarget(target) ||
        target->spawn_time != thinker->channel.target_spawn_time) {
        possession_strip_channel(thinker);
        S_SpellEndChannel(thinker);
        return;
    }
    if (G_Time() < thinker->spawn_time) return;
    possession_strip_channel(thinker);
    S_SpellEndChannel(thinker);
    if (caster && caster->inuse && !M_IsDead(caster) && S_SpellIsAliveTarget(target))
        possession_takeover(caster, target);
}

static void possession_two_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT duration = S_SpellDuration(spell->code, level, G_UnitIsHero(st.entity));
    FLOAT damage_mult = S_SpellData(spell->code, level, 2);
    FLOAT invuln = S_SpellData(spell->code, level, 3);
    FLOAT magic_imm = S_SpellData(spell->code, level, 4);
    LPCSTR buffs = G_AbilityLevel(spell->code, level)->buffID;
    char target_buff[5] = "Bpos", caster_buff[5] = "Bpoc";
    LPEDICT thinker;
    heroabilitystatus_t *slot;

    if (!st.entity) return;
    if (buffs && sscanf(buffs, "%4[^,],%4s", target_buff, caster_buff) != 2)
        fprintf(stderr, "WC3 Possession: BuffID expected Bpos,Bpoc for %08x\n", spell->code);

    thinker = S_SpellChannelThinker(caster, spell->code);
    thinker->goalentity = st.entity;
    thinker->channel.target_spawn_time = st.entity->spawn_time;
    thinker->spawn_time = G_Time() + (DWORD)(duration * 1000.0f);
    thinker->damage = invuln > 0.0f ? 1 : 0;
    thinker->invulnerable = st.entity->invulnerable;
    thinker->think = possession_two_think;

    unit_addtimedstatus(st.entity, target_buff, level, duration);
    unit_addtimedstatus(caster, caster_buff, level, duration);
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        slot = st.entity->abilstatus + i;
        if (slot->level && slot->code == *((DWORD const *)target_buff)) {
            slot->data = magic_imm > 0.0f ? BZ_POS_MAGIC_IMMUNE : 0;
            break;
        }
    }
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        slot = caster->abilstatus + i;
        if (slot->level && slot->code == *((DWORD const *)caster_buff)) {
            slot->data = (DWORD)(damage_mult * 1000.0f + 0.5f);
            break;
        }
    }
    if (invuln > 0.0f) st.entity->invulnerable = true;
}

BOOL S_PossessionSpellImmune(LPCEDICT unit) {
    if (!unit) return false;
    FOR_LOOP(i, MAX_UNIT_STATUSES)
        if (unit->abilstatus[i].level && unit->abilstatus[i].code == BZ_BPOS &&
            (unit->abilstatus[i].data & BZ_POS_MAGIC_IMMUNE) &&
            (!unit->abilstatus[i].timestamp || unit->abilstatus[i].timestamp > G_Time()))
            return true;
    return false;
}

/* DataB lives on Bpoc.data as milli-units (1.66 → 1660). Attack hits only. */
int S_PossessionDamageTaken(LPEDICT target, int damage) {
    DWORD milli = 0;
    if (!target || damage <= 0) return damage;
    FOR_LOOP(i, MAX_UNIT_STATUSES)
        if (target->abilstatus[i].level && target->abilstatus[i].code == BZ_BPOC &&
            (!target->abilstatus[i].timestamp || target->abilstatus[i].timestamp > G_Time())) {
            milli = target->abilstatus[i].data; break;
        }
    if (!milli) return damage;
    return (int)((FLOAT)damage * (FLOAT)milli / 1000.0f);
}

BZ_VALIDATED_SPELL_PROC(AbilityPossession, possession_validate, possession_execute)

BZ_ABILITY_PROC(CAbilityPossessionTwo) {
    spellTarget_t target = (msg == A_VALIDATE || msg == A_EXECUTE) && call && call->target ?
        *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
    switch (msg) {
    case A_VALIDATE: return possession_validate(ent, target, call ? call->item : NULL);
    case A_EXECUTE: possession_two_execute(ent, target, call ? call->item : NULL); return true;
    default: return CAbilityPossession(ent, msg, call);
    }
}
