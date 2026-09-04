/*
 * hud.c — FDF → uiframe serialization bridge.
 *
 * Converts parsed FRAMEDEF trees (from stb_fdf.h) into uiFrame_t wire
 * format for transmission via svc_layout.  These functions need gi for
 * network writes, so they live in the game module rather than menu_fdf.c.
 *
 * HUD panels have been split into sibling files in game/hud/:
 *   hud_write.c     — frame-write primitives, theme, text formatting
 *   hud_console.c   — ConsoleUI backdrop, minimap, resource bar
 *   hud_commands.c  — command buttons, build queue, inventory
 *   hud_infopanel.c — info panel, multiselect, per-frame update stubs
 *   hud_quests.c    — quest dialog
 *   hud_log.c       — persistent single-player message log
 *   hud_cinematic.c — cinematic layer, interface toggle, message overlay
 */

#include "hud_local.h"
#include "hud_utils.h"

hud_t hud;

/* frames[] is defined in fdf_parser.c (common/) */

#define MAX_FRAMES_WRITE 1024
static LPCFRAMEDEF framesWritten[MAX_FRAMES_WRITE];
static LPCFRAMEDEF *frameptr;

void UI_ResetFrameWriteList(void) {
    frameptr = framesWritten;
}

static BOOL AddFrame(LPCFRAMEDEF frame) {
    if (frameptr - framesWritten < MAX_FRAMES_WRITE) {
        *(frameptr++) = frame;
        return true;
    }
    return false;
}

static DWORD FindFrameNumber(LPCFRAMEDEF frame, DWORD def) {
    for (LPCFRAMEDEF *it = framesWritten; it < frameptr; it++) {
        if (*it == frame) {
            def = (DWORD)(it - framesWritten) + 1;
        }
    }
    return def;
}

DWORD UI_FindFrameNumber(LPCSTR name) {
    LPFRAMEDEF frame = UI_FindFrame(name);
    return frame ? FindFrameNumber(frame, 0) : 0;
}

#define CONVERT_UV(DEST, SRC) \
    DEST[0] = SRC.min.x * 0xff; \
    DEST[1] = SRC.max.x * 0xff; \
    DEST[2] = SRC.min.y * 0xff; \
    DEST[3] = SRC.max.y * 0xff;

static void UI_CopyFrameBase(LPUIFRAME dest, LPCFRAMEDEF src) {
    AddFrame(src);
    FOR_LOOP(i, FPP_COUNT * 2) {
        dest->points.x[i].targetPos = src->Points.x[i].targetPos;
        dest->points.x[i].used = src->Points.x[i].used;
        dest->points.x[i].relativeTo = FindFrameNumber(src->Points.x[i].relativeTo, UI_PARENT);
        dest->points.x[i].offset = (SHORT)(src->Points.x[i].offset * UI_FRAMEPOINT_SCALE);
    }
    static char tooltip[1024];
    LPCSTR tooltip_text = NULL;
    tooltip[0] = '\0';
    if (src->Tip && src->Ubertip) {
        snprintf(tooltip, sizeof(tooltip), "%s\n%s", src->Tip, src->Ubertip);
        tooltip_text = tooltip;
    } else if (src->Tip) {
        tooltip_text = src->Tip;
    } else if (src->Ubertip) {
        tooltip_text = src->Ubertip;
    }
    CONVERT_UV(dest->tex.coord, src->Texture.TexCoord);
    dest->number = FindFrameNumber(src, 0);
    dest->parent = FindFrameNumber(src->Parent, 0);
    dest->color = src->Color;
    dest->size.width = src->Width;
    dest->size.height = src->Height;
    dest->tex.index = UI_LiveImage(src->Texture.Image);
    dest->tex.index2 = UI_LiveImage(src->Texture.Image2);
    dest->flags.type = src->Type;
    dest->flags.alphaMode = src->AlphaMode;
    dest->textLength = src->TextLength;
    dest->stat = src->Stat;
    dest->text = src->Text;
    dest->tooltip = tooltip_text;
    dest->onclick = src->OnClick;
}

static uiBackdrop_t MakeBackdrop(LPCFRAMEDEF frame) {
    if (!frame) return (uiBackdrop_t){ 0 };
    return MAKE(uiBackdrop_t,
        .CornerFlags = frame->Backdrop.CornerFlags,
        .TileBackground = frame->Backdrop.TileBackground,
        .Background = UI_LiveImage(frame->Backdrop.Background),
        .CornerSize = frame->Backdrop.CornerSize,
        .BackgroundSize = frame->Backdrop.BackgroundSize,
        .BackgroundInsets = {
            frame->Backdrop.BackgroundInsets[0],
            frame->Backdrop.BackgroundInsets[1],
            frame->Backdrop.BackgroundInsets[2],
            frame->Backdrop.BackgroundInsets[3],
        },
        .EdgeFile = UI_LiveImage(frame->Backdrop.EdgeFile),
        .BlendAll = frame->Backdrop.BlendAll,
        .Mirrored = frame->Backdrop.Mirrored,
    );
}

static uiHighlight_t MakeHighlight(LPCFRAMEDEF frame) {
    if (!frame) return (uiHighlight_t){ 0 };
    return MAKE(uiHighlight_t,
        .alphaFile = UI_LiveImage(frame->Highlight.AlphaFile),
        .alphaMode = frame->Highlight.AlphaMode,
    );
}

static uiLabel_t MakeLabel(LPCFRAMEDEF frame) {
    return MAKE(uiLabel_t,
        .textalignx = frame->Font.Justification.Horizontal,
        .textaligny = frame->Font.Justification.Vertical,
        /* Label offsets share the fixed-point wire scale used by frame points;
         * assigning normalized FDF floats directly truncated authored padding. */
        .offsetx = frame->Font.Justification.Offset.x * UI_FRAMEPOINT_SCALE,
        .offsety = frame->Font.Justification.Offset.y * UI_FRAMEPOINT_SCALE,
        .font = UI_LiveFont(frame->Font.Index),
    );
}

static LPCFRAMEDEF UI_ButtonPart(LPCFRAMEDEF frame, LPCSTR name) {
    if (!frame || !name || !*name) return NULL;
    return UI_FindFrameNear(frame, name);
}

typedef struct {
    LPCSTR background;
    LPCSTR edge;
} uiControlSkin_t;

/* Blizzard's EscMenu templates clone these child names into every stock
 * GLUETEXTBUTTON/GLUECHECKBOX. The cached child FRAMEDEF can already contain a
 * placeholder image by the time the server-authored window is serialized, so
 * the original skin key is no longer recoverable from the image configstring.
 * The child role itself is stable, however. Resolve the stock role through
 * war3skins for the concrete local player instead of trusting the cached art. */
static BOOL UI_ControlBackdropSkin(LPCSTR name, uiControlSkin_t *skin) {
    if (!name || !skin) return false;
    *skin = (uiControlSkin_t){ 0 };

    if (!strcmp(name, "ButtonBackdropTemplate") ||
        !strcmp(name, "EscMenuButtonBackdropTemplate")) {
        skin->background = "EscMenuButtonBackground";
        skin->edge = "EscMenuButtonBorder";
    } else if (!strcmp(name, "ButtonPushedBackdropTemplate") ||
               !strcmp(name, "EscMenuButtonPushedBackdropTemplate")) {
        skin->background = "EscMenuButtonPushedBackground";
        skin->edge = "EscMenuButtonPushedBorder";
    } else if (!strcmp(name, "ButtonDisabledBackdropTemplate") ||
               !strcmp(name, "EscMenuButtonDisabledBackdropTemplate")) {
        skin->background = "EscMenuButtonDisabledBackground";
        skin->edge = "EscMenuButtonDisabledBorder";
    } else if (!strcmp(name, "ButtonDisabledPushedBackdropTemplate") ||
               !strcmp(name, "EscMenuButtonDisabledPushedBackdropTemplate")) {
        skin->background = "EscMenuButtonDisabledPushedBackground";
        skin->edge = "EscMenuButtonDisabledPushedBorder";
    } else if (!strcmp(name, "EscMenuCheckBoxBackdrop")) {
        skin->background = "EscMenuCheckBoxBackground";
    } else if (!strcmp(name, "EscMenuCheckBoxPushedBackdrop")) {
        skin->background = "EscMenuCheckBoxPushedBackground";
    } else if (!strcmp(name, "EscMenuDisabledCheckBoxBackdrop")) {
        skin->background = "EscMenuDisabledCheckBoxBackground";
    } else {
        return false;
    }
    return true;
}

static LPCSTR UI_ControlHighlightSkin(LPCSTR name) {
    if (!name) return NULL;
    if (!strcmp(name, "ButtonMouseOverHighlightTemplate") ||
        !strcmp(name, "EscMenuButtonMouseOverHighlightTemplate")) {
        return "EscMenuButtonMouseOverHighlight";
    }
    if (!strcmp(name, "EscMenuCheckHighlightTemplate")) {
        return "EscMenuCheckBoxCheckHighlight";
    }
    if (!strcmp(name, "EscMenuDisabledCheckHighlightTemplate")) {
        return "EscMenuDisabledCheckHighlight";
    }
    return NULL;
}

static DWORD UI_ControlThemeImage(LPCSTR key) {
    if (!key || !*key) return 0;
    return gi.ImageIndex(key);
}

static uiBackdrop_t MakeButtonBackdrop(LPCFRAMEDEF frame, LPCSTR name) {
    LPCFRAMEDEF part = UI_ButtonPart(frame, name);
    uiBackdrop_t result = { 0 };
    uiControlSkin_t skin;
    DWORD themed;

    if (!part) return result;
    if (part->Type == FT_BACKDROP) result = MakeBackdrop(part);
    else if (part->Type == FT_TEXTURE) result.Background = UI_LiveImage(part->Texture.Image);
    else return result;

    if (UI_ControlBackdropSkin(name, &skin) || UI_ControlBackdropSkin(part->Name, &skin)) {
        themed = UI_ControlThemeImage(skin.background);
        if (themed) result.Background = themed;
        themed = UI_ControlThemeImage(skin.edge);
        if (themed) result.EdgeFile = themed;
    }
    return result;
}

static uiHighlight_t MakeButtonHighlight(LPCFRAMEDEF frame, LPCSTR name) {
    LPCFRAMEDEF part = UI_ButtonPart(frame, name);
    uiHighlight_t result = { 0 };
    LPCSTR skin_key;
    DWORD themed;

    if (!part) return result;
    if (part->Type == FT_HIGHLIGHT) {
        result = MAKE(uiHighlight_t,
            .alphaFile = UI_LiveImage(part->Highlight.AlphaFile),
            .alphaMode = part->Highlight.AlphaMode,
        );
    } else if (part->Type == FT_TEXTURE) {
        result = MAKE(uiHighlight_t,
            .alphaFile = UI_LiveImage(part->Texture.Image),
            .alphaMode = part->AlphaMode,
        );
    } else {
        return result;
    }

    skin_key = UI_ControlHighlightSkin(name);
    if (!skin_key) skin_key = UI_ControlHighlightSkin(part->Name);
    themed = UI_ControlThemeImage(skin_key);
    if (themed) result.alphaFile = themed;
    return result;
}

static LPCSTR UI_ButtonStateName(LPCSTR preferred, LPCSTR fallback) {
    return preferred && *preferred ? preferred : fallback;
}

static uiGlueTextButton_t MakeGlueTextButton(LPCFRAMEDEF frame) {
    LPCSTR normal = UI_ButtonStateName(frame->Control.Backdrop.Normal, frame->Button.NormalTexture);
    LPCSTR pushed = UI_ButtonStateName(frame->Control.Backdrop.Pushed, frame->Button.PushedTexture);
    LPCSTR disabled = UI_ButtonStateName(frame->Control.Backdrop.Disabled, frame->Button.DisabledTexture);
    LPCSTR disabled_pushed = UI_ButtonStateName(frame->Control.Backdrop.DisabledPushed, disabled);
    LPCSTR highlight = UI_ButtonStateName(frame->Control.Backdrop.MouseOver, frame->Button.UseHighlight);
    uiGlueTextButton_t result = {
        .normal = MakeButtonBackdrop(frame, normal),
        .pushed = MakeButtonBackdrop(frame, UI_ButtonStateName(pushed, normal)),
        .disabled = MakeButtonBackdrop(frame, UI_ButtonStateName(disabled, normal)),
        .disabledPushed = MakeButtonBackdrop(frame, UI_ButtonStateName(disabled_pushed, disabled)),
        .highlight = MakeButtonHighlight(frame, highlight),
        .pushedTextOffset = frame->Button.PushedTextOffset,
    };

    if (!result.pushed.Background && !result.pushed.EdgeFile) result.pushed = result.normal;
    if (!result.disabled.Background && !result.disabled.EdgeFile) result.disabled = result.normal;
    if (!result.disabledPushed.Background && !result.disabledPushed.EdgeFile)
        result.disabledPushed = result.disabled;
    return result;
}

static uiCheckBox_t MakeCheckBox(LPCFRAMEDEF frame) {
    LPCSTR normal = UI_ButtonStateName(frame->Control.Backdrop.Normal, frame->Button.NormalTexture);
    LPCSTR pushed = UI_ButtonStateName(frame->Control.Backdrop.Pushed, frame->Button.PushedTexture);
    LPCSTR disabled = UI_ButtonStateName(frame->Control.Backdrop.Disabled, frame->Button.DisabledTexture);
    LPCSTR disabled_pushed = UI_ButtonStateName(frame->Control.Backdrop.DisabledPushed, disabled);
    uiCheckBox_t result = {
        .normal = MakeButtonBackdrop(frame, normal),
        .pushed = MakeButtonBackdrop(frame, UI_ButtonStateName(pushed, normal)),
        .disabled = MakeButtonBackdrop(frame, UI_ButtonStateName(disabled, normal)),
        .disabledPushed = MakeButtonBackdrop(frame, UI_ButtonStateName(disabled_pushed, disabled)),
        .mouseOver = MakeButtonHighlight(frame, frame->Control.Backdrop.MouseOver),
        .checked = MakeButtonHighlight(frame, frame->CheckBox.CheckHighlight),
        .disabledChecked = MakeButtonHighlight(frame, frame->CheckBox.DisabledCheckHighlight),
    };

    if (!result.pushed.Background && !result.pushed.EdgeFile) result.pushed = result.normal;
    if (!result.disabled.Background && !result.disabled.EdgeFile) result.disabled = result.normal;
    if (!result.disabledPushed.Background && !result.disabledPushed.EdgeFile)
        result.disabledPushed = result.disabled;
    if (!result.disabledChecked.alphaFile) result.disabledChecked = result.checked;
    return result;
}


/* Server-authored Warcraft FDF scrollbars keep their increment/decrement/thumb
 * buttons as child frames.  Those children are control art, not standalone
 * layout objects, so serialize the complete legacy scrollbar payload here. */
static uiBackdrop_t MakeScrollBarImage(DWORD image) {
    return MAKE(uiBackdrop_t,
        .Background = image,
        .BlendAll = true,
    );
}

static DWORD UI_ScrollBarThemeImage(LPCSTR key, LPCSTR fallback) {
    DWORD image = UI_ControlThemeImage(key);
    if (!image && fallback && *fallback)
        image = gi.ImageIndex(UI_ResolveTextureAlias(fallback));
    return image;
}

static uiScrollBar_t MakeScrollBar(LPCFRAMEDEF frame) {
    uiScrollBar_t result = { 0 };
    DWORD track = UI_ScrollBarThemeImage(
        "EscMenuSliderBackground", "UI\\Widgets\\EscMenu\\Human\\slider-background.blp");
    DWORD border = UI_ScrollBarThemeImage(
        "EscMenuSliderBorder", "UI\\Widgets\\EscMenu\\Human\\slider-border.blp");
    DWORD thumb = UI_ScrollBarThemeImage(
        "EscMenuSliderThumbButton", "UI\\Widgets\\EscMenu\\Human\\slider-knob.blp");
    DWORD up = gi.ImageIndex(UI_ResolveTextureAlias(
        "UI\\Widgets\\Glues\\GlueScreen-Scrollbar-UpArrow.blp"));
    DWORD down = gi.ImageIndex(UI_ResolveTextureAlias(
        "UI\\Widgets\\Glues\\GlueScreen-Scrollbar-DownArrow.blp"));

    if (!frame) return result;

    /* Keep authored backdrop geometry when present, but make stock EscMenu art
     * deterministic for cached FDF templates just like buttons/checkboxes. */
    result.background = MakeBackdrop(frame);
    if (track) result.background.Background = track;
    if (border) result.background.EdgeFile = border;
    if (!result.background.CornerSize) result.background.CornerSize = 0.006f;
    if (!result.background.BackgroundSize) result.background.BackgroundSize = 0.006f;
    result.background.BlendAll = true;
    result.background.TileBackground = true;

    result.decButton = MakeScrollBarImage(up);
    result.incButton = MakeScrollBarImage(down);
    result.thumbButton = MakeScrollBarImage(thumb);
    return result;
}

static void UI_PrepareTextAreaScrollBar(LPCFRAMEDEF text_area) {
    LPFRAMEDEF scrollbar;
    FLOAT inset;

    if (!text_area || text_area->Type != FT_TEXTAREA || !text_area->TextArea.ScrollBar[0]) return;
    scrollbar = UI_FindFrameNear(text_area, text_area->TextArea.ScrollBar);
    if (!scrollbar || scrollbar->Type != FT_SCROLLBAR || scrollbar->hidden) return;

    /* Blizzard TextArea semantics attach the declared scrollbar to the right
     * edge of the text viewport even when the FDF scrollbar template itself
     * supplies only a width.  The server-authored bridge must reproduce that
     * relationship before flattening the tree to uiFrame_t anchors. */
    inset = text_area->TextArea.Inset;
    memset(&scrollbar->Points, 0, sizeof(scrollbar->Points));
    scrollbar->AnyPointsSet = true;
    UI_SetPoint(scrollbar, FRAMEPOINT_TOPRIGHT, text_area, FRAMEPOINT_TOPRIGHT,
                -inset, -inset);
    UI_SetPoint(scrollbar, FRAMEPOINT_BOTTOMRIGHT, text_area, FRAMEPOINT_BOTTOMRIGHT,
                -inset, inset);
    if (scrollbar->Width <= 0.0f) scrollbar->Width = 0.015f;
}

static uiSimpleButtonState_t MakeSimpleButtonState(LPCFRAMEDEF frame,
                                                    LPCSTR texture_name,
                                                    BUTTONTEXT const *button_text,
                                                    COLOR32 fallback_color)
{
    LPCFRAMEDEF texture = UI_ButtonPart(frame, texture_name);
    LPCFRAMEDEF text = button_text && button_text->frame[0]
        ? UI_ButtonPart(frame, button_text->frame)
        : NULL;
    COLOR32 fontcolor = text && text->Font.Color.a
        ? text->Font.Color
        : fallback_color.a ? fallback_color : COLOR32_WHITE;
    uiSimpleButtonState_t result = {
        .texture = UI_LiveImage(texture && texture->Type == FT_TEXTURE
            ? texture->Texture.Image : frame->Texture.Image),
        .font = UI_LiveFont(text ? text->Font.Index : frame->Font.Index),
        .fontcolor = fontcolor,
    };
    BOX2 const uv = texture && texture->Type == FT_TEXTURE
        ? texture->Texture.TexCoord
        : frame->Texture.TexCoord;
    CONVERT_UV(result.texcoord, uv);
    return result;
}

static uiSimpleButton_t MakeSimpleButton(LPCFRAMEDEF frame) {
    uiSimpleButton_t result = {
        .normal = MakeSimpleButtonState(frame, frame->Button.NormalTexture,
                                        &frame->Button.NormalText, frame->Font.Color),
        .pushed = MakeSimpleButtonState(frame,
                                        UI_ButtonStateName(frame->Button.PushedTexture,
                                                           frame->Button.NormalTexture),
                                        &frame->Button.NormalText, frame->Font.Color),
        .disabled = MakeSimpleButtonState(frame,
                                          UI_ButtonStateName(frame->Button.DisabledTexture,
                                                             frame->Button.NormalTexture),
                                          &frame->Button.DisabledText,
                                          frame->Font.DisabledColor.a
                                              ? frame->Font.DisabledColor
                                              : frame->Font.Color),
        .highlight = MakeSimpleButtonState(frame, frame->Button.UseHighlight,
                                           &frame->Button.HighlightText,
                                           frame->Font.HighlightColor.a
                                               ? frame->Font.HighlightColor
                                               : frame->Font.Color),
    };
    if (!result.pushed.texture) result.pushed = result.normal;
    if (!result.disabled.texture) result.disabled = result.normal;
    return result;
}

static BOOL UI_IsSingleLineText(LPCSTR text) {
    if (!text) return true;
    return strchr(text, '\n') == NULL && strchr(text, '\r') == NULL;
}

BOOL UI_BuildFrameForWrite(LPCFRAMEDEF frame,
                                  LPUIFRAME out,
                                  LPBYTE typedata,
                                  DWORD typedata_max,
                                  LPSTR textbuf,
                                  DWORD textbuf_max)
{
    struct { LPBYTE data; DWORD maxsize; DWORD cursize; BOOL overflowed; } buf = {
        .data = typedata, .maxsize = typedata_max,
    };

    if (!frame || !out || !typedata || typedata_max == 0 || !textbuf || textbuf_max == 0) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    memset(typedata, 0, typedata_max);
    memset(textbuf, 0, textbuf_max);

    UI_CopyFrameBase(out, frame);

    switch (frame->Type) {
        case FT_BACKDROP: {
            uiBackdrop_t data = MakeBackdrop(frame);
            if (buf.cursize + sizeof(data) <= buf.maxsize) {
                memcpy(buf.data + buf.cursize, &data, sizeof(data));
                buf.cursize += sizeof(data);
            } else { buf.overflowed = true; }
            break;
        }
        case FT_HIGHLIGHT: {
            uiHighlight_t data = MakeHighlight(frame);
            if (buf.cursize + sizeof(data) <= buf.maxsize) {
                memcpy(buf.data + buf.cursize, &data, sizeof(data));
                buf.cursize += sizeof(data);
            } else { buf.overflowed = true; }
            break;
        }
        case FT_TOOLTIPTEXT: {
            uiTooltip_t data = { .background = MakeBackdrop(frame), .text = MakeLabel(frame) };
            if (buf.cursize + sizeof(data) <= buf.maxsize) {
                memcpy(buf.data + buf.cursize, &data, sizeof(data));
                buf.cursize += sizeof(data);
            } else { buf.overflowed = true; }
            break;
        }
        case FT_NAMETAG: {
            uiNameTag_t data = { .background = MakeBackdrop(frame), .text = MakeLabel(frame),
                                 .padding_x = 0.008f, .padding_y = 0.006f };
            if (buf.cursize + sizeof(data) <= buf.maxsize) {
                memcpy(buf.data + buf.cursize, &data, sizeof(data)); buf.cursize += sizeof(data);
            } else { buf.overflowed = true; }
            break;
        }
        case FT_STRING:
        case FT_TEXT: {
            uiLabel_t data = MakeLabel(frame);
            /* Match the local FDF renderer: a single-line text frame with no
             * authored Width/Height uses the declared FDF font size for its
             * layout height.  Renderer glyph bounds are drawing metrics, not
             * the authored line box used by anchor chains. */
            if (frame->Width == 0.0f && frame->Height == 0.0f &&
                frame->Font.Size > 0.0f && UI_IsSingleLineText(frame->Text)) {
                out->size.height = frame->Font.Size;
            }
            if (!out->points.x[FPP_MIN].used && !out->points.x[FPP_MID].used && !out->points.x[FPP_MAX].used) {
                DWORD anchor = frame->Font.Justification.Horizontal ^ 1;
                out->points.x[anchor].targetPos = anchor;
                out->points.x[anchor].relativeTo = UI_PARENT;
                out->points.x[anchor].used = 1;
            }
            if (!out->points.y[FPP_MIN].used && !out->points.y[FPP_MID].used && !out->points.y[FPP_MAX].used) {
                DWORD anchor = frame->Font.Justification.Vertical ^ 1;
                out->points.y[anchor].targetPos = anchor;
                out->points.y[anchor].relativeTo = UI_PARENT;
                out->points.y[anchor].used = 1;
            }
            out->color = frame->Font.Color;
            if (*frame->Text == '\0') out->text = frame->Name;
            if (buf.cursize + sizeof(data) <= buf.maxsize) {
                memcpy(buf.data + buf.cursize, &data, sizeof(data));
                buf.cursize += sizeof(data);
            } else { buf.overflowed = true; }
            break;
        }
        case FT_TEXTAREA: {
            uiTextArea_t data = { .font = UI_LiveFont(frame->Font.Index), .inset = frame->TextArea.Inset };
            if (buf.cursize + sizeof(data) <= buf.maxsize) {
                memcpy(buf.data + buf.cursize, &data, sizeof(data));
                buf.cursize += sizeof(data);
            } else { buf.overflowed = true; }
            break;
        }
        case FT_SCROLLBAR: {
            uiScrollBar_t data = MakeScrollBar(frame);
            FLOAT range = frame->Slider.MaxValue - frame->Slider.MinValue;
            out->value = range > 0.0f
                ? (frame->Slider.InitialValue - frame->Slider.MinValue) / range
                : 0.0f;
            out->value = MAX(0.0f, MIN(out->value, 1.0f));
            if (buf.cursize + sizeof(data) <= buf.maxsize) {
                memcpy(buf.data + buf.cursize, &data, sizeof(data));
                buf.cursize += sizeof(data);
            } else { buf.overflowed = true; }
            break;
        }
        case FT_SIMPLEBUTTON: {
            uiSimpleButton_t data = MakeSimpleButton(frame);
            LPCSTR text_key = frame->OnClick[0] || !frame->Button.DisabledText.text[0]
                ? frame->Button.NormalText.text
                : frame->Button.DisabledText.text;
            if ((!out->text || !*out->text) && text_key && *text_key)
                out->text = UI_GetString(text_key);
            if (buf.cursize + sizeof(data) <= buf.maxsize) {
                memcpy(buf.data + buf.cursize, &data, sizeof(data));
                buf.cursize += sizeof(data);
            } else { buf.overflowed = true; }
            break;
        }
        case FT_CHECKBOX:
        case FT_GLUECHECKBOX:
        case FT_SIMPLECHECKBOX: {
            uiCheckBox_t data = MakeCheckBox(frame);
            out->value = frame->CheckBox.Checked ? 1.0f : 0.0f;
            if (buf.cursize + sizeof(data) <= buf.maxsize) {
                memcpy(buf.data + buf.cursize, &data, sizeof(data));
                buf.cursize += sizeof(data);
            } else { buf.overflowed = true; }
            break;
        }
        case FT_BUTTON:
        case FT_TEXTBUTTON:
        case FT_POPUPMENU:
        case FT_GLUEPOPUPMENU:
        case FT_GLUETEXTBUTTON:
        case FT_GLUEBUTTON: {
            uiGlueTextButton_t data = MakeGlueTextButton(frame);
            if (buf.cursize + sizeof(data) <= buf.maxsize) {
                memcpy(buf.data + buf.cursize, &data, sizeof(data));
                buf.cursize += sizeof(data);
            } else { buf.overflowed = true; }
            break;
        }
        case FT_BUILDQUEUE: {
            DWORD size = sizeof(uiBuildQueue_t) + sizeof(uiBuildQueueItem_t) * frame->BuildQueue.NumQueue;
            uiBuildQueue_t *data = (uiBuildQueue_t *)(buf.data + buf.cursize);
            if (buf.cursize + size > buf.maxsize) { buf.overflowed = true; break; }
            data->firstitem = 0;
            data->buildtimer = 0;
            data->itemoffset = 0.0f;
            data->numitems = frame->BuildQueue.NumQueue;
            memcpy(data->items, frame->BuildQueue.Queue, sizeof(uiBuildQueueItem_t) * data->numitems);
            buf.cursize += size;
            break;
        }
        case FT_MULTISELECT: {
            DWORD size = sizeof(uiMultiselect_t) + sizeof(uiMultiselectItem_t) * frame->Multiselect.NumItems;
            uiMultiselect_t *data = (uiMultiselect_t *)(buf.data + buf.cursize);
            if (buf.cursize + size > buf.maxsize) { buf.overflowed = true; break; }
            data->hp_bar = frame->Multiselect.HpBar;
            data->mana_bar = frame->Multiselect.ManaBar;
            data->offset = MAKE(VECTOR2, 0.031f, 0.050f);
            data->numcolumns = 6;
            data->numitems = frame->Multiselect.NumItems;
            memcpy(data->items, frame->Multiselect.Items, sizeof(uiMultiselectItem_t) * data->numitems);
            buf.cursize += size;
            break;
        }
        case FT_MODEL:
        case FT_SPRITE:
        case FT_PORTRAIT:
            out->tex.index = frame->Portrait.model;
            break;
        default:
            break;
    }

    if (buf.overflowed) {
        out->buffer.size = 0;
        out->buffer.data = NULL;
        return false;
    }
    out->buffer.size = buf.cursize;
    out->buffer.data = buf.data;
    out->flags.type = frame->Type;
    return true;
}

static void UI_WriteBuiltFrame(LPCFRAMEDEF frame, FLOAT value, BOOL override_value) {
    UINAME textbuf;
    uiFrame_t tmp;
    BYTE typedata[256] = { 0 };

    if (!UI_BuildFrameForWrite(frame, &tmp, typedata, sizeof(typedata), textbuf, sizeof(textbuf))) {
        return;
    }
    if (override_value) tmp.value = value;
    if (ui_window_writing) {
        tmp.text = (LPCSTR)(uintptr_t)UI_WindowTextOffset(tmp.text);
        tmp.tooltip = (LPCSTR)(uintptr_t)UI_WindowTextOffset(tmp.tooltip);
        tmp.onclick = (LPCSTR)(uintptr_t)UI_WindowTextOffset(tmp.onclick);
    }
    /* FDF frames and proxy frames share one wire namespace; previously the first proxy overwrote frame 1. */
    ui_next_frame_number = UI_NextProxyFrameNumber(ui_next_frame_number, tmp.number);
    /* Window strings are arena offsets; the normal codec dereferenced offset 1 as an inline string. */
    gi.Write(ui_window_writing ? PF_UIWINDOWFRAME : PF_UIFRAME, &tmp);
}

void UI_WriteFrame(LPCFRAMEDEF frame) {
    UI_WriteBuiltFrame(frame, 0.0f, false);
}

void UI_WriteFrameValue(LPCFRAMEDEF frame, FLOAT value) {
    UI_WriteBuiltFrame(frame, MAX(0.0f, MIN(value, 1.0f)), true);
}

DWORD UI_GetWrittenFrameNumber(LPCFRAMEDEF frame) {
    return FindFrameNumber(frame, 0);
}

void UI_WriteFrameWithChildren(LPCFRAMEDEF frame, LPCFRAMEDEF parent) {
    UI_PrepareTextAreaScrollBar(frame);
    if (parent) {
        LPCFRAMEDEF oldparent = frame->Parent;
        ((LPFRAMEDEF)frame)->Parent = parent;
        UI_WriteFrame(frame);
        ((LPFRAMEDEF)frame)->Parent = oldparent;
    } else {
        UI_WriteFrame(frame);
    }
    FOR_LOOP(i, MAX_UI_CLASSES) {
        LPCFRAMEDEF it = frames + i;
        if (it->Parent == frame && !it->hidden &&
            !UI_IsEmbeddedControlArtPart(frame, it)) {
            UI_WriteFrameWithChildren(it, NULL);
        }
    }
}

void UI_WriteFrameWithChildrenWithTriggers(LPEDICT ent, LPCFRAMEDEF frame, LPCFRAMEDEF parent, uiTrigger_t const *triggers) {
    UI_PrepareTextAreaScrollBar(frame);
    if (parent) {
        LPCFRAMEDEF oldparent = frame->Parent;
        ((LPFRAMEDEF)frame)->Parent = parent;
        UI_WriteFrame(frame);
        ((LPFRAMEDEF)frame)->Parent = oldparent;
    } else {
        UI_WriteFrame(frame);
    }
    for (uiTrigger_t const *t = triggers; t->name; t++) {
        if (!strcmp(t->name, frame->Name)) {
            t->callback(ent, (LPFRAMEDEF)frame);
        }
    }
    FOR_LOOP(i, MAX_UI_CLASSES) {
        LPCFRAMEDEF it = frames + i;
        if (it->Parent == frame && !it->hidden &&
            !UI_IsEmbeddedControlArtPart(frame, it)) {
            UI_WriteFrameWithChildrenWithTriggers(ent, it, NULL, triggers);
        }
    }
}

void UI_WriteLayout(LPEDICT ent, LPCFRAMEDEF root, DWORD layer) {
    UI_WriteStart(layer);
    UI_WriteFrameWithChildren(root, NULL);
    UI_WriteEnd(ent);
}

void UI_WriteWindow(LPEDICT ent, LPCFRAMEDEF root, uiWindowDef_t const *def) {
    UI_WriteWindowStart(def);
    UI_WriteFrameWithChildren(root, NULL);
    UI_WriteWindowEnd(ent);
}

void UI_WriteWithTriggers(LPEDICT ent, LPCFRAMEDEF root, DWORD layer, uiTrigger_t const *triggers) {
    UI_WriteStart(layer);
    UI_WriteFrameWithChildrenWithTriggers(ent, root, NULL, triggers);
    UI_WriteEnd(ent);
}

/* Stubbed UI framework functions */
void UI_Init(void) {}
void UI_ClearCreateGameSlots(void) {}
void UI_AddCreateGameSlot(DWORD slot, LPCSTR name, LPCSTR race, LPCSTR color, DWORD team) {
    (void)slot; (void)name; (void)race; (void)color; (void)team;
}

#define BZ_HOST_HIDDEN __attribute__((visibility("hidden")))

void UI_ResetHud(void) {
    memset(&hud, 0, sizeof(hud));
    UI_ClearTemplates();
}

void UI_LoadHud(void) {
    /* Parse every in-game panel once per level, after SV_Map has a live
     * CS_IMAGES table. Write paths then serialize hud.* without EnsureFDF. */
    UI_LoadHudConsole();
    UI_LoadHudInfoPanel();
    UI_LoadHudQuests();
    UI_LoadHudLog();
    UI_LoadHudMenu();
    UI_LoadHudAllies();
    UI_LoadHudGameResult();
    UI_LoadHudCinematic();
    UI_LoadHudMessage();
}

/* FDF host services — game module implementations using gi */
BZ_HOST_HIDDEN HANDLE UI_FdfAlloc(long size) { return gi.MemAlloc(size); }
BZ_HOST_HIDDEN void UI_FdfFree(HANDLE ptr) { gi.MemFree(ptr); }
BZ_HOST_HIDDEN int UI_FdfReadFile(LPCSTR name, HANDLE *out) {
    DWORD size = 0;
    *out = gi.ReadFile(name, &size);
    return *out ? (int)size : -1;
}
BZ_HOST_HIDDEN void UI_FdfFreeFile(HANDLE buf) { gi.MemFree(buf); }

/* Game module doesn't handle UI events or themes — stub these */
BZ_HOST_HIDDEN void UI_WireFrameTypeFunctions(LPFRAMEDEF frame) { (void)frame; }
BZ_HOST_HIDDEN void UI_ClearTheme(void) {}

/* Game module doesn't load 3D models for UI — stub */
BZ_HOST_HIDDEN DWORD UI_LoadModel(LPCSTR file, BOOL decorate) { (void)file; (void)decorate; return 0; }
