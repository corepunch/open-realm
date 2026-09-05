/*
 * hud_console.c — ConsoleUI backdrop, minimap, resource bar.
 *
 * Draws the static chrome of the in-game HUD: the textured console
 * frame at top/bottom, the minimap viewport rect, and the gold/lumber/
 * supply/upkeep resource bar.
 *
 * Blizzard's templates supply the skin; C code handles the final
 * composition and the minimap extension.
 */

#include "hud_local.h"

#define RESOURCE_TOOLTIP_TIP_SIZE 256
#define RESOURCE_TOOLTIP_UBERTIP_SIZE 2048

typedef struct {
    char tip[RESOURCE_TOOLTIP_TIP_SIZE];
    char ubertip[RESOURCE_TOOLTIP_UBERTIP_SIZE];
} resourceTooltipText_t;

static resourceTooltipText_t resource_tooltips[4];

static LPCSTR UI_OptionalGlobalString(LPCSTR key) {
    LPCSTR value;

    if (!key || !*key) return NULL;
    value = UI_GetString(key);
    return value && strcmp(value, key) ? value : NULL;
}

static LPCSTR UI_GlobalStringOrFallback(LPCSTR key, LPCSTR fallback) {
    LPCSTR value = UI_OptionalGlobalString(key);
    return value ? value : fallback;
}

static void UI_AppendTooltipText(LPSTR out, DWORD out_size, LPCSTR text) {
    DWORD used;

    if (!out || !out_size || !text || !*text) return;
    used = (DWORD)strlen(out);
    if (used >= out_size - 1) return;
    snprintf(out + used, out_size - used, "%s", text);
}

static LPCSTR UI_UpkeepLabel(DWORD tier) {
    if (tier > 1) return UI_GlobalStringOrFallback("UPKEEP_HIGH", "High Upkeep");
    if (tier == 1) return UI_GlobalStringOrFallback("UPKEEP_LOW", "Low Upkeep");
    return UI_GlobalStringOrFallback("UPKEEP_NONE", "No Upkeep");
}

static BOOL UI_HasLumberUpkeepTax(void) {
    FOR_LOOP(i, game.constants.upkeepLumberTaxCount) {
        if (game.constants.upkeepLumberTax[i] > 0.0001f) return true;
    }
    return false;
}

static BOOL UI_UpkeepBodyHasTierRanges(LPCSTR text) {
    DWORD ranges = 0;

    if (!text) return false;
    for (LPCSTR p = text; *p; p++) {
        LPCSTR q;
        if (!isdigit((unsigned char)*p)) continue;
        q = p;
        while (isdigit((unsigned char)*q)) q++;
        if (*q == '-' && isdigit((unsigned char)q[1])) {
            ranges++;
            p = q;
        }
    }
    return ranges >= 2;
}

static void UI_FormatUpkeepLegend(LPSTR out, DWORD out_size,
                                  LPCSTR info, BOOL use_wood_info) {
    DWORD tier_count;

    if (!out || !out_size || !info || !*info) return;
    /* Thresholds delimit tiers; the final tier lies above the final threshold.
     * Tax arrays may be shorter, in which case gameplay clamps to their last
     * authored rate through G_GetUpkeep*RateForTier(). */
    tier_count = MIN(game.constants.upkeepUsageCount + 1, MAX_UPKEEP_TIERS + 1);

    FOR_LOOP(tier, tier_count) {
        LONG lower = 0;
        LONG upper;
        LONG gold_rate = G_GetUpkeepGoldRateForTier(tier);
        LONG lumber_rate = G_GetUpkeepLumberRateForTier(tier);
        LPCSTR label = UI_UpkeepLabel(tier);
        char line[512];

        if (tier > 0 && tier - 1 < game.constants.upkeepUsageCount)
            lower = (LONG)game.constants.upkeepUsage[tier - 1] + 1;
        if (tier < game.constants.upkeepUsageCount) {
            upper = (LONG)game.constants.upkeepUsage[tier];
        } else if (game.constants.foodCeiling > 0) {
            upper = game.constants.foodCeiling;
        } else {
            /* Split Warcraft INFO formats require a min/max pair. If gameplay
             * has no food ceiling, keep the final reachable tier visible with
             * a compact fallback rather than silently dropping High Upkeep. */
            if (use_wood_info) {
                snprintf(line, sizeof(line), "|n%ld+ Food: %s|R (%ld%% G, %ld%% L)",
                         (long)lower, label, (long)gold_rate, (long)lumber_rate);
            } else {
                snprintf(line, sizeof(line), "|n%ld+ Food: %s|R (%ld%% income)",
                         (long)lower, label, (long)gold_rate);
            }
            UI_AppendTooltipText(out, out_size, line);
            continue;
        }

        if (upper < lower) continue;
        if (use_wood_info) {
            snprintf(line, sizeof(line), info, (int)lower, (int)upper, label,
                     (int)gold_rate, (int)lumber_rate);
        } else {
            snprintf(line, sizeof(line), info, (int)lower, (int)upper, label,
                     (int)gold_rate);
        }
        UI_AppendTooltipText(out, out_size, line);
    }
}

static void UI_FormatUpkeepUbertip(LPSTR out, DWORD out_size) {
    LPCSTR base = UI_OptionalGlobalString("RESOURCE_UBERTIP_UPKEEP");
    LPCSTR info = NULL;
    BOOL wood_tax = UI_HasLumberUpkeepTax();
    BOOL use_wood_info = false;

    if (!out || !out_size) return;
    out[0] = '\0';
    if (base) UI_AppendTooltipText(out, out_size, base);

    /* Some data versions embed the whole tier table directly in the base
     * Ubertip. Preserve that authored/localized table. Otherwise prefer the
     * split INFO key and finally fall back to the retail row shape so older or
     * incomplete data still explains every reachable upkeep tier. */
    if (UI_UpkeepBodyHasTierRanges(base)) return;
    if (wood_tax) {
        info = UI_OptionalGlobalString("RESOURCE_UBERTIP_UPKEEP_INFO_WOOD");
        use_wood_info = info != NULL;
    }
    if (!info) info = UI_OptionalGlobalString("RESOURCE_UBERTIP_UPKEEP_INFO");
    if (!info) {
        info = UI_OptionalGlobalString("RESOURCE_UBERTIP_UPKEEP_INFO_WOOD");
        use_wood_info = info != NULL;
    }
    if (!info) {
        info = wood_tax
            ? "|n%d-%d Food: %s|R (%d%% G, %d%% L)"
            : "|n%d-%d Food: %s|R (%d%% income)";
        use_wood_info = wood_tax;
    }
    UI_FormatUpkeepLegend(out, out_size, info, use_wood_info);
}

#ifdef BZ_TESTS
BOOL UI_TestUpkeepBodyHasTierRanges(LPCSTR text) {
    return UI_UpkeepBodyHasTierRanges(text);
}

void UI_TestFormatUpkeepLegend(LPSTR out, DWORD out_size, LPCSTR info, BOOL use_wood_info) {
    if (out && out_size) out[0] = '\0';
    UI_FormatUpkeepLegend(out, out_size, info, use_wood_info);
}
#endif

static void UI_SetResourceTooltip(LPFRAMEDEF frame, resourceTooltipText_t *storage,
                                  LPCSTR label_key, LPCSTR label_fallback,
                                  LPCSTR ubertip_key) {
    LPCSTR label;
    LPCSTR body;

    if (!frame || !storage) return;
    label = UI_GlobalStringOrFallback(label_key, label_fallback);
    body = UI_OptionalGlobalString(ubertip_key);
    /* {value} is expanded on the client from this frame's Stat binding. This
     * deliberately follows the same replicated playerstate used to draw the
     * resource bar instead of freezing the value into an older layout packet. */
    snprintf(storage->tip, sizeof(storage->tip), "%s {value}", label);
    snprintf(storage->ubertip, sizeof(storage->ubertip), "%s", body ? body : "");
    frame->Tip = storage->tip;
    frame->Ubertip = storage->ubertip[0] ? storage->ubertip : NULL;
}

static void UI_SetUpkeepTooltip(LPFRAMEDEF frame, resourceTooltipText_t *storage,
                                DWORD tier, LONG gold_rate) {
    LPCSTR upkeep_label;
    LPCSTR upkeep_prefix;
    LPCSTR income_prefix;

    if (!frame || !storage) return;
    upkeep_label = UI_UpkeepLabel(tier);
    upkeep_prefix = UI_GlobalStringOrFallback("COLON_UPKEEP", "Upkeep:");
    income_prefix = UI_GlobalStringOrFallback("COLON_GOLD_INCOME_RATE", "Gold Income Rate:");

    /* UPKEEP_* strings carry their own Warcraft color prefix and intentionally
     * omit the reset. Close it before the income-rate line so the following
     * authored text does not inherit the tier color. */
    snprintf(storage->tip, sizeof(storage->tip), "%s %s|R|n%s %ld%%",
             upkeep_prefix, upkeep_label, income_prefix, (long)gold_rate);
    UI_FormatUpkeepUbertip(storage->ubertip, sizeof(storage->ubertip));
    frame->Tip = storage->tip;
    frame->Ubertip = storage->ubertip[0] ? storage->ubertip : NULL;
}

void UI_LoadHudConsole(void) {
    if (hud.console.ConsoleUI) return;
    /* ResourceBar.fdf references global resource/upkeep strings indirectly;
     * load that catalog explicitly instead of relying on another HUD panel to
     * have parsed it first. Missing strings degrade to descriptive fallbacks. */
    UI_EnsureFDF("UI\\FrameDef\\GlobalStrings.fdf");
    if (!ConsoleUI_Load(&hud.console)) return;
    UI_SetAllPoints(hud.console.ConsoleUI);
    ResourceBar_Load(&hud.res);
    UI_SetParent(hud.res.ResourceBarFrame, hud.console.ConsoleUI);
    UI_SetPoint(hud.res.ResourceBarFrame, FRAMEPOINT_TOPRIGHT, hud.console.ConsoleUI, FRAMEPOINT_TOPRIGHT, 0.0f, 0.0f);
    if (UpperButtonBar_Load(&hud.upper)) {
        /* UpperButtonBar.fdf is a separate authored root. Attach it to the
         * serialized ConsoleUI tree and bind the same server actions used by
         * the default F9-F12 bindings in share/config.cfg. */
        UI_SetParent(hud.upper.UpperButtonBarFrame, hud.console.ConsoleUI);
        UI_SetOnClick(hud.upper.UpperButtonBarQuestsButton, "quests");
        UI_SetOnClick(hud.upper.UpperButtonBarMenuButton, "menu");
        UI_SetOnClick(hud.upper.UpperButtonBarAlliesButton, "allies");
        UI_SetOnClick(hud.upper.UpperButtonBarChatButton, "log");
        snprintf(hud.upper_cmds[0], sizeof(hud.upper_cmds[0]), "%s", hud.upper.UpperButtonBarQuestsButton->OnClick);
        snprintf(hud.upper_cmds[1], sizeof(hud.upper_cmds[1]), "%s", hud.upper.UpperButtonBarMenuButton->OnClick);
        snprintf(hud.upper_cmds[2], sizeof(hud.upper_cmds[2]), "%s", hud.upper.UpperButtonBarAlliesButton->OnClick);
        snprintf(hud.upper_cmds[3], sizeof(hud.upper_cmds[3]), "%s", hud.upper.UpperButtonBarChatButton->OnClick);
    }
    UI_SetSize(hud.res.ResourceBarGoldText, hud.res.ResourceBarUpkeepText->Width, hud.res.ResourceBarGoldText->Height);
    UI_SetSize(hud.res.ResourceBarLumberText, hud.res.ResourceBarUpkeepText->Width, hud.res.ResourceBarLumberText->Height);
    UI_SetSize(hud.res.ResourceBarSupplyText, hud.res.ResourceBarUpkeepText->Width, hud.res.ResourceBarSupplyText->Height);
    hud.res.ResourceBarGoldText->Stat = PLAYERSTATE_RESOURCE_GOLD;
    hud.res.ResourceBarLumberText->Stat = PLAYERSTATE_RESOURCE_LUMBER;
    hud.res.ResourceBarSupplyText->Stat = PLAYERSTATE_RESOURCE_FOOD_USED;
}

static void UI_WriteTimeOfDayIndicator(LPGAMECLIENT client) {
    uiFrame_t frame;
    LPCSTR model;
    DWORD parent;

    if (!client || !gi.ModelIndex) return;
    model = Theme_PlayerString(client, "TimeOfDayIndicator", NULL);
    parent = UI_GetWrittenFrameNumber(hud.console.ConsoleUI);
    if (!model || !*model || !parent) return;

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_SPRITE;
    frame.color = COLOR32_WHITE;
    frame.tex.index = gi.ModelIndex(model);
    frame.stat = UI_PLAYERSTAT_ENV_PHASE;
    frame.flagsvalue |= UIFLAG_SPRITE_STAT_SEQUENCE;
    frame.value = (FLOAT)UI_PLAYERSTAT_ENV_VARIANT;
    frame.text = "#0";
    if (!frame.tex.index) return;

    /* Warsmash/retail-style time art is authored in model-space and anchored
     * at ConsoleUI's bottom-left; the model itself owns the clock geometry. */
    UI_SetFramePoint(&frame.points.x[FPP_MIN], FPP_MIN, UI_PARENT, 0.0f, false);
    UI_SetFramePoint(&frame.points.y[FPP_MAX], FPP_MAX, UI_PARENT, 0.0f, true);
    UI_WriteProxyFrameToParent(&frame, NULL, 0, parent);
}

void UI_WriteMinimapFrame(void) {
    uiFrame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_MINIMAP;
    frame.color = COLOR32_WHITE;
    frame.tex.coord[1] = 0xff;
    frame.tex.coord[3] = 0xff;
    UI_SetFrameRect(&frame, 0.0070f, 0.4525f, 0.1395f, 0.1395f);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

void UI_WriteConsoleBackdrop(LPGAMECLIENT client, LONG food_used, LONG food_cap) {
    DWORD upkeep_tier;
    LPCSTR upkeep_text;
    COLOR32 upkeep_color;
    LONG gold_rate;

    /* Keep symbolic DecorateFileNames keys in the payload; the local WC3 UI
     * resolves them for the recipient's race when the image configstring loads. */
    UI_SetCurrentClient(client);
    if (!hud.console.ConsoleUI) {
        UI_SetCurrentClient(NULL);
        return;
    }

    if (hud.upper.UpperButtonBarFrame) {
        LPFRAMEDEF buttons[] = {
            hud.upper.UpperButtonBarQuestsButton,
            hud.upper.UpperButtonBarMenuButton,
            hud.upper.UpperButtonBarAlliesButton,
            hud.upper.UpperButtonBarChatButton,
        };
        FOR_LOOP(i, 4) UI_SetOnClick(buttons[i], "%s", hud.upper_cmds[i]);
    }

    gold_rate = (LONG)client->ps.stats[PLAYERSTATE_GOLD_UPKEEP_RATE];

    upkeep_tier = G_GetPlayerUpkeepTier(client);
    upkeep_text = UI_UpkeepLabel(upkeep_tier);
    upkeep_color = upkeep_tier > 1 ? MAKE(COLOR32, 255, 64, 64, 255)
                 : upkeep_tier == 1 ? MAKE(COLOR32, 255, 200, 64, 255)
                                    : MAKE(COLOR32, 96, 255, 96, 255);
    UI_SetText(hud.res.ResourceBarUpkeepText, "%s", upkeep_text);
    hud.res.ResourceBarUpkeepText->Font.Color = upkeep_color;
    hud.res.ResourceBarSupplyText->Font.Color = food_used > food_cap
        ? MAKE(COLOR32, 255, 64, 64, 255)
        : COLOR32_WHITE;

    /* Resource-bar tooltip listeners follow these named value frames. Their
     * headings contain a client-side {value} token so hover text always uses
     * the same current Stat value as the visible resource bar. */
    UI_SetResourceTooltip(hud.res.ResourceBarGoldText, &resource_tooltips[0],
                          "COLON_GOLD", "Gold:", "RESOURCE_UBERTIP_GOLD");
    UI_SetResourceTooltip(hud.res.ResourceBarLumberText, &resource_tooltips[1],
                          "COLON_LUMBER", "Lumber:", "RESOURCE_UBERTIP_LUMBER");
    UI_SetResourceTooltip(hud.res.ResourceBarSupplyText, &resource_tooltips[2],
                          "COLON_FOOD", "Food:", "RESOURCE_UBERTIP_SUPPLY");
    UI_SetUpkeepTooltip(hud.res.ResourceBarUpkeepText, &resource_tooltips[3],
                        upkeep_tier, gold_rate);

    UI_WriteFrameWithChildren(hud.console.ConsoleUI, NULL);
    UI_WriteTimeOfDayIndicator(client);
    /* Resource-bar fields are present even with no unit selected, so the
     * console layer must carry its own standard tooltip presentation frame. */
    UI_WriteTooltipFrame();
    UI_SetCurrentClient(NULL);
}
