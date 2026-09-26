#include "client.h"

#define MAX_MISSILES 64
#define MAX_SPELL_IMPACTS 32
#define MAX_FLOATING_TEXTS 64      /* entries; bounds simultaneous transient world labels */
#define MAX_ENTITY_INDICATORS 32     /* entries; repeated calls for one entity reuse its slot */
#define FLOATING_TEXT_CAPACITY 32  /* bytes including terminator; numeric gains are much shorter */
#define SPELL_IMPACT_LIFETIME 800  /* ms — one-shot birth animation duration */
#define ENTITY_INDICATOR_LIFETIME 1000 /* ms — retail AddIndicator flashes twice */
#define ENTITY_INDICATOR_PERIOD 500    /* ms per flash cycle */
#define ENTITY_INDICATOR_ON_TIME 250   /* ms visible at the start of each cycle */

typedef struct  {
    vector3_t origin;
    uint32_t timespamp;
    color32_t tint;
} moveConfirmation_t;

typedef enum {
    MISSILE_FREE,
    MISSILE_NORMAL,
} mistype_t;

typedef struct {
    mistype_t type;
    vector3_t origin;
    float angle;
    float speed;
    uint32_t model;
    uint32_t starttime;
    uint32_t killtime;
} missile_t;

typedef struct {
    bool active;
    vector3_t origin;
    uint32_t model;       /* configstring model index */
    uint32_t starttime;
    uint32_t lifetime;    /* ms */
} spellImpact_t;

typedef struct {
    bool active;
    uint32_t entity;
    color32_t color;
    uint32_t starttime;
} entityIndicator_t;

typedef struct {
    bool active;
    vector3_t origin;
    char text[FLOATING_TEXT_CAPACITY];
    color32_t color;
    uint32_t font;        /* configstring font index */
    uint32_t starttime;
    uint32_t lifetime;    /* ms */
    uint32_t fade_start;  /* ms from spawn */
    float velocity_x;  /* screen pixels per second */
    float velocity_y;  /* screen pixels per second; positive rises */
} floatingText_t;

struct {
    missile_t missiles[MAX_MISSILES];
    spellImpact_t impacts[MAX_SPELL_IMPACTS];
    entityIndicator_t indicators[MAX_ENTITY_INDICATORS];
    floatingText_t texts[MAX_FLOATING_TEXTS];
} tents = { 0 };

moveConfirmation_t cl_confs[MAX_CONFIRMATION_OBJECTS] = { 0 };
uint32_t cl_confcounter = 0;

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

static entityIndicator_t *CL_AllocIndicator(uint32_t entity) {
    entityIndicator_t *oldest = &tents.indicators[0];

    FOR_LOOP(i, MAX_ENTITY_INDICATORS) {
        entityIndicator_t *indicator = &tents.indicators[i];
        if (indicator->active && indicator->entity == entity) return indicator;
        if (!indicator->active) return indicator;
        if (indicator->starttime < oldest->starttime) oldest = indicator;
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

void CL_AllocateConfirmationObject(vector3_t const * origin, color32_t tint) {
    uint32_t i = cl_confcounter++;
    cl_confs[i % MAX_CONFIRMATION_OBJECTS].origin = *origin;
    cl_confs[i % MAX_CONFIRMATION_OBJECTS].timespamp = cl.time;
    cl_confs[i % MAX_CONFIRMATION_OBJECTS].tint = tint;
}

void CL_ParseTEnt(sizeBuf_t * msg) {
    vector3_t pos;//, pos2, dir;
    tempEvent_t evt = MSG_ReadByte(msg);
    missile_t *missile;
    switch (evt) {
        case TE_MOVE_CONFIRMATION:
            MSG_ReadPos(msg, &pos);
            CL_AllocateConfirmationObject(&pos, (color32_t){ 0, 255, 0, 255 });
            break;
        case TE_ATTACK_CONFIRMATION:
            MSG_ReadPos(msg, &pos);
            CL_AllocateConfirmationObject(&pos, (color32_t){ 255, 0, 0, 255 });
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
        case TE_ENTITY_INDICATOR:
            {
                entityIndicator_t *indicator;
                int32_t number = MSG_ReadLong(msg);
                uint32_t packed = (uint32_t)MSG_ReadLong(msg);

                if (number <= 0 || number >= MAX_CLIENT_ENTITIES) break;
                indicator = CL_AllocIndicator((uint32_t)number);
                indicator->entity = (uint32_t)number;
                indicator->color = MAKE(color32_t,
                    packed & 0xffu, (packed >> 8) & 0xffu,
                    (packed >> 16) & 0xffu, (packed >> 24) & 0xffu);
                indicator->starttime = cl.time;
                indicator->active = indicator->color.a != 0;
            }
            break;
        case TE_FLOATING_TEXT:
            {
                floatingText_t *text = CL_AllocFloatingText();
                uint32_t packed;
                int32_t font, lifetime, fade_start;

                memset(text, 0, sizeof(*text));
                MSG_ReadPos(msg, &text->origin);
                MSG_ReadStringN(msg, text->text, sizeof(text->text));
                packed = (uint32_t)MSG_ReadLong(msg);
                text->color = MAKE(color32_t,
                    packed & 0xffu, (packed >> 8) & 0xffu,
                    (packed >> 16) & 0xffu, (packed >> 24) & 0xffu);
                font = MSG_ReadShort(msg);
                text->font = font > 0 && font < MAX_FONTSTYLES ? (uint32_t)font : 0;
                lifetime = MSG_ReadLong(msg);
                fade_start = MSG_ReadLong(msg);
                text->lifetime = lifetime > 0 ? (uint32_t)lifetime : 0;
                text->fade_start = fade_start > 0 ? (uint32_t)fade_start : 0;
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

/* Build the transient point marker payload; the renderer owns support-surface lookup for bridge geometry. */
static renderEntity_t CL_BuildConfirmationEntity(moveConfirmation_t const *mc, model_t * model) {
    renderEntity_t ent;
    memset(&ent, 0, sizeof(ent));
    ent.origin = mc->origin;
    ent.ground_offset = 8.0f;
    ent.origin.z = CM_GetHeightAtPoint(ent.origin.x, ent.origin.y) + ent.ground_offset;
    ent.scale = 1;
    ent.frame = cl.time - mc->timespamp;
    ent.oldframe = cl.time - mc->timespamp;
    ent.model = model;
    ent.tint = mc->tint;
    /* Confirmation art is a world-space ground marker. Let the WC3 renderer
     * replace terrain Z with authored walkable-surface Z when the point lies
     * on a live bridge, while preserving the existing +8 visual offset. */
    ent.flags |= RF_GROUND_CONFORM | RF_NO_FOGOFWAR | RF_NO_SHADOW | RF_NO_LIGHTING;
    return ent;
}

static void CL_AddConfirmationObject(moveConfirmation_t const *mc) {
    renderEntity_t ent;
    if (!cl.moveConfirmation) return; /* An empty server media slot disables the marker. */
    ent = CL_BuildConfirmationEntity(mc, cl.moveConfirmation);
    V_AddEntity(&ent);
}

void CL_AddMissile(missile_t const *missile) {
    vector3_t dir = { cos(missile->angle), sin(missile->angle), 0 };
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
        uint32_t age = cl.time - imp->starttime;
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
    float const pixel_x = window.width ? SCR_UICanvasWidth() / (float)window.width : 0.0f;
    float const pixel_y = window.height ? UI_BASE_HEIGHT / (float)window.height : 0.0f;

    FOR_LOOP(i, MAX_FLOATING_TEXTS) {
        floatingText_t *text = &tents.texts[i];
        vector2_t screen;
        uint32_t age;
        float seconds, alpha = 1.0f;
        color32_t color, shadow;
        rect_t rect;

        if (!text->active) continue;
        age = cl.time - text->starttime;
        if (age >= text->lifetime) {
            text->active = false;
            continue;
        }
        if (!text->font || !cl.fonts[text->font] || !SCR_ProjectWorldPoint(&text->origin, &screen))
            continue;

        if (age >= text->fade_start && text->lifetime > text->fade_start) {
            alpha = (float)(text->lifetime - age) /
                    (float)(text->lifetime - text->fade_start);
        }
        seconds = (float)age / 1000.0f;
        screen.x += text->velocity_x * seconds * pixel_x;
        screen.y -= text->velocity_y * seconds * pixel_y;
        color = text->color;
        color.a = (uint8_t)(color.a * MAX(0.0f, MIN(1.0f, alpha)));
        shadow = MAKE(color32_t, 0, 0, 0, color.a);

        /* Warsmash's built-in gain labels are left-origin text with a small
         * dark drop shadow; the game, not this generic client path, supplies
         * resource-specific colour/font/timing. */
        rect = MAKE(rect_t, screen.x + 3.0f * pixel_x, screen.y + 1.0f * pixel_y,
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

#ifdef BZ_TESTS
#include "shared/test.h"

TEST(client_tent, confirmation_carries_walkable_ground_conform_contract) {
    model_t model = { 0 };
    moveConfirmation_t const confirmation = {
        .origin = { 128.0f, 256.0f, 0.0f },
        .timespamp = 100,
        .tint = { 0, 255, 0, 255 },
    };
    uint32_t const old_time = cl.time;
    renderEntity_t ent;

    cl.time = 250;
    ent = CL_BuildConfirmationEntity(&confirmation, &model);

    T_ASSERT(ent.flags & RF_GROUND_CONFORM);
    T_ASSERT(ent.model == &model);
    T_FEQ(ent.ground_offset, 8.0f, 0.001f);
    T_FEQ(ent.origin.x, confirmation.origin.x, 0.001f);
    T_FEQ(ent.origin.y, confirmation.origin.y, 0.001f);
    T_EQ(ent.frame, 150);
    T_EQ(ent.oldframe, 150);

    cl.time = old_time;
}
#endif

void CL_ApplyIndicator(renderEntity_t *ent) {
    if (!ent || !ent->number) return;

    FOR_LOOP(i, MAX_ENTITY_INDICATORS) {
        entityIndicator_t *indicator = &tents.indicators[i];
        uint32_t age;

        if (!indicator->active) continue;
        age = cl.time - indicator->starttime;
        if (age >= ENTITY_INDICATOR_LIFETIME) {
            indicator->active = false;
            continue;
        }
        if (indicator->entity != ent->number) continue;
        if (age % ENTITY_INDICATOR_PERIOD < ENTITY_INDICATOR_ON_TIME) {
            ent->indicator = indicator->color;
        }
        return;
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
