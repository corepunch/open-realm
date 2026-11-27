/*
 * g_ui.c — Server-authored WoW HUD via svc_layout.
 *
 * Reproduces the classic WoW 1.12 HUD layout (action bar, targeting frame,
 * minimap, copper) using the actual WoW assets and pixel positions from the
 * virtual 1024×768 canvas, exactly matching what menu.dll rendered before
 * in-game UI was moved server-side.
 */

#include "g_wow_local.h"
#include "g_crafting.h"
#include "g_landmarks.h"
#include "common/wow_character_utils.h"
#include <ctype.h>
#include <math.h>

#define VW 1024.0f
#define VH 768.0f
#define PX(x) ((x) / VW)
#define PY(y) ((y) / VH)
#define PW(w) ((w) / VW)
#define PH(h) ((h) / VH)
#define HUD_FONT_SIZE 10
#define WOW_BUTTON_TEXT_COLOR MAKE(color32_t, 255, 209, 0, 255) // RGBA; GameFontNormal 1.0/0.82/0; quest buttons

static uint32_t ui_next_frame_number;
static uint8_t ui_window_text[MAX_MSGLEN];
static uint32_t ui_window_text_size;
static bool ui_window_writing;

/* svc_layout wire contract: every message carries exactly ONE layer —
 * `svc_layout` byte, layer byte, frames, terminator (int32_t 0 + int16_t 0).  The
 * client's CL_ParseLayout stops at the terminator and the outer parser reads the
 * next byte as a *message id*, so a frame written outside a layer (before
 * UI_WriteStart or after UI_WriteEnd) is silently discarded.  Track the open
 * layer and refuse to serialize a frame outside one instead of dropping it. */
static uint8_t ui_layout_layer = 0xFF; /* 0xFF = no layer open */

/* Open a layout layer: emit the svc_layout + layer header and reset frame numbers. */
static void UI_WriteStart(uint32_t layer) {
    ui_layout_layer = (uint8_t)layer;
    gi.Write(PF_BYTE, &(int32_t){svc_layout});
    gi.Write(PF_BYTE, &(int32_t){layer});
    ui_next_frame_number = 1;
}

/* Close the current layer with its terminator; nothing may be written until the next UI_WriteStart. */
static void UI_WriteEnd(void) {
    gi.Write(PF_LONG, &(int32_t){0});   /* bits=0 */
    gi.Write(PF_SHORT, &(int32_t){0});  /* number=0 — MSG_ReadEntityBits consumes int32_t+int16_t */
    ui_layout_layer = 0xFF;
}

/* Add one server-authored window string to the retained packet arena. */
static uint32_t UI_WindowTextOffset(cstring_t text) {
    uint32_t size, offset;
    if (!text || !*text) return 0;
    size = (uint32_t)strlen(text) + 1;
    if (size > sizeof(ui_window_text) - ui_window_text_size) {
        fprintf(stderr, "WoW UI: tutorial window text arena overflow\n");
        return 0;
    }
    offset = ui_window_text_size;
    memcpy(ui_window_text + offset, text, size);
    ui_window_text_size += size;
    return offset;
}

/* Open the generic server-authored window stream; all following proxy strings use arena offsets. */
static void UI_WriteWindowStart(uiWindowDef_t const *def) {
    ui_window_writing = true;
    ui_window_text[0] = '\0'; ui_window_text_size = 1;
    gi.Write(PF_BYTE, &(int32_t){svc_window}); gi.Write(PF_BYTE, &(int32_t){UI_WINDOW_OPEN});
    gi.Write(PF_LONG, &def->id); gi.Write(PF_LONG, &def->class_id); gi.Write(PF_LONG, &def->flags);
    ui_next_frame_number = 1;
}

/* Finish and unicast one complete server-authored window packet. */
static void UI_WriteWindowEnd(edict_t *ent) {
    pfWriteData_t text = { .data = ui_window_text, .size = ui_window_text_size };
    ui_window_writing = false;
    gi.Write(PF_LONG, &(int32_t){0}); gi.Write(PF_SHORT, &(int32_t){0});
    gi.Write(PF_LONG, &ui_window_text_size); gi.Write(PF_DATA, &text); gi.unicast(ent);
}

static void UI_WriteImage(cstring_t path, float x, float y, float w, float h, color32_t color);

static void UI_SetFramePoint(uiFramePoint_t *point, uiFramePointPos_t target, uint32_t relative, float offset, bool y_axis) {
    point->used = 1;
    point->targetPos = target;
    point->relativeTo = (uint8_t)relative;
    point->offset = (int16_t)((y_axis ? -offset : offset) * UI_FRAMEPOINT_SCALE);
}

static void UI_SetFrameRect(uiFrame_t *frame, float x, float y, float w, float h) {
    UI_SetFramePoint(&frame->points.x[FPP_MIN], FPP_MIN, 0, x, false);
    UI_SetFramePoint(&frame->points.y[FPP_MIN], FPP_MIN, 0, y, true);
    frame->size.width = w;
    frame->size.height = h;
}

static void UI_WriteProxyFrame(uiFrame_t *frame, handle_t data, uint32_t data_size) {
    if (ui_layout_layer == 0xFF && !ui_window_writing) {
        fprintf(stderr, "WoW UI: FT_%d frame written outside a svc_layout layer (missing UI_WriteStart); skipped\n",
                (int)frame->flags.type);
        return;
    }
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
    if (ui_window_writing) {
        frame->text = (cstring_t)(uintptr_t)UI_WindowTextOffset(frame->text);
        frame->tooltip = (cstring_t)(uintptr_t)UI_WindowTextOffset(frame->tooltip);
        frame->onclick = (cstring_t)(uintptr_t)UI_WindowTextOffset(frame->onclick);
    }
    gi.Write(ui_window_writing ? PF_UIWINDOWFRAME : PF_UIFRAME, frame);
}

static void UI_WriteTextFrame(float x, float y, float w, float h, cstring_t text, color32_t color, uiFontJustificationH_t align) {
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

/* Write the static world-hover widget; the client resolves its selected creature context each frame. */
void UI_WriteWowHover(edict_t *ent) {
    uiFrame_t frame = { 0 };
    uiLabel_t label = { 0 };

    if (!ent || !ent->client) return;
    UI_WriteStart(LAYER_WORLD_HOVER);
    frame.flags.type = FT_STRING; frame.stat = UI_STAT_CONTEXT_NAME; frame.color = MAKE(color32_t, 0, 255, 0, 255);
    label.font = gi.FontIndex("Fonts\\FRIZQT__.TTF", 14);
    label.textalignx = FONT_JUSTIFYCENTER; label.textaligny = FONT_JUSTIFYMIDDLE;
    UI_SetFrameRect(&frame, -PW(120), -PH(34), PW(240), PH(20));
    UI_WriteProxyFrame(&frame, &label, sizeof(label));

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_SIMPLESTATUSBAR; frame.tex.index = gi.ImageIndex("Interface\\TargetingFrame\\UI-StatusBar.blp");
    frame.stat = UI_STAT_CONTEXT_HEALTH; frame.color = MAKE(color32_t, 0, 255, 0, 255);
    UI_SetFrameRect(&frame, -PW(60), -PH(13), PW(120), PH(6));
    UI_WriteProxyFrame(&frame, NULL, 0);

    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_SIMPLESTATUSBAR; frame.tex.index = gi.ImageIndex("Interface\\TargetingFrame\\UI-StatusBar.blp");
    frame.stat = UI_STAT_CONTEXT_MANA; frame.color = MAKE(color32_t, 60, 90, 235, 255);
    UI_SetFrameRect(&frame, -PW(60), -PH(6), PW(120), PH(5));
    UI_WriteProxyFrame(&frame, NULL, 0);
    UI_WriteEnd();
    gi.unicast(ent);
}

static void UI_WriteTextArea(float x, float y, float w, float h, cstring_t text, color32_t color) {
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
static void UI_WriteQuestScrollBar(float x, float y) {
    cstring_t paths[] = {
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
    scroll.texcoord[0] = scroll.texcoord[2] = (uint8_t)(0.25f * 0xff);
    scroll.texcoord[1] = scroll.texcoord[3] = (uint8_t)(0.75f * 0xff);
    UI_SetFrameRect(&frame, x + PW(329), y + PH(81), PW(16), PH(334));
    UI_WriteProxyFrame(&frame, &scroll, sizeof(scroll));
}

static void UI_WriteClickRegion(float x, float y, float w, float h, cstring_t command) {
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

static void UI_WriteSimpleButton(float x, float y, float w, float h,
                                 cstring_t text, cstring_t command) {
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
    button.normal.texcoord[1] = (uint8_t)(0.625f * 0xff);
    button.normal.texcoord[3] = (uint8_t)(0.6875f * 0xff);
    button.normal.fontcolor = WOW_BUTTON_TEXT_COLOR;
    button.pushed = button.normal;
    button.disabled = button.normal;
    button.highlight = button.normal;
    UI_SetFrameRect(&frame, x, y, w, h);
    UI_WriteProxyFrame(&frame, &button, sizeof(button));
}

/* QuestFrame.xml uses a distinct 18px Morpheus title inside the parchment. */
static void UI_WriteQuestTitle(float x, float y, cstring_t text) {
    uiFrame_t frame = {0};
    uiLabel_t label = {0};

    frame.flags.type = FT_STRING;
    frame.text = text;
    frame.color = MAKE(color32_t, 0, 0, 0, 255);
    label.font = gi.FontIndex("Fonts\\MORPHEUS.ttf", 18);
    label.textalignx = FONT_JUSTIFYLEFT;
    label.textaligny = FONT_JUSTIFYTOP;
    UI_SetFrameRect(&frame, x + PW(28), y + PH(91), PW(285), PH(24));
    UI_WriteProxyFrame(&frame, &label, sizeof(label));
}

/* QuestNpcNameFrame centers the giver name in the metal title bar. */
static void UI_WriteQuestNpcName(float x, float y, cstring_t text) {
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
static void UI_WriteQuestPortrait(float x, float y, RESOURCE model) {
    uiFrame_t frame = {0};
    if (!model) return;
    frame.flags.type = FT_PORTRAIT;
    frame.tex.index = model;
    UI_SetFrameRect(&frame, x + PW(7), y + PH(6), PW(60), PH(60));
    UI_WriteProxyFrame(&frame, NULL, 0);
}

typedef struct { edict_t *ent; cstring_t src; string_t dst; size_t size; } wowQuestText_t;

/* Expand the player tokens retained from authoritative quest_template text. */
static void UI_FormatQuestText(wowQuestText_t const *fmt) {
    char race[64], sex[64];
    cstring_t name = fmt->ent->client->ps.name && *fmt->ent->client->ps.name ? fmt->ent->client->ps.name : "adventurer";
    cstring_t cls = Wow_ClassName(Wow_GetPlayerClass());
    Wow_GetPlayerRaceSex(race, sizeof(race), sex, sizeof(sex));
    struct { char key; cstring_t value; } repl[] = {
        { 'N', name }, { 'n', name }, { 'C', cls }, { 'c', cls },
        { 'R', race }, { 'r', race },
    };
    size_t out = 0;

    for (cstring_t src = fmt->src ? fmt->src : ""; *src && out + 1 < fmt->size; src++) {
        cstring_t value = NULL;
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
static cstring_t UI_QuestGiverName(uint32_t quest_id) {
    FOR_LOOP(i, Wow_QuestGiverCount()) {
        wowQuestGiver_t const *data = Wow_QuestGiver(i);
        wowCreature_t const *creature;
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
static void UI_WriteQuestDialog(edict_t *ent) {
    wowClient_t *wc = (wowClient_t *)ent->client;

    UI_WriteStart(LAYER_QUESTDIALOG);

    if (wc->quest_open) {
        wowQuestDetail_t const *detail = Wow_QuestDetail(wc->quest_id);
        svQuestEntry_t *state = SV_QuestFind(wc->quest_log, wc->quest_count, wc->quest_id);
        uint32_t slot = state ? (uint32_t)(state - wc->quest_log) : 0;
        edict_t *selected = wc->selected_entity && wc->selected_entity < (uint32_t)globals.num_edicts
            ? &wow_edicts[wc->selected_entity] : NULL;
        wowEntityLocal_t *giver = selected ? Wow_EntityLocal(selected) : NULL;
        cstring_t giver_name = NULL;
        char command[64], desc[2048], obj[1024], text[3072];
        float x = PX(0), y = PY(104);
        bool is_complete = state && state->status == SV_QUEST_COMPLETE;
        bool is_accepted = (state && state->status == SV_QUEST_ACTIVE) || Wow_QuestWasRewarded(wc, wc->quest_id);

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
            UI_FormatQuestText(&(wowQuestText_t){ ent, is_complete && !wc->quest_journal_detail ? detail->reward_text : detail->description, desc, sizeof(desc) });
            UI_FormatQuestText(&(wowQuestText_t){ ent, detail->objectives_text, obj, sizeof(obj) });
            if (is_complete && !wc->quest_journal_detail)
                off = snprintf(text, sizeof(text), "%s\n\nRewards:\n%d XP  |  %u copper", desc, (int)detail->reward_xp, (unsigned)detail->reward_gold);
            else {
                off = snprintf(text, sizeof(text), "%s%s%s", desc, obj[0] ? "\n\n" : "", obj);
                if (state && detail->kill_objective_count) {
                    off += snprintf(text + off, sizeof(text) - off, "\n\nProgress:");
                    FOR_LOOP(j, detail->kill_objective_count) {
                        cstring_t name = Wow_CachedCreatureName(detail->kill_objectives[j].display_id);
                        off += snprintf(text + off, sizeof(text) - off, "\n  %s: %u/%u", name ? name : "Creature", (unsigned)(state ? wc->kill_progress[slot][j] : 0), (unsigned)detail->kill_objectives[j].required_count);
                    }
                }
            }
        } else {
            snprintf(text, sizeof(text), "Quest data not available.");
        }
        UI_WriteTextArea(x + PW(28), y + PH(116), PW(270), PH(286), text, MAKE(color32_t, 0, 0, 0, 255));
        UI_WriteQuestScrollBar(x, y);

        if (wc->quest_journal_detail && state) {
            snprintf(command, sizeof(command), "quest_abandon %u", (unsigned)wc->quest_id);
            UI_WriteSimpleButton(x + PW(23), y + PH(418), PW(90), PH(22), "Abandon", command);
        } else if (is_complete) {
            snprintf(command, sizeof(command), "quest_complete %u", (unsigned)wc->quest_id);
            UI_WriteSimpleButton(x + PW(23), y + PH(418), PW(120), PH(22), "Complete Quest", command);
        } else if (!is_accepted) {
            snprintf(command, sizeof(command), "quest_accept %u", (unsigned)wc->quest_id);
            UI_WriteSimpleButton(x + PW(23), y + PH(418), PW(77), PH(22), "Accept", command);
        } else if (state && state->status == SV_QUEST_ACTIVE) {
            snprintf(command, sizeof(command), "quest_abandon %u", (unsigned)wc->quest_id);
            UI_WriteSimpleButton(x + PW(23), y + PH(418), PW(90), PH(22), "Abandon", command);
        }
        UI_WriteSimpleButton(x + PW(267), y + PH(418), PW(78), PH(22), wc->quest_journal_detail ? "Back" : "Decline", wc->quest_journal_detail ? "quest_back" : "quest_close");
    } else if (wc->questlog_open) {
        float x = PX(24), y = PY(68);
        float line_y = y + PH(48);
        uint32_t line_count = 0;
        char buf[128], cmd[64];

        UI_WriteImage("Interface\\QuestFrame\\UI-QuestGreeting-TopLeft.blp", x, y, PW(256), PH(128), COLOR32_WHITE);
        UI_WriteImage("Interface\\QuestFrame\\UI-QuestGreeting-TopRight.blp", x + PW(256), y, PW(128), PH(128), COLOR32_WHITE);
        UI_WriteImage("Interface\\QuestFrame\\UI-QuestGreeting-BotLeft.blp", x, y + PH(256), PW(256), PH(256), COLOR32_WHITE);
        UI_WriteImage("Interface\\QuestFrame\\UI-QuestGreeting-BotRight.blp", x + PW(256), y + PH(256), PW(128), PH(256), COLOR32_WHITE);

        UI_WriteTextFrame(x + PW(42), y + PH(12), PW(280), PH(22), "Quest Log", MAKE(color32_t, 255, 215, 120, 255), FONT_JUSTIFYCENTER);

        if (wc->questlog_offset >= wc->quest_count)
            wc->questlog_offset = wc->quest_count
                ? (wc->quest_count - 1) / WOW_QUEST_PAGE_SIZE * WOW_QUEST_PAGE_SIZE : 0;
        if (!wc->quest_count) {
            UI_WriteTextFrame(x + PW(42), line_y, PW(280), PH(22), "No active quests.", MAKE(color32_t, 160, 150, 140, 255), FONT_JUSTIFYCENTER);
        }
        for (uint32_t i = wc->questlog_offset; i < wc->quest_count && line_count < WOW_QUEST_PAGE_SIZE; i++) {
            svQuestEntry_t *qs = &wc->quest_log[i];
            wowQuestDetail_t const *detail = Wow_QuestDetail(qs->quest_id);
            cstring_t status = qs->status == SV_QUEST_COMPLETE ? " (Complete)" : "";

            snprintf(buf, sizeof(buf), "%s%s", detail ? detail->title : "Unknown Quest", status);
            snprintf(cmd, sizeof(cmd), "quest_inspect %u", (unsigned)qs->quest_id);
            UI_WriteSimpleButton(x + PW(28), line_y, PW(178), PH(22), buf, cmd);
            snprintf(cmd, sizeof(cmd), "quest_track %u", (unsigned)qs->quest_id);
            UI_WriteSimpleButton(x + PW(210), line_y, PW(60), PH(22), wc->quest_tracked[i] ? "Untrack" : "Track", cmd);
            snprintf(cmd, sizeof(cmd), "quest_abandon %u", (unsigned)qs->quest_id);
            UI_WriteSimpleButton(x + PW(274), line_y, PW(62), PH(22), "Abandon", cmd);
            line_y += PH(28);
            line_count++;
        }
        if (wc->questlog_offset)
            UI_WriteSimpleButton(x + PW(28), y + PH(410), PW(82), PH(22), "Previous", "questlog_page prev");
        if (wc->questlog_offset + line_count < wc->quest_count)
            UI_WriteSimpleButton(x + PW(120), y + PH(410), PW(82), PH(22), "Next", "questlog_page next");
        UI_WriteSimpleButton(x + PW(250), y + PH(450), PW(90), PH(28), "Close", "quest_close");
    }

    UI_WriteEnd();
}

static void UI_WriteQuestLog(edict_t *ent) {
    (void)ent; /* merged into UI_WriteQuestDialog */
}

/* Write an FT_TEXTURE frame with float-precision UV (supports l>r or t>b for flips). */
static void UI_WriteImageUV(cstring_t path, float x, float y, float w, float h,
                            float l, float r, float t, float b, color32_t color) {
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

static void UI_WriteImage(cstring_t path, float x, float y, float w, float h, color32_t color) {
    UI_WriteImageUV(path, x, y, w, h, 0.0f, 1.0f, 0.0f, 1.0f, color);
}

/* Draw the server-owned unread message pool and its selected message panel. */
void UI_WriteWowMessageQueue(edict_t *ent) {
    wowClient_t *wc = ent ? (wowClient_t *)ent->client : NULL;
    uiMessageQueue_t queue = {0};
    char command[64];
    uint32_t unread = 0;

    if (!wc) return;
    UI_WriteStart(LAYER_MESSAGE);
    FOR_LOOP(i, wc->message_count) {
        wowUiMessage_t const *message = &wc->messages[i];
        if (!(message->flags & WOW_UI_MESSAGE_UNREAD) && message->message_id != wc->message_open_id) continue;
        queue = (uiMessageQueue_t){
            .message_id = message->message_id,
            .image = gi.ImageIndex("Interface\\TutorialFrame\\TutorialFrameAlert"),
            .title_font = gi.FontIndex("Fonts\\FRIZQT__.TTF", 16),
            .body_font = gi.FontIndex("Fonts\\FRIZQT__.TTF", 12),
            .flags = (message->flags & WOW_UI_MESSAGE_UNREAD ? UI_MESSAGE_UNREAD : 0) |
                     (message->message_id == wc->message_open_id ? UI_MESSAGE_OPEN : 0),
        };
        uiFrame_t frame = {0};
        frame.flags.type = FT_MESSAGE_QUEUE;
        frame.text = message->title;
        frame.tooltip = message->body;
        if (message->message_id == wc->message_open_id)
            snprintf(command, sizeof(command), "message_close");
        else
            snprintf(command, sizeof(command), "message_open %u", (unsigned)message->message_id);
        frame.onclick = command;
        UI_SetFrameRect(&frame, message->message_id == wc->message_open_id ? PX(256) : PX(360) + PW(unread * 42),
                        message->message_id == wc->message_open_id ? PY(192) : PY(680),
                        message->message_id == wc->message_open_id ? PW(512) : PW(32),
                        message->message_id == wc->message_open_id ? PH(169) : PH(32));
        frame.buffer.data = &queue;
        frame.buffer.size = sizeof(queue);
        frame.color = COLOR32_WHITE;
        frame.number = ui_next_frame_number++;
        gi.Write(PF_UIFRAME, &frame);
        if (message->flags & WOW_UI_MESSAGE_UNREAD) unread++;
    }
    UI_WriteEnd();
    gi.unicast(ent);
}

/* Solid-color quad via a null texture slot */
static void UI_WriteColorRect(float x, float y, float w, float h, color32_t color) {
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

/* Author the initial loading screen; only the progress value remains client-owned. */
void UI_WriteLoadingLayout(edict_t *ent) {
    uiFrame_t frame = { 0 };
    uiTextureUV_t uv = { .l = 0, .r = 1, .t = 0, .b = 1, .color = COLOR32_WHITE, .alphamode = BLEND_MODE_ALPHAKEY };

    UI_WriteStart(LAYER_LOADING);
    frame.flags.type = FT_TEXTURE; frame.color = COLOR32_WHITE; frame.tex.index = gi.ImageIndex(wow_loading_texture);
    /* Layout sizes are normalized; pixel dimensions magnified the background into a solid corner. */
    UI_SetFrameRect(&frame, 0, 0, 1, 1); UI_WriteProxyFrame(&frame, &uv, sizeof(uv));
    UI_WriteTextFrame(PX(164), PY(590), PW(696), PH(42), wow_loading_title,
                      MAKE(color32_t, 255, 215, 120, 255), FONT_JUSTIFYCENTER);
    UI_WriteColorRect(PX(164), PY(650), PW(696), PH(12), MAKE(color32_t, 12, 10, 8, 220));
    memset(&frame, 0, sizeof(frame));
    frame.flags.type = FT_LOADING_BAR;
    /* Classic archives provide Loading-BarFill; Loading-Bar.blp never resolved. */
    frame.tex.index = gi.ImageIndex("Interface\\Glues\\LoadingBar\\Loading-BarFill.blp");
    frame.color = MAKE(color32_t, 220, 180, 60, 255);
    UI_SetFrameRect(&frame, PX(166), PY(652), PW(692), PH(8)); UI_WriteProxyFrame(&frame, NULL, 0);
    UI_WriteEnd();
    if (ent) gi.unicast(ent);
}

/* Solid health/mana bar drawn as two color rects (dark background + colored fill) */
static void UI_WriteColorBar(float x, float y, float w, float h,
                             float value, float maxvalue,
                             color32_t fill_color) {
    float p = maxvalue > 0.0f ? value / maxvalue : 0.0f;
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    UI_WriteColorRect(x, y, w, h, MAKE(color32_t, 12, 10, 8, 220));
    if (p > 0.0f)
        UI_WriteColorRect(x + PW(2), y + PH(2), (w - PW(4)) * p, h - PH(4), fill_color);
}

/* Minimap.xml authors a 140px circle centered inside the cropped 192px border atlas. */
static void UI_WriteMinimapFrames(void) {
    uiFrame_t minimap;

    memset(&minimap, 0, sizeof(minimap));
    minimap.flags.type = FT_MINIMAP;
    minimap.color = COLOR32_WHITE;
    UI_SetFrameRect(&minimap, PX(867), PY(22), PW(140), PH(140));
    UI_WriteProxyFrame(&minimap, NULL, 0);
    /* MinimapBackdrop and the cluster's top strip use the exact Minimap.xml atlas crops. */
    UI_WriteImageUV("Interface\\Minimap\\UI-Minimap-Border.blp", PX(832), PY(20), PW(192), PH(192),
                    0.25f, 1.0f, 0.125f, 0.875f, COLOR32_WHITE);
    UI_WriteImageUV("Interface\\Minimap\\UI-Minimap-Border.blp", PX(832), PY(0), PW(192), PH(32),
                    0.25f, 1.0f, 0.0f, 0.125f, COLOR32_WHITE);
}

/* Main action bar: four 256×53 strips + two end-caps from UI-MainMenuBar-Dwarf.blp */
static void UI_WriteActionBar(void) {
    static cstring_t const bar = "Interface\\MainMenuBar\\UI-MainMenuBar-Dwarf.blp";
    static cstring_t const cap = "Interface\\MainMenuBar\\UI-MainMenuBar-EndCap-Dwarf.blp";
    /* Each strip covers a different vertical slice of the texture (v slices at 53/256 intervals) */
    static float const strips[4][4] = {
        /* {l, r, t, b}, screen x starts at 0 */
        { 0.0f, 1.0f, 0.79296875f, 1.0f },
        { 0.0f, 1.0f, 0.54296875f, 0.75f },
        { 0.0f, 1.0f, 0.29296875f, 0.5f },
        { 0.0f, 1.0f, 0.04296875f, 0.25f },
    };

    FOR_LOOP(i, 4)
        UI_WriteImageUV(bar, PX((float)(i * 256)), PY(715), PW(256), PH(53), strips[i][0], strips[i][1], strips[i][2], strips[i][3], COLOR32_WHITE);

    /* Left end-cap (normal orientation) */
    UI_WriteImage(cap, PX(-96), PY(640), PW(128), PH(128), COLOR32_WHITE);
    /* Right end-cap (horizontally flipped: l=1, r=0) */
    UI_WriteImageUV(cap, PX(992), PY(640), PW(128), PH(128), 1.0f, 0.0f, 0.0f, 1.0f, COLOR32_WHITE);
}

/* Action button slot at grid position i (0..11 = left row, 12..15 = right empty slots) */
static void UI_WriteActionButtonSlot(float x, float y, uint32_t image_index, uint32_t count) {
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

void UI_WriteWowActionBar(edict_t *ent) {
    wowClient_t const *client = (wowClient_t *)ent->client;
    wowEntityLocal_t const *local = Wow_EntityLocal(ent);

    UI_WriteStart(LAYER_COMMANDBAR);
    FOR_LOOP(i, WOW_UI_ACTION_SLOTS) {
        float x = PX(8.0f + (float)i * 42.0f);
        float y = PY(728);
        UI_WriteActionButtonSlot(x, y, 0, 0);
        if (client->action_spells[i] == SPELL_NONE || !client->actions[i].icon[0]) continue;

        uiCommandButton_t timer = {0};
        uint32_t spell = client->action_spells[i];
        uint32_t remaining = local->gcd_time, duration = WOW_GCD_MS;
        if (spell < wow_spell_count && local->spell_cooldown[spell] > remaining) {
            remaining = local->spell_cooldown[spell];
            duration = wow_spells[spell].cooldown_ms;
        }
        if (remaining && gi.GetTime) {
            timer.radialEndTime = gi.GetTime() + remaining;
            timer.radialStartTime = timer.radialEndTime - duration;
        }
        char command[32];
        snprintf(command, sizeof(command), "wow_action %u", (unsigned)i);
        uiFrame_t button = {0};
        button.flags.type = FT_COMMANDBUTTON;
        button.stat = UINT8_MAX;
        button.tex.index = gi.ImageIndex(client->actions[i].icon);
        button.onclick = command;
        if (timer.radialEndTime != timer.radialStartTime) button.flagsvalue |= UIFLAG_RADIAL_SHADE;
        UI_SetFrameRect(&button, x, y, PW(36), PH(36));
        UI_WriteProxyFrame(&button, &timer, sizeof(timer));
    }
    UI_WriteEnd();
    gi.unicast(ent);
}

/* Targeting frame: the WoW character frame backdrop + health/mana bars + name/level text */
static void UI_WriteTargetingFrame(edict_t *ent) {
    player_t *ps = &ent->client->ps;
    char name_buf[64], level_buf[32], health_buf[32], power_buf[32];
    float health = ps->stats[WOW_STAT_HEALTH_MAX] ? (float)ps->stats[WOW_STAT_HEALTH] / ps->stats[WOW_STAT_HEALTH_MAX] : 0;
    float power = ps->stats[WOW_STAT_POWER_MAX] ? (float)ps->stats[WOW_STAT_POWER] / ps->stats[WOW_STAT_POWER_MAX] : 0;

    /* PlayerFrameTexture inherits the unmodified vertex color from PlayerFrame.xml. */
    UI_WriteImageUV("Interface\\TargetingFrame\\UI-TargetingFrame.blp", PX(-19), PY(4), PW(232), PH(100),
                    1.0f, 0.09375f, 0.0f, 0.78125f, COLOR32_WHITE);

    /* Classic 1.12 unit-frame portraits are 2D per-race/sex textures, already oval-masked. */
    {
        char race[64], sex[64], path[256];
        Wow_GetPlayerRaceSex(race, sizeof(race), sex, sizeof(sex));
        snprintf(path, sizeof(path), "Interface\\CharacterFrame\\TemporaryPortrait-%s-%s.blp", sex, race);
        UI_WriteImage(path, PX(23), PY(16), PW(64), PH(64), COLOR32_WHITE);
    }

    /* Dark name area */
    UI_WriteColorRect(PX(87), PY(22), PW(119), PH(41), MAKE(color32_t, 0, 0, 0, 128));

    /* Name */
    snprintf(name_buf, sizeof(name_buf), "%s", ps->name && *ps->name ? ps->name : "Player");
    UI_WriteTextFrame(PX(72), PY(18), PW(100), PH(12), name_buf, MAKE(color32_t, 255, 215, 120, 255), FONT_JUSTIFYCENTER);

    /* Level */
    snprintf(level_buf, sizeof(level_buf), "Lvl %d", (int)ps->stats[WOW_STAT_LEVEL]);
    UI_WriteTextFrame(PX(24), PY(58), PW(42), PH(12), level_buf, MAKE(color32_t, 235, 225, 190, 255), FONT_JUSTIFYCENTER);

    /* Health bar */
    UI_WriteColorRect(PX(87), PY(41), PW(119), PH(12), MAKE(color32_t, 12, 10, 8, 220));
    if (health > 0) UI_WriteImageUV("Interface\\TargetingFrame\\UI-StatusBar.blp", PX(87), PY(41), PW(119)*health,
                                   PH(12), 0, health, 0, 1, MAKE(color32_t, 20, 178, 48, 255));

    /* Mana/power bar */
    UI_WriteColorRect(PX(87), PY(52), PW(119), PH(12), MAKE(color32_t, 12, 10, 8, 220));
    if (power > 0) UI_WriteImageUV("Interface\\TargetingFrame\\UI-StatusBar.blp", PX(87), PY(52), PW(119)*power,
                                  PH(12), 0, power, 0, 1, MAKE(color32_t, 26, 82, 210, 255));

    /* TextStatusBarText is centered over each authored bar. */
    snprintf(health_buf, sizeof(health_buf), "%u / %u", (unsigned)ps->stats[WOW_STAT_HEALTH],
             (unsigned)ps->stats[WOW_STAT_HEALTH_MAX]);
    snprintf(power_buf, sizeof(power_buf), "%u / %u", (unsigned)ps->stats[WOW_STAT_POWER],
             (unsigned)ps->stats[WOW_STAT_POWER_MAX]);
    UI_WriteTextFrame(PX(87), PY(41), PW(119), PH(12), health_buf, COLOR32_WHITE, FONT_JUSTIFYCENTER);
    UI_WriteTextFrame(PX(87), PY(52), PW(119), PH(12), power_buf, COLOR32_WHITE, FONT_JUSTIFYCENTER);

    /* PlayerFrame.lua hides PlayerRestIcon outside resting state; the server does not author that state yet. */
}

/* -------------------------------------------------------------------------
 * Loot window — shows corpse loot snapshot, one row per item with click-to-take.
 * Items are stored in client->loot_snap[] so the window is fully server-authored
 * and requires no direct entity access in the layout pass.
 * -------------------------------------------------------------------------*/
static void UI_WriteLootWindow(edict_t *ent) {
    wowClient_t *wc = (wowClient_t *)ent->client;
    char buf[96], loot_all_command[64];
    uint32_t visible = 0;
    float x, y, h;

    if (!wc->loot_target) return;

    /* Count visible slots to size the panel dynamically. */
    FOR_LOOP(i, WOW_MAX_LOOT_ITEMS) if (wc->loot_snap[i].icon[0]) visible++;
    if (!visible) { /* nothing left; auto-close */
        wc->loot_target = 0;
        return;
    }

    x = PX(300.0f); y = PY(160.0f);
    h = PH(52.0f + (float)visible * 36.0f);

    /* Dark parchment background + gold border */
    UI_WriteColorRect(x, y, PW(380.0f), h, MAKE(color32_t, 20, 16, 10, 230));
    UI_WriteColorRect(x, y, PW(380.0f), PH(1.0f), MAKE(color32_t, 170, 140, 60, 255));
    UI_WriteColorRect(x, y + h, PW(380.0f), PH(1.0f), MAKE(color32_t, 170, 140, 60, 255));
    UI_WriteColorRect(x, y, PW(1.0f), h, MAKE(color32_t, 170, 140, 60, 255));
    UI_WriteColorRect(x + PW(379.0f), y, PW(1.0f), h, MAKE(color32_t, 170, 140, 60, 255));

    /* Title bar */
    UI_WriteColorRect(x, y, PW(380.0f), PH(22.0f), MAKE(color32_t, 50, 40, 20, 240));
    UI_WriteTextFrame(x + PW(8), y + PH(5), PW(200), PH(14), "Loot",
                      MAKE(color32_t, 255, 215, 120, 255), FONT_JUSTIFYLEFT);

    snprintf(loot_all_command, sizeof(loot_all_command), "loot_all %llu",
             (unsigned long long)wc->loot_generation);
    UI_WriteSimpleButton(x + PW(240), y + PH(3), PW(74), PH(18), "Take all", loot_all_command);

    /* Close button — top right corner */
    UI_WriteSimpleButton(x + PW(320), y + PH(3), PW(56), PH(18), "Close", "loot_close");

    /* Item rows */
    uint32_t row = 0;
    FOR_LOOP(i, WOW_MAX_LOOT_ITEMS) {
        float row_y;
        uint32_t icon_img;
        char cmd[64];

        if (!wc->loot_snap[i].icon[0]) continue;
        row_y = y + PH(26.0f + (float)row * 36.0f);

        /* Slot background */
        UI_WriteColorRect(x + PW(8), row_y, PW(362.0f), PH(32.0f), MAKE(color32_t, 35, 28, 16, 200));

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
        snprintf(cmd, sizeof(cmd), "loot_take %u %llu", (unsigned)i,
                 (unsigned long long)wc->loot_generation);
        UI_WriteClickRegion(x + PW(8), row_y, PW(360.0f), PH(32.0f), cmd);
        row++;
    }
}

/* -------------------------------------------------------------------------
 * Backpack window — 4×4 grid showing all WOW_UI_INVENTORY_SLOTS item slots.
 * Positioned in the upper-right, above the bag slot row.
 * -------------------------------------------------------------------------*/
static void UI_WriteMerchantWindow(edict_t *ent) {
    wowClient_t *wc = (wowClient_t *)ent->client;
    float x = PX(300), y = PY(160);
    char text[128], command[48];
    uint32_t row = 0;
    if (!wc->merchant_target) return;
    if (!Wow_MerchantValid(ent)) { wc->merchant_target = wc->merchant_entry = 0; return; }
    UI_WriteColorRect(x, y, PW(400), PH(420), MAKE(color32_t, 20, 16, 10, 240));
    UI_WriteTextFrame(x + PW(8), y + PH(6), PW(180), PH(16), "Merchant", COLOR32_WHITE, FONT_JUSTIFYLEFT);
    UI_WriteSimpleButton(x + PW(210), y + PH(3), PW(76), PH(18), "Quest", "merchant_quest");
    UI_WriteSimpleButton(x + PW(330), y + PH(3), PW(56), PH(18), "Close", "merchant_close");
    UI_WriteSimpleButton(x + PW(290), y + PH(3), PW(36), PH(18), wc->merchant_buyback_view ? "Sell" : "Back", "merchant_view");
    if (!wc->merchant_buyback_view) {
    UI_WriteTextFrame(x + PW(8), y + PH(30), PW(380), PH(16), "Buy (copper)", COLOR32_WHITE, FONT_JUSTIFYLEFT);
    for (uint32_t i = 0; i < Wow_MerchantItemCount(); i++) {
        wowMerchantItem_t const *item = Wow_MerchantItem(i);
        snprintf(text, sizeof(text), "%s - %u copper", item->item.name, (unsigned)item->buy_price);
        snprintf(command, sizeof(command), "merchant_buy %u %u", (unsigned)i, (unsigned)wc->merchant_revision);
        UI_WriteSimpleButton(x + PW(8), y + PH(50 + row++ * 22), PW(380), PH(20), text, command);
    }
    UI_WriteTextFrame(x + PW(8), y + PH(106), PW(380), PH(16), "Sell stack (eligible items)", COLOR32_WHITE, FONT_JUSTIFYLEFT);
    row = 0;
    for (uint32_t i = 0; i < WOW_UI_INVENTORY_SLOTS; i++) {
        wowHudIcon_t const *v = &wc->inventory[i];
        if (!v->icon[0] || !v->count) continue;
        for (uint32_t j = 0; j < Wow_MerchantItemCount(); j++) {
            wowMerchantItem_t const *item = Wow_MerchantItem(j);
            if (v->item_id != item->item.item_id) continue;
            snprintf(text, sizeof(text), "%s x%u - %llu copper", v->name, (unsigned)v->count, (unsigned long long)item->sell_price * v->count);
            snprintf(command, sizeof(command), "merchant_sell %u %u %u %u", (unsigned)i, (unsigned)wc->merchant_revision, (unsigned)v->item_id, (unsigned)v->count);
            UI_WriteSimpleButton(x + PW(8), y + PH(126 + row++ * 17), PW(380), PH(16), text, command);
            break;
        }
    }
    } else {
    UI_WriteTextFrame(x + PW(8), y + PH(30), PW(380), PH(16), "Buy back", COLOR32_WHITE, FONT_JUSTIFYLEFT);
    for (uint32_t i = 0; i < 12; i++) {
        wowHudIcon_t const *v = &wc->merchant_buyback[i];
        if (!v->count) continue;
        snprintf(text, sizeof(text), "%s x%u - %u copper", v->name, (unsigned)v->count, (unsigned)wc->merchant_buyback_price[i]);
        snprintf(command, sizeof(command), "merchant_buyback %u %u", (unsigned)i, (unsigned)wc->merchant_revision);
        UI_WriteSimpleButton(x + PW(8), y + PH(50 + i * 24), PW(380), PH(18), text, command);
    }
    }
}

/* Bank transfers are stamped with a UI revision and source snapshot. */
static void UI_WriteBankWindow(edict_t *ent) {
    wowClient_t *wc = (wowClient_t *)ent->client;
    if (!wc->bank_target) return;
    if (!Wow_BankValid(ent)) { wc->bank_target = wc->bank_entry = 0; return; }
    if (!++wc->bank_revision) ++wc->bank_revision;
    float x = PX(170), y = PY(165);
    UI_WriteColorRect(x, y, PW(610), PH(370), MAKE(color32_t, 20, 16, 10, 240));
    UI_WriteTextFrame(x + PW(8), y + PH(5), PW(250), PH(18), "Backpack: deposit", COLOR32_WHITE, FONT_JUSTIFYLEFT);
    UI_WriteTextFrame(x + PW(310), y + PH(5), PW(200), PH(18), "Bank: withdraw", COLOR32_WHITE, FONT_JUSTIFYLEFT);
    UI_WriteSimpleButton(x + PW(548), y + PH(3), PW(56), PH(18), "Close", "bank_close");
    for (uint32_t column = 0; column < 2; column++) {
        wowHudIcon_t const *slots = column ? wc->bank : wc->inventory;
        for (uint32_t i = 0; i < 16; i++) {
            wowHudIcon_t const *item = &slots[i];
            if (!item->icon[0] || !item->count) continue;
            float sx = x + PW(8 + column * 302), sy = y + PH(28 + i * 21);
            char label[96], command[96];
            snprintf(label, sizeof(label), "%.48s x%u", item->name[0] ? item->name : "Unknown item", (unsigned)item->count);
            snprintf(command, sizeof(command), "bank_%s %u %u %u %u 0", column ? "withdraw" : "deposit", (unsigned)i, (unsigned)wc->bank_revision, (unsigned)item->item_id, (unsigned)item->count);
            UI_WriteSimpleButton(sx, sy, PW(235), PH(19), label, command);
            if (item->count > 1) {
                snprintf(command, sizeof(command), "bank_%s %u %u %u %u 1", column ? "withdraw" : "deposit", (unsigned)i, (unsigned)wc->bank_revision, (unsigned)item->item_id, (unsigned)item->count);
                UI_WriteSimpleButton(sx + PW(238), sy, PW(50), PH(19), "Half", command);
            }
        }
    }
}

static void UI_WriteBackpackWindow(edict_t *ent) {
    wowClient_t *wc = (wowClient_t *)ent->client;
    float x, y, w, h;

    if (!wc->backpack_open) return;

    x = PX(818.0f); y = PY(478.0f);
    w = PW(190.0f); h = PH(258.0f);

    /* Background + border */
    UI_WriteColorRect(x, y, w, h, MAKE(color32_t, 20, 16, 10, 230));
    UI_WriteColorRect(x, y, w, PH(1.0f), MAKE(color32_t, 170, 140, 60, 255));
    UI_WriteColorRect(x, y + h, w, PH(1.0f), MAKE(color32_t, 170, 140, 60, 255));
    UI_WriteColorRect(x, y, PW(1.0f), h, MAKE(color32_t, 170, 140, 60, 255));
    UI_WriteColorRect(x + w, y, PW(1.0f), h, MAKE(color32_t, 170, 140, 60, 255));

    /* Title bar */
    UI_WriteColorRect(x, y, w, PH(22.0f), MAKE(color32_t, 50, 40, 20, 240));
    UI_WriteTextFrame(x + PW(6), y + PH(5), PW(100), PH(14), "Backpack",
                      MAKE(color32_t, 255, 215, 120, 255), FONT_JUSTIFYLEFT);
    UI_WriteSimpleButton(x + w - PW(54), y + PH(3), PW(50), PH(18), "Close", "backpack");

    /* 4×4 grid of item slots */
    FOR_LOOP(i, WOW_UI_INVENTORY_SLOTS) {
        float sx = x + PW(8.0f + (float)(i % 4) * 44.0f);
        float sy = y + PH(26.0f + (float)(i / 4) * 44.0f);
        uint32_t img = wc->inventory[i].icon[0] ? gi.ImageIndex(wc->inventory[i].icon) : 0;
        UI_WriteActionButtonSlot(sx, sy, img, wc->inventory[i].count);
        char command[32];
        snprintf(command, sizeof(command), "bag_select %u", (unsigned)i);
        UI_WriteClickRegion(sx, sy, PW(40), PH(40), command);
        if (wc->backpack_selected == i + 1)
            UI_WriteColorRect(sx, sy, PW(40), PH(2), MAKE(color32_t, 255, 215, 100, 255));
    }
    if (wc->backpack_selected) {
        char command[32];
        snprintf(command, sizeof(command), "bag_use %u", (unsigned)(wc->backpack_selected - 1));
        UI_WriteSimpleButton(x + PW(8), y + PH(207), PW(60), PH(20), "Use", command);
        UI_WriteSimpleButton(x + PW(72), y + PH(207), PW(60), PH(20), "Split", "bag_split_select");
        UI_WriteTextFrame(x + PW(8), y + PH(232), PW(174), PH(16),
                          wc->backpack_splitting ? "Choose empty slot" : wc->inventory[wc->backpack_selected - 1].name, COLOR32_WHITE, FONT_JUSTIFYLEFT);
    }
}

static void UI_WriteCrafting(edict_t *ent) {
    wowClient_t *wc = (wowClient_t *)ent->client;
    float x = PX(210), y = PY(140);
    uint32_t linen = 0;
    char text[128], command[32];
    if (!wc->crafting_open) return;
    for (uint32_t i = 0; i < WOW_UI_INVENTORY_SLOTS; ++i)
        if (wc->inventory[i].item_id == 2589) linen += wc->inventory[i].count;
    UI_WriteColorRect(x, y, PW(500), PH(190), MAKE(color32_t, 20, 16, 10, 245));
    UI_WriteTextFrame(x + PW(12), y + PH(8), PW(300), PH(20), "First Aid", MAKE(color32_t, 255, 215, 120, 255), FONT_JUSTIFYLEFT);
    UI_WriteSimpleButton(x + PW(430), y + PH(5), PW(60), PH(22), "Close", "crafting");
    snprintf(text, sizeof(text), "Linen Cloth: %u  (loot from kobolds)", (unsigned)linen);
    UI_WriteTextFrame(x + PW(12), y + PH(37), PW(400), PH(20), text, COLOR32_WHITE, FONT_JUSTIFYLEFT);
    for (uint32_t i = 0; i < Wow_RecipeCount(); ++i) {
        wowRecipe_t const *recipe = Wow_Recipe(i);
        snprintf(text, sizeof(text), "%s - %u Linen Cloth", recipe->name, (unsigned)recipe->reagent_count);
        UI_WriteTextFrame(x + PW(12), y + PH(68 + i * 42), PW(350), PH(20), text, COLOR32_WHITE, FONT_JUSTIFYLEFT);
        snprintf(command, sizeof(command), "craft %u", (unsigned)recipe->spell);
        UI_WriteSimpleButton(x + PW(400), y + PH(65 + i * 42), PW(80), PH(24), "Create", command);
    }
}

static void UI_WriteSpellbook(edict_t *ent) {
    wowClient_t *wc = (wowClient_t *)ent->client;
    float x = PX(200), y = PY(100);
    uint32_t selected = wc->spellbook_selected;
    char text[160], command[64];
    unsigned row = 0;
    if (!wc->spellbook_open) return;
    if (!Wow_PlayerKnowsSpell(ent, selected)) selected = WOW_SPELL_ATTACK;
    UI_WriteColorRect(x, y, PW(620), PH(480), MAKE(color32_t, 20, 16, 10, 240));
    UI_WriteTextFrame(x + PW(12), y + PH(10), PW(300), PH(20), "Spellbook", MAKE(color32_t, 255, 215, 120, 255), FONT_JUSTIFYLEFT);
    UI_WriteSimpleButton(x + PW(550), y + PH(6), PW(60), PH(22), "Close", "spellbook");
    for (uint32_t i = 0; i < wow_spell_count; ++i) {
        if (!Wow_PlayerKnowsSpell(ent, i)) continue;
        snprintf(command, sizeof(command), "spellbook_select %u", (unsigned)i);
        UI_WriteSimpleButton(x + PW(12), y + PH(42 + row * 30), PW(175), PH(24), wow_spells[i].name, command);
        ++row;
    }
    UI_WriteTextFrame(x + PW(200), y + PH(42), PW(390), PH(20), wow_spells[selected].name, MAKE(color32_t, 255, 215, 120, 255), FONT_JUSTIFYLEFT);
    snprintf(text, sizeof(text), "Mana: %u   Cast: %.1f s   Range: %.0f   Cooldown: %.1f s", (unsigned)wow_spells[selected].mana_cost, wow_spells[selected].cast_time / 1000.0f, wow_spells[selected].range, wow_spells[selected].cooldown_ms / 1000.0f);
    UI_WriteTextFrame(x + PW(200), y + PH(70), PW(405), PH(20), text, COLOR32_WHITE, FONT_JUSTIFYLEFT);
    {
        static cstring_t const descriptions[WOW_SPELL_COUNT] = {
            "Attack the selected enemy with your weapon.",
            "Launch a fire projectile at an enemy for 2 damage.",
            "Launch a frost projectile for 3 damage and slow the enemy for 2 seconds.",
            "Restore 2 health to yourself.",
            "Blast an enemy instantly for 24 to 32 damage.",
            "Launch a fire projectile for 31 to 45 damage."
        };
        UI_WriteTextFrame(x + PW(200), y + PH(135), PW(400), PH(38), descriptions[selected], COLOR32_WHITE, FONT_JUSTIFYLEFT);
    }

    snprintf(command, sizeof(command), "spellbook_cast %u", (unsigned)selected);
    UI_WriteSimpleButton(x + PW(200), y + PH(100), PW(80), PH(24), "Cast", command);
    if (Wow_GetPlayerClass() == WOW_CLASS_MAGE && !wc->fireball_rank2_learned) {
        UI_WriteTextFrame(x + PW(200), y + PH(180), PW(340), PH(20), "Rank 2 training: 400 earned XP (prototype)", COLOR32_WHITE, FONT_JUSTIFYLEFT);
        UI_WriteSimpleButton(x + PW(530), y + PH(178), PW(75), PH(22), "Learn", "spellbook_train_fireball");
    }
    UI_WriteTextFrame(x + PW(12), y + PH(225), PW(500), PH(20), "Assign selected spell to an action slot:", COLOR32_WHITE, FONT_JUSTIFYLEFT);
    for (uint32_t i = 0; i < WOW_UI_ACTION_SLOTS; ++i) {
        float sx = x + PW(12 + (i % 6) * 98), sy = y + PH(252 + (i / 6) * 72);
        snprintf(text, sizeof(text), "%u: %.12s", (unsigned)i + 1, wc->action_spells[i] < wow_spell_count ? wow_spells[wc->action_spells[i]].name : "Empty");
        snprintf(command, sizeof(command), "spellbook_bind %u %u", (unsigned)i, (unsigned)selected);
        UI_WriteSimpleButton(sx, sy, PW(94), PH(24), text, command);
        snprintf(command, sizeof(command), "spellbook_bind %u clear", (unsigned)i);
        UI_WriteSimpleButton(sx, sy + PH(26), PW(94), PH(22), "Clear", command);
    }
}

/* Show the welcome tutorial without taking control away from the player. */
void UI_WriteWelcomeWindow(edict_t *ent) {
    uiFrame_t frame = {0};
    uiBackdrop_t backdrop = {0};

    UI_WriteWindowStart(&(uiWindowDef_t){ .id = 1, .class_id = 1,
        .flags = UI_WINDOW_UNIQUE });
    frame.flags.type = FT_BACKDROP; frame.color = COLOR32_WHITE;
    backdrop.Background = gi.ImageIndex("Interface\\DialogFrame\\UI-DialogBox-Background.blp");
    backdrop.EdgeFile = gi.ImageIndex("Interface\\DialogFrame\\UI-DialogBox-Border.blp");
    UI_SetFrameRect(&frame, PX(256), PY(192), PW(512), PH(384)); UI_WriteProxyFrame(&frame, &backdrop, sizeof(backdrop));
    UI_WriteTextFrame(PX(288), PY(224), PW(448), PH(32), "Welcome to World of Warcraft!", COLOR32_WHITE, FONT_JUSTIFYCENTER);
    UI_WriteTextArea(PX(288), PY(272), PW(448), PH(210),
                     "Welcome, adventurer. Explore Azeroth, complete quests, and discover your destiny.",
                     COLOR32_WHITE);
    UI_WriteSimpleButton(PX(472), PY(512), PW(80), PH(28), "Okay", UI_WINDOW_CLOSE_ACTION);
    UI_WriteWindowEnd(ent);
}

static cstring_t UI_WorldMapAreaTitle(uint8_t const *data, uint32_t size, uint32_t area_id);

/* Shared validation keeps chooser entries and accepted row commands aligned. */
static cstring_t UI_WorldMapRowName(stbDbc_t const *header, uint8_t const *record,
                                   uint8_t const *strings) {
    if (Stb_DbcField(header, record, 1) != CM_WowGetMapId()) return NULL;
    float left = Stb_DbcReadFloat(record + 16), right = Stb_DbcReadFloat(record + 20);
    float top = Stb_DbcReadFloat(record + 24), bottom = Stb_DbcReadFloat(record + 28);
    if (!isfinite(left) || !isfinite(right) || !isfinite(top) || !isfinite(bottom) ||
        !isfinite(right - left) || !isfinite(bottom - top) ||
        fabsf(right - left) < 1.0f || fabsf(bottom - top) < 1.0f) return NULL;
    uint32_t offset = Stb_DbcField(header, record, 3);
    cstring_t name = Stb_DbcString(strings, header->string_size, offset);
    if (!name || !memchr(name, '\0', header->string_size - offset) ||
        !*name || strlen(name) >= sizeof(((wowClient_t *)0)->worldmap_area)) return NULL;
    for (cstring_t p = name; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '_') return NULL;
    return name;
}

/* Resolve the smallest map area containing the player's world coordinates.
 * Classic map rows store the horizontal (world Y) bounds before vertical X.
 * A continent-sized row provides a fallback when no smaller area contains the
 * player. */
bool UI_ResolveWorldMap(edict_t *ent) {
    wowClient_t *wc = (wowClient_t *)ent->client;
    uint32_t size = 0;
    uint8_t *data = gi.ReadFile("DBFilesClient\\WorldMapArea.dbc", &size);
    stbDbc_t header;
    float smallest_area = INFINITY;
    bool showing_continent = false;
    uint32_t selected_area = 0;

    wc->worldmap_area[0] = '\0';
    wc->worldmap_area_record_id = 0;
    wc->worldmap_has_continent = false;
    if (!Stb_DbcValid(data, size, &header) || header.fields < 8 || header.record_size < 32) {
        wc->worldmap_title_area_id = 0;
        wc->worldmap_title[0] = '\0';
        SAFE_DELETE(data, gi.MemFree);
        return false;
    }
    uint8_t const *records = Stb_DbcRecords(data);
    uint8_t const *strings = Stb_DbcStrings(data, &header);
    FOR_LOOP(i, header.records) {
        uint8_t const *record = Stb_DbcRecordAt(records, i, header.record_size);
        cstring_t name = UI_WorldMapRowName(&header, record, strings);
        if (!name || (wc->worldmap_selection &&
            (wc->worldmap_selection != i + 1 || Stb_DbcField(&header, record, 2) == 0))) continue;
        float left = Stb_DbcReadFloat(record + 16), right = Stb_DbcReadFloat(record + 20);
        float top = Stb_DbcReadFloat(record + 24), bottom = Stb_DbcReadFloat(record + 28);
        float width = fabsf(right - left), height = fabsf(bottom - top);
        float x = ent->s.origin.x, y = ent->s.origin.y;
        if (!wc->worldmap_selection && (y < MIN(left, right) || y > MAX(left, right) ||
            x < MIN(top, bottom) || x > MAX(top, bottom)))
            continue;
        bool continent = Stb_DbcField(&header, record, 2) == 0;
        wc->worldmap_has_continent |= continent;
        if (wc->worldmap_continent && showing_continent && !continent) continue;
        if ((!wc->worldmap_continent || showing_continent == continent) &&
            width * height >= smallest_area) continue;
        snprintf(wc->worldmap_area, sizeof(wc->worldmap_area), "%s", name);
        wc->worldmap_left = left;
        wc->worldmap_right = right;
        wc->worldmap_top = top;
        wc->worldmap_bottom = bottom;
        smallest_area = width * height;
        showing_continent = continent;
        selected_area = Stb_DbcField(&header, record, 2);
        wc->worldmap_area_record_id = Stb_DbcField(&header, record, 0);
    }
    gi.MemFree(data);
    if (wc->worldmap_title_area_id == selected_area) return wc->worldmap_area[0] != '\0';
    wc->worldmap_title_area_id = selected_area;
    wc->worldmap_title[0] = '\0';
    if (selected_area) {
        uint32_t area_size = 0;
        uint8_t *areas = gi.ReadFile("DBFilesClient\\AreaTable.dbc", &area_size);
        cstring_t title = UI_WorldMapAreaTitle(areas, area_size, selected_area);
        if (title) snprintf(wc->worldmap_title, sizeof(wc->worldmap_title), "%s", title);
        SAFE_DELETE(areas, gi.MemFree);
    }
    return wc->worldmap_area[0] != '\0';
}

/* AreaTable names are localized in fields 11 through 18 in classic DBCs.
 * Prefer the first nonempty archive locale; the archive determines which
 * localized strings are available. */
static cstring_t UI_WorldMapAreaTitle(uint8_t const *data, uint32_t size, uint32_t area_id) {
    stbDbc_t header;
    if (!Stb_DbcValid(data, size, &header) || header.fields < 12 || header.record_size < 48)
        return NULL;
    uint8_t const *records = Stb_DbcRecords(data);
    uint8_t const *strings = Stb_DbcStrings(data, &header);
    FOR_LOOP(i, header.records) {
        uint8_t const *record = Stb_DbcRecordAt(records, i, header.record_size);
        if (Stb_DbcField(&header, record, 0) != area_id) continue;
        for (uint32_t field = 11; field < MIN(header.fields, 19); field++) {
            uint32_t offset = Stb_DbcField(&header, record, field);
            cstring_t title = Stb_DbcString(strings, header.string_size, offset);
            if (title && *title && memchr(title, '\0', header.string_size - offset)) return title;
        }
        break;
    }
    return NULL;
}

/* Zone chooser uses DBC row indices only as ephemeral commands; every selection
 * is validated again by UI_ResolveWorldMap against the current map. */
static void UI_WriteWorldMapZones(wowClient_t *wc) {
    uint32_t size = 0, area_size = 0, count = 0;
    uint8_t *data = gi.ReadFile("DBFilesClient\\WorldMapArea.dbc", &size);
    uint8_t *areas = gi.ReadFile("DBFilesClient\\AreaTable.dbc", &area_size);
    wc->worldmap_has_next_page = false;
    stbDbc_t header;
    if (!Stb_DbcValid(data, size, &header) || header.fields < 8 || header.record_size < 32) {
        SAFE_DELETE(data, gi.MemFree);
        SAFE_DELETE(areas, gi.MemFree);
        return;
    }
    uint8_t const *records = Stb_DbcRecords(data);
    uint8_t const *strings = Stb_DbcStrings(data, &header);
    FOR_LOOP(i, header.records) {
        uint8_t const *record = Stb_DbcRecordAt(records, i, header.record_size);
        cstring_t name = UI_WorldMapRowName(&header, record, strings);
        if (!name || Stb_DbcField(&header, record, 2) == 0) continue;
        uint32_t n = count++;
        if (n / 20 != wc->worldmap_page) continue;
        char command[48];
        snprintf(command, sizeof(command), "worldmap zone %u", (unsigned)(i + 1));
        cstring_t title = UI_WorldMapAreaTitle(areas, area_size, Stb_DbcField(&header, record, 2));
        UI_WriteSimpleButton(PX(150 + (n % 20 / 10) * 360), PY(135 + (n % 10) * 36),
                             PW(340), PH(30), title ? title : name, command);
    }
    if (wc->worldmap_page > 0)
        UI_WriteSimpleButton(PX(150), PY(520), PW(100), PH(24), "Previous", "worldmap previous");
    wc->worldmap_has_next_page = (wc->worldmap_page + 1) * 20 < count;
    if (wc->worldmap_has_next_page)
        UI_WriteSimpleButton(PX(720), PY(520), PW(100), PH(24), "Next", "worldmap next");
    SAFE_DELETE(areas, gi.MemFree);
    gi.MemFree(data);
}

/* AreaPOI rows use world X/Y; show only entries on the current physical map
 * and inside the displayed area. Row IDs are validated again on selection. */
static void UI_WriteLandmarks(wowClient_t *wc, float x, float y, float width, float height) {
    uint32_t size = 0, count = 0;
    uint8_t *data = gi.ReadFile("DBFilesClient\\AreaPOI.dbc", &size);
    uint32_t rows = Wow_LandmarkRows(data, size);
    for (uint32_t row = 0; row < rows; ++row) {
        wowLandmark_t landmark;
        if (!Wow_LandmarkRead(data, size, row, CM_WowGetMapId(), wc->worldmap_left,
                              wc->worldmap_right, wc->worldmap_top, wc->worldmap_bottom, &landmark)) continue;
        if (wc->worldmap_landmarks_list) {
            uint32_t n = count++;
            if (n / 20 != wc->worldmap_landmarks_page) continue;
            char command[64];
            snprintf(command, sizeof(command), "worldmap landmark %u", row + 1);
            UI_WriteSimpleButton(PX(150 + (n % 20 / 10) * 360), PY(135 + (n % 10) * 36),
                                 PW(340), PH(30), landmark.name, command);
        } else if (wc->worldmap_landmarks_visible) {
            float px = x + width * landmark.u, py = y + height * landmark.v;
            /* Center remains within the map even for rows on an edge. */
            px = MAX(x + PW(4), MIN(x + width - PW(4), px));
            py = MAX(y + PH(4), MIN(y + height - PH(4), py));
            bool selected = wc->worldmap_landmarks_selected == row + 1;
            UI_WriteColorRect(px - PW(4), py - PH(4), PW(8), PH(8),
                              selected ? MAKE(color32_t, 255, 255, 255, 255) : MAKE(color32_t, 80, 210, 255, 255));
            if (selected) UI_WriteTextFrame(MIN(px + PW(5), x + width - PW(180)), MAX(y, py - PH(18)), PW(180), PH(18), landmark.name,
                                             COLOR32_WHITE, FONT_JUSTIFYLEFT);
        }
    }
    if (wc->worldmap_landmarks_list) {
        wc->worldmap_landmarks_next = (wc->worldmap_landmarks_page + 1) * 20 < count;
        if (!count) UI_WriteTextFrame(PX(150), PY(145), PW(300), PH(20), "No landmarks in this area.", COLOR32_WHITE, FONT_JUSTIFYLEFT);
        if (wc->worldmap_landmarks_page) UI_WriteSimpleButton(PX(150), PY(520), PW(100), PH(24), "Previous", "worldmap landmark_prev");
        if (wc->worldmap_landmarks_next) UI_WriteSimpleButton(PX(720), PY(520), PW(100), PH(24), "Next", "worldmap landmark_next");
    }
    SAFE_DELETE(data, gi.MemFree);
}

/* Map area record IDs select the art set. The terrain's AreaTable ID decides
 * which overlays the character has explored inside that set. */
static void UI_WriteExploredTerrain(wowClient_t const *client, float x, float y, float width, float height) {
    uint32_t count;
    wowMapOverlay_t *overlays = Wow_GetMapOverlays(&count);
    for (uint32_t i = 0; i < count; i++) {
        wowMapOverlay_t *overlay = &overlays[i];
        if (overlay->map_area != client->worldmap_area_record_id ||
            !Wow_MapOverlayDiscovered(&client->exploration, overlay)) continue;
        int columns = (overlay->width + 255) / 256;
        int rows = (overlay->height + 255) / 256;
        for (int part = 0; part < columns * rows; part++) {
            wowMapPiece_t piece;
            char path[MAX_PATHLEN];
            snprintf(path, sizeof(path), "Interface\\WorldMap\\%s\\%s%d.blp",
                     client->worldmap_area, overlay->texture, part + 1);
            if (!Wow_ReadOverlayTileSize(overlay, part, path) ||
                !Wow_MapOverlayPiece(overlay, part, &piece)) continue;
            UI_WriteImageUV(path, x + width * (piece.x / 1002.0f), y + height * (piece.y / 668.0f),
                            width * (piece.width / 1002.0f), height * (piece.height / 668.0f),
                            piece.u0, piece.u1, piece.v0, piece.v1, COLOR32_WHITE);
        }
    }
}

void UI_WriteWorldMap(edict_t *ent) {
    wowClient_t *wc = (wowClient_t *)ent->client;
    UI_WriteStart(LAYER_GAME_0);
    if (!wc->worldmap_open) {
        UI_WriteEnd();
        gi.unicast(ent);
        return;
    }

    float x = PX(128), y = PY(95), width = PW(768), height = PH(512);
    UI_WriteColorRect(x - PW(8), y - PH(35), width + PW(16), height + PH(70),
                      MAKE(color32_t, 18, 15, 12, 250));
    UI_WriteTextFrame(x + PW(8), y - PH(27), PW(465), PH(20),
                      wc->worldmap_title[0] ? wc->worldmap_title :
                      (wc->worldmap_area[0] ? wc->worldmap_area : "Map unavailable"),
                      MAKE(color32_t, 255, 215, 120, 255), FONT_JUSTIFYLEFT);
    UI_WriteSimpleButton(x + width - PW(65), y - PH(30), PW(62), PH(22), "Close", "worldmap");
    if (wc->worldmap_has_continent || wc->worldmap_selection) {
        UI_WriteSimpleButton(x + width - PW(175), y - PH(30), PW(104), PH(22),
                             (wc->worldmap_continent || wc->worldmap_selection) ? "Current zone" : "Continent",
                             (wc->worldmap_continent || wc->worldmap_selection) ? "worldmap current" : "worldmap continent");
    }

    UI_WriteSimpleButton(x + width - PW(280), y - PH(30), PW(100), PH(22),
                         wc->worldmap_choosing ? "Back" : "Zones", "worldmap zones");
    UI_WriteSimpleButton(x + PW(8), y + height + PH(6), PW(100), PH(22),
                         wc->worldmap_landmarks_list ? "Back to map" : "Landmarks", "worldmap places");
    UI_WriteSimpleButton(x + PW(112), y + height + PH(6), PW(115), PH(22),
                         wc->worldmap_landmarks_visible ? "Hide landmarks" : "Show landmarks", "worldmap landmarks");
    if (wc->worldmap_landmarks_list) {
        UI_WriteLandmarks(wc, x, y, width, height);
        UI_WriteEnd();
        gi.unicast(ent);
        return;
    }
    if (wc->worldmap_choosing) {
        UI_WriteWorldMapZones(wc);
        UI_WriteEnd();
        gi.unicast(ent);
        return;
    }

    if (!wc->worldmap_area[0]) {
        UI_WriteEnd();
        gi.unicast(ent);
        return;
    }
    for (uint32_t row = 0; row < 3; row++) {
        for (uint32_t col = 0; col < 4; col++) {
            char path[MAX_PATHLEN];
            float tile_w = col == 3 ? 234.0f : 256.0f;
            float tile_h = row == 2 ? 156.0f : 256.0f;
            snprintf(path, sizeof(path), "Interface\\WorldMap\\%s\\%s%u.blp",
                     wc->worldmap_area, wc->worldmap_area, row * 4 + col + 1);
            UI_WriteImageUV(path, x + width * (256.0f * col / 1002.0f),
                            y + height * (256.0f * row / 668.0f),
                            width * (tile_w / 1002.0f), height * (tile_h / 668.0f),
                            0.0f, tile_w / 256.0f, 0.0f, tile_h / 256.0f, COLOR32_WHITE);
        }
    }
    UI_WriteExploredTerrain(wc, x, y, width, height);
    UI_WriteLandmarks(wc, x, y, width, height);
    UI_WriteSimpleButton(x + PW(8), y + height + PH(6), PW(100), PH(22), "Landmarks", "worldmap places");
    UI_WriteSimpleButton(x + PW(112), y + height + PH(6), PW(115), PH(22),
                         wc->worldmap_landmarks_visible ? "Hide landmarks" : "Show landmarks", "worldmap landmarks");
    /* Only instantiated locations on this loaded map can become quest pins. */
    uint32_t pin_count = 0;
    for (uint32_t n = 0; n < (uint32_t)globals.num_edicts && pin_count < 64; ++n) {
        edict_t *location = &wow_edicts[n];
        if (!location->inuse) continue;
        wowEntityLocal_t *local = Wow_EntityLocal(location);
        if (!local) continue;
        for (uint32_t q = 0; q < wc->quest_count; ++q) {
            svQuestEntry_t const *state = &wc->quest_log[q];
            bool match = false;
            if (!wc->quest_tracked[q]) continue;
            if (state->status == SV_QUEST_ACTIVE && local->go_type == WOW_QUEST_OBJECTIVE_ANCHOR)
                match = local->go_entry == state->quest_id;
            else if (state->status == SV_QUEST_COMPLETE && (location->s.flags & EF_HAS_QUEST)) {
                uint32_t group = Wow_QuestGiverGroup(local->quest_id, &local->home);
                if (group == WOW_QUEST_GIVER_GROUP_NONE) match = local->quest_id == state->quest_id;
                else for (uint32_t k = 0; k < Wow_QuestGiverGroupCount(group); ++k)
                    if (Wow_QuestGiverInGroup(group, k)->quest_id == state->quest_id) { match = true; break; }
            }
            if (!match) continue;
            float pu = (location->s.origin.y - wc->worldmap_left) / (wc->worldmap_right - wc->worldmap_left);
            float pv = (location->s.origin.x - wc->worldmap_top) / (wc->worldmap_bottom - wc->worldmap_top);
            if (pu >= 0 && pu <= 1 && pv >= 0 && pv <= 1) {
                UI_WriteColorRect(x + width * pu - PW(4), y + height * pv - PH(4), PW(8), PH(8),
                                  MAKE(color32_t, 255, 215, 40, 255));
                ++pin_count;
            }
            break;
        }
    }
    float u = (ent->s.origin.y - wc->worldmap_left) / (wc->worldmap_right - wc->worldmap_left);
    float v = (ent->s.origin.x - wc->worldmap_top) / (wc->worldmap_bottom - wc->worldmap_top);
    if (u >= 0 && u <= 1 && v >= 0 && v <= 1)
        UI_WriteImage("Interface\\WorldMap\\WorldMapPlayerIcon.blp",
                      x + width * u - PW(10), y + height * v - PH(10), PW(20), PH(20), COLOR32_WHITE);
    UI_WriteEnd();
    gi.unicast(ent);
}

/* Refresh the small feedback layout during a cast or cooldown, and clear it
 * once when both timers have expired. The retained HUD stays in place. */
void UI_WriteWowCombatFeedback(edict_t *ent, bool force) {
    wowEntityLocal_t const *local = Wow_EntityLocal(ent);
    wowClient_t *client = (wowClient_t *)ent->client;
    bool casting = local->cast_spell < wow_spell_count && local->cast_duration > 0;
    if (!force && !casting && !client->combat_feedback_visible) return;

    UI_WriteStart(LAYER_INFOPANEL);
    if (casting) {
        float progress = 1.0f - (float)local->cast_remaining / (float)local->cast_duration;
        char text[96];
        snprintf(text, sizeof(text), "%s  %.1f s", wow_spells[local->cast_spell].name,
                 (float)local->cast_remaining / 1000.0f);
        UI_WriteColorRect(PX(337), PY(674), PW(350), PH(25), MAKE(color32_t, 0, 0, 0, 224));
        UI_WriteColorBar(PX(338), PY(675), PW(348), PH(23), progress, 1.0f,
                         MAKE(color32_t, 182, 119, 30, 255));
        UI_WriteTextFrame(PX(338), PY(676), PW(348), PH(20), text, COLOR32_WHITE,
                          FONT_JUSTIFYCENTER);
    }
    UI_WriteEnd();
    gi.unicast(ent);
    client->combat_feedback_visible = casting;
}

/* Build and unicast the retained WoW HUD layers for a player. */
void UI_WriteWowHud(edict_t *ent) {
    player_t *ps;
    wowClient_t *wc;
    char copper_buf[64];

    if (!ent || !ent->client)
        return;
    ps = &ent->client->ps;
    wc = (wowClient_t *)ent->client;

    UI_WriteStart(LAYER_CONSOLE);

    /* The server owns the tracker and sends the same objective counts as the journal. */
    {
        float y = PY(287);
        uint32_t shown = 0;
        for (uint32_t i = 0; i < wc->quest_count && shown < 5; ++i) {
            wowQuestDetail_t const *detail;
            svQuestEntry_t const *state = &wc->quest_log[i];
            char line[192];
            if (!wc->quest_tracked[i]) continue;
            detail = Wow_QuestDetail(state->quest_id);
            snprintf(line, sizeof(line), "%s%s", detail ? detail->title : "Unknown Quest",
                     state->status == SV_QUEST_COMPLETE ? " (Complete)" : "");
            UI_WriteTextFrame(PX(790), y, PW(220), PH(18), line,
                              MAKE(color32_t, 255, 215, 120, 255), FONT_JUSTIFYLEFT);
            y += PH(18);
            if (detail && state->status != SV_QUEST_COMPLETE) {
                for (uint32_t j = 0; j < detail->kill_objective_count; ++j) {
                    cstring_t name = Wow_CachedCreatureName(detail->kill_objectives[j].display_id);
                    snprintf(line, sizeof(line), "%s: %u/%u", name ? name : "Creature",
                             (unsigned)wc->kill_progress[i][j],
                             (unsigned)detail->kill_objectives[j].required_count);
                    UI_WriteTextFrame(PX(800), y, PW(210), PH(18), line, COLOR32_WHITE, FONT_JUSTIFYLEFT);
                    y += PH(18);
                }
            }
            ++shown;
        }
    }

    /* Character/targeting frame (portrait area top-left) */
    UI_WriteTargetingFrame(ent);

    /* Damage flash overlays — brief text shown after hits.  Timers tick in Wow_UpdatePlayerHud. */
    if (wc->outgoing_dmg_timer > 0) {
        char dmg_buf[32];
        snprintf(dmg_buf, sizeof(dmg_buf), "-%u", (unsigned)wc->outgoing_damage);
        UI_WriteTextFrame(PX(180.0f), PY(72.0f), PW(80), PH(18), dmg_buf,
                          MAKE(color32_t, 255, 255, 50, 255), FONT_JUSTIFYLEFT);
    }
    if (wc->incoming_dmg_timer > 0) {
        char dmg_buf[32];
        snprintf(dmg_buf, sizeof(dmg_buf), "-%u", (unsigned)wc->incoming_damage);
        UI_WriteTextFrame(PX(130.0f), PY(60.0f), PW(80), PH(18), dmg_buf,
                          MAKE(color32_t, 255, 60, 60, 255), FONT_JUSTIFYLEFT);
    }

    /* Main action bar + end-caps */
    UI_WriteActionBar();

    /* First 6 inventory slots shown in the quick-access bar; all 16 visible in backpack window. */
    FOR_LOOP(i, 6) {
        uint32_t img = wc->inventory[i].icon[0] ? gi.ImageIndex(wc->inventory[i].icon) : 0;
        UI_WriteActionButtonSlot(PX(939.0f - (float)i * 42.0f), PY(728), img, wc->inventory[i].count);
    }

    /* Backpack button — image + click region to toggle the backpack window */
    UI_WriteImage("Interface\\Buttons\\Button-Backpack-Up.blp", PX(981), PY(729), PW(37), PH(37), COLOR32_WHITE);
    UI_WriteClickRegion(PX(981), PY(729), PW(37), PH(37), "backpack");

    /* Minimap border + viewport */
    UI_WriteMinimapFrames();

    /* Quest log icon + label */
    UI_WriteImage("Interface\\QuestFrame\\UI-QuestLog-BookIcon.blp", PX(840), PY(162), PW(32), PH(32), COLOR32_WHITE);
    UI_WriteTextFrame(PX(876), PY(164), PW(110), PH(20), "Quests", MAKE(color32_t, 255, 215, 120, 255), FONT_JUSTIFYLEFT);
    UI_WriteClickRegion(PX(834), PY(156), PW(72), PH(44), "quest");
    UI_WriteClickRegion(PX(910), PY(156), PW(78), PH(44), "questlog");

    UI_WriteSimpleButton(PX(840), PY(200), PW(115), PH(24), "Spellbook", "spellbook");
    UI_WriteSimpleButton(PX(840), PY(228), PW(115), PH(24), "Flight paths (T)", "taxi");

    UI_WriteSimpleButton(PX(840), PY(256), PW(115), PH(24), "First Aid", "crafting");

    /* Copper display */
    snprintf(copper_buf, sizeof(copper_buf), "Copper %d", (int)ps->stats[WOW_STAT_COPPER]);
    UI_WriteTextFrame(PX(816), PY(704), PW(150), PH(20), copper_buf, MAKE(color32_t, 255, 210, 100, 255), FONT_JUSTIFYRIGHT);

    /* Loot and backpack windows are HUD overlays on LAYER_CONSOLE.  They must be
     * written before the console terminator: writing them after a layer's
     * terminator (as previously done after UI_WriteQuestDialog) appends raw frame
     * bytes the client parses as a new message id and discards. */
    UI_WriteLootWindow(ent);
    UI_WriteBackpackWindow(ent);
    UI_WriteSpellbook(ent);
    UI_WriteCrafting(ent);
    UI_WriteEnd();
    UI_WriteStart(LAYER_GAME_3);
    UI_WriteMerchantWindow(ent);
    UI_WriteBankWindow(ent);
    UI_WriteEnd();
    UI_WriteWowMessageQueue(ent);
    UI_WriteQuestDialog(ent);
    UI_WriteQuestLog(ent);
    gi.unicast(ent);
    UI_WriteWowActionBar(ent);
}

/* Taxi destinations and commands are authored by the game and revalidated on selection. */
void UI_WriteTaxi(edict_t *ent) {
    wowClient_t *c; size_t i; unsigned shown=0, eligible=0; bool has_next=false;
    if (!ent || !ent->client) return;
    c=(wowClient_t *)ent->client;
    UI_WriteStart(LAYER_GAME_2);
    if (c->taxi_open) {
        UI_WriteTextFrame(PX(300),PY(100),PW(424),PH(24),"Flight paths",COLOR32_WHITE,FONT_JUSTIFYCENTER);
        for(i=0;i<wow_taxi_graph.node_count;i++) {
            wowTaxiNode_t const *node=&wow_taxi_graph.nodes[i]; wowTaxiRoute_t route;
            char text[192], cmd[48];
            if(node->id / 8>=sizeof(c->taxi_known) || node->id==c->taxi_source || !(c->taxi_known[node->id / 8] & (1u << (node->id % 8))) ||
               !WowTaxi_Route(&wow_taxi_graph,c->taxi_source,node->id,c->taxi_known,sizeof(c->taxi_known),&route)) continue;
            if (eligible++ < c->taxi_page * 12) continue;
            if (shown == 12) { has_next=true; break; }
            snprintf(text,sizeof(text),"%s (%llu copper)",node->name,(unsigned long long)route.price);
            snprintf(cmd,sizeof(cmd),"taxi %u",node->id);
            UI_WriteSimpleButton(PX(300),PY(132+shown*28),PW(424),PH(24),text,cmd);
            shown++;
        }
        if(!shown) UI_WriteTextFrame(PX(300),PY(140),PW(424),PH(24),
            c->taxi_source ? "Discover more flight paths to travel." : "Stand near a flight path to discover it.",
            COLOR32_WHITE,FONT_JUSTIFYCENTER);
        if (c->taxi_page) UI_WriteSimpleButton(PX(300),PY(485),PW(100),PH(24),"Previous","taxi previous");
        if (has_next) UI_WriteSimpleButton(PX(624),PY(485),PW(100),PH(24),"Next","taxi next");
        UI_WriteSimpleButton(PX(450),PY(485),PW(124),PH(24),"Close","taxi close");
    }
    UI_WriteEnd();
    gi.unicast(ent);
}
