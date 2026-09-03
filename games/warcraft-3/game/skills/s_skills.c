#include "s_skills.h"

#ifdef WC3_DEBUG_AUTOCAST
int G_AutocastDebugLevel(void) {
    LPCSTR value;

    if (!gi.CvarString) return 0;
    value = gi.CvarString("wc3_autocast_debug", "0");
    return value ? atoi(value) : 0;
}
#endif

typedef struct {
    LPCSTR classname;
    ability_t *ability;
} abilityitem_t;

/* ROC/TFT physical data columns are normalized by the AbilityData DDX schema. */
FLOAT AB_Data(LPCSTR classname, DWORD level, DWORD index) {
    AbilityData_t const *row = G_AbilityDataName(classname);
    level = MAX(1, MIN(level, 4)); index = MAX(1, MIN(index, 9));
    return row->data[level - 1][index - 1];
}

static abilityitem_t abilitylist[] = {
    { STR_CmdStop, &a_stop },
    { STR_CmdMove, &a_move },
    { STR_CmdAttack, &a_attack },
    { STR_CmdBuild, &a_build },
    { STR_CmdHoldPos, &a_holdpos },
    { STR_CmdPatrol, &a_patrol },
    { STR_CmdRally, &a_rally },
    { STR_CmdCancel, &a_cancel },
    { STR_CmdSelectSkill, &a_selectskill },

    { "Ahar", &a_harvest },
    { "Amil", &a_militia },
    { "Arep", &a_repair },
    { "Agld", &a_goldmine },
    { "AHad", &a_devotionaura },
    { "AHhb", &a_holylight },
    { "AHwe", &a_water_elemental },
    { "AHbz", &a_blizzard },
    { "AHtb", &a_thunderbolt },
    { "ANfb", &a_firebolt },
    { "Apxf", &a_phoenix_fire },
    { "AOsf", &a_feral_spirit },
    { "Abun", &a_cargo_hold_burrow },
    { "Astd", &a_stand_down },
    { "AEim", &a_immolation },
    { "Aenc", &a_cargo_hold_entangled_mine },
    { "Aent", &a_entangle_goldmine },
    { "Aegm", &a_entangled_mine },
    { "Aeat", &a_eat_tree },
    { "Ambt", &a_moon_well },
    { "ANch", &a_charm },
    { "AIco", &a_charm },
    { "AHca", &a_cold_arrows },
    { "Agl2", &a_goldmine_overlayed },
    { "Abgm", &a_blighted_goldmine },
    { "Abli", &a_blight },
    { "Aaha", &a_acolyte_harvest },
    { "Artn", &a_return_resources },
    { "Awha", &a_wisp_harvest },
    { "Ahrl", &a_harvest_lumber },
    { "ANcl", &a_channel_test },
    { "AUcs", &a_carrion_swarm },
    { "AInv", &a_inventory },
    { "Aren", &a_repair_generic },
    { "Arst", &a_repair_generic },
    { "Avul", &a_invulnerable },
    { "Apit", &a_shop_purchase_item },
    { "Aneu", &a_neutral_building },
    { "Aall", &a_shop_sharing },
    { "Acoi", &a_couple_instant },
    { "AIhe", &a_item_heal },
    { "AIma", &a_item_mana_regain },
    { "AIat", &a_item_attack_bonus },
    { "AIab", &a_item_stat_bonus },
    { "AIim", &a_item_permanent_stat_gain },
    { "AIsm", &a_item_permanent_stat_gain },
    { "AIam", &a_item_permanent_stat_gain },
    { "AIxm", &a_item_permanent_stat_gain },
    { "AIde", &a_item_defense_bonus },
    { "AIml", &a_item_life_bonus },
    { "AImm", &a_item_mana_bonus },
    { "AIfs", &a_item_figurine_summon },
    { "AImi", &a_item_permanent_life_gain },
    { "AIem", &a_item_experience_gain },
    { "AIlm", &a_item_level_gain },
    { "AIda", &a_item_defense_aoe },
    { "Acar", &a_cargo_hold },
    { "Aloa", &a_load },
    { "Adro", &a_drop },
    { "Adri", &a_drop_instant },
    { "Aroo", &a_root },

    /* Night Elf Warden (Maiev) hero abilities. */
    { "AEbl", &a_blink },
    { "AEfk", &a_fan_of_knives },
    { "AEsh", &a_shadow_strike },

    /* Additional hero spells. */
    { "ANfs", &a_flame_strike },  /* Flame Strike (Pit Lord) */
    { "ANdr", &a_siphon_mana },  /* Siphon Mana (Blood Mage) */
};

ability_t const *FindAbilityByClassname(LPCSTR classname) {
    FOR_LOOP(i, game.num_abilities) {
        if (!abilitylist[i].classname)
            continue;
        if (!strcmp(abilitylist[i].classname, classname))
            return abilitylist[i].ability;
    }
    return NULL;
}

/* Command-card names use two namespaces. Engine commands (CmdBuild, CmdMove,
 * etc.) are full strings registered directly in abilitylist. WC3 abilities are
 * four-character rawcodes whose AbilityData alias may point at a base handler.
 * Only rawcodes belong in the SLK resolver: passing CmdBuild through FS_SLKKey
 * truncates it to CmdB and loses the registered build command. */
ability_t const *FindAbilityForCommand(LPCSTR classname) {
    if (!classname || !*classname) {
        return NULL;
    }
    if (strlen(classname) != 4) {
        return FindAbilityByClassname(classname);
    }
    return FindAbilityByClassname(GetClassName(G_AbilityCodeName(classname)));
}

static BOOL unit_has_ability_handler(LPEDICT ent, ability_t const *wanted) {
    LPCSTR abilities;

    if (!ent || !wanted || !ent->UnitAbilities) return false;
    abilities = ent->UnitAbilities->abilList;
    if (!abilities) return false;

    PARSE_LIST(abilities, ability_name, parse_segment) {
        if (FindAbilityForCommand(ability_name) == wanted) return true;
    }
    return false;
}

BOOL G_UnitAutocastIsOn(LPEDICT ent, ability_t const *ability) {
    return ent && ability && ability->autocast_is_on && unit_has_ability_handler(ent, ability) &&
           ability->autocast_is_on(ent);
}

BOOL G_SetUnitAutocast(LPEDICT ent, ability_t const *ability, BOOL enabled) {
    LPCSTR abilities;

    if (!ent || !ability || !ability->autocast_set || !unit_has_ability_handler(ent, ability)) {
#ifdef WC3_DEBUG_AUTOCAST
        if (G_AutocastDebugLevel() >= 1) {
            fprintf(stderr, "WC3_AUTOCAST toggle rejected unit=%ld ability=%p enabled=%d flags=0x%x\n",
                    ent && g_edicts ? (long)(ent - g_edicts) : -1L, (void *)ability,
                    enabled ? 1 : 0, ent ? ent->aiflags : 0);
        }
#endif
        return false;
    }

    /* Warsmash keeps one selected autocast ability per unit. Turning a new one
     * on first disables every other autocast-capable ability currently present
     * on this unit; abilities without autocast hooks remain untouched. */
    if (enabled && ent->UnitAbilities && (abilities = ent->UnitAbilities->abilList)) {
        PARSE_LIST(abilities, ability_name, parse_segment) {
            ability_t const *other = FindAbilityForCommand(ability_name);
            if (other && other != ability && other->autocast_set) other->autocast_set(ent, false);
        }
    }
    ability->autocast_set(ent, enabled);
    if (enabled) {
        ent->aiflags |= AI_AUTOCAST_ACTIVE;
    } else {
        BOOL any_enabled = false;
        if (ent->UnitAbilities && (abilities = ent->UnitAbilities->abilList)) {
            PARSE_LIST(abilities, ability_name, parse_segment) {
                ability_t const *other = FindAbilityForCommand(ability_name);
                if (other && other->autocast_is_on && other->autocast_is_on(ent)) {
                    any_enabled = true;
                    break;
                }
            }
        }
        if (!any_enabled) ent->aiflags &= ~AI_AUTOCAST_ACTIVE;
    }
#ifdef WC3_DEBUG_AUTOCAST
    if (G_AutocastDebugLevel() >= 1) {
        fprintf(stderr, "WC3_AUTOCAST toggle unit=%ld class=%.4s enabled=%d flags=0x%x idle_worker=%d abilities=%s\n",
                g_edicts ? (long)(ent - g_edicts) : -1L, (LPCSTR)&ent->class_id,
                enabled ? 1 : 0, ent->aiflags, G_UnitIsIdleWorker(ent) ? 1 : 0,
                ent->UnitAbilities && ent->UnitAbilities->abilList ? ent->UnitAbilities->abilList : "<none>");
    }
#endif
    return true;
}

BOOL G_TryUnitAutocast(LPEDICT ent) {
    LPCSTR abilities;

    if (!ent || !(ent->aiflags & AI_AUTOCAST_ACTIVE) || !ent->UnitAbilities ||
        !(abilities = ent->UnitAbilities->abilList)) {
#ifdef WC3_DEBUG_AUTOCAST
        if (G_AutocastDebugLevel() >= 2 && ent) {
            fprintf(stderr, "WC3_AUTOCAST skip unit=%ld class=%.4s flags=0x%x abilities=%s\n",
                    g_edicts ? (long)(ent - g_edicts) : -1L, (LPCSTR)&ent->class_id,
                    ent->aiflags,
                    ent->UnitAbilities && ent->UnitAbilities->abilList ? ent->UnitAbilities->abilList : "<none>");
        }
#endif
        return false;
    }
#ifdef WC3_DEBUG_AUTOCAST
    if (G_AutocastDebugLevel() >= 2) {
        fprintf(stderr, "WC3_AUTOCAST try unit=%ld class=%.4s flags=0x%x abilities=%s\n",
                g_edicts ? (long)(ent - g_edicts) : -1L, (LPCSTR)&ent->class_id,
                ent->aiflags, abilities);
    }
#endif
    PARSE_LIST(abilities, ability_name, parse_segment) {
        ability_t const *ability = FindAbilityForCommand(ability_name);
        BOOL is_on;
        if (!ability || !ability->autocast_is_on || !ability->autocast_acquire) continue;
        is_on = ability->autocast_is_on(ent);
#ifdef WC3_DEBUG_AUTOCAST
        if (G_AutocastDebugLevel() >= 2) {
            fprintf(stderr, "WC3_AUTOCAST ability unit=%ld code=%s on=%d\n",
                    g_edicts ? (long)(ent - g_edicts) : -1L, ability_name, is_on ? 1 : 0);
        }
#endif
        if (is_on && ability->autocast_acquire(ent)) {
#ifdef WC3_DEBUG_AUTOCAST
            if (G_AutocastDebugLevel() >= 1) {
                fprintf(stderr, "WC3_AUTOCAST acquired unit=%ld code=%s\n",
                        g_edicts ? (long)(ent - g_edicts) : -1L, ability_name);
            }
#endif
            return true;
        }
    }
#ifdef WC3_DEBUG_AUTOCAST
    if (G_AutocastDebugLevel() >= 2) {
        fprintf(stderr, "WC3_AUTOCAST no_target unit=%ld\n",
                g_edicts ? (long)(ent - g_edicts) : -1L);
    }
#endif
    return false;
}

DWORD FindAbilityIndex(LPCSTR classname) {
    FOR_LOOP(i, game.num_abilities) {
        if (!abilitylist[i].classname)
            continue;
        if (!strcmp(abilitylist[i].classname, classname))
            return i;
    }
    return 255;
}

void InitAbilities(void) {
    game.num_abilities = sizeof(abilitylist)/sizeof(abilitylist[0]);
    FOR_LOOP(i, game.num_abilities) {
        abilityitem_t *abil = &abilitylist[i];
        if (abil->ability->init) {
            abil->ability->init(abil->classname, abil->ability);
        }
    }
}

void SetAbilityNames(void) {
//    FOR_LOOP(i, game.num_abilities) {
//        if (!abilitylist[i].classname)
//            continue;
//        abilityitem_t *abil = &abilitylist[i];
//    }
}

ability_t const *GetAbilityByIndex(DWORD index) {
    if (index >= game.num_abilities)
        return NULL;
    return abilitylist[index].ability;
}

DWORD GetAbilityIndex(ability_t const *ability) {
    FOR_LOOP(i, game.num_abilities) {
        if (abilitylist[i].ability == ability) {
            return i;
        }
    }
    return 255;
}
