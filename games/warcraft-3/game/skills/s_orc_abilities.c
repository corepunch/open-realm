#include "s_skills.h"

/* Aast inherits the nearest-target contract: only the caster's dead ordinary Tauren inside authored range qualify. */
static LPEDICT ancestral_spirit_target(LPEDICT caster, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT range = S_SpellRange(spell->code, level), nearest = 0.0f;
    LPEDICT selected = NULL;
    FILTER_EDICTS(target, target->inuse && (target->svflags & SVF_MONSTER) &&
                  (target->svflags & SVF_DEADMONSTER) && M_IsDead(target) &&
                  target->class_id == MAKEFOURCC('o','t','a','u') && !G_UnitIsHero(target) &&
                  target->s.player == caster->s.player) {
        FLOAT distance = Vector2_distance(&target->s.origin2, &caster->s.origin2);
        if ((range <= 0.0f || distance <= range) && (!selected || distance < nearest)) {
            nearest = distance; selected = target;
        }
    }
    return selected;
}

/* Reject an empty cast before the common spell path spends the authored 250 mana. */
static BOOL ancestral_spirit_validate(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    (void)st;
    return ancestral_spirit_target(caster, spell) != NULL;
}

/* Revive the original edict so JASS handles, owner, unit type and runtime identity remain authoritative. */
static void ancestral_spirit_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    LPEDICT target = ancestral_spirit_target(caster, spell);
    DWORD level = S_SpellLevel(caster, spell->code);
    (void)st;
    if (!target) return;
    G_ReviveCorpse(target, S_SpellData(spell->code, level, 1));
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, target, NULL, true);
}

BZ_VALIDATED_SPELL_PROC(AbilityAncestralSpirit, ancestral_spirit_validate, ancestral_spirit_execute)

/* ---- Purge (Aprg / Apg2 / AIlp) ---------------------------------------------
 * Name=Purge
 * DataA=Movement Update Frequency (fixtures use it as slow complement: 1-DataA)
 * DataC=Summoned Unit Damage; DataD=Unit Pause Duration; DataE=Hero Pause Duration
 * BuffID=Bprg (ROC Aprg may omit; fall back to Bprg).
 *
 * TODO: retail slow gradually recovers over Dur using DataA update frequency;
 * this build applies a uniform 1-DataA reduction for the whole buff lifetime.
 */
static heroabilitystatus_t const *purge_status(LPCEDICT unit) {
    if (!unit) return NULL;
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t const *slot = unit->abilstatus + i;
        abilityitem_t item;
        if (!slot->level || !slot->data) continue;
        if (slot->timestamp && slot->timestamp <= G_Time()) continue;
        item = S_AbilityItem(slot->data);
        if (item.ability && item.ability->proc == CAbilityPurge) return slot;
    }
    return NULL;
}

static FLOAT purge_pause_seconds(LPCEDICT unit, heroabilitystatus_t const *slot) {
    return S_SpellData(slot->data, slot->level, G_UnitIsHero(unit) ? 5 : 4);
}

static void purge_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    LPCSTR buff;
    heroabilitystatus_t *slot;
    if (!st.entity) return;
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t *s = st.entity->abilstatus + i;
        if (!s->level || !s->timestamp) continue;
        S_HumanStatusExpired(st.entity, s->code, s->level);
        memset(s, 0, sizeof(*s));
    }
    buff = G_AbilityLevel(spell->code, level)->buffID;
    if (!buff || strlen(buff) < 4) buff = "Bprg";
    unit_addtimedstatus(st.entity, buff, level, S_SpellDuration(spell->code, level, false));
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        slot = st.entity->abilstatus + i;
        if (slot->level && slot->code == *((DWORD const *)buff)) { slot->data = spell->code; break; }
    }
    if (st.entity->owner)
        S_SpellDamage(st.entity, caster, (int)MAX(1.0f, S_SpellData(spell->code, level, 3)));
    if (S_PurgeIsImmobilized(st.entity) && st.entity->stand) st.entity->stand(st.entity);
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, st.entity, NULL, true);
}

BZ_SIMPLE_SPELL_PROC(AbilityPurge) { purge_execute(caster, st, spell); }

/* True while inside DataD (unit) / DataE (hero) pause window of an active Purge. */
BOOL S_PurgeIsImmobilized(LPCEDICT unit) {
    heroabilitystatus_t const *slot = purge_status(unit);
    FLOAT pause;
    DWORD start, pause_ms;
    if (!slot || !slot->duration_ms || slot->timestamp < slot->duration_ms) return false;
    pause = purge_pause_seconds(unit, slot);
    if (pause <= 0.0f) return false;
    start = slot->timestamp - slot->duration_ms;
    pause_ms = (DWORD)(pause * 1000.0f);
    return G_Time() < start + pause_ms;
}

/* During pause reduction is 1.0; afterward 1-DataA from the casting rawcode in status.data. */
FLOAT S_PurgeMoveReduction(LPCEDICT unit) {
    heroabilitystatus_t const *slot = purge_status(unit);
    if (!slot) return 0.0f;
    if (S_PurgeIsImmobilized(unit)) return 1.0f;
    return 1.0f - S_SpellData(slot->data, slot->level, 1);
}

/* ---- Lightning Shield (Alsh) -----------------------------------------------
 * Name=Lightning Shield
 * Ubertip="Forms a shield of electricity around a target unit, dealing
 *          <Alsh,DataA1> damage per second to units around it. |nLasts
 *          <Alsh,Dur1> seconds."
 *
 * Applies the Blsh buff to the target and spawns a periodic thinker that
 * damages all other living units within Area of the carrier each second.
 * Attribution uses the original caster for damage and resistance calculations.
 */
static void lsh_think(LPEDICT thinker) {
    DWORD level;
    FLOAT area, damage;
    if (!thinker->owner || !thinker->owner->inuse) { G_FreeEdict(thinker); return; }
    level = G_UnitStatusLevel(thinker->owner, MAKEFOURCC('B', 'l', 's', 'h'));
    if (!level || G_Time() >= thinker->spawn_time) { G_FreeEdict(thinker); return; }
    if (thinker->freetime && G_Time() < thinker->freetime) return;
    area = S_SpellNumber(MAKEFOURCC('A', 'l', 's', 'h'), ABILITY_NUMBER_AREA, level);
    damage = S_SpellData(MAKEFOURCC('A', 'l', 's', 'h'), level, 1);
    FILTER_EDICTS(target, target != thinker->owner && S_SpellIsAliveTarget(target) &&
                  Vector2_distance(&target->s.origin2, &thinker->owner->s.origin2) <= area)
        S_SpellDamage(target, thinker->goalentity, (int)MAX(1.0f, damage));
    thinker->freetime = G_Time() + 1000;
}

BZ_SIMPLE_SPELL_PROC(AbilityLightningShield) {
    DWORD level = S_SpellLevel(caster, spell->code);
    FLOAT dur = S_SpellDuration(spell->code, level, false);
    LPCSTR buff = G_AbilityLevel(spell->code, level)->buffID;
    LPEDICT thinker;
    if (!st.entity || !buff || strlen(buff) < 4) return;
    unit_addtimedstatus(st.entity, buff, level, dur);
    thinker = G_Spawn();
    thinker->owner = st.entity; thinker->goalentity = caster;
    thinker->spawn_time = G_Time() + (DWORD)(dur * 1000.0f);
    thinker->think = lsh_think; lsh_think(thinker);
}

/* ---- Healing Ward (Ahwd) ---------------------------------------------------
 * Name=Healing Ward
 * Ubertip="Summons an immovable ward that heals <Aoar,DataA1,%>% of a nearby
 *          friendly non-mechanical unit's hit points per second. |nLasts
 *          <Ahwd,Dur1> seconds."
 *
 * Spawns the ward unit at the target point; the ward's Aoar ability drives the
 * S_RegenerationHealthAura path in s_hero_passives.c.
 */
BZ_SIMPLE_SPELL_PROC(AbilityHealingWard) {
    DWORD level = S_SpellLevel(caster, spell->code);
    S_SummonAt(caster, S_SpellUnitId(spell->code, level), &st.point, S_SpellDuration(spell->code, level, false));
}

/* Passive marker for the regen-life aura families (Aoar/Aabr).
 * The per-second HP regeneration is computed by S_RegenerationHealthAura; this
 * procedure exists only to provide a named TFT-class entry in the registry. */
BZ_ABILITY_PROC(CAbilityAuraRegenLife) { return CAbilityPassive(ent, msg, call); }
