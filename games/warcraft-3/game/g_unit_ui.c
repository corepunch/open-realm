/*
 * g_unit_ui.c — Server-side unit HUD data helpers.
 */

#include "g_local.h"

/* Defined in skills/s_spell.c — remaining cooldown fraction for a unit's ability,
 * used to shade the command-card button while it recharges. */
FLOAT S_SpellCooldownFraction(LPEDICT caster, DWORD code, DWORD level);

static void G_CopyString(LPSTR out, DWORD out_size, LPCSTR text) {
    if (!out || out_size == 0) {
        return;
    }
    snprintf(out, out_size, "%s", text ? text : "");
}

static LPCSTR G_ResearchField(LPCSTR field, BOOL research) {
    static char buffer[64];

    if (!research) {
        return field;
    }
    snprintf(buffer, sizeof(buffer), "Research%s", field);
    return buffer;
}

static unitRace_t G_RaceFromString(LPCSTR race) {
    if (!race) return RACE_UNKNOWN;
    if (!strcmp(race, STR_HUMAN)) return RACE_HUMAN;
    if (!strcmp(race, STR_ORC)) return RACE_ORC;
    if (!strcmp(race, STR_UNDEAD)) return RACE_UNDEAD;
    if (!strcmp(race, STR_NIGHTELF)) return RACE_NIGHTELF;
    return RACE_UNKNOWN;
}

LPCSTR GetBuildCommand(unitRace_t race) {
    switch (race) {
        case RACE_HUMAN: return STR_CmdBuildHuman;
        case RACE_ORC: return STR_CmdBuildOrc;
        case RACE_UNDEAD: return STR_CmdBuildUndead;
        case RACE_NIGHTELF: return STR_CmdBuildNightElf;
        default: return STR_CmdBuild;
    }
}

static LPCSTR G_CommandArtCode(LPEDICT ent, LPCSTR code) {
    if (!strcmp(code, STR_CmdBuild)) {
        return GetBuildCommand(G_RaceFromString(ent->data.UnitData->race));
    }
    return code;
}

static LPCSTR G_RemoveQuotes(LPCSTR text) {
    static char buffers[4][1024];
    static DWORD cursor;
    LPSTR out = buffers[cursor++ & 3];
    size_t len;

    out[0] = '\0';
    if (!text) {
        return out;
    }
    len = strlen(text);
    if (len >= 2 && text[0] == '"' && text[len - 1] == '"') {
        snprintf(out, sizeof(buffers[0]), "%.*s", (int)(len - 2), text + 1);
    } else {
        snprintf(out, sizeof(buffers[0]), "%s", text);
    }
    return out;
}

static LPCSTR G_AbilityString(LPCSTR classname, LPCSTR field) {
    return G_AbilityDataText(classname, field);
}

static LPCSTR G_ProcessTooltipString(LPCSTR input) {
    static char buffers[4][1024];
    static DWORD cursor;
    LPSTR out = buffers[cursor++ & 3];

    out[0] = '\0';
    if (!input) {
        return out;
    }
    for (LPCSTR p = input; *p && strlen(out) < sizeof(buffers[0]) - 1; p++) {
        if (*p == '<') {
            char classname[16];
            char field[16];
            LPCSTR replacement;
            int matched = sscanf(p, "<%15[^,],%15[^>]>", classname, field);

            if (matched == 2 && (replacement = G_AbilityString(classname, field))) {
                strncat(out, replacement, sizeof(buffers[0]) - strlen(out) - 1);
                p += strlen(classname) + strlen(field) + 2;
                continue;
            }
        }
        strncat(out, p, 1);
    }
    return out;
}

static LPCSTR G_StringForLevel(LPCSTR text, DWORD level) {
    if (!text || level == 0) {
        return text;
    }
    PARSE_LIST(text, perlevel, parse_segment) {
        if (level > 1) {
            level--;
        } else {
            return perlevel;
        }
    }
    return text;
}

static LPCSTR G_FormatTooltipLevel(LPCSTR input, DWORD level) {
    static char buffers[4][1024];
    static DWORD cursor;
    LPSTR out = buffers[cursor++ & 3];
    LPSTR const out_end = out + sizeof(buffers[0]) - 1;

    if (!input) {
        out[0] = '\0';
        return out;
    }
    while (*input && out < out_end) {
        if (level && input[0] == '%' && input[1] == 'd') {
            char number[16];
            size_t count;

            snprintf(number, sizeof(number), "%u", (unsigned)level);
            count = MIN(strlen(number), (size_t)(out_end - out));
            memcpy(out, number, count);
            out += count;
            input += 2;
            continue;
        }
        *out++ = *input++;
    }
    *out = '\0';
    return buffers[(cursor - 1) & 3];
}

static LPCSTR G_CleanTooltipString(LPCSTR text, DWORD level) {
    return G_RemoveQuotes(G_FormatTooltipLevel(
        G_ProcessTooltipString(G_StringForLevel(text, level)), level));
}

static LPCSTR G_UIArtPath(LPCSTR art) {
    if (!art || !*art) {
        return art;
    }
    return Theme_String(art, art);
}

static BOOL G_BuildCommandButtonState(LPEDICT ent, LPCSTR code, BOOL research, DWORD level, int toggle_state, gameCommandButton_t *button) {
    char command_code[256];
    char art_level[256];
    LPCSTR base_code;
    LPCSTR art_code;
    LPCSTR art;
    LPCSTR art_path;
    LPCSTR buttonpos;
    LPCSTR tip;
    LPCSTR ubertip;
    LPCSTR hotkey;
    ability_t const *ability;
    DWORD ability_code = 0;
    BOOL toggle_on = false;
    BOOL upgrade_research = false;
    DWORD x = UINT_MAX;
    DWORD y = UINT_MAX;

    if (!ent || !code || !*code || !button) {
        return false;
    }

    /* parse_segment() returns a shared static buffer. Command-card callers
     * commonly pass that buffer here, while tooltip level selection also uses
     * parse_segment(). Own the command string before any nested parsing so
     * ResearchTip/ResearchUbertip processing cannot overwrite the rawcode. */
    G_CopyString(command_code, sizeof(command_code), code);
    code = command_code;

    memset(button, 0, sizeof(*button));
    ability = FindAbilityForCommand(code);
    if (strlen(code) == 4) {
        ability_code = G_AbilityCodeName(code);
        base_code = GetClassName(ability_code);
        upgrade_research = research && G_UpgradeData(ability_code)->id == ability_code;
    } else {
        base_code = code;
    }
    art_code = G_CommandArtCode(ent, code);
    toggle_on = !research && (toggle_state >= 0 ? toggle_state != 0 :
        ability && ability->is_toggle_on && ability->is_toggle_on(ent));
    art = FindConfigValue(art_code, toggle_on ? STR_UNART :
                         G_ResearchField(STR_ART, research && !upgrade_research));
    buttonpos = FindConfigValue(art_code, toggle_on ? STR_UNBUTTONPOS :
                               G_ResearchField(STR_BUTTONPOS, research && !upgrade_research));
    tip = FindConfigValue(art_code, toggle_on ? STR_UNTIP :
                         G_ResearchField(STR_TIP, research && !upgrade_research));
    ubertip = FindConfigValue(art_code, toggle_on ? STR_UNUBERTIP :
                             G_ResearchField(STR_UBERTIP, research && !upgrade_research));
    hotkey = FindConfigValue(art_code, toggle_on ? STR_UNHOTKEY :
                            G_ResearchField(STR_HOTKEY, research && !upgrade_research));
    G_CopyString(art_level, sizeof(art_level), research ? G_StringForLevel(art, level) : art);
    art_path = G_UIArtPath(art_level);

    if (buttonpos && *buttonpos) {
        sscanf(buttonpos, "%u,%u", &x, &y);
    }

    G_CopyString(button->art, sizeof(button->art), art_path);
    G_CopyString(button->tooltip, sizeof(button->tooltip), G_CleanTooltipString(tip, level));
    G_CopyString(button->ubertip, sizeof(button->ubertip), G_CleanTooltipString(ubertip, level));
    G_CopyString(button->command, sizeof(button->command), code);
    if (!research && ability && ability->autocast_set && ability->autocast_is_on) {
        strlcpy(button->alternate, "autocast ", sizeof(button->alternate));
        strlcat(button->alternate, code, sizeof(button->alternate));
        button->alternate_active = G_UnitAutocastIsOn(ent, ability) ? 1 : 0;
    }
    hotkey = research ? G_StringForLevel(hotkey, level) : hotkey;
    button->hotkey = hotkey && *hotkey ? *hotkey : '\0';
    button->x = x == UINT_MAX ? 255 : (BYTE)MIN(x, 3);
    button->y = y == UINT_MAX ? 255 : (BYTE)MIN(y, 2);
    button->research = research ? 1 : 0;
    button->active = (BYTE)GetAbilityIndex(ability);
    if (ability_code) {
        button->manacost = S_SpellNumber(ability_code, ABILITY_NUMBER_COST, level);
    }
    if (!button->art[0]) {
        fprintf(stderr,
                "G_BuildCommandButton: skipping missing art unit=%.4s code=%s art_code=%s raw_art=%s\n",
                (char *)&ent->class_id,
                base_code,
                art_code ? art_code : "",
                art ? art : "");
        return false;
    }
    return true;
}

BOOL G_BuildCommandButton(LPEDICT ent, LPCSTR code, BOOL research, DWORD level, gameCommandButton_t *button) {
    return G_BuildCommandButtonState(ent, code, research, level, -1, button);
}

static void G_AddCommandButton(LPEDICT ent,
                               gameCommandButton_t *buttons,
                               BYTE max_buttons,
                               BYTE *count,
                               LPCSTR code,
                               BOOL research,
                               DWORD level) {
    if (!buttons || !count || *count >= max_buttons) {
        return;
    }
    if (G_BuildCommandButton(ent, code, research, level, &buttons[*count])) {
        if (buttons[*count].x == 255 || buttons[*count].y == 255) {
            buttons[*count].x = *count % 4;
            buttons[*count].y = *count / 4;
        }
        (*count)++;
    }
}

static BOOL G_IsImplementedAbility(LPCSTR code) {
    ability_t const *ability = FindAbilityForCommand(code);
    return ability && ability->cmd;
}

static BOOL G_HasCommandRawcode(gameCommandButton_t const *buttons, BYTE count, DWORD code) {
    FOR_LOOP(i, count) {
        DWORD button_code = 0;
        if (strlen(buttons[i].command) < 4) continue;
        memcpy(&button_code, buttons[i].command, sizeof(button_code));
        if (button_code == code) return true;
    }
    return false;
}

static void G_AddAbilityCommandButtons(LPEDICT ent, gameCommandButton_t *buttons, BYTE max_buttons,
                                       BYTE *count, LPCSTR code) {
    ability_t const *ability = FindAbilityForCommand(code);
    BYTE idx;
    DWORD rawcode;

    if (!ability || !ability->cmd || strlen(code) != 4 || *count >= max_buttons) return;
    memcpy(&rawcode, code, sizeof(rawcode));
    if (G_HasCommandRawcode(buttons, *count, rawcode)) return;
    idx = *count;
    G_AddCommandButton(ent, buttons, max_buttons, count, code, false, 0);
    if (*count > idx) buttons[idx].cooldown = S_SpellCooldownFraction(ent, rawcode, 0);
    if (!(ability->flags & ABILITY_SEPARATE_OFF) || *count >= max_buttons) return;
    if (G_BuildCommandButtonState(ent, code, false, 0, 1, &buttons[*count])) {
        size_t used;
        if (buttons[*count].x == 255 || buttons[*count].y == 255) {
            buttons[*count].x = *count % 4;
            buttons[*count].y = *count / 4;
        }
        used = strlen(buttons[*count].command);
        snprintf(buttons[*count].command + used, sizeof(buttons[*count].command) - used, ":off");
        (*count)++;
    }
}

static void G_DisableCommandButton(gameCommandButton_t *button, LPCSTR reason) {
    size_t used;

    if (!button) return;
    button->disabled = 1;
    if (!reason || !*reason) return;
    used = strlen(button->ubertip);
    snprintf(button->ubertip + used, sizeof(button->ubertip) - used,
             "%s|cffffcc00%s|r", used ? "|n" : "", reason);
}

static BOOL G_BuildHeroReviveButton(LPEDICT altar, LPEDICT hero, BYTE slot,
                                    gameCommandButton_t *button) {
    char command[32];
    char fallback[128];
    LPCSTR code;
    LPCSTR art;
    LPCSTR tip;
    LPCSTR ubertip;

    if (!G_HeroCanBeRevivedAt(altar, hero) || !button) return false;
    code = GetClassName(hero->class_id);
    art = FindConfigValue(code, STR_ART);
    tip = hero->data.UnitProfile ? hero->data.UnitProfile->reviveTip : NULL;
    ubertip = hero->data.UnitProfile ? hero->data.UnitProfile->uberTip : NULL;
    if (!art || !*art) return false;

    memset(button, 0, sizeof(*button));
    G_CopyString(button->art, sizeof(button->art), G_UIArtPath(art));
    if (tip && *tip) {
        G_CopyString(button->tooltip, sizeof(button->tooltip), G_CleanTooltipString(tip, 0));
    } else {
        snprintf(fallback, sizeof(fallback), "Revive %s",
                 hero->data.UnitProfile && hero->data.UnitProfile->name ? hero->data.UnitProfile->name : code);
        G_CopyString(button->tooltip, sizeof(button->tooltip), fallback);
    }
    G_CopyString(button->ubertip, sizeof(button->ubertip), G_CleanTooltipString(ubertip, 0));
    snprintf(command, sizeof(command), "revive:%u", (unsigned)hero->s.number);
    G_CopyString(button->command, sizeof(button->command), command);
    button->x = slot % 4;
    button->y = slot / 4;
    button->active = 255;
    return true;
}

BYTE G_GetCommandButtons(LPEDICT ent, gameCommandButton_t *buttons, BYTE max_buttons) {
    BYTE count = 0;
    UnitBalance_t const *b;
    UnitWeapons_t const *w;
    UnitAbilities_t const *a;

    if (!ent || !ent->class_id || !buttons) {
        return 0;
    }
    memset(buttons, 0, sizeof(*buttons) * max_buttons);
    b = ent->data.UnitBalance;
    w = ent->data.UnitWeapons;
    a = ent->data.UnitAbilities;

    /* Construction has its own command-card state.  Returning no buttons for
     * every birth move made spawned Human buildings impossible to cancel. */
    if (ent->construction.active) {
        G_AddCommandButton(ent, buttons, max_buttons, &count, STR_CmdCancelBuild, false, 0);
        return count;
    }
    if (ent->currentmove && ent->currentmove->think == ai_birth) {
        return 0;
    }

    if (b->speed > 0) {
        G_AddCommandButton(ent, buttons, max_buttons, &count, STR_CmdMove, false, 0);
        G_AddCommandButton(ent, buttons, max_buttons, &count, STR_CmdHoldPos, false, 0);
        G_AddCommandButton(ent, buttons, max_buttons, &count, STR_CmdPatrol, false, 0);
        G_AddCommandButton(ent, buttons, max_buttons, &count, STR_CmdStop, false, 0);
    }
    if (w->attack1.damageDice != 0) {
        G_AddCommandButton(ent, buttons, max_buttons, &count, STR_CmdAttack, false, 0);
    }
    if (G_UnitProfile(ent->class_id)->builds) {
        G_AddCommandButton(ent, buttons, max_buttons, &count, STR_CmdBuild, false, 0);
    }
    if (a->heroAbilList) {
        BYTE const idx = count;
        G_AddCommandButton(ent, buttons, max_buttons, &count, STR_CmdSelectSkill, false, 0);
        if (count > idx) {
            buttons[idx].number = ent->hero.skillpoints;
        }
    }
    if (G_UnitHasRally(ent)) {
        G_AddCommandButton(ent, buttons, max_buttons, &count, STR_CmdRally, false, 0);
    }
    if (a->abilList) {
        PARSE_LIST(a->abilList, abil, parse_segment) {
            if (G_IsImplementedAbility(abil) && G_ActorHasSkill(ent, abil))
                G_AddAbilityCommandButtons(ent, buttons, max_buttons, &count, abil);
        }
    }
    FOR_LOOP(i, ARRAY_COUNT(ent->abilities.added)) {
        char abil[5] = {0};
        DWORD const code = ent->abilities.added[i];
        if (!code) continue;
        memcpy(abil, &code, 4);
        if (G_IsImplementedAbility(abil) && G_ActorHasSkill(ent, abil))
            G_AddAbilityCommandButtons(ent, buttons, max_buttons, &count, abil);
    }
    FOR_LOOP(i, MAX_HERO_ABILITIES) {
        heroability_t const *ha = ent->heroabilities + i;
        if (ha->level > 0) {
            BYTE const idx = count;
            G_AddCommandButton(ent, buttons, max_buttons, &count, GetClassName(ha->code), false, ha->level);
            if (count > idx) {
                buttons[idx].cooldown = S_SpellCooldownFraction(ent, ha->code, ha->level);
            }
        }
    }
    if (G_UnitProfile(ent->class_id)->trains) {
        PARSE_LIST(G_UnitProfile(ent->class_id)->trains, unit, parse_segment) {
            LPGAMECLIENT client = G_GetPlayerClientByNumber(ent->s.player);
            DWORD unit_id = 0;
            buildCommandState_t state;
            char reason[128];
            BYTE idx;

            if (strlen(unit) != 4 || !client || client->ps.number != ent->s.player) continue;
            memcpy(&unit_id, unit, sizeof(unit_id));
            state = G_GetTrainCommandState(client, ent, unit_id, reason, sizeof(reason));
            if (state == BUILD_COMMAND_ABSENT || state == BUILD_COMMAND_HIDDEN) continue;
            idx = count;
            G_AddCommandButton(ent, buttons, max_buttons, &count, unit, false, 0);
            if (state == BUILD_COMMAND_DISABLED && count > idx) {
                G_DisableCommandButton(&buttons[idx], reason);
            }
        }
    }
    if (G_UnitProfile(ent->class_id)->researches) {
        PARSE_LIST(G_UnitProfile(ent->class_id)->researches, upgrade, parse_segment) {
            LPGAMECLIENT client = G_GetPlayerClientByNumber(ent->s.player);
            DWORD upgrade_id = 0;
            LONG next_level = 0;
            buildCommandState_t state;
            char reason[128];
            BYTE idx;

            if (strlen(upgrade) != 4 || !client || client->ps.number != ent->s.player) continue;
            memcpy(&upgrade_id, upgrade, sizeof(upgrade_id));
            state = G_GetResearchCommandState(client, ent, upgrade_id, &next_level, reason, sizeof(reason));
            if (state == BUILD_COMMAND_ABSENT || state == BUILD_COMMAND_HIDDEN) continue;
            idx = count;
            G_AddCommandButton(ent, buttons, max_buttons, &count, upgrade, true, (DWORD)next_level);
            if (state == BUILD_COMMAND_DISABLED && count > idx) {
                G_DisableCommandButton(&buttons[idx], reason);
            }
        }
    }
    if (G_UnitCanReviveHeroes(ent)) {
        FILTER_EDICTS(hero, hero->inuse && hero->s.player == ent->s.player) {
            if (count >= max_buttons) break;
            if (G_BuildHeroReviveButton(ent, hero, count, &buttons[count])) count++;
        }
    }
    /* The existing Cancel command can safely cancel the active revival. Do
     * not expose it for ordinary unit training until that queue has matching
     * refund semantics. */
    if (ent->build && ent->build->revival.reviving) {
        G_AddCommandButton(ent, buttons, max_buttons, &count, STR_CmdCancel, false, 0);
    }

    return count;
}

BOOL G_BuildInventoryItem(LPEDICT ent, LPEDICT item, BYTE slot, gameInventoryItem_t *out) {
    LPCSTR item_name;
    LPCSTR art;

    if (!ent || !out || slot >= G_InventoryCapacity(ent) || !G_IsItem(item) ||
        item->item.carrier != ent || item->item.inventory_slot != slot || item->item.in_world) return false;

    memset(out, 0, sizeof(*out));
    item_name = GetClassName(item->class_id);
    art = FindConfigValue(item_name, STR_ART);
    G_CopyString(out->art, sizeof(out->art), G_UIArtPath(art));
    G_CopyString(out->tooltip, sizeof(out->tooltip), G_CleanTooltipString(FindConfigValue(item_name, STR_TIP), 0));
    G_CopyString(out->ubertip, sizeof(out->ubertip), G_CleanTooltipString(FindConfigValue(item_name, STR_UBERTIP), 0));
    out->slot = slot;
    out->charges = G_ItemCharges(item);
    if (!out->art[0]) {
        fprintf(stderr, "G_BuildInventoryItem: missing Art item=%.4s slot=%u\n",
                (char *)&item->class_id, (unsigned)slot);
    }
    return true;
}

BYTE G_GetInventory(LPEDICT ent, gameInventoryItem_t *items, BYTE max_items) {
    BYTE count = 0;
    DWORD capacity;

    if (!ent || !items) return 0;
    memset(items, 0, sizeof(*items) * max_items);
    capacity = G_InventoryCapacity(ent);
    FOR_LOOP(slot, capacity) {
        if (count >= max_items) break;
        if (G_BuildInventoryItem(ent, ent->inventory[slot], (BYTE)slot, &items[count])) count++;
    }
    return count;
}

BYTE G_GetBuildQueue(LPEDICT ent, gameQueueItem_t *queue, BYTE max_queue) {
    BYTE count = 0;
    DWORD cursor = G_Time();
    BOOL food_blocked = false;

    if (!ent || !queue) {
        return 0;
    }
    memset(queue, 0, sizeof(*queue) * max_queue);
    for (LPEDICT build = ent->build; build && count < max_queue;
         build = build->revival.reviving ? build->revival.queue_next : build->build) {
        DWORD duration;
        FLOAT progress = 0;

        if (build->research.upgrade != 0) {
            gameCommandButton_t button;
            duration = (DWORD)(MAX(0.0f, build->research.duration) * 1000.0f);
            if (G_BuildCommandButton(ent, GetClassName(build->research.upgrade), true,
                                     (DWORD)build->research.level, &button)) {
                G_CopyString(queue[count].art, sizeof(queue[count].art), button.art);
            }
            if (count == 0 && build->research.duration > 0.0f) {
                progress = build->research.progress / build->research.duration;
                progress = MAX(0, MIN(progress, 1));
            }
        } else {
            LPCSTR build_name = GetClassName(build->class_id);
            duration = build->revival.reviving
                ? (DWORD)(G_HeroReviveTime(build) * 1000.0f)
                : (build->data.UnitBalance ? (DWORD)MAX(0, build->data.UnitBalance->buildTime) * 1000 : 0);
            if (count == 0) {
                LONG cost = build->data.UnitBalance ? MAX(0, build->data.UnitBalance->foodUsed) : 0;
                if (build->revival.reviving && duration > 0) {
                    progress = build->revival.progress / ((FLOAT)duration / 1000.0f);
                    progress = MAX(0, MIN(progress, 1));
                } else if (build->health.max_value > 0) {
                    progress = build->health.value / build->health.max_value;
                    progress = MAX(0, MIN(progress, 1));
                }
                food_blocked = build->training && cost > 0 && build->food.used == 0 && G_FoodLimitsEnabled();
            }
            G_CopyString(queue[count].art, sizeof(queue[count].art), FindConfigValue(build_name, STR_ART));
        }

        if (food_blocked) {
            /* A food-stalled head has no meaningful predicted completion time.
             * Zero times are an explicit wire sentinel: the client holds the
             * progress bar at zero and still renders every waiting queue icon. */
            queue[count].starttime = 0;
            queue[count].endtime = 0;
        } else if (duration > 0) {
            DWORD elapsed = (DWORD)(duration * progress);
            queue[count].starttime = count == 0 && elapsed <= cursor ? cursor - elapsed : cursor;
            queue[count].endtime = queue[count].starttime + duration;
            cursor = queue[count].endtime;
        } else {
            queue[count].starttime = cursor;
            queue[count].endtime = cursor;
        }
        count++;
        if ((build->revival.reviving && build->revival.queue_next == build) ||
            (!build->revival.reviving && build->build == build)) {
            break;
        }
    }
    return count;
}
