#include "client.h"

#define MAX_MISSILES 64
#define MAX_SPELL_IMPACTS 32
#define MAX_FLOATING_TEXTS 64      /* entries; bounds simultaneous transient world labels */
#define FLOATING_TEXT_CAPACITY 32  /* bytes including terminator; numeric gains are much shorter */
#define SPELL_IMPACT_LIFETIME 800  /* ms — one-shot birth animation duration */

typedef struct  {
    VECTOR3 origin;
    DWORD timespamp;
    COLOR32 tint;
} moveConfirmation_t;

typedef enum {
    MISSILE_FREE,
    MISSILE_NORMAL,
} mistype_t;

typedef struct {
    mistype_t type;
    VECTOR3 origin;
    float angle;
    float speed;
    DWORD model;
    DWORD starttime;
    DWORD killtime;
} missile_t;

typedef struct {
    BOOL active;
    VECTOR3 origin;
    DWORD model;       /* configstring model index */
    DWORD starttime;
    DWORD lifetime;    /* ms */
} spellImpact_t;

typedef struct {
    BOOL active;
    VECTOR3 origin;
    char text[FLOATING_TEXT_CAPACITY];
    COLOR32 color;
    DWORD font;        /* configstring font index */
    DWORD starttime;
    DWORD lifetime;    /* ms */
    DWORD fade_start;  /* ms from spawn */
    FLOAT velocity_x;  /* screen pixels per second */
    FLOAT velocity_y;  /* screen pixels per second; positive rises */
} floatingText_t;

struct {
    missile_t missiles[MAX_MISSILES];
    spellImpact_t impacts[MAX_SPELL_IMPACTS];
    floatingText_t texts[MAX_FLOATING_TEXTS];
} tents = { 0 };

moveConfirmation_t cl_confs[MAX_CONFIRMATION_OBJECTS] = { 0 };
DWORD cl_confcounter = 0;

/* Keep impact bursts bounded; overwrite the oldest only when every slot is active. */
static spellImpact_t *CL_AllocSpellImpact(void) {
    spellImpact_t *oldest = &tents.impacts[0];
    FOR_LOOP(i, MAX_SPELL_IMPACTS) {
        if (!tents.impacts[i].active) return &tents.impacts[i];
        if (tents.impacts[i].starttime < oldest->starttime) oldest = &tents.impacts[i];
    }
    return oldest;
}

/* Transient labels are presentation-only. If a pathological burst fills the
 * bounded pool, replacing the oldest label avoids unbounded client memory. */
static floatingText_t *CL_AllocFloatingText(void) {
    floatingText_t *oldest = &tents.texts[0];
    FOR_LOOP(i, MAX_FLOATING_TEXTS) {
        if (!tents.texts[i].active) return &tents.texts[i];
        if (tents.texts[i].starttime < oldest->starttime) oldest = &tents.texts[i];
    }
    return oldest;
}

missile_t *CL_AllocMissile(void) {
    FOR_LOOP(i, MAX_MISSILES) {
        if (tents.missiles[i].type == MISSILE_FREE) {
            return &tents.missiles[i];
        }
    }
    return tents.missiles;
}

void CL_AllocateConfirmationObject(LPCVECTOR3 origin, COLOR32 tint) {
    DWORD i = cl_confcounter++;
    cl_confs[i % MAX_CONFIRMATION_OBJECTS].origin = *origin;
    cl_confs[i % MAX_CONFIRMATION_OBJECTS].timespamp = cl.time;
    cl_confs[i % MAX_CONFIRMATION_OBJECTS].tint = tint;
}

void CL_ParseTEnt(LPSIZEBUF msg) {
    VECTOR3 pos;//, pos2, dir;
    tempEvent_t evt = MSG_ReadByte(msg);
    missile_t *missile;
    switch (evt) {
        case TE_MOVE_CONFIRMATION:
            MSG_ReadPos(msg, &pos);
            CL_AllocateConfirmationObject(&pos, (COLOR32){ 0, 255, 0, 255 });
            break;
        case TE_ATTACK_CONFIRMATION:
            MSG_ReadPos(msg, &pos);
            CL_AllocateConfirmationObject(&pos, (COLOR32){ 255, 0, 0, 255 });
            break;
        case TE_MISSILE:
            missile = CL_AllocMissile();
            MSG_ReadPos(msg, &missile->origin);
            missile->model = MSG_ReadShort(msg);
            missile->speed = MSG_ReadShort(msg);
            missile->killtime = MSG_ReadShort(msg) + cl.time;
            missile->angle = MSG_ReadAngle(msg);
            missile->starttime = cl.time;
            missile->type = MISSILE_NORMAL;
            break;
        case TE_FIREBOLT_IMPACT:
        case TE_FROSTBOLT_IMPACT:
            {
                spellImpact_t *imp = CL_AllocSpellImpact();
                MSG_ReadPos(msg, &imp->origin);
                imp->model     = MSG_ReadShort(msg);
                imp->starttime = cl.time;
                imp->lifetime  = SPELL_IMPACT_LIFETIME;
                imp->active    = true;
            }
            break;
        case TE_FLOATING_TEXT:
            {
                floatingText_t *text = CL_AllocFloatingText();
                DWORD packed;
                LONG font, lifetime, fade_start;

                memset(text, 0, sizeof(*text));
                MSG_ReadPos(msg, &text->origin);
                MSG_ReadStringN(msg, text->text, sizeof(text->text));
                packed = (DWORD)MSG_ReadLong(msg);
                text->color = MAKE(COLOR32,
                    packed & 0xffu, (packed >> 8) & 0xffu,
                    (packed >> 16) & 0xffu, (packed >> 24) & 0xffu);
                font = MSG_ReadShort(msg);
                text->font = font > 0 && font < MAX_FONTSTYLES ? (DWORD)font : 0;
                lifetime = MSG_ReadLong(msg);
                fade_start = MSG_ReadLong(msg);
                text->lifetime = lifetime > 0 ? (DWORD)lifetime : 0;
                text->fade_start = fade_start > 0 ? (DWORD)fade_start : 0;
                text->fade_start = MIN(text->fade_start, text->lifetime);
                text->velocity_x = MSG_ReadFloat(msg);
                text->velocity_y = MSG_ReadFloat(msg);
                text->starttime = cl.time;
                text->active = text->text[0] && text->lifetime > 0;
            }
            break;
        default:
            Com_Error(ERR_DROP, "CL_ParseTEnt: bad type %d", evt);
            break;
    }
}

static void CL_AddConfirmationObject(moveConfirmation_t const *mc) {
    renderEntity_t ent;
    memset(&ent, 0, sizeof(ent));
    ent.origin = mc->origin;
    ent.origin.z = CM_GetHeightAtPoint(ent.origin.x, ent.origin.y) + 8.0f;
    ent.scale = 1;
    ent.frame = cl.time - mc->timespamp;
    ent.oldframe = cl.time - mc->timespamp;
    ent.model = cl.moveConfirmation;
    ent.tint = mc->tint;
    ent.flags |= RF_NO_FOGOFWAR | RF_NO_SHADOW | RF_NO_LIGHTING;
    V_AddEntity(&ent);
}

void CL_AddMissile(missile_t const *missile) {
    VECTOR3 dir = { cos(missile->angle), sin(missile->angle), 0 };
    float distance = (cl.time - missile->starttime) * missile->speed / 1000;
    renderEntity_t ent;
    memset(&ent, 0, sizeof(ent));
    float k = (float)(cl.time -  missile->starttime) / (float)(missile->killtime - missile->starttime);
    ent.origin = Vector3_mad(&missile->origin, distance, &dir);
    ent.origin.z += sqrt(1.0 - fabs(k - 0.5) * 2.0) * 200;
    ent.scale = 1;
    ent.frame = 0;//cl.time % 1000;
    ent.oldframe = 0;//cl.time % 1000;
    ent.angle = missile->angle;
    ent.model = cl.models[missile->model];
    V_AddEntity(&ent);
}

void CL_AddConfirmations(void) {
    FOR_LOOP(i, MAX_CONFIRMATION_OBJECTS) {
        if (cl.time - cl_confs[i].timespamp > 1000)
            continue;
        CL_AddConfirmationObject(cl_confs+i);
    }
}

void CL_AddMissiles(void) {
    FOR_LOOP(i, MAX_MISSILES) {
        missile_t *missile = tents.missiles+i;
        if (missile->type == MISSILE_FREE)
            continue;
        if (missile->killtime < cl.time) {
            missile->type = MISSILE_FREE;
            continue;;
        }
        CL_AddMissile(tents.missiles+i);
    }
}


static void CL_AddSpellImpacts(void) {
    FOR_LOOP(i, MAX_SPELL_IMPACTS) {
        spellImpact_t *imp = &tents.impacts[i];
        if (!imp->active) continue;
        DWORD age = cl.time - imp->starttime;
        if (age >= imp->lifetime) { imp->active = false; continue; }
        renderEntity_t ent;
        memset(&ent, 0, sizeof(ent));
        ent.origin    = imp->origin;
        ent.scale     = 1.0f;
        ent.frame     = age;
        ent.oldframe  = age;
        ent.model     = cl.models[imp->model];
        ent.flags     = RF_GROUND_ANCHOR | RF_NO_SHADOW | RF_NO_FOGOFWAR;
        V_AddEntity(&ent);
    }
}

/* Draw generic world labels after the 3D frame so they remain presentation
 * overlays while still projecting from a stable world-space spawn point. */
void CL_DrawTEnts(void) {
    size2_t const window = re.GetWindowSize();
    FLOAT const pixel_x = window.width ? SCR_UICanvasWidth() / (FLOAT)window.width : 0.0f;
    FLOAT const pixel_y = window.height ? UI_BASE_HEIGHT / (FLOAT)window.height : 0.0f;

    FOR_LOOP(i, MAX_FLOATING_TEXTS) {
        floatingText_t *text = &tents.texts[i];
        VECTOR2 screen;
        DWORD age;
        FLOAT seconds, alpha = 1.0f;
        COLOR32 color, shadow;
        RECT rect;

        if (!text->active) continue;
        age = cl.time - text->starttime;
        if (age >= text->lifetime) {
            text->active = false;
            continue;
        }
        if (!text->font || !cl.fonts[text->font] || !SCR_ProjectWorldPoint(&text->origin, &screen))
            continue;

        if (age >= text->fade_start && text->lifetime > text->fade_start) {
            alpha = (FLOAT)(text->lifetime - age) /
                    (FLOAT)(text->lifetime - text->fade_start);
        }
        seconds = (FLOAT)age / 1000.0f;
        screen.x += text->velocity_x * seconds * pixel_x;
        screen.y -= text->velocity_y * seconds * pixel_y;
        color = text->color;
        color.a = (BYTE)(color.a * MAX(0.0f, MIN(1.0f, alpha)));
        shadow = MAKE(COLOR32, 0, 0, 0, color.a);

        /* Warsmash's built-in gain labels are left-origin text with a small
         * dark drop shadow; the game, not this generic client path, supplies
         * resource-specific colour/font/timing. */
        rect = MAKE(RECT, screen.x + 3.0f * pixel_x, screen.y + 1.0f * pixel_y,
                    SCR_UICanvasWidth(), UI_BASE_HEIGHT);
        re.DrawText(&MAKE(drawText_t,
            .font = cl.fonts[text->font], .text = text->text, .rect = rect,
            .color = shadow, .textWidth = SCR_UICanvasWidth(),
            .halign = FONT_JUSTIFYLEFT, .valign = FONT_JUSTIFYTOP));
        rect.x = screen.x;
        rect.y = screen.y;
        re.DrawText(&MAKE(drawText_t,
            .font = cl.fonts[text->font], .text = text->text, .rect = rect,
            .color = color, .textWidth = SCR_UICanvasWidth(),
            .halign = FONT_JUSTIFYLEFT, .valign = FONT_JUSTIFYTOP));
    }
}

void CL_ClearTEnts(void) {
    memset(&tents, 0, sizeof(tents));
    memset(cl_confs, 0, sizeof(cl_confs));
}

void CL_AddTEnts(void) {
    CL_AddConfirmations();
    CL_AddMissiles();
    CL_AddSpellImpacts();
}
