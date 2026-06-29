/*
 * hud.c — SC2Layout → uiframe serialization bridge.
 *
 * Converts parsed SC2FRAMEDEF trees (from stb_sc2layout.h) into uiFrame_t
 * wire format for transmission via svc_layout.  These functions need gi for
 * network writes, so they live in the game module rather than the UI module.
 *
 * Also provides the game-side .SC2Layout XML parser using libxml2 + gi.ReadFile.
 */

#include "hud_local.h"
#include <libxml/parser.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

/* ---------------------------------------------------------------
 * Host services — game module implementations using gi
 * --------------------------------------------------------------- */
HANDLE SC2_Alloc(long size) { return gi.MemAlloc(size); }
void   SC2_Free(HANDLE ptr) { gi.MemFree(ptr); }
DWORD  SC2_FontIndex(LPCSTR name, DWORD size) { return gi.FontIndex(name, size); }
int    SC2_ReadFile(LPCSTR name, HANDLE *out) {
    DWORD size = 0;
    *out = gi.ReadFile(name, &size);
    return *out ? (int)size : -1;
}
void   SC2_FreeFile(HANDLE buf) { gi.MemFree(buf); }

/* ---------------------------------------------------------------
 * Frame storage
 * --------------------------------------------------------------- */
DWORD ui_next_frame_number;
#define MAX_FRAMES_SC2 1024
static SC2FRAMEDEF sc2_frames[MAX_FRAMES_SC2];
static DWORD sc2_num_frames;

/* ---------------------------------------------------------------
 * SVG layout protocol helpers
 * --------------------------------------------------------------- */
void SC2_WriteStart(DWORD layer) {
    gi.Write(PF_BYTE, &(LONG){svc_layout});
    gi.Write(PF_BYTE, &(LONG){layer});
    ui_next_frame_number = 1;
}

void SC2_WriteEnd(LPEDICT ent) {
    gi.Write(PF_LONG, &(LONG){0});  /* terminator */
    gi.Write(PF_SHORT, &(LONG){0});
    gi.unicast(ent);
}

/* ---------------------------------------------------------------
 * FRAMEDEF → uiFrame_t conversion
 * --------------------------------------------------------------- */
static void CopyFrameBase(LPUIFRAME dest, LPCSC2FRAMEDEF src) {
    FOR_LOOP(i, FPP_COUNT) {
        dest->points.x[i].targetPos = src->Points.x[i].targetPos;
        dest->points.x[i].used = src->Points.x[i].used;
        dest->points.x[i].relativeTo = src->Points.x[i].relativeTo ? UI_PARENT : 0;
        dest->points.x[i].offset = (SHORT)(src->Points.x[i].offset * 1000.0f);
        dest->points.y[i].targetPos = src->Points.y[i].targetPos;
        dest->points.y[i].used = src->Points.y[i].used;
        dest->points.y[i].relativeTo = src->Points.y[i].relativeTo ? UI_PARENT : 0;
        dest->points.y[i].offset = (SHORT)(src->Points.y[i].offset * 1000.0f);
    }

    dest->parent = src->Parent ? (DWORD)src->Parent->resolved_index : 0;
    dest->color = src->Color;
    dest->size.width = src->Width;
    dest->size.height = src->Height;
    dest->flags.type = src->Type;
    dest->text = src->Text;
    dest->onclick = src->OnClick;
    dest->stat = src->Stat;
    dest->color.a = src->Alpha * 255.0f;
    /* hidden handled by SC2_WriteFrameWithChildren tree skip; disabled not in wire format */

    if (src->num_textures > 0 && src->textures[0].image)
        dest->tex.index = (USHORT)src->textures[0].image;
}

static BOOL BuildFrameForWrite(LPCSC2FRAMEDEF frame, LPUIFRAME out,
                                LPBYTE typedata, DWORD typedata_max,
                                LPSTR textbuf, DWORD textbuf_max) {
    if (!frame || !out || !typedata || typedata_max == 0)
        return false;

    memset(out, 0, sizeof(*out));
    memset(typedata, 0, typedata_max);
    if (textbuf && textbuf_max > 0)
        memset(textbuf, 0, textbuf_max);

    CopyFrameBase(out, frame);

    struct { LPBYTE data; DWORD maxsize; DWORD cursize; BOOL overflowed; } buf = {
        .data = typedata, .maxsize = typedata_max,
    };

    switch (frame->Type) {
        case FT_BACKDROP: {
            struct { DWORD bg, edge, tile; FLOAT insets[4]; } data;
            memset(&data, 0, sizeof(data));
            data.bg = frame->backdrop.bg;
            data.edge = frame->backdrop.edge;
            data.tile = frame->backdrop.tile;
            memcpy(data.insets, frame->backdrop.insets, sizeof(data.insets));
            if (buf.cursize + sizeof(data) <= buf.maxsize) {
                memcpy(buf.data + buf.cursize, &data, sizeof(data));
                buf.cursize += (DWORD)sizeof(data);
            } else { buf.overflowed = true; }
            break;
        }
        case FT_TEXT:
        case FT_STRING: {
            if (!out->points.x[FPP_MIN].used && !out->points.x[FPP_MID].used && !out->points.x[FPP_MAX].used) {
                out->points.x[FPP_MID].targetPos = FPP_MID;
                out->points.x[FPP_MID].relativeTo = UI_PARENT;
                out->points.x[FPP_MID].used = 1;
            }
            if (!out->points.y[FPP_MIN].used && !out->points.y[FPP_MID].used && !out->points.y[FPP_MAX].used) {
                out->points.y[FPP_MID].targetPos = FPP_MID;
                out->points.y[FPP_MID].relativeTo = UI_PARENT;
                out->points.y[FPP_MID].used = 1;
            }
            out->color = frame->font.color;
            break;
        }
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
    return true;
}

/* ---------------------------------------------------------------
 * Frame tree serialization
 * --------------------------------------------------------------- */
void SC2_WriteFrame(LPSC2FRAMEDEF frame) {
    uiFrame_t tmp;
    BYTE typedata[256];
    char textbuf[256];

    if (!BuildFrameForWrite(frame, &tmp, typedata, sizeof(typedata), textbuf, sizeof(textbuf)))
        return;

    tmp.number = ++ui_next_frame_number;
    frame->resolved_index = (int)tmp.number;
    gi.Write(PF_UIFRAME, &tmp);
}

void SC2_WriteFrameWithChildren(LPSC2FRAMEDEF frame) {
    if (!frame) return;
    SC2_WriteFrame(frame);
    FOR_LOOP(i, sc2_num_frames) {
        LPSC2FRAMEDEF child = &sc2_frames[i];
        if (child->Parent == frame && !child->hidden)
            SC2_WriteFrameWithChildren(child);
    }
}

void SC2_WriteLayout(LPEDICT ent, LPSC2FRAMEDEF root, DWORD layer) {
    SC2_WriteStart(layer);
    SC2_WriteFrameWithChildren(root);
    SC2_WriteEnd(ent);
}

/* ---------------------------------------------------------------
 * SC2Layout XML parser (game-side, using libxml2 + gi)
 * --------------------------------------------------------------- */
static SC2FRAMEDEF *AddFrame(LPCSC2FRAMEDEF parent) {
    if (sc2_num_frames >= MAX_FRAMES_SC2) return NULL;
    LPSC2FRAMEDEF f = &sc2_frames[sc2_num_frames++];
    memset(f, 0, sizeof(*f));
    f->Parent = parent;
    f->Color = (COLOR32){ 255, 255, 255, 255 };
    f->Alpha = 1.0f;
    return f;
}

static FRAMETYPE TypeFromSC2String(LPCSTR typeStr) {
    if (!typeStr) return FT_FRAME;
    if (!strcasecmp(typeStr, "Image")) return FT_TEXTURE;
    if (!strcasecmp(typeStr, "Button") || !strcasecmp(typeStr, "CommandButton")) return FT_COMMANDBUTTON;
    if (!strcasecmp(typeStr, "Label")) return FT_TEXT;
    if (!strcasecmp(typeStr, "EditBox")) return FT_EDITBOX;
    if (!strcasecmp(typeStr, "Model")) return FT_PORTRAIT;
    return FT_FRAME;
}

static FLOAT SC2_NormX(int px) { return (FLOAT)px / SC2_VIRT_W; }
static FLOAT SC2_NormY(int py) { return (FLOAT)py / SC2_VIRT_H; }

static FLOAT ParseFloatAttr(xmlNode *node, LPCSTR name, FLOAT def) {
    xmlChar *val = xmlGetProp(node, (xmlChar *)name);
    if (!val) return def;
    FLOAT r = (FLOAT)atof((char *)val);
    xmlFree(val);
    return r;
}

static int ParseIntAttr(xmlNode *node, LPCSTR name, int def) {
    xmlChar *val = xmlGetProp(node, (xmlChar *)name);
    if (!val) return def;
    int r = atoi((char *)val);
    xmlFree(val);
    return r;
}

static void ParseStrAttr(xmlNode *node, LPCSTR name, LPCSTR def, LPSTR out, DWORD out_size) {
    xmlChar *val = xmlGetProp(node, (xmlChar *)name);
    if (!val) {
        strncpy(out, def, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
    strncpy(out, (char *)val, out_size - 1);
    out[out_size - 1] = '\0';
    xmlFree(val);
}

static DWORD ResolveTexture(LPCSTR resource) {
    if (!resource || !*resource) return 0;
    LPCSTR path = resource;
    if (path[0] == '@' && path[1] == '@') path += 2;
    else if (path[0] == '@') path += 1;
    return (DWORD)gi.ImageIndex(path);
}

static void ParseAnchorNode(xmlNode *xml_anchor, LPSC2FRAMEDEF frame, LPCSC2FRAMEDEF parent) {
    char sideStr[32], posStr[32], relStr[64];
    ParseStrAttr(xml_anchor, "side", "Left", sideStr, sizeof(sideStr));
    ParseStrAttr(xml_anchor, "pos", "Min", posStr, sizeof(posStr));
    ParseStrAttr(xml_anchor, "relative", "$parent", relStr, sizeof(relStr));
    int offset = ParseIntAttr(xml_anchor, "offset", 0);

    int axis = (!strcasecmp(sideStr, "Top") || !strcasecmp(sideStr, "Bottom")) ? 1 : 0;
    SC2ANCHORPOS pos = (!strcasecmp(posStr, "Max")) ? SC2_APOS_MAX :
                      (!strcasecmp(posStr, "Mid")) ? SC2_APOS_MID : SC2_APOS_MIN;

    /* Convert side+pos to FPP_* positions */
    uiFramePointPos_t fpp = FPP_MID;
    if (axis == 0) {
        fpp = (!strcasecmp(sideStr, "Left")) ? FPP_MIN :
              (!strcasecmp(sideStr, "Right")) ? FPP_MAX : FPP_MID;
    } else {
        fpp = (!strcasecmp(sideStr, "Top")) ? FPP_MIN :
              (!strcasecmp(sideStr, "Bottom")) ? FPP_MAX : FPP_MID;
    }

    uiFramePointPos_t tgt = fpp;
    switch (pos) {
        case SC2_APOS_MAX: tgt = FPP_MAX; break;
        case SC2_APOS_MID: tgt = FPP_MID; break;
        default:           tgt = FPP_MIN; break;
    }

    FLOAT norm_offset = (axis == 0) ? SC2_NormX(offset) : SC2_NormY(offset);
    LPCSC2FRAMEDEF rel = (!strcasecmp(relStr, "$parent")) ? parent : NULL;

    if (axis == 0) {
        frame->Points.x[fpp].used = true;
        frame->Points.x[fpp].targetPos = tgt;
        frame->Points.x[fpp].relativeTo = rel;
        frame->Points.x[fpp].offset = norm_offset;
    } else {
        frame->Points.y[fpp].used = true;
        frame->Points.y[fpp].targetPos = tgt;
        frame->Points.y[fpp].relativeTo = rel;
        frame->Points.y[fpp].offset = norm_offset;
    }
}

static void ParseFrameNode(xmlNode *xml_frame, LPCSC2FRAMEDEF parent) {
    if (!xml_frame || xml_frame->type != XML_ELEMENT_NODE) return;

    LPSC2FRAMEDEF frame = AddFrame(parent);
    if (!frame) return;

    char typeStr[64], nameStr[256], templateStr[256];
    ParseStrAttr(xml_frame, "type", "Frame", typeStr, sizeof(typeStr));
    ParseStrAttr(xml_frame, "name", "", nameStr, sizeof(nameStr));
    ParseStrAttr(xml_frame, "template", "", templateStr, sizeof(templateStr));
    strncpy(frame->Name, nameStr, sizeof(frame->Name) - 1);
    strncpy(frame->template_path, templateStr, sizeof(frame->template_path) - 1);
    frame->Type = TypeFromSC2String(typeStr);

    for (xmlNode *child = xml_frame->children; child; child = child->next) {
        if (child->type != XML_ELEMENT_NODE) continue;
        LPCSTR tag = (LPCSTR)child->name;

        if (!strcasecmp(tag, "Width"))
            frame->Width = SC2_NormX(ParseIntAttr(child, "val", 0));
        else if (!strcasecmp(tag, "Height"))
            frame->Height = SC2_NormY(ParseIntAttr(child, "val", 0));
        else if (!strcasecmp(tag, "Visible"))
            frame->hidden = !ParseIntAttr(child, "val", 1);
        else if (!strcasecmp(tag, "Color")) {
            char val[64];
            ParseStrAttr(child, "val", "255,255,255,255", val, sizeof(val));
            int r = 255, g = 255, b = 255, a = 255;
            sscanf(val, "%d,%d,%d,%d", &r, &g, &b, &a);
            frame->Color = (COLOR32){ (BYTE)r, (BYTE)g, (BYTE)b, (BYTE)a };
        } else if (!strcasecmp(tag, "Alpha"))
            frame->Alpha = ParseFloatAttr(child, "val", 1.0f);
        else if (!strcasecmp(tag, "Anchor"))
            ParseAnchorNode(child, frame, parent);
        else if (!strcasecmp(tag, "Texture")) {
            char texVal[256];
            ParseStrAttr(child, "val", "", texVal, sizeof(texVal));
            int layer = ParseIntAttr(child, "layer", 0);
            if (frame->num_textures < 4) {
                sc2Texture_t *tex = &frame->textures[frame->num_textures++];
                strncpy(tex->resource, texVal, sizeof(tex->resource) - 1);
                tex->layer = layer;
                tex->has_texture = (*texVal != '\0');
                tex->image = ResolveTexture(texVal);
            }
        } else if (!strcasecmp(tag, "Frame"))
            ParseFrameNode(child, frame);
    }
}

/* Track loaded layouts to prevent double-loading */
#define MAX_LOADED_LAYOUTS 16
static LPSTR loaded_layouts[MAX_LOADED_LAYOUTS];
static int num_loaded_layouts;

BOOL SC2_EnsureLayout(LPCSTR filename) {
    if (!filename || !*filename) return false;

    FOR_LOOP(i, num_loaded_layouts) {
        if (!strcasecmp(loaded_layouts[i], filename))
            return true;
    }

    DWORD size = 0;
    void *buf = gi.ReadFile(filename, &size);
    if (!buf || size == 0) {
        fprintf(stderr, "SC2_Layout: failed to read '%s'\n", filename);
        return false;
    }

    xmlDocPtr doc = xmlParseMemory(buf, (int)size);
    gi.MemFree(buf);
    if (!doc) {
        fprintf(stderr, "SC2_Layout: failed to parse '%s'\n", filename);
        return false;
    }

    xmlNode *root = xmlDocGetRootElement(doc);
    if (!root || strcasecmp((char *)root->name, "Desc")) {
        fprintf(stderr, "SC2_Layout: root not <Desc> in '%s'\n", filename);
        xmlFreeDoc(doc);
        return false;
    }

    for (xmlNode *child = root->children; child; child = child->next) {
        if (child->type != XML_ELEMENT_NODE) continue;
        if (!strcasecmp((char *)child->name, "Frame"))
            ParseFrameNode(child, NULL);
    }

    xmlFreeDoc(doc);
    fprintf(stderr, "SC2_Layout: loaded '%s' -> %u frames\n", filename, (unsigned)sc2_num_frames);

    if (num_loaded_layouts < MAX_LOADED_LAYOUTS) {
        LPSTR copy = SC2_Alloc(strlen(filename) + 1);
        if (copy) { strcpy(copy, filename); loaded_layouts[num_loaded_layouts++] = copy; }
    }
    return true;
}

/* Frame lookup */
LPSC2FRAMEDEF SC2_FindFrame(LPCSTR name) {
    if (!name || !*name) return NULL;
    FOR_LOOP(i, sc2_num_frames) {
        if (!strcasecmp(sc2_frames[i].Name, name))
            return &sc2_frames[i];
    }
    return NULL;
}

LPSC2FRAMEDEF SC2_FindChildFrame(LPCSC2FRAMEDEF parent, LPCSTR name) {
    if (!parent || !name || !*name) return NULL;
    FOR_LOOP(i, sc2_num_frames) {
        if (sc2_frames[i].Parent == parent && !strcasecmp(sc2_frames[i].Name, name))
            return &sc2_frames[i];
    }
    return NULL;
}

LPSC2FRAMEDEF SC2_FindFrameByNumber(DWORD number) {
    FOR_LOOP(i, sc2_num_frames) {
        if ((DWORD)sc2_frames[i].resolved_index == number)
            return &sc2_frames[i];
    }
    return NULL;
}

/* Frame helpers */
void SC2_InitFrame(LPSC2FRAMEDEF frame, FRAMETYPE type) {
    if (!frame) return;
    memset(frame, 0, sizeof(*frame));
    frame->Type = type;
    frame->Color = (COLOR32){ 255, 255, 255, 255 };
    frame->Alpha = 1.0f;
}

void SC2_SetPoint(LPSC2FRAMEDEF frame, uiFramePointPos_t framePointX, uiFramePointPos_t framePointY,
                  LPCSC2FRAMEDEF other, uiFramePointPos_t otherPoint, FLOAT x, FLOAT y) {
    if (!frame) return;
    frame->Points.x[framePointX].used = true;
    frame->Points.x[framePointX].targetPos = otherPoint;
    frame->Points.x[framePointX].relativeTo = other;
    frame->Points.x[framePointX].offset = x;
    frame->Points.y[framePointY].used = true;
    frame->Points.y[framePointY].targetPos = otherPoint;
    frame->Points.y[framePointY].relativeTo = other;
    frame->Points.y[framePointY].offset = y;
}

void SC2_SetAllPoints(LPSC2FRAMEDEF frame) {
    if (!frame) return;
    SC2_SetPoint(frame, FPP_MIN, FPP_MIN, NULL, FPP_MIN, 0.0f, 0.0f);
    SC2_SetPoint(frame, FPP_MAX, FPP_MAX, NULL, FPP_MAX, 0.0f, 0.0f);
}

void SC2_SetParent(LPSC2FRAMEDEF frame, LPCSC2FRAMEDEF parent) {
    if (frame) frame->Parent = parent;
}

void SC2_SetSize(LPSC2FRAMEDEF frame, FLOAT width, FLOAT height) {
    if (!frame) return;
    frame->Width = width;
    frame->Height = height;
}

void SC2_SetHidden(LPSC2FRAMEDEF frame, BOOL value) {
    if (frame) frame->hidden = value;
}

void SC2_SetEnabled(LPSC2FRAMEDEF frame, BOOL enabled) {
    if (frame) frame->disabled = !enabled;
}

void SC2_SetText(LPSC2FRAMEDEF frame, LPCSTR format, ...) {
    if (!frame || !format) return;
    static char buf[1024];
    va_list argptr;
    va_start(argptr, format);
    vsnprintf(buf, sizeof(buf), format, argptr);
    va_end(argptr);
    if (frame->DynamicText) { gi.MemFree(frame->DynamicText); frame->DynamicText = NULL; frame->DynamicTextCapacity = 0; }
    size_t len = strlen(buf);
    frame->DynamicText = gi.MemAlloc(len + 1);
    if (frame->DynamicText) { memcpy(frame->DynamicText, buf, len + 1); frame->DynamicTextCapacity = (DWORD)(len + 1); frame->Text = frame->DynamicText; }
}

/* Init / shutdown */
void SC2_HudInit(void) {
    sc2_num_frames = 0;
    memset(sc2_frames, 0, sizeof(sc2_frames));
}

void SC2_HudShutdown(void) {
    FOR_LOOP(i, num_loaded_layouts) {
        if (loaded_layouts[i]) SC2_Free(loaded_layouts[i]);
    }
    num_loaded_layouts = 0;
    FOR_LOOP(i, sc2_num_frames) {
        if (sc2_frames[i].DynamicText) {
            gi.MemFree(sc2_frames[i].DynamicText);
            sc2_frames[i].DynamicText = NULL;
            sc2_frames[i].DynamicTextCapacity = 0;
        }
    }
    sc2_num_frames = 0;
    memset(sc2_frames, 0, sizeof(sc2_frames));
}
