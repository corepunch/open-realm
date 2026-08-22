/*
 * g_ui.c — Server-authored WoW HUD via svc_layout.
 *
 * Reproduces the classic WoW 1.12 HUD layout (action bar, targeting frame,
 * minimap, copper) using the actual WoW assets and pixel positions from the
 * virtual 1024×768 canvas, exactly matching what ui.dll rendered before
 * in-game UI was moved server-side.
 */

#include "g_wow_local.h"
#include "common/wow_character_utils.h"

#define VW 1024.0f
#define VH 768.0f
#define PX(x) ((x) / VW)
#define PY(y) ((y) / VH)
#define PW(w) ((w) / VW)
#define PH(h) ((h) / VH)
#define HUD_FONT_SIZE 10
#define WOW_BUTTON_TEXT_COLOR MAKE(COLOR32, 255, 209, 0, 255) // RGBA; GameFontNormal 1.0/0.82/0; quest buttons

static DWORD ui_next_frame_number;

static void UI_WriteImage(LPCSTR path, FLOAT x, FLOAT y, FLOAT w, FLOAT h, COLOR32 color);

static void UI_SetFramePoint(uiFramePoint_t *point, uiFramePointPos_t target, DWORD relative, FLOAT offset, BOOL y_axis) {
    point->used = 1;
    point->targetPos = target;
    point->relativeTo = (BYTE)relative;
    point->offset = (SHORT)((y_axis ? -offset : offset) * UI_FRAMEPOINT_SCALE);
}

static void UI_SetFrameRect(LPUIFRAME frame, FLOAT x, FLOAT y, FLOAT w, FLOAT h) {
    UI_SetFramePoint(&frame->points.x[FPP_MIN], FPP_MIN, 0, x, false);
    UI_SetFramePoint(&frame->points.y[FPP_MIN], FPP_MIN, 0, y, true);
    frame->size.width = w;
    frame->size.height = h;
}

static void UI_WriteProxyFrame(LPUIFRAME frame, HANDLE data, DWORD data_size) {
    frame->number = ui_next_frame_number++;
    frame->parent = 0;
    frame->color = frame->color.a ? frame->color : COLOR32_WHITE;
    /* Set default full-UV only when caller left coords zeroed */
    if (!frame->tex.coord[1] && !frame->tex.coord[3]) {
        frame->tex.coord[1] = 0xff;
        frame->tex.coord[3] = 0xff;
    }
    frame->buffer.data = data;
    frame->buffer.size = data_size;
    gi.Write(PF_UIFRAME, frame);
}

static void UI_WriteTextFrame(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR text, COLOR32 color, uiFontJustificationH_t align) {
    uiFrame_t frame;
    uiLabel_t label;

    memset(&frame, 0, sizeof(frame));
    memset(&label, 0, sizeof(label));
    frame.flags.type = FT_STRING;
    frame.text = text;
    frame.color = color;
    label.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", HUD_FONT_SIZE);
    label.textalignx = align;
    label.textaligny = FONT_JUSTIFYTOP;
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

static void UI_WriteTextArea(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR text, COLOR32 color) {
    uiFrame_t frame;
    uiTextArea_t area;

    memset(&frame, 0, sizeof(frame));
    memset(&area, 0, sizeof(area));
    frame.flags.type = FT_TEXTAREA;
    frame.text = text ? text : "";
    frame.color = color;
    area.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", 13);
    area.inset = 0;
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, &area, sizeof(area));
}

/* UIPanelScrollFrameTemplate is one slider frame with fixed arrow and thumb textures. */
static void UI_WriteQuestScrollBar(FLOAT x, FLOAT y) {
    LPCSTR paths[] = {
        "Interface\\Buttons\\UI-ScrollBar-ScrollDownButton-Up.blp",
        "Interface\\Buttons\\UI-ScrollBar-ScrollUpButton-Up.blp",
        "Interface\\Buttons\\UI-ScrollBar-Knob.blp",
    };
    uiFrame_t frame = {0};
    uiScrollBarImage_t scroll = {0};

    frame.flags.type = FT_SCROLLBAR;
    FOR_LOOP(i, sizeof(paths) / sizeof(paths[0])) {
        scroll.image[i] = gi.ImageIndex(paths[i]);
    }
    scroll.texcoord[0] = scroll.texcoord[2] = (BYTE)(0.25f * 0xff);
    scroll.texcoord[1] = scroll.texcoord[3] = (BYTE)(0.75f * 0xff);
    UI_SetFrameRect(&frame, x + PW(329), y + PH(81), PW(16), PH(334));
    UI_WriteProxyFrame(&frame, &scroll, sizeof(scroll));
}

static void UI_WriteClickRegion(FLOAT x, FLOAT y, FLOAT w, FLOAT h, LPCSTR command) {
    uiFrame_t frame;
    uiLabel_t label;

    memset(&frame, 0, sizeof(frame));
    memset(&label, 0, sizeof(label));
    frame.flags.type = FT_STRING;
    frame.text = "";
    frame.onclick = command;
    label.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", HUD_FONT_SIZE);
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

static void UI_WriteSimpleButton(FLOAT x, FLOAT y, FLOAT w, FLOAT h,
                                 LPCSTR text, LPCSTR command) {
    uiFrame_t frame;
    uiSimpleButton_t button;
    RESOURCE texture = gi.ImageIndex("Interface\\Buttons\\UI-Panel-Button-Up.blp");
    RESOURCE font = gi.FontIndex("Fonts\\FRIZQT__.TTF", 12);

    memset(&frame, 0, sizeof(frame));
    memset(&button, 0, sizeof(button));
    frame.flags.type = FT_SIMPLEBUTTON;
    frame.text = text;
    frame.onclick = command;
    button.normal.texture = texture;
    button.normal.font = font;
    /* UIPanelButtonTemplate crops the atlas; full UVs made its opaque art occupy only part of the frame. */
    button.normal.texcoord[1] = (BYTE)(0.625f * 0xff);
    button.normal.texcoord[3] = (BYTE)(0.6875f * 0xff);
    button.normal.fontcolor = WOW_BUTTON_TEXT_COLOR;
    button.pushed = button.normal;
    button.disabled = button.normal;
    button.highlight = button.normal;
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, &button, sizeof(button));
}

/* QuestFrame.xml uses a distinct 18px Morpheus title inside the parchment. */
static void UI_WriteQuestTitle(FLOAT x, FLOAT y, LPCSTR text) {
    uiFrame_t frame = {0};
    uiLabel_t label = {0};

    frame.flags.type = FT_STRING;
    frame.text = text;
    frame.color = MAKE(COLOR32, 0, 0, 0, 255);
    label.font = gi.FontIndex("Fonts\\MORPHEUS.ttf", 18);
    label.textalignx = FONT_JUSTIFYLEFT;
    label.textaligny = FONT_JUSTIFYTOP;
    UI_SetFrameRect(&frame, x + PW(28), y + PH(91), PW(285), PH(24));
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

/* QuestNpcNameFrame centers the giver name in the metal title bar. */
static void UI_WriteQuestNpcName(FLOAT x, FLOAT y, LPCSTR text) {
    uiFrame_t frame = {0};
    uiLabel_t label = {0};

    frame.flags.type = FT_STRING;
    frame.text = text;
    frame.color = COLOR32_WHITE;
    label.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", 12);
    label.textalignx = FONT_JUSTIFYCENTER;
    label.textaligny = FONT_JUSTIFYTOP;
    /* QuestNpcNameFrame is TOP-anchored 23px below QuestFrame; the old 17px offset placed text over the border. */
    UI_SetFrameRect(&frame, x + PW(42), y + PH(23), PW(300), PH(14));
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

/* The quest giver's model occupies the 60px portrait aperture in QuestFrame.xml. */
static void UI_WriteQuestPortrait(FLOAT x, FLOAT y, RESOURCE model) {
    uiFrame_t frame = {0};
    if (!model) return;
    frame.flags.type = FT_PORTRAIT;
    frame.tex.index = model;
    UI_SetFrameRect(&frame, x + PW(7), y + PH(6), PW(60), PH(60));
    UI_WriteProxyFrame(&frame, NULL, 0);
}

typedef struct { LPEDICT ent; LPCSTR src; LPSTR dst; size_t size; } wowQuestText_t;

/* Expand the player tokens retained from authoritative quest_template text. */
static void UI_FormatQuestText(wowQuestText_t const *fmt) {
    char race[64], sex[64];
    LPCSTR name = fmt->ent->client->ps.name && *fmt->ent->client->ps.name ? fmt->ent->client->ps.name : "adventurer";
    LPCSTR cls = Wow_ClassName(Wow_GetPlayerClass());
    Wow_GetPlayerRaceSex(race, sizeof(race), sex, sizeof(sex));
    struct { char key; LPCSTR value; } repl[] = {
        { 'N', name }, { 'n', name }, { 'C', cls }, { 'c', cls },
        { 'R', race }, { 'r', race },
    };
    size_t out = 0;

    for (LPCSTR src = fmt->src ? fmt->src : ""; *src && out + 1 < fmt->size; src++) {
        LPCSTR value = NULL;
        if (*src == '$' && src[1]) FOR_LOOP(i, sizeof(repl) / sizeof(repl[0]))
            if (repl[i].key == src[1]) { value = repl[i].value; break; }
        if (!value) { fmt->dst[out++] = *src; continue; }
        size_t len = strlen(value);
        if (len > fmt->size - out - 1) len = fmt->size - out - 1;
        memcpy(fmt->dst + out, value, len); out += len; src++;
    }
    fmt->dst[out] = '\0';
}

/* Resolve the queststarter relation back to the creature-template name. */
static LPCSTR UI_QuestGiverName(DWORD quest_id) {
    FOR_LOOP(i, Wow_QuestGiverCount()) {
        LPCWOWQUESTGIVER data = Wow_QuestGiver(i);
        LPCWOWCREATURE creature;
        if (data->quest_id != quest_id) continue;
        creature = Wow_CreatureByEntry(data->creature_entry);
        return creature ? creature->name : NULL;
    }
    return NULL;
}

/* The client ships QuestFrame.xml/Lua, but WoW game mode suppresses client UI
 * scripts. Recreate the classic quest dialog and quest log on one shared layer.
 * Both UI_WriteQuestDialog and UI_WriteQuestLog previously wrote separate
 * svc_layout messages to LAYER_QUESTDIALOG; the second write always cleared the
 * first.  Merged into a single write: quest_open takes priority, then
 * questlog_open, otherwise the layer is cleared. */
static void UI_WriteQuestDialog(LPEDICT ent) {
    wowClient_t *wc = (wowClient_t *)ent->client;

    gi.Write(PF_BYTE, &(LONG){svc_layout});
    gi.Write(PF_BYTE, &(LONG){LAYER_QUESTDIALOG});
    ui_next_frame_number = 1;

    if (wc->quest_open) {
        LPCWOWQUESTDETAIL detail = Wow_QuestDetail(wc->quest_id);
        svQuestEntry_t *state = SV_QuestFind(wc->client.ps.quest_log, wc->client.ps.quest_count, wc->quest_id);
        DWORD slot = state ? (DWORD)(state - wc->client.ps.quest_log) : 0;
        LPEDICT selected = wc->client.ps.selected_entity && wc->client.ps.selected_entity < (DWORD)globals.num_edicts
            ? &wow_edicts[wc->client.ps.selected_entity] : NULL;
        wowEntityLocal_t *giver = selected ? Wow_EntityLocal(selected) : NULL;
        LPCSTR giver_name = NULL;
        char command[64], desc[2048], obj[1024], text[3072];
        FLOAT x = PX(0), y = PY(104);
        BOOL is_complete = state && state->status == SV_QUEST_COMPLETE;
        BOOL is_accepted = state && state->status == SV_QUEST_ACTIVE;

        UI_WriteImage("Interface\\QuestFrame\\UI-QuestGreeting-TopLeft.blp", x, y, PW(256), PH(256), COLOR32_WHITE);
        UI_WriteImage("Interface\\QuestFrame\\UI-QuestGreeting-TopRight.blp", x + PW(256), y, PW(128), PH(256), COLOR32_WHITE);
        UI_WriteImage("Interface\\QuestFrame\\UI-QuestGreeting-BotLeft.blp", x, y + PH(256), PW(256), PH(256), COLOR32_WHITE);
        UI_WriteImage("Interface\\QuestFrame\\UI-QuestGreeting-BotRight.blp", x + PW(256), y + PH(256), PW(128), PH(256), COLOR32_WHITE);

        giver_name = UI_QuestGiverName(giver ? giver->quest_id : detail ? detail->quest_id : 0);
        UI_WriteQuestPortrait(x, y, selected ? selected->s.model : 0);
        UI_WriteQuestNpcName(x, y, giver_name ? giver_name : "Quest");
        UI_WriteImage("Interface\\Buttons\\UI-Panel-MinimizeButton-Up.blp", x + PW(326), y + PH(14), PW(32), PH(32), COLOR32_WHITE);
        UI_WriteClickRegion(x + PW(326), y + PH(14), PW(32), PH(32), "quest_close");
        UI_WriteQuestTitle(x, y, detail ? detail->title : "Quest");

        if (detail) {
            int off = 0;
            UI_FormatQuestText(&(wowQuestText_t){ ent, is_complete ? detail->reward_text : detail->description, desc, sizeof(desc) });
            UI_FormatQuestText(&(wowQuestText_t){ ent, detail->objectives_text, obj, sizeof(obj) });
            if (is_complete)
                off = snprintf(text, sizeof(text), "%s\n\nRewards:\n%d XP  |  %u copper", desc, (int)detail->reward_xp, (unsigned)detail->reward_gold);
            else {
                off = snprintf(text, sizeof(text), "%s%s%s", desc, obj[0] ? "\n\n" : "", obj);
                if (is_accepted && detail->kill_objective_count) {
                    off += snprintf(text + off, sizeof(text) - off, "\n\nProgress:");
                    FOR_LOOP(j, detail->kill_objective_count) {
                        LPCSTR name = Wow_CachedCreatureName(detail->kill_objectives[j].display_id);
                        off += snprintf(text + off, sizeof(text) - off, "\n  %s: %u/%u", name ? name : "Creature", (unsigned)(state ? wc->kill_progress[slot][j] : 0), (unsigned)detail->kill_objectives[j].required_count);
                    }
                }
            }
        } else {
            snprintf(text, sizeof(text), "Quest data not available.");
        }
        UI_WriteTextArea(x + PW(28), y + PH(116), PW(270), PH(286), text, MAKE(COLOR32, 0, 0, 0, 255));
        UI_WriteQuestScrollBar(x, y);

        if (is_complete) {
            snprintf(command, sizeof(command), "quest_complete %u", (unsigned)wc->quest_id);
            UI_WriteSimpleButton(x + PW(23), y + PH(418), PW(120), PH(22), "Complete Quest", command);
        } else if (!is_accepted) {
            snprintf(command, sizeof(command), "quest_accept %u", (unsigned)wc->quest_id);
            UI_WriteSimpleButton(x + PW(23), y + PH(418), PW(77), PH(22), "Accept", command);
        }
        UI_WriteSimpleButton(x + PW(267), y + PH(418), PW(78), PH(22), "Decline", "quest_close");
    } else if (wc->questlog_open) {
        FLOAT x = PX(24), y = PY(68);
        FLOAT line_y = y + PH(48);
        DWORD line_count = 0;
        char buf[128], cmd[64];

        UI_WriteImage("Interface\\QuestFrame\\UI-QuestGreeting-TopLeft.blp", x, y, PW(256), PH(128), COLOR32_WHITE);
        UI_WriteImage("Interface\\QuestFrame\\UI-QuestGreeting-TopRight.blp", x + PW(256), y, PW(128), PH(128), COLOR32_WHITE);
        UI_WriteImage("Interface\\QuestFrame\\UI-QuestGreeting-BotLeft.blp", x, y + PH(256), PW(256), PH(256), COLOR32_WHITE);
        UI_WriteImage("Interface\\QuestFrame\\UI-QuestGreeting-BotRight.blp", x + PW(256), y + PH(256), PW(128), PH(256), COLOR32_WHITE);

        UI_WriteTextFrame(x + PW(42), y + PH(12), PW(280), PH(22), "Quest Log", MAKE(COLOR32, 255, 215, 120, 255), FONT_JUSTIFYCENTER);

        if (!wc->client.ps.quest_count) {
            UI_WriteTextFrame(x + PW(42), line_y, PW(280), PH(22), "No active quests.", MAKE(COLOR32, 160, 150, 140, 255), FONT_JUSTIFYCENTER);
        } else FOR_LOOP(i, wc->client.ps.quest_count) {
            svQuestEntry_t *qs = &wc->client.ps.quest_log[i];
            LPCWOWQUESTDETAIL detail = Wow_QuestDetail(qs->quest_id);
            LPCSTR status = qs->status == SV_QUEST_COMPLETE ? " (Complete)" : "";

            snprintf(buf, sizeof(buf), "%s%s", detail ? detail->title : "Unknown Quest", status);
            snprintf(cmd, sizeof(cmd), "quest %u", (unsigned)qs->quest_id);
            UI_WriteSimpleButton(x + PW(28), line_y, PW(308), PH(22), buf, cmd);
            line_y += PH(28);
            if (++line_count >= 14) break;
        }
        UI_WriteSimpleButton(x + PW(250), y + PH(450), PW(90), PH(28), "Close", "quest_close");
    }

    gi.Write(PF_LONG, &(LONG){0});
    gi.Write(PF_SHORT, &(LONG){0});
}

static void UI_WriteQuestLog(LPEDICT ent) {
    (void)ent; /* merged into UI_WriteQuestDialog */
}

/* Write an FT_TEXTURE frame with float-precision UV (supports l>r or t>b for flips). */
static void UI_WriteImageUV(LPCSTR path, FLOAT x, FLOAT y, FLOAT w, FLOAT h,
                            FLOAT l, FLOAT r, FLOAT t, FLOAT b, COLOR32 color) {
    uiFrame_t frame;
    uiTextureUV_t uv;

    memset(&frame, 0, sizeof(frame));
    memset(&uv, 0, sizeof(uv));
    frame.flags.type = FT_TEXTURE;
    frame.color = color;
    frame.tex.index = gi.ImageIndex(path);
    uv.l = l; uv.r = r; uv.t = t; uv.b = b;
    uv.color = color;
    uv.alphamode = BLEND_MODE_ALPHAKEY;
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, &uv, sizeof(uv));
}

static void UI_WriteImage(LPCSTR path, FLOAT x, FLOAT y, FLOAT w, FLOAT h, COLOR32 color) {
    UI_WriteImageUV(path, x, y, w, h, 0.0f, 1.0f, 0.0f, 1.0f, color);
}

/* Solid-color quad via a null texture slot */
static void UI_WriteColorRect(FLOAT x, FLOAT y, FLOAT w, FLOAT h, COLOR32 color) {
    uiFrame_t frame;

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_TEXTURE;
    frame.color = color;
    frame.tex.index = 0;
    frame.tex.coord[1] = 0xff;
    frame.tex.coord[3] = 0xff;
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, NULL, 0);
}

/* Solid health/mana bar drawn as two color rects (dark background + colored fill) */
static void UI_WriteColorBar(FLOAT x, FLOAT y, FLOAT w, FLOAT h,
                             FLOAT value, FLOAT maxvalue,
                             COLOR32 fill_color) {
    FLOAT p = maxvalue > 0.0f ? value / maxvalue : 0.0f;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    UI_WriteColorRect(x, y, w, h, MAKE(COLOR32, 12, 10, 8, 220));
    if (p > 0.0f)
        UI_WriteColorRect(x + PW(2), y + PH(2), (w - PW(4)) * p, h - PH(4), fill_color);
}

/* Minimap: border image + actual minimap viewport */
static void UI_WriteMinimapFrames(void) {
    uiFrame_t minimap;

    /* Minimap border overlay */
    UI_WriteImage("Interface\\Minimap\\UI-Minimap-Border.blp", PX(879), PY(8), PW(128), PH(128), COLOR32_WHITE);

    /* Minimap viewport — FT_MINIMAP; client calls DrawMinimap() for this rect. */
    memset(&minimap, 0, sizeof(minimap));
    minimap.flags.type = FT_MINIMAP;
    minimap.color = COLOR32_WHITE;
    UI_SetFrameRect(&minimap, PX(896), PY(25), PW(91), PH(91));
    UI_WriteProxyFrame(&minimap, NULL, 0);
}

/* Main action bar: four 256×53 strips + two end-caps from UI-MainMenuBar-Dwarf.blp */
static void UI_WriteActionBar(void) {
    static LPCSTR const bar = "Interface\\MainMenuBar\\UI-MainMenuBar-Dwarf.blp";
    static LPCSTR const cap = "Interface\\MainMenuBar\\UI-MainMenuBar-EndCap-Dwarf.blp";
    /* Each strip covers a different vertical slice of the texture (v slices at 53/256 intervals) */
    static FLOAT const strips[4][4] = {
        /* {l, r, t, b}, screen x starts at 0 */
        { 0.0f, 1.0f, 0.79296875f, 1.0f },
        { 0.0f, 1.0f, 0.54296875f, 0.75f },
        { 0.0f, 1.0f, 0.29296875f, 0.5f },
        { 0.0f, 1.0f, 0.04296875f, 0.25f },
    };

    FOR_LOOP(i, 4)
        UI_WriteImageUV(bar, PX((FLOAT)(i * 256)), PY(715), PW(256), PH(53), strips[i][0], strips[i][1], strips[i][2], strips[i][3], COLOR32_WHITE);

    /* Left end-cap (normal orientation) */
    UI_WriteImage(cap, PX(-96), PY(640), PW(128), PH(128), COLOR32_WHITE);
    /* Right end-cap (horizontally flipped: l=1, r=0) */
    UI_WriteImageUV(cap, PX(992), PY(640), PW(128), PH(128), 1.0f, 0.0f, 0.0f, 1.0f, COLOR32_WHITE);
}

/* Action button slot at grid position i (0..11 = left row, 12..15 = right empty slots) */
static void UI_WriteActionButtonSlot(FLOAT x, FLOAT y, DWORD image_index, DWORD count) {
    char count_buf[16];

    /* Slot frame */
    UI_WriteImage("Interface\\Buttons\\UI-Quickslot2.blp", x + PX(-14), y + PY(-13), PW(64), PH(64), COLOR32_WHITE);
    /* Icon (may be 0 = empty slot, renderer draws nothing for index 0) */
    if (image_index) {
        uiFrame_t icon;
        memset(&icon, 0, sizeof(icon));
        icon.flags.type = FT_TEXTURE;
        icon.color = COLOR32_WHITE;
        icon.tex.index = image_index;
        icon.tex.coord[1] = 0xff;
        icon.tex.coord[3] = 0xff;
        UI_SetFrameRect(&icon, x + PX(2), y + PY(2), PW(32), PH(32));
        UI_WriteProxyFrame(&icon, NULL, 0);
    }
    /* The old Lua HUD drew stack counts in the corner of action buttons; keep
     * the server-authored HUD visually identical by writing the same overlay. */
    if (count > 1) {
        snprintf(count_buf, sizeof(count_buf), "%u", (unsigned)count);
        UI_WriteTextFrame(x + PX(2), y + PY(23), PW(32), PH(10), count_buf, COLOR32_WHITE, FONT_JUSTIFYRIGHT);
    }
}

/* Targeting frame: the WoW character frame backdrop + health/mana bars + name/level text */
static void UI_WriteTargetingFrame(LPEDICT ent) {
    LPPLAYER ps = &ent->client->ps;
    char name_buf[64], level_buf[32];

    /* Character frame backdrop — drawn with a slight tint matching the original */
    UI_WriteImageUV("Interface\\TargetingFrame\\UI-TargetingFrame.blp", PX(-19), PY(4), PW(232), PH(100), 1.0f, 0.09375f, 0.0f, 0.78125f, MAKE(COLOR32, 96, 92, 84, 230));

    /* Classic 1.12 unit-frame portraits are 2D per-race/sex textures, already oval-masked. */
    {
        char race[64], sex[64], path[256];
        Wow_GetPlayerRaceSex(race, sizeof(race), sex, sizeof(sex));
        snprintf(path, sizeof(path), "Interface\\CharacterFrame\\TemporaryPortrait-%s-%s.blp", sex, race);
        UI_WriteImage(path, PX(23), PY(16), PW(64), PH(64), COLOR32_WHITE);
    }

    /* Dark name area */
    UI_WriteColorRect(PX(87), PY(22), PW(119), PH(41), MAKE(COLOR32, 0, 0, 0, 128));

    /* Name */
    snprintf(name_buf, sizeof(name_buf), "%s", ps->name && *ps->name ? ps->name : "Player");
    UI_WriteTextFrame(PX(72), PY(18), PW(100), PH(12), name_buf, MAKE(COLOR32, 255, 215, 120, 255), FONT_JUSTIFYCENTER);

    /* Level */
    snprintf(level_buf, sizeof(level_buf), "Lvl %d", (int)ps->stats[WOW_STAT_LEVEL]);
    UI_WriteTextFrame(PX(24), PY(58), PW(42), PH(12), level_buf, MAKE(COLOR32, 235, 225, 190, 255), FONT_JUSTIFYCENTER);

    /* Health bar */
    UI_WriteColorBar(PX(105), PY(41), PW(119), PH(12), (FLOAT)ps->stats[WOW_STAT_HEALTH], (FLOAT)ps->stats[WOW_STAT_HEALTH_MAX], MAKE(COLOR32, 20, 178, 48, 235));

    /* Mana/power bar */
    UI_WriteColorBar(PX(105), PY(54), PW(119), PH(11), (FLOAT)ps->stats[WOW_STAT_POWER], (FLOAT)ps->stats[WOW_STAT_POWER_MAX], MAKE(COLOR32, 26, 82, 210, 235));
}

/* -------------------------------------------------------------------------
 * Loot window — shows corpse loot snapshot, one row per item with click-to-take.
 * Items are stored in client->loot_snap[] so the window is fully server-authored
 * and requires no direct entity access in the layout pass.
 * -------------------------------------------------------------------------*/
static void UI_WriteLootWindow(LPEDICT ent) {
    wowClient_t *wc = (wowClient_t *)ent->client;
    char buf[96];
    DWORD visible = 0;
    FLOAT x, y, h;

    if (!wc->loot_target) return;

    /* Count visible slots to size the panel dynamically. */
    FOR_LOOP(i, WOW_MAX_LOOT_ITEMS) if (wc->loot_snap[i].icon[0]) visible++;
    if (!visible) { /* nothing left; auto-close */
        wc->loot_target = 0;
        return;
    }

    x = PX(300.0f); y = PY(160.0f);
    h = PH(52.0f + (FLOAT)visible * 36.0f);

    /* Dark parchment background + gold border */
    UI_WriteColorRect(x, y, PW(380.0f), h, MAKE(COLOR32, 20, 16, 10, 230));
    UI_WriteColorRect(x, y, PW(380.0f), PH(1.0f), MAKE(COLOR32, 170, 140, 60, 255));
    UI_WriteColorRect(x, y + h, PW(380.0f), PH(1.0f), MAKE(COLOR32, 170, 140, 60, 255));
    UI_WriteColorRect(x, y, PW(1.0f), h, MAKE(COLOR32, 170, 140, 60, 255));
    UI_WriteColorRect(x + PW(379.0f), y, PW(1.0f), h, MAKE(COLOR32, 170, 140, 60, 255));

    /* Title bar */
    UI_WriteColorRect(x, y, PW(380.0f), PH(22.0f), MAKE(COLOR32, 50, 40, 20, 240));
    UI_WriteTextFrame(x + PW(8), y + PH(5), PW(200), PH(14), "Loot",
                      MAKE(COLOR32, 255, 215, 120, 255), FONT_JUSTIFYLEFT);

    /* Close button — top right corner */
    UI_WriteSimpleButton(x + PW(320), y + PH(3), PW(56), PH(18), "Close", "loot_close");

    /* Item rows */
    DWORD row = 0;
    FOR_LOOP(i, WOW_MAX_LOOT_ITEMS) {
        FLOAT row_y;
        DWORD icon_img;
        char cmd[32];

        if (!wc->loot_snap[i].icon[0]) continue;
        row_y = y + PH(26.0f + (FLOAT)row * 36.0f);

        /* Slot background */
        UI_WriteColorRect(x + PW(8), row_y, PW(362.0f), PH(32.0f), MAKE(COLOR32, 35, 28, 16, 200));

        /* Item icon */
        icon_img = gi.ImageIndex(wc->loot_snap[i].icon);
        if (icon_img) {
            uiFrame_t icon; memset(&icon, 0, sizeof(icon));
            icon.flags.type = FT_TEXTURE;
            icon.color = COLOR32_WHITE;
            icon.tex.index = icon_img;
            icon.tex.coord[1] = 0xff; icon.tex.coord[3] = 0xff;
            UI_SetFrameRect(&icon, x + PW(12), row_y + PH(2), PW(28), PH(28));
            UI_WriteProxyFrame(&icon, NULL, 0);
        }

        /* Item name + count */
        if (wc->loot_snap[i].count > 1)
            snprintf(buf, sizeof(buf), "%s x%u", wc->loot_snap[i].name, (unsigned)wc->loot_snap[i].count);
        else
            snprintf(buf, sizeof(buf), "%s", wc->loot_snap[i].name);
        UI_WriteTextFrame(x + PW(48), row_y + PH(9), PW(280), PH(16), buf, COLOR32_WHITE, FONT_JUSTIFYLEFT);

        /* Click region: "loot_take <slot>" */
        snprintf(cmd, sizeof(cmd), "loot_take %u", (unsigned)i);
        UI_WriteClickRegion(x + PW(8), row_y, PW(360.0f), PH(32.0f), cmd);
        row++;
    }
}

/* -------------------------------------------------------------------------
 * Backpack window — 4×4 grid showing all WOW_UI_INVENTORY_SLOTS item slots.
 * Positioned in the upper-right, above the bag slot row.
 * -------------------------------------------------------------------------*/
static void UI_WriteBackpackWindow(LPEDICT ent) {
    wowClient_t *wc = (wowClient_t *)ent->client;
    FLOAT x, y, w, h;

    if (!wc->backpack_open) return;

    x = PX(818.0f); y = PY(478.0f);
    w = PW(190.0f); h = PH(230.0f);

    /* Background + border */
    UI_WriteColorRect(x, y, w, h, MAKE(COLOR32, 20, 16, 10, 230));
    UI_WriteColorRect(x, y, w, PH(1.0f), MAKE(COLOR32, 170, 140, 60, 255));
    UI_WriteColorRect(x, y + h, w, PH(1.0f), MAKE(COLOR32, 170, 140, 60, 255));
    UI_WriteColorRect(x, y, PW(1.0f), h, MAKE(COLOR32, 170, 140, 60, 255));
    UI_WriteColorRect(x + w, y, PW(1.0f), h, MAKE(COLOR32, 170, 140, 60, 255));

    /* Title bar */
    UI_WriteColorRect(x, y, w, PH(22.0f), MAKE(COLOR32, 50, 40, 20, 240));
    UI_WriteTextFrame(x + PW(6), y + PH(5), PW(100), PH(14), "Backpack",
                      MAKE(COLOR32, 255, 215, 120, 255), FONT_JUSTIFYLEFT);
    UI_WriteSimpleButton(x + w - PW(54), y + PH(3), PW(50), PH(18), "Close", "backpack");

    /* 4×4 grid of item slots */
    FOR_LOOP(i, WOW_UI_INVENTORY_SLOTS) {
        FLOAT sx = x + PW(8.0f + (FLOAT)(i % 4) * 44.0f);
        FLOAT sy = y + PH(26.0f + (FLOAT)(i / 4) * 44.0f);
        DWORD img = wc->inventory[i].icon[0] ? gi.ImageIndex(wc->inventory[i].icon) : 0;
        UI_WriteActionButtonSlot(sx, sy, img, wc->inventory[i].count);
    }
}

/* Send svc_window to show (show=1) or hide (show=0) a named XML window. */
static void UI_WriteWindowMsg(LPCSTR window_id, int show) {
    gi.Write(PF_BYTE, &(LONG){svc_window});
    gi.Write(PF_STRING, window_id);
    gi.Write(PF_BYTE, &(LONG){show});
}

/* Show the classic "Welcome to World of Warcraft" message box for ent. */
void UI_WriteWelcomeWindow(LPEDICT ent) {
    UI_WriteWindowMsg("WelcomeFrame", 1);
    gi.unicast(ent);
}

/* Hide a named window by ID. */
void UI_HideWindow(LPEDICT ent, LPCSTR window_id) {
    if (!window_id || !window_id[0]) return;
    UI_WriteWindowMsg(window_id, 0);
    gi.unicast(ent);
}

/* Build and unicast the WoW HUD layer for a player */
void UI_WriteWowHud(LPEDICT ent) {
    LPPLAYER ps;
    wowClient_t *wc;
    char copper_buf[64];

    if (!ent || !ent->client)
        return;
    ps = &ent->client->ps;
    wc = (wowClient_t *)ent->client;

    gi.Write(PF_BYTE, &(LONG){svc_layout});
    gi.Write(PF_BYTE, &(LONG){LAYER_CONSOLE});
    ui_next_frame_number = 1;

    /* Character/targeting frame (portrait area top-left) */
    UI_WriteTargetingFrame(ent);

    /* Damage flash overlays — brief text shown after hits.  Timers tick in Wow_UpdatePlayerHud. */
    if (wc->outgoing_dmg_timer > 0) {
        char dmg_buf[32];
        snprintf(dmg_buf, sizeof(dmg_buf), "-%u", (unsigned)wc->outgoing_damage);
        UI_WriteTextFrame(PX(180.0f), PY(72.0f), PW(80), PH(18), dmg_buf,
                          MAKE(COLOR32, 255, 255, 50, 255), FONT_JUSTIFYLEFT);
    }
    if (wc->incoming_dmg_timer > 0) {
        char dmg_buf[32];
        snprintf(dmg_buf, sizeof(dmg_buf), "-%u", (unsigned)wc->incoming_damage);
        UI_WriteTextFrame(PX(130.0f), PY(60.0f), PW(80), PH(18), dmg_buf,
                          MAKE(COLOR32, 255, 60, 60, 255), FONT_JUSTIFYLEFT);
    }

    /* Main action bar + end-caps */
    UI_WriteActionBar();

    /* 12 action buttons, left row */
    FOR_LOOP(i, 12) {
        DWORD img = wc->actions[i].icon[0] ? gi.ImageIndex(wc->actions[i].icon) : 0;
        UI_WriteActionButtonSlot(PX(8.0f + (FLOAT)i * 42.0f), PY(728), img, wc->actions[i].count);
    }

    /* First 6 inventory slots shown in the quick-access bar; all 16 visible in backpack window. */
    FOR_LOOP(i, 6) {
        DWORD img = wc->inventory[i].icon[0] ? gi.ImageIndex(wc->inventory[i].icon) : 0;
        UI_WriteActionButtonSlot(PX(939.0f - (FLOAT)i * 42.0f), PY(728), img, wc->inventory[i].count);
    }

    /* Backpack button — image + click region to toggle the backpack window */
    UI_WriteImage("Interface\\Buttons\\Button-Backpack-Up.blp", PX(981), PY(729), PW(37), PH(37), COLOR32_WHITE);
    UI_WriteClickRegion(PX(981), PY(729), PW(37), PH(37), "backpack");

    /* Minimap border + viewport */
    UI_WriteMinimapFrames();

    /* Quest log icon + label */
    UI_WriteImage("Interface\\QuestFrame\\UI-QuestLog-BookIcon.blp", PX(840), PY(162), PW(32), PH(32), COLOR32_WHITE);
    UI_WriteTextFrame(PX(876), PY(164), PW(110), PH(20), "Quests", MAKE(COLOR32, 255, 215, 120, 255), FONT_JUSTIFYLEFT);
    UI_WriteClickRegion(PX(834), PY(156), PW(72), PH(44), "quest");
    UI_WriteClickRegion(PX(910), PY(156), PW(78), PH(44), "questlog");

    /* Copper display */
    snprintf(copper_buf, sizeof(copper_buf), "Copper %d", (int)ps->stats[WOW_STAT_COPPER]);
    UI_WriteTextFrame(PX(816), PY(704), PW(150), PH(20), copper_buf, MAKE(COLOR32, 255, 210, 100, 255), FONT_JUSTIFYRIGHT);

    /* Cast bar — centered above action bar, shown during spell casts */
    {
        USHORT progress = ps->stats[WOW_STAT_CAST_PROGRESS];
        USHORT max_val = ps->stats[WOW_STAT_CAST_MAX];
        if (max_val > 0) {
            char text[64];
            snprintf(text, sizeof(text), "%.1f s", (FLOAT)progress / 1000.0f);
            /* Background */
            UI_WriteColorRect(PX(262), PY(690), PW(500), PH(28), MAKE(COLOR32, 0, 0, 0, 192));
            /* Fill bar: width * (1 - progress/max) since progress counts down */
            {
                FLOAT ratio = (FLOAT)(max_val - progress) / (FLOAT)max_val;
                UI_WriteColorBar(PX(263), PY(691), PW(498), PH(26), ratio, 1.0f, MAKE(COLOR32, 255, 200, 50, 255));
            }
            /* Border */
            UI_WriteColorRect(PX(262), PY(690), PW(500), PH(1), COLOR32_WHITE);
            UI_WriteColorRect(PX(262), PY(717), PW(500), PH(1), COLOR32_WHITE);
            UI_WriteColorRect(PX(262), PY(691), PW(1), PH(27), COLOR32_WHITE);
            UI_WriteColorRect(PX(761), PY(691), PW(1), PH(27), COLOR32_WHITE);
            /* Time text */
            UI_WriteTextFrame(PX(462), PY(695), PW(100), PH(20), text, COLOR32_WHITE, FONT_JUSTIFYCENTER);
        }
    }

    gi.Write(PF_LONG, &(LONG){0});
    gi.Write(PF_SHORT, &(LONG){0});
    UI_WriteQuestDialog(ent);
    UI_WriteQuestLog(ent);
    UI_WriteLootWindow(ent);
    UI_WriteBackpackWindow(ent);
    gi.unicast(ent);
}
