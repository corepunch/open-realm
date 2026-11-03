/*
 * hud_infopanel.c — Info panel, multiselect, and per-frame update stubs.
 *
 * Builds the single-unit info panel (name, level, damage, armor, hero
 * attributes, XP bar, HP/mana), the multi-select grid, and the
 * build-queue overlay.  Also contains the stubbed entry points that
 * console_ui.c now handles client-side.
 */

#include "hud_local.h"
#include "hud_utils.h"
#include "../skills/s_skills.h"

#define INVENTORY_CHARGE_FONT_SIZE 10
#define DAMAGE_TOOLTIP_TIP_SIZE 256 // bytes; fits one localized label with color codes and NUL for tooltip formatting
#define DAMAGE_TOOLTIP_BODY_SIZE 1536 // bytes; fits the expanded damage/armor paragraphs and markup before serialization
/* Warsmash anchors its 0.180 x 0.120 simple info panel at the bottom-centre
 * of the 0.800 x 0.600 UI. Cargo icons are TOPLEFT-relative to that panel at
 * y = -0.75 * frontQueueIconWidth. Convert that bottom-origin geometry to
 * OpenRealm's top-left proxy-frame coordinates instead of positioning the
 * slots in world-screen space above the status panel. */
static int timed_status_debug_level(void) {
    cstring_t value;

    value = gi.CvarString("wc3_timed_status_debug", "0");
    return value ? atoi(value) : 0;
}

static void timed_status_debug_dump(edict_t *ent, gameClient_t *viewer, cstring_t stage) {
    int const debug = timed_status_debug_level();
    uint32_t const now = G_Time();
    char unit_code[5] = { 0 };

    if (debug < 1 || !ent) return;
    memcpy(unit_code, &ent->class_id, 4);
    fprintf(stderr,
            "WC3_TIMED_STATUS server stage=%s unit=%u type=%s owner=%u viewer=%d now=%u\n",
            stage ? stage : "?", (unsigned)ent->s.number, unit_code,
            (unsigned)ent->s.player, viewer ? (int)viewer->ps.number : -1,
            (unsigned)now);
    FOR_LOOP(i, ent->unit_state ? MAX_UNIT_STATUSES : 0) {
        heroabilitystatus_t const *status = ent->unit_state->statuses + i;
        char code[5] = { 0 };
        int32_t remaining;

        if (!status->level) continue;
        memcpy(code, &status->code, 4);
        remaining = status->timestamp > now ? (int32_t)(status->timestamp - now) : 0;
        fprintf(stderr,
                "WC3_TIMED_STATUS server status slot=%u code=%s level=%u timestamp=%u duration_ms=%u remaining_ms=%ld eligible=%u fraction=%.4f\n",
                (unsigned)i, code, (unsigned)status->level,
                (unsigned)status->timestamp, (unsigned)status->duration_ms,
                (long)remaining, (unsigned)unit_statusshowstimedbar(status->code),
                unit_statusremainingfraction(status));
    }
}

static bool InfoPanelStringsResolved(void) {
    static cstring_t const required[] = {
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
        cstring_t const value = UI_GetString(required[i]);
        if (!value || !strcmp(value, required[i])) return false;
    }
    return true;
}

static void InitStatusWrapper(frameDef_t *frame, float x, float y, float width, float height) {
    UI_InitFrame(frame, FT_SIMPLEFRAME);
    UI_SetSize(frame, width, height);
    UI_SetPoint(frame, FRAMEPOINT_TOPLEFT, hud.simple.SimpleInfoPanelUnitDetail,
                FRAMEPOINT_TOPLEFT, x, y);
}

void UI_LoadHudInfoPanel(void) {
    bool global_strings_loaded;
    bool infopanel_strings_loaded;

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
        InitStatusWrapper(&hud.rank,    0.100f, -0.07050f, 0.100f, 0.030125f);
        InitStatusWrapper(&hud.hero,    0.100f, -0.03700f, 0.100f, 0.062500f);
        InitStatusWrapper(&hud.food,    0.100f, -0.03925f, 0.100f, 0.030125f);
        InitStatusWrapper(&hud.gold,    0.100f, -0.03925f, 0.100f, 0.030125f);

        /* Bind the runtime-controlled status bars to the retail FDF geometry.
         * The FDF owns their anchors; Warsmash supplies only width, textures,
         * colour and the live progress value. */
        UI_SetSize(hud.simple.SimpleHeroLevelBar, 0.180f, hud.simple.SimpleHeroLevelBar->Height);
        UI_SetTexture(hud.simple.SimpleHeroLevelBar, "SimpleXpBarConsole", false);
        UI_SetTexture2(hud.simple.SimpleHeroLevelBar, "SimpleXpBarBorder", false);
        hud.simple.SimpleHeroLevelBar->Color = MAKE(color32_t, 138, 0, 131, 255);
        UI_SetSize(hud.simple.SimpleProgressIndicator, 0.180f, hud.simple.SimpleProgressIndicator->Height);
        UI_SetTexture(hud.simple.SimpleProgressIndicator, "SimpleProgressBarConsole", false);
        UI_SetTexture2(hud.simple.SimpleProgressIndicator, "SimpleProgressBarBorder", false);
        hud.simple.SimpleProgressIndicator->Color = MAKE(color32_t, 65, 130, 210, 255);
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
    frameDef_t *const frames_to_hide[] = {
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
                                    int32_t min_damage, int32_t max_damage, float temporary_bonus) {
    int32_t const bonus = (int32_t)temporary_bonus;
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

static void FormatAttackDamageWithStatus(char *buffer, size_t buffer_size,
                                         edict_t const *ent, int32_t min_damage,
                                         int32_t max_damage, float temporary_bonus,
                                         float damage_base) {
    float const roar_bonus = S_RoarDamageBonus(ent);
    float const percent = unit_status_modifier_total(ent, WC3_STATUS_MOD_ATTACK_DAMAGE_PERCENT);
    if (roar_bonus != 0.0f) {
        int32_t const bonus = (int32_t)(damage_base * roar_bonus + 0.5f);
        if (bonus > 0) {
            snprintf(buffer, buffer_size, "%ld - %ld |cff00ff00+%ld|r",
                     (long)min_damage, (long)max_damage, (long)bonus);
            return;
        }
    }
    if (percent != 0.0f) {
        snprintf(buffer, buffer_size, "%ld - %ld |cff00ff00+%d%%|r",
                 (long)min_damage, (long)max_damage, (int)(percent * 100.0f + 0.5f));
        return;
    }
    FormatAttackDamageValue(buffer, buffer_size, min_damage, max_damage, temporary_bonus);
}

static void AppendInfoPanelBonus(char *body, size_t body_size, cstring_t label, float bonus) {
    size_t const used = body ? strlen(body) : 0;
    int const value = (int)bonus;
    if (!body || !label || !body_size || used >= body_size || !value) return;
    snprintf(body + used, body_size - used, "\n%s: %s%d|r", label,
             value > 0 ? "|cff00ff00+" : "|cffff0000", value);
}

static void AppendDamageBonusTooltip(char *body, size_t body_size, edict_t const *ent,
                                     float temporary_bonus, float damage_base) {
    float const roar = S_RoarDamageBonus(ent);
    float const percent = unit_status_modifier_total(ent, WC3_STATUS_MOD_ATTACK_DAMAGE_PERCENT);
    float bonus = temporary_bonus;
    if (roar != 0.0f) bonus = damage_base * roar;
    else if (percent != 0.0f) {
        size_t const used = body ? strlen(body) : 0;
        int const value = (int)(percent * 100.0f + 0.5f);
        if (body && used < body_size)
            snprintf(body + used, body_size - used, "\nBonus Damage: |cff00ff00+%d%%|r", value);
        return;
    }
    AppendInfoPanelBonus(body, body_size, "Bonus Damage", bonus);
}

#ifdef BZ_TESTS
void UI_TestFormatAttackDamageWithStatus(char *buffer, size_t buffer_size,
                                         edict_t const *ent, int32_t min_damage,
                                         int32_t max_damage, float temporary_bonus,
                                         float damage_base) {
    FormatAttackDamageWithStatus(buffer, buffer_size, ent, min_damage, max_damage,
                                 temporary_bonus, damage_base);
}
#endif

static void FormatArmorValue(char *buffer, size_t buffer_size, edict_t const *ent) {
    float const temporary_bonus = ent->unit_state ? ent->unit_state->temporary_armor_bonus : 0.0f;
    float const status_bonus = unit_status_modifier_total(ent, WC3_STATUS_MOD_ARMOR_FLAT);
    int32_t const base = (int32_t)roundf(G_UnitArmorValue(ent) - temporary_bonus - status_bonus);
    int32_t const bonus = (int32_t)(temporary_bonus + status_bonus);

    if (bonus > 0) {
        snprintf(buffer, buffer_size, "%ld |cff00ff00+%ld|r", (long)base, (long)bonus);
    } else if (bonus < 0) {
        snprintf(buffer, buffer_size, "%ld |cffff0000%ld|r", (long)base, (long)bonus);
    } else {
        snprintf(buffer, buffer_size, "%ld", (long)(base + bonus));
    }
}

static void WriteLegacyUnitStats(edict_t *ent, UnitWeapons_t const *weapons,
                                 bool has_attack2, int32_t min_damage, int32_t max_damage,
                                 int32_t min_damage2, int32_t max_damage2, bool is_hero,
                                 uint32_t level) {
    char buffer[128];

    UI_SetText(hud.unit.AttackLabel1, "Damage:");
    FormatAttackDamageWithStatus(buffer, sizeof(buffer), ent, min_damage, max_damage,
                            ent->unit_state->attack1.temporaryDamageBonus,
                            ent->unit_state->attack1.damageBase);
    UI_SetText(hud.unit.AttackValue1, "%s", buffer);
    UI_SetText(hud.unit.AttackLabel2, "Damage:");
    FormatAttackDamageWithStatus(buffer, sizeof(buffer), ent, min_damage2, max_damage2,
                            ent->unit_state->attack2.temporaryDamageBonus,
                            ent->unit_state->attack2.damageBase);
    UI_SetText(hud.unit.AttackValue2, "%s", buffer);
    UI_SetHidden(hud.unit.AttackLabel2, !has_attack2);
    UI_SetHidden(hud.unit.AttackValue2, !has_attack2);
    UI_SetText(hud.unit.DefenseLabel, "Armor:");
    if (G_UnitHasFlag(ent, UNIT_FLAG_INVULNERABLE)) UI_SetText(hud.unit.DefenseValue, "%s", "|cffff0000Invulnerable|r");
    else {
        FormatArmorValue(buffer, sizeof(buffer), ent);
        UI_SetText(hud.unit.DefenseValue, "%s", buffer);
    }
    UI_SetText(hud.unit.SpeedTitle, "Speed:");
    UI_SetText(hud.unit.SpeedValue, "%d", (int)(ent->unitinfo.MoveSpeed + 0.5f));
    UI_SetText(hud.unit.RangeTitle1, "Range:");
    UI_SetText(hud.unit.RangeValue1, "%d", (int)(ent->unit_state->attack1.range + 0.5f));
    UI_SetText(hud.unit.RangeTitle2, "Range:");
    UI_SetText(hud.unit.RangeValue2, "%d", (int)(ent->unit_state->attack2.range + 0.5f));
    UI_SetHidden(hud.unit.RangeTitle2, !has_attack2);
    UI_SetHidden(hud.unit.RangeValue2, !has_attack2);

    if (is_hero) {
        cstring_t const prim = ent->data.UnitBalance->primaryAttribute;
        struct { cstring_t code; uint32_t val; } attrs[3] = {
            { "STR", ent->unit_state->hero.str }, { "AGI", ent->unit_state->hero.agi }, { "INT", ent->unit_state->hero.intel },
        };
        frameDef_t *icon_values[3] = {
            hud.unit.IconValue1, hud.unit.IconValue2, hud.unit.IconValue3,
        };

        FOR_LOOP(a, 3) {
            bool const isprim = prim && !strcmp(prim, attrs[a].code);
            UI_SetText(icon_values[a], "%lu", (unsigned long)attrs[a].val);
            icon_values[a]->Font.Color = isprim ? MAKE(color32_t, 120, 230, 120, 255) : COLOR32_WHITE;
        }

        uint32_t const need = G_HeroXPForLevel(level + 1);
        uint32_t const have = G_HeroXPForLevel(level);
        if (need > have) {
            snprintf(buffer, sizeof(buffer), "XP: %lu / %lu",
                     (unsigned long)(ent->unit_state->hero.xp - (ent->unit_state->hero.xp < have ? ent->unit_state->hero.xp : have)),
                     (unsigned long)(need - have));
            UI_SetText(hud.unit.IconValue4, "%s", buffer);
            hud.unit.IconValue4->Font.Color = MAKE(color32_t, 200, 200, 200, 255);
        }
    }
}

static bool InfoPanelTextureExists(cstring_t path) {
    uint32_t size = 0;
    handle_t data;

    if (!path || !*path) return false;
    path = UI_ResolveTextureAlias(path);
    data = gi.ReadFile(path, &size);
    if (!data) return false;
    gi.MemFree(data);
    return true;
}

static int InfoPanelIconTypeIndex(cstring_t prefix, cstring_t type, cstring_t *normalized) {
    static cstring_t const damage_types[] = {
        "Unknown", "Normal", "Pierce", "Siege", "Spells", "Chaos", "Magic", "Hero",
    };
    static cstring_t const armor_types[] = {
        "Small", "Medium", "Large", "Fort", "Normal", "Hero", "Divine", "None",
    };
    cstring_t const *types;
    cstring_t fallback;

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

static cstring_t InfoPanelThemeIcon(cstring_t prefix, cstring_t type, bool has_upgrade) {
    char key[96];
    cstring_t texture;

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

static cstring_t ResolveTypedInfoPanelIcon(cstring_t prefix, cstring_t type, bool has_upgrade) {
    cstring_t normalized;
    cstring_t texture, fallback;
    int const family = !strcasecmp(prefix, "Armor") ? 1 : 0;
    int const type_index = InfoPanelIconTypeIndex(prefix, type, &normalized);
    int const upgrade_index = has_upgrade ? 1 : 0;
    infoPanelIconCache_t *cache = &hud.icon_cache[family][type_index][upgrade_index];

    if (cache->resolved) return cache->texture[0] ? cache->texture : NULL;
    cache->resolved = true;

    texture = InfoPanelThemeIcon(prefix, normalized, has_upgrade);
    if (texture && *texture && InfoPanelTextureExists(texture)) {
        UI_CopyString(cache->texture, sizeof(cache->texture), texture);
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
            UI_CopyString(cache->texture, sizeof(cache->texture), fallback);
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
cstring_t UI_TestResolveTypedInfoPanelIcon(cstring_t prefix, cstring_t type, bool has_upgrade) {
    return ResolveTypedInfoPanelIcon(prefix, type, has_upgrade);
}
#endif

static void SetTypedInfoPanelIcon(frameDef_t *frame, cstring_t prefix, cstring_t type, bool has_upgrade) {
    cstring_t texture;

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

static void SetHeroPrimaryAttributeIcon(cstring_t primary) {
    cstring_t key = "InfoPanelIconHeroIconSTR";
    cstring_t texture;

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

static cstring_t InfoPanelString(cstring_t key, cstring_t fallback);

static float InfoPanelHeroAttributeValue(cstring_t key, float fallback) {
    cstring_t value = Stb_IniCacheFind(&game.config.misc, "Misc", key);
    return value && *value ? (float)atof(value) : fallback;
}

static void AppendHeroAttributeLine(char *body, size_t body_size, cstring_t line) {
    size_t const used = strlen(body);
    if (!line || !*line || used >= body_size) return;
    snprintf(body + used, body_size - used, "%s - %s", used ? "\n" : "", line);
}

static void AppendHeroBonusLine(char *body, size_t body_size, cstring_t line) {
    size_t const used = strlen(body);
    if (!line || !*line || used >= body_size) return;
    snprintf(body + used, body_size - used, "%s%s", used ? "\n" : "", line);
}

static void FormatHeroAttributesTooltip(char *tip, size_t tip_size, char *body,
                                        size_t body_size, cstring_t primary) {
    static cstring_t const labels[3] = { "COLON_STRENGTH", "COLON_AGILITY", "COLON_INTELLECT" };
    static cstring_t const fallbacks[3] = { "Strength:", "Agility:", "Intelligence:" };
    float const hp_per_strength = InfoPanelHeroAttributeValue("StrHitPointBonus", 25.0f);
    float const armor_per_agility = game.constants.combatConstantsLoaded
        ? game.constants.agiDefenseBonus : InfoPanelHeroAttributeValue("AgiDefenseBonus", 0.3f);
    float const damage_per_primary = game.constants.combatConstantsLoaded
        ? game.constants.strAttackBonus : InfoPanelHeroAttributeValue("StrAttackBonus", 1.0f);
    float const mana_per_intelligence = InfoPanelHeroAttributeValue("IntManaBonus", 15.0f);
    size_t used;

    if (!tip || !tip_size || !body || !body_size) return;
    snprintf(tip, tip_size, "%s", InfoPanelString("COLON_HERO_ATTRIBUTES", "Hero Attributes:"));
    body[0] = '\0';
    FOR_LOOP(i, 3) {
        cstring_t const attribute = i == 0 ? "STR" : i == 1 ? "AGI" : "INT";
        bool const is_primary = primary && !strcmp(primary, attribute);
        used = strlen(body);
        if (used < body_size)
            snprintf(body + used, body_size - used, "%s%s", used ? "\n" : "",
                     InfoPanelString(labels[i], fallbacks[i]));
        if (is_primary) {
            char line[256];
            AppendHeroAttributeLine(body, body_size,
                InfoPanelString("PRIMARY_ATTRIBUTE", "Primary Attribute"));
            snprintf(line, sizeof(line), InfoPanelString("BONUS_DAMAGE",
                     " - Each point increases damage by %d"), (int)(damage_per_primary + 0.5f));
            AppendHeroBonusLine(body, body_size, line);
        }
        if (i == 0) {
            char line[256];
            snprintf(line, sizeof(line), InfoPanelString("BONUS_HITPOINTS",
                     " - Each point increases hit points by %d"), (int)(hp_per_strength + 0.5f));
            AppendHeroBonusLine(body, body_size, line);
            AppendHeroBonusLine(body, body_size,
                InfoPanelString("BONUS_HPREGEN", " - Each point increases hit point regeneration"));
        } else if (i == 1) {
            char line[256];
            uint32_t const agility_per_armor = armor_per_agility > 0.0f
                ? MAX(1u, (uint32_t)(1.0f / armor_per_agility)) : 0;
            snprintf(line, sizeof(line), InfoPanelString("BONUS_DEFENSE_FIXED",
                     " - Every %d points increase armor by 1"), (int)agility_per_armor);
            AppendHeroBonusLine(body, body_size, line);
            AppendHeroBonusLine(body, body_size,
                InfoPanelString("BONUS_ATTACK_SPEED", " - Each point increases attack speed"));
        } else {
            char line[256];
            snprintf(line, sizeof(line), InfoPanelString("BONUS_MANA",
                     " - Each point increases mana by %d"), (int)(mana_per_intelligence + 0.5f));
            AppendHeroBonusLine(body, body_size, line);
            AppendHeroBonusLine(body, body_size,
                InfoPanelString("BONUS_MANAREGEN", " - Each point increases mana regeneration"));
        }
    }
}

static void SetHeroAttributesTooltip(frameDef_t *icon, cstring_t primary) {
    static char tip[128];
    static char body[1536];

    if (!icon) return;
    FormatHeroAttributesTooltip(tip, sizeof(tip), body, sizeof(body), primary);
    icon->Tip = tip;
    icon->Ubertip = body;
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

static uint32_t UnitWeaponUpgrade(edict_t *ent) {
    static cstring_t const classes[] = { "melee", "ranged", "artillery" };

    if (!ent || !ent->data.UnitBalance) return 0;
    FOR_LOOP(i, sizeof(classes) / sizeof(classes[0])) {
        uint32_t const upgrade = G_GetUnitUpgradeForClass(ent, classes[i]);
        if (upgrade) return upgrade;
    }
    return 0;
}

static uint32_t UnitArmorUpgrade(edict_t *ent) {
    if (!ent || !ent->data.UnitBalance) return 0;
    return G_GetUnitUpgradeForClass(ent, "armor");
}

static cstring_t InfoPanelString(cstring_t key, cstring_t fallback) {
    cstring_t value = key ? UI_GetString(key) : NULL;
    return value && *value && strcmp(value, key) ? value : fallback;
}

static bool InfoPanelCsvToken(cstring_t list, uint32_t index, char *out, size_t out_size) {
    uint32_t current = 0;
    bool quoted = false;
    size_t written = 0;
    cstring_t p = list;

    if (!out || !out_size) return false;
    out[0] = '\0';
    if (!list) return false;
    while (true) {
        char const ch = *p;
        if (!ch || (ch == ',' && !quoted)) {
            if (current == index) {
                while (written && (out[written - 1] == ' ' || out[written - 1] == '\t')) written--;
                out[written] = '\0';
                return written > 0;
            }
            current++;
            written = 0;
            if (!ch) return false;
            p++;
            while (*p == ' ' || *p == '\t') p++;
            continue;
        }
        if (ch == '"') quoted = !quoted;
        else if (current == index && written + 1 < out_size) out[written++] = ch;
        p++;
    }
}

static void InfoPanelDecodeNewlines(char *text) {
    char *read = text;
    char *write = text;
    if (!text) return;
    while (*read) {
        if (read[0] == '|' && read[1] == 'n') {
            *write++ = '\n';
            read += 2;
        } else *write++ = *read++;
    }
    *write = '\0';
}

static void FormatCasterRankTooltip(char *tip, size_t tip_size, char *body,
                                    size_t body_size, cstring_t rank_names,
                                    cstring_t rank_spells, cstring_t upgrade_names,
                                    uint32_t rank) {
    char rank_name[128] = "Initiate";
    char upgrade_name[256] = "";
    char spells[768] = "";
    char upgrade[384] = "";
    cstring_t upgrade_format;

    if (!tip || !tip_size || !body || !body_size) return;
    InfoPanelCsvToken(rank_names, rank, rank_name, sizeof(rank_name));
    InfoPanelCsvToken(upgrade_names, rank, upgrade_name, sizeof(upgrade_name));
    InfoPanelCsvToken(rank_spells, rank, spells, sizeof(spells));
    InfoPanelDecodeNewlines(spells);
    if (*upgrade_name) {
        upgrade_format = InfoPanelString("UPGRADE_TOOLTIP", "%s - Level %d");
        snprintf(upgrade, sizeof(upgrade), upgrade_format, G_LevelString(upgrade_name), (int)rank);
    }
    snprintf(tip, tip_size, "%s", InfoPanelString("COLON_RANK", "Rank:"));
    snprintf(body, body_size, "%s %s\n%s %s",
             InfoPanelString("COLON_NAME", "Name:"), G_LevelString(rank_name),
             InfoPanelString("COLON_UPGRADE", "Upgrade:"), upgrade);
    if (*spells) {
        size_t const used = strlen(body);
        if (used < body_size)
            snprintf(body + used, body_size - used, "\n\n%s\n%s",
                     InfoPanelString("COLON_SPELLS", "Spells:"), spells);
    }
}

static void SetCasterRankTooltip(frameDef_t *icon, UnitProfile_t const *profile,
                                 cstring_t upgrade_names, uint32_t rank) {
    static char tip[128];
    static char body[1536];

    if (!icon || !profile) return;
    FormatCasterRankTooltip(tip, sizeof(tip), body, sizeof(body),
                            profile->casterUpgradeName, profile->casterUpgradeTip,
                            upgrade_names, rank);
    icon->Tip = tip;
    icon->Ubertip = body;
}

static float InfoPanelThreshold(cstring_t key, float fallback) {
    cstring_t value = Stb_IniCacheFind(&game.config.misc, "InfoPanel", key);
    return value && *value ? (float)atof(value) : fallback;
}

static cstring_t InfoPanelAttackSpeed(float cooldown) {
    float const very_slow = InfoPanelThreshold("AttackVerySlow", 3.0f);
    float const slow = InfoPanelThreshold("AttackSlow", 2.0f);
    float const average = InfoPanelThreshold("AttackAverage", 1.5f);
    float const fast = InfoPanelThreshold("AttackFast", 1.0f);

    if (cooldown > very_slow) return InfoPanelString("MOVESPEEDVERYSLOW", "Very Slow");
    if (cooldown > slow) return InfoPanelString("MOVESPEEDSLOW", "Slow");
    if (cooldown >= average) return InfoPanelString("MOVESPEEDAVERAGE", "Average");
    if (cooldown >= fast) return InfoPanelString("MOVESPEEDFAST", "Fast");
    return InfoPanelString("MOVESPEEDVERYFAST", "Very Fast");
}

static cstring_t InfoPanelMoveSpeed(float speed) {
    float const very_slow = InfoPanelThreshold("SpeedVerySlow", 175.0f);
    float const slow = InfoPanelThreshold("SpeedSlow", 220.0f);
    float const average = InfoPanelThreshold("SpeedAverage", 280.0f);
    float const fast = InfoPanelThreshold("SpeedFast", 350.0f);

    if (speed < very_slow) return InfoPanelString("MOVESPEEDVERYSLOW", "Very Slow");
    if (speed < slow) return InfoPanelString("MOVESPEEDSLOW", "Slow");
    if (speed < average) return InfoPanelString("MOVESPEEDAVERAGE", "Average");
    if (speed < fast) return InfoPanelString("MOVESPEEDFAST", "Fast");
    return InfoPanelString("MOVESPEEDVERYFAST", "Very Fast");
}

static void InfoPanelArmorTypeStrings(uint32_t type, cstring_t *type_key,
                                      cstring_t *type_fallback, cstring_t *tip_key,
                                      cstring_t *tip_fallback) {
    switch (type) {
    case 0: *type_key = "ARMOR_SMALL"; *type_fallback = "Type: Light";
            *tip_key = "ARMORTIP_SMALL"; *tip_fallback = "Light armor takes extra damage from Piercing and Magic attacks."; break;
    case 1: *type_key = "ARMOR_MEDIUM"; *type_fallback = "Type: Medium";
            *tip_key = "ARMORTIP_MEDIUM"; *tip_fallback = "Medium armor takes extra damage from Normal attacks, and reduces damage from Piercing, Magic, and Siege attacks."; break;
    case 2: *type_key = "ARMOR_LARGE"; *type_fallback = "Type: Heavy";
            *tip_key = "ARMORTIP_LARGE"; *tip_fallback = "Heavy armor takes extra damage from Magic attacks."; break;
    case 3: *type_key = "ARMOR_FORT"; *type_fallback = "Type: Fortified";
            *tip_key = "ARMORTIP_FORT"; *tip_fallback = "Fortified armor greatly reduces Piercing, Magic, Normal and Hero attacks, but takes extra damage from Siege attacks."; break;
    case 4: *type_key = "ARMOR_NORMAL"; *type_fallback = "Type: Normal";
            *tip_key = "ARMORTIP_NORMAL"; *tip_fallback = "Normal armor takes standard damage from all attack types."; break;
    case 5: *type_key = "ARMOR_HERO"; *type_fallback = "Type: Hero";
            *tip_key = "ARMORTIP_HERO"; *tip_fallback = "Heroes take reduced damage from Piercing, Magic, Siege attacks, and Spells."; break;
    case 6: *type_key = "ARMOR_DIVINE"; *type_fallback = "Type: Divine";
            *tip_key = "ARMORTIP_DIVINE"; *tip_fallback = "Divine beings only take damage from Chaos attacks."; break;
    case 7: *type_key = "ARMOR_NONE"; *type_fallback = "Type: Unarmored";
            *tip_key = "ARMORTIP_NONE"; *tip_fallback = "Unarmored takes extra damage from Piercing, and Siege attacks."; break;
    default: *type_key = "ARMOR_UNKNOWN"; *type_fallback = "Type: Unknown";
             *tip_key = "ARMORTIP_UNKNOWN"; *tip_fallback = "Unknown armor applies an unknown reduction to all attacks."; break;
    }
}

static void InfoPanelDamageTypeStrings(attackType_t type, cstring_t *label_key,
                                       cstring_t *label_fallback, cstring_t *tip_key,
                                       cstring_t *tip_fallback);

/* The gameplay damage matrix (including war3mapMisc overrides) is authoritative.
 * Only use the localized stock explanation if every relevant multiplier still
 * matches stock; otherwise give the actual percentages rather than misleading
 * players with a canned description. */
static bool InfoPanelArmorMatchupsAreDefault(uint32_t defense) {
    for (uint32_t attack = ATK_NORMAL; attack <= ATK_HERO; ++attack)
        if (!G_AttackTypeDamageMultiplierIsDefault(attack, defense)) return false;
    return true;
}

static bool InfoPanelDamageMatchupsAreDefault(attackType_t attack) {
    for (uint32_t defense = 0; defense < 8; ++defense)
        if (!G_AttackTypeDamageMultiplierIsDefault(attack, defense)) return false;
    return true;
}

static void InfoPanelAppendMatchup(char *body, size_t body_size, cstring_t label, float multiplier) {
    size_t const used = strlen(body);
    if (used < body_size)
        snprintf(body + used, body_size - used, "\n%s  %.0f%%", label, multiplier * 100.0f);
}

static void InfoPanelAppendArmorMatchups(char *body, size_t body_size, uint32_t defense) {
    size_t const used = strlen(body);
    if (used < body_size)
        snprintf(body + used, body_size - used, "\n\n%s",
                 InfoPanelString("ARMOR_MATCHUPS", "Incoming attack multipliers:"));
    for (uint32_t attack = ATK_NORMAL; attack <= ATK_HERO; ++attack) {
        cstring_t key, fallback, tip_key, tip_fallback;
        InfoPanelDamageTypeStrings((attackType_t)attack, &key, &fallback, &tip_key, &tip_fallback);
        InfoPanelAppendMatchup(body, body_size, InfoPanelString(key, fallback),
                               G_AttackTypeDamageMultiplier(attack, defense));
    }
}

static void InfoPanelAppendDamageMatchups(char *body, size_t body_size, attackType_t attack) {
    size_t const used = strlen(body);
    if (used < body_size)
        snprintf(body + used, body_size - used, "\n\n%s",
                 InfoPanelString("DAMAGE_MATCHUPS", "Damage multipliers vs armor:"));
    for (uint32_t defense = 0; defense < 8; ++defense) {
        cstring_t key, fallback, tip_key, tip_fallback;
        InfoPanelArmorTypeStrings(defense, &key, &fallback, &tip_key, &tip_fallback);
        InfoPanelAppendMatchup(body, body_size, InfoPanelString(key, fallback),
                               G_AttackTypeDamageMultiplier(attack, defense));
    }
}

static int32_t InfoPanelDamageReduction(float armor) {
    float const coefficient = game.constants.combatConstantsLoaded
        ? game.constants.defenseArmor : 0.06f;
    float reduction;

    if (armor >= 0.0f) reduction = 1.0f - 1.0f / (1.0f + armor * coefficient);
    else reduction = 1.0f - (2.0f - powf(1.0f - coefficient, -armor));
    return (int32_t)(reduction * 100.0f + (reduction >= 0.0f ? 0.5f : -0.5f));
}

static void FormatArmorTooltip(char *tip, size_t tip_size, char *body, size_t body_size,
                               uint32_t type, float armor, float move_speed,
                               cstring_t upgrade_name, uint32_t upgrade_level) {
    cstring_t type_key, type_fallback, armor_tip_key, armor_tip_fallback;
    cstring_t armor_label = InfoPanelString("COLON_ARMOR", "Armor:");
    cstring_t reduction_label = InfoPanelString("COLON_DAMAGE_REDUCTION", "Damage Reduction:");
    cstring_t speed_label = InfoPanelString("COLON_MOVE_SPEED", "Move Speed:");
    size_t used;

    if (!tip || !tip_size || !body || !body_size) return;
    snprintf(tip, tip_size, "%s %d", armor_label, (int)roundf(armor));
    InfoPanelArmorTypeStrings(type, &type_key, &type_fallback, &armor_tip_key, &armor_tip_fallback);
    snprintf(body, body_size, "%s\n%s %d%%\n%s %s",
             InfoPanelString(type_key, type_fallback), reduction_label,
             InfoPanelDamageReduction(armor), speed_label, InfoPanelMoveSpeed(move_speed));
    used = strlen(body);
    if (upgrade_name && *upgrade_name && used < body_size) {
        cstring_t format = InfoPanelString("UPGRADE_TOOLTIP", "%s - Level %d");
        char upgrade[256];
        snprintf(upgrade, sizeof(upgrade), format, upgrade_name, (int)upgrade_level);
        snprintf(body + used, body_size - used, "\n%s %s",
                 InfoPanelString("COLON_UPGRADE", "Upgrade:"), upgrade);
        used = strlen(body);
    }
    if (!InfoPanelArmorMatchupsAreDefault(type))
        InfoPanelAppendArmorMatchups(body, body_size, type);
    else if (armor_tip_fallback && *armor_tip_fallback && used < body_size)
        snprintf(body + used, body_size - used, "\n\n%s",
                 InfoPanelString(armor_tip_key, armor_tip_fallback));
}

static void SetArmorTooltip(frameDef_t *icon, edict_t *ent, uint32_t armor_type,
                            float armor, float move_speed, uint32_t upgrade) {
    static char tip[256];
    static char body[1536];
    char armor_value[64];
    gameClient_t *owner = ent ? G_GetPlayerClientByNumber(ent->s.player) : NULL;
    uint32_t const upgrade_level = upgrade && owner && owner->ps.number == ent->s.player
        ? G_GetPlayerTechResearchedLevel(owner, upgrade) : 0;
    cstring_t const upgrade_name = upgrade ? G_ObjectName(upgrade) : NULL;

    if (!icon) return;
    if (!ent || !ent->unit_state) {
        icon->Tip = icon->Ubertip = NULL;
        return;
    }
    FormatArmorTooltip(tip, sizeof(tip), body, sizeof(body), armor_type, armor,
                       move_speed, upgrade_name, upgrade_level);
    AppendInfoPanelBonus(body, sizeof(body), "Bonus Armor",
        ent->unit_state->temporary_armor_bonus +
        unit_status_modifier_total(ent, WC3_STATUS_MOD_ARMOR_FLAT));
    FormatArmorValue(armor_value, sizeof(armor_value), ent);
    snprintf(tip, sizeof(tip), "%s %s", InfoPanelString("COLON_ARMOR", "Armor:"), armor_value);
    icon->Tip = tip;
    icon->Ubertip = body;
}

static void InfoPanelDamageTypeStrings(attackType_t type, cstring_t *label_key,
                                       cstring_t *label_fallback, cstring_t *tip_key,
                                       cstring_t *tip_fallback) {
    switch (type) {
    case ATK_NORMAL:
        *label_key = "DAMAGE_NORMAL"; *label_fallback = "Type: Normal";
        *tip_key = "DAMAGETIP_NORMAL";
        *tip_fallback = "Normal attacks do extra damage against Medium armor, and reduced damage to Fortified armor.";
        break;
    case ATK_PIERCE:
        *label_key = "DAMAGE_PIERCE"; *label_fallback = "Type: Piercing";
        *tip_key = "DAMAGETIP_PIERCE";
        *tip_fallback = "Piercing attacks do extra damage to Unarmored units and Light armor, and reduced damage to Fortified, Medium armor and Heroes.";
        break;
    case ATK_SIEGE:
        *label_key = "DAMAGE_SIEGE"; *label_fallback = "Type: Siege";
        *tip_key = "DAMAGETIP_SIEGE";
        *tip_fallback = "Siege attacks do extra damage to Fortified armor and Unarmored units, and reduced damage to Medium armor and Heroes.";
        break;
    case ATK_MAGIC:
        *label_key = "DAMAGE_MAGIC"; *label_fallback = "Type: Magic";
        *tip_key = "DAMAGETIP_MAGIC";
        *tip_fallback = "Magic attacks do extra damage against Heavy armor and Light armor, and reduced damage to Medium armor, Fortified armor and Heroes.";
        break;
    case ATK_CHAOS:
        *label_key = "DAMAGE_CHAOS"; *label_fallback = "Type: Chaos";
        *tip_key = "DAMAGETIP_CHAOS";
        *tip_fallback = "Chaos attacks do full damage to all armor types.";
        break;
    case ATK_HERO:
        *label_key = "DAMAGE_HERO"; *label_fallback = "Type: Hero";
        *tip_key = "DAMAGETIP_HERO";
        *tip_fallback = "Hero attacks do reduced damage to Fortified armor.";
        break;
    case ATK_SPELLS:
        *label_key = NULL; *label_fallback = "Type: Spells";
        *tip_key = NULL; *tip_fallback = NULL;
        break;
    default:
        *label_key = "DAMAGE_UNKNOWN"; *label_fallback = "Type: Unknown";
        *tip_key = "DAMAGETIP_UNKNOWN";
        *tip_fallback = "Unknown damage does reduced damage against unknown armor types.";
        break;
    }
}

static void FormatDamageTooltip(char *tip, size_t tip_size, char *body, size_t body_size,
                                attackType_t type, int32_t min_damage, int32_t max_damage,
                                float range, float cooldown, cstring_t upgrade_name,
                                uint32_t upgrade_level) {
    cstring_t type_key, type_fallback, damage_tip_key, damage_tip_fallback;
    cstring_t damage_label = InfoPanelString("COLON_DAMAGE", "Damage:");
    cstring_t range_label = InfoPanelString("COLON_RANGE", "Range:");
    cstring_t speed_label = InfoPanelString("COLON_SPEED", "Speed:");
    size_t used;

    if (!tip || !tip_size || !body || !body_size) return;
    snprintf(tip, tip_size, "%s %ld - %ld", damage_label, (long)min_damage, (long)max_damage);
    body[0] = '\0';
    InfoPanelDamageTypeStrings(type, &type_key, &type_fallback, &damage_tip_key, &damage_tip_fallback);
    snprintf(body, body_size, "%s\n%s %d\n%s %s", InfoPanelString(type_key, type_fallback),
             range_label, (int)(range + 0.5f), speed_label, InfoPanelAttackSpeed(cooldown));
    used = strlen(body);
    if (upgrade_name && *upgrade_name && used < body_size) {
        cstring_t format = InfoPanelString("UPGRADE_TOOLTIP", "%s - Level %d");
        char upgrade[256];
        snprintf(upgrade, sizeof(upgrade), format, upgrade_name, (int)upgrade_level);
        snprintf(body + used, body_size - used, "\n%s %s",
                 InfoPanelString("COLON_UPGRADE", "Upgrade:"), upgrade);
        used = strlen(body);
    }
    if (!InfoPanelDamageMatchupsAreDefault(type))
        InfoPanelAppendDamageMatchups(body, body_size, type);
    else if (damage_tip_fallback && *damage_tip_fallback && used < body_size)
        snprintf(body + used, body_size - used, "\n\n%s",
                 InfoPanelString(damage_tip_key, damage_tip_fallback));
}

static void SetDamageTooltip(frameDef_t *icon, edict_t *ent, attackType_t type,
                             int32_t min_damage, int32_t max_damage, float range,
                             float cooldown, uint32_t upgrade,
                             float temporary_damage_bonus, float damage_base) {
    static char tips[2][DAMAGE_TOOLTIP_TIP_SIZE];
    static char bodies[2][DAMAGE_TOOLTIP_BODY_SIZE];
    static uint32_t slot;
    char damage_value[128];
    uint32_t const current = slot++ & 1u;
    gameClient_t *owner = ent ? G_GetPlayerClientByNumber(ent->s.player) : NULL;
    uint32_t const upgrade_level = upgrade && owner && owner->ps.number == ent->s.player
        ? G_GetPlayerTechResearchedLevel(owner, upgrade) : 0;
    cstring_t const upgrade_name = upgrade ? G_ObjectName(upgrade) : NULL;

    if (!icon) return;
    if (!ent || !ent->unit_state) {
        icon->Tip = icon->Ubertip = NULL;
        return;
    }
    FormatDamageTooltip(tips[current], sizeof(tips[current]), bodies[current], sizeof(bodies[current]),
                        type, min_damage, max_damage, range, cooldown,
                        upgrade_name, upgrade_level);
    AppendDamageBonusTooltip(bodies[current], sizeof(bodies[current]), ent,
                             temporary_damage_bonus, damage_base);
    FormatAttackDamageWithStatus(damage_value, sizeof(damage_value), ent, min_damage,
                                 max_damage, temporary_damage_bonus, damage_base);
    snprintf(tips[current], sizeof(tips[current]), "%s %s",
             InfoPanelString("COLON_DAMAGE", "Damage:"), damage_value);
    icon->Tip = tips[current];
    icon->Ubertip = bodies[current];
}

#ifdef BZ_TESTS
void UI_TestFormatDamageTooltip(char *tip, size_t tip_size, char *body, size_t body_size,
                                attackType_t type, int32_t min_damage, int32_t max_damage,
                                float range, float cooldown, cstring_t upgrade_name,
                                uint32_t upgrade_level) {
    FormatDamageTooltip(tip, tip_size, body, body_size, type, min_damage, max_damage,
                        range, cooldown, upgrade_name, upgrade_level);
}

void UI_TestSetDamageTooltip(frameDef_t *icon, edict_t *ent, attackType_t type,
                             int32_t min_damage, int32_t max_damage, float range,
                             float cooldown, uint32_t upgrade,
                             float temporary_damage_bonus, float damage_base) {
    SetDamageTooltip(icon, ent, type, min_damage, max_damage, range, cooldown, upgrade,
                     temporary_damage_bonus, damage_base);
}

void UI_TestFormatArmorValue(char *buffer, size_t buffer_size, edict_t const *ent) {
    FormatArmorValue(buffer, buffer_size, ent);
}

void UI_TestFormatArmorTooltip(char *tip, size_t tip_size, char *body, size_t body_size,
                               uint32_t type, float armor, float move_speed,
                               cstring_t upgrade_name, uint32_t upgrade_level) {
    FormatArmorTooltip(tip, tip_size, body, body_size, type, armor, move_speed,
                       upgrade_name, upgrade_level);
}

void UI_TestSetArmorTooltip(frameDef_t *icon, edict_t *ent, uint32_t type,
                            float armor, float move_speed, uint32_t upgrade) {
    SetArmorTooltip(icon, ent, type, armor, move_speed, upgrade);
}

void UI_TestSetHeroAttributesTooltip(frameDef_t *icon, cstring_t primary) {
    SetHeroAttributesTooltip(icon, primary);
}

void UI_TestSetCasterRankTooltip(frameDef_t *icon, cstring_t rank_names,
                                 cstring_t rank_spells, cstring_t upgrade_names,
                                 uint32_t rank) {
    UnitProfile_t profile = { 0 };
    profile.casterUpgradeName = rank_names;
    profile.casterUpgradeTip = rank_spells;
    SetCasterRankTooltip(icon, &profile, upgrade_names, rank);
}
#endif

static void SetUpgradeLevel(frameDef_t *frame, uint32_t upgrade, edict_t *ent) {
    gameClient_t *owner;

    if (!frame) return;
    if (!upgrade || !ent || !(owner = G_GetPlayerClientByNumber(ent->s.player))) {
        UI_SetText(frame, "%s", "");
        UI_SetHidden(frame, true);
        return;
    }
    UI_SetHidden(frame, false);
    UI_SetText(frame, "%ld", (long)G_GetPlayerTechResearchedLevel(owner, upgrade));
}

static void WriteCasterRankStatusFrame(edict_t *ent) {
    UnitProfile_t const *profile;
    gameClient_t *owner;
    uint32_t upgrade;
    uint32_t rank;
    char upgrade_code[5] = { 0 };
    cstring_t upgrade_names;
    char rank_name[128] = "Initiate";

    if (!ent || !ent->data.UnitProfile || !hud.simple.SimpleInfoPanelIconRank) return;
    profile = ent->data.UnitProfile;
    if (!profile->casterUpgradeName || !*profile->casterUpgradeName) return;
    upgrade = G_GetUnitUpgradeForClass(ent, "caster");
    if (!upgrade) return;

    owner = G_GetPlayerClientByNumber(ent->s.player);
    rank = owner ? (uint32_t)G_GetPlayerTechResearchedLevel(owner, upgrade) : 0;
    memcpy(upgrade_code, &upgrade, 4);
    upgrade_names = FindConfigValue(upgrade_code, "Name");
    if (!upgrade_names || !*upgrade_names) upgrade_names = G_ObjectName(upgrade);
    InfoPanelCsvToken(profile->casterUpgradeName, rank, rank_name, sizeof(rank_name));

    if (profile->casterUpgradeArt && *profile->casterUpgradeArt)
        UI_SetTexture(hud.simple.InfoPanelIconBackdrop_3, profile->casterUpgradeArt, false);
    UI_SetText(hud.simple.InfoPanelIconLabel_3, "%s", UI_GetString("COLON_RANK"));
    UI_SetText(hud.simple.InfoPanelIconValue_3, "%s", G_LevelString(rank_name));
    SetUpgradeLevel(hud.simple.InfoPanelIconLevel_3, upgrade, ent);
    SetCasterRankTooltip(hud.simple.InfoPanelIconBackdrop_3, profile, upgrade_names, rank);
    UI_WriteFrame(&hud.rank);
    UI_WriteFrameWithChildren(hud.simple.SimpleInfoPanelIconRank, &hud.rank);
}

static cstring_t StatusBuffField(uint32_t code, cstring_t field) {
    char name[5] = { 0 };
    AbilityBuffData_t const *buff;
    cstring_t value;

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

static cstring_t TimedStatusLabel(heroabilitystatus_t const *status) {
    uint32_t buff_code;
    cstring_t tip;

    if (!status || !unit_statusshowstimedbar(status->code)) return NULL;
    buff_code = G_UnitStatusBuffCode(status);
    if (!buff_code) return NULL;
    tip = StatusBuffField(buff_code, "Bufftip");
    if (!tip || !*tip) return "";
    return UI_GetString(tip);
}

/* Hex's duration is visible on the affected unit's selected status panel,
 * including when the selected target belongs to an opponent or neutral. */
static bool TimedStatusVisibleToViewer(edict_t const *unit, gameClient_t const *viewer,
                                       heroabilitystatus_t const *status) {
    return unit && viewer && status &&
        (unit->s.player == viewer->ps.number || status->code == MAKEFOURCC('B','O','h','x'));
}

static cstring_t StatusBuffArt(uint32_t code) {
    cstring_t art;

    /* Warsmash routes timed-life-bar buffs through SimpleProgressIndicator
     * instead of the ordinary status icon strip. Cooldowns are stored
     * separately and never enter this status presentation path. */
    if (unit_statusshowstimedbar(code)) return NULL;
    art = G_AbilityBuffArt(code);
    if (!art || !*art) return NULL;
    return art;
}

static void WriteVirtualBuffStatusFrame(uint32_t buff_code, uint32_t shown[MAX_UNIT_STATUSES], uint32_t *slot) {
    bool duplicate = false;
    cstring_t art;
    cstring_t tip;
    cstring_t ubertip;
    frameDef_t *icon;

    if (!buff_code || !slot || *slot >= MAX_UNIT_STATUSES) return;
    FOR_LOOP(i, *slot) if (shown[i] == buff_code) { duplicate = true; break; }
    if (duplicate || !(art = StatusBuffArt(buff_code))) return;

    icon = &hud.buff_icon[*slot];
    UI_SetTexture(&hud.buff_tex[*slot], art, false);
    tip = StatusBuffField(buff_code, "Bufftip");
    ubertip = StatusBuffField(buff_code, "Buffubertip");
    icon->Tip = tip && *tip ? UI_GetString(tip) : NULL;
    icon->Ubertip = ubertip && *ubertip ? UI_GetString(ubertip) : NULL;
    UI_WriteFrame(icon);
    UI_WriteFrame(&hud.buff_tex[*slot]);
    shown[*slot] = buff_code;
    (*slot)++;
}

static void WriteBuffStatusFrames(edict_t *ent) {
    uint32_t slot = 0;
    uint32_t shown[MAX_UNIT_STATUSES] = { 0 };

    if (!ent || !hud.simple.SimpleInfoPanelUnitDetail) return;
    UI_SetText(&hud.buff_label, "%s", UI_GetString("COLON_STATUS"));
    UI_WriteFrame(&hud.buff_label);

    FOR_LOOP(i, ent->unit_state ? MAX_UNIT_STATUSES : 0) {
        heroabilitystatus_t const *status = ent->unit_state->statuses + i;
        cstring_t art;
        cstring_t tip;
        cstring_t ubertip;
        frameDef_t *icon;
        uint32_t const buff_code = G_UnitStatusBuffCode(status);

        if (!status->level || slot >= MAX_UNIT_STATUSES || !buff_code) continue;
        /* Auras may be represented by both their ability rawcode (AHad) and
         * their buff rawcode (BHad) in abilstatus[].  Resolve first, then emit
         * each visible buff once. */
        {
            bool duplicate = false;
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

    /* Passive auras do not own gameplay status slots. Their recipient
     * presentation is reconciled by the shared aura cadence; render authored
     * BuffIDs virtually without fabricating abilstatus[]. */
    WriteVirtualBuffStatusFrame(S_BrillianceAuraBuff(ent), shown, &slot);
    WriteVirtualBuffStatusFrame(S_DevotionAuraBuff(ent), shown, &slot);
    WriteVirtualBuffStatusFrame(S_UnholyAuraBuff(ent), shown, &slot);
    WriteVirtualBuffStatusFrame(S_EnduranceAuraBuff(ent), shown, &slot);
    WriteVirtualBuffStatusFrame(S_VampiricAuraBuff(ent), shown, &slot);
    WriteVirtualBuffStatusFrame(S_ThornsAuraBuff(ent), shown, &slot);
    WriteVirtualBuffStatusFrame(S_TrueshotAuraBuff(ent), shown, &slot);
}

static void WriteSelectedUnitStatusFrames(edict_t *ent, UnitWeapons_t const *weapons,
                                          bool has_attack1, bool has_attack2,
                                          int32_t min_damage, int32_t max_damage,
                                          int32_t min_damage2, int32_t max_damage2,
                                          bool is_hero) {
    char value[64];
    uint32_t const weapon_upgrade = UnitWeaponUpgrade(ent);
    uint32_t const armor_upgrade = UnitArmorUpgrade(ent);

    if (!hud.simple.SimpleInfoPanelUnitDetail) return;
    RefreshSimpleInfoPanelStrings();

    UI_SetPoint(&hud.armor, FRAMEPOINT_TOPLEFT, hud.simple.SimpleInfoPanelUnitDetail,
                FRAMEPOINT_TOPLEFT, 0.0f, has_attack1 ? -0.0705f : -0.0400f);
    UI_SetPoint(&hud.rank, FRAMEPOINT_TOPLEFT, hud.simple.SimpleInfoPanelUnitDetail,
                FRAMEPOINT_TOPLEFT, 0.100f, has_attack1 ? -0.0705f : -0.0400f);
    hud.simple.InfoPanelIconBackdrop->Tip = hud.simple.InfoPanelIconBackdrop->Ubertip = NULL;
    if (hud.attack2_icon_backdrop)
        hud.attack2_icon_backdrop->Tip = hud.attack2_icon_backdrop->Ubertip = NULL;

    if (has_attack1) {
        SetTypedInfoPanelIcon(hud.simple.InfoPanelIconBackdrop, "Damage", weapons->attack1.attackType,
                              weapon_upgrade != 0);
        SetDamageTooltip(hud.simple.InfoPanelIconBackdrop, ent, ent->unit_state->attack1.type,
                         min_damage, max_damage, ent->unit_state->attack1.range,
                         ent->unit_state->attack1.cooldown, weapon_upgrade,
                         ent->unit_state->attack1.temporaryDamageBonus,
                         ent->unit_state->attack1.damageBase);
        FormatAttackDamageWithStatus(value, sizeof(value), ent, min_damage, max_damage,
                                ent->unit_state->attack1.temporaryDamageBonus,
                                ent->unit_state->attack1.damageBase);
        UI_SetText(hud.simple.InfoPanelIconValue, "%s", value);
        SetUpgradeLevel(hud.simple.InfoPanelIconLevel, weapon_upgrade, ent);
        UI_WriteFrame(&hud.attack1);
        UI_WriteFrameWithChildren(hud.simple.SimpleInfoPanelIconDamage, &hud.attack1);
    }
    if (has_attack2) {
        SetTypedInfoPanelIcon(hud.attack2_icon_backdrop, "Damage", weapons->attack2.attackType,
                              weapon_upgrade != 0);
        SetDamageTooltip(hud.attack2_icon_backdrop, ent, ent->unit_state->attack2.type,
                         min_damage2, max_damage2, ent->unit_state->attack2.range,
                         ent->unit_state->attack2.cooldown, weapon_upgrade,
                         ent->unit_state->attack2.temporaryDamageBonus,
                         ent->unit_state->attack2.damageBase);
        FormatAttackDamageWithStatus(value, sizeof(value), ent, min_damage2, max_damage2,
                                ent->unit_state->attack2.temporaryDamageBonus,
                                ent->unit_state->attack2.damageBase);
        UI_SetText(hud.attack2_icon_value, "%s", value);
        SetUpgradeLevel(hud.attack2_icon_level, weapon_upgrade, ent);
        UI_WriteFrame(&hud.attack2);
        UI_WriteFrameWithChildren(hud.attack2_icon, &hud.attack2);
    }

    SetTypedInfoPanelIcon(hud.simple.InfoPanelIconBackdrop_2, "Armor", ent->data.UnitBalance->defenseType,
                          armor_upgrade != 0);
    SetArmorTooltip(hud.simple.InfoPanelIconBackdrop_2, ent,
                    ent->unit_state->defense_type, G_UnitArmorValue(ent),
                    ent->unitinfo.MoveSpeed, armor_upgrade);
    if (G_UnitHasFlag(ent, UNIT_FLAG_INVULNERABLE)) UI_SetText(hud.simple.InfoPanelIconValue_2, "%s", "|cffff0000Invulnerable|r");
    else {
        FormatArmorValue(value, sizeof(value), ent);
        UI_SetText(hud.simple.InfoPanelIconValue_2, "%s", value);
    }
    SetUpgradeLevel(hud.simple.InfoPanelIconLevel_2, armor_upgrade, ent);
    UI_WriteFrame(&hud.armor);
    UI_WriteFrameWithChildren(hud.simple.SimpleInfoPanelIconArmor, &hud.armor);
    WriteCasterRankStatusFrame(ent);

    if (ent->resources > 0) {
        cstring_t const gold_art = "InfoPanelIconGold";
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
        cstring_t const food_art = "InfoPanelIconFood";
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
        SetHeroAttributesTooltip(hud.simple.InfoPanelIconHeroIcon,
                                 ent->data.UnitBalance->primaryAttribute);
        UI_SetText(hud.simple.InfoPanelIconHeroStrengthValue, "%lu", (unsigned long)ent->unit_state->hero.str);
        UI_SetText(hud.simple.InfoPanelIconHeroAgilityValue, "%lu", (unsigned long)ent->unit_state->hero.agi);
        UI_SetText(hud.simple.InfoPanelIconHeroIntellectValue, "%lu", (unsigned long)ent->unit_state->hero.intel);
        UI_WriteFrame(&hud.hero);
        UI_WriteFrameWithChildren(hud.simple.SimpleInfoPanelIconHero, &hud.hero);
    }

    WriteBuffStatusFrames(ent);
}

static float HeroLevelProgress(edict_t *ent) {
    uint32_t level;
    uint32_t have;
    uint32_t need;

    if (!ent) return 0.0f;
    level = MAX(1u, ent->unit_state->hero.level);
    have = G_HeroXPForLevel(level);
    need = G_HeroXPForLevel(level + 1);
    if (need <= have) return 1.0f;
    if (ent->unit_state->hero.xp <= have) return 0.0f;
    return MIN(1.0f, (float)(ent->unit_state->hero.xp - have) / (float)(need - have));
}

static void WriteHeroLevelBar(frameDef_t *bar, edict_t *ent) {
    char tooltip[64];
    uint32_t const level = MAX(1u, ent->unit_state->hero.level);
    uint32_t const have = G_HeroXPForLevel(level);
    uint32_t const need = G_HeroXPForLevel(level + 1);
    uint32_t const span = need > have ? need - have : 0;
    uint32_t const earned = span && ent->unit_state->hero.xp > have
        ? MIN(ent->unit_state->hero.xp - have, span) : 0;
    cstring_t const old_tip = bar->Tip;
    cstring_t const old_ubertip = bar->Ubertip;

    snprintf(tooltip, sizeof(tooltip), "XP: %lu / %lu",
             (unsigned long)earned, (unsigned long)span);
    bar->Tip = tooltip;
    bar->Ubertip = NULL;
    UI_WriteFrameValue(bar, HeroLevelProgress(ent));
    bar->Tip = old_tip;
    bar->Ubertip = old_ubertip;
}

#ifdef BZ_TESTS
void UI_TestWriteHeroLevelBar(frameDef_t *bar, edict_t *ent) {
    WriteHeroLevelBar(bar, ent);
}
#endif

static void WriteSimpleUnitHeader(edict_t *ent, cstring_t display_name, bool is_hero, gameClient_t *viewer) {
    char class_text[128];
    frameDef_t *unit_action_label;
    heroabilitystatus_t const *timed_status = NULL;
    cstring_t timed_label = NULL;
    cstring_t unit_name;
    cstring_t class_format;
    bool old_hero_hidden;

    if (!hud.simple.SimpleInfoPanelUnitDetail) return;
    /* SimpleInfoPanel.fdf supplies a building-action placeholder on this
     * shared unit tree. Ordinary units do not use it; clear it before the
     * tree is serialized so the retail placeholder cannot leak into the HUD. */
    unit_action_label = UI_FindChildFrame(hud.simple.SimpleInfoPanelUnitDetail,
                                          "SimpleBuildingActionLabel");
    if (unit_action_label) {
        UI_SetText(unit_action_label, "%s", "");
        UI_SetHidden(unit_action_label, true);
    }
    UI_SetText(hud.simple.SimpleNameValue, "%s", display_name ? display_name : "");
    unit_name = G_UnitName(ent->class_id);

    /* UI_SendInfoPanel guarantees single-selection. Most timed statuses remain
     * owner-only; Hex is visible on the affected unit's selected panel too. */
    if (viewer) {
        timed_status = unit_findtimedbarstatus(ent);
        if (!TimedStatusVisibleToViewer(ent, viewer, timed_status)) timed_status = NULL;
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
                     (unsigned)MAX(1u, ent->unit_state->hero.level), unit_name);
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
        WriteHeroLevelBar(hud.simple.SimpleHeroLevelBar, ent);
    } else {
        UI_SetHidden(hud.simple.SimpleHeroLevelBar, true);
    }
}

uint32_t UI_WriteBuildingQueueShell(edict_t *ent, cstring_t action_key, bool show_queue_slots) {
    cstring_t name;

    if (!ent) return 0;
    if (!hud.simple.SimpleInfoPanelUnitDetail) return 0;

    name = G_UnitName(ent->class_id);
    UI_SetText(hud.simple.SimpleBuildingNameValue, "%s", name);
    UI_SetText(hud.simple.SimpleBuildingDescriptionValue, "%s", "");
    UI_SetHidden(hud.simple.SimpleBuildingDescriptionValue, true);
    UI_SetText(hud.simple.SimpleBuildingActionLabel, "%s", UI_GetString(action_key ? action_key : "TRAINING"));
    UI_SetHidden(hud.simple.SimpleBuildTimeIndicator, false);
    UI_SetHidden(hud.simple.SimpleBuildQueueBackdrop, !show_queue_slots);

    UI_WriteFrame(&hud.bottom);
    UI_WriteFrameWithChildren(hud.simple.SimpleInfoPanelBuildingDetail, &hud.bottom);
    return UI_GetWrittenFrameNumber(hud.simple.SimpleBuildTimeIndicator);
}

void UI_WriteSingleInfo(edict_t *ent, gameClient_t *viewer) {
    cargo_t *hold = G_GetCargo(ent);
    UnitBalance_t const *balance = ent->data.UnitBalance;
    UnitWeapons_t const *weapons = ent->data.UnitWeapons;
    cstring_t name = ent->unit_state && ent->unit_state->proper_name && *ent->unit_state->proper_name
        ? ent->unit_state->proper_name
        : G_LevelString(G_UnitProfile(ent->class_id)->properNames);
    cstring_t unit_name = G_UnitName(ent->class_id);
    bool const is_hero = balance->strength > 0 || balance->agility > 0 || balance->intelligence > 0;
    uint32_t level = is_hero && ent->unit_state->hero.level > 0 ? ent->unit_state->hero.level
                                                 : MAX(1, balance->level);
    int32_t dice = ent->unit_state->attack1.numberOfDice;
    bool has_attack1 = dice > 0 && (!S_WarClubActive(ent) || S_UnitAttackSlotEnabled(ent, 0));
    int32_t min_damage = has_attack1 ? MAX(0, (int32_t)(ent->unit_state->attack1.damageBase + dice)) : 0;
    int32_t max_damage = has_attack1 ? MAX(0, (int32_t)(ent->unit_state->attack1.damageBase + dice * ent->unit_state->attack1.sidesPerDie)) : 0;
    int32_t dice2 = ent->unit_state->attack2.numberOfDice;
    bool has_attack2 = dice2 > 0 && (S_WarClubActive(ent) ? S_UnitAttackSlotEnabled(ent, 1) : UI_HasSecondAttack(weapons));
    int32_t min_damage2 = has_attack2 ? MAX(0, (int32_t)(ent->unit_state->attack2.damageBase + dice2)) : 0;
    int32_t max_damage2 = has_attack2 ? MAX(0, (int32_t)(ent->unit_state->attack2.damageBase + dice2 * ent->unit_state->attack2.sidesPerDie)) : 0;

    if (!name || !*name) name = unit_name;

    if (hud.simple.SimpleInfoPanelUnitDetail) {
        /* SimpleNameValue owns the Warcraft title font/anchors. Ordinary units
         * have no synthetic "Level N <type>" line; Heroes use the XP bar in
         * that slot instead of a duplicate level/class label. */
        HideLegacyUnitStats();
        WriteSimpleUnitHeader(ent, is_hero ? name : unit_name, is_hero, viewer);

        /* Warsmash replaces the ordinary damage/armor/stat presentation with
         * transport slots whenever a cargo holder contains units.  Capacity
         * comes from the holder ability (Abun is four in standard data), so
         * custom maps can author a different number of visible slots. The
         * stock FDF has no slot frame definitions, so these native proxy
         * frames use inline authored values as permitted for native controls. */
        if (hold && hold->count > 0 && S_CargoCapacity(ent) > 0) {
            uint32_t const capacity = S_CargoCapacity(ent);
            cstring_t const slot_art = Theme_String("CargoBackdrop", NULL);
            uint32_t slot_image;

            if (!slot_art || !*slot_art) {
                fprintf(stderr, "UI_WC3: missing CargoBackdrop texture for cargo panel\n");
                return;
            }
            slot_image = gi.ImageIndex(slot_art);
            if (!slot_image) {
                fprintf(stderr, "UI_WC3: failed to load CargoBackdrop texture \"%s\"\n", slot_art);
                return;
            }

            FOR_LOOP(i, capacity) {
                float const x = 0.310f + (float)i * (0.02671875f * 1.20f);
                edict_t *occupant = S_CargoUnitAt(ent, i);
                uiFrame_t backdrop = { .flags = { .type = FT_TEXTURE }, .tex = { .index = slot_image },
                                       .color = COLOR32_WHITE };

                UI_SetFrameRect(&backdrop, x, 0.5000390625f, 0.02671875f, 0.02671875f);
                UI_WriteProxyFrame(&backdrop, NULL, 0);
                if (occupant) {
                    uiFrame_t frame;
                    char command[64];
                    cstring_t art = FindConfigValue(GetClassName(occupant->class_id), STR_ART);
                    cstring_t tip = G_UnitName(occupant->class_id);

                    if (!art || !*art) {
                        fprintf(stderr, "UI_WC3: missing cargo art for unit %s\n", GetClassName(occupant->class_id));
                        continue;
                    }
                    /* A direct path is already authoritative; Theme_String returns it unchanged when no skin key
                     * applies. */
                    art = G_UIArtPath(art);
                    if (!art || !*art) {
                        fprintf(stderr, "UI_WC3: unresolved cargo art key for unit %s\n",
                                GetClassName(occupant->class_id));
                        continue;
                    }

                    memset(&frame, 0, sizeof(frame));
                    frame.flags.type = FT_COMMANDBUTTON;
                    frame.color = COLOR32_WHITE;
                    frame.tex.index = gi.ImageIndex(art);
                    if (!frame.tex.index) {
                        fprintf(stderr, "UI_WC3: failed to load cargo art \"%s\" for unit %s\n", art,
                                GetClassName(occupant->class_id));
                        continue;
                    }
                    frame.tooltip = tip && *tip ? tip : GetClassName(occupant->class_id);
                    snprintf(command, sizeof(command), "cargounload %u", (unsigned)i);
                    frame.onclick = command;
                    UI_SetFrameRect(&frame, x, 0.5000390625f, 0.02671875f, 0.02671875f);
                    UI_WriteProxyFrame(&frame, NULL, 0);
                }
            }
            return;
        }
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

#define WC3_MULTISELECT_CLASSIC_CAPACITY 12 // units; the original 6x2 icon grid, larger selections need the 8x3 compact grid

void UI_WriteMultiselect(edict_t * *ents, uint32_t count, gameClient_t *viewer) {
    edict_t *focused = viewer ? G_GetMainSelectedUnit(viewer) : NULL;
    cstring_t highlight = Theme_String("SelectedSubgroupHighlight", NULL);

    if (count > G_SelectionLimit()) count = G_SelectionLimit();
    uint32_t size = sizeof(uiMultiselect_t) + sizeof(uiMultiselectItem_t) * count;
    uint8_t *buffer = gi.MemAlloc(size);
    uiMultiselect_t *multi = (uiMultiselect_t *)buffer;
    uiFrame_t frame;

    memset(buffer, 0, size);
    multi->hp_bar = gi.ImageIndex("SimpleHpBarConsole");
    multi->mana_bar = gi.ImageIndex("SimpleManaBarConsole");
    multi->focus_highlight = highlight && *highlight ? gi.ImageIndex(highlight) : 0;
    /* The 8x3 Reforged-style layout fits inside the existing info panel.
     * Keep original spacing at the classic capacity to preserve the classic HUD. */
    bool const compact = count > WC3_MULTISELECT_CLASSIC_CAPACITY;
    multi->offset = compact ? MAKE(vec2_t, 0.022f, 0.038f) : MAKE(vec2_t, 0.031f, 0.050f);
    multi->numcolumns = compact ? 8 : 6;
    multi->numitems = count;
    FOR_LOOP(i, count) {
        multi->items[i].entity = ents[i]->s.number;
        multi->items[i].image = gi.ImageIndex(
            G_UIArtPath(FindConfigValue(GetClassName(ents[i]->class_id), STR_ART)));
        /* Warsmash highlights the whole selected subgroup: units group by
         * unit type, even though clicking one icon chooses a concrete focus. */
        if (focused && ents[i]->class_id == focused->class_id)
            multi->items[i].flags |= UI_MULTISELECT_ITEM_FOCUSED;
    }

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_MULTISELECT;
    frame.color = COLOR32_WHITE;
    UI_SetFrameRect(&frame, 0.314f, 0.500f, compact ? 0.019f : 0.025f,
                    compact ? 0.019f : 0.025f);
    UI_WriteProxyFrame(&frame, buffer, size);
    gi.MemFree(buffer);
}

static bool UI_UsesBuildingQueuePanel(gameClient_t *viewer, edict_t *unit) {
    if (!viewer || !unit || !G_UnitIsBuilding(unit->class_id))
        return false;
    if (!G_UnitCanControl(viewer, unit))
        return false;
    return G_GetConstruction(unit) || G_BuildingUpgradeActive(unit) || unit->build != NULL;
}

#ifdef BZ_TESTS
bool UI_TestUsesBuildingQueuePanel(gameClient_t *viewer, edict_t *unit) {
    return UI_UsesBuildingQueuePanel(viewer, unit);
}
#endif

void UI_SeedInfoPanelCache(edict_t *ent, edict_t * *selected, uint32_t count) {
    if (!ent->client) return;
    if (count == 1 && !UI_UsesBuildingQueuePanel(ent->client, selected[0])) {
        ent->client->infopanel.entity = selected[0]->s.number;
        ent->client->infopanel.count = 0;
        ent->client->infopanel.xp = (int32_t)(selected[0]->unit_state ? selected[0]->unit_state->hero.xp : 0);
    } else {
        ent->client->infopanel.entity = 0;
        /* HP/mana no longer drive the live portrait bars. Reuse hp as the
         * non-single selection-count cache so async removals (death/fog/hide)
         * can invalidate a still-multiselect info panel. -1 distinguishes
         * the building queue from an empty selection. */
        ent->client->infopanel.count = count == 1 ? -1 : (int32_t)count;
        ent->client->infopanel.xp = 0;
    }
}

void UI_SendInfoPanel(edict_t *ent, edict_t * *selected, uint32_t count) {
    UI_WriteStart(LAYER_INFOPANEL);
    if (count == 1) {
        bool const queue_panel = UI_UsesBuildingQueuePanel(ent->client, selected[0]);
        if (queue_panel) {
            UI_WriteBuildQueue(selected[0], ent->client);
        } else {
            UI_WriteSingleInfo(selected[0], ent->client);
        }
        /* Tooltip-bearing status icons and cargo buttons live in the info-panel
         * layer.  The client draws FT_TOOLTIPTEXT only from the layer that owns
         * the hovered source, so keep one shared presenter in this layer rather
         * than special-casing individual info-panel modes. */
        UI_WriteTooltipFrame();
    } else if (count > 1) {
        UI_WriteMultiselect(selected, count, ent->client);
    }
    UI_WriteEnd(ent);
    UI_SeedInfoPanelCache(ent, selected, count);
}

static uint32_t SelectedUnits(gameClient_t *client, edict_t * *out, uint32_t max_out) {
    return G_GetOrderedSelectedUnits(client, out, max_out);
}

void Get_Commands_f(edict_t *ent) {
    edict_t *selected = ent && ent->client ? G_GetMainSelectedUnit(ent->client) : NULL;
    gameClient_t *previous_ui_client;
    gameCommandButton_t buttons[12];
    gameCommandButton_t unit_buttons[12];
    uint8_t count;

    if (!ent || !ent->client) return;
    G_UpdateShopPatronEffect(ent->client);
    G_UpdateRallyIndicator(ent->client);
    ent->client->commands_dirty = false;
    /* A command-card rebuild replaces server-owned build targeting. Clear the
     * cursor through the build subsystem before the generic menu reset loses
     * the callback that identifies the active placement mode. */
    G_ClearBuildPlacementMode(ent);
    /* The command bar can also be rebuilt while item-owned building
     * targeting is active (selection change, Escape, interrupted order). */
    if (ent->client->menu.on_location_selected) {
        S_AbilityTargetModeExit(ent);
        gi.Write(PF_BYTE, &(int32_t){svc_cursor});
        gi.Write(PF_ENTITY, &(entityState_t){0});
        gi.unicast(ent);
    }
    memset(&ent->client->menu, 0, sizeof(ent->client->menu));
    if (!selected || (!G_UnitCanControl(ent->client, selected) &&
                      !G_CanUseItemShop(ent->client, selected) &&
                      !G_CanUseUnitShop(ent->client, selected))) {
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
    if (G_CanUseItemShop(ent->client, selected) || G_CanUseUnitShop(ent->client, selected)) {
        uint8_t const shop_count = G_GetShopButtons(&(shopItemButtonsParams_t){
            .client = ent->client, .shop = selected, .buttons = buttons, .max_buttons = 12 });
        uint8_t const unit_count = G_GetCommandButtons(selected, unit_buttons,
                                                        (uint8_t)(12 - shop_count));
        count = shop_count;
        FOR_LOOP(i, unit_count) {
            buttons[count] = unit_buttons[i];
            count++;
        }
    } else {
        count = G_GetCommandButtons(selected, buttons, 12);
    }
    /* Resource-consuming buttons must agree with the authoritative advanced-
     * sharing check. Ordinary orders and hero skill points remain usable with
     * basic control; production, research, upgrades, revival and the Build
     * submenu (ui_builds is gated the same way) do not. */
    if (!G_UnitCanSpendResources(ent->client, selected)) {
        UnitProfile_t const *profile = G_UnitProfile(selected->class_id);
        FOR_LOOP(i, count) {
            bool production = buttons[i].building_upgrade != 0 ||
                              !strncmp(buttons[i].command, "revive:", 7) ||
                              !strcmp(buttons[i].command, STR_CmdCancelBuild) ||
                              !strcmp(buttons[i].command, STR_CmdBuild) ||
                              (!strcmp(buttons[i].command, STR_CmdCancel) &&
                               selected->build && G_HeroIsReviving(selected->build));
            if (profile && strlen(buttons[i].command) == 4) {
                cstring_t const lists[] = {profile->trains, profile->researches,
                                          profile->upgrade};
                FOR_LOOP(j, 3) {
                    if (!lists[j]) continue;
                    PARSE_LIST(lists[j], item, parse_segment) {
                        if (strlen(item) == 4 && !memcmp(item, buttons[i].command, 4)) {
                            production = true;
                            break;
                        }
                    }
                    if (production) break;
                }
            }
            if (production) buttons[i].disabled = 1;
        }
    }
    FOR_LOOP(i, count) {
        UI_WriteCommandButtonFrame(&buttons[i]);
    }
    if (count) UI_WriteTooltipFrame();
    UI_WriteEnd(ent);
    UI_SetCurrentClient(previous_ui_client);
}

static void WritePortraitFrame(edict_t *ent) {
    uiFrame_t frame;
    char command[64];

    if (!ent || !ent->s.model) return;
    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_PORTRAIT;
    frame.color = COLOR32_WHITE;
    frame.tex.index = ent->s.model;
    frame.stat = G_GetUnitTeamColor(ent);
    frame.text = G_UnitResponseTalking(ent) ? "Portrait Talk" : "Portrait";
    snprintf(command, sizeof(command), "+portraitcamera %u", (unsigned)ent->s.number);
    frame.onclick = command;
    UI_SetFrameRect(&frame, WC3_HUD_PORTRAIT_X, WC3_HUD_PORTRAIT_Y,
                    WC3_HUD_PORTRAIT_WIDTH, WC3_HUD_PORTRAIT_HEIGHT);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

static color32_t PortraitHealthColor(edict_t *ent) {
    float ratio;
    float red;
    float green;

    if (!ent || ent->health.max_value <= 0.0f) return MAKE(color32_t, 255, 0, 0, 255);
    ratio = MAX(0.0f, MIN(1.0f, ent->health.value / ent->health.max_value));
    red = MIN(1.0f, 2.0f - ratio * 2.0f);
    green = MIN(1.0f, ratio * 2.0f);
    return MAKE(color32_t,
                (uint8_t)(red * 255.0f + 0.5f),
                (uint8_t)(green * 255.0f + 0.5f),
                0, 255);
}

static void WritePortraitText(cstring_t text, color32_t color, float bottom, uint32_t stat) {
    uiFrame_t frame;
    uiLabel_t label;

    memset(&frame, 0, sizeof(frame));
    memset(&label, 0, sizeof(label));
    frame.flags.type = FT_STRING;
    frame.text = text && *text ? text : " ";
    frame.color = color;
    frame.stat = stat;
    frame.textLength = 20; /* Warsmash UnitPortraitTextTemplate */
    frame.size.width = 0.0835f; /* Keep 9999 / 9999 inside the portrait width. */
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

static void WritePortraitStats(edict_t *ent) {
    char health[32];
    char mana[32];
    int32_t hp;
    int32_t max_hp;
    int32_t mp;
    int32_t max_mp;

    if (!ent) return;
    hp = (int32_t)MAX(0.0f, ent->health.value);
    max_hp = (int32_t)MAX(0.0f, ent->health.max_value);
    mp = (int32_t)MAX(0.0f, ent->mana.value);
    max_mp = (int32_t)MAX(0.0f, ent->mana.max_value);

    snprintf(health, sizeof(health), "%ld / %ld", (long)hp, (long)max_hp);
    if (max_mp > 0)
        snprintf(mana, sizeof(mana), "%ld / %ld", (long)mp, (long)max_mp);
    else
        mana[0] = '\0';

    /* Warsmash's UnitPortrait places the stat strings in the 0.029-high strip
     * below the model: HP at BOTTOM + 0.014 and mana at BOTTOM - 0.0005.
     * OpenRealm keeps the portrait runtime-authored because SmashUI is not a
     * retail MPQ FDF, but uses the same final WC3-space geometry. */
    /* Move both baselines up by roughly two pixels while preserving their gap. */
    if (!G_UnitHasFlag(ent, UNIT_FLAG_INVULNERABLE))
        WritePortraitText(health, PortraitHealthColor(ent), 0.584f, UI_STAT_SELECTION_HEALTH_TEXT);
    WritePortraitText(mana, COLOR32_WHITE, 0.5985f, UI_STAT_SELECTION_MANA_TEXT);
}

void UI_WriteSelectedPortraitLayer(edict_t *ent) {
    edict_t *selected[MAX_SELECTED_ENTITIES];
    edict_t *focused;
    uint32_t count;

    if (!ent || !ent->client) return;
    count = SelectedUnits(ent->client, selected, MAX_SELECTED_ENTITIES);
    focused = count ? G_GetMainSelectedUnit(ent->client) : NULL;
    if (!focused && count) focused = selected[0];

    UI_WriteStart(LAYER_PORTRAIT);
    if (focused) {
        WritePortraitFrame(focused);
        WritePortraitStats(focused);
    }
    UI_WriteEnd(ent);
}

static void WriteInventoryCharge(float x, float y, float w, float h, uint32_t charges) {
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
static void WriteInventoryCover(edict_t *player) {
    static FRAMEDEF frame;
    cstring_t art = "ConsoleInventoryCoverTexture";

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

static void WriteInventoryNoCapacitySlot(uint8_t slot, cstring_t art) {
    float bx = UI_BASE_WIDTH * 0.5f + 0.1315f + (float)(slot % 2) * 0.0394f;
    float by = UI_BASE_HEIGHT - 0.0971f + (float)(slot / 2) * 0.0384f;
    UI_WriteTextureFrame(bx - 0.0165f, by - 0.0165f, 0.033f, 0.033f, art);
}

static void WriteInventoryTitle(void) {
    uiFrame_t frame = { 0 };
    uiLabel_t label = { 0 };

    frame.flags.type = FT_STRING;
    frame.text = UI_GetString("INVENTORY");
    frame.color = MAKE(color32_t, 252, 222, 18, 255);
    label.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", 11);
    label.textalignx = FONT_JUSTIFYCENTER;
    label.textaligny = FONT_JUSTIFYMIDDLE;
    UI_SetFrameRect(&frame, 0.516f, 0.4684375f, 0.071f, 0.01125f);
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

static void WriteInventory(edict_t *player, edict_t *ent, bool disabled) {
    gameInventoryItem_t items[MAX_INVENTORY];
    uint32_t capacity = G_InventoryCapacity(ent);
    uint8_t count;

    if (!capacity) { WriteInventoryCover(player); return; }
    WriteInventoryTitle();
    if (capacity < MAX_INVENTORY) {
        cstring_t art = "ConsoleInventoryNoCapacity";
        if (!art || !*art) {
            fprintf(stderr, "WriteInventory: missing ConsoleInventoryNoCapacity for player skin\n");
            return;
        }
        for (uint32_t slot = capacity; slot < MAX_INVENTORY; slot++) WriteInventoryNoCapacitySlot((uint8_t)slot, art);
    }

    count = G_GetInventory(ent, items, MAX_INVENTORY);
    FOR_LOOP(i, count) {
        float bx = UI_BASE_WIDTH * 0.5f + 0.1315f + (float)(items[i].slot % 2) * 0.0394f;
        float by = UI_BASE_HEIGHT - 0.0971f + (float)(items[i].slot / 2) * 0.0384f;
        uiFrame_t frame;
        char onclick[128];
        char onrightclick[128];
        char tooltip[1024];
        memset(&frame, 0, sizeof(frame));
        frame.flags.type = FT_COMMANDBUTTON;
        frame.color = disabled ? MAKE(color32_t, 128, 128, 128, 255) : COLOR32_WHITE;
        frame.tex.index = gi.ImageIndex(items[i].art);
        if (disabled) frame.flagsvalue |= UIFRAME_DISABLED;
        UI_FormatTooltip("", items[i].tooltip, items[i].ubertip, 0, tooltip, sizeof(tooltip));
        frame.tooltip = tooltip;
        if (!disabled) {
            snprintf(onclick, sizeof(onclick), "inventory %u", (unsigned)items[i].slot);
            snprintf(onrightclick, sizeof(onrightclick), "itemdrag %u", (unsigned)items[i].slot);
            frame.onclick = onclick;
        }
        /* Command-button text is the existing secondary/right-click command
         * channel (also used by autocast). Inventory items use it to begin the
         * Warsmash-style hold-item cursor interaction. */
        frame.text = disabled ? NULL : onrightclick;
        UI_SetFrameRect(&frame, bx - 0.0165f, by - 0.0165f, 0.033f, 0.033f);
        UI_WriteProxyFrame(&frame, NULL, 0);
        WriteInventoryCharge(bx - 0.0165f, by - 0.0165f, 0.033f, 0.033f, items[i].charges);
    }
    if (count) UI_WriteTooltipFrame();
}

static void UI_SendInventoryLayer(edict_t *ent, edict_t * *selected, uint32_t count) {
    edict_t *focused = count > 0 && ent && ent->client ? G_GetMainSelectedUnit(ent->client) : NULL;
    bool shop_mode = focused && (G_CanUseItemShop(ent->client, focused) ||
                                 G_CanUseUnitShop(ent->client, focused));

    (void)selected;
    /* Neutral shops borrow the selected patron's inventory presentation while
     * the building itself remains the world selection, matching Warcraft's
     * shop flow without granting command authority over the neutral unit. */
    if (focused && G_CanUseItemShop(ent->client, focused)) {
        focused = G_FindShopPatron(ent->client, focused);
    }
    UI_WriteStart(LAYER_INVENTORY);
    if (focused) WriteInventory(ent, focused, shop_mode);
    UI_WriteEnd(ent);
}

void G_RefreshInventoryLayer(edict_t *ent) {
    edict_t *selected[MAX_SELECTED_ENTITIES];
    uint32_t count;

    if (!ent || !ent->client) return;
    count = SelectedUnits(ent->client, selected, MAX_SELECTED_ENTITIES);
    UI_SendInventoryLayer(ent, selected, count);
}

void Get_Portrait_f(edict_t *ent) {
    edict_t *selected[MAX_SELECTED_ENTITIES];
    uint32_t count;

    if (!ent || !ent->client) return;
    count = SelectedUnits(ent->client, selected, MAX_SELECTED_ENTITIES);

    /* A normal-game transmission temporarily owns LAYER_PORTRAIT. Selection
     * still updates the info/inventory panels, but the talking head remains
     * authoritative until the transmission ends. */
    UI_WriteDialoguePresentation(ent);

    UI_SendInfoPanel(ent, selected, count);
    UI_SendInventoryLayer(ent, selected, count);
}

void G_InvalidateUnitInfoPanel(edict_t *unit) {
    if (!unit) return;
    FOR_LOOP(i, game.max_clients) {
        gameClient_t *client = game.clients + i;
        if (client->connected && G_IsEntitySelected(client, unit)) {
            /* -1 means the queue layer was already serialized. Preserve the
             * existing cache representation, but make that queue panel dirty
             * so an upgrade/construction state change is sent next frame. */
            if (client->infopanel.count == -1) {
                client->infopanel.count = 0;
            } else if (client->infopanel.entity == unit->s.number) {
                /* UINT32_MAX is outside the entity-number range. Reuse the
                 * entity cache key to invalidate only a currently displayed
                 * single-unit panel without changing the save layout. */
                client->infopanel.entity = UINT32_MAX;
            }
        }
    }
}

/* The portrait layer is authored separately from the info panel and is not
 * rebuilt by G_UpdateClientInfoPanels(). In-place type changes therefore have
 * to dirty the selected-unit presentation explicitly so G_RunClients() emits
 * the new model on the next server frame. Keep this deferred rather than
 * writing svc_layout from inside gameplay state mutation. */
void G_InvalidateUnitPortrait(edict_t *unit) {
    if (!unit) return;
    FOR_LOOP(i, game.max_clients) {
        gameClient_t *client = game.clients + i;
        if (client->connected && G_IsEntitySelected(client, unit))
            client->presentation_dirty = true;
    }
}

static uint16_t SelectedPortraitStat(float value) {
    int32_t whole = (int32_t)MAX(0.0f, value); /* Warsmash FastNumberFormat truncates */
    return (uint16_t)MIN(whole, USHRT_MAX);
}

static uint16_t SelectedTimedStatusStat(gameClient_t *client, edict_t *selected) {
    heroabilitystatus_t const *status;
    float fraction;

    if (!client || !selected) return 0;
    status = unit_findtimedbarstatus(selected);
    if (!TimedStatusVisibleToViewer(selected, client, status)) return 0;
    fraction = unit_statusremainingfraction(status);
    return (uint16_t)MIN((uint32_t)(fraction * (float)USHRT_MAX + 0.5f), (uint32_t)USHRT_MAX);
}

#ifdef BZ_TESTS
uint16_t UI_TestSelectedTimedStatusStat(gameClient_t *client, edict_t *selected) {
    return SelectedTimedStatusStat(client, selected);
}
#endif

static void UpdateSelectedLiveStats(gameClient_t *client, edict_t *selected) {
    uint16_t old_timed;
    uint16_t new_timed;
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
        uint32_t const old_bucket = ((uint32_t)old_timed * 10u) / USHRT_MAX;
        uint32_t const new_bucket = ((uint32_t)new_timed * 10u) / USHRT_MAX;
        if (debug >= 3 || old_timed == 0 || new_timed == 0 || old_bucket != new_bucket) {
            fprintf(stderr,
                    "WC3_TIMED_STATUS server publish player=%u unit=%u old=%u new=%u fraction=%.4f\n",
                    (unsigned)client->ps.number, (unsigned)selected->s.number,
                    (unsigned)old_timed, (unsigned)new_timed,
                    new_timed / (float)USHRT_MAX);
        }
    }
}

/* Keep selected-unit HP/mana and the normalized timed-status fraction in
 * playerState so live bars/text update through ordinary snapshots instead of
 * forcing a whole FDF layer resend every server frame. Re-send LAYER_INFOPANEL
 * only when its static presentation (selection, timer eligibility/label, XP) changes. */
void G_RefreshInfoPanel(edict_t *ent) {
    edict_t *selected[MAX_SELECTED_ENTITIES];
    uint32_t count;
    bool queue_panel;

    if (!ent || !ent->client) return;
    count = SelectedUnits(ent->client, selected, MAX_SELECTED_ENTITIES);
    UpdateSelectedLiveStats(ent->client, count ? G_GetMainSelectedUnit(ent->client) : NULL);
    if (count != 1) {
        if (ent->client->infopanel.entity == 0 &&
            ent->client->infopanel.count == (int32_t)count) {
            return;
        }
        UI_SendInfoPanel(ent, selected, count);
        return;
    }

    queue_panel = UI_UsesBuildingQueuePanel(ent->client, selected[0]);
    if (queue_panel) {
        /* A selected building can enter construction or an in-place upgrade
         * without a new selection event. The queue-panel cache is 0/-1 only
         * after its layer has actually been serialized; any other cache state
         * needs the transition payload before the live queue timer can update. */
        /* The client-side queue timer advances from its serialized timestamps.
         * Rebuild them while Unsummon owns the building so a paused training
         * queue remains visually paused instead of continuing on the client. */
        if (G_BuildingIsUnsummoning(selected[0]) ||
            ent->client->infopanel.entity != 0 || ent->client->infopanel.count != -1) {
            UI_SendInfoPanel(ent, selected, count);
        }
        return;
    }
    /* HP/mana/timed-status progress are live player-state bindings, so changing
     * their values must not force LAYER_INFOPANEL/FDF reserialization every frame. */
    if (selected[0]->s.number == ent->client->infopanel.entity &&
        (int32_t)(selected[0]->unit_state ? selected[0]->unit_state->hero.xp : 0) == ent->client->infopanel.xp) {
        return;
    }
    UI_SendInfoPanel(ent, selected, count);
}

/* Once per server frame, keep every connected player's info panel in sync.
 * Client edicts live in the reserved [0, max_clients) range and are not normal
 * in-use world entities, so do not gate this on edict->inuse. */
void G_UpdateClientInfoPanels(void) {
    FOR_LOOP(i, game.max_clients) {
        gameClient_t *client = game.clients + i;
        edict_t *ent;

        if (!client->connected) continue;
        ent = G_GetPlayerEntityByNumber(client->ps.number);
        if (ent && ent->client == client) G_RefreshInfoPanel(ent);
    }
}

/* Re-send LAYER_CONSOLE only when resource display/tooltip state changed. */
void G_RefreshResourceBar(edict_t *ent) {
    player_t *ps;
    int32_t gold, lumber, food_u, food_c, gold_rate, lumber_rate;

    if (!ent || !ent->client) return;
    ps          = &ent->client->ps;
    gold        = (int32_t)ps->stats[PLAYERSTATE_RESOURCE_GOLD];
    lumber      = (int32_t)ps->stats[PLAYERSTATE_RESOURCE_LUMBER];
    food_u      = (int32_t)ps->stats[PLAYERSTATE_RESOURCE_FOOD_USED];
    food_c      = G_GetEffectiveFoodCap(ent->client);
    gold_rate   = (int32_t)ps->stats[PLAYERSTATE_GOLD_UPKEEP_RATE];
    lumber_rate = (int32_t)ps->stats[PLAYERSTATE_LUMBER_UPKEEP_RATE];

    if (ent->client->quest_until <= level.time) ent->client->quest_until = 0;
    if (ent->client->quest_until == ent->client->resourcebar.quest_until &&
        ent->client->canvas == ent->client->resourcebar.canvas &&
        gold        == ent->client->resourcebar.gold        &&
        lumber      == ent->client->resourcebar.lumber      &&
        food_u      == ent->client->resourcebar.food_used   &&
        food_c      == ent->client->resourcebar.food_cap    &&
        gold_rate   == ent->client->resourcebar.gold_rate   &&
        lumber_rate == ent->client->resourcebar.lumber_rate)
        return;

    /* Allied Team Resources reads this player's economy; refresh eligible
     * viewers on real changes, not on every frame.  Computer and departed
     * owners have no console to redraw, but their cached values still feed
     * the panels of allies holding advanced control. */
    G_MarkMultiboardPlayerDirty(ps->number);

    if (ent->client->connected) {
        UI_WriteStart(LAYER_CONSOLE);
        UI_WriteConsoleBackdrop(ent->client, food_u, food_c);
        UI_WriteMinimapFrame();
        UI_WriteEnd(ent);
    }

    ent->client->resourcebar.quest_until = ent->client->quest_until;
    ent->client->resourcebar.canvas      = ent->client->canvas;
    ent->client->resourcebar.gold        = gold;
    ent->client->resourcebar.lumber      = lumber;
    ent->client->resourcebar.food_used   = food_u;
    ent->client->resourcebar.food_cap    = food_c;
    ent->client->resourcebar.gold_rate   = gold_rate;
    ent->client->resourcebar.lumber_rate = lumber_rate;
}

/* Reserved player edicts are connected clients, not inuse world units. */
/* Every player slot, not only connected clients: the compare pass is what
 * notices a computer ally's spending for Team Resources. */
void G_UpdateClientResourceBars(void) {
    FOR_LOOP(i, game.max_clients)
        G_RefreshResourceBar(G_GetPlayerEntityByNumber(game.clients[i].ps.number));
}
