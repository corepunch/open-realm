/*
 * g_ui.c — Server-authored WoW HUD via svc_layout.
 *
 * Reproduces the classic WoW 1.12 HUD layout (action bar, targeting frame,
 * minimap, copper) using the actual WoW assets and pixel positions from the
 * virtual 1024×768 canvas, exactly matching what ui.dll rendered before
 * in-game UI was moved server-side.
 */

#include "g_wow_local.h"

#define VW 1024.0f
#define VH 768.0f
#define PX(x) ((x) / VW)
#define PY(y) ((y) / VH)
#define PW(w) ((w) / VW)
#define PH(h) ((h) / VH)
#define HUD_FONT_SIZE 10

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
    area.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", 10);
    area.inset = PW(8);
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, &area, sizeof(area));
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
    RESOURCE font = gi.FontIndex("Fonts\\FRIZQT__.TTF", HUD_FONT_SIZE);

    memset(&frame, 0, sizeof(frame));
    memset(&button, 0, sizeof(button));
    frame.flags.type = FT_SIMPLEBUTTON;
    frame.text = text;
    frame.onclick = command;
    button.normal.texture = texture;
    button.normal.font = font;
    button.normal.texcoord[1] = 0xff;
    button.normal.texcoord[3] = 0xff;
    button.normal.fontcolor = COLOR32_WHITE;
    button.pushed = button.normal;
    button.disabled = button.normal;
    button.highlight = button.normal;
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, &button, sizeof(button));
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
        wowQuestState_t *state = Wow_FindQuestState(wc, wc->quest_id);
        char command[64], text[512];
        FLOAT x = PX(24), y = PY(104);
        BOOL is_complete = state && state->status == WOW_QUEST_COMPLETE;
        BOOL is_accepted = state && state->status == WOW_QUEST_ACCEPTED;

        UI_WriteImage("Interface\\QuestFrame\\UI-QuestGreeting-TopLeft.blp", x, y, PW(256), PH(256), COLOR32_WHITE);
        UI_WriteImage("Interface\\QuestFrame\\UI-QuestGreeting-TopRight.blp", x + PW(256), y, PW(128), PH(256), COLOR32_WHITE);
        UI_WriteImage("Interface\\QuestFrame\\UI-QuestGreeting-BotLeft.blp", x, y + PH(256), PW(256), PH(256), COLOR32_WHITE);
        UI_WriteImage("Interface\\QuestFrame\\UI-QuestGreeting-BotRight.blp", x + PW(256), y + PH(256), PW(128), PH(256), COLOR32_WHITE);

        UI_WriteTextFrame(x + PW(42), y + PH(18), PW(280), PH(22), detail ? detail->title : "Quest", MAKE(COLOR32, 255, 215, 120, 255), FONT_JUSTIFYCENTER);

        if (detail) {
            int off = 0;
            if (is_complete)
                off = snprintf(text, sizeof(text), "%s\\n\\nRewards:\\n%d XP  |  %u copper", detail->reward_text, (int)detail->reward_xp, (unsigned)detail->reward_gold);
            else {
                off = snprintf(text, sizeof(text), "%s\\n\\n%s", detail->description, detail->objectives_text);
                if (is_accepted && detail->kill_objective_count) {
                    off += snprintf(text + off, sizeof(text) - off, "\\n\\nProgress:");
                    FOR_LOOP(j, detail->kill_objective_count) {
                        LPCSTR name = Wow_CachedCreatureName(detail->kill_objectives[j].display_id);
                        off += snprintf(text + off, sizeof(text) - off, "\\n  %s: %u/%u", name ? name : "Creature", (unsigned)(state ? state->kill_progress[j] : 0), (unsigned)detail->kill_objectives[j].required_count);
                    }
                }
            }
        } else {
            snprintf(text, sizeof(text), "Quest data not available.");
        }
        UI_WriteTextArea(x + PW(28), y + PH(82), PW(328), PH(320), text, MAKE(COLOR32, 240, 230, 205, 255));

        if (is_complete) {
            snprintf(command, sizeof(command), "quest_complete %u", (unsigned)wc->quest_id);
            UI_WriteSimpleButton(x + PW(22), y + PH(420), PW(180), PH(28), "Complete Quest", command);
        } else if (!is_accepted) {
            snprintf(command, sizeof(command), "quest_accept %u", (unsigned)wc->quest_id);
            UI_WriteSimpleButton(x + PW(22), y + PH(420), PW(120), PH(28), "Accept", command);
        }
        UI_WriteSimpleButton(x + PW(250), y + PH(420), PW(90), PH(28), "Close", "quest_close");
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

        if (!wc->quest_count) {
            UI_WriteTextFrame(x + PW(42), line_y, PW(280), PH(22), "No active quests.", MAKE(COLOR32, 160, 150, 140, 255), FONT_JUSTIFYCENTER);
        } else FOR_LOOP(i, wc->quest_count) {
            wowQuestState_t *qs = &wc->quests[i];
            LPCWOWQUESTDETAIL detail = Wow_QuestDetail(qs->quest_id);
            LPCSTR status = qs->status == WOW_QUEST_COMPLETE ? " (Complete)" : "";

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

    /* Main action bar + end-caps */
    UI_WriteActionBar();

    /* 12 action buttons, left row */
    FOR_LOOP(i, 12) {
        DWORD img = wc->actions[i].icon[0] ? gi.ImageIndex(wc->actions[i].icon) : 0;
        UI_WriteActionButtonSlot(PX(8.0f + (FLOAT)i * 42.0f), PY(728), img, wc->actions[i].count);
    }

    /* 6 inventory slots, right side (replaces empty placeholder slots) */
    FOR_LOOP(i, 6) {
        DWORD img = wc->inventory[i].icon[0] ? gi.ImageIndex(wc->inventory[i].icon) : 0;
        UI_WriteActionButtonSlot(PX(939.0f - (FLOAT)i * 42.0f), PY(728), img, wc->inventory[i].count);
    }

    /* Backpack */
    UI_WriteImage("Interface\\Buttons\\Button-Backpack-Up.blp", PX(981), PY(729), PW(37), PH(37), COLOR32_WHITE);

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
    gi.unicast(ent);
}
