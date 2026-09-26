#include "s_skills.h"

/* Name=Immolation
 * Ubertip="Immolates nearby enemy units, dealing damage over time."
 * Untip="Deactivate Immolation"
 * Unubertip=""
 */

BZ_SIMPLE_SPELL_PROC(AbilityImmolation) {
    uint32_t code = spell->code;

    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        if (caster->abilstatus[i].level && caster->abilstatus[i].code == code) {
            memset(&caster->abilstatus[i], 0, sizeof(caster->abilstatus[i]));
            G_InvalidateUnitInfoPanel(caster);
            return;
        }
    }
    unit_addstatus(caster, "Biml", 1);
}

/* Name=Cold Arrows
 * Ubertip="Adds cold damage to attacks and slows the movement speed of the attacked unit."
 * Untip="Right-click to activate auto-casting."
 * Unubertip="Right-click to deactivate auto-casting."
 */

BZ_SIMPLE_SPELL_PROC(AbilityColdArrows) {
    uint32_t code = MAKEFOURCC('c', 'o', 'l', 'd');

    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        if (caster->abilstatus[i].level && caster->abilstatus[i].code == code) {
            memset(&caster->abilstatus[i], 0, sizeof(caster->abilstatus[i]));
            G_InvalidateUnitInfoPanel(caster);
            return;
        }
    }
    unit_addstatus(caster, "cold", 1);
}

/* Name=War Stomp
 * Ubertip="Slams the ground, dealing <AOws,DataA1> damage to nearby enemy land units and stunning them for <AOws,Dur1> seconds."
 */
BZ_SIMPLE_SPELL_PROC(AbilityStomp) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    float radius = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    uint32_t damage = (uint32_t)S_SpellData(spell->code, level, 1);
    float duration = S_SpellDuration(spell->code, level, false);

#define WAR_STOMP_HITS(t) ((t)->inuse && (t) != caster && S_SpellIsAliveTarget(t) && \
                           S_SpellIsEnemy(caster, t) && (t)->targtype == TARG_GROUND && \
                           Vector2_distance(&(t)->s.origin2, &caster->s.origin2) <= radius)
    FILTER_EDICTS(target, WAR_STOMP_HITS(target)) {
        if (S_SpellDamage(target, caster, damage) && !M_IsDead(target) && duration > 0.0f)
            unit_addtimedstatus(target, "Bstu", 1, duration);
    }
#undef WAR_STOMP_HITS
}

/* Name=Endurance Aura
 * Ubertip="Increases nearby friendly units' movement speed and attack rate."
 */
/* Name=Wind Walk
 * Ubertip="Allows the Blademaster to become invisible and move faster until it attacks or uses an ability."
 */
BZ_SIMPLE_SPELL_PROC(AbilityWindWalk) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    caster->s.renderfx |= RF_HIDDEN;
    unit_addtimedstatus(caster, "BOwk", level, S_SpellDuration(spell->code, level, true));
}

/* Name=Mana Burn
 * Ubertip="Sends a bolt of negative energy that burns a target enemy unit's mana and deals damage proportional to the amount of mana burned."
 */
BZ_SIMPLE_SPELL_PROC(AbilityManaBurn) {
    edict_t * target = st.entity;
    uint32_t level = S_SpellLevel(caster, spell->code);
    float amount = MIN(target->mana.value, S_SpellData(spell->code, level, 1));

    target->mana.value -= amount;
}

/* Name=Dark Ritual
 * Ubertip="Sacrifices a friendly non-Hero unit, converting a percentage of its hit points into mana for the caster."
 * Dark Ritual converts the authored fraction of an allied non-hero's maximum
 * life into caster mana, then uses the normal damage/death path to sacrifice it. */
BZ_SIMPLE_SPELL_PROC(AbilityDarkRitual) {
    edict_t * target = st.entity;
    uint32_t level = S_SpellLevel(caster, spell->code);
    float mana = target->health.max_value * S_SpellData(spell->code, level, 1);

    caster->mana.value = MIN(caster->mana.max_value, caster->mana.value + mana);
    T_Damage(target, caster, (uint32_t)MAX(1.0f, target->health.value));
}

/* Name=Frost Armor
 * Ubertip="Creates a shield of frost around a target friendly unit. The shield adds <ACfu,DataB1> armor and slows melee units that attack it for <ACfu,Dur1> seconds. Lasts <ACfu,DataA1> seconds."
 * Untip="Right-click to activate auto-casting."
 * Unubertip="Right-click to deactivate auto-casting."
 */
BZ_SIMPLE_SPELL_PROC(AbilityFrostArmor) {
    edict_t * target = st.entity;
    uint32_t level = S_SpellLevel(caster, spell->code);
    cstring_t buff = G_AbilityLevel(spell->code, level)->buffID;

    if (!target || !buff || strlen(buff) < 4) {
        fprintf(stderr, "WC3: %.4s has no authored BuffID\n", (cstring_t)&spell->code);
        return;
    }
    unit_addtimedstatus(target, buff, level, S_SpellData(spell->code, level, 1));
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, target, NULL, true);
}

void divine_shield_think(edict_t * ent) {
    edict_t * caster = ent->owner;

    if (!caster || !caster->inuse) {
        G_FreeEdict(ent);
        return;
    }
    if (G_Time() < ent->spawn_time) return;
    caster->invulnerable = ent->resources;
    G_FreeEdict(ent);
}

/* Name=Divine Shield
 * Ubertip="Makes the Paladin invulnerable to damage for <AHds,Dur1> seconds."
 */
BZ_SIMPLE_SPELL_PROC(AbilityDivineShield) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    float duration = MAX(0.1f, S_SpellDuration(spell->code, level, true));
    cstring_t buff = G_AbilityLevel(spell->code, level)->buffID;
    edict_t * thinker = G_Spawn();

    if (!thinker) return;
    thinker->owner = caster;
    thinker->resources = caster->invulnerable;
    thinker->spawn_time = G_Time() + (uint32_t)(duration * 1000.0f);
    caster->invulnerable = true;
    thinker->think = divine_shield_think;
    if (buff && strlen(buff) >= 4) unit_addtimedstatus(caster, buff, level, duration);
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_CASTER, 0, caster, NULL, true);
}

/* Name=Bash
 * Ubertip="Gives a chance that an attack will deal bonus damage and stun the target."
 * TODO: no command handler; the attack-resolution path must consume this passive.
 */
/* Entangling Roots applies the authored timed buff; movement owns the root
 * consumer so expiry naturally restores the unit without a second cleanup path. */
/* Name=Entangling Roots
 * Ubertip="Roots a target enemy unit in place, preventing movement for <AEer,Dur1> seconds."
 */
BZ_SIMPLE_SPELL_PROC(AbilityEntanglingRoots) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    cstring_t buff = G_AbilityLevel(spell->code, level)->buffID;
    edict_t * target = st.entity;
    float duration;

    if (!target || !buff || strlen(buff) < 4) {
        fprintf(stderr, "WC3: Entangling Roots has no authored BuffID\n");
        return;
    }
    duration = S_SpellDuration(spell->code, level, G_UnitIsHero(target));
    unit_addtimedstatus(target, buff, level, duration);
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, target, NULL, true);
}

/* Name=Phoenix Fire
 * Ubertip="Automatically attacks nearby enemy units with flaming projectiles."
 * TODO: passive attack-resolution behavior is not implemented here.
 */
/* Name=Invulnerable
 * Ubertip="This unit cannot be damaged."
 */

/* Couple Instant remains a non-spell ability with a stub command handler. */
BZ_COMMAND_PROC(AbilityCoupleInstant) { UI_AddCancelButton(clent); }
