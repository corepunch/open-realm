/*
 * hud_infopanel.c — Info panel, multiselect, and per-frame update stubs.
 *
 * Builds the single-unit info panel (name, level, damage, armor, hero
 * attributes, XP bar, HP/mana), the multi-select grid, and the
 * build-queue overlay.  Also contains the stubbed entry points that
 * console_ui.c now handles client-side.
 */

#include "hud_local.h"
#include "../generated/info_panel_unit_detail.h"
#include "../generated/info_panel_building_detail.h"
#include "../generated/simple_info_panel.h"

static InfoPanelUnitDetail_t unit_panel;
static InfoPanelBuildingDetail_t building_panel;
static SimpleInfoPanel_t simple_panel;
static FRAMEDEF bottom_panel;
static FRAMEDEF attack1_wrapper;
static FRAMEDEF attack2_wrapper;
static FRAMEDEF armor_wrapper;
static FRAMEDEF hero_wrapper;
static FRAMEDEF food_wrapper;
static FRAMEDEF buff_status_label;
static FRAMEDEF buff_status_icons[MAX_UNIT_STATUSES];
static FRAMEDEF buff_status_icon_textures[MAX_UNIT_STATUSES];
static LPFRAMEDEF attack2_icon;
static LPFRAMEDEF attack2_icon_backdrop;
static LPFRAMEDEF attack2_icon_level;
static LPFRAMEDEF attack2_icon_label;
static LPFRAMEDEF attack2_icon_value;
static BOOL simple_infopanel_loaded;
static BOOL infopanel_loaded;

#define INVENTORY_CHARGE_FONT_SIZE 10

static void InitStatusWrapper(LPFRAMEDEF frame, FLOAT x, FLOAT y, FLOAT width, FLOAT height) {
    UI_InitFrame(frame, FT_SIMPLEFRAME);
    UI_SetSize(frame, width, height);
    UI_SetPoint(frame, FRAMEPOINT_TOPLEFT, simple_panel.SimpleInfoPanelUnitDetail,
                FRAMEPOINT_TOPLEFT, x, y);
}

static void InfoPanelEnsureLoaded(void) {
    if (infopanel_loaded) return;
    infopanel_loaded = true;
    InfoPanelUnitDetail_Load(&unit_panel);
    InfoPanelBuildingDetail_Load(&building_panel);
    if (!UI_EnsureFDF("UI\\FrameDef\\GlobalStrings.fdf")) {
        fprintf(stderr, "InfoPanelEnsureLoaded: missing UI\\FrameDef\\GlobalStrings.fdf; cannot resolve SimpleInfoPanel string IDs\n");
        simple_infopanel_loaded = false;
    } else {
        simple_infopanel_loaded = SimpleInfoPanel_Load(&simple_panel);
    }
    if (!simple_infopanel_loaded) {
        fprintf(stderr, "InfoPanelEnsureLoaded: missing UI\\FrameDef\\UI\\SimpleInfoPanel.fdf status templates\n");
    } else {
        /* The retail SimpleInfoPanel FDF owns every icon/label/value offset.
         * The game only supplies the dynamic wrappers that WC3 repositions
         * according to attack count and Hero state. */
        attack2_icon = UI_CloneFrameTree(simple_panel.SimpleInfoPanelIconDamage, NULL);
        if (attack2_icon) {
            attack2_icon_backdrop = UI_FindChildFrame(attack2_icon, "InfoPanelIconBackdrop");
            attack2_icon_level = UI_FindChildFrame(attack2_icon, "InfoPanelIconLevel");
            attack2_icon_label = UI_FindChildFrame(attack2_icon, "InfoPanelIconLabel");
            attack2_icon_value = UI_FindChildFrame(attack2_icon, "InfoPanelIconValue");
        }
        if (!attack2_icon || !attack2_icon_backdrop || !attack2_icon_level ||
            !attack2_icon_label || !attack2_icon_value) {
            fprintf(stderr, "InfoPanelEnsureLoaded: failed to clone SimpleInfoPanelIconDamage context 1\n");
            simple_infopanel_loaded = false;
        }
    }

    UI_InitFrame(&bottom_panel, FT_SIMPLEFRAME);
    UI_SetSize(&bottom_panel, 0.180f, 0.120f);
    /* UI_SetPoint Y uses WC3 FDF convention: negative = downward from TOPLEFT.
     * UI_CopyFrameBase encodes the raw float; the client negates it on decode.
     * So to place the panel at top-origin y=0.480, pass -(0.480). */
    UI_SetPoint(&bottom_panel, FRAMEPOINT_TOPLEFT, NULL, FRAMEPOINT_TOPLEFT, 0.310f, -(UI_BASE_HEIGHT - 0.120f));

    if (simple_infopanel_loaded) {
        InitStatusWrapper(&attack1_wrapper, 0.000f, -0.04000f, 0.100f, 0.030125f);
        InitStatusWrapper(&attack2_wrapper, 0.100f, -0.03925f, 0.100f, 0.030125f);
        InitStatusWrapper(&armor_wrapper,   0.000f, -0.07050f, 0.100f, 0.030125f);
        InitStatusWrapper(&hero_wrapper,    0.100f, -0.03700f, 0.100f, 0.062500f);
        InitStatusWrapper(&food_wrapper,    0.100f, -0.03925f, 0.100f, 0.030125f);

        /* Bind the runtime-controlled status bars to the retail FDF geometry.
         * The FDF owns their anchors; Warsmash supplies only width, textures,
         * colour and the live progress value. */
        UI_SetSize(simple_panel.SimpleHeroLevelBar, 0.180f, simple_panel.SimpleHeroLevelBar->Height);
        UI_SetTexture(simple_panel.SimpleHeroLevelBar, Theme_String("SimpleXpBarConsole", "SimpleXpBarConsole"), false);
        UI_SetTexture2(simple_panel.SimpleHeroLevelBar, Theme_String("SimpleXpBarBorder", "SimpleXpBarBorder"), false);
        simple_panel.SimpleHeroLevelBar->Color = MAKE(COLOR32, 138, 0, 131, 255);
        UI_SetSize(simple_panel.SimpleBuildTimeIndicator, 0.10538f, 0.0103f);
        UI_SetTexture(simple_panel.SimpleBuildTimeIndicator,
                      Theme_String("SimpleBuildTimeIndicator", "SimpleBuildTimeIndicator"), false);
        UI_SetTexture2(simple_panel.SimpleBuildTimeIndicator,
                       Theme_String("SimpleBuildTimeIndicatorBorder", "SimpleBuildTimeIndicatorBorder"), false);
        UI_SetSize(simple_panel.SimpleBuildQueueBackdrop, 0.180f, 0.090f);

        /* Warsmash's status strip is runtime-owned rather than defined by the
         * retail SimpleInfoPanel FDF.  Keep its geometry relative to the retail
         * unit-detail frame: label at BOTTOMLEFT + (0.03, 0.003), then 0.015
         * icons chained left-to-right with a 0.001 gap. */
        UI_InitFrame(&buff_status_label, FT_STRING);
        snprintf(buff_status_label.Name, sizeof(buff_status_label.Name), "SmashBuffStatusBar");
        UI_SetSize(&buff_status_label, 0.035f, 0.010f);
        UI_SetPoint(&buff_status_label, FRAMEPOINT_BOTTOMLEFT, simple_panel.SimpleInfoPanelUnitDetail,
                    FRAMEPOINT_BOTTOMLEFT, 0.030f, 0.003f);
        buff_status_label.Font.Size = 0.010f;
        buff_status_label.Font.Index = gi.FontIndex(Theme_String("MasterFont", "Fonts\\FRIZQT__.TTF"), HUD_FONT_SIZE);
        buff_status_label.Font.Justification.Horizontal = FONT_JUSTIFYLEFT;
        buff_status_label.Font.Justification.Vertical = FONT_JUSTIFYMIDDLE;
        UI_SetText(&buff_status_label, "%s", UI_GetString("COLON_STATUS"));

        FOR_LOOP(i, MAX_UNIT_STATUSES) {
            UI_InitFrame(&buff_status_icons[i], FT_SIMPLEFRAME);
            snprintf(buff_status_icons[i].Name, sizeof(buff_status_icons[i].Name),
                     "SmashBuffStatusBarIcon%u", i);
            UI_SetSize(&buff_status_icons[i], 0.015f, 0.015f);
            UI_SetPoint(&buff_status_icons[i], FRAMEPOINT_LEFT,
                        i ? &buff_status_icons[i - 1] : &buff_status_label,
                        FRAMEPOINT_RIGHT, 0.001f, 0.0f);

            UI_InitFrame(&buff_status_icon_textures[i], FT_TEXTURE);
            snprintf(buff_status_icon_textures[i].Name, sizeof(buff_status_icon_textures[i].Name),
                     "SmashBuffStatusBarIcon%uTexture", i);
            UI_SetParent(&buff_status_icon_textures[i], &buff_status_icons[i]);
            UI_SetAllPoints(&buff_status_icon_textures[i]);
        }
    }
}

static void HideLegacyUnitStats(void) {
    LPFRAMEDEF const frames_to_hide[] = {
        unit_panel.DefenseLabel, unit_panel.DefenseValue,
        unit_panel.AttackLabel1, unit_panel.AttackValue1,
        unit_panel.AttackLabel2, unit_panel.AttackValue2,
        unit_panel.SpeedTitle, unit_panel.SpeedValue,
        unit_panel.RangeTitle1, unit_panel.RangeValue1,
        unit_panel.RangeTitle2, unit_panel.RangeValue2,
        unit_panel.IconBackdrop1, unit_panel.IconValue1,
        unit_panel.IconBackdrop2, unit_panel.IconValue2,
        unit_panel.IconBackdrop3, unit_panel.IconValue3,
        unit_panel.IconBackdrop4, unit_panel.IconValue4,
    };
    FOR_LOOP(i, sizeof(frames_to_hide) / sizeof(frames_to_hide[0]))
        UI_SetHidden(frames_to_hide[i], true);
}

static void WriteLegacyUnitStats(LPEDICT ent, UnitWeapons_t const *weapons,
                                 BOOL has_attack2, LONG min_damage, LONG max_damage,
                                 LONG min_damage2, LONG max_damage2, BOOL is_hero,
                                 DWORD level) {
    char buffer[128];

    UI_SetText(unit_panel.AttackLabel1, "Damage:");
    UI_SetText(unit_panel.AttackValue1, "%ld - %ld", (long)min_damage, (long)max_damage);
    UI_SetText(unit_panel.AttackLabel2, "Damage:");
    UI_SetText(unit_panel.AttackValue2, "%ld - %ld", (long)min_damage2, (long)max_damage2);
    UI_SetHidden(unit_panel.AttackLabel2, !has_attack2);
    UI_SetHidden(unit_panel.AttackValue2, !has_attack2);
    UI_SetText(unit_panel.DefenseLabel, "Armor:");
    UI_SetText(unit_panel.DefenseValue, "%d", (int)(ent->armor_value + 0.5f));
    UI_SetText(unit_panel.SpeedTitle, "Speed:");
    UI_SetText(unit_panel.SpeedValue, "%d", (int)(ent->unitinfo.MoveSpeed + 0.5f));
    UI_SetText(unit_panel.RangeTitle1, "Range:");
    UI_SetText(unit_panel.RangeValue1, "%d", (int)(ent->attack1.range + 0.5f));
    UI_SetText(unit_panel.RangeTitle2, "Range:");
    UI_SetText(unit_panel.RangeValue2, "%d", (int)(weapons->attack2.range + 0.5f));
    UI_SetHidden(unit_panel.RangeTitle2, !has_attack2);
    UI_SetHidden(unit_panel.RangeValue2, !has_attack2);

    if (is_hero) {
        LPCSTR const prim = ent->UnitBalance->primaryAttribute;
        struct { LPCSTR code; DWORD val; } attrs[3] = {
            { "STR", ent->hero.str }, { "AGI", ent->hero.agi }, { "INT", ent->hero.intel },
        };
        LPFRAMEDEF icon_values[3] = {
            unit_panel.IconValue1, unit_panel.IconValue2, unit_panel.IconValue3,
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
            UI_SetText(unit_panel.IconValue4, "%s", buffer);
            unit_panel.IconValue4->Font.Color = MAKE(COLOR32, 200, 200, 200, 255);
        }
    }
}

static void SetTypedInfoPanelIcon(LPFRAMEDEF frame, LPCSTR prefix, LPCSTR type) {
    char code[32];
    char key[96];
    LPCSTR texture;
    size_t length;

    if (!frame || !prefix || !type || !*type) return;
    length = MIN(strlen(type), sizeof(code) - 1);
    memcpy(code, type, length);
    code[length] = '\0';
    code[0] = (char)toupper((unsigned char)code[0]);
    snprintf(key, sizeof(key), "InfoPanelIcon%s%s", prefix, code);
    texture = Theme_String(key, NULL);
    if (!texture || !*texture) {
        fprintf(stderr, "SetTypedInfoPanelIcon: missing war3skins key %s\n", key);
        return;
    }
    UI_SetTexture(frame, texture, false);
}

static void SetHeroPrimaryAttributeIcon(LPCSTR primary) {
    LPCSTR key = "InfoPanelIconHeroIconSTR";
    LPCSTR texture;

    if (!simple_panel.InfoPanelIconHeroIcon) return;
    if (primary && !strcmp(primary, "AGI")) key = "InfoPanelIconHeroIconAGI";
    else if (primary && !strcmp(primary, "INT")) key = "InfoPanelIconHeroIconINT";
    texture = Theme_String(key, NULL);
    if (!texture || !*texture) {
        fprintf(stderr, "SetHeroPrimaryAttributeIcon: missing war3skins key %s\n", key);
        return;
    }
    UI_SetTexture(simple_panel.InfoPanelIconHeroIcon, texture, false);
}

static void RefreshSimpleInfoPanelStrings(void) {
    if (!simple_infopanel_loaded) return;
    UI_SetText(simple_panel.InfoPanelIconLabel, "%s", UI_GetString("COLON_DAMAGE"));
    if (attack2_icon_label) UI_SetText(attack2_icon_label, "%s", UI_GetString("COLON_DAMAGE"));
    UI_SetText(simple_panel.InfoPanelIconLabel_2, "%s", UI_GetString("COLON_ARMOR"));
    UI_SetText(simple_panel.InfoPanelIconLabel_4, "%s", UI_GetString("COLON_FOOD_PROVIDED"));
    UI_SetText(simple_panel.InfoPanelIconHeroStrengthLabel, "%s", UI_GetString("COLON_STRENGTH"));
    UI_SetText(simple_panel.InfoPanelIconHeroAgilityLabel, "%s", UI_GetString("COLON_AGILITY"));
    UI_SetText(simple_panel.InfoPanelIconHeroIntellectLabel, "%s", UI_GetString("COLON_INTELLECT"));
    UI_SetText(&buff_status_label, "%s", UI_GetString("COLON_STATUS"));
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

static DWORD UnitUpgradeForClass(LPCSTR upgrades, LPCSTR wanted_class) {
    LPCSTR cursor = upgrades;

    if (!cursor || !wanted_class) return 0;
    while (*cursor) {
        char rawcode[5] = { 0 };
        DWORD length = 0;
        LPCSTR upgrade_class;
        UpgradeData_t const *upgrade;

        while (*cursor && (isspace((unsigned char)*cursor) || *cursor == ',' || *cursor == ';')) cursor++;
        while (cursor[length] && cursor[length] != ',' && cursor[length] != ';' &&
               !isspace((unsigned char)cursor[length]) && length < 4) {
            rawcode[length] = cursor[length];
            length++;
        }
        if (length == 4) {
            upgrade = G_UpgradeData(FS_SLKKey(rawcode));
            upgrade_class = upgrade && upgrade->id ? upgrade->upgradeClass : NULL;
            if (upgrade_class && !strcasecmp(upgrade_class, wanted_class))
                return upgrade->id;
        }
        while (*cursor && *cursor != ',' && *cursor != ';') cursor++;
        if (*cursor) cursor++;
    }
    return 0;
}

static DWORD UnitWeaponUpgrade(LPEDICT ent, BOOL is_hero) {
    static LPCSTR const classes[] = { "melee", "ranged", "artillery" };

    if (!ent || !ent->UnitBalance || is_hero) return 0;
    FOR_LOOP(i, sizeof(classes) / sizeof(classes[0])) {
        DWORD const upgrade = UnitUpgradeForClass(ent->UnitBalance->upgrades, classes[i]);
        if (upgrade) return upgrade;
    }
    return 0;
}

static DWORD UnitArmorUpgrade(LPEDICT ent, BOOL is_hero) {
    if (!ent || !ent->UnitBalance || is_hero) return 0;
    return UnitUpgradeForClass(ent->UnitBalance->upgrades, "armor");
}

static void SetUpgradeLevel(LPFRAMEDEF frame, DWORD upgrade, LPEDICT ent) {
    LPGAMECLIENT owner;

    if (!frame) return;
    if (!upgrade || !ent || !(owner = G_GetPlayerClientByNumber(ent->s.player))) {
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

static LPCSTR StatusBuffArt(DWORD code) {
    LPCSTR art;

    /* Timed life has a dedicated WC3 countdown presentation rather than a
     * normal buff icon.  Cooldown markers share abilstatus[] but have ability
     * rawcodes and therefore do not resolve through AbilityBuffData. */
    if (code == MAKEFOURCC('B', 'T', 'L', 'F')) return NULL;
    art = StatusBuffField(code, "Buffart");
    if (!art || !*art) return NULL;
    return Theme_String(art, art);
}

static void WriteBuffStatusFrames(LPEDICT ent) {
    DWORD slot = 0;
    DWORD shown[MAX_UNIT_STATUSES] = { 0 };

    if (!ent || !simple_infopanel_loaded) return;
    UI_SetText(&buff_status_label, "%s", UI_GetString("COLON_STATUS"));
    UI_WriteFrame(&buff_status_label);

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
            if (buff_code != MAKEFOURCC('B', 'T', 'L', 'F')) {
                char rawcode[5] = { 0 };
                memcpy(rawcode, &buff_code, 4);
                fprintf(stderr, "WriteBuffStatusFrames: missing Buffart for status %s\n", rawcode);
            }
            continue;
        }

        icon = &buff_status_icons[slot];
        UI_SetTexture(&buff_status_icon_textures[slot], art, false);
        tip = StatusBuffField(buff_code, "Bufftip");
        ubertip = StatusBuffField(buff_code, "Buffubertip");
        icon->Tip = tip && *tip ? UI_GetString(tip) : NULL;
        icon->Ubertip = ubertip && *ubertip ? UI_GetString(ubertip) : NULL;
        UI_WriteFrame(icon);
        UI_WriteFrame(&buff_status_icon_textures[slot]);
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
    DWORD const weapon_upgrade = UnitWeaponUpgrade(ent, is_hero);
    DWORD const armor_upgrade = UnitArmorUpgrade(ent, is_hero);

    if (!simple_infopanel_loaded) return;
    RefreshSimpleInfoPanelStrings();

    UI_SetPoint(&armor_wrapper, FRAMEPOINT_TOPLEFT, simple_panel.SimpleInfoPanelUnitDetail,
                FRAMEPOINT_TOPLEFT, 0.0f, has_attack1 ? -0.0705f : -0.0400f);

    if (has_attack1) {
        SetTypedInfoPanelIcon(simple_panel.InfoPanelIconBackdrop, "Damage", weapons->attack1.attackType);
        snprintf(value, sizeof(value), "%ld - %ld", (long)min_damage, (long)max_damage);
        UI_SetText(simple_panel.InfoPanelIconValue, "%s", value);
        SetUpgradeLevel(simple_panel.InfoPanelIconLevel, weapon_upgrade, ent);
        UI_WriteFrame(&attack1_wrapper);
        UI_WriteFrameWithChildren(simple_panel.SimpleInfoPanelIconDamage, &attack1_wrapper);
    }
    if (has_attack2) {
        SetTypedInfoPanelIcon(attack2_icon_backdrop, "Damage", weapons->attack2.attackType);
        snprintf(value, sizeof(value), "%ld - %ld", (long)min_damage2, (long)max_damage2);
        UI_SetText(attack2_icon_value, "%s", value);
        SetUpgradeLevel(attack2_icon_level, weapon_upgrade, ent);
        UI_WriteFrame(&attack2_wrapper);
        UI_WriteFrameWithChildren(attack2_icon, &attack2_wrapper);
    }

    SetTypedInfoPanelIcon(simple_panel.InfoPanelIconBackdrop_2, "Armor", ent->UnitBalance->defenseType);
    UI_SetText(simple_panel.InfoPanelIconValue_2, "%d", (int)(ent->armor_value + 0.5f));
    SetUpgradeLevel(simple_panel.InfoPanelIconLevel_2, armor_upgrade, ent);
    UI_WriteFrame(&armor_wrapper);
    UI_WriteFrameWithChildren(simple_panel.SimpleInfoPanelIconArmor, &armor_wrapper);

    if (ent->UnitBalance->foodMade > 0) {
        LPCSTR const food_art = Theme_String("InfoPanelIconFood", NULL);
        if (!food_art || !*food_art) {
            fprintf(stderr, "WriteSelectedUnitStatusFrames: missing war3skins InfoPanelIconFood\n");
        } else {
            UI_SetTexture(simple_panel.InfoPanelIconBackdrop_4, food_art, false);
        }
        UI_SetHidden(simple_panel.InfoPanelIconLevel_4, true);
        UI_SetText(simple_panel.InfoPanelIconValue_4, "%ld", (long)ent->UnitBalance->foodMade);
        UI_WriteFrame(&food_wrapper);
        UI_WriteFrameWithChildren(simple_panel.SimpleInfoPanelIconFood, &food_wrapper);
    }

    if (is_hero) {
        SetHeroPrimaryAttributeIcon(ent->UnitBalance->primaryAttribute);
        UI_SetText(simple_panel.InfoPanelIconHeroStrengthValue, "%lu", (unsigned long)ent->hero.str);
        UI_SetText(simple_panel.InfoPanelIconHeroAgilityValue, "%lu", (unsigned long)ent->hero.agi);
        UI_SetText(simple_panel.InfoPanelIconHeroIntellectValue, "%lu", (unsigned long)ent->hero.intel);
        UI_WriteFrame(&hero_wrapper);
        UI_WriteFrameWithChildren(simple_panel.SimpleInfoPanelIconHero, &hero_wrapper);
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

static void WriteSimpleUnitHeader(LPEDICT ent, LPCSTR display_name, BOOL is_hero) {
    BOOL old_hero_hidden;

    if (!simple_infopanel_loaded) return;
    UI_SetText(simple_panel.SimpleNameValue, "%s", display_name ? display_name : "");
    UI_SetText(simple_panel.SimpleClassValue, "%s", "");
    UI_SetHidden(simple_panel.SimpleClassValue, true);
    UI_SetHidden(simple_panel.SimpleProgressIndicator, true);

    old_hero_hidden = simple_panel.SimpleHeroLevelBar->hidden;
    UI_SetHidden(simple_panel.SimpleHeroLevelBar, true);
    UI_WriteFrame(&bottom_panel);
    UI_WriteFrameWithChildren(simple_panel.SimpleInfoPanelUnitDetail, &bottom_panel);
    UI_SetHidden(simple_panel.SimpleHeroLevelBar, old_hero_hidden);

    if (is_hero) {
        UI_SetHidden(simple_panel.SimpleHeroLevelBar, false);
        UI_WriteFrameValue(simple_panel.SimpleHeroLevelBar, HeroLevelProgress(ent));
    } else {
        UI_SetHidden(simple_panel.SimpleHeroLevelBar, true);
    }
}

DWORD UI_WriteBuildingQueueShell(LPEDICT ent, LPCSTR action_key) {
    LPCSTR name;

    if (!ent) return 0;
    InfoPanelEnsureLoaded();
    if (!simple_infopanel_loaded) return 0;

    name = G_UnitProfile(ent->class_id)->name;
    if (!name || !*name) name = GetClassName(ent->class_id);
    UI_SetText(simple_panel.SimpleBuildingNameValue, "%s", name);
    UI_SetText(simple_panel.SimpleBuildingDescriptionValue, "%s", "");
    UI_SetText(simple_panel.SimpleBuildingActionLabel, "%s", UI_GetString(action_key ? action_key : "TRAINING"));
    UI_SetHidden(simple_panel.SimpleBuildTimeIndicator, false);
    UI_SetHidden(simple_panel.SimpleBuildQueueBackdrop, false);

    UI_WriteFrame(&bottom_panel);
    UI_WriteFrameWithChildren(simple_panel.SimpleInfoPanelBuildingDetail, &bottom_panel);
    return UI_GetWrittenFrameNumber(simple_panel.SimpleBuildTimeIndicator);
}

void UI_WriteSingleInfo(LPEDICT ent) {
    UnitBalance_t const *balance = ent->UnitBalance;
    UnitWeapons_t const *weapons = ent->UnitWeapons;
    LPCSTR name = G_UnitProfile(ent->class_id)->properNames;
    LPCSTR unit_name = G_UnitProfile(ent->class_id)->name;
    BOOL const is_hero = balance->strength > 0 || balance->agility > 0 || balance->intelligence > 0;
    DWORD level = is_hero && ent->hero.level > 0 ? ent->hero.level
                                                 : MAX(1, balance->level);
    LONG dice = ent->attack1.numberOfDice;
    BOOL has_attack1 = dice > 0;
    LONG min_damage = has_attack1 ? (LONG)(ent->attack1.damageBase + dice) : 0;
    LONG max_damage = has_attack1 ? (LONG)(ent->attack1.damageBase + dice * ent->attack1.sidesPerDie) : 0;
    LONG dice2 = weapons->attack2.damageDice;
    BOOL has_attack2 = UI_HasSecondAttack(weapons);
    LONG min_damage2 = has_attack2 ? weapons->attack2.damageBase + dice2 : 0;
    LONG max_damage2 = has_attack2 ? weapons->attack2.damageBase + dice2 * weapons->attack2.damageSides : 0;

    if (!unit_name || !*unit_name) unit_name = GetClassName(ent->class_id);
    if (!name || !*name) name = unit_name;

    InfoPanelEnsureLoaded();

    if (simple_infopanel_loaded) {
        /* SimpleNameValue owns the Warcraft title font/anchors. Ordinary units
         * have no synthetic "Level N <type>" line; Heroes use the XP bar in
         * that slot instead of a duplicate level/class label. */
        HideLegacyUnitStats();
        WriteSimpleUnitHeader(ent, is_hero ? name : unit_name, is_hero);
    } else {
        char buffer[128];
        UI_SetText(unit_panel.NameValue, "%s", name);
        snprintf(buffer, sizeof(buffer), "Level %lu %s", (unsigned long)level, unit_name ? unit_name : "");
        UI_SetText(unit_panel.ClassValue, "%s", buffer);
        WriteLegacyUnitStats(ent, weapons, has_attack2, min_damage, max_damage,
                             min_damage2, max_damage2, is_hero, level);
        UI_WriteFrame(&bottom_panel);
        UI_WriteFrameWithChildren(unit_panel.InfoPanelUnitDetail, &bottom_panel);
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
    multi->hp_bar = gi.ImageIndex(Theme_String("SimpleHpBarConsole", "UI\\Widgets\\Console\\Human\\human-statbar-fill.blp"));
    multi->mana_bar = gi.ImageIndex(Theme_String("SimpleManaBarConsole", "UI\\Widgets\\Console\\Human\\human-statbar-fill.blp"));
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

void UI_SeedInfoPanelCache(LPEDICT ent, LPEDICT *selected, DWORD count) {
    if (!ent->client) return;
    if (count == 1 && (!selected[0]->build || !G_UnitCanControl(ent->client, selected[0]))) {
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
        if (selected[0]->build && G_UnitCanControl(ent->client, selected[0])) {
            UI_WriteBuildQueue(selected[0]);
        } else {
            UI_WriteSingleInfo(selected[0]);
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
    gameCommandButton_t buttons[12];
    BYTE count;

    if (!ent || !ent->client) return;
    ent->client->commands_dirty = false;
    memset(&ent->client->menu, 0, sizeof(ent->client->menu));
    if (!selected || !G_UnitCanControl(ent->client, selected)) {
        UI_ClearLayer(ent, LAYER_COMMANDBAR);
        return;
    }

    UI_WriteStart(LAYER_COMMANDBAR);
    count = G_GetCommandButtons(selected, buttons, 12);
    FOR_LOOP(i, count) {
        UI_WriteCommandButtonFrame(&buttons[i]);
    }
    if (count) UI_WriteTooltipFrame();
    UI_WriteEnd(ent);
}

static void WritePortraitFrame(LPEDICT ent) {
    uiFrame_t frame;
    if (!ent || !ent->s.model) return;
    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_PORTRAIT;
    frame.color = COLOR32_WHITE;
    frame.tex.index = ent->s.model;
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

static void WritePortraitText(LPCSTR text, COLOR32 color, FLOAT bottom) {
    uiFrame_t frame;
    uiLabel_t label;

    memset(&frame, 0, sizeof(frame));
    memset(&label, 0, sizeof(label));
    frame.flags.type = FT_STRING;
    frame.text = text && *text ? text : " ";
    frame.color = color;
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
    hp = (LONG)(MAX(0.0f, ent->health.value) + 0.5f);
    max_hp = (LONG)(MAX(0.0f, ent->health.max_value) + 0.5f);
    mp = (LONG)(MAX(0.0f, ent->mana.value) + 0.5f);
    max_mp = (LONG)(MAX(0.0f, ent->mana.max_value) + 0.5f);

    snprintf(health, sizeof(health), "%ld / %ld", (long)hp, (long)max_hp);
    if (max_mp > 0)
        snprintf(mana, sizeof(mana), "%ld / %ld", (long)mp, (long)max_mp);
    else
        mana[0] = '\0';

    /* Warsmash's UnitPortrait places the stat strings in the 0.029-high strip
     * below the model: HP at BOTTOM + 0.014 and mana at BOTTOM - 0.0005.
     * OpenRealm keeps the portrait runtime-authored because SmashUI is not a
     * retail MPQ FDF, but uses the same final WC3-space geometry. */
    WritePortraitText(health, PortraitHealthColor(ent), 0.586f);
    WritePortraitText(mana, COLOR32_WHITE, 0.6005f);
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
 * the native frame directly while keeping its texture race-selected from war3skins. */
static void WriteInventoryCover(LPEDICT player) {
    static FRAMEDEF frame;
    LPCSTR art = Theme_PlayerString(player ? player->client : NULL, "ConsoleInventoryCoverTexture", NULL);

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

static void WriteInventory(LPEDICT player, LPEDICT ent) {
    gameInventoryItem_t items[MAX_INVENTORY];
    DWORD capacity = G_InventoryCapacity(ent);
    BYTE count;

    if (!capacity) { WriteInventoryCover(player); return; }
    if (capacity < MAX_INVENTORY) {
        LPCSTR art = Theme_PlayerString(player ? player->client : NULL, "ConsoleInventoryNoCapacity", NULL);
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
    UI_WriteStart(LAYER_INVENTORY);
    if (count == 1) WriteInventory(ent, selected[0]);
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

/* Re-send LAYER_INFOPANEL only when HP, mana, XP, or an explicitly invalidated
 * selected-unit presentation changed. */
void G_RefreshInfoPanel(LPEDICT ent) {
    LPEDICT selected[MAX_SELECTED_ENTITIES];
    DWORD count;
    LONG hp, mana;

    if (!ent || !ent->client) return;
    count = SelectedUnits(ent->client, selected, MAX_SELECTED_ENTITIES);
    if (count != 1 || (selected[0]->build && G_UnitCanControl(ent->client, selected[0]))) {
        ent->client->infopanel.entity = 0;
        return;
    }
    hp = (LONG)(selected[0]->health.value + 0.5f);
    mana = (LONG)(selected[0]->mana.value + 0.5f);
    if (selected[0]->s.number == ent->client->infopanel.entity &&
        hp == ent->client->infopanel.hp &&
        mana == ent->client->infopanel.mana &&
        (LONG)selected[0]->hero.xp == ent->client->infopanel.xp) {
        return;
    }
    if (selected[0]->s.number != ent->client->infopanel.entity ||
        hp != ent->client->infopanel.hp || mana != ent->client->infopanel.mana)
        ent->client->presentation_dirty = true;
    UI_SendInfoPanel(ent, selected, count);
}

/* Once per server frame, keep every player's info panel in sync. */
void G_UpdateClientInfoPanels(void) {
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = g_edicts + i;
        if (ent->inuse && ent->client)
            G_RefreshInfoPanel(ent);
    }
}

/* Re-send LAYER_CONSOLE only when a resource value changed. */
void G_RefreshResourceBar(LPEDICT ent) {
    LPPLAYER ps;
    LONG gold, lumber, food_u, food_c;

    if (!ent || !ent->client) return;
    ps     = &ent->client->ps;
    gold   = (LONG)ps->stats[PLAYERSTATE_RESOURCE_GOLD];
    lumber = (LONG)ps->stats[PLAYERSTATE_RESOURCE_LUMBER];
    food_u = (LONG)ps->stats[PLAYERSTATE_RESOURCE_FOOD_USED];
    food_c = G_GetEffectiveFoodCap(ent->client);

    if (gold   == ent->client->resourcebar.gold   &&
        lumber == ent->client->resourcebar.lumber  &&
        food_u == ent->client->resourcebar.food_used &&
        food_c == ent->client->resourcebar.food_cap)
        return;

    UI_WriteStart(LAYER_CONSOLE);
    UI_WriteConsoleBackdrop(ent->client, food_u, food_c);
    UI_WriteMinimapFrame();
    UI_WriteEnd(ent);

    ent->client->resourcebar.gold      = gold;
    ent->client->resourcebar.lumber    = lumber;
    ent->client->resourcebar.food_used = food_u;
    ent->client->resourcebar.food_cap  = food_c;
}

/* Once per server frame, keep every player's resource bar in sync. */
void G_UpdateClientResourceBars(void) {
    FOR_LOOP(i, globals.num_edicts) {
        LPEDICT ent = g_edicts + i;
        if (ent->inuse && ent->client)
            G_RefreshResourceBar(ent);
    }
}
