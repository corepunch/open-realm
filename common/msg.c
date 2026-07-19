#include <stdarg.h>

#include "common.h"

#define NETF(type, x) #x,((uint8_t *)&((type*)0)->x - (uint8_t *)NULL)

typedef enum {
    NFT_BYTE,
    NFT_SHORT,
    NFT_LONG,
    NFT_FLOAT,
    NFT_ROUND,
    NFT_PACKED_FLOAT,
    NFT_QUATERNION,
    NFT_VECTOR2,
    NFT_VECTOR3,
    NFT_VECTOR3_FLOAT,
    NFT_ANGLE,
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
#ifdef WOW
    { NETF(entityState_t, origin), NFT_VECTOR3_FLOAT },
#else
    { NETF(entityState_t, origin), NFT_VECTOR3 },
#endif
    { NETF(entityState_t, angle), NFT_ANGLE },
    { NETF(entityState_t, rotation), NFT_VECTOR3 },
    { NETF(entityState_t, scale), NFT_PACKED_FLOAT },
    { NETF(entityState_t, frame), NFT_LONG },
    { NETF(entityState_t, model), NFT_SHORT },
    { NETF(entityState_t, model2), NFT_SHORT },
#ifdef WOW
    { NETF(entityState_t, appearance), NFT_LONG },
    { NETF(entityState_t, equipment), NFT_LONG },
#endif
    { NETF(entityState_t, image), NFT_SHORT },
    { NETF(entityState_t, player), NFT_BYTE },
    { NETF(entityState_t, flags), NFT_LONG },
    { NETF(entityState_t, radius), NFT_ROUND },
    { NETF(entityState_t, splat), NFT_LONG },
    { NETF(entityState_t, shadow), NFT_SHORT },
    { NETF(entityState_t, shadow_rect), NFT_LONG },
    { NETF(entityState_t, stats), NFT_LONG },
    { NULL }
};

/* Wire fields for a single UI frame (uiFrame_t).
 *
 * The server generates all UI and sends each frame to the client as a
 * delta-encoded uiFrame_t — only fields that changed since the last
 * transmission are included.  Conceptually each frame carries:
 *
 *   parent        — parent frame index (UI_PARENT = 255 for layer root)
 *   flagsvalue    — frame type (FT_TEXT, FT_BACKDROP, FT_COMMANDBUTTON, …)
 *                   packed with alpha mode
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
 *   text          — static display string (label, button caption, …)
 *   tooltip       — tooltip string shown on hover
 *   onclick       — server command sent back when the element is clicked
 *                   (e.g. "button Amov")
 *
 * A small type-specific buffer is appended after these base fields for
 * backdrop edge textures, button states, label alignment, etc.
 * See UI_WriteFrame() in src/game/ui/ui_write.c.
 */
netField_t uiFrameFields[] = {
    { NETF(uiFrame_t, parent), NFT_SHORT },
    { NETF(uiFrame_t, flagsvalue), NFT_SHORT },
    { NETF(uiFrame_t, points.x[FPP_MIN]), NFT_LONG },
    { NETF(uiFrame_t, points.x[FPP_MID]), NFT_LONG },
    { NETF(uiFrame_t, points.x[FPP_MAX]), NFT_LONG },
    { NETF(uiFrame_t, points.y[FPP_MIN]), NFT_LONG },
    { NETF(uiFrame_t, points.y[FPP_MID]), NFT_LONG },
    { NETF(uiFrame_t, points.y[FPP_MAX]), NFT_LONG },
    { NETF(uiFrame_t, size), NFT_VECTOR2 },
    { NETF(uiFrame_t, tex.index), NFT_LONG },
    { NETF(uiFrame_t, tex.coord), NFT_LONG },
    { NETF(uiFrame_t, stat), NFT_BYTE },
    { NETF(uiFrame_t, color), NFT_LONG },
    { NETF(uiFrame_t, text), NFT_TEXT },
    { NETF(uiFrame_t, tooltip), NFT_TEXT },
    { NETF(uiFrame_t, onclick), NFT_TEXT },
    { NULL }
};

netField_t playerStateFields[] = {
    { NETF(PLAYER, viewquat), NFT_QUATERNION },
#ifdef WOW
    { NETF(PLAYER, viewangles), NFT_VECTOR3_FLOAT },
#endif
    { NETF(PLAYER, origin), NFT_VECTOR2 },
    { NETF(PLAYER, fov), NFT_BYTE },
    { NETF(PLAYER, distance), NFT_ROUND },
    { NETF(PLAYER, rdflags), NFT_LONG },
    { NETF(PLAYER, uiflags), NFT_LONG },
    { NETF(PLAYER, client_ui_state), NFT_LONG },
    { NETF(PLAYER, cinematic_portrait), NFT_LONG },
    { NETF(PLAYER, team), NFT_BYTE },
    { NETF(PLAYER, color), NFT_BYTE },
    { NETF(PLAYER, race), NFT_BYTE },
    { NETF(PLAYER, name), NFT_DUPTEXT },
    { NETF(PLAYER, start_location), NFT_LONG },
    { NETF(PLAYER, cinefade), NFT_FLOAT },
    { NETF(PLAYER, stats[0]), NFT_LONG },
    { NETF(PLAYER, stats[2]), NFT_LONG },
    { NETF(PLAYER, stats[4]), NFT_LONG },
    { NETF(PLAYER, stats[6]), NFT_LONG },
    { NETF(PLAYER, stats[8]), NFT_LONG },
    { NETF(PLAYER, stats[16]), NFT_LONG },
    { NETF(PLAYER, stats[18]), NFT_LONG },
    { NETF(PLAYER, stats[20]), NFT_LONG },
    { NETF(PLAYER, texts[0]), NFT_DUPTEXT },
    { NETF(PLAYER, texts[1]), NFT_DUPTEXT },
    { NETF(PLAYER, texts[2]), NFT_DUPTEXT },
    { NETF(PLAYER, texts[3]), NFT_DUPTEXT },
    { NETF(PLAYER, texts[4]), NFT_DUPTEXT },
    { NETF(PLAYER, texts[5]), NFT_DUPTEXT },
    { NETF(PLAYER, texts[6]), NFT_DUPTEXT },
    { NETF(PLAYER, texts[7]), NFT_DUPTEXT },
    { NULL }
};
void MSG_Write(LPSIZEBUF buf, LPCVOID value, DWORD size) {
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

void MSG_WriteByte(LPSIZEBUF buf, int value) {
    char val = value;
    MSG_Write(buf, &val, 1);
}

void MSG_WriteShort(LPSIZEBUF buf, int value) {
    short val = value;
    MSG_Write(buf, &val, 2);
}

void MSG_WriteLong(LPSIZEBUF buf, int value) {
    MSG_Write(buf, &value, 4);
}

void MSG_WriteFloat(LPSIZEBUF buf, float value) {
    MSG_Write(buf, &value, 4);
}

void MSG_WriteFloat2(LPSIZEBUF buf, float value) {
    MSG_WriteShort(buf, value * 0xffff);
}

void MSG_WriteString(LPSIZEBUF buf, LPCSTR value) {
    MSG_Write(buf, value, (int)strlen(value) + 1);
}

void MSG_WritePos(LPSIZEBUF buf, LPCVECTOR3 pos) {
    MSG_WriteShort(buf, pos->x);
    MSG_WriteShort(buf, pos->y);
    MSG_WriteShort(buf, pos->z);
}

void MSG_ReadPos(LPSIZEBUF buf, LPVECTOR3 pos) {
    pos->x = MSG_ReadShort(buf);
    pos->y = MSG_ReadShort(buf);
    pos->z = MSG_ReadShort(buf);
}

void MSG_WriteDir(LPSIZEBUF buf, LPCVECTOR3 dir) {
    MSG_WriteFloat(buf, dir->x);
    MSG_WriteFloat(buf, dir->y);
    MSG_WriteFloat(buf, dir->z);
}

void MSG_ReadDir(LPSIZEBUF buf, LPVECTOR3 dir) {
    dir->x = MSG_ReadFloat(buf);
    dir->y = MSG_ReadFloat(buf);
    dir->z = MSG_ReadFloat(buf);
}

void MSG_WriteAngle(LPSIZEBUF buf, float f) {
    MSG_WriteByte(buf, (int)(f*256/(2*M_PI))&0xff);
}

float MSG_ReadAngle(LPSIZEBUF buf) {
    return MSG_ReadByte(buf)*(2*M_PI)/256;
}

int MSG_Read(LPSIZEBUF buf, HANDLE value, DWORD size) {
    if (buf->readcount + size > buf->cursize) {
        return 0;
    }
    memcpy(value, buf->data + buf->readcount, size);
    buf->readcount += size;
    return size;
}

int MSG_ReadByte(LPSIZEBUF buf) {
    char value = 0;
    MSG_Read(buf, &value, 1);
    return value;
}

int MSG_ReadShort(LPSIZEBUF buf) {
    short value = 0;
    MSG_Read(buf, &value, 2);
    return value;
}

int MSG_ReadLong(LPSIZEBUF buf) {
    int value = 0;
    MSG_Read(buf, &value, 4);
    return value;
}

float MSG_ReadFloat(LPSIZEBUF buf) {
    float value = 0;
    MSG_Read(buf, &value, 4);
    return value;
}

void MSG_ReadString(LPSIZEBUF buf, LPSTR value) {
    for (int c = MSG_ReadByte(buf), i = 0;; c = MSG_ReadByte(buf), i++) {
        value[i] = c;
        if (c == 0)
            break;
    }
}

void MSG_ReadStringN(LPSIZEBUF buf, LPSTR value, int maxlen) {
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

LPCSTR MSG_ReadString2(LPSIZEBUF buf) {
    static char buffer[2048];
    MSG_ReadString(buf, buffer);
    return buffer;
}

static DWORD MSG_GetBits(void const *from,
                         void const *to,
                         netField_t *fields)
{
    DWORD bits = 0;
    for (netField_t *field = fields; field->name; field++) {
        int *fromF = (int *)((uint8_t *)from + field->offset);
        int *toF = (int *)((uint8_t *)to + field->offset);
        switch (field->type) {
            case NFT_VECTOR2:
                if (memcmp(fromF, toF, sizeof(VECTOR2))!=0) bits |= 1 << (field - fields);
                break;
            case NFT_VECTOR3:
            case NFT_VECTOR3_FLOAT:
                if (memcmp(fromF, toF, sizeof(VECTOR3))!=0) bits |= 1 << (field - fields);
                break;
            case NFT_QUATERNION:
                if (memcmp(fromF, toF, sizeof(QUATERNION))!=0) bits |= 1 << (field - fields);
                break;
            default:
                if (*fromF != *toF) {
                    if ((field->type == NFT_TEXT || field->type == NFT_DUPTEXT) && **((LPCSTR *)toF) == 0) {
                        continue;
                    }
                    bits |= 1 << (field - fields);
                }
                break;
        }
    }
    return bits;
}

static void MSG_WriteFields(LPSIZEBUF msg,
                            void const *to,
                            netField_t *fields,
                            DWORD bits)
{
    for (netField_t *field = fields; field->name; field++) {
        if ((bits & (1 << (field - fields))) == 0)
            continue;
        int *toF = (int *)((uint8_t *)to + field->offset);
        FLOAT *_float = (FLOAT *)toF;
        switch (field->type) {
            case NFT_FLOAT: MSG_WriteFloat(msg, *_float); break;
            case NFT_ROUND: MSG_WriteShort(msg, *_float); break;
            case NFT_PACKED_FLOAT: MSG_WriteShort(msg, *_float * 500); break;
            case NFT_ANGLE: MSG_WriteShort(msg, *_float / 360 * 0xffff); break;
            case NFT_LONG: MSG_WriteLong(msg, *toF); break;
            case NFT_SHORT: MSG_WriteShort(msg, *toF); break;
            case NFT_BYTE: MSG_WriteByte(msg, *toF); break;
            case NFT_TEXT: MSG_WriteString(msg, *(LPCSTR *)toF); break;
            case NFT_DUPTEXT: MSG_WriteString(msg, *(LPCSTR *)toF); break;
            case NFT_VECTOR2: FOR_LOOP(i, 2) MSG_WriteFloat(msg, _float[i]); break;
            case NFT_VECTOR3: FOR_LOOP(i, 3) MSG_WriteShort(msg, _float[i]); break;
            case NFT_VECTOR3_FLOAT: FOR_LOOP(i, 3) MSG_WriteFloat(msg, _float[i]); break;
            case NFT_QUATERNION: FOR_LOOP(i, 4) MSG_WriteShort(msg, _float[i] * 32767); break;
        }
    }
}

static void MSG_ReadFields(LPSIZEBUF msg,
                           void const *edict,
                           netField_t *fields,
                           DWORD bits)
{
    for (netField_t *field = fields; field->name; field++) {
        if ((bits & (1 << (field - fields))) == 0)
            continue;
        int *toF = (int *)((uint8_t *)edict + field->offset);
        FLOAT *_float = (FLOAT *)toF;
        switch (field->type) {
            case NFT_FLOAT: *_float = MSG_ReadFloat(msg); break;
            case NFT_ROUND: *_float = MSG_ReadShort(msg); break;
            case NFT_PACKED_FLOAT: *_float = MSG_ReadShort(msg) / 500.f; break;
            case NFT_ANGLE: *_float = MSG_ReadShort(msg) * 360.f / 0xffff; break;
            case NFT_LONG: *toF = MSG_ReadLong(msg); break;
            case NFT_SHORT: *toF = MSG_ReadShort(msg); break;
            case NFT_BYTE: *toF = MSG_ReadByte(msg); break;
            case NFT_TEXT:
                *((LPCSTR *)toF) = (LPCSTR)(msg->data + msg->readcount);
                while (*(msg->data+(msg->readcount++)));
                break;
            case NFT_DUPTEXT:
                if (*((LPSTR *)toF)) {
                    MemFree(*((LPSTR *)toF));
                }
                *((LPCSTR *)toF) = strdup((LPCSTR)(msg->data + msg->readcount));
                while (*(msg->data+(msg->readcount++)));
                break;
            case NFT_VECTOR2: FOR_LOOP(i, 2) _float[i] = MSG_ReadFloat(msg); break;
            case NFT_VECTOR3: FOR_LOOP(i, 3) _float[i] = MSG_ReadShort(msg); break;
            case NFT_VECTOR3_FLOAT: FOR_LOOP(i, 3) _float[i] = MSG_ReadFloat(msg); break;
            case NFT_QUATERNION:
                FOR_LOOP(i, 4) _float[i] = ((float)MSG_ReadShort(msg)) / 32767;
                break;
        }
    }
}

void MSG_WriteDeltaEntity(LPSIZEBUF msg,
                          LPCENTITYSTATE from,
                          LPCENTITYSTATE to,
                          bool force)
{
    DWORD bits = MSG_GetBits(from, to, entityStateFields);
    if (bits == 0 && !force)
        return;
    MSG_WriteEntityBits(msg, bits, to->number);
    MSG_WriteFields(msg, to, entityStateFields, bits);
}

void MSG_ReadDeltaEntity(LPSIZEBUF msg,
                         LPENTITYSTATE edict,
                         int number,
                         int bits)
{
    edict->number = number;
    MSG_ReadFields(msg, edict, entityStateFields, bits);
}

void MSG_WriteDeltaUIFrame(LPSIZEBUF msg,
                           LPCUIFRAME from,
                           LPCUIFRAME to,
                           bool force)
{
    DWORD bits = MSG_GetBits(from, to, uiFrameFields);
    if (bits == 0 && !force)
        return;
    MSG_WriteEntityBits(msg, bits, to->number);
    MSG_WriteFields(msg, to, uiFrameFields, bits);
}

void MSG_ReadDeltaUIFrame(LPSIZEBUF msg,
                          LPUIFRAME edict,
                          int number,
                          int bits)
{
    edict->number = number;
    MSG_ReadFields(msg, edict, uiFrameFields, bits);
}

void MSG_WriteDeltaPlayerState(LPSIZEBUF msg,
                               LPCPLAYER from,
                               LPCPLAYER to)
{
    DWORD bits = MSG_GetBits(from, to, playerStateFields);
    MSG_WritePlayerBits(msg, bits, to->number);
    MSG_WriteFields(msg, to, playerStateFields, bits);
}

void MSG_ReadDeltaPlayerState(LPSIZEBUF msg,
                              LPPLAYER edict,
                              int number,
                              int bits)
{
    edict->number = number;
    MSG_ReadFields(msg, edict, playerStateFields, bits);
}

void SZ_Printf(LPSIZEBUF msg, LPCSTR fmt, ...) {
    va_list argptr;
    int written;

    if (msg->cursize >= msg->maxsize) {
        msg->overflowed = true;
        return;
    }

    va_start(argptr, fmt);
    written = vsnprintf((LPSTR)(msg->data + msg->cursize), msg->maxsize - msg->cursize, fmt, argptr);
    va_end(argptr);

    if (written < 0 || (DWORD)written >= msg->maxsize - msg->cursize) {
        fprintf(stderr,
                "Write buffer overflow (msg): maxsize=%u cursize=%u\n",
                (unsigned)msg->maxsize,
                (unsigned)msg->cursize);
        msg->overflowed = true;
        return;
    }
    msg->cursize += written + 1;
}

void MSG_WriteEntityBits(LPSIZEBUF buf, DWORD bits, DWORD number) {
    MSG_WriteLong(buf, bits);
    MSG_WriteShort(buf, number);
}

int MSG_ReadEntityBits(LPSIZEBUF buf, DWORD *bits) {
    *bits = MSG_ReadLong(buf);
    return MSG_ReadShort(buf);
}

void MSG_WritePlayerBits(LPSIZEBUF buf, DWORD bits, DWORD number) {
    MSG_WriteLong(buf, bits);
    MSG_WriteShort(buf, number);
}

int MSG_ReadPlayerBits(LPSIZEBUF buf, DWORD *bits) {
    *bits = MSG_ReadLong(buf);
    return MSG_ReadShort(buf);
}
