#include "s_skills.h"

/* Name=Immolation
 * Ubertip="Immolates nearby enemy units, dealing damage over time."
 * Untip="Deactivate Immolation"
 * Unubertip=""
 */

/* Immolation's DataA is damage per second, DataB is mana drain per second,
 * and Area is its self-centered radius. Mana drain is distributed over seven substeps per second
 * (one point each for stock DataB=7); damage remains a one-second pulse. */
#define IMMOLATION_TICK_MS 1000 // milliseconds; authored damage is per second
#define IMMOLATION_MANA_RATE 7 // substeps/second; preserves stock one-point mana drain cadence
#define ID_IMMOLATION_STATUS MAKEFOURCC('B','E','i','m')
BZ_ABILITY_PROC(CAbilityImmolation) {
    heroabilitystatus_t *status;
    uint32_t code;
    if (msg == A_TOGGLE_ON)
        return ent && unit_findstatus(ent, ID_IMMOLATION_STATUS) != NULL;
    if (msg == A_UPDATE) {
        if (!ent || !ent->unit_state) return true;
        status = unit_findstatus(ent, ID_IMMOLATION_STATUS);
        if (!status || !status->level || !status->data) return true;
        if (!status->next_tick) status->next_tick = G_Time() + (IMMOLATION_TICK_MS + IMMOLATION_MANA_RATE - 1) / IMMOLATION_MANA_RATE;
        while (status->level && status->next_tick && G_Time() >= status->next_tick) {
            abilityLevel_t const *row;
            float damage, mana_drain, area;
            uint32_t level;
            code = status->data;
            level = status->rank ? status->rank : MAX(1u, S_SpellLevel(ent, code));
            row = G_AbilityLevel(code, level);
            damage = row->data[0].number;
            mana_drain = row->data[1].number;
            area = row->area;
            if (damage > 0.0f && area > 0.0f && (status->duration_ms + 1) % IMMOLATION_MANA_RATE == 0) {
                FILTER_EDICTS(target, target != ent && S_SpellIsAliveTarget(target) &&
                              S_SpellIsEnemy(ent, target) && G_UnitTargetType(target) == TARG_GROUND &&
                              Vector2_distance(&target->s.origin2, &ent->s.origin2) <= area) {
                    S_SpellDamage(target, ent, (int)damage);
                }
            }
            /* Damage timing must not depend on DataB. The old variable step
             * count changed DPS for custom rates and never ticked at DataB=0. */
            uint32_t const step = status->duration_ms + 1;
            uint32_t const elapsed_ms = (uint32_t)(((uint64_t)step * IMMOLATION_TICK_MS + IMMOLATION_MANA_RATE - 1) / IMMOLATION_MANA_RATE);
            uint32_t const next_ms = (uint32_t)(((uint64_t)(step + 1) * IMMOLATION_TICK_MS + IMMOLATION_MANA_RATE - 1) / IMMOLATION_MANA_RATE);
            status->duration_ms = step;
            status->next_tick += next_ms - elapsed_ms;
            if (mana_drain > 0.0f) {
                ent->mana.value = MAX(0.0f, ent->mana.value - mana_drain / IMMOLATION_MANA_RATE);
                G_InvalidateUnitInfoPanel(ent);
                if (ent->mana.value <= 0.0f) {
                    unit_removestatus(ent, status, STATUS_REMOVE_SCRIPT);
                    G_InvalidateUnitInfoPanel(ent);
                    G_InvalidateUnitCommands(ent);
                    return true;
                }
            }
        }
        return true;
    }
    if (msg != A_EXECUTE) return CAbilitySimpleSpell(ent, msg, call);
    code = call && call->item ? call->item->code : 0;
    if (!code || !ent || !ent->unit_state) return true;
    status = unit_findstatus(ent, ID_IMMOLATION_STATUS);
    if (status) {
        unit_removestatus(ent, status, STATUS_REMOVE_SCRIPT);
        G_InvalidateUnitInfoPanel(ent);
        G_InvalidateUnitCommands(ent);
        return true;
    }
    {
        uint32_t const level = MAX(1u, S_SpellLevel(ent, code));
        float const cost = S_SpellNumber(code, ABILITY_NUMBER_COST, level);
        status_application_t application = {
            .buff = "BEim", .level = 1, .data = code,
            .source_ability = code, .source = ent, .rank = level,
            .buff_flags = WC3_STATUS_BUFF_TARGET_ART
        };
        if (ent->mana.value < cost) return true;
        status = unit_applystatus(ent, &application);
        if (!status) return true;
        /* Reserve the status slot before charging. A full status table must
         * not consume the activation cost without enabling Immolation. */
        if (!S_SpellSpendMana(ent, code, level)) {
            status = unit_findstatus(ent, ID_IMMOLATION_STATUS);
            if (status) unit_removestatus(ent, status, STATUS_REMOVE_SCRIPT);
            return true;
        }
        status = unit_findstatus(ent, ID_IMMOLATION_STATUS);
        if (status) {
            status->data = code;
            status->source_ability = code;
            status->source = ent;
            status->source_spawn_time = ent->spawn_time;
            status->rank = level;
            status->duration_ms = 0;
            status->next_tick = G_Time() + (IMMOLATION_TICK_MS + IMMOLATION_MANA_RATE - 1) / IMMOLATION_MANA_RATE;
        }
        G_InvalidateUnitInfoPanel(ent);
        G_InvalidateUnitCommands(ent);
    }
    return true;
}

/* Cold Arrows is an attack modifier. The selected autocast alias controls
 * attack damage and projectile presentation; clicking the command button
 * opens the same one-shot target order used by Searing Arrows. */
BZ_ABILITY_PROC(CAbilityColdArrows) {
    uint32_t const code = call && call->item ? call->item->code : 0;
    bool const active = ent && ent->unit_state && ent->unit_state->autocast == code;
    switch (msg) {
    case A_TOGGLE_ON: return active;
    case A_AUTOCAST_ON: return active;
    case A_AUTOCAST_SET: return true;
    default: return CAbilitySimpleSpell(ent, msg, call);
    }
}

/* Name=War Stomp
 * Ubertip="Slams the ground, dealing <AOws,DataA1> damage to nearby enemy land units and stunning them for <AOws,Dur1> seconds."
 */
BZ_SIMPLE_SPELL_PROC(AbilityStomp) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    float radius = S_SpellNumber(spell->code, ABILITY_NUMBER_AREA, level);
    uint32_t damage = (uint32_t)S_SpellData(spell->code, level, 1);
    float duration = S_SpellDuration(spell->code, level, false);

    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_CASTER, 0, caster, NULL, true);

#define WAR_STOMP_HITS(t) ((t)->inuse && (t) != caster && S_SpellIsAliveTarget(t) && \
                           S_SpellIsEnemy(caster, t) && G_UnitTargetType(t) == TARG_GROUND && \
                           Vector2_distance(&(t)->s.origin2, &caster->s.origin2) <= radius)
    FILTER_EDICTS(target, WAR_STOMP_HITS(target)) {
        if (S_SpellDamage(target, caster, damage) && !M_IsDead(target) && duration > 0.0f)
            S_SpellApplyStun(target, duration);
    }
#undef WAR_STOMP_HITS
}

/* Name=Endurance Aura
 * Ubertip="Increases nearby friendly units' movement speed and attack rate."
 */
/* Name=Mana Burn
 * Ubertip="Sends a bolt of negative energy that burns a target enemy unit's mana and deals damage proportional to the amount of mana burned."
 */
BZ_SIMPLE_SPELL_PROC(AbilityManaBurn) {
    edict_t *target = st.entity;
    uint32_t level = S_SpellLevel(caster, spell->code);
    float amount = MIN(target->mana.value, S_SpellData(spell->code, level, 1));

    G_SpawnAbilityLightning(&(abilityLightningParams_t){
        .ability_id = spell->code, .index = 0, .source = caster, .target = target,
        .duration_ms = 300,
    });
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, target, NULL, true);
    target->mana.value -= amount;
    if (amount > 0.0f) S_SpellDamage(target, caster, (int)amount);
}

/* Name=Dark Ritual
 * Ubertip="Sacrifices a friendly non-Hero unit, converting a percentage of its hit points into mana for the caster."
 * Dark Ritual converts the authored fraction of an allied non-hero's maximum
 * life into caster mana, then uses the normal damage/death path to sacrifice it. */
static void dark_ritual_cast_done(edict_t *caster) {
    if (caster && caster->stand) caster->stand(caster);
}

static umove_t dark_ritual_cast_move = { "spell", ai_idle, dark_ritual_cast_done, CAbilityDarkRitual };

static void AbilityDarkRitual_Execute(edict_t *caster, spellTarget_t st, abilityitem_t const *spell) {
    edict_t *target = st.entity;
    uint32_t level = S_SpellLevel(caster, spell->code);
    float mana = target->health.max_value * S_SpellData(spell->code, level, 1);

    unit_setmove(caster, &dark_ritual_cast_move);
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_TARGET, 0, target, NULL, true);
    caster->mana.value = MIN(caster->mana.max_value, caster->mana.value + mana);
    G_SpawnAbilityEffectTarget(spell->code, WC3_EFFECT_CASTER, 0, caster, NULL, true);
    T_Damage(target, caster, (uint32_t)MAX(1.0f, target->health.value));
}

BZ_ABILITY_PROC(CAbilityDarkRitual) {
    abilityitem_t const *spell = call ? call->item : NULL;
    if (msg == A_COMMAND) {
        edict_t *clent = call ? call->entity : NULL;
        edict_t *caster = clent && clent->client ? G_GetMainSelectedUnit(clent->client) : ent;
        if (caster && caster->mana.max_value > 0.0f && caster->mana.value >= caster->mana.max_value) {
            G_ShowCommandErrorKey(clent, "Manamaxed", NULL);
            return true;
        }
    } else if (msg == A_EXECUTE) {
        spellTarget_t target = call && call->target ? *call->target : MAKE(spellTarget_t, .type = SPELL_TARGET_NONE);
        AbilityDarkRitual_Execute(ent, target, spell);
        return true;
    }
    return CAbilitySimpleSpell(ent, msg, call);
}

/* Name=Frost Armor
 * Ubertip="Creates a shield of frost around a target friendly unit. The shield adds <ACfu,DataB1> armor and slows melee units that attack it for <ACfu,Dur1> seconds. Lasts <ACfu,DataA1> seconds."
 * Untip="Right-click to activate auto-casting."
 * Unubertip="Right-click to deactivate auto-casting."
 */
BZ_SIMPLE_SPELL_PROC(AbilityFrostArmor) {
    edict_t *target = st.entity;
    uint32_t level = S_SpellLevel(caster, spell->code);
    cstring_t buff = S_SpellBuffId(spell->code, level);

    if (!target || !buff || strlen(buff) < 4) {
        fprintf(stderr, "WC3: %.4s has no authored BuffID\n", (cstring_t)&spell->code);
        return;
    }
    {
        heroabilitystatus_t *status = S_SpellApplyTimedStatusFromAbility(
            target, spell->code, level, buff, S_SpellData(spell->code, level, 1));
        /* Frost Armor's TargetArt is authored on its BUfa buff, not AUfa/AUfu. */
        if (status) unit_status_enabletargetart(target, status, "chest");
    }
}

void divine_shield_think(edict_t *ent) {
    edict_t *caster = S_SpellChannelOwner(ent);

    if (!caster) {
        G_FreeEdict(ent);
        return;
    }
    if (G_Time() < ent->spawn_time) return;
    /* Preserve innate/scripted invulnerability; independent status contributors
     * remain authoritative through unit_hasstatusstate after this thinker ends. */
    G_SetUnitFlag(caster, UNIT_FLAG_INVULNERABLE, ent->resources);
    G_FreeEdict(ent);
}

/* Name=Divine Shield
 * Ubertip="Makes the Paladin invulnerable to damage for <AHds,Dur1> seconds."
 */
BZ_SIMPLE_SPELL_PROC(AbilityDivineShield) {
    uint32_t level = S_SpellLevel(caster, spell->code);
    float duration = MAX(0.1f, S_SpellDuration(spell->code, level, true));
    cstring_t buff = S_SpellBuffId(spell->code, level);
    edict_t *thinker = S_SpellIdentityThinker(caster, 0, NULL);
    heroabilitystatus_t *status;

    if (!thinker) return;
#if BZ_COMPAT_WORKAROUNDS
    /* RoC AbilityData has no BuffID column; the HumanAbilityFunc contract
     * still authors Divine Shield's TargetArt under BHds. */
    if ((!buff || !*buff || !strcmp(buff, "-") || !strcmp(buff, "_")) &&
        spell->code == MAKEFOURCC('A','H','d','s')) buff = "BHds";
#endif
    thinker->resources = G_UnitHasFlag(caster, UNIT_FLAG_INVULNERABLE);
    thinker->spawn_time = G_Time() + (uint32_t)(duration * 1000.0f);
    G_SetUnitFlag(caster, UNIT_FLAG_INVULNERABLE, true);
    thinker->think = divine_shield_think;
    status = buff && strlen(buff) >= 4 ? S_SpellApplyTimedStatus(caster, buff, level, duration) : NULL;
    if (status) unit_status_enabletargetart(caster, status, "origin");
}

/* Phase Shift is unavailable while the Faerie Dragon's Mana Flare status locks orders. */
BZ_ABILITY_PROC(CAbilityPhaseShift) {
    uint32_t const code = call && call->item ? call->item->code : 0;
    switch (msg) {
    case A_AUTOCAST_ON:
        return ent && ent->unit_state && ent->unit_state->autocast == code;
    case A_AUTOCAST_SET:
        return true;
    case A_DAMAGED:
        /* UnitAbilities.slk selects Apsh as the Faerie Dragon's default
         * autocast. Trigger its ordinary no-target cast when a hit lands. */
        if (ent && code && G_UnitAutocastIsOn(ent, code))
            (void)S_CastNoTargetSpell(ent, code);
        return false;
    default:
        break;
    }
    if (msg == A_VALIDATE && S_ManaFlareActive(ent))
        return false;
    return CAbilityDivineShield(ent, msg, call);
}

/* Name=Bash
 * Ubertip="Gives a chance that an attack will deal bonus damage and stun the target."
 * TODO: no command handler; the attack-resolution path must consume this passive.
 */
/* Name=Invulnerable
 * Ubertip="This unit cannot be damaged."
 */

/* Couple Instant remains a non-spell ability with a stub command handler. */
BZ_COMMAND_PROC(AbilityCoupleInstant) { UI_AddCancelButton(clent); }
