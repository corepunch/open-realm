/*
 * fdf_parser.c — FDF parser extracted from ui_fdf.c.
 *
 * Parses FDF text buffers and registers frameDef_t templates in the global
 * frame registry. Uses UI_Fdf* host abstraction for memory, fonts, and file I/O.
 */

#include <stdlib.h>
#include <ctype.h>
#ifndef _WIN32
#include <strings.h>
#endif
#include "common/common.h"
#include "common/stb_fdf.h"

FRAMEDEF frames[MAX_UI_CLASSES] = { 0 };

extern void UI_WireFrameTypeFunctions(LPFRAMEDEF frame);
extern void UI_ClearTheme(void);

extern LPCSTR parse_token(LPPARSER p);
extern LPCSTR parse_segment(LPPARSER p);
extern LPCSTR parse_segment2(LPPARSER p);

#define UINAME_FMT "\"%79[^\"]\""
#define PATHSTR_FMT "\"%255[^\"]\""

#define FDF_F(x, type) { #x,((uint8_t *)&((FRAMEDEF *)0)->x - (uint8_t *)NULL), FDF_Parse##type }

LPCSTR FrameType[] = {
    "",
    "BACKDROP",
    "BUTTON",
    "CHATDISPLAY",
    "CHECKBOX",
    "CONTROL",
    "DIALOG",
    "EDITBOX",
    "FRAME",
    "GLUEBUTTON",
    "GLUECHECKBOX",
    "GLUEEDITBOX",
    "GLUEPOPUPMENU",
    "GLUETEXTBUTTON",
    "HIGHLIGHT",
    "LISTBOX",
    "MENU",
    "MODEL",
    "POPUPMENU",
    "SCROLLBAR",
    "SIMPLEBUTTON",
    "SIMPLECHECKBOX",
    "SIMPLEFRAME",
    "SIMPLESTATUSBAR",
    "SLASHCHATBOX",
    "SLIDER",
    "SPRITE",
    "TEXT",
    "TEXTAREA",
    "TEXTBUTTON",
    "TIMERTEXT",
    "TEXTURE",
    "STRING",
    "LAYER",
    "SCREEN",
    "COMMANDBUTTON",
    "PORTRAIT",
    "STRINGLIST",
    "BUILDQUEUE",
    "MULTISELECT",
    "TOOLTIPTEXT",
    NULL
};
LPCSTR HighlightType[] = {
    "FILETEXTURE",
    NULL
};
LPCSTR AlphaMode[] = {
    "NONE",
    "ALPHAKEY",
    "BLEND",
    "ADD",
    "MODULATE",
    "MODULATE2X",
    NULL
};
LPCSTR FontJustificationH[] = {
    "JUSTIFYCENTER",
    "JUSTIFYLEFT",
    "JUSTIFYRIGHT",
    NULL
};
LPCSTR FontJustificationV[] = {
    "JUSTIFYMIDDLE",
    "JUSTIFYTOP",
    "JUSTIFYBOTTOM",
    NULL
};
LPCSTR FramePointType[] = {
    "TOPLEFT",
    "TOP",
    "TOPRIGHT",
    "<UNUSED>",
    "LEFT",
    "CENTER",
    "RIGHT",
    "<UNUSED>",
    "BOTTOMLEFT",
    "BOTTOM",
    "BOTTOMRIGHT",
    "<UNUSED>",
    NULL
};
LPCSTR FontFlags[] = {
    "FIXEDSIZE",
    "PASSWORDFIELD",
    NULL
};
LPCSTR ControlStyle[] = {
    "AUTOTRACK",
    "HIGHLIGHTONFOCUS",
    "HIGHLIGHTONMOUSEOVER",
    NULL
};
LPCSTR CornerFlags[] = {
    "UL",
    "T",
    "UR",
    "L",
    "-",
    "R",
    "BL",
    "B",
    "BR",
    NULL
};

static PATHSTR ui_loaded_fdfs[128] = { 0 };
static DWORD ui_num_loaded_fdfs = 0;

void FDF_ParseFrame(LPPARSER p, LPFRAMEDEF frame);
static char *UI_Trim(char *text);
static void UI_CopyDisplayString(char *out, size_t out_size, LPCSTR in);
static void UI_SetFrameDisplayString(LPFRAMEDEF frame, LPCSTR text);
static void UI_FixCopiedFrameTextPointer(LPFRAMEDEF frame, LPCFRAMEDEF source);
static void UI_FreeFrameDynamicText(LPFRAMEDEF frame);
static void UI_RemoveBom(LPSTR buffer);
static void UI_CloneTemplateChildren(LPCFRAMEDEF source, LPFRAMEDEF parent);

void UI_ClearTemplates(void) {
    FOR_LOOP(i, MAX_UI_CLASSES) {
        UI_FreeFrameDynamicText(&frames[i]);
    }
    memset(frames, 0, sizeof(frames));
    memset(ui_loaded_fdfs, 0, sizeof(ui_loaded_fdfs));
    ui_num_loaded_fdfs = 0;
    UI_ClearTheme();
}

LPFRAMEDEF UI_Spawn(FRAMETYPE type, LPFRAMEDEF parent) {
    FOR_LOOP(i, MAX_UI_CLASSES) {
        if (i==0) continue;
        LPFRAMEDEF frame = &frames[i];
        if (!frame->inuse) {
            UI_InitFrame(frame, type);
            UI_WireFrameTypeFunctions(frame);
            frame->Parent = parent;
            return frame;
        }
    }
    return NULL;
}

typedef struct {
    LPCSTR name;
    DWORD fofs;
    void (*func)(LPCSTR, LPFRAMEDEF frame, void *);
} fdf_parseArg_t;

typedef struct {
    LPCSTR name;
    fdf_parseArg_t args[16];
    void (*func)(LPPARSER , LPFRAMEDEF);
} fdf_parseItem_t;

typedef struct {
    LPCSTR name;
    void (*func)(LPPARSER , LPFRAMEDEF);
} fdf_parse_class_t;

int FDF_ParseEnumString(LPCSTR token, LPCSTR const *values) {
    for (int i = 0; *values; i++, values++) {
        if (!strcmp(token, *values)) {
            return i;
        }
    }
    return -1;
}

static char *UI_Trim(char *text) {
    text += strspn(text, " \t\r\n");
    for (char *end = text + strlen(text); end > text && isspace((unsigned char)end[-1]); )
        *--end = '\0';
    return text;
}

static void UI_RemoveBom(LPSTR buffer) {
    static unsigned char const utf8_bom[] = { 0xEF, 0xBB, 0xBF };
    size_t length;

    if (!buffer) {
        return;
    }
    length = strlen(buffer);
    if (length >= sizeof(utf8_bom) &&
        memcmp((unsigned char *)buffer, utf8_bom, sizeof(utf8_bom)) == 0) {
        memmove(buffer, buffer + sizeof(utf8_bom), length - sizeof(utf8_bom) + 1);
    }
}

static void UI_CopyDisplayString(char *out, size_t out_size, LPCSTR in) {
    if (!out || out_size == 0) {
        return;
    }
    if (!in) {
        out[0] = '\0';
        return;
    }
    snprintf(out, out_size, "%s", in);
}

static void UI_FreeFrameDynamicText(LPFRAMEDEF frame) {
    if (frame && frame->DynamicText) {
        UI_FdfFree(frame->DynamicText);
        frame->DynamicText = NULL;
        frame->DynamicTextCapacity = 0;
    }
}

static void UI_SetFrameDisplayString(LPFRAMEDEF frame, LPCSTR text) {
    size_t len;

    if (!frame) {
        return;
    }

    UI_FreeFrameDynamicText(frame);

    if (!text) {
        frame->TextStorage[0] = '\0';
        frame->Text = frame->TextStorage;
        return;
    }

    len = strlen(text);
    if (len < sizeof(frame->TextStorage)) {
        UI_CopyDisplayString(frame->TextStorage, sizeof(frame->TextStorage), text);
        frame->Text = frame->TextStorage;
        return;
    }

    frame->DynamicText = UI_FdfAlloc((long)len + 1);
    if (frame->DynamicText) {
        memcpy(frame->DynamicText, text, len + 1);
        frame->DynamicTextCapacity = (DWORD)(len + 1);
        frame->Text = frame->DynamicText;
    } else {
        UI_CopyDisplayString(frame->TextStorage, sizeof(frame->TextStorage), text);
        frame->Text = frame->TextStorage;
    }
}

static void UI_FixCopiedFrameTextPointer(LPFRAMEDEF frame, LPCFRAMEDEF source) {
    LPCSTR copied_text;

    if (!frame || !source || !source->Text) {
        return;
    }

    copied_text = source->Text;
    frame->DynamicText = NULL;
    frame->DynamicTextCapacity = 0;

    if (copied_text == source->TextStorage) {
        frame->Text = frame->TextStorage;
    } else if (copied_text == source->DynamicText) {
        UI_SetFrameDisplayString(frame, copied_text);
    } else {
        frame->Text = copied_text;
    }
}

#define FDF_MAKE_PARSER(TYPE) \
void FDF_Parse##TYPE(LPCSTR token, LPFRAMEDEF frame, void *out)

#define FDF_MAKE_PARSERCALL(TYPE) \
void TYPE(LPPARSER parser, LPFRAMEDEF frame)

#define FDF_MAKE_ENUMPARSER(TYPE) \
FDF_MAKE_PARSER(TYPE) { \
    UINAME fmt; \
    if (*token == '"') sscanf(token, UINAME_FMT, fmt); \
    else strcpy(fmt, token); \
    *((DWORD *)out) = FDF_ParseEnumString(fmt, TYPE); \
}

#define FDF_MAKE_FLAGSPARSER(TYPE) \
FDF_MAKE_PARSER(TYPE) { \
    PATHSTR b; \
    sscanf(token, PATHSTR_FMT, b); \
    for (LPSTR s = b; *s; s++) *s = *s == '|' ? ',' : *s; \
    PARSE_LIST(b, flag, parse_segment) { \
        *((DWORD *)out) |= 1 << FDF_ParseEnumString(flag, TYPE); \
    } \
}

static void FDF_ParseFloatList(LPCSTR token, FLOAT *values, DWORD count) {
    LPCSTR p = token;
    for (DWORD i = 0; i < count; i++) {
        char *endptr = NULL;
        values[i] = strtof(p, &endptr);
        if (endptr == p) {
            break;
        }
        p = endptr;
        while (*p == 'f' || *p == 'F' || *p == ',' || isspace(*p)) {
            p++;
        }
    }
}

FDF_MAKE_PARSER(Float) { *((FLOAT *)out) = atof(token); }
FDF_MAKE_PARSER(Integer) { *((LONG *)out) = atoi(token); }
FDF_MAKE_PARSER(Vector2) { FDF_ParseFloatList(token, out, 2); }
FDF_MAKE_PARSER(Vector3) { FDF_ParseFloatList(token, out, 3); }
FDF_MAKE_PARSER(Vector4) { FDF_ParseFloatList(token, out, 4); }
FDF_MAKE_PARSER(Color) {
    FLOAT values[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    FDF_ParseFloatList(token, values, 4);
    ((COLOR32 *)out)->r = values[0] * 0xff;
    ((COLOR32 *)out)->g = values[1] * 0xff;
    ((COLOR32 *)out)->b = values[2] * 0xff;
    ((COLOR32 *)out)->a = values[3] * 0xff;
}
FDF_MAKE_PARSER(FramePtr) {
    *(LPCFRAMEDEF *)out = NULL;
    UINAME name = {0};
    sscanf(token, UINAME_FMT, name);
    FOR_LOOP(i, MAX_UI_CLASSES) {
        if (labs(frames+i-frame) > labs(*((LPCFRAMEDEF *)out)-frame))
            return;
        if (!strcmp(frames[i].Name, name)) {
            *(LPCFRAMEDEF *)out = frames+i;
        }
    }
}
FDF_MAKE_PARSER(Name) {
    memset(out, 0, sizeof(UINAME));
    sscanf(token, UINAME_FMT, (LPSTR)out);
}
FDF_MAKE_PARSER(ButtonText) {
    BUTTONTEXT *bt = out;
    memset(bt, 0, sizeof(BUTTONTEXT));
    assert(sscanf(token, UINAME_FMT " " UINAME_FMT, bt->frame, bt->text) == 2);
}
FDF_MAKE_PARSER(Text) {
    UINAME key = { 0 };
    sscanf(token, UINAME_FMT, key);
    LPCSTR str = UI_GetString(key);
    if (frame && out == frame->TextStorage) {
        UI_SetFrameDisplayString(frame, str);
    } else {
        memset(out, 0, sizeof(UINAME));
        UI_CopyDisplayString(out, sizeof(UINAME), str);
        frame->Text = out;
    }
}

typedef struct stringListItem_s {
    struct stringListItem_s *next;
    UINAME name;
    LPSTR value;
} stringListItem_t;

stringListItem_t *strings = NULL;

FDF_MAKE_PARSER(StringListItem) {
    char value[1024];
    char *start;
    char *end;

    snprintf(value, sizeof(value), "%s", token ? token : "");
    start = UI_Trim(value);
    if (*start == '"') {
        start++;
    }
    end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    if (end > start && end[-1] == '"') {
        *--end = '\0';
    }
    strings->value = strdup(start);
}

FDF_MAKE_ENUMPARSER(AlphaMode);
FDF_MAKE_ENUMPARSER(FontJustificationH);
FDF_MAKE_ENUMPARSER(FontJustificationV);
FDF_MAKE_ENUMPARSER(HighlightType);
FDF_MAKE_ENUMPARSER(FontFlags);
FDF_MAKE_ENUMPARSER(FramePointType);
FDF_MAKE_FLAGSPARSER(CornerFlags);
FDF_MAKE_FLAGSPARSER(ControlStyle);

FDF_MAKE_PARSER(TextureFile) {
    PATHSTR path = { 0 };
    DWORD image = 0;
    sscanf(token, PATHSTR_FMT, path);
    image = path[0] ? UI_LoadTexture(path, true) : 0;
    *((DWORD *)out) = image;
#ifdef DIAG_OUTPUT
    if (path[0] && image == 0) {
        DIAGF("FDF_ParseTextureFile: frame=%s token=%s resolved image=0\n", frame->Name, path);
    }
#endif
}

FDF_MAKE_PARSER(ModelPath) {
    PATHSTR path = { 0 };
    BOOL decorate = frame->DecorateFileNames | (frame->Parent ? frame->Parent->DecorateFileNames : false);
    DWORD modelIndex = 0;
    sscanf(token, PATHSTR_FMT, path);
    modelIndex = UI_LoadModel(path, decorate);
    *((DWORD *)out) = modelIndex;
#ifdef DIAG_OUTPUT
    LPCSTR model = decorate ? Theme_String(path, "Default") : path;
    if (decorate && path[0] && !strcmp(model, path)) {
        DIAGF("FDF_ParseModelPath: unresolved skin key frame=%s token=%s\n", frame->Name, path);
    }
    if (path[0] && modelIndex == 0) {
        DIAGF("FDF_ParseModelPath: frame=%s token=%s resolved=%s modelIndex=0\n", frame->Name, path, model);
    }
#endif
}

FDF_MAKE_PARSERCALL(Font) {
    LPCSTR file = Theme_String(frame->Font.Name, "Default");
    frame->Font.Index = UI_FdfFontIndex(file, frame->Font.Size * 1000);
}

FDF_MAKE_PARSERCALL(SetPoint) {
    UI_ApplyFramePoints(frame);
}

FDF_MAKE_PARSERCALL(Anchor) {
    frame->SetPoint.type = frame->Anchor.corner;
    frame->SetPoint.target = frame->Anchor.corner;
    frame->SetPoint.relativeTo = frame->Parent;
    frame->SetPoint.x = frame->Anchor.x;
    frame->SetPoint.y = frame->Anchor.y;
    SetPoint(parser, frame);
}

FDF_MAKE_PARSERCALL(DecorateFileNames) {
    frame->DecorateFileNames = true;
}

FDF_MAKE_PARSERCALL(BackdropMirrored) {
    frame->Backdrop.Mirrored = true;
}

FDF_MAKE_PARSERCALL(SliderLayoutHorizontal) {
    frame->Slider.Layout = LAYOUT_HORIZONTAL;
}

FDF_MAKE_PARSERCALL(SliderLayoutVertical) {
    frame->Slider.Layout = LAYOUT_VERTICAL;
}

FDF_MAKE_PARSERCALL(BackdropTileBackground) {
    frame->Backdrop.TileBackground = true;
}

FDF_MAKE_PARSERCALL(BackdropHalfSides) {
}

FDF_MAKE_PARSERCALL(UseActiveContext) {
}

FDF_MAKE_PARSERCALL(TabFocusDefault) {
    frame->Control.TabFocusDefault = true;
}

FDF_MAKE_PARSERCALL(TabFocusPush) {
}

FDF_MAKE_PARSERCALL(BackdropBlendAll) {
    frame->Backdrop.BlendAll = true;
}

FDF_MAKE_PARSERCALL(SetAllPoints) {
    UI_SetAllPoints(frame);
}

FDF_MAKE_PARSERCALL(Texture) {
    FDF_ParseFrame(parser, UI_Spawn(FT_TEXTURE, frame));
}

FDF_MAKE_PARSERCALL(Layer) {
    FDF_ParseFrame(parser, UI_Spawn(FT_LAYER, frame));
}

FDF_MAKE_PARSERCALL(String) {
    FDF_ParseFrame(parser, UI_Spawn(FT_STRING, frame));
}

FDF_MAKE_PARSERCALL(Frame) {
    LPCSTR stype = parse_token(parser);
    FRAMETYPE type = FDF_ParseEnumString(stype, FrameType);
    LPFRAMEDEF current = UI_Spawn(type, frame);
    FDF_ParseFrame(parser, current);
    if (type == FT_POPUPMENU || type == FT_GLUEPOPUPMENU) {
        LPFRAMEDEF title = UI_FindChildFrame(current, current->Popup.TitleFrame);
        LPFRAMEDEF arrow = UI_FindChildFrame(current, current->Popup.ArrowFrame);
        if (title) title->ui_flags |= UIFLAG_PASSTHROUGH;
        if (arrow) arrow->ui_flags |= UIFLAG_PASSTHROUGH;
    }
}

FDF_MAKE_PARSERCALL(IncludeFile) {
    LPCSTR filename = parse_token(parser);
    UI_ParseFDF(filename);
}

FDF_MAKE_PARSERCALL(StringList) {
    FRAMEDEF string_list;
    memset(&string_list, 0, sizeof(FRAMEDEF));
    string_list.Type = FT_STRINGLIST;
    FDF_ParseFrame(parser, &string_list);
}

FDF_MAKE_PARSERCALL(EditHighlightInitial) {
    frame->Edit.HighlightInitial = true;
}

FDF_MAKE_PARSERCALL(EditSetFocus) {
    frame->Edit.Focus = true;
}

FDF_MAKE_PARSERCALL(MenuItem) {
    LPCSTR text = parse_segment2(parser);
    LPCSTR value = parse_segment2(parser);
    UINAME key = { 0 };
    LONG item_value = 0;

    if (!frame || !text || !value) {
        return;
    }
    if (*text == '"') {
        sscanf(text, UINAME_FMT, key);
    } else {
        snprintf(key, sizeof(key), "%s", text);
    }
    item_value = atoi(value);
    UI_MenuAddItem(frame, UI_GetString(key), item_value);
}

#define FDF_F_END { NULL }

static fdf_parse_class_t classes[] = {
    { "Frame", Frame },
    { "Texture", Texture },
    { "String", String },
    { "Layer", Layer },
    { "StringList", StringList },
    { "IncludeFile", IncludeFile },
    FDF_F_END,
};

static fdf_parseItem_t items[] = {
    { "DecorateFileNames", { FDF_F_END }, DecorateFileNames },
    { "BackdropMirrored", { FDF_F_END }, BackdropMirrored },
    { "SetAllPoints", { FDF_F_END }, SetAllPoints },
    { "SetPoint", { FDF_F(SetPoint.type, FramePointType), FDF_F(SetPoint.relativeTo, FramePtr), FDF_F(SetPoint.target, FramePointType), FDF_F(SetPoint.x, Float), FDF_F(SetPoint.y, Float), FDF_F_END }, SetPoint },
    { "UseActiveContext", { FDF_F_END }, UseActiveContext },
    { "ControlShortcutKey", { FDF_F(Control.ShortcutKey, Name), FDF_F_END } },
    { "ControlFocusHighlight", { FDF_F(Control.Backdrop.Focus, Name), FDF_F_END } },
    { "TabFocusDefault", { FDF_F_END }, TabFocusDefault },
    { "TabFocusPush", { FDF_F_END }, TabFocusPush },
    { "TabFocusNext", { FDF_F(Control.TabFocusNext, Name), FDF_F_END } },
    { "DialogBackdrop", { FDF_F(DialogBackdropName, Name), FDF_F_END } },
    { "Width", { FDF_F(Width, Float), FDF_F_END } },
    { "Height", { FDF_F(Height, Float), FDF_F_END } },
    { "File", { FDF_F(Texture.Image, TextureFile), FDF_F_END } },
    { "TexCoord", { FDF_F(Texture.TexCoord.min.x, Float), FDF_F(Texture.TexCoord.max.x, Float), FDF_F(Texture.TexCoord.min.y, Float), FDF_F(Texture.TexCoord.max.y, Float), FDF_F_END } },
    { "BackgroundArt", { FDF_F(Portrait.model, ModelPath), FDF_F_END } },
    { "AlphaMode", { FDF_F(AlphaMode, AlphaMode), FDF_F_END } },
    { "Anchor", { FDF_F(Anchor.corner, FramePointType), FDF_F(Anchor.x, Float), FDF_F(Anchor.y, Float), FDF_F_END }, Anchor },
    { "Font", { FDF_F(Font.Name, Name), FDF_F(Font.Size, Float), FDF_F_END }, Font },
    { "Text", { FDF_F(TextStorage, Text), FDF_F_END } },
    { "TextLength", { FDF_F(TextLength, Integer), FDF_F_END } },
    { "FrameFont", { FDF_F(Font.Name, Name), FDF_F(Font.Size, Float), FDF_F(Font.Unknown, Name), FDF_F_END }, Font },
    { "FontJustificationH", { FDF_F(Font.Justification.Horizontal, FontJustificationH), FDF_F_END } },
    { "FontJustificationV", { FDF_F(Font.Justification.Vertical, FontJustificationV), FDF_F_END } },
    { "FontJustificationOffset", { FDF_F(Font.Justification.Offset, Vector2), FDF_F_END } },
    { "FontFlags", { FDF_F(Font.FontFlags, FontFlags), FDF_F_END } },
    { "FontColor", { FDF_F(Font.Color, Color), FDF_F_END } },
    { "FontHighlightColor", { FDF_F(Font.HighlightColor, Color), FDF_F_END } },
    { "FontDisabledColor", { FDF_F(Font.DisabledColor, Color), FDF_F_END } },
    { "FontShadowColor", { FDF_F(Font.ShadowColor, Color), FDF_F_END } },
    { "FontShadowOffset", { FDF_F(Font.ShadowOffset, Vector2), FDF_F_END } },
    { "BackdropTileBackground", { FDF_F_END }, BackdropTileBackground },
    { "BackdropHalfSides", { FDF_F_END }, BackdropHalfSides },
    { "BackdropBackground", { FDF_F(Backdrop.Background, TextureFile), FDF_F_END } },
    { "BackdropCornerFlags", { FDF_F(Backdrop.CornerFlags, CornerFlags), FDF_F_END } },
    { "BackdropCornerFile", { FDF_F(Backdrop.EdgeFile, TextureFile), FDF_F_END } },
    { "BackdropLeftFile", { FDF_F_END }, BackdropHalfSides },
    { "BackdropRightFile", { FDF_F_END }, BackdropHalfSides },
    { "BackdropTopFile", { FDF_F_END }, BackdropHalfSides },
    { "BackdropBottomFile", { FDF_F_END }, BackdropHalfSides },
    { "BackdropCornerSize", { FDF_F(Backdrop.CornerSize, Float), FDF_F_END } },
    { "BackdropBackgroundSize", { FDF_F(Backdrop.BackgroundSize, Float), FDF_F_END } },
    { "BackdropBackgroundInsets", { FDF_F(Backdrop.BackgroundInsets, Vector4), FDF_F_END } },
    { "BackdropEdgeFile", { FDF_F(Backdrop.EdgeFile, TextureFile), FDF_F_END } },
    { "BackdropBlendAll", { FDF_F_END }, BackdropBlendAll },
    { "HighlightType", { FDF_F(Highlight.Type, HighlightType), FDF_F_END } },
    { "HighlightAlphaFile", { FDF_F(Highlight.AlphaFile, TextureFile), FDF_F_END } },
    { "HighlightAlphaMode", { FDF_F(Highlight.AlphaMode, AlphaMode), FDF_F_END } },
    { "HighlightColor", { FDF_F(Highlight.Color, Color), FDF_F_END } },
    { "ControlStyle", { FDF_F(Control.Style, ControlStyle), FDF_F_END } },
    { "ControlBackdrop", { FDF_F(Control.Backdrop.Normal, Name), FDF_F_END } },
    { "ControlPushedBackdrop", { FDF_F(Control.Backdrop.Pushed, Name), FDF_F_END } },
    { "ControlDisabledBackdrop", { FDF_F(Control.Backdrop.Disabled, Name), FDF_F_END } },
    { "ControlMouseOverHighlight", { FDF_F(Control.Backdrop.MouseOver, Name), FDF_F_END } },
    { "ControlDisabledPushedBackdrop", { FDF_F(Control.Backdrop.DisabledPushed, Name), FDF_F_END } },
    { "SliderInitialValue", { FDF_F(Slider.InitialValue, Float), FDF_F_END } },
    { "SliderLayoutHorizontal", { FDF_F_END }, SliderLayoutHorizontal },
    { "SliderLayoutVertical", { FDF_F_END }, SliderLayoutVertical },
    { "SliderMaxValue", { FDF_F(Slider.MaxValue, Float), FDF_F_END } },
    { "SliderMinValue", { FDF_F(Slider.MinValue, Float), FDF_F_END } },
    { "SliderStepSize", { FDF_F(Slider.StepSize, Float), FDF_F_END } },
    { "ScrollBarIncButtonFrame", { FDF_F(Slider.IncButtonFrame, Name), FDF_F_END } },
    { "ScrollBarDecButtonFrame", { FDF_F(Slider.DecButtonFrame, Name), FDF_F_END } },
    { "SliderThumbButtonFrame", { FDF_F(Slider.ThumbButtonFrame, Name), FDF_F_END } },
    { "ListBoxBorder", { FDF_F(ListBox.Border, Float), FDF_F_END } },
    { "ListBoxScrollBar", { FDF_F(ListBox.ScrollBar, Name), FDF_F_END } },
    { "MenuBorder", { FDF_F(Menu.Border, Float), FDF_F_END } },
    { "MenuItem", { FDF_F_END }, MenuItem },
    { "MenuItemHeight", { FDF_F(Menu.Item.Height, Float), FDF_F_END } },
    { "MenuTextHighlightColor", { FDF_F(Menu.TextHighlightColor, Color), FDF_F_END } },
    { "EditBorderSize", { FDF_F(Edit.BorderSize, Float), FDF_F_END } },
    { "EditCursorColor", { FDF_F(Edit.CursorColor, Color), FDF_F_END } },
    { "EditHighlightColor", { FDF_F(Edit.HighlightColor, Color), FDF_F_END } },
    { "EditHighlightInitial", { FDF_F_END }, EditHighlightInitial },
    { "EditMaxChars", { FDF_F(Edit.MaxChars, Integer), FDF_F_END } },
    { "EditSetFocus", { FDF_F_END }, EditSetFocus },
    { "EditText", { FDF_F(Edit.Text, Text), FDF_F_END } },
    { "EditTextColor", { FDF_F(Edit.TextColor, Color), FDF_F_END } },
    { "EditTextFrame", { FDF_F(Edit.TextFrame, Name), FDF_F_END } },
    { "EditTextOffset", { FDF_F(Edit.TextOffset, Vector2), FDF_F_END } },
    { "PopupButtonInset", { FDF_F(Popup.ButtonInset, Float), FDF_F_END } },
    { "PopupArrowFrame", { FDF_F(Popup.ArrowFrame, Name), FDF_F_END } },
    { "PopupMenuFrame", { FDF_F(Popup.MenuFrame, Name), FDF_F_END } },
    { "PopupTitleFrame", { FDF_F(Popup.TitleFrame, Name), FDF_F_END } },
    { "TextAreaLineHeight", { FDF_F(TextArea.LineHeight, Float), FDF_F_END } },
    { "TextAreaLineGap", { FDF_F(TextArea.LineGap, Float), FDF_F_END } },
    { "TextAreaInset", { FDF_F(TextArea.Inset, Float), FDF_F_END } },
    { "TextAreaScrollBar", { FDF_F(TextArea.ScrollBar, Name), FDF_F_END } },
    { "TextAreaMaxLines", { FDF_F(TextArea.MaxLines, Integer), FDF_F_END } },
    { "ChatDisplayLineHeight", { FDF_F(TextArea.LineHeight, Float), FDF_F_END } },
    { "ChatDisplayBorderSize", { FDF_F(TextArea.Inset, Float), FDF_F_END } },
    { "CheckBoxCheckHighlight", { FDF_F(CheckBox.CheckHighlight, Name), FDF_F_END } },
    { "CheckBoxDisabledCheckHighlight", { FDF_F(CheckBox.DisabledCheckHighlight, Name), FDF_F_END } },
    { "ButtonText", { FDF_F(TextStorage, Text), FDF_F_END } },
    { "ButtonPushedTextOffset", { FDF_F(Button.PushedTextOffset, Vector2), FDF_F_END } },
    { "NormalTexture", { FDF_F(Button.NormalTexture, Name), FDF_F_END } },
    { "PushedTexture", { FDF_F(Button.PushedTexture, Name), FDF_F_END } },
    { "DisabledTexture", { FDF_F(Button.DisabledTexture, Name), FDF_F_END } },
    { "NormalText", { FDF_F(Button.NormalText, ButtonText), FDF_F_END } },
    { "DisabledText", { FDF_F(Button.DisabledText, ButtonText), FDF_F_END } },
    { "HighlightText", { FDF_F(Button.HighlightText, ButtonText), FDF_F_END } },
    { "UseHighlight", { FDF_F(Button.UseHighlight, Name), FDF_F_END } },
    FDF_F_END
};

void parse_item(LPPARSER parser, LPFRAMEDEF frame, fdf_parseItem_t *item) {
    for (fdf_parseArg_t *arg = item->args; arg->name; arg++) {
        LPCSTR token = parse_segment2(parser);
        arg->func(token, frame, (uint8_t *)frame + arg->fofs);
    }
    if (!item->args->name && item->func != MenuItem) {
        parse_segment(parser);
    }
    if (item->func) {
        item->func(parser, frame);
    }
}

void parse_func(LPPARSER parser, LPFRAMEDEF frame) {
    LPCSTR token = NULL;
    while ((token = parse_token(parser)) && *token && (*token != '}')) {
        if (frame->Type == FT_STRINGLIST) {
            static fdf_parseItem_t stringitem = { "", { FDF_F(Name, StringListItem), FDF_F_END } };
            stringListItem_t *str = UI_FdfAlloc(sizeof(stringListItem_t));
            ADD_TO_LIST(str, strings);
            strcpy(str->name, token);
            parse_item(parser, frame, &stringitem);
            goto parse_next;
        } else {
            for (fdf_parseItem_t *it = items; it->name; it++) {
                if (!strcmp(it->name, token)) {
                    parse_item(parser, frame, it);
                    goto parse_next;
                }
            }
            for (fdf_parse_class_t *it = classes; it->name; it++) {
                if (!strcmp(it->name, token)) {
                    it->func(parser, frame);
                    goto parse_next;
                }
            }
        }
        fprintf(stderr, "Can't recognize token '%s'\n", token);
        fprintf(stderr, "parse context: %.120s\n", parser->buffer);
        parser->error = true;
        return;
    parse_next:;
    }
}

LPFRAMEDEF FindFrameTemplate(LPCSTR str) {
    FOR_LOOP(i, MAX_UI_CLASSES) {
        LPFRAMEDEF tmp = frames+i;
        if (!strcmp(tmp->Name, str))
            return tmp;
    }
    return NULL;
}

static BOOL UI_FrameTypesCompatible(FRAMETYPE frameType, FRAMETYPE inheritType) {
    if (frameType == inheritType) {
        return true;
    }
    switch (frameType) {
        case FT_GLUETEXTBUTTON: return inheritType == FT_TEXTBUTTON;
        case FT_GLUEBUTTON: return inheritType == FT_BUTTON;
        case FT_GLUECHECKBOX: return inheritType == FT_CHECKBOX;
        case FT_GLUEEDITBOX: return inheritType == FT_EDITBOX;
        case FT_GLUEPOPUPMENU: return inheritType == FT_POPUPMENU;
        case FT_SLASHCHATBOX: return inheritType == FT_EDITBOX;
        case FT_SIMPLEBUTTON: return inheritType == FT_BUTTON;
        case FT_SIMPLECHECKBOX: return inheritType == FT_CHECKBOX;
        case FT_SIMPLESTATUSBAR: return inheritType == FT_SIMPLESTATUSBAR;
        default: return false;
    }
}

void UI_InheritFrom(LPFRAMEDEF frame, LPCSTR inheritName) {
    LPFRAMEDEF inherit = FindFrameTemplate(inheritName);
    if (inherit && UI_FrameTypesCompatible(frame->Type, inherit->Type)) {
        FRAMEDEF tmp;
        FRAMETYPE requested_type = frame->Type;
        memcpy(&tmp, frame, sizeof(FRAMEDEF));
        UI_FreeFrameDynamicText(frame);
        memcpy(frame, inherit, sizeof(FRAMEDEF));
        UI_FixCopiedFrameTextPointer(frame, inherit);
        memcpy(frame->Name, tmp.Name, sizeof(UINAME));
        frame->Parent = tmp.Parent;
        frame->Type = requested_type;
        frame->AnyPointsSet = false;
    } else if (inherit) {
        fprintf(stderr, "Can't inherit from different type %s\n", inheritName);
    } else {
        fprintf(stderr, "Can't find template %s\n", inheritName);
    }
}

void FDF_ParseFrame(LPPARSER p, LPFRAMEDEF frame) {
    DWORD state = 0;
    LPCSTR tok;
    while ((tok = parse_token(p)) && (*tok != '{')) {
        if (!strcmp(tok, "INHERITS")) {
            LPCSTR inheritName = parse_token(p);
            BOOL with_children = false;
            if (!strcmp(inheritName, "WITHCHILDREN")) {
                with_children = true;
                inheritName = parse_token(p);
            }
            UI_InheritFrom(frame, inheritName);
            if (with_children) {
                UI_CloneTemplateChildren(FindFrameTemplate(inheritName), frame);
            }
            state++;
        } else if (state == 0) {
            strncpy(frame->Name, tok, sizeof(UINAME));
            state++;
        } else {
            parser_error(p);
            return;
        }
    }
    parse_func(p, frame);
}

static LPCFRAMEDEF UI_RemapClonedFrame(LPCFRAMEDEF frame,
                                       LPCFRAMEDEF const *sources,
                                       LPFRAMEDEF const *copies,
                                       DWORD count)
{
    FOR_LOOP(i, count) {
        if (sources[i] == frame) {
            return copies[i];
        }
    }
    return frame;
}

static void UI_RemapClonedPoint(FRAMEPOINT *point,
                                LPCFRAMEDEF const *sources,
                                LPFRAMEDEF const *copies,
                                DWORD count)
{
    if (point && point->relativeTo) {
        point->relativeTo = UI_RemapClonedFrame(point->relativeTo, sources, copies, count);
    }
}

static void UI_RemapClonedFramePointers(LPFRAMEDEF frame,
                                        LPFRAMEDEF parent,
                                        LPCFRAMEDEF const *sources,
                                        LPFRAMEDEF const *copies,
                                        DWORD count)
{
    frame->Parent = UI_RemapClonedFrame(frame->Parent, sources, copies, count);
    if (!frame->Parent && parent) {
        frame->Parent = parent;
    }
    frame->DialogBackdrop = UI_RemapClonedFrame(frame->DialogBackdrop, sources, copies, count);
    frame->SetPoint.relativeTo = UI_RemapClonedFrame(frame->SetPoint.relativeTo, sources, copies, count);
    FOR_LOOP(i, FPP_COUNT) {
        UI_RemapClonedPoint(&frame->Points.x[i], sources, copies, count);
        UI_RemapClonedPoint(&frame->Points.y[i], sources, copies, count);
    }
}

LPFRAMEDEF UI_CloneFrameTree(LPCFRAMEDEF source, LPFRAMEDEF parent) {
    enum { MAX_CLONED_FRAMES = 128 };
    LPCFRAMEDEF sources[MAX_CLONED_FRAMES];
    LPFRAMEDEF copies[MAX_CLONED_FRAMES];
    DWORD const count = source ? UI_CollectFrameTreeRecursiveEx(source, sources, MAX_CLONED_FRAMES, true) : 0;

    if (count == 0 || count > MAX_CLONED_FRAMES) {
        return NULL;
    }

    FOR_LOOP(i, count) {
        copies[i] = UI_Spawn(sources[i]->Type, parent);
        if (!copies[i]) {
            return NULL;
        }
        *copies[i] = *sources[i];
        UI_FixCopiedFrameTextPointer(copies[i], sources[i]);
    }
    FOR_LOOP(i, count) {
        UI_RemapClonedFramePointers(copies[i], parent, sources, copies, count);
    }
    copies[0]->Parent = parent;
    return copies[0];
}

static void UI_CloneTemplateChildren(LPCFRAMEDEF source, LPFRAMEDEF parent) {
    if (!source || !parent) {
        return;
    }

    FOR_LOOP(i, MAX_UI_CLASSES) {
        if (frames[i].Parent == source) {
            UI_CloneFrameTree(frames + i, parent);
        }
    }
}

void FDF_ParseScene(LPPARSER parser) {
    LPCSTR token = NULL;
    LPFRAMEDEF frame = NULL;
    while (*(token = parse_token(parser))) {
        for (fdf_parse_class_t *it = classes; it->name; it++) {
            if (!strcmp(it->name, token)) {
                it->func(parser, frame);
                goto parse_next;
            }
        }
        fprintf(stderr, "Unknown token %s\n", token);
        parser_error(parser);
        return;
    parse_next:;
        while (*parser->buffer == ',') {
            ++parser->buffer;
        }
    }
}

void UI_ParseFDF_Buffer(LPCSTR fileName, LPSTR buffer2) {
    LPSTR buffer = buffer2;
    UI_RemoveBom(buffer);
    PARSER parser = {
        .buffer = buffer,
        .delimiters = ",;{}",
        .eat_quotes = true,
    };
    FDF_ParseScene(&parser);
    if (parser.error) {
        fprintf(stderr, "Failed to parse %s\n", fileName);
    }
}

static BOOL UI_FDFLoaded(LPCSTR fileName) {
    if (!fileName || !*fileName) {
        return false;
    }
    FOR_LOOP(i, ui_num_loaded_fdfs) {
        if (!strcmp(ui_loaded_fdfs[i], fileName)) {
            return true;
        }
    }
    return false;
}

static void UI_MarkFDFLoaded(LPCSTR fileName) {
    if (!fileName || !*fileName || UI_FDFLoaded(fileName)) {
        return;
    }
    if (ui_num_loaded_fdfs >= sizeof(ui_loaded_fdfs) / sizeof(ui_loaded_fdfs[0])) {
        return;
    }
    snprintf(ui_loaded_fdfs[ui_num_loaded_fdfs],
             sizeof(ui_loaded_fdfs[ui_num_loaded_fdfs]),
             "%s",
             fileName);
    ui_num_loaded_fdfs++;
}

BOOL UI_EnsureFDF(LPCSTR fileName) {
    void *buffer = NULL;
    BOOL loaded = false;

    if (UI_FDFLoaded(fileName)) {
        return true;
    }

    int size = UI_FdfReadFile(fileName, &buffer);
    if (size >= 0 && buffer) {
        const BYTE *raw = (const BYTE *)buffer;
        BOOL utf16le = (size >= 2 && raw[0] == 0xFF && raw[1] == 0xFE);
        DWORD text_size = utf16le ? (DWORD)(size / 2) : (DWORD)size;
        LPSTR text = UI_FdfAlloc(text_size + 1);
        if (text) {
            if (utf16le) {
                DWORD out = 0;
                for (int i = 2; i + 1 < size; i += 2) {
                    text[out++] = (char)raw[i];
                }
                text[out] = '\0';
            } else {
                memcpy(text, buffer, (size_t)size);
                text[size] = '\0';
            }
            UI_ParseFDF_Buffer(fileName, text);
            UI_FdfFree(text);
            UI_MarkFDFLoaded(fileName);
            loaded = true;
        }
        UI_FdfFreeFile(buffer);
    }
    return loaded;
}

void UI_ParseFDF(LPCSTR fileName) {
    UI_EnsureFDF(fileName);
}

void UI_SetText(LPFRAMEDEF frame, LPCSTR format, ...) {
    va_list argptr;
    static char text[1024];
    if (!frame || !format) {
        return;
    }
    va_start(argptr, format);
    vsnprintf(text, sizeof(text), format, argptr);
    va_end(argptr);
    UI_SetFrameDisplayString(frame, UI_GetString(text));
}

void UI_SetTextPointer(LPFRAMEDEF frame, LPCSTR text) {
    UI_FreeFrameDynamicText(frame);
    frame->Text = text;
}

LPCSTR UI_GetString(LPCSTR textID) {
    FOR_EACH_LIST(stringListItem_t, it, strings) {
        if (!strcmp(textID, it->name)) {
            return it->value;
        }
    }
    return textID;
}

void UI_SetTexture(LPFRAMEDEF frame, LPCSTR name, BOOL decorate) {
    frame->Texture.Image = UI_LoadTexture(name, decorate);
}

void UI_SetTexture2(LPFRAMEDEF frame, LPCSTR name, BOOL decorate) {
    frame->Texture.Image2 = UI_LoadTexture(name, decorate);
}
