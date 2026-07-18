#include "s_skills.h"

typedef struct {
    LPCSTR classname;
    ability_t *ability;
} abilityitem_t;

void CMD_CancelCommand(LPEDICT ent);

FLOAT AB_Number(LPCSTR classname, LPCSTR field) {
    LPCSTR str = FS_FindSheetCell(game.config.abilities, classname, field);
    return str ? atof(str) : 0;
}

static abilityitem_t abilitylist[] = {
    { STR_CmdStop, &a_stop },
    { STR_CmdMove, &a_move },
    { STR_CmdAttack, &a_attack },
    { STR_CmdBuild, &a_build },
    { STR_CmdHoldPos, &a_holdpos },
    { STR_CmdPatrol, &a_patrol },
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
    { "Acar", &a_cargo_hold },
    { "Aloa", &a_load },
    { "Adro", &a_drop },
    { "Adri", &a_drop_instant },
    { "Aroo", &a_root },

    /* Night Elf Warden (Maiev) hero abilities. */
    { "AEbl", &a_blink },
    { "AEfk", &a_fan_of_knives },
    { "AEsh", &a_shadow_strike },
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
