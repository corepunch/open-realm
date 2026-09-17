#include "s_skills.h"

/* ---- Purge (Aprg) ----------------------------------------------------------
 * Name=Purge
 * Ubertip="Removes all buffs from a target unit, and slows its movement speed
 *          by a factor of <Aprg,DataA1>. Purged units will slowly regain their
 *          movement speed over <Aprg,Dur1> seconds. |nDeals <Aprg,DataC1>
 *          damage to summoned units."
 *
 * Purge removes every timed status from the target, applies its own slow buff,
 * and deals DataC damage to summoned units. DataA is the slow factor (movement
 * speed fraction; 0.5 = 50% speed). TODO: the retail slow gradually recovers
 * over Dur1 seconds; this implementation applies the full reduction uniformly
 * for the buff duration.
 */
static void purge_execute(LPEDICT caster, spellTarget_t st, abilityitem_t const *spell) {
    DWORD level = S_SpellLevel(caster, spell->code);
    LPCSTR buff;
    if (!st.entity) return;
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t *slot = st.entity->abilstatus + i;
        if (!slot->level || !slot->timestamp) continue;
        S_HumanStatusExpired(st.entity, slot->code, slot->level);
        memset(slot, 0, sizeof(*slot));
    }
    buff = G_AbilityLevel(spell->code, level)->buffID;
    if (buff && strlen(buff) >= 4)
        unit_addtimedstatus(st.entity, buff, level, S_SpellDuration(spell->code, level, false));
    if (st.entity->owner)
        S_SpellDamage(st.entity, caster, (int)MAX(1.0f, S_SpellData(spell->code, level, 3)));
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, st.entity, NULL, true);
}

BZ_SIMPLE_SPELL_PROC(AbilityPurge) { purge_execute(caster, st, spell); }

/* DataA1 is the movement speed fraction applied while Bprg is active (0 = stopped). */
FLOAT S_PurgeMoveReduction(LPCEDICT unit) {
    DWORD level = G_UnitStatusLevel(unit, MAKEFOURCC('B', 'p', 'r', 'g'));
    return level ? 1.0f - S_SpellData(MAKEFOURCC('A', 'p', 'r', 'g'), level, 1) : 0.0f;
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
