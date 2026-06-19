/*
 * g_unit_ui.c — Server-side unit HUD data helpers.
 */

#include "g_local.h"
#include "g_metadata.h"

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
        return GetBuildCommand(G_RaceFromString(UnitStringField(UnitsMetaData, ent->class_id, "urac")));
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
    return game.config.abilities ? FS_FindSheetCell(game.config.abilities, classname, field) : NULL;
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

static LPCSTR G_CleanTooltipString(LPCSTR text, DWORD level) {
    return G_RemoveQuotes(G_ProcessTooltipString(G_StringForLevel(text, level)));
}

static LPCSTR G_CommandArtPath(LPCSTR art) {
    if (!art || !*art) {
        return art;
    }
    return Theme_String(art, art);
}

BOOL G_BuildCommandButton(LPEDICT ent, LPCSTR code, BOOL research, DWORD level, gameCommandButton_t *button) {
    LPCSTR base_code;
    LPCSTR art_code;
    LPCSTR art;
    LPCSTR art_path;
    LPCSTR buttonpos;
    LPCSTR tip;
    LPCSTR ubertip;
    LPCSTR hotkey;
    DWORD x = UINT_MAX;
    DWORD y = UINT_MAX;

    (void)level;
    if (!ent || !code || !*code || !button) {
        return false;
    }

    memset(button, 0, sizeof(*button));
    base_code = game.config.abilities ? FS_FindSheetCell(game.config.abilities, code, "code") : NULL;
    if (!base_code) {
        base_code = code;
    }
    art_code = G_CommandArtCode(ent, code);
    art = FindConfigValue(art_code, G_ResearchField(STR_ART, research));
    buttonpos = FindConfigValue(art_code, G_ResearchField(STR_BUTTONPOS, research));
    tip = FindConfigValue(art_code, G_ResearchField(STR_TIP, research));
    ubertip = FindConfigValue(art_code, G_ResearchField(STR_UBERTIP, research));
    hotkey = FindConfigValue(art_code, STR_HOTKEY);
    art_path = G_CommandArtPath(art);

    if (buttonpos && *buttonpos) {
        sscanf(buttonpos, "%u,%u", &x, &y);
    }

    G_CopyString(button->art, sizeof(button->art), art_path);
    G_CopyString(button->tooltip, sizeof(button->tooltip), G_CleanTooltipString(tip, level));
    G_CopyString(button->ubertip, sizeof(button->ubertip), G_CleanTooltipString(ubertip, level));
    G_CopyString(button->command, sizeof(button->command), code);
    button->hotkey = hotkey && *hotkey ? *hotkey : '\0';
    button->x = x == UINT_MAX ? 255 : (BYTE)MIN(x, 3);
    button->y = y == UINT_MAX ? 255 : (BYTE)MIN(y, 2);
    button->research = research ? 1 : 0;
    button->active = (BYTE)FindAbilityIndex(base_code);
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
    return FindAbilityByClassname(code) != NULL;
}

BYTE G_GetCommandButtons(LPEDICT ent, gameCommandButton_t *buttons, BYTE max_buttons) {
    BYTE count = 0;

    if (!ent || !ent->class_id || !buttons) {
        return 0;
    }
    memset(buttons, 0, sizeof(*buttons) * max_buttons);

    if (ent->currentmove && ent->currentmove->think == ai_birth) {
        return 0;
    }

    if (UNIT_SPEED(ent->class_id) > 0) {
        G_AddCommandButton(ent, buttons, max_buttons, &count, STR_CmdMove, false, 0);
        G_AddCommandButton(ent, buttons, max_buttons, &count, STR_CmdHoldPos, false, 0);
        G_AddCommandButton(ent, buttons, max_buttons, &count, STR_CmdPatrol, false, 0);
        G_AddCommandButton(ent, buttons, max_buttons, &count, STR_CmdStop, false, 0);
    }
    if (UNIT_ATTACK1_DAMAGE_NUMBER_OF_DICE(ent->class_id) != 0) {
        G_AddCommandButton(ent, buttons, max_buttons, &count, STR_CmdAttack, false, 0);
    }
    if (UNIT_BUILDS(ent->class_id)) {
        G_AddCommandButton(ent, buttons, max_buttons, &count, STR_CmdBuild, false, 0);
    }
    if (UNIT_ABILITIES_HERO(ent->class_id)) {
        G_AddCommandButton(ent, buttons, max_buttons, &count, STR_CmdSelectSkill, false, 0);
    } else if (UNIT_ABILITIES_NORMAL(ent->class_id)) {
        PARSE_LIST(UNIT_ABILITIES_NORMAL(ent->class_id), abil, parse_segment) {
            LPCSTR code = game.config.abilities ? FS_FindSheetCell(game.config.abilities, abil, "code") : NULL;
            if (code && G_IsImplementedAbility(code)) {
                BYTE const idx = count;
                G_AddCommandButton(ent, buttons, max_buttons, &count, abil, false, 0);
                if (count > idx && strlen(code) >= 4) {
                    buttons[idx].cooldown = S_SpellCooldownFraction(ent,
                        MAKEFOURCC(code[0], code[1], code[2], code[3]), 0);
                }
            }
        }
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
    if (UNIT_TRAINS(ent->class_id)) {
        PARSE_LIST(UNIT_TRAINS(ent->class_id), unit, parse_segment) {
            G_AddCommandButton(ent, buttons, max_buttons, &count, unit, false, 0);
        }
    }

    return count;
}

BYTE G_GetInventory(LPEDICT ent, gameInventoryItem_t *items, BYTE max_items) {
    BYTE count = 0;

    if (!ent || !items) {
        return 0;
    }
    memset(items, 0, sizeof(*items) * max_items);

    for (BYTE slot = 0; slot < MAX_INVENTORY && count < max_items; slot++) {
        LPEDICT item = ent->inventory[slot];
        if (!item || !item->class_id) {
            continue;
        }
        LPCSTR item_name = GetClassName(item->class_id);
        G_CopyString(items[count].art, sizeof(items[count].art), FindConfigValue(item_name, STR_ART));
        G_CopyString(items[count].tooltip, sizeof(items[count].tooltip),
                     G_RemoveQuotes(FindConfigValue(item_name, STR_TIP)));
        G_CopyString(items[count].ubertip, sizeof(items[count].ubertip),
                     G_RemoveQuotes(FindConfigValue(item_name, STR_UBERTIP)));
        items[count].slot = slot;
        count++;
    }

    return count;
}

BYTE G_GetBuildQueue(LPEDICT ent, gameQueueItem_t *queue, BYTE max_queue) {
    BYTE count = 0;
    DWORD cursor = gi.GetTime();

    if (!ent || !queue) {
        return 0;
    }
    memset(queue, 0, sizeof(*queue) * max_queue);
    for (LPEDICT build = ent->build; build && count < max_queue; build = build->build) {
        LPCSTR build_name = GetClassName(build->class_id);
        DWORD duration = UNIT_BUILD_TIME_MSEC(build->class_id);
        FLOAT progress = 0;

        if (count == 0 && build->health.max_value > 0) {
            progress = build->health.value / build->health.max_value;
            progress = MAX(0, MIN(progress, 1));
        }
        G_CopyString(queue[count].art, sizeof(queue[count].art), FindConfigValue(build_name, STR_ART));
        if (duration > 0) {
            DWORD elapsed = (DWORD)(duration * progress);
            queue[count].starttime = count == 0 && elapsed <= cursor ? cursor - elapsed : cursor;
            queue[count].endtime = queue[count].starttime + duration;
            cursor = queue[count].endtime;
        } else {
            queue[count].starttime = cursor;
            queue[count].endtime = cursor;
        }
        count++;
        if (build->build == build) {
            break;
        }
    }
    return count;
}
