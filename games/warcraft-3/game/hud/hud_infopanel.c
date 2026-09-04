/*
 * hud_infopanel.c — Info panel, multiselect, and per-frame update stubs.
 *
 * Builds the single-unit info panel (name, level, damage, armor, hero
 * attributes, XP bar, HP/mana), the multi-select grid, and the
 * build-queue overlay.  Also contains the stubbed entry points that
 * console_ui.c now handles client-side.
 */

#include "hud_local.h"

#define INVENTORY_CHARGE_FONT_SIZE 10

static int timed_status_debug_level(void) {
    LPCSTR value;

    value = gi.CvarString("wc3_timed_status_debug", "0");
    return value ? atoi(value) : 0;
}

static void timed_status_debug_dump(LPEDICT ent, LPGAMECLIENT viewer, LPCSTR stage) {
    int const debug = timed_status_debug_level();
    DWORD const now = G_Time();
    char unit_code[5] = { 0 };

    if (debug < 1 || !ent) return;
    memcpy(unit_code, &ent->class_id, 4);
    fprintf(stderr,
            "WC3_TIMED_STATUS server stage=%s unit=%u type=%s owner=%u viewer=%d now=%u\n",
            stage ? stage : "?", (unsigned)ent->s.number, unit_code,
            (unsigned)ent->s.player, viewer ? (int)viewer->ps.number : -1,
            (unsigned)now);
    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t const *status = ent->abilstatus + i;
        char code[5] = { 0 };
        LONG remaining;

        if (!status->level) continue;
        memcpy(code, &status->code, 4);
        remaining = status->timestamp > now ? (LONG)(status->timestamp - now) : 0;
        fprintf(stderr,
                "WC3_TIMED_STATUS server status slot=%u code=%s level=%u timestamp=%u duration_ms=%u remaining_ms=%ld eligible=%u fraction=%.4f\n",
                (unsigned)i, code, (unsigned)status->level,
                (unsigned)status->timestamp, (unsigned)status->duration_ms,
                (long)remaining, (unsigned)unit_statusshowstimedbar(status->code),
                unit_statusremainingfraction(status));
    }
}

static BOOL InfoPanelStringsResolved(void) {
    static LPCSTR const required[] = {
        "COLON_DAMAGE",
        "COLON_ARMOR",
        "COLON_FOOD",
        "COLON_FOOD_PROVIDED",
        "COLON_GOLD",
        "COLON_STRENGTH",
        "COLON_AGILITY",
        "COLON_INTELLECT",
        "COLON_STATUS",
    };

    FOR_LOOP(i, sizeof(required) / sizeof(required[0])) {
        LPCSTR const value = UI_GetString(required[i]);
        if (!value || !strcmp(value, required[i])) return false;
    }
    return true;
}

static void InitStatusWrapper(LPFRAMEDEF frame, FLOAT x, FLOAT y, FLOAT width, FLOAT height) {
    UI_InitFrame(frame, FT_SIMPLEFRAME);
    UI_SetSize(frame, width, height);
    UI_SetPoint(frame, FRAMEPOINT_TOPLEFT, hud.simple.SimpleInfoPanelUnitDetail,
                FRAMEPOINT_TOPLEFT, x, y);
}

void UI_LoadHudInfoPanel(void) {
    BOOL global_strings_loaded;
    BOOL infopanel_strings_loaded;

    if (hud.bottom.Type) return;

    /* FrameDef.toc registers both localized StringLists before any info-panel
     * frame definitions. Classic data splits the labels across them (for
     * example COLON_ARMOR is in GlobalStrings while COLON_DAMAGE and the Hero
     * attributes are in InfoPanelStrings), so load both before parsing frames
     * or unresolved IDs become baked into the cached templates. */
    global_strings_loaded = UI_EnsureFDF("UI\\FrameDef\\GlobalStrings.fdf");
    infopanel_strings_loaded = UI_EnsureFDF("UI\\FrameDef\\InfoPanelStrings.fdf");
    if (!global_strings_loaded) {
        fprintf(stderr, "UI_LoadHudInfoPanel: missing UI\\FrameDef\\GlobalStrings.fdf; cannot resolve info-panel string IDs\n");
    }
    if (!infopanel_strings_loaded && !InfoPanelStringsResolved()) {
        fprintf(stderr, "UI_LoadHudInfoPanel: missing UI\\FrameDef\\InfoPanelStrings.fdf and required info-panel strings are unresolved\n");
    }

    InfoPanelUnitDetail_Load(&hud.unit);
    InfoPanelBuildingDetail_Load(&hud.building);
    if (!global_strings_loaded || !InfoPanelStringsResolved() || !SimpleInfoPanel_Load(&hud.simple)) {
        fprintf(stderr, "UI_LoadHudInfoPanel: missing UI\\FrameDef\\UI\\SimpleInfoPanel.fdf status templates\n");
    } else {
        /* The retail SimpleInfoPanel FDF owns every icon/label/value offset.
         * The game only supplies the dynamic wrappers that WC3 repositions
         * according to attack count and Hero state. */
        hud.attack2_icon = UI_CloneFrameTree(hud.simple.SimpleInfoPanelIconDamage, NULL);
        if (hud.attack2_icon) {
            hud.attack2_icon_backdrop = UI_FindChildFrame(hud.attack2_icon, "InfoPanelIconBackdrop");
            hud.attack2_icon_level = UI_FindChildFrame(hud.attack2_icon, "InfoPanelIconLevel");
            hud.attack2_icon_label = UI_FindChildFrame(hud.attack2_icon, "InfoPanelIconLabel");
            hud.attack2_icon_value = UI_FindChildFrame(hud.attack2_icon, "InfoPanelIconValue");
        }
        if (!hud.attack2_icon || !hud.attack2_icon_backdrop || !hud.attack2_icon_level ||
            !hud.attack2_icon_label || !hud.attack2_icon_value) {
            fprintf(stderr, "UI_LoadHudInfoPanel: failed to clone SimpleInfoPanelIconDamage context 1\n");
            hud.simple.SimpleInfoPanelUnitDetail = NULL;
        }
    }

    UI_InitFrame(&hud.bottom, FT_SIMPLEFRAME);
    UI_SetSize(&hud.bottom, 0.180f, 0.120f);
    /* UI_SetPoint Y uses WC3 FDF convention: negative = downward from TOPLEFT.
     * UI_CopyFrameBase encodes the raw float; the client negates it on decode.
     * So to place the panel at top-origin y=0.480, pass -(0.480). */
    UI_SetPoint(&hud.bottom, FRAMEPOINT_TOPLEFT, NULL, FRAMEPOINT_TOPLEFT, 0.310f, -(UI_BASE_HEIGHT - 0.120f));

    if (hud.simple.SimpleInfoPanelUnitDetail) {
        InitStatusWrapper(&hud.attack1, 0.000f, -0.04000f, 0.100f, 0.030125f);
        InitStatusWrapper(&hud.attack2, 0.100f, -0.03925f, 0.100f, 0.030125f);
        InitStatusWrapper(&hud.armor,   0.000f, -0.07050f, 0.100f, 0.030125f);
        InitStatusWrapper(&hud.hero,    0.100f, -0.03700f, 0.100f, 0.062500f);
        InitStatusWrapper(&hud.food,    0.100f, -0.03925f, 0.100f, 0.030125f);
        InitStatusWrapper(&hud.gold,    0.100f, -0.03925f, 0.100f, 0.030125f);

        /* Bind the runtime-controlled status bars to the retail FDF geometry.
         * The FDF owns their anchors; Warsmash supplies only width, textures,
         * colour and the live progress value. */
        UI_SetSize(hud.simple.SimpleHeroLevelBar, 0.180f, hud.simple.SimpleHeroLevelBar->Height);
        UI_SetTexture(hud.simple.SimpleHeroLevelBar, "SimpleXpBarConsole", false);
        UI_SetTexture2(hud.simple.SimpleHeroLevelBar, "SimpleXpBarBorder", false);
        hud.simple.SimpleHeroLevelBar->Color = MAKE(COLOR32, 138, 0, 131, 255);
        UI_SetSize(hud.simple.SimpleProgressIndicator, 0.180f, hud.simple.SimpleProgressIndicator->Height);
        UI_SetTexture(hud.simple.SimpleProgressIndicator, "SimpleProgressBarConsole", false);
        UI_SetTexture2(hud.simple.SimpleProgressIndicator, "SimpleProgressBarBorder", false);
        hud.simple.SimpleProgressIndicator->Color = MAKE(COLOR32, 65, 130, 210, 255);
        UI_SetHidden(hud.simple.SimpleProgressIndicator, true);
        UI_SetSize(hud.simple.SimpleBuildTimeIndicator, 0.10538f, 0.0103f);
        UI_SetTexture(hud.simple.SimpleBuildTimeIndicator,
                      "SimpleBuildTimeIndicator", false);
        UI_SetTexture2(hud.simple.SimpleBuildTimeIndicator,
                       "SimpleBuildTimeIndicatorBorder", false);
        UI_SetSize(hud.simple.SimpleBuildQueueBackdrop, 0.180f, 0.090f);

        /* Warsmash's status strip is runtime-owned rather than defined by the
         * retail SimpleInfoPanel FDF.  Keep its geometry relative to the retail
         * unit-detail frame: label at BOTTOMLEFT + (0.03, 0.003), then 0.015
         * icons chained left-to-right with a 0.001 gap. */
        UI_InitFrame(&hud.buff_label, FT_STRING);
        snprintf(hud.buff_label.Name, sizeof(hud.buff_label.Name), "SmashBuffStatusBar");
        UI_SetSize(&hud.buff_label, 0.035f, 0.010f);
        UI_SetPoint(&hud.buff_label, FRAMEPOINT_BOTTOMLEFT, hud.simple.SimpleInfoPanelUnitDetail,
                    FRAMEPOINT_BOTTOMLEFT, 0.030f, 0.003f);
        hud.buff_label.Font.Size = 0.010f;
        hud.buff_label.Font.Index = gi.FontIndex(Theme_String("MasterFont", "Fonts\\FRIZQT__.TTF"), HUD_FONT_SIZE);
        hud.buff_label.Font.Justification.Horizontal = FONT_JUSTIFYLEFT;
        hud.buff_label.Font.Justification.Vertical = FONT_JUSTIFYMIDDLE;
        UI_SetText(&hud.buff_label, "%s", UI_GetString("COLON_STATUS"));

        FOR_LOOP(i, MAX_UNIT_STATUSES) {
            UI_InitFrame(&hud.buff_icon[i], FT_SIMPLEFRAME);
            snprintf(hud.buff_icon[i].Name, sizeof(hud.buff_icon[i].Name),
                     "SmashBuffStatusBarIcon%u", i);
            UI_SetSize(&hud.buff_icon[i], 0.015f, 0.015f);
            UI_SetPoint(&hud.buff_icon[i], FRAMEPOINT_LEFT,
                        i ? &hud.buff_icon[i - 1] : &hud.buff_label,
                        FRAMEPOINT_RIGHT, 0.001f, 0.0f);

            UI_InitFrame(&hud.buff_tex[i], FT_TEXTURE);
            snprintf(hud.buff_tex[i].Name, sizeof(hud.buff_tex[i].Name),
                     "SmashBuffStatusBarIcon%uTexture", i);
            UI_SetParent(&hud.buff_tex[i], &hud.buff_icon[i]);
            UI_SetAllPoints(&hud.buff_tex[i]);
        }
    }
}

static void HideLegacyUnitStats(void) {
    LPFRAMEDEF const frames_to_hide[] = {
        hud.unit.DefenseLabel, hud.unit.DefenseValue,
        hud.unit.AttackLabel1, hud.unit.AttackValue1,
        hud.unit.AttackLabel2, hud.unit.AttackValue2,
        hud.unit.SpeedTitle, hud.unit.SpeedValue,
        hud.unit.RangeTitle1, hud.unit.RangeValue1,
        hud.unit.RangeTitle2, hud.unit.RangeValue2,
        hud.unit.IconBackdrop1, hud.unit.IconValue1,
        hud.unit.IconBackdrop2, hud.unit.IconValue2,
        hud.unit.IconBackdrop3, hud.unit.IconValue3,
        hud.unit.IconBackdrop4, hud.unit.IconValue4,
    };
    FOR_LOOP(i, sizeof(frames_to_hide) / sizeof(frames_to_hide[0]))
        UI_SetHidden(frames_to_hide[i], true);
}

static void FormatAttackDamageValue(char *buffer, size_t buffer_size,
                                    LONG min_damage, LONG max_damage, FLOAT temporary_bonus) {
    LONG const bonus = (LONG)temporary_bonus;
    if (bonus > 0) {
        snprintf(buffer, buffer_size, "%ld - %ld |cff00ff00+%ld|r",
                 (long)min_damage, (long)max_damage, (long)bonus);
    } else if (bonus < 0) {
        snprintf(buffer, buffer_size, "%ld - %ld |cffff0000%ld|r",
                 (long)min_damage, (long)max_damage, (long)bonus);
    } else {
        snprintf(buffer, buffer_size, "%ld - %ld", (long)min_damage, (long)max_damage);
    }
}

static void WriteLegacyUnitStats(LPEDICT ent, UnitWeapons_t const *weapons,
                                 BOOL has_attack2, LONG min_damage, LONG max_damage,
                                 LONG min_damage2, LONG max_damage2, BOOL is_hero,
                                 DWORD level) {
    char buffer[128];

    UI_SetText(hud.unit.AttackLabel1, "Damage:");
    FormatAttackDamageValue(buffer, sizeof(buffer), min_damage, max_damage,
                            ent->attack1.temporaryDamageBonus);
    UI_SetText(hud.unit.AttackValue1, "%s", buffer);
    UI_SetText(hud.unit.AttackLabel2, "Damage:");
    FormatAttackDamageValue(buffer, sizeof(buffer), min_damage2, max_damage2,
                            ent->attack2.temporaryDamageBonus);
    UI_SetText(hud.unit.AttackValue2, "%s", buffer);
    UI_SetHidden(hud.unit.AttackLabel2, !has_attack2);
    UI_SetHidden(hud.unit.AttackValue2, !has_attack2);
    UI_SetText(hud.unit.DefenseLabel, "Armor:");
    UI_SetText(hud.unit.DefenseValue, "%d", (int)(G_UnitArmorValue(ent) + 0.5f));
    UI_SetText(hud.unit.SpeedTitle, "Speed:");
    UI_SetText(hud.unit.SpeedValue, "%d", (int)(ent->unitinfo.MoveSpeed + 0.5f));
    UI_SetText(hud.unit.RangeTitle1, "Range:");
    UI_SetText(hud.unit.RangeValue1, "%d", (int)(ent->attack1.range + 0.5f));
    UI_SetText(hud.unit.RangeTitle2, "Range:");
    UI_SetText(hud.unit.RangeValue2, "%d", (int)(ent->attack2.range + 0.5f));
    UI_SetHidden(hud.unit.RangeTitle2, !has_attack2);
    UI_SetHidden(hud.unit.RangeValue2, !has_attack2);

    if (is_hero) {
        LPCSTR const prim = ent->data.UnitBalance->primaryAttribute;
        struct { LPCSTR code; DWORD val; } attrs[3] = {
            { "STR", ent->hero.str }, { "AGI", ent->hero.agi }, { "INT", ent->hero.intel },
        };
        LPFRAMEDEF icon_values[3] = {
            hud.unit.IconValue1, hud.unit.IconValue2, hud.unit.IconValue3,
        };

        FOR_LOOP(a, 3) {
            BOOL const isprim = prim && !strcmp(prim, attrs[a].code);
            UI_SetText(icon_values[a], "%lu", (unsigned long)attrs[a].val);
            icon_values[a]->Font.Color = isprim ? MAKE(COLOR32, 120, 230, 120, 255) : COLOR32_WHITE;
        }

        DWORD const need = G_HeroXPForLevel(level + 1);
        DWORD const have = G_HeroXPForLevel(level);
        if (need > have) {
            snprintf(buffer, sizeof(buffer), "XP: %lu / %lu",
                     (unsigned long)(ent->hero.xp - (ent->hero.xp < have ? ent->hero.xp : have)),
                     (unsigned long)(need - have));
            UI_SetText(hud.unit.IconValue4, "%s", buffer);
            hud.unit.IconValue4->Font.Color = MAKE(COLOR32, 200, 200, 200, 255);
        }
    }
}

static BOOL InfoPanelTextureExists(LPCSTR path) {
    DWORD size = 0;
    HANDLE data;

    if (!path || !*path) return false;
    path = UI_ResolveTextureAlias(path);
    data = gi.ReadFile(path, &size);
    if (!data) return false;
    gi.MemFree(data);
    return true;
}

static int InfoPanelIconTypeIndex(LPCSTR prefix, LPCSTR type, LPCSTR *normalized) {
    static LPCSTR const damage_types[] = {
        "Unknown", "Normal", "Pierce", "Siege", "Spells", "Chaos", "Magic", "Hero",
    };
    static LPCSTR const armor_types[] = {
        "Small", "Medium", "Large", "Fort", "Normal", "Hero", "Divine", "None",
    };
    LPCSTR const *types;
    LPCSTR fallback;

    if (!strcasecmp(prefix, "Armor")) {
        types = armor_types;
        fallback = armor_types[0];
        if (type && !strcasecmp(type, "heavy")) type = "Large";
    } else {
        types = damage_types;
        fallback = damage_types[0];
        if (type && !strcasecmp(type, "seige")) type = "Siege";
    }
    if (type && *type) {
        FOR_LOOP(i, 8) {
            if (!strcasecmp(type, types[i])) {
                if (normalized) *normalized = types[i];
                return (int)i;
            }
        }
    }
    if (normalized) *normalized = fallback;
    return 0;
}

static LPCSTR InfoPanelThemeIcon(LPCSTR prefix, LPCSTR type, BOOL has_upgrade) {
    char key[96];
    LPCSTR texture;

    UI_InfoPanelIconSkinKey(prefix, type, has_upgrade, key, sizeof(key));
    texture = Theme_String(key, NULL);
    if ((!texture || !*texture) && !strcasecmp(prefix, "Damage") &&
        !strcasecmp(type, "Spells")) {
        /* Warsmash only aliases Spells to Magic when the authored Spells skin
         * field is absent; a custom skin is allowed to provide Spells art. */
        UI_InfoPanelIconSkinKey(prefix, "Magic", has_upgrade, key, sizeof(key));
        texture = Theme_String(key, NULL);
    }
    return texture;
}

static LPCSTR ResolveTypedInfoPanelIcon(LPCSTR prefix, LPCSTR type, BOOL has_upgrade) {
    LPCSTR normalized;
    LPCSTR texture, fallback;
    int const family = !strcasecmp(prefix, "Armor") ? 1 : 0;
    int const type_index = InfoPanelIconTypeIndex(prefix, type, &normalized);
    int const upgrade_index = has_upgrade ? 1 : 0;
    infoPanelIconCache_t *cache = &hud.icon_cache[family][type_index][upgrade_index];

    if (cache->resolved) return cache->texture[0] ? cache->texture : NULL;
    cache->resolved = true;

    texture = InfoPanelThemeIcon(prefix, normalized, has_upgrade);
    if (texture && *texture && InfoPanelTextureExists(texture)) {
        G_CopyString(cache->texture, sizeof(cache->texture), texture);
        return cache->texture;
    }

    if (!has_upgrade) {
        /* InfoPanelIconBackdrops in Warsmash tries the Neutral texture first,
         * then retries the same attack/defense type from the normal family if
         * the Neutral asset fails to load (not merely when its skin key is
         * absent). This is especially important for stock HeroNeutral entries
         * whose path may be unavailable in a particular archive set. */
        fallback = InfoPanelThemeIcon(prefix, normalized, true);
        if (fallback && *fallback && InfoPanelTextureExists(fallback)) {
            fprintf(stderr, "WC3 info panel: %s %s Neutral icon '%s' unavailable; using '%s'\n",
                    prefix, normalized, texture && *texture ? texture : "<missing skin field>", fallback);
            G_CopyString(cache->texture, sizeof(cache->texture), fallback);
            return cache->texture;
        }
    }

    fprintf(stderr, "WC3 info panel: missing %s %s%s icon '%s'\n", prefix, normalized,
            has_upgrade ? "" : " Neutral", texture && *texture ? texture : "<missing skin field>");
    cache->texture[0] = '\0';
    return NULL;
}

#ifdef BZ_TESTS
void UI_TestResetInfoPanelIconCache(void) { memset(hud.icon_cache, 0, sizeof(hud.icon_cache)); }
LPCSTR UI_TestResolveTypedInfoPanelIcon(LPCSTR prefix, LPCSTR type, BOOL has_upgrade) {
    return ResolveTypedInfoPanelIcon(prefix, type, has_upgrade);
}
#endif

static void SetTypedInfoPanelIcon(LPFRAMEDEF frame, LPCSTR prefix, LPCSTR type, BOOL has_upgrade) {
    LPCSTR texture;

    if (!frame || !prefix) return;
    texture = ResolveTypedInfoPanelIcon(prefix, type, has_upgrade);
    if (!texture) {
        /* Warsmash's backdrop table stores NULL when neither candidate loads.
         * Clear the frame instead of retaining artwork from the last unit. */
        frame->Texture.Image = 0;
        return;
    }
    UI_SetTexture(frame, texture, false);
}

static void SetHeroPrimaryAttributeIcon(LPCSTR primary) {
    LPCSTR key = "InfoPanelIconHeroIconSTR";
    LPCSTR texture;

    if (!hud.simple.InfoPanelIconHeroIcon) return;
    if (primary && !strcmp(primary, "AGI")) key = "InfoPanelIconHeroIconAGI";
    else if (primary && !strcmp(primary, "INT")) key = "InfoPanelIconHeroIconINT";
    texture = Theme_String(key, NULL);
    if (!texture || !*texture) {
        fprintf(stderr, "SetHeroPrimaryAttributeIcon: missing war3skins key %s\n", key);
        return;
    }
    UI_SetTexture(hud.simple.InfoPanelIconHeroIcon, texture, false);
}

static void RefreshSimpleInfoPanelStrings(void) {
    if (!hud.simple.SimpleInfoPanelUnitDetail) return;
    UI_SetText(hud.simple.InfoPanelIconLabel, "%s", UI_GetString("COLON_DAMAGE"));
    if (hud.attack2_icon_label) UI_SetText(hud.attack2_icon_label, "%s", UI_GetString("COLON_DAMAGE"));
    UI_SetText(hud.simple.InfoPanelIconLabel_2, "%s", UI_GetString("COLON_ARMOR"));
    UI_SetText(hud.simple.InfoPanelIconLabel_4, "%s", UI_GetString("COLON_FOOD_PROVIDED"));
    UI_SetText(hud.simple.InfoPanelIconLabel_5, "%s", UI_GetString("COLON_GOLD"));
    UI_SetText(hud.simple.InfoPanelIconHeroStrengthLabel, "%s", UI_GetString("COLON_STRENGTH"));
    UI_SetText(hud.simple.InfoPanelIconHeroAgilityLabel, "%s", UI_GetString("COLON_AGILITY"));
    UI_SetText(hud.simple.InfoPanelIconHeroIntellectLabel, "%s", UI_GetString("COLON_INTELLECT"));
    UI_SetText(&hud.buff_label, "%s", UI_GetString("COLON_STATUS"));
}

static DWORD RawcodeFromListToken(LPCSTR text) {
    char rawcode[5] = { 0 };
    DWORD length = 0;

    if (!text) return 0;
    while (*text && (isspace((unsigned char)*text) || *text == ',' || *text == ';')) text++;
    while (text[length] && text[length] != ',' && text[length] != ';' &&
           !isspace((unsigned char)text[length]) && length < 4) {
        rawcode[length] = text[length];
        length++;
    }
    return length == 4 ? FS_SLKKey(rawcode) : 0;
}

static DWORD UnitWeaponUpgrade(LPEDICT ent) {
    static LPCSTR const classes[] = { "melee", "ranged", "artillery" };

    if (!ent || !ent->data.UnitBalance) return 0;
    FOR_LOOP(i, sizeof(classes) / sizeof(classes[0])) {
        DWORD const upgrade = G_GetUnitUpgradeForClass(ent, classes[i]);
        if (upgrade) return upgrade;
    }
    return 0;
}

static DWORD UnitArmorUpgrade(LPEDICT ent) {
    if (!ent || !ent->data.UnitBalance) return 0;
    return G_GetUnitUpgradeForClass(ent, "armor");
}

static void SetUpgradeLevel(LPFRAMEDEF frame, DWORD upgrade, LPEDICT ent) {
    LPGAMECLIENT owner;

    if (!frame) return;
    if (!upgrade || !ent || !(owner = G_GetPlayerClientByNumber(ent->s.player))) {
        UI_SetText(frame, "%s", "");
        UI_SetHidden(frame, true);
        return;
    }
    UI_SetHidden(frame, false);
    UI_SetText(frame, "%ld", (long)G_GetPlayerTechResearchedLevel(owner, upgrade));
}

static DWORD StatusBuffCode(heroabilitystatus_t const *status) {
    AbilityData_t const *ability;
    DWORD level;
    DWORD buff;

    if (!status || !status->level) return 0;
    if ((status->code & 0xff) == 'B') return status->code;
    /* abilstatus[] also stores active spell cooldowns as Axxx records.  Those
     * have an expiry timestamp and are command-card state, not visible buffs.
     * Persistent Axxx status records (for example Devotion Aura on its caster)
     * have timestamp == 0 and may resolve through AbilityData.BuffID*. */
    if (status->timestamp) return 0;
    ability = G_AbilityData(status->code);
    if (!ability || !ability->id) return 0;
    level = MIN(MAX(status->level, 1u), 4u) - 1u;
    buff = RawcodeFromListToken(ability->buffID[level]);
    if (!buff && level != 0) buff = RawcodeFromListToken(ability->buffID[0]);
    return buff;
}

static LPCSTR StatusBuffField(DWORD code, LPCSTR field) {
    char name[5] = { 0 };
    AbilityBuffData_t const *buff;
    LPCSTR value;

    memcpy(name, &code, 4);
    value = FindConfigValue(name, field);
    if (value && *value) return value;

    buff = G_AbilityBuffData(code);
    if (!buff || !buff->id) return NULL;
    if (!strcmp(field, "Buffart")) return buff->buffArt;
    if (!strcmp(field, "Bufftip")) return buff->buffTip;
    if (!strcmp(field, "Buffubertip")) return buff->buffUberTip;
    return NULL;
}

static LPCSTR TimedStatusLabel(heroabilitystatus_t const *status) {
    DWORD buff_code;
    LPCSTR tip;

    if (!status || !unit_statusshowstimedbar(status->code)) return NULL;
    buff_code = StatusBuffCode(status);
    if (!buff_code) return NULL;
    tip = StatusBuffField(buff_code, "Bufftip");
    if (!tip || !*tip) return "";
    return UI_GetString(tip);
}

static LPCSTR StatusBuffArt(DWORD code) {
    LPCSTR art;

    /* Warsmash routes timed-life-bar buffs through SimpleProgressIndicator
     * instead of the ordinary status icon strip. Cooldown markers share
     * abilstatus[] but have ability rawcodes and do not resolve here. */
    if (unit_statusshowstimedbar(code)) return NULL;
    art = StatusBuffField(code, "Buffart");
    if (!art || !*art) return NULL;
    return art;
}

static void WriteBuffStatusFrames(LPEDICT ent) {
    DWORD slot = 0;
    DWORD shown[MAX_UNIT_STATUSES] = { 0 };

    if (!ent || !hud.simple.SimpleInfoPanelUnitDetail) return;
    UI_SetText(&hud.buff_label, "%s", UI_GetString("COLON_STATUS"));
    UI_WriteFrame(&hud.buff_label);

    FOR_LOOP(i, MAX_UNIT_STATUSES) {
        heroabilitystatus_t const *status = ent->abilstatus + i;
        LPCSTR art;
        LPCSTR tip;
        LPCSTR ubertip;
        LPFRAMEDEF icon;
        DWORD const buff_code = StatusBuffCode(status);

        if (!status->level || slot >= MAX_UNIT_STATUSES || !buff_code) continue;
        /* Auras may be represented by both their ability rawcode (AHad) and
         * their buff rawcode (BHad) in abilstatus[].  Resolve first, then emit
         * each visible buff once. */
        {
            BOOL duplicate = false;
            FOR_LOOP(j, slot) {
                if (shown[j] == buff_code) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) continue;
        }
        art = StatusBuffArt(buff_code);
        if (!art) {
            if (!unit_statusshowstimedbar(buff_code)) {
                char rawcode[5] = { 0 };
                memcpy(rawcode, &buff_code, 4);
                fprintf(stderr, "WriteBuffStatusFrames: missing Buffart for status %s\n", rawcode);
            }
            continue;
        }

        icon = &hud.buff_icon[slot];
        UI_SetTexture(&hud.buff_tex[slot], art, false);
        tip = StatusBuffField(buff_code, "Bufftip");
        ubertip = StatusBuffField(buff_code, "Buffubertip");
        icon->Tip = tip && *tip ? UI_GetString(tip) : NULL;
        icon->Ubertip = ubertip && *ubertip ? UI_GetString(ubertip) : NULL;
        UI_WriteFrame(icon);
        UI_WriteFrame(&hud.buff_tex[slot]);
        shown[slot] = buff_code;
        slot++;
    }
}

static void WriteSelectedUnitStatusFrames(LPEDICT ent, UnitWeapons_t const *weapons,
                                          BOOL has_attack1, BOOL has_attack2,
                                          LONG min_damage, LONG max_damage,
                                          LONG min_damage2, LONG max_damage2,
                                          BOOL is_hero) {
    char value[64];
    DWORD const weapon_upgrade = UnitWeaponUpgrade(ent);
    DWORD const armor_upgrade = UnitArmorUpgrade(ent);

    if (!hud.simple.SimpleInfoPanelUnitDetail) return;
    RefreshSimpleInfoPanelStrings();

    UI_SetPoint(&hud.armor, FRAMEPOINT_TOPLEFT, hud.simple.SimpleInfoPanelUnitDetail,
                FRAMEPOINT_TOPLEFT, 0.0f, has_attack1 ? -0.0705f : -0.0400f);

    if (has_attack1) {
        SetTypedInfoPanelIcon(hud.simple.InfoPanelIconBackdrop, "Damage", weapons->attack1.attackType,
                              weapon_upgrade != 0);
        FormatAttackDamageValue(value, sizeof(value), min_damage, max_damage,
                                ent->attack1.temporaryDamageBonus);
        UI_SetText(hud.simple.InfoPanelIconValue, "%s", value);
        SetUpgradeLevel(hud.simple.InfoPanelIconLevel, weapon_upgrade, ent);
        UI_WriteFrame(&hud.attack1);
        UI_WriteFrameWithChildren(hud.simple.SimpleInfoPanelIconDamage, &hud.attack1);
    }
    if (has_attack2) {
        SetTypedInfoPanelIcon(hud.attack2_icon_backdrop, "Damage", weapons->attack2.attackType,
                              weapon_upgrade != 0);
        FormatAttackDamageValue(value, sizeof(value), min_damage2, max_damage2,
                                ent->attack2.temporaryDamageBonus);
        UI_SetText(hud.attack2_icon_value, "%s", value);
        SetUpgradeLevel(hud.attack2_icon_level, weapon_upgrade, ent);
        UI_WriteFrame(&hud.attack2);
        UI_WriteFrameWithChildren(hud.attack2_icon, &hud.attack2);
    }

    SetTypedInfoPanelIcon(hud.simple.InfoPanelIconBackdrop_2, "Armor", ent->data.UnitBalance->defenseType,
                          armor_upgrade != 0);
    UI_SetText(hud.simple.InfoPanelIconValue_2, "%d", (int)(G_UnitArmorValue(ent) + 0.5f));
    SetUpgradeLevel(hud.simple.InfoPanelIconLevel_2, armor_upgrade, ent);
    UI_WriteFrame(&hud.armor);
    UI_WriteFrameWithChildren(hud.simple.SimpleInfoPanelIconArmor, &hud.armor);

    if (ent->resources > 0) {
        LPCSTR const gold_art = "InfoPanelIconGold";
        if (!gold_art || !*gold_art) {
            fprintf(stderr, "WriteSelectedUnitStatusFrames: missing war3skins InfoPanelIconGold\n");
        } else {
            UI_SetTexture(hud.simple.InfoPanelIconBackdrop_5, gold_art, false);
        }
        UI_SetText(hud.simple.InfoPanelIconLevel_5, "%s", "");
        UI_SetHidden(hud.simple.InfoPanelIconLevel_5, true);
        UI_SetText(hud.simple.InfoPanelIconValue_5, "%u", (unsigned)ent->resources);
        UI_WriteFrame(&hud.gold);
        UI_WriteFrameWithChildren(hud.simple.SimpleInfoPanelIconGold, &hud.gold);
    } else if (ent->data.UnitBalance->foodMade > 0) {
        LPCSTR const food_art = "InfoPanelIconFood";
        if (!food_art || !*food_art) {
            fprintf(stderr, "WriteSelectedUnitStatusFrames: missing war3skins InfoPanelIconFood\n");
        } else {
            UI_SetTexture(hud.simple.InfoPanelIconBackdrop_4, food_art, false);
        }
        UI_SetText(hud.simple.InfoPanelIconLevel_4, "%s", "");
        UI_SetHidden(hud.simple.InfoPanelIconLevel_4, true);
        UI_SetText(hud.simple.InfoPanelIconValue_4, "%ld", (long)ent->data.UnitBalance->foodMade);
        UI_WriteFrame(&hud.food);
        UI_WriteFrameWithChildren(hud.simple.SimpleInfoPanelIconFood, &hud.food);
    }

    if (is_hero) {
        SetHeroPrimaryAttributeIcon(ent->data.UnitBalance->primaryAttribute);
        UI_SetText(hud.simple.InfoPanelIconHeroStrengthValue, "%lu", (unsigned long)ent->hero.str);
        UI_SetText(hud.simple.InfoPanelIconHeroAgilityValue, "%lu", (unsigned long)ent->hero.agi);
        UI_SetText(hud.simple.InfoPanelIconHeroIntellectValue, "%lu", (unsigned long)ent->hero.intel);
        UI_WriteFrame(&hud.hero);
        UI_WriteFrameWithChildren(hud.simple.SimpleInfoPanelIconHero, &hud.hero);
    }

    WriteBuffStatusFrames(ent);
}

static FLOAT HeroLevelProgress(LPEDICT ent) {
    DWORD level;
    DWORD have;
    DWORD need;

    if (!ent) return 0.0f;
    level = MAX(1u, ent->hero.level);
    have = G_HeroXPForLevel(level);
    need = G_HeroXPForLevel(level + 1);
    if (need <= have) return 1.0f;
    if (ent->hero.xp <= have) return 0.0f;
    return MIN(1.0f, (FLOAT)(ent->hero.xp - have) / (FLOAT)(need - have));
}

static void WriteSimpleUnitHeader(LPEDICT ent, LPCSTR display_name, BOOL is_hero, LPGAMECLIENT viewer) {
    char class_text[128];
    heroabilitystatus_t const *timed_status = NULL;
    LPCSTR timed_label = NULL;
    LPCSTR unit_name;
    LPCSTR class_format;
    BOOL old_hero_hidden;

    if (!hud.simple.SimpleInfoPanelUnitDetail) return;
    UI_SetText(hud.simple.SimpleNameValue, "%s", display_name ? display_name : "");
    unit_name = G_UnitProfile(ent->class_id)->name;
    if (!unit_name || !*unit_name) unit_name = GetClassName(ent->class_id);

    /* Warsmash shows this timer only for a single unit owned by the local
     * player. UI_SendInfoPanel already guarantees single-selection here; keep
     * ownership explicit rather than leaking an enemy/allied timer. */
    if (viewer && ent->s.player == viewer->ps.number) {
        timed_status = unit_findtimedbarstatus(ent);
        timed_label = TimedStatusLabel(timed_status);
    }

    if (timed_status_debug_level() >= 1) {
        char code[5] = { 0 };
        timed_status_debug_dump(ent, viewer, "write_header");
        if (timed_status) memcpy(code, &timed_status->code, 4);
        fprintf(stderr,
                "WC3_TIMED_STATUS server header unit=%u owned=%u selected_status=%s label=\"%s\" frame=%s type=%u hidden=%u parent=%s size=(%.4f,%.4f) texture=%u border=%u\n",
                (unsigned)ent->s.number,
                (unsigned)(viewer && ent->s.player == viewer->ps.number),
                timed_status ? code : "<none>", timed_label ? timed_label : "<null>",
                hud.simple.SimpleProgressIndicator->Name,
                (unsigned)hud.simple.SimpleProgressIndicator->Type,
                (unsigned)hud.simple.SimpleProgressIndicator->hidden,
                hud.simple.SimpleProgressIndicator->Parent ? hud.simple.SimpleProgressIndicator->Parent->Name : "<none>",
                hud.simple.SimpleProgressIndicator->Width,
                hud.simple.SimpleProgressIndicator->Height,
                (unsigned)hud.simple.SimpleProgressIndicator->Texture.Image,
                (unsigned)hud.simple.SimpleProgressIndicator->Texture.Image2);
    }

    if (timed_status) {
        UI_SetText(hud.simple.SimpleClassValue, "%s", timed_label ? timed_label : "");
        UI_SetHidden(hud.simple.SimpleClassValue, false);
        hud.simple.SimpleProgressIndicator->Stat = UI_STAT_SELECTION_TIMED_STATUS;
        UI_SetHidden(hud.simple.SimpleProgressIndicator, false);
    } else {
        if (is_hero) {
            class_format = UI_GetString("INFOPANEL_LEVEL_CLASS");
            snprintf(class_text, sizeof(class_text), class_format,
                     (unsigned)MAX(1u, ent->hero.level), unit_name);
            UI_SetText(hud.simple.SimpleClassValue, "%s", class_text);
            UI_SetHidden(hud.simple.SimpleClassValue, false);
        } else {
            UI_SetText(hud.simple.SimpleClassValue, "%s", "");
            UI_SetHidden(hud.simple.SimpleClassValue, true);
        }
        hud.simple.SimpleProgressIndicator->Stat = 0;
        UI_SetHidden(hud.simple.SimpleProgressIndicator, true);
    }

    if (timed_status_debug_level() >= 1) {
        fprintf(stderr,
                "WC3_TIMED_STATUS server frame_ready unit=%u status=%u hidden=%u stat=%u label_hidden=%u\n",
                (unsigned)ent->s.number, (unsigned)(timed_status != NULL),
                (unsigned)hud.simple.SimpleProgressIndicator->hidden,
                (unsigned)hud.simple.SimpleProgressIndicator->Stat,
                (unsigned)hud.simple.SimpleClassValue->hidden);
    }

    old_hero_hidden = hud.simple.SimpleHeroLevelBar->hidden;
    UI_SetHidden(hud.simple.SimpleHeroLevelBar, true);
    UI_WriteFrame(&hud.bottom);
    UI_WriteFrameWithChildren(hud.simple.SimpleInfoPanelUnitDetail, &hud.bottom);
    UI_SetHidden(hud.simple.SimpleHeroLevelBar, old_hero_hidden);

    if (is_hero) {
        UI_SetHidden(hud.simple.SimpleHeroLevelBar, false);
        UI_WriteFrameValue(hud.simple.SimpleHeroLevelBar, HeroLevelProgress(ent));
    } else {
        UI_SetHidden(hud.simple.SimpleHeroLevelBar, true);
    }
}

DWORD UI_WriteBuildingQueueShell(LPEDICT ent, LPCSTR action_key) {
    LPCSTR name;

    if (!ent) return 0;
    if (!hud.simple.SimpleInfoPanelUnitDetail) return 0;

    name = G_UnitProfile(ent->class_id)->name;
    if (!name || !*name) name = GetClassName(ent->class_id);
    UI_SetText(hud.simple.SimpleBuildingNameValue, "%s", name);
    UI_SetText(hud.simple.SimpleBuildingDescriptionValue, "%s", "");
    UI_SetHidden(hud.simple.SimpleBuildingDescriptionValue, true);
    UI_SetText(hud.simple.SimpleBuildingActionLabel, "%s", UI_GetString(action_key ? action_key : "TRAINING"));
    UI_SetHidden(hud.simple.SimpleBuildTimeIndicator, false);
    UI_SetHidden(hud.simple.SimpleBuildQueueBackdrop, false);

    UI_WriteFrame(&hud.bottom);
    UI_WriteFrameWithChildren(hud.simple.SimpleInfoPanelBuildingDetail, &hud.bottom);
    return UI_GetWrittenFrameNumber(hud.simple.SimpleBuildTimeIndicator);
}

void UI_WriteSingleInfo(LPEDICT ent, LPGAMECLIENT viewer) {
    UnitBalance_t const *balance = ent->data.UnitBalance;
    UnitWeapons_t const *weapons = ent->data.UnitWeapons;
    LPCSTR name = G_UnitProfile(ent->class_id)->properNames;
    LPCSTR unit_name = G_UnitProfile(ent->class_id)->name;
    BOOL const is_hero = balance->strength > 0 || balance->agility > 0 || balance->intelligence > 0;
    DWORD level = is_hero && ent->hero.level > 0 ? ent->hero.level
                                                 : MAX(1, balance->level);
    LONG dice = ent->attack1.numberOfDice;
    BOOL has_attack1 = dice > 0;
    LONG min_damage = has_attack1 ? MAX(0, (LONG)(ent->attack1.damageBase + dice)) : 0;
    LONG max_damage = has_attack1 ? MAX(0, (LONG)(ent->attack1.damageBase + dice * ent->attack1.sidesPerDie)) : 0;
    LONG dice2 = ent->attack2.numberOfDice;
    BOOL has_attack2 = UI_HasSecondAttack(weapons) && dice2 > 0;
    LONG min_damage2 = has_attack2 ? MAX(0, (LONG)(ent->attack2.damageBase + dice2)) : 0;
    LONG max_damage2 = has_attack2 ? MAX(0, (LONG)(ent->attack2.damageBase + dice2 * ent->attack2.sidesPerDie)) : 0;

    if (!unit_name || !*unit_name) unit_name = GetClassName(ent->class_id);
    if (!name || !*name) name = unit_name;

    if (hud.simple.SimpleInfoPanelUnitDetail) {
        /* SimpleNameValue owns the Warcraft title font/anchors. Ordinary units
         * have no synthetic "Level N <type>" line; Heroes use the XP bar in
         * that slot instead of a duplicate level/class label. */
        HideLegacyUnitStats();
        WriteSimpleUnitHeader(ent, is_hero ? name : unit_name, is_hero, viewer);
    } else {
        char buffer[128];
        UI_SetText(hud.unit.NameValue, "%s", name);
        snprintf(buffer, sizeof(buffer), "Level %lu %s", (unsigned long)level, unit_name ? unit_name : "");
        UI_SetText(hud.unit.ClassValue, "%s", buffer);
        WriteLegacyUnitStats(ent, weapons, has_attack2, min_damage, max_damage,
                             min_damage2, max_damage2, is_hero, level);
        UI_WriteFrame(&hud.bottom);
        UI_WriteFrameWithChildren(hud.unit.InfoPanelUnitDetail, &hud.bottom);
    }

    WriteSelectedUnitStatusFrames(ent, weapons, has_attack1, has_attack2,
                                  min_damage, max_damage, min_damage2, max_damage2,
                                  is_hero);
}

void UI_WriteMultiselect(LPEDICT *ents, DWORD count) {
    if (count > 12) count = 12;
    DWORD size = sizeof(uiMultiselect_t) + sizeof(uiMultiselectItem_t) * count;
    LPBYTE buffer = gi.MemAlloc(size);
    uiMultiselect_t *multi = (uiMultiselect_t *)buffer;
    uiFrame_t frame;

    memset(buffer, 0, size);
    multi->hp_bar = gi.ImageIndex("SimpleHpBarConsole");
    multi->mana_bar = gi.ImageIndex("SimpleManaBarConsole");
    multi->offset = MAKE(VECTOR2, 0.031f, 0.050f);
    multi->numcolumns = 6;
    multi->numitems = count;
    FOR_LOOP(i, count) {
        multi->items[i].entity = ents[i]->s.number;
        multi->items[i].image = gi.ImageIndex(FindConfigValue(GetClassName(ents[i]->class_id), STR_ART));
    }

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_MULTISELECT;
    frame.color = COLOR32_WHITE;
    UI_SetFrameRect(&frame, 0.314f, 0.500f, 0.025f, 0.025f);
    UI_WriteProxyFrame(&frame, buffer, size);
    gi.MemFree(buffer);
}

static BOOL UI_UsesBuildingQueuePanel(LPGAMECLIENT viewer, LPEDICT unit) {
    if (!viewer || !unit || !unit->data.UnitBalance || !unit->data.UnitBalance->isBuilding)
        return false;
    if (!G_UnitCanControl(viewer, unit))
        return false;
    return unit->construction.active || unit->build != NULL;
}

void UI_SeedInfoPanelCache(LPEDICT ent, LPEDICT *selected, DWORD count) {
    if (!ent->client) return;
    if (count == 1 && !UI_UsesBuildingQueuePanel(ent->client, selected[0])) {
        ent->client->infopanel.entity = selected[0]->s.number;
        ent->client->infopanel.hp = (LONG)(selected[0]->health.value + 0.5f);
        ent->client->infopanel.mana = (LONG)(selected[0]->mana.value + 0.5f);
        ent->client->infopanel.xp = (LONG)selected[0]->hero.xp;
    } else {
        ent->client->infopanel.entity = 0;
    }
}

void UI_SendInfoPanel(LPEDICT ent, LPEDICT *selected, DWORD count) {
    UI_WriteStart(LAYER_INFOPANEL);
    if (count == 1) {
        if (UI_UsesBuildingQueuePanel(ent->client, selected[0])) {
            UI_WriteBuildQueue(selected[0]);
        } else {
            UI_WriteSingleInfo(selected[0], ent->client);
        }
    } else if (count > 1) {
        UI_WriteMultiselect(selected, count);
    }
    UI_WriteEnd(ent);
    UI_SeedInfoPanelCache(ent, selected, count);
}

static DWORD SelectedUnits(LPGAMECLIENT client, LPEDICT *out, DWORD max_out) {
    DWORD count = 0;
    FOR_SELECTED_UNITS(client, ent) {
        if (count < max_out) out[count] = ent;
        count++;
    }
    return MIN(count, max_out);
}

void Get_Commands_f(LPEDICT ent) {
    LPEDICT selected = ent && ent->client ? G_GetMainSelectedUnit(ent->client) : NULL;
    LPGAMECLIENT previous_ui_client;
    gameCommandButton_t buttons[12];
    BYTE count;

    if (!ent || !ent->client) return;
    G_UpdateRallyIndicator(ent->client);
    ent->client->commands_dirty = false;
    memset(&ent->client->menu, 0, sizeof(ent->client->menu));
    if (!selected || !G_UnitCanControl(ent->client, selected)) {
        UI_ClearLayer(ent, LAYER_COMMANDBAR);
        return;
    }

    /* Command tooltip formatting is player-sensitive for research because the
     * next upgrade level determines gold/lumber cost. Keep the same current-
     * client contract used by UI_WRITE_LAYER while this manually-authored
     * command-bar layer is serialized. */
    previous_ui_client = ui_current_client;
    UI_SetCurrentClient(ent->client);
    UI_WriteStart(LAYER_COMMANDBAR);
    count = G_GetCommandButtons(selected, buttons, 12);
    FOR_LOOP(i, count) {
        UI_WriteCommandButtonFrame(&buttons[i]);
    }
    if (count) UI_WriteTooltipFrame();
    UI_WriteEnd(ent);
    UI_SetCurrentClient(previous_ui_client);
}

static void WritePortraitFrame(LPEDICT ent) {
    uiFrame_t frame;
    char command[64];

    if (!ent || !ent->s.model) return;
    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_PORTRAIT;
    frame.color = COLOR32_WHITE;
    frame.tex.index = ent->s.model;
    snprintf(command, sizeof(command), "+portraitcamera %u", (unsigned)ent->s.number);
    frame.onclick = command;
    UI_SetFrameRect(&frame, 0.211f, 0.4865f, 0.0835f, 0.085f);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

static COLOR32 PortraitHealthColor(LPEDICT ent) {
    FLOAT ratio;
    FLOAT red;
    FLOAT green;

    if (!ent || ent->health.max_value <= 0.0f) return MAKE(COLOR32, 255, 0, 0, 255);
    ratio = MAX(0.0f, MIN(1.0f, ent->health.value / ent->health.max_value));
    red = MIN(1.0f, 2.0f - ratio * 2.0f);
    green = MIN(1.0f, ratio * 2.0f);
    return MAKE(COLOR32,
                (BYTE)(red * 255.0f + 0.5f),
                (BYTE)(green * 255.0f + 0.5f),
                0, 255);
}

static void WritePortraitText(LPCSTR text, COLOR32 color, FLOAT bottom, DWORD stat) {
    uiFrame_t frame;
    uiLabel_t label;

    memset(&frame, 0, sizeof(frame));
    memset(&label, 0, sizeof(label));
    frame.flags.type = FT_STRING;
    frame.text = text && *text ? text : " ";
    frame.color = color;
    frame.stat = stat;
    frame.textLength = 20; /* Warsmash UnitPortraitTextTemplate */
    frame.size.height = 0.01640625f;
    label.font = gi.FontIndex(Theme_String("MasterFont", "Fonts\\FRIZQT__.TTF"), HUD_FONT_SIZE);
    label.textalignx = FONT_JUSTIFYCENTER;
    label.textaligny = FONT_JUSTIFYBOTTOM;
    /* The string has natural text width. Anchor its midpoint to the portrait
     * midpoint and its bottom edge to the Warsmash UnitPortrait offsets. */
    UI_SetFramePoint(&frame.points.x[FPP_MID], FPP_MIN, 0, 0.25275f, false);
    UI_SetFramePoint(&frame.points.y[FPP_MAX], FPP_MIN, 0, bottom, true);
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

static void WritePortraitStats(LPEDICT ent) {
    char health[32];
    char mana[32];
    LONG hp;
    LONG max_hp;
    LONG mp;
    LONG max_mp;

    if (!ent) return;
    hp = (LONG)MAX(0.0f, ent->health.value);
    max_hp = (LONG)MAX(0.0f, ent->health.max_value);
    mp = (LONG)MAX(0.0f, ent->mana.value);
    max_mp = (LONG)MAX(0.0f, ent->mana.max_value);

    snprintf(health, sizeof(health), "%ld / %ld", (long)hp, (long)max_hp);
    if (max_mp > 0)
        snprintf(mana, sizeof(mana), "%ld / %ld", (long)mp, (long)max_mp);
    else
        mana[0] = '\0';

    /* Warsmash's UnitPortrait places the stat strings in the 0.029-high strip
     * below the model: HP at BOTTOM + 0.014 and mana at BOTTOM - 0.0005.
     * OpenRealm keeps the portrait runtime-authored because SmashUI is not a
     * retail MPQ FDF, but uses the same final WC3-space geometry. */
    WritePortraitText(health, PortraitHealthColor(ent), 0.586f, UI_STAT_SELECTION_HEALTH_TEXT);
    WritePortraitText(mana, COLOR32_WHITE, 0.6005f, UI_STAT_SELECTION_MANA_TEXT);
}

void UI_WriteSelectedPortraitLayer(LPEDICT ent) {
    LPEDICT selected[MAX_SELECTED_ENTITIES];
    DWORD count;

    if (!ent || !ent->client) return;
    count = SelectedUnits(ent->client, selected, MAX_SELECTED_ENTITIES);

    UI_WriteStart(LAYER_PORTRAIT);
    if (count == 1) {
        WritePortraitFrame(selected[0]);
        WritePortraitStats(selected[0]);
    }
    UI_WriteEnd(ent);
}

static void WriteInventoryCharge(FLOAT x, FLOAT y, FLOAT w, FLOAT h, DWORD charges) {
    uiFrame_t frame;
    uiLabel_t label;
    char text[16];

    if (!charges) return;
    memset(&frame, 0, sizeof(frame)); memset(&label, 0, sizeof(label));
    snprintf(text, sizeof(text), "%u", (unsigned)charges);
    frame.flags.type = FT_STRING; frame.text = text; frame.color = COLOR32_WHITE;
    label.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", INVENTORY_CHARGE_FONT_SIZE);
    label.textalignx = FONT_JUSTIFYRIGHT; label.textaligny = FONT_JUSTIFYBOTTOM;
    UI_SetFrameRect(&frame, x + 0.001f, y + 0.001f, w - 0.002f, h - 0.002f);
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

/* WC3's classic inventory cover has no usable ROC FDF definition, so construct
 * the native frame directly and send its symbolic war3skins key to the client. */
static void WriteInventoryCover(LPEDICT player) {
    static FRAMEDEF frame;
    LPCSTR art = "ConsoleInventoryCoverTexture";

    if (!art || !*art) {
        fprintf(stderr, "WriteInventoryCover: missing ConsoleInventoryCoverTexture for player skin\n");
        return;
    }
    UI_InitFrame(&frame, FT_TEXTURE);
    UI_SetSize(&frame, 0.128f, 0.175f);
    /* Native WC3 anchor is BOTTOMRIGHT at (0.600, 0.000) in bottom-left coordinates.
     * Relative to OpenRealm's top-left scene origin that is x=0.600, y=0.600. */
    UI_SetPoint(&frame, FRAMEPOINT_BOTTOMRIGHT, NULL, FRAMEPOINT_TOPLEFT, 0.600f, -0.600f);
    frame.AlphaMode = BLEND_MODE_ALPHAKEY;
    frame.Texture.TexCoord.min.y = 0.380859375f;
    frame.Texture.Image = gi.ImageIndex(art);
    UI_WriteFrame(&frame);
}

static void WriteInventoryNoCapacitySlot(BYTE slot, LPCSTR art) {
    FLOAT bx = UI_BASE_WIDTH * 0.5f + 0.1315f + (FLOAT)(slot % 2) * 0.0394f;
    FLOAT by = UI_BASE_HEIGHT - 0.0971f + (FLOAT)(slot / 2) * 0.0384f;
    UI_WriteTextureFrame(bx - 0.0165f, by - 0.0165f, 0.033f, 0.033f, art);
}

static void WriteInventoryTitle(void) {
    uiFrame_t frame = { 0 };
    uiLabel_t label = { 0 };

    frame.flags.type = FT_STRING;
    frame.text = UI_GetString("INVENTORY");
    frame.color = MAKE(COLOR32, 252, 222, 18, 255);
    label.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", 11);
    label.textalignx = FONT_JUSTIFYCENTER;
    label.textaligny = FONT_JUSTIFYMIDDLE;
    UI_SetFrameRect(&frame, 0.516f, 0.4684375f, 0.071f, 0.01125f);
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

static void WriteInventory(LPEDICT player, LPEDICT ent) {
    gameInventoryItem_t items[MAX_INVENTORY];
    DWORD capacity = G_InventoryCapacity(ent);
    BYTE count;

    if (!capacity) { WriteInventoryCover(player); return; }
    WriteInventoryTitle();
    if (capacity < MAX_INVENTORY) {
        LPCSTR art = "ConsoleInventoryNoCapacity";
        if (!art || !*art) {
            fprintf(stderr, "WriteInventory: missing ConsoleInventoryNoCapacity for player skin\n");
            return;
        }
        for (DWORD slot = capacity; slot < MAX_INVENTORY; slot++) WriteInventoryNoCapacitySlot((BYTE)slot, art);
    }

    count = G_GetInventory(ent, items, MAX_INVENTORY);
    FOR_LOOP(i, count) {
        FLOAT bx = UI_BASE_WIDTH * 0.5f + 0.1315f + (FLOAT)(items[i].slot % 2) * 0.0394f;
        FLOAT by = UI_BASE_HEIGHT - 0.0971f + (FLOAT)(items[i].slot / 2) * 0.0384f;
        uiFrame_t frame;
        char onclick[128];
        char tooltip[1024];
        memset(&frame, 0, sizeof(frame));
        frame.flags.type = FT_COMMANDBUTTON;
        frame.color = COLOR32_WHITE;
        frame.tex.index = gi.ImageIndex(items[i].art);
        UI_FormatTooltip("", items[i].tooltip, items[i].ubertip, 0, tooltip, sizeof(tooltip));
        frame.tooltip = tooltip;
        snprintf(onclick, sizeof(onclick), "inventory %u", (unsigned)items[i].slot);
        frame.onclick = onclick;
        UI_SetFrameRect(&frame, bx - 0.0165f, by - 0.0165f, 0.033f, 0.033f);
        UI_WriteProxyFrame(&frame, NULL, 0);
        WriteInventoryCharge(bx - 0.0165f, by - 0.0165f, 0.033f, 0.033f, items[i].charges);
    }
    if (count) UI_WriteTooltipFrame();
}

static void UI_SendInventoryLayer(LPEDICT ent, LPEDICT *selected, DWORD count) {
    LPEDICT focused = count > 0 && ent && ent->client ? G_GetMainSelectedUnit(ent->client) : NULL;

    (void)selected;
    UI_WriteStart(LAYER_INVENTORY);
    if (focused) WriteInventory(ent, focused);
    UI_WriteEnd(ent);
}

void G_RefreshInventoryLayer(LPEDICT ent) {
    LPEDICT selected[MAX_SELECTED_ENTITIES];
    DWORD count;

    if (!ent || !ent->client) return;
    count = SelectedUnits(ent->client, selected, MAX_SELECTED_ENTITIES);
    UI_SendInventoryLayer(ent, selected, count);
}

void Get_Portrait_f(LPEDICT ent) {
    LPEDICT selected[MAX_SELECTED_ENTITIES];
    DWORD count;

    if (!ent || !ent->client) return;
    count = SelectedUnits(ent->client, selected, MAX_SELECTED_ENTITIES);

    /* A normal-game transmission temporarily owns LAYER_PORTRAIT. Selection
     * still updates the info/inventory panels, but the talking head remains
     * authoritative until the transmission ends. */
    UI_WriteDialoguePresentation(ent);

    UI_SendInfoPanel(ent, selected, count);
    UI_SendInventoryLayer(ent, selected, count);
}

void G_InvalidateUnitInfoPanel(LPEDICT unit) {
    if (!unit) return;
    FOR_LOOP(i, game.max_clients) {
        LPGAMECLIENT client = game.clients + i;
        if (client->connected && G_IsEntitySelected(client, unit))
            client->infopanel.entity = 0;
    }
}

/* The portrait layer is authored separately from the info panel and is not
 * rebuilt by G_UpdateClientInfoPanels(). In-place type changes therefore have
 * to dirty the selected-unit presentation explicitly so G_RunClients() emits
 * the new model on the next server frame. Keep this deferred rather than
 * writing svc_layout from inside gameplay state mutation. */
void G_InvalidateUnitPortrait(LPEDICT unit) {
    if (!unit) return;
    FOR_LOOP(i, game.max_clients) {
        LPGAMECLIENT client = game.clients + i;
        if (client->connected && G_IsEntitySelected(client, unit))
            client->presentation_dirty = true;
    }
}

static USHORT SelectedPortraitStat(FLOAT value) {
    LONG whole = (LONG)MAX(0.0f, value); /* Warsmash FastNumberFormat truncates */
    return (USHORT)MIN(whole, USHRT_MAX);
}

static USHORT SelectedTimedStatusStat(LPGAMECLIENT client, LPEDICT selected) {
    heroabilitystatus_t const *status;
    FLOAT fraction;

    if (!client || !selected || selected->s.player != client->ps.number) return 0;
    status = unit_findtimedbarstatus(selected);
    if (!status) return 0;
    fraction = unit_statusremainingfraction(status);
    return (USHORT)MIN((DWORD)(fraction * (FLOAT)USHRT_MAX + 0.5f), (DWORD)USHRT_MAX);
}

#ifdef BZ_TESTS
USHORT UI_TestSelectedTimedStatusStat(LPGAMECLIENT client, LPEDICT selected) {
    return SelectedTimedStatusStat(client, selected);
}
#endif

static void UpdateSelectedLiveStats(LPGAMECLIENT client, LPEDICT selected) {
    USHORT old_timed;
    USHORT new_timed;
    int debug;

    if (!client) return;
    old_timed = client->ps.stats[UI_PLAYERSTAT_SELECTION_TIMED_STATUS];
    debug = timed_status_debug_level();
    if (!selected) {
        client->ps.stats[UI_PLAYERSTAT_SELECTION_HEALTH] = 0;
        client->ps.stats[UI_PLAYERSTAT_SELECTION_MAX_HEALTH] = 0;
        client->ps.stats[UI_PLAYERSTAT_SELECTION_MANA] = 0;
        client->ps.stats[UI_PLAYERSTAT_SELECTION_MAX_MANA] = 0;
        client->ps.stats[UI_PLAYERSTAT_SELECTION_TIMED_STATUS] = 0;
        if (debug >= 2 && old_timed) {
            fprintf(stderr,
                    "WC3_TIMED_STATUS server publish player=%u unit=<none> old=%u new=0\n",
                    (unsigned)client->ps.number, (unsigned)old_timed);
        }
        return;
    }
    client->ps.stats[UI_PLAYERSTAT_SELECTION_HEALTH] = SelectedPortraitStat(selected->health.value);
    client->ps.stats[UI_PLAYERSTAT_SELECTION_MAX_HEALTH] = SelectedPortraitStat(selected->health.max_value);
    client->ps.stats[UI_PLAYERSTAT_SELECTION_MANA] = SelectedPortraitStat(selected->mana.value);
    client->ps.stats[UI_PLAYERSTAT_SELECTION_MAX_MANA] = SelectedPortraitStat(selected->mana.max_value);
    new_timed = SelectedTimedStatusStat(client, selected);
    client->ps.stats[UI_PLAYERSTAT_SELECTION_TIMED_STATUS] = new_timed;
    if (debug >= 2 && old_timed != new_timed) {
        DWORD const old_bucket = ((DWORD)old_timed * 10u) / USHRT_MAX;
        DWORD const new_bucket = ((DWORD)new_timed * 10u) / USHRT_MAX;
        if (debug >= 3 || old_timed == 0 || new_timed == 0 || old_bucket != new_bucket) {
            fprintf(stderr,
                    "WC3_TIMED_STATUS server publish player=%u unit=%u old=%u new=%u fraction=%.4f\n",
                    (unsigned)client->ps.number, (unsigned)selected->s.number,
                    (unsigned)old_timed, (unsigned)new_timed,
                    new_timed / (FLOAT)USHRT_MAX);
        }
    }
}

/* Keep selected-unit HP/mana and the normalized timed-status fraction in
 * playerState so live bars/text update through ordinary snapshots instead of
 * forcing a whole FDF layer resend every server frame. Re-send LAYER_INFOPANEL
 * only when its static presentation (selection, timer eligibility/label, XP) changes. */
void G_RefreshInfoPanel(LPEDICT ent) {
    LPEDICT selected[MAX_SELECTED_ENTITIES];
    DWORD count;
    BOOL queue_panel;

    if (!ent || !ent->client) return;
    count = SelectedUnits(ent->client, selected, MAX_SELECTED_ENTITIES);
    UpdateSelectedLiveStats(ent->client, count == 1 ? selected[0] : NULL);
    if (count != 1) {
        ent->client->infopanel.entity = 0;
        return;
    }

    queue_panel = UI_UsesBuildingQueuePanel(ent->client, selected[0]);
    if (queue_panel) {
        ent->client->infopanel.entity = 0;
        return;
    }
    /* HP/mana/timed-status progress are live player-state bindings, so changing
     * their values must not force LAYER_INFOPANEL/FDF reserialization every frame. */
    if (selected[0]->s.number == ent->client->infopanel.entity &&
        (LONG)selected[0]->hero.xp == ent->client->infopanel.xp) {
        return;
    }
    UI_SendInfoPanel(ent, selected, count);
}

/* Once per server frame, keep every connected player's info panel in sync.
 * Client edicts live in the reserved [0, max_clients) range and are not normal
 * in-use world entities, so do not gate this on edict->inuse. */
void G_UpdateClientInfoPanels(void) {
    FOR_LOOP(i, game.max_clients) {
        LPGAMECLIENT client = game.clients + i;
        LPEDICT ent;

        if (!client->connected) continue;
        ent = G_GetPlayerEntityByNumber(client->ps.number);
        if (ent && ent->client == client) G_RefreshInfoPanel(ent);
    }
}

/* Re-send LAYER_CONSOLE only when resource display/tooltip state changed. */
void G_RefreshResourceBar(LPEDICT ent) {
    LPPLAYER ps;
    LONG gold, lumber, food_u, food_c, gold_rate, lumber_rate;

    if (!ent || !ent->client) return;
    ps          = &ent->client->ps;
    gold        = (LONG)ps->stats[PLAYERSTATE_RESOURCE_GOLD];
    lumber      = (LONG)ps->stats[PLAYERSTATE_RESOURCE_LUMBER];
    food_u      = (LONG)ps->stats[PLAYERSTATE_RESOURCE_FOOD_USED];
    food_c      = G_GetEffectiveFoodCap(ent->client);
    gold_rate   = (LONG)ps->stats[PLAYERSTATE_GOLD_UPKEEP_RATE];
    lumber_rate = (LONG)ps->stats[PLAYERSTATE_LUMBER_UPKEEP_RATE];

    if (gold        == ent->client->resourcebar.gold        &&
        lumber      == ent->client->resourcebar.lumber      &&
        food_u      == ent->client->resourcebar.food_used   &&
        food_c      == ent->client->resourcebar.food_cap    &&
        gold_rate   == ent->client->resourcebar.gold_rate   &&
        lumber_rate == ent->client->resourcebar.lumber_rate)
        return;

    UI_WriteStart(LAYER_CONSOLE);
    UI_WriteConsoleBackdrop(ent->client, food_u, food_c);
    UI_WriteMinimapFrame();
    UI_WriteEnd(ent);

    ent->client->resourcebar.gold        = gold;
    ent->client->resourcebar.lumber      = lumber;
    ent->client->resourcebar.food_used   = food_u;
    ent->client->resourcebar.food_cap    = food_c;
    ent->client->resourcebar.gold_rate   = gold_rate;
    ent->client->resourcebar.lumber_rate = lumber_rate;
}

/* Once per server frame, keep every player's resource bar in sync. */
void G_UpdateClientResourceBars(void) {
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = g_edicts + i;
        if (ent->inuse && ent->client)
            G_RefreshResourceBar(ent);
    }
}
