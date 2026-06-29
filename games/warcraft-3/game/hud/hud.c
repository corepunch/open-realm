/*
 * hud.c — FDF → uiframe serialization bridge.
 *
 * Converts parsed FRAMEDEF trees (from stb_fdf.h) into uiFrame_t wire
 * format for transmission via svc_layout.  These functions need gi for
 * network writes, so they live in the game module rather than ui_fdf.c.
 *
 * HUD panels have been split into sibling files in game/hud/:
 *   hud_write.c     — frame-write primitives, theme, text formatting
 *   hud_console.c   — ConsoleUI backdrop, minimap, resource bar
 *   hud_commands.c  — command buttons, build queue, inventory
 *   hud_infopanel.c — info panel, multiselect, per-frame update stubs
 *   hud_quests.c    — quest dialog
 *   hud_cinematic.c — cinematic layer, interface toggle, message overlay
 */

#include "hud_local.h"

/* frames[] is defined in fdf_parser.c (common/) */

#define MAX_FRAMES_WRITE 1024
static LPCFRAMEDEF framesWritten[MAX_FRAMES_WRITE];
static LPCFRAMEDEF *frameptr;

static void UI_ResetFrameWriteList(void) {
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
    tooltip[0] = '\0';
    if (src->Tip || src->Ubertip) {
        snprintf(tooltip, sizeof(tooltip), "%s\n%s",
                 src->Tip ? src->Tip : "",
                 src->Ubertip ? src->Ubertip : "");
    }
    CONVERT_UV(dest->tex.coord, src->Texture.TexCoord);
    dest->number = FindFrameNumber(src, 0);
    dest->parent = FindFrameNumber(src->Parent, 0);
    dest->color = src->Color;
    dest->size.width = src->Width;
    dest->size.height = src->Height;
    dest->tex.index = src->Texture.Image;
    dest->tex.index2 = src->Texture.Image2;
    dest->flags.type = src->Type;
    dest->flags.alphaMode = src->AlphaMode;
    dest->textLength = src->TextLength;
    dest->stat = src->Stat;
    dest->text = src->Text;
    dest->tooltip = tooltip[0] ? tooltip : NULL;
    dest->onclick = src->OnClick;
}

static uiBackdrop_t MakeBackdrop(LPCFRAMEDEF frame) {
    if (!frame) return (uiBackdrop_t){ 0 };
    return MAKE(uiBackdrop_t,
        .CornerFlags = frame->Backdrop.CornerFlags,
        .TileBackground = frame->Backdrop.TileBackground,
        .Background = frame->Backdrop.Background,
        .CornerSize = frame->Backdrop.CornerSize,
        .BackgroundSize = frame->Backdrop.BackgroundSize,
        .BackgroundInsets = {
            frame->Backdrop.BackgroundInsets[0],
            frame->Backdrop.BackgroundInsets[1],
            frame->Backdrop.BackgroundInsets[2],
            frame->Backdrop.BackgroundInsets[3],
        },
        .EdgeFile = frame->Backdrop.EdgeFile,
        .BlendAll = frame->Backdrop.BlendAll,
        .Mirrored = frame->Backdrop.Mirrored,
    );
}

/* TODO: MakeHighlight unused — reserved for future highlight frame support */

static uiLabel_t MakeLabel(LPCFRAMEDEF frame) {
    return MAKE(uiLabel_t,
        .textalignx = frame->Font.Justification.Horizontal,
        .textaligny = frame->Font.Justification.Vertical,
        .offsetx = frame->Font.Justification.Offset.x,
        .offsety = frame->Font.Justification.Offset.y,
        .font = frame->Font.Index,
    );
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
        case FT_TOOLTIPTEXT: {
            uiTooltip_t data = { .background = MakeBackdrop(frame), .text = MakeLabel(frame) };
            if (buf.cursize + sizeof(data) <= buf.maxsize) {
                memcpy(buf.data + buf.cursize, &data, sizeof(data));
                buf.cursize += sizeof(data);
            } else { buf.overflowed = true; }
            break;
        }
        case FT_STRING:
        case FT_TEXT: {
            uiLabel_t data = MakeLabel(frame);
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
            uiTextArea_t data = { .font = frame->Font.Index, .inset = frame->TextArea.Inset };
            if (buf.cursize + sizeof(data) <= buf.maxsize) {
                memcpy(buf.data + buf.cursize, &data, sizeof(data));
                buf.cursize += sizeof(data);
            } else { buf.overflowed = true; }
            break;
        }
        case FT_EDITBOX:
        case FT_GLUEEDITBOX:
        case FT_SLASHCHATBOX: {
            LPFRAMEDEF Backdrop = UI_FindFrameNear(frame, frame->Control.Backdrop.Normal);
            LPFRAMEDEF TextFrame = UI_FindFrameNear(frame, frame->Edit.TextFrame);
            COLOR32 text_color = frame->Edit.TextColor.a ? frame->Edit.TextColor :
                                 TextFrame && TextFrame->Font.Color.a ? TextFrame->Font.Color :
                                 frame->Font.Color;
            COLOR32 cursor_color = frame->Edit.CursorColor.a ? frame->Edit.CursorColor : COLOR32_WHITE;
            uiEditBox_t data = {
                .background = MakeBackdrop(Backdrop),
                .font = TextFrame ? TextFrame->Font.Index : frame->Font.Index,
                .borderSize = frame->Edit.BorderSize,
                .textColor = text_color.a ? text_color : COLOR32_WHITE,
                .cursorColor = cursor_color,
                .maxChars = frame->Edit.MaxChars,
            };
            if (frame->Edit.Text[0]) out->text = frame->Edit.Text;
            else if (!out->text || !*out->text) out->text = "";
            if (buf.cursize + sizeof(data) <= buf.maxsize) {
                memcpy(buf.data + buf.cursize, &data, sizeof(data));
                buf.cursize += sizeof(data);
            } else { buf.overflowed = true; }
            break;
        }
        case FT_LISTBOX: {
            LPFRAMEDEF Backdrop = UI_FindFrameNear(frame, frame->Control.Backdrop.Normal);
            uiListBox_t data = {
                .background = MakeBackdrop(Backdrop),
                .text = MakeLabel(frame),
                .border = frame->ListBox.Border,
                .itemHeight = frame->Menu.Item.Height,
                .selectedIndex = -1,
            };
            snprintf(data.id, sizeof(data.id), "%s", frame->Name);
            snprintf(data.fetchCommand, sizeof(data.fetchCommand), "%s", frame->ListBox.FetchCommand);
            if (buf.cursize + sizeof(data) <= buf.maxsize) {
                memcpy(buf.data + buf.cursize, &data, sizeof(data));
                buf.cursize += sizeof(data);
            } else { buf.overflowed = true; }
            break;
        }
        case FT_SCROLLBAR: {
            LPFRAMEDEF incButton = UI_FindFrameNear(frame, frame->Slider.IncButtonFrame);
            LPFRAMEDEF decButton = UI_FindFrameNear(frame, frame->Slider.DecButtonFrame);
            LPFRAMEDEF thumbButton = UI_FindFrameNear(frame, frame->Slider.ThumbButtonFrame);
            uiScrollBar_t data = {
                .background = MakeBackdrop(UI_FindFrameNear(frame, frame->Control.Backdrop.Normal)),
                .incButton = MakeBackdrop(incButton),
                .decButton = MakeBackdrop(decButton),
                .thumbButton = MakeBackdrop(thumbButton),
            };
            if (buf.cursize + sizeof(data) <= buf.maxsize) {
                memcpy(buf.data + buf.cursize, &data, sizeof(data));
                buf.cursize += sizeof(data);
            } else { buf.overflowed = true; }
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

void UI_WriteFrame(LPCFRAMEDEF frame) {
    UINAME textbuf;
    uiFrame_t tmp;
    BYTE typedata[256] = { 0 };

    if (!UI_BuildFrameForWrite(frame, &tmp, typedata, sizeof(typedata), textbuf, sizeof(textbuf))) {
        return;
    }
    gi.Write(PF_UIFRAME, &tmp);
}

void UI_WriteFrameWithChildren(LPCFRAMEDEF frame, LPCFRAMEDEF parent) {
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
        if (it->Parent == frame && !it->hidden) {
            UI_WriteFrameWithChildren(it, NULL);
        }
    }
}

void UI_WriteFrameWithChildrenWithTriggers(LPEDICT ent, LPCFRAMEDEF frame, LPCFRAMEDEF parent, uiTrigger_t const *triggers) {
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
        if (it->Parent == frame && !it->hidden) {
            UI_WriteFrameWithChildrenWithTriggers(ent, it, NULL, triggers);
        }
    }
}

void UI_WriteLayout(LPEDICT ent, LPCFRAMEDEF root, DWORD layer) {
    UI_ResetFrameWriteList();
    UI_WriteStart(layer);
    UI_WriteFrameWithChildren(root, NULL);
    gi.Write(PF_LONG, &(LONG){0});
    gi.unicast(ent);
}

void UI_WriteWithTriggers(LPEDICT ent, LPCFRAMEDEF root, DWORD layer, uiTrigger_t const *triggers) {
    UI_ResetFrameWriteList();
    UI_WriteStart(layer);
    UI_WriteFrameWithChildrenWithTriggers(ent, root, NULL, triggers);
    gi.Write(PF_LONG, &(LONG){0});
    gi.unicast(ent);
}

/* Stubbed UI framework functions */
void UI_Init(void) {}
void UI_ClearCreateGameSlots(void) {}
void UI_AddCreateGameSlot(DWORD slot, LPCSTR name, LPCSTR race, LPCSTR color, DWORD team) {
    (void)slot; (void)name; (void)race; (void)color; (void)team;
}

/* FDF host services — game module implementations using gi */
HANDLE UI_FdfAlloc(long size) { return gi.MemAlloc(size); }
void UI_FdfFree(HANDLE ptr) { gi.MemFree(ptr); }
DWORD UI_FdfFontIndex(LPCSTR name, DWORD size) { return gi.FontIndex(name, size); }
int UI_FdfReadFile(LPCSTR name, HANDLE *out) {
    DWORD size = 0;
    *out = gi.ReadFile(name, &size);
    return *out ? (int)size : -1;
}
void UI_FdfFreeFile(HANDLE buf) { gi.MemFree(buf); }

/* Game module doesn't handle UI events or themes — stub these */
void UI_WireFrameTypeFunctions(LPFRAMEDEF frame) { (void)frame; }
void UI_ClearTheme(void) {}
void UI_ClearTextures(void) {}

/* Game module doesn't load 3D models for UI — stub */
DWORD UI_LoadModel(LPCSTR file, BOOL decorate) { (void)file; (void)decorate; return 0; }
