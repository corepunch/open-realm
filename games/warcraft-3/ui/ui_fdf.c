/*
 * ui_fdf.c — FDF parser and programmatic frame-definition API.
 *
 * This file provides two ways to create UI frames without loading an external
 * .fdf file:
 *
 * 1. Parsing FDF text at runtime
 *    UI_ParseFDF_Buffer() accepts a C string written in FDF syntax (see
 *    ui_init.c for an inline example) and registers the resulting frameDef_t
 *    templates in the global frame registry.  The string is mutated during
 *    parsing, so always pass a writable copy (e.g. strdup()).
 *
 * 2. Programmatic C API
 *    Individual frames can be created directly in C without any FDF text.
 *    Initialise a FRAMEDEF struct with UI_InitFrame, set its properties via
 *    the helper functions below, then emit it with UI_WriteFrame:
 *
 *      FRAMEDEF f;
 *      UI_InitFrame(&f, FT_TEXT);
 *      UI_SetPoint(&f, FRAMEPOINT_TOPLEFT, NULL, FRAMEPOINT_TOPLEFT, 0.05, -0.30);
 *      UI_SetText(&f, "Hello, world!");
 *      UI_WriteFrame(&f);
 *
 *    Helper functions:
 *
 *      UI_InitFrame(frame, type)
 *          Zero-initialise and set the frame type (FT_TEXT, FT_BACKDROP,
 *          FT_COMMANDBUTTON, …).
 *
 *      UI_SetPoint(frame, point, relativeTo, targetPoint, x, y)
 *          Anchor one of the frame's nine points to another frame.
 *
 *      UI_SetSize(frame, width, height)
 *          Set explicit width/height dimensions.
 *
 *      UI_SetText(frame, fmt, ...)
 *          Printf-style text assignment for label/text frames.
 *
 *      UI_SetTexture(frame, name, decorate)
 *          Assign a texture by skin-entry name.
 *
 *      UI_SetParent(frame, parent)
 *          Attach the frame to a parent in the hierarchy.
 *
 *      UI_WriteFrame(frame)
 *          Serialize one frame into the outgoing svc_layout message.
 *
 *      UI_WriteFrameWithChildren(frame, parent)
 *          Serialize a frame and its entire sub-tree.
 *
 *    See src/game/hud/ui_log.c for a complete worked example using the
 *    programmatic API.
 */

#include <stdlib.h>
#include <ctype.h>
#ifndef _WIN32
#include <strings.h>
#endif
#include "ui_local.h"
#include "parser.h"

#define UINAME_FMT "\"%79[^\"]\""
#define PATHSTR_FMT "\"%255[^\"]\""

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

/* MAX_UI_CLASSES already defined in ui_local.h */
#define F(x, type) { #x,((uint8_t *)&((FRAMEDEF *)0)->x - (uint8_t *)NULL), Parse##type }
#define ITEM(x) { #x, x }
#define TARGET(TYPE) TYPE *target = (TYPE *)((uint8_t *)frame + arg->fofs);

FRAMEDEF frames[MAX_UI_CLASSES] = { 0 };
static PATHSTR ui_loaded_fdfs[128] = { 0 };
static DWORD ui_num_loaded_fdfs = 0;
static LPCTEXTURE ui_textures[MAX_IMAGES] = { 0 };
static PATHSTR ui_texture_names[MAX_IMAGES] = { 0 };
static PATHSTR ui_texture_keys[MAX_IMAGES] = { 0 };
static BOOL ui_texture_decorated[MAX_IMAGES] = { 0 };
static LPCMODEL ui_models[MAX_MODELS] = { 0 };
static PATHSTR ui_model_names[MAX_MODELS] = { 0 };

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
    memset(ui_textures, 0, sizeof(ui_textures));
    memset(ui_texture_names, 0, sizeof(ui_texture_names));
    memset(ui_texture_keys, 0, sizeof(ui_texture_keys));
    memset(ui_texture_decorated, 0, sizeof(ui_texture_decorated));
    memset(ui_models, 0, sizeof(ui_models));
    memset(ui_model_names, 0, sizeof(ui_model_names));
    memset(ui_loaded_fdfs, 0, sizeof(ui_loaded_fdfs));
    ui_num_loaded_fdfs = 0;
    UI_ClearTheme();
}

void UI_InitFrame(LPFRAMEDEF frame, FRAMETYPE type) {
    memset(frame, 0, sizeof(FRAMEDEF));
    frame->inuse = true;
    frame->Type = type;
    frame->Color = COLOR32_WHITE;
    frame->Text = frame->TextStorage;
    switch (type) {
        case FT_TEXTURE:
        case FT_SIMPLESTATUSBAR:
        case FT_COMMANDBUTTON:
        case FT_BACKDROP:
            frame->Texture.TexCoord.max.x = 1;
            frame->Texture.TexCoord.max.y = 1;
            break;
        case FT_STRING:
        case FT_TEXT:
            frame->Font.Color = COLOR32_WHITE;
            break;
        default:
            break;
    }
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
} parseArg_t;

typedef struct {
    LPCSTR name;
    parseArg_t args[16];
    void (*func)(LPPARSER , LPFRAMEDEF);
} parseItem_t;

typedef struct {
    LPCSTR name;
    void (*func)(LPPARSER , LPFRAMEDEF);
} fdf_parse_class_t;

int ParseEnumString(LPCSTR token, LPCSTR const *values) {
    for (int i = 0; *values; i++, values++) {
        if (!strcmp(token, *values)) {
            return i;
        }
    }
    return -1;
}

int ParseEnum(LPPARSER p, LPCSTR const *values) {
    LPCSTR token = parse_token(p);
    int value = ParseEnumString(token, values);
    if (value == -1) {
        parser_error(p);
    }
    return value;
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
        uiimport.MemFree(frame->DynamicText);
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

    frame->DynamicText = uiimport.MemAlloc((long)len + 1);
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

static BOOL UI_HasKnownTextureExtension(LPCSTR file) {
    LPCSTR dot = file ? strrchr(file, '.') : NULL;

    return dot && (!strcasecmp(dot, ".blp") ||
                   !strcasecmp(dot, ".tga") ||
                   !strcasecmp(dot, ".dds"));
}

LPCSTR EnsureExtension(LPCSTR file, LPCSTR ext) {
    static PATHSTR blp;
    if (!UI_HasKnownTextureExtension(file)) {
        snprintf(blp, sizeof(blp), "%s%s", file, ext);
        return blp;
    } else {
        return file;
    }
}

DWORD UI_LoadTexture(LPCSTR file, BOOL decorate) {
    LPRENDERER renderer;
    LPCSTR resolved;
    DWORD index;

    if (!file || !*file) {
        return 0;
    }

    resolved = decorate ? Theme_String(file, "Default") : file;
    resolved = EnsureExtension(resolved, ".blp");

    FOR_LOOP(i, MAX_IMAGES) {
        if (!ui_texture_names[i][0]) {
            continue;
        }
        if (decorate) {
            if (ui_texture_decorated[i] && !strcmp(ui_texture_keys[i], file)) {
                return i;
            }
        } else if (!ui_texture_decorated[i] && !strcmp(ui_texture_names[i], resolved)) {
            return i;
        }
    }

    index = 0;
    for (DWORD i = 1; i < MAX_IMAGES; i++) {
        if (!ui_texture_names[i][0]) {
            index = i;
            break;
        }
    }
    if (!index || !uiimport.GetRenderer) {
        return 0;
    }

    snprintf(ui_texture_names[index], sizeof(ui_texture_names[index]), "%s", resolved);
    snprintf(ui_texture_keys[index], sizeof(ui_texture_keys[index]), "%s", file);
    ui_texture_decorated[index] = decorate;
    renderer = uiimport.GetRenderer();
    if (renderer && renderer->LoadTexture && !ui_textures[index]) {
        ui_textures[index] = renderer->LoadTexture(resolved);
    }
    return index;
}

LPCSTR UI_TextureName(DWORD index) {
    if (!index || index >= MAX_IMAGES) {
        return NULL;
    }
    return ui_texture_names[index][0] ? ui_texture_names[index] : NULL;
}

LPCTEXTURE UI_GetTexture(DWORD index) {
    LPRENDERER renderer;
    LPCSTR resolved;

    if (!index || index >= MAX_IMAGES) {
        return NULL;
    }
    if (ui_texture_decorated[index] && ui_texture_keys[index][0]) {
        resolved = EnsureExtension(Theme_String(ui_texture_keys[index], "Default"), ".blp");
        if (strcmp(ui_texture_names[index], resolved)) {
            renderer = uiimport.GetRenderer();
            if (renderer && renderer->LoadTexture) {
                if (ui_textures[index] && renderer->ReleaseTexture) {
                    renderer->ReleaseTexture((LPTEXTURE)ui_textures[index]);
                }
                ui_textures[index] = renderer->LoadTexture(resolved);
                snprintf(ui_texture_names[index], sizeof(ui_texture_names[index]), "%s", resolved);
            }
        }
    }
    return ui_textures[index];
}

LPCMODEL UI_GetModel(DWORD index) {
    if (!index || index >= MAX_MODELS) {
        return NULL;
    }
    return ui_models[index];
}

DWORD UI_LoadModel(LPCSTR file, BOOL decorate) {
    LPRENDERER renderer = NULL;
    DWORD modelIndex = 0;
    LPCSTR model = file;

    if (!model || !*model) {
        return 0;
    }

    model = decorate ? Theme_String(model, "Default") : model;
    FOR_LOOP(i, MAX_MODELS) {
        if (ui_model_names[i][0] && !strcmp(ui_model_names[i], model)) {
            return i;
        }
    }

    for (DWORD i = 1; i < MAX_MODELS; i++) {
        if (!ui_model_names[i][0]) {
            modelIndex = i;
            break;
        }
    }
    if (!modelIndex || !uiimport.GetRenderer) {
        return 0;
    }

    snprintf(ui_model_names[modelIndex], sizeof(ui_model_names[modelIndex]), "%s", model);
    renderer = uiimport.GetRenderer();
    if (renderer && renderer->LoadModel && !ui_models[modelIndex]) {
        ui_models[modelIndex] = renderer->LoadModel(model);
    }
    return modelIndex;
}

#define MAKE_PARSER(TYPE) \
void Parse##TYPE(LPCSTR token, LPFRAMEDEF frame, void *out)

#define MAKE_PARSERCALL(TYPE) \
void TYPE(LPPARSER parser, LPFRAMEDEF frame)

#define MAKE_ENUMPARSER(TYPE) \
MAKE_PARSER(TYPE) { \
    UINAME fmt; \
    if (*token == '"') sscanf(token, UINAME_FMT, fmt); \
    else strcpy(fmt, token); \
    *((DWORD *)out) = ParseEnumString(fmt, TYPE); \
}

#define MAKE_FLAGSPARSER(TYPE) \
MAKE_PARSER(TYPE) { \
    PATHSTR b; \
    sscanf(token, PATHSTR_FMT, b); \
    for (LPSTR s = b; *s; s++) *s = *s == '|' ? ',' : *s; \
    PARSE_LIST(b, flag, parse_segment) { \
        *((DWORD *)out) |= 1 << ParseEnumString(flag, TYPE); \
    } \
}

static void ParseFloatList(LPCSTR token, FLOAT *values, DWORD count) {
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

MAKE_PARSER(Float) { *((FLOAT *)out) = atof(token); }
MAKE_PARSER(Integer) { *((LONG *)out) = atoi(token); }
MAKE_PARSER(Vector2) { ParseFloatList(token, out, 2); }
MAKE_PARSER(Vector3) { ParseFloatList(token, out, 3); }
MAKE_PARSER(Vector4) { ParseFloatList(token, out, 4); }
MAKE_PARSER(Color) {
    FLOAT values[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    ParseFloatList(token, values, 4);
    ((COLOR32 *)out)->r = values[0] * 0xff;
    ((COLOR32 *)out)->g = values[1] * 0xff;
    ((COLOR32 *)out)->b = values[2] * 0xff;
    ((COLOR32 *)out)->a = values[3] * 0xff;
}
MAKE_PARSER(FramePtr) {
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
MAKE_PARSER(Name) {
    memset(out, 0, sizeof(UINAME));
    sscanf(token, UINAME_FMT, (LPSTR)out);
}
MAKE_PARSER(ButtonText) {
    BUTTONTEXT *bt = out;
    memset(bt, 0, sizeof(BUTTONTEXT));
    assert(sscanf(token, UINAME_FMT " " UINAME_FMT, bt->frame, bt->text) == 2);
}
MAKE_PARSER(Text) {
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

MAKE_PARSER(StringListItem) {
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

MAKE_ENUMPARSER(AlphaMode);
MAKE_ENUMPARSER(FontJustificationH);
MAKE_ENUMPARSER(FontJustificationV);
MAKE_ENUMPARSER(HighlightType);
MAKE_ENUMPARSER(FontFlags);
MAKE_ENUMPARSER(FramePointType);
MAKE_FLAGSPARSER(CornerFlags);
MAKE_FLAGSPARSER(ControlStyle);

MAKE_PARSER(TextureFile) {
    PATHSTR path = { 0 };
    DWORD image = 0;
    sscanf(token, PATHSTR_FMT, path);
//    BOOL decorate = frame->DecorateFileNames | (frame->Parent ? frame->Parent->DecorateFileNames : false);
    image = path[0] ? UI_LoadTexture(path, true) : 0;
    *((DWORD *)out) = image;
#ifdef DIAG_OUTPUT
    if (path[0] && image == 0) {
        DIAGF("ParseTextureFile: frame=%s token=%s resolved image=0\n", frame->Name, path);
    }
#endif
}

MAKE_PARSER(ModelPath) {
    PATHSTR path = { 0 };
    BOOL decorate = frame->DecorateFileNames | (frame->Parent ? frame->Parent->DecorateFileNames : false);
    DWORD modelIndex = 0;
    sscanf(token, PATHSTR_FMT, path);
    modelIndex = UI_LoadModel(path, decorate);
    *((DWORD *)out) = modelIndex;
#ifdef DIAG_OUTPUT
    LPCSTR model = decorate ? Theme_String(path, "Default") : path;
    if (decorate && path[0] && !strcmp(model, path)) {
        DIAGF("ParseModelPath: unresolved skin key frame=%s token=%s\n", frame->Name, path);
    }
    if (path[0] && modelIndex == 0) {
        DIAGF("ParseModelPath: frame=%s token=%s resolved=%s modelIndex=0\n", frame->Name, path, model);
    }
#endif
}

MAKE_PARSERCALL(Font) {
    LPCSTR file = Theme_String(frame->Font.Name, "Default");
    frame->Font.Index = uiimport.FontIndex(file, frame->Font.Size * 1000);
}

static DWORD UI_DecodeFramePointY(DWORD framepoint) {
    return (framepoint >> 2) & 3;
}

MAKE_PARSERCALL(SetPoint) {
    if (!frame->AnyPointsSet) { // clear any template points
        memset(&frame->Points, 0, sizeof(frame->Points));
        frame->AnyPointsSet = true;
    }
    DWORD const x = frame->SetPoint.type & 3;
    FRAMEPOINT *xp = frame->Points.x;
    if (x != FPP_MID || (!xp[FPP_MIN].used && !xp[FPP_MAX].used)) {
        xp[FPP_MID].used = false;
        xp[x].used = true;
        xp[x].offset = frame->SetPoint.x;
        xp[x].targetPos = frame->SetPoint.target & 3;
        xp[x].relativeTo = frame->SetPoint.relativeTo;
    }
    DWORD y = UI_DecodeFramePointY(frame->SetPoint.type);
    FRAMEPOINT *yp = frame->Points.y;
    if (y != FPP_MID || (!yp[FPP_MIN].used && !yp[FPP_MAX].used)) {
        yp[FPP_MID].used = false;
        yp[y].used = true;
        yp[y].offset = frame->SetPoint.y;
        yp[y].targetPos = UI_DecodeFramePointY(frame->SetPoint.target);
        yp[y].relativeTo = frame->SetPoint.relativeTo;
    }
}

MAKE_PARSERCALL(Anchor) {
    frame->SetPoint.type = frame->Anchor.corner;
    frame->SetPoint.target = frame->Anchor.corner;
    frame->SetPoint.relativeTo = frame->Parent;
    frame->SetPoint.x = frame->Anchor.x;
    frame->SetPoint.y = frame->Anchor.y;
    SetPoint(parser, frame);
}

MAKE_PARSERCALL(DecorateFileNames) {
    frame->DecorateFileNames = true;
}

MAKE_PARSERCALL(BackdropMirrored) {
    frame->Backdrop.Mirrored = true;
}

MAKE_PARSERCALL(SliderLayoutHorizontal) {
    frame->Slider.Layout = LAYOUT_HORIZONTAL;
}

MAKE_PARSERCALL(SliderLayoutVertical) {
    frame->Slider.Layout = LAYOUT_VERTICAL;
}

MAKE_PARSERCALL(BackdropTileBackground) {
    frame->Backdrop.TileBackground = true;
}

MAKE_PARSERCALL(BackdropHalfSides) {
    // Warcraft uses this for some button backdrops. We don't model the split
    // edge geometry separately yet, but we can safely accept the token so the
    // template file loads.
}

MAKE_PARSERCALL(UseActiveContext) {
//    frame->Backdrop.TileBackground = true;
}

MAKE_PARSERCALL(TabFocusDefault) {
    frame->Control.TabFocusDefault = true;
}

MAKE_PARSERCALL(TabFocusPush) {
}

MAKE_PARSERCALL(BackdropBlendAll) {
    frame->Backdrop.BlendAll = true;
}

MAKE_PARSERCALL(SetAllPoints) {
    UI_SetAllPoints(frame);
}

MAKE_PARSERCALL(Texture) {
    FDF_ParseFrame(parser, UI_Spawn(FT_TEXTURE, frame));
}

MAKE_PARSERCALL(Layer) {
    FDF_ParseFrame(parser, UI_Spawn(FT_LAYER, frame));
}

MAKE_PARSERCALL(String) {
    FDF_ParseFrame(parser, UI_Spawn(FT_STRING, frame));
}

MAKE_PARSERCALL(Frame) {
    LPCSTR stype = parse_token(parser);
    FRAMETYPE type = ParseEnumString(stype, FrameType);
    LPFRAMEDEF current = UI_Spawn(type, frame);
    FDF_ParseFrame(parser, current);
}

MAKE_PARSERCALL(IncludeFile) {
    LPCSTR filename = parse_token(parser);
    UI_ParseFDF(filename);
}

MAKE_PARSERCALL(StringList) {
    FRAMEDEF string_list;
    memset(&string_list, 0, sizeof(FRAMEDEF));
    string_list.Type = FT_STRINGLIST;
    FDF_ParseFrame(parser, &string_list);
}

MAKE_PARSERCALL(EditHighlightInitial) {
    frame->Edit.HighlightInitial = true;
}

MAKE_PARSERCALL(EditSetFocus) {
    frame->Edit.Focus = true;
}

MAKE_PARSERCALL(MenuItem) {
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

#define F_END { NULL }

static fdf_parse_class_t classes[] = {
    { "Frame", Frame },
    { "Texture", Texture },
    { "String", String },
    { "Layer", Layer },
    { "StringList", StringList },
    { "IncludeFile", IncludeFile },
    F_END,
};

static parseItem_t items[] = {
    // Flags
    { "DecorateFileNames", { F_END }, DecorateFileNames },
    { "BackdropMirrored", { F_END }, BackdropMirrored },
    { "SetAllPoints", { F_END }, SetAllPoints },
    { "SetPoint", { F(SetPoint.type, FramePointType), F(SetPoint.relativeTo, FramePtr), F(SetPoint.target, FramePointType), F(SetPoint.x, Float), F(SetPoint.y, Float), F_END }, SetPoint },
    { "UseActiveContext", { F_END }, UseActiveContext },
    { "ControlShortcutKey", { F(Control.ShortcutKey, Name), F_END } },
    { "ControlFocusHighlight", { F(Control.Backdrop.Focus, Name), F_END } },
    { "TabFocusDefault", { F_END }, TabFocusDefault },
    { "TabFocusPush", { F_END }, TabFocusPush },
    { "TabFocusNext", { F(Control.TabFocusNext, Name), F_END } },
    { "DialogBackdrop", { F(DialogBackdropName, Name), F_END } },
    // Fields
    { "Width", { F(Width, Float), F_END } },
    { "Height", { F(Height, Float), F_END } },
    { "File", { F(Texture.Image, TextureFile), F_END } },
    { "TexCoord", { F(Texture.TexCoord.min.x, Float), F(Texture.TexCoord.max.x, Float), F(Texture.TexCoord.min.y, Float), F(Texture.TexCoord.max.y, Float), F_END } },
    { "BackgroundArt", { F(Portrait.model, ModelPath), F_END } },
    { "AlphaMode", { F(AlphaMode, AlphaMode), F_END } },
    { "Anchor", { F(Anchor.corner, FramePointType), F(Anchor.x, Float), F(Anchor.y, Float), F_END }, Anchor },
    { "Font", { F(Font.Name, Name), F(Font.Size, Float), F_END }, Font },
    { "Text", { F(TextStorage, Text), F_END } },
    { "TextLength", { F(TextLength, Integer), F_END } },
    { "FrameFont", { F(Font.Name, Name), F(Font.Size, Float), F(Font.Unknown, Name), F_END }, Font },
    // Font
    { "FontJustificationH", { F(Font.Justification.Horizontal, FontJustificationH), F_END } },
    { "FontJustificationV", { F(Font.Justification.Vertical, FontJustificationV), F_END } },
    { "FontJustificationOffset", { F(Font.Justification.Offset, Vector2), F_END } },
    { "FontFlags", { F(Font.FontFlags, FontFlags), F_END } },
    { "FontColor", { F(Font.Color, Color), F_END } },
    { "FontHighlightColor", { F(Font.HighlightColor, Color), F_END } },
    { "FontDisabledColor", { F(Font.DisabledColor, Color), F_END } },
    { "FontShadowColor", { F(Font.ShadowColor, Color), F_END } },
    { "FontShadowOffset", { F(Font.ShadowOffset, Vector2), F_END } },
    // Backdrop
    { "BackdropTileBackground", { F_END }, BackdropTileBackground },
    { "BackdropHalfSides", { F_END }, BackdropHalfSides },
    { "BackdropBackground", { F(Backdrop.Background, TextureFile), F_END } },
    { "BackdropCornerFlags", { F(Backdrop.CornerFlags, CornerFlags), F_END } },
    { "BackdropCornerFile", { F(Backdrop.EdgeFile, TextureFile), F_END } },
    { "BackdropLeftFile", { F_END }, BackdropHalfSides },
    { "BackdropRightFile", { F_END }, BackdropHalfSides },
    { "BackdropTopFile", { F_END }, BackdropHalfSides },
    { "BackdropBottomFile", { F_END }, BackdropHalfSides },
    { "BackdropCornerSize", { F(Backdrop.CornerSize, Float), F_END } },
    { "BackdropBackgroundSize", { F(Backdrop.BackgroundSize, Float), F_END } },
    { "BackdropBackgroundInsets", { F(Backdrop.BackgroundInsets, Vector4), F_END } },
    { "BackdropEdgeFile", { F(Backdrop.EdgeFile, TextureFile), F_END } },
    { "BackdropBlendAll", { F_END }, BackdropBlendAll },
    // Highlight
    { "HighlightType", { F(Highlight.Type, HighlightType), F_END } },
    { "HighlightAlphaFile", { F(Highlight.AlphaFile, TextureFile), F_END } },
    { "HighlightAlphaMode", { F(Highlight.AlphaMode, AlphaMode), F_END } },
    { "HighlightColor", { F(Highlight.Color, Color), F_END } },
    // Control
    { "ControlStyle", { F(Control.Style, ControlStyle), F_END } },
    { "ControlBackdrop", { F(Control.Backdrop.Normal, Name), F_END } },
    { "ControlPushedBackdrop", { F(Control.Backdrop.Pushed, Name), F_END } },
    { "ControlDisabledBackdrop", { F(Control.Backdrop.Disabled, Name), F_END } },
    { "ControlMouseOverHighlight", { F(Control.Backdrop.MouseOver, Name), F_END } },
    { "ControlDisabledPushedBackdrop", { F(Control.Backdrop.DisabledPushed, Name), F_END } },
    // Slider
    { "SliderInitialValue", { F(Slider.InitialValue, Float), F_END } },
    { "SliderLayoutHorizontal", { F_END }, SliderLayoutHorizontal },
    { "SliderLayoutVertical", { F_END }, SliderLayoutVertical },
    { "SliderMaxValue", { F(Slider.MaxValue, Float), F_END } },
    { "SliderMinValue", { F(Slider.MinValue, Float), F_END } },
    { "SliderStepSize", { F(Slider.StepSize, Float), F_END } },
    { "ScrollBarIncButtonFrame", { F(Slider.IncButtonFrame, Name), F_END } },
    { "ScrollBarDecButtonFrame", { F(Slider.DecButtonFrame, Name), F_END } },
    { "SliderThumbButtonFrame", { F(Slider.ThumbButtonFrame, Name), F_END } },
    // Menu
    { "ListBoxBorder", { F(ListBox.Border, Float), F_END } },
    { "ListBoxScrollBar", { F(ListBox.ScrollBar, Name), F_END } },
    { "MenuBorder", { F(Menu.Border, Float), F_END } },
    { "MenuItem", { F_END }, MenuItem },
    { "MenuItemHeight", { F(Menu.Item.Height, Float), F_END } },
    { "MenuTextHighlightColor", { F(Menu.TextHighlightColor, Color), F_END } },
    // Edit
    { "EditBorderSize", { F(Edit.BorderSize, Float), F_END } },
    { "EditCursorColor", { F(Edit.CursorColor, Color), F_END } },
    { "EditHighlightColor", { F(Edit.HighlightColor, Color), F_END } },
    { "EditHighlightInitial", { F_END }, EditHighlightInitial },
    { "EditMaxChars", { F(Edit.MaxChars, Integer), F_END } },
    { "EditSetFocus", { F_END }, EditSetFocus },
    { "EditText", { F(Edit.Text, Text), F_END } },
    { "EditTextColor", { F(Edit.TextColor, Color), F_END } },
    { "EditTextFrame", { F(Edit.TextFrame, Name), F_END } },
    { "EditTextOffset", { F(Edit.TextOffset, Vector2), F_END } },
    // Popup
    { "PopupButtonInset", { F(Popup.ButtonInset, Float), F_END } },
    { "PopupArrowFrame", { F(Popup.ArrowFrame, Name), F_END } },
    { "PopupMenuFrame", { F(Popup.MenuFrame, Name), F_END } },
    { "PopupTitleFrame", { F(Popup.TitleFrame, Name), F_END } },
    // TextArea
    { "TextAreaLineHeight", { F(TextArea.LineHeight, Float), F_END } },
    { "TextAreaLineGap", { F(TextArea.LineGap, Float), F_END } },
    { "TextAreaInset", { F(TextArea.Inset, Float), F_END } },
    { "TextAreaScrollBar", { F(TextArea.ScrollBar, Name), F_END } },
    { "TextAreaMaxLines", { F(TextArea.MaxLines, Integer), F_END } },
    { "ChatDisplayLineHeight", { F(TextArea.LineHeight, Float), F_END } },
    { "ChatDisplayBorderSize", { F(TextArea.Inset, Float), F_END } },
    // CheckBox
    { "CheckBoxCheckHighlight", { F(CheckBox.CheckHighlight, Name), F_END } },
    { "CheckBoxDisabledCheckHighlight", { F(CheckBox.DisabledCheckHighlight, Name), F_END } },
    // Button
    { "ButtonText", { F(TextStorage, Text), F_END } },
    { "ButtonPushedTextOffset", { F(Button.PushedTextOffset, Vector2), F_END } },
    { "NormalTexture", { F(Button.NormalTexture, Name), F_END } },
    { "PushedTexture", { F(Button.PushedTexture, Name), F_END } },
    { "DisabledTexture", { F(Button.DisabledTexture, Name), F_END } },
    { "NormalText", { F(Button.NormalText, ButtonText), F_END } },
    { "DisabledText", { F(Button.DisabledText, ButtonText), F_END } },
    { "HighlightText", { F(Button.HighlightText, ButtonText), F_END } },
    { "UseHighlight", { F(Button.UseHighlight, Name), F_END } },
    // End of list
    F_END
};

LPCSTR parse_segment2(LPPARSER p);

void parse_item(LPPARSER parser, LPFRAMEDEF frame, parseItem_t *item) {
    for (parseArg_t *arg = item->args; arg->name; arg++) {
        LPCSTR token = parse_segment2(parser);
        arg->func(token, frame, (uint8_t *)frame + arg->fofs);
    }
    if (!item->args->name && item->func != MenuItem) { // eat trailing comma
        parse_segment(parser);
    }
    if (item->func) {
        item->func(parser, frame);
    }
}

void parse_func(LPPARSER parser, LPFRAMEDEF frame) {
    LPCSTR token = NULL;
    /* Some shipped FDF files end while a frame is still open. Treat EOF like
     * an implicit close so we can consume the original assets verbatim. */
    while ((token = parse_token(parser)) && *token && (*token != '}')) {
        if (frame->Type == FT_STRINGLIST) {
            static parseItem_t stringitem = { "", { F(Name, StringListItem), F_END } };
            stringListItem_t *str = uiimport.MemAlloc(sizeof(stringListItem_t));
            ADD_TO_LIST(str, strings);
            strcpy(str->name, token);
            parse_item(parser, frame, &stringitem);
//            printf("%s %s\n", str->name, str->value);
            goto parse_next;
        } else {
            for (parseItem_t *it = items; it->name; it++) {
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

LPFRAMEDEF UI_FindFrame(LPCSTR name) {
    FOR_LOOP(i, MAX_UI_CLASSES) {
        if (!strcmp(frames[i].Name, name)) {
            return frames+i;
        }
    }
    return NULL;
}

LPFRAMEDEF UI_FindFrameByNumber(DWORD number) {
    if (number < MAX_UI_CLASSES && frames[number].inuse) {
        return &frames[number];
    }
    return NULL;
}

LPFRAMEDEF UI_FindFrameNear(LPCFRAMEDEF anchor, LPCSTR name) {
    if (!name || !*name) {
        return NULL;
    }
    if (!anchor || anchor < frames || anchor >= frames + MAX_UI_CLASSES) {
        return UI_FindFrame(name);
    }

    LPFRAMEDEF child = UI_FindChildFrame((LPFRAMEDEF)anchor, name);
    if (child) {
        return child;
    }

    DWORD const anchor_index = (DWORD)(anchor - frames);
    DWORD best_distance = MAX_UI_CLASSES;
    LPFRAMEDEF best = NULL;

    FOR_LOOP(i, MAX_UI_CLASSES) {
        if (!strcmp(frames[i].Name, name)) {
            DWORD const distance = i > anchor_index ? i - anchor_index : anchor_index - i;
            if (!best || distance < best_distance) {
                best = frames + i;
                best_distance = distance;
            }
        }
    }
    return best;
}

LPFRAMEDEF UI_FindChildFrame(LPFRAMEDEF frame, LPCSTR name) {
    if (!strcmp(frame->Name, name))
        return frame;
    FOR_LOOP(i, MAX_UI_CLASSES) {
        if (frames[i].Parent != frame)
            continue;
        LPFRAMEDEF found = UI_FindChildFrame(frames+i, name);
        if (found)
            return found;
    }
    return NULL;
}

static BOOL UI_FrameNameEquals(LPCFRAMEDEF frame, LPCSTR name) {
    return frame && name && *name && !strcmp(frame->Name, name);
}

static BOOL UI_IsButtonFrameType(FRAMETYPE type) {
    return type == FT_BUTTON ||
           type == FT_TEXTBUTTON ||
           type == FT_GLUETEXTBUTTON ||
           type == FT_GLUEBUTTON ||
           type == FT_GLUEPOPUPMENU ||
           type == FT_SIMPLEBUTTON;
}

static BOOL UI_IsCheckBoxFrameType(FRAMETYPE type) {
    return type == FT_CHECKBOX ||
           type == FT_GLUECHECKBOX ||
           type == FT_SIMPLECHECKBOX;
}

static BOOL UI_IsEmbeddedControlPart(LPCFRAMEDEF parent, LPCFRAMEDEF child) {
    if (!parent || !child) {
        return false;
    }
    /* Control art children are serialized into their owning control's payload by
     * ui_write.c. Emitting them again as standalone children draws duplicates. */
    if (child->Type == FT_BACKDROP) {
        return UI_FrameNameEquals(child, parent->Control.Backdrop.Normal) ||
               UI_FrameNameEquals(child, parent->Control.Backdrop.Pushed) ||
               UI_FrameNameEquals(child, parent->Control.Backdrop.Disabled) ||
               UI_FrameNameEquals(child, parent->Control.Backdrop.DisabledPushed);
    }
    if (child->Type == FT_HIGHLIGHT) {
        return UI_FrameNameEquals(child, parent->Control.Backdrop.MouseOver) ||
               (UI_IsCheckBoxFrameType(parent->Type) &&
                (UI_FrameNameEquals(child, parent->CheckBox.CheckHighlight) ||
                 UI_FrameNameEquals(child, parent->CheckBox.DisabledCheckHighlight)));
    }
    if (child->Type == FT_TEXT) {
        if (UI_IsButtonFrameType(parent->Type) &&
            (UI_FrameNameEquals(child, parent->Text) ||
             UI_FrameNameEquals(child, parent->Button.NormalText.frame))) {
            return true;
        }
        return UI_FrameNameEquals(child, parent->Edit.TextFrame);
    }
    if (parent->Type == FT_SLIDER && UI_IsButtonFrameType(child->Type)) {
        return UI_FrameNameEquals(child, parent->Slider.ThumbButtonFrame) ||
               UI_FrameNameEquals(child, parent->Slider.IncButtonFrame) ||
               UI_FrameNameEquals(child, parent->Slider.DecButtonFrame);
    }
    return false;
}

static DWORD UI_CollectFrameTreeRecursiveEx(LPCFRAMEDEF frame,
                                            LPCFRAMEDEF *out,
                                            DWORD max,
                                            BOOL include_embedded)
{
    DWORD total = 0;

    if (!frame) {
        return 0;
    }

    if (out && total < max) {
        out[total] = frame;
    }
    total++;

    FOR_LOOP(i, MAX_UI_CLASSES) {
        LPCFRAMEDEF child = frames + i;
        if (child->Parent == frame &&
            (include_embedded || (!child->hidden && !UI_IsEmbeddedControlPart(frame, child)))) {
            DWORD emitted = UI_CollectFrameTreeRecursiveEx(child,
                                                           out ? out + total : NULL,
                                                           max > total ? max - total : 0,
                                                           include_embedded);
            total += emitted;
        }
    }

    return total;
}

static DWORD UI_CollectFrameTreeRecursive(LPCFRAMEDEF frame, LPCFRAMEDEF *out, DWORD max) {
    return UI_CollectFrameTreeRecursiveEx(frame, out, max, false);
}

/* Collect the write-order frame graph rooted at root.
 * The result order matches UI_WriteFrameWithChildren traversal:
 *   node first, then visible children in template-slot order.
 * Returns the total number of frames in the traversal, even if out is too
 * small to hold all of them. */
DWORD UI_CollectFrameTree(LPCFRAMEDEF root, LPCFRAMEDEF *out, DWORD max) {
    return UI_CollectFrameTreeRecursive(root, out, max);
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

void UI_BindMapList(LPFRAMEDEF frame,
                    uiMapListState_t *state,
                    LPCFRAMEDEF label,
                    DWORD visible_rows,
                    LPCSTR select_command)
{
    uiMapListControl_t *control;

    if (!frame) {
        return;
    }

    control = &frame->MapListControl;
    memset(control, 0, sizeof(*control));
    control->State = state;
    UI_WireFrameTypeFunctions(frame);
    control->VisibleRows = visible_rows;
    control->RowHeight = 0.019f;
    control->InsetX = 0.008f;
    control->InsetY = 0.007f;
    snprintf(control->SelectCommand,
             sizeof(control->SelectCommand),
             "%s",
             select_command ? select_command : "");
    snprintf(control->FontName,
             sizeof(control->FontName),
             "%s",
             label && label->Font.Name[0] ? label->Font.Name : "MasterFont");
    control->FontSize = label && label->Font.Size > 0 ? label->Font.Size : 0.010f;
    control->TextColor = Theme_ListBoxTextColor();
    control->SelectedTextColor = Theme_ListBoxSelectedTextColor();
}

void UI_MenuClearItems(LPFRAMEDEF frame) {
    if (!frame) {
        return;
    }
    frame->Menu.ItemCount = 0;
    memset(frame->Menu.Items, 0, sizeof(frame->Menu.Items));
}

void UI_MenuAddItem(LPFRAMEDEF frame, LPCSTR text, LONG value) {
    uiMenuItem_t *item;

    if (!frame || frame->Menu.ItemCount >= UI_MAX_MENU_ITEMS) {
        return;
    }
    item = &frame->Menu.Items[frame->Menu.ItemCount++];
    snprintf(item->text, sizeof(item->text), "%s", text ? text : "");
    item->value = value;
    snprintf(frame->Menu.Item.Text, sizeof(frame->Menu.Item.Text), "%s", item->text);
    frame->Menu.Item.Value = (DWORD)value;
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
    /* Text preprocessing - simplified for client-side */
    /* gi.TextRemoveComments(buffer); */
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

    int size = uiimport.FS_ReadFile(fileName, &buffer);
    if (size >= 0 && buffer) {
        const BYTE *raw = (const BYTE *)buffer;
        BOOL utf16le = (size >= 2 && raw[0] == 0xFF && raw[1] == 0xFE);
        DWORD text_size = utf16le ? (DWORD)(size / 2) : (DWORD)size;
        LPSTR text = uiimport.MemAlloc(text_size + 1);
        if (text) {
            if (utf16le) {
                /* Strip UTF-16 LE BOM and fold 16-bit chars to 8-bit.
                 * FDF files are ASCII-only so the high byte is always 0. */
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
            uiimport.MemFree(text);
            UI_MarkFDFLoaded(fileName);
            loaded = true;
        }
        uiimport.FS_FreeFile(buffer);
    }
    return loaded;
}

void UI_ParseFDF(LPCSTR fileName) {
    UI_EnsureFDF(fileName);
}

void UI_WriteFrameWithChildren(LPCFRAMEDEF frame, LPCFRAMEDEF parent) {
    /* Stub - frame serialization not used in client-side UI library */
    (void)frame;
    (void)parent;
}

/* Stub function - not used in client-side UI */
void UI_WriteFrame(LPCFRAMEDEF frame) {
    (void)frame;
}

/* Stub function - not used in client-side UI */
void UI_WriteStart(DWORD layer) {
    (void)layer;
}

void UI_SetPoint(LPFRAMEDEF frame,
                 UIFRAMEPOINT framePoint,
                 LPCFRAMEDEF other,
                 UIFRAMEPOINT otherPoint,
                 FLOAT x,
                 FLOAT y)
{
    frame->SetPoint.type = framePoint;
    frame->SetPoint.relativeTo = other;
    frame->SetPoint.target = otherPoint;
    frame->SetPoint.x = x;
    frame->SetPoint.y = y;
    SetPoint(NULL, frame);
}

void UI_SetAllPoints(LPFRAMEDEF frame) {
    UI_SetPoint(frame, FRAMEPOINT_TOPLEFT, NULL, FRAMEPOINT_TOPLEFT, 0, 0);
    UI_SetPoint(frame, FRAMEPOINT_BOTTOMRIGHT, NULL, FRAMEPOINT_BOTTOMRIGHT, 0, 0);
}

void UI_SetParent(LPFRAMEDEF frame, LPCFRAMEDEF parent) {
    frame->Parent = parent;
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

void UI_SetOnClick(LPFRAMEDEF frame, LPCSTR format, ...) {
    va_list argptr;
    if (!frame || !format) {
        return;
    }
    va_start(argptr, format);
    vsnprintf(frame->OnClick, sizeof(frame->OnClick), format, argptr);
    va_end(argptr);
}

void UI_SetEnabled(LPFRAMEDEF frame, BOOL enabled) {
    if (!frame) {
        return;
    }
    frame->disabled = !enabled;
    if (frame->disabled) frame->ui_flags |= UIFLAG_DISABLED;
    else frame->ui_flags &= ~UIFLAG_DISABLED;
}

void UI_SetTextPointer(LPFRAMEDEF frame, LPCSTR text) {
    UI_FreeFrameDynamicText(frame);
    frame->Text = text;
}

void UI_SetSize(LPFRAMEDEF frame, FLOAT width, FLOAT height) {
    frame->Width = width;
    frame->Height = height;
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

void UI_SetHidden(LPFRAMEDEF frame, BOOL value) {
    if (!frame) {
        return;
    }
    frame->hidden = value;
    if (frame->hidden) frame->ui_flags &= ~UIFLAG_VISIBLE;
    else frame->ui_flags |= UIFLAG_VISIBLE;
}

void UI_WriteFrameWithChildrenWithTriggers(LPEDICT ent, LPCFRAMEDEF frame, LPCFRAMEDEF parent, uiTrigger_t const *triggers) {
    /* Stub - not used in client-side UI library */
    (void)ent;
    (void)frame;
    (void)parent;
    (void)triggers;
}

void UI_WriteWithTriggers(LPEDICT ent, LPCFRAMEDEF root, DWORD layer, uiTrigger_t const *triggers) {
    /* Stub - not used in client-side UI library */
    (void)ent;
    (void)root;
    (void)layer;
    (void)triggers;
}
