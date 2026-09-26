#include <stdarg.h>

#include "common.h"

#define NETF(type, x) #x,((uint8_t *)&((type*)0)->x - (uint8_t *)NULL)
#define MSG_FIELDBIT(field, fields) (1u << ((field) - (fields)))
#define MSG_FIELD_COUNT(arr) ((int)(sizeof(arr) / sizeof((arr)[0]) - 1))

typedef enum {
    NFT_BYTE,
    NFT_SHORT,
    NFT_LONG,
    NFT_FLOAT,
    NFT_ROUND,
    NFT_PACKED_FLOAT,
    NFT_QUATERNION,
    NFT_VECTOR2,
    NFT_BOX2,
    NFT_VECTOR3,
    NFT_VECTOR3_FLOAT,
    NFT_ANGLE, /* Radians, one turn in 65536 unsigned wire steps. */
    NFT_TEXT,
    NFT_DUPTEXT,
} netFieldType_t;

typedef struct {
    char *name;
    long offset;
    netFieldType_t type;
} netField_t;

netField_t entityStateFields[] = {
    { NETF(entityState_t, class_id), NFT_LONG },
    /* Whole-unit coordinates turned fractional RTS movement into visible grid steps. */
    { NETF(entityState_t, origin), NFT_VECTOR3_FLOAT },
    { NETF(entityState_t, angle), NFT_ANGLE },
#ifdef WOW
    { NETF(entityState_t, rotation), NFT_VECTOR3 },
#endif
    { NETF(entityState_t, scale), NFT_PACKED_FLOAT },
    { NETF(entityState_t, frame), NFT_LONG },
    { NETF(entityState_t, model), NFT_SHORT },
    { NETF(entityState_t, model2), NFT_SHORT },
    { NETF(entityState_t, effect), NFT_BYTE },
    { NETF(entityState_t, effect_flags), NFT_SHORT },
#ifdef WOW
    { NETF(entityState_t, appearance), NFT_LONG },
    { NETF(entityState_t, equipment), NFT_LONG },
#endif
    { NETF(entityState_t, image), NFT_SHORT },
    { NETF(entityState_t, name), NFT_SHORT },
    { NETF(entityState_t, hover_value), NFT_LONG },
    { NETF(entityState_t, player), NFT_BYTE },
    { NETF(entityState_t, flags), NFT_SHORT },
    { NETF(entityState_t, renderfx), NFT_BYTE },
    { NETF(entityState_t, ability), NFT_BYTE },
    { NETF(entityState_t, pathing_width), NFT_SHORT },
    { NETF(entityState_t, pathing_height), NFT_SHORT },
    { NETF(entityState_t, pathing_preview), NFT_LONG },
    /* Preserve both sub-unit selection circles and large building radii; integer rounding erased small rings. */
    { NETF(entityState_t, radius), NFT_FLOAT },
    /* Collision is a world-unit radius and can exceed the packed-float range.
     * It changes rarely, so preserve the exact value rather than rounding it. */
    { NETF(entityState_t, collision), NFT_FLOAT },
    { NETF(entityState_t, ground_offset), NFT_FLOAT },
    { NETF(entityState_t, splat), NFT_LONG },
#ifndef USE_SHADOWMAPS
    { NETF(entityState_t, shadow), NFT_SHORT },
    { NETF(entityState_t, shadow_rect), NFT_LONG },
#endif
    { NETF(entityState_t, stats), NFT_LONG },
    { NETF(entityState_t, event), NFT_BYTE },
    { NETF(entityState_t, sound), NFT_SHORT },
    { NULL }
};

/* Wire fields for a single UI frame (uiFrame_t).
 *
 * The server generates all UI and sends each frame to the client as a
 * delta-encoded uiFrame_t — only fields that changed since the last
 * transmission are included.  Conceptually each frame carries:
 *
 *   parent        — parent frame index (UI_PARENT = 255 for layer root)
 *   flagsvalue    — frame type (FT_TEXT, FT_BACKDROP, FT_COMMANDBUTTON, …),
 *                   alpha mode, and generic retained-layout behavior flags
 *   x / y         — position on each axis; internally stored as three
 *                   4-byte uiFramePoint_t slots per axis (FPP_MIN/MID/MAX
 *                   = left/center/right for x, top/middle/bottom for y).
 *                   Each slot packs: used (1 b), targetPos (7 b),
 *                   relativeTo frame index (8 b), and offset (16 b, int) —
 *                   the offset is the coordinate multiplied by
 *                   UI_FRAMEPOINT_SCALE (32767), i.e. 0.02 → 655.
 *                   Typically one slot per axis is set; two slots allow
 *                   the frame to stretch between two anchor points.
 *   size          — explicit width/height in normalised screen units
 *                   (viewport is 0.8 × 0.6)
 *   tex.index     — texture/"pic" index resolved from the MPQ
 *   tex.coord     — UV sub-rectangle inside the texture (4 bytes:
 *                   xmin, xmax, ymin, ymax scaled to 0-255)
 *   stat          — player stat index shown as a live number;
 *                   0 means use the text string instead
 *   color         — RGBA tint
 *   text          — type-specific string (display text for text frames; optional
 *                   secondary click command for command buttons)
 *   tooltip       — tooltip string shown on hover
 *   onclick       — server command sent back when the element is clicked
 *                   (e.g. "button Amov")
 *
 * A small type-specific buffer is appended after these base fields for
 * backdrop edge textures, button states, label alignment, etc.
 * See UI_WriteFrame() in games/warcraft-3/game/hud/hud_write.c.
 */
netField_t uiFrameFields[] = {
    { NETF(uiFrame_t, parent), NFT_SHORT },
    { NETF(uiFrame_t, flagsvalue), NFT_LONG },
    { NETF(uiFrame_t, points.x[FPP_MIN]), NFT_LONG },
    { NETF(uiFrame_t, points.x[FPP_MID]), NFT_LONG },
    { NETF(uiFrame_t, points.x[FPP_MAX]), NFT_LONG },
    { NETF(uiFrame_t, points.y[FPP_MIN]), NFT_LONG },
    { NETF(uiFrame_t, points.y[FPP_MID]), NFT_LONG },
    { NETF(uiFrame_t, points.y[FPP_MAX]), NFT_LONG },
    { NETF(uiFrame_t, size), NFT_VECTOR2 },
    { NETF(uiFrame_t, textLength), NFT_SHORT },
    { NETF(uiFrame_t, tex.index), NFT_LONG },
    { NETF(uiFrame_t, tex.coord), NFT_LONG },
    { NETF(uiFrame_t, stat), NFT_BYTE },
    { NETF(uiFrame_t, color), NFT_LONG },
    { NETF(uiFrame_t, text), NFT_TEXT },
    { NETF(uiFrame_t, tooltip), NFT_TEXT },
    { NETF(uiFrame_t, onclick), NFT_TEXT },
    { NETF(uiFrame_t, hotkey), NFT_BYTE },
    { NETF(uiFrame_t, value), NFT_FLOAT },
    { NULL }
};

/* Player-state deltas use a 32-bit mask; map metadata moved to configstrings, leaving room for generic camera fields. */
netField_t playerStateFields[] = {
    { NETF(player_t, viewangles), NFT_VECTOR3_FLOAT },
    { NETF(player_t, vieworigin), NFT_VECTOR3_FLOAT },
    { NETF(player_t, fov), NFT_FLOAT },
    /* distance, znear, zfar are consecutive FLOATs packed as one VECTOR3 field. */
    { NETF(player_t, distance), NFT_VECTOR3_FLOAT },
    { NETF(player_t, rdflags), NFT_LONG },
    { NETF(player_t, uiflags), NFT_LONG },
    { NETF(player_t, client_ui_state), NFT_LONG },
    /* cinematic_portrait, team, color, race are consecutive BYTEs; one int32_t keeps the 32-bit mask. */
    { NETF(player_t, cinematic_portrait), NFT_LONG },
    { NETF(player_t, name), NFT_DUPTEXT },
    { NETF(player_t, start_location), NFT_LONG },
    { NETF(player_t, cinefade), NFT_FLOAT },
    { NETF(player_t, stats[0]), NFT_LONG },
    { NETF(player_t, stats[2]), NFT_LONG },
    { NETF(player_t, stats[4]), NFT_LONG },
    { NETF(player_t, stats[6]), NFT_LONG },
    { NETF(player_t, stats[8]), NFT_LONG },
    { NETF(player_t, stats[16]), NFT_LONG },
    /* stats[18..22] are generic live-selection HUD bindings; stats[23] is a
     * generic environment-presentation variant paired with the final slot. */
    { NETF(player_t, stats[18]), NFT_LONG },
    { NETF(player_t, stats[20]), NFT_LONG },
    { NETF(player_t, stats[22]), NFT_LONG },
    { NETF(player_t, stats[23]), NFT_LONG },
    { NETF(player_t, texts[0]), NFT_DUPTEXT },
    { NETF(player_t, texts[1]), NFT_DUPTEXT },
    /* Map metadata moved to the WoW map-info configstring; only cinematic text remains in player state. */
    { NULL }
};

_Static_assert(MSG_FIELD_COUNT(playerStateFields) <= 32, "player-state delta mask is 32 bits");
_Static_assert(MSG_FIELD_COUNT(entityStateFields) <= 32, "entity-state delta mask is 32 bits");
_Static_assert(MSG_FIELD_COUNT(uiFrameFields) <= 32, "ui-frame delta mask is 32 bits");

void MSG_Write(sizeBuf_t *buf, void const *value, uint32_t size) {
    if (buf->cursize + size > buf->maxsize) {
        fprintf(stderr,
                "Write buffer overflow (msg): size=%u cursize=%u maxsize=%u\n",
                (unsigned)size,
                (unsigned)buf->cursize,
                (unsigned)buf->maxsize);
        buf->overflowed = true;
        return;
    }
    memcpy(buf->data + buf->cursize, value, size);
    buf->cursize += size;
}

void MSG_WriteByte(sizeBuf_t *buf, int value) {
    uint8_t val = (uint8_t)value;
    MSG_Write(buf, &val, 1);
}

void MSG_WriteShort(sizeBuf_t *buf, int value) {
    short val = value;
    MSG_Write(buf, &val, 2);
}

void MSG_WriteLong(sizeBuf_t *buf, int value) {
    MSG_Write(buf, &value, 4);
}

void MSG_WriteFloat(sizeBuf_t *buf, float value) {
    MSG_Write(buf, &value, 4);
}

void MSG_WriteFloat2(sizeBuf_t *buf, float value) {
    MSG_WriteShort(buf, value * 0xffff);
}

void MSG_WriteString(sizeBuf_t *buf, cstring_t value) {
    MSG_Write(buf, value, (int)strlen(value) + 1);
}

void MSG_WritePos(sizeBuf_t *buf, vector3_t const *pos) {
    MSG_WriteShort(buf, pos->x);
    MSG_WriteShort(buf, pos->y);
    MSG_WriteShort(buf, pos->z);
}

void MSG_ReadPos(sizeBuf_t *buf, vector3_t *pos) {
    pos->x = MSG_ReadShort(buf);
    pos->y = MSG_ReadShort(buf);
    pos->z = MSG_ReadShort(buf);
}

void MSG_WriteDir(sizeBuf_t *buf, vector3_t const *dir) {
    MSG_WriteFloat(buf, dir->x);
    MSG_WriteFloat(buf, dir->y);
    MSG_WriteFloat(buf, dir->z);
}

void MSG_ReadDir(sizeBuf_t *buf, vector3_t *dir) {
    dir->x = MSG_ReadFloat(buf);
    dir->y = MSG_ReadFloat(buf);
    dir->z = MSG_ReadFloat(buf);
}

void MSG_WriteAngle(sizeBuf_t *buf, float f) {
    MSG_WriteByte(buf, (int)(f*256/(2*M_PI))&0xff);
}

float MSG_ReadAngle(sizeBuf_t *buf) {
    return MSG_ReadByte(buf)*(2*M_PI)/256;
}

int MSG_Read(sizeBuf_t *buf, handle_t value, uint32_t size) {
    if (buf->readcount + size > buf->cursize) {
        return 0;
    }
    memcpy(value, buf->data + buf->readcount, size);
    buf->readcount += size;
    return size;
}

int MSG_ReadByte(sizeBuf_t *buf) {
    uint8_t value = 0;
    MSG_Read(buf, &value, 1);
    return value;
}

int MSG_ReadShort(sizeBuf_t *buf) {
    short value = 0;
    MSG_Read(buf, &value, 2);
    return value;
}

int MSG_ReadLong(sizeBuf_t *buf) {
    int value = 0;
    MSG_Read(buf, &value, 4);
    return value;
}

float MSG_ReadFloat(sizeBuf_t *buf) {
    float value = 0;
    MSG_Read(buf, &value, 4);
    return value;
}

void MSG_ReadString(sizeBuf_t *buf, string_t value) {
    for (int c = MSG_ReadByte(buf), i = 0;; c = MSG_ReadByte(buf), i++) {
        value[i] = c;
        if (c == 0)
            break;
    }
}

void MSG_ReadStringN(sizeBuf_t *buf, string_t value, int maxlen) {
    int i = 0;
    for (;;) {
        int c = MSG_ReadByte(buf);
        if (c == 0)
            break;
        if (i < maxlen - 1)
            value[i++] = (char)c;
    }
    value[i] = '\0';
}

cstring_t MSG_ReadString2(sizeBuf_t *buf) {
    static char buffer[2048];
    MSG_ReadString(buf, buffer);
    return buffer;
}

static uint32_t MSG_GetBits(void const *from, void const *to, netField_t *fields, bool text_offsets)
{
    uint32_t bits = 0;
    for (netField_t *field = fields; field->name; field++) {
        int *fromF = (int *)((uint8_t *)from + field->offset);
        int *toF = (int *)((uint8_t *)to + field->offset);
        switch (field->type) {
            case NFT_VECTOR2:
                if (memcmp(fromF, toF, sizeof(vector2_t))!=0) bits |= MSG_FIELDBIT(field, fields);
                break;
            case NFT_BOX2:
                if (memcmp(fromF, toF, sizeof(box2_t))!=0) bits |= MSG_FIELDBIT(field, fields);
                break;
            case NFT_VECTOR3:
            case NFT_VECTOR3_FLOAT:
                if (memcmp(fromF, toF, sizeof(vector3_t))!=0) bits |= MSG_FIELDBIT(field, fields);
                break;
            case NFT_QUATERNION:
                if (memcmp(fromF, toF, sizeof(quaternion_t))!=0) bits |= MSG_FIELDBIT(field, fields);
                break;
            case NFT_BYTE:
                if (*(uint8_t *)fromF != *(uint8_t *)toF) bits |= MSG_FIELDBIT(field, fields);
                break;
            case NFT_SHORT:
                if (*(uint16_t *)fromF != *(uint16_t *)toF) bits |= MSG_FIELDBIT(field, fields);
                break;
            default:
                if (*fromF != *toF) {
                    if (!text_offsets && (field->type == NFT_TEXT || field->type == NFT_DUPTEXT) && **((cstring_t *)toF) == 0) {
                        continue;
                    }
                    bits |= MSG_FIELDBIT(field, fields);
                }
                break;
        }
    }
    return bits;
}

static void MSG_WriteFields(sizeBuf_t *msg,
                            void const *to,
                            netField_t *fields,
                            uint32_t bits,
                            bool text_offsets)
{
    for (netField_t *field = fields; field->name; field++) {
        if ((bits & MSG_FIELDBIT(field, fields)) == 0)
            continue;
        int *toF = (int *)((uint8_t *)to + field->offset);
        float *_float = (float *)toF;
        switch (field->type) {
            case NFT_FLOAT: MSG_WriteFloat(msg, *_float); break;
            case NFT_ROUND: MSG_WriteShort(msg, *_float); break;
            case NFT_PACKED_FLOAT: MSG_WriteShort(msg, *_float * 500); break;
            case NFT_ANGLE:
                MSG_WriteShort(msg, (int)floor(fmod(*_float, 2 * M_PI) * (65536 / (2 * M_PI)) + 0.5) & 0xffff);
                break;
            case NFT_LONG: MSG_WriteLong(msg, *toF); break;
            case NFT_SHORT: MSG_WriteShort(msg, *(uint16_t *)toF); break;
            case NFT_BYTE: MSG_WriteByte(msg, *(uint8_t *)toF); break;
            case NFT_TEXT:
                if (text_offsets) MSG_WriteLong(msg, *toF);
                else MSG_WriteString(msg, *(cstring_t *)toF);
                break;
            case NFT_DUPTEXT: MSG_WriteString(msg, *(cstring_t *)toF); break;
            case NFT_VECTOR2: FOR_LOOP(i, 2) MSG_WriteFloat(msg, _float[i]); break;
            case NFT_BOX2: FOR_LOOP(i, 4) MSG_WriteFloat(msg, _float[i]); break;
            case NFT_VECTOR3: FOR_LOOP(i, 3) MSG_WriteShort(msg, _float[i]); break;
            case NFT_VECTOR3_FLOAT: FOR_LOOP(i, 3) MSG_WriteFloat(msg, _float[i]); break;
            case NFT_QUATERNION: FOR_LOOP(i, 4) MSG_WriteShort(msg, _float[i] * 32767); break;
        }
    }
}

static void MSG_ReadFields(sizeBuf_t *msg,
                           void const *edict,
                           netField_t *fields,
                           uint32_t bits,
                           bool text_offsets)
{
    for (netField_t *field = fields; field->name; field++) {
        if ((bits & MSG_FIELDBIT(field, fields)) == 0)
            continue;
        int *toF = (int *)((uint8_t *)edict + field->offset);
        float *_float = (float *)toF;
        switch (field->type) {
            case NFT_FLOAT: *_float = MSG_ReadFloat(msg); break;
            case NFT_ROUND: *_float = MSG_ReadShort(msg); break;
            case NFT_PACKED_FLOAT: *_float = MSG_ReadShort(msg) / 500.f; break;
            case NFT_ANGLE: *_float = (uint16_t)MSG_ReadShort(msg) * (2 * M_PI / 65536); break;
            case NFT_LONG: *toF = MSG_ReadLong(msg); break;
            case NFT_SHORT: *(uint16_t *)toF = (uint16_t)MSG_ReadShort(msg); break;
            case NFT_BYTE: *(uint8_t *)toF = (uint8_t)MSG_ReadByte(msg); break;
            case NFT_TEXT:
                if (text_offsets) *toF = MSG_ReadLong(msg);
                else {
                    *((cstring_t *)toF) = (cstring_t)(msg->data + msg->readcount);
                    while (*(msg->data+(msg->readcount++)));
                }
                break;
            case NFT_DUPTEXT:
                if (*((string_t *)toF)) {
                    MemFree(*((string_t *)toF));
                }
                *((cstring_t *)toF) = strdup((cstring_t)(msg->data + msg->readcount));
                while (*(msg->data+(msg->readcount++)));
                break;
            case NFT_VECTOR2: FOR_LOOP(i, 2) _float[i] = MSG_ReadFloat(msg); break;
            case NFT_BOX2: FOR_LOOP(i, 4) _float[i] = MSG_ReadFloat(msg); break;
            case NFT_VECTOR3: FOR_LOOP(i, 3) _float[i] = MSG_ReadShort(msg); break;
            case NFT_VECTOR3_FLOAT: FOR_LOOP(i, 3) _float[i] = MSG_ReadFloat(msg); break;
            case NFT_QUATERNION:
                FOR_LOOP(i, 4) _float[i] = ((float)MSG_ReadShort(msg)) / 32767;
                break;
        }
    }
}

void MSG_WriteDeltaEntity(sizeBuf_t *msg,
                          entityState_t const *from,
                          entityState_t const *to,
                          bool force)
{
    /* Unchanged snapshots dominate static scenes; the old path walked every wire field before discovering no delta. */
    if (!force && memcmp(from, to, sizeof(*to)) == 0)
        return;
    uint32_t bits = MSG_GetBits(from, to, entityStateFields, false);
    if (bits == 0 && !force)
        return;
    MSG_WriteEntityBits(msg, (uint32_t)bits, to->number);
    MSG_WriteFields(msg, to, entityStateFields, bits, false);
}

void MSG_ReadDeltaEntity(sizeBuf_t *msg,
                         entityState_t *edict,
                         int number,
                         int bits)
{
    edict->number = number;
    MSG_ReadFields(msg, edict, entityStateFields, bits, false);
}

void MSG_WriteDeltaUIFrame(sizeBuf_t *msg,
                           uiFrame_t const *from,
                           uiFrame_t const *to,
                           bool force)
{
    uint32_t bits = MSG_GetBits(from, to, uiFrameFields, false);
    if (bits == 0 && !force)
        return;
    MSG_WriteEntityBits(msg, (uint32_t)bits, to->number);
    MSG_WriteFields(msg, to, uiFrameFields, bits, false);
}

void MSG_ReadDeltaUIFrame(sizeBuf_t *msg,
                          uiFrame_t *edict,
                          int number,
                          int bits)
{
    edict->number = number;
    MSG_ReadFields(msg, edict, uiFrameFields, bits, false);
}

/* Window frame strings are uint32_t offsets into the packet's trailing text arena. */
void MSG_WriteDeltaUIWindowFrame(sizeBuf_t *msg, uiFrame_t const *from, uiFrame_t const *to, bool force) {
    uint32_t bits = MSG_GetBits(from, to, uiFrameFields, true);
    if (!bits && !force) return;
    MSG_WriteEntityBits(msg, (uint32_t)bits, to->number);
    MSG_WriteFields(msg, to, uiFrameFields, bits, true);
}

bool MSG_ReadDeltaUIWindowFrame(sizeBuf_t *msg, uiFrame_t *edict, int number, int bits) {
    uint32_t size = 0, known = 0;

    for (netField_t *field = uiFrameFields; field->name; field++) {
        uint32_t bit = 1u << (field - uiFrameFields);
        if (!(bits & bit)) continue;
        known |= bit;
        switch (field->type) {
            case NFT_BYTE: size += 1; break;
            case NFT_ROUND: case NFT_PACKED_FLOAT: case NFT_ANGLE: case NFT_SHORT: size += 2; break;
            case NFT_FLOAT: case NFT_LONG: case NFT_TEXT: size += 4; break;
            case NFT_VECTOR2: size += 8; break;
            case NFT_BOX2: size += 16; break;
            case NFT_VECTOR3: size += 6; break;
            case NFT_VECTOR3_FLOAT: size += 12; break;
            case NFT_QUATERNION: size += 8; break;
            default: return false;
        }
    }
    if (((uint32_t)bits & ~known) || msg->readcount > msg->cursize || size > msg->cursize - msg->readcount) return false;
    edict->number = number;
    MSG_ReadFields(msg, edict, uiFrameFields, bits, true);
    return true;
}

void MSG_WriteDeltaPlayerState(sizeBuf_t *msg,
                               player_t const *from,
                               player_t const *to)
{
    uint32_t bits = MSG_GetBits(from, to, playerStateFields, false);
    MSG_WritePlayerBits(msg, bits, to->number);
    MSG_WriteFields(msg, to, playerStateFields, bits, false);
}

void MSG_ReadDeltaPlayerState(sizeBuf_t *msg,
                              player_t *edict,
                              int number,
                              int bits)
{
    edict->number = number;
    MSG_ReadFields(msg, edict, playerStateFields, bits, false);
}

void SZ_Printf(sizeBuf_t *msg, cstring_t fmt, ...) {
    va_list argptr;
    int written;

    if (msg->cursize >= msg->maxsize) {
        msg->overflowed = true;
        return;
    }

    va_start(argptr, fmt);
    written = vsnprintf((string_t)(msg->data + msg->cursize), msg->maxsize - msg->cursize, fmt, argptr);
    va_end(argptr);

    if (written < 0 || (uint32_t)written >= msg->maxsize - msg->cursize) {
        fprintf(stderr,
                "Write buffer overflow (msg): maxsize=%u cursize=%u\n",
                (unsigned)msg->maxsize,
                (unsigned)msg->cursize);
        msg->overflowed = true;
        return;
    }
    msg->cursize += written + 1;
}

void MSG_WriteEntityBits(sizeBuf_t *buf, uint32_t bits, uint32_t number) {
    MSG_WriteLong(buf, bits);
    MSG_WriteShort(buf, number);
}

int MSG_ReadEntityBits(sizeBuf_t *buf, uint32_t *bits) {
    *bits = MSG_ReadLong(buf);
    return MSG_ReadShort(buf);
}

void MSG_WritePlayerBits(sizeBuf_t *buf, uint32_t bits, uint32_t number) {
    MSG_WriteLong(buf, bits);
    MSG_WriteShort(buf, number);
}

int MSG_ReadPlayerBits(sizeBuf_t *buf, uint32_t *bits) {
    *bits = MSG_ReadLong(buf);
    return MSG_ReadShort(buf);
}

/* Each controller operation carries only its typed payload; no native struct padding goes on the wire. */
void MSG_WriteInput(sizeBuf_t *buf, inputCmd_t const *cmd) {
    MSG_WriteByte(buf, cmd->action);
    switch (cmd->action) {
    case BZ_INPUT_FOCUS:
        MSG_WriteFloat(buf, cmd->focus.x); MSG_WriteFloat(buf, cmd->focus.y);
        break;
    case BZ_INPUT_VIEW:
        MSG_WriteDir(buf, &cmd->view.angles); MSG_WriteFloat(buf, cmd->view.distance);
        break;
    case BZ_INPUT_MOVE:
        MSG_WriteByte(buf, cmd->move.buttons); MSG_WriteShort(buf, cmd->move.msec);
        break;
    }
}

/* Validate the complete operation before the server passes it to an authoritative player edict. */
bool MSG_ReadInput(sizeBuf_t *buf, inputCmd_t *cmd) {
    static uint32_t const sizes[] = { 8, 16, 3 };
    if (buf->readcount >= buf->cursize) return false;
    *cmd = (inputCmd_t){ .action = MSG_ReadByte(buf) };
    if (cmd->action > BZ_INPUT_MOVE || sizes[cmd->action] > buf->cursize - buf->readcount) return false;
    switch (cmd->action) {
    case BZ_INPUT_FOCUS:
        cmd->focus = (vector2_t){ MSG_ReadFloat(buf), MSG_ReadFloat(buf) };
        return isfinite(cmd->focus.x) && isfinite(cmd->focus.y);
    case BZ_INPUT_VIEW:
        MSG_ReadDir(buf, &cmd->view.angles); cmd->view.distance = MSG_ReadFloat(buf);
        return isfinite(cmd->view.angles.x) && isfinite(cmd->view.angles.y) && isfinite(cmd->view.angles.z) &&
            isfinite(cmd->view.distance) && cmd->view.distance >= 0;
    case BZ_INPUT_MOVE:
        cmd->move.buttons = MSG_ReadByte(buf); cmd->move.msec = (uint16_t)MSG_ReadShort(buf);
        return !(cmd->move.buttons & ~BZ_INPUT_MOVE_MASK) && cmd->move.msec <= BZ_INPUT_MAX_MSEC;
    }
    return false;
}
