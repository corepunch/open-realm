/*
 * g_monster.c — Unit and monster shared behavior.
 *
 * This file owns the per-unit animation driver (M_MoveFrame), the waypoint
 * pool used for move orders (Waypoint_add), and unit initialization
 * (SP_SpawnUnit) which reads unit stats from the data tables and sets up
 * combat parameters, models, and collision radii.
 *
 * The think function registered on every unit entity is monster_think(),
 * which advances the current animation frame and calls the active umove_t
 * think callback each game tick.
 */
#include "g_local.h"

#define MAX_WAYPOINTS 256
static edict_t waypoints[MAX_WAYPOINTS];
DWORD current_waypoint = 0;

LPCSTR attack_type[] = {
    "none",
    "normal",
    "pierce",
    "siege",
    "spells",
    "chaos",
    "magic",
    "hero",
    NULL
};

/* WC3 defType enum order (matches the damage-table columns). */
LPCSTR defense_type[] = {
    "small",
    "medium",
    "large",
    "fort",
    "normal",
    "hero",
    "divine",
    "none",
    NULL
};

LPCSTR weapon_type[] = {
    "none",
    "normal",
    "instant",
    "artillery",
    "aline",
    "missile",
    "msplash",
    "mbounce",
    "mline",
    NULL
};

DWORD FindEnumValue(LPCSTR value, LPCSTR values[]) {
    if (!value)
        return 0;
    for (LPCSTR *s = values; *s; s++) {
        if (!strcmp(*s, value)) {
            return (DWORD)(s - values);
        }
    }
    return 0;
}

static FLOAT get_unit_collision(pathTex_t const *pathtex) {
    int size = 0;
    for (int x = 0; x < pathtex->width; x++) {
        if (pathtex->map[(pathtex->width + 1) * x].b)
            size++;
    }
    return size * 16 * 1.3;
}

/* Allocate a waypoint entity at the given map location.
 * The waypoint's Z coordinate is set from the terrain height so that units
 * moving toward it will hug the ground.  The pool is a circular array of
 * MAX_WAYPOINTS slots, so old waypoints are silently recycled. */
LPEDICT Waypoint_add(LPCVECTOR2 spot) {
    LPEDICT waypoint = &waypoints[current_waypoint++ % MAX_WAYPOINTS];
    waypoint->s.origin.x = spot->x;
    waypoint->s.origin.y = spot->y;
    waypoint->heatmap2 = 0;
    waypoint->secondarygoal = NULL;
    waypoint->collision = 0;
    M_CheckGround(waypoint);
    return waypoint;
}

BOOL player_pay(LPPLAYER ps, DWORD project) {
    if (!ps) return false;
    if (UNIT_GOLD_COST(project) > ps->stats[PLAYERSTATE_RESOURCE_GOLD]) return false;
    if (UNIT_LUMBER_COST(project) > ps->stats[PLAYERSTATE_RESOURCE_LUMBER]) return false;
    ps->stats[PLAYERSTATE_RESOURCE_GOLD] -= UNIT_GOLD_COST(project);
    ps->stats[PLAYERSTATE_RESOURCE_LUMBER] -= UNIT_LUMBER_COST(project);
    return true;
}

BOOL M_IsDead(LPEDICT ent) {
    return ent->health.value <= 0;
}

/* Per-sim-tick budget on expensive flow-field bakes. Under heavy combat many
 * units chase many moving targets; without a hard cap the bakes exceed the tick
 * budget and the fixed-timestep catch-up loop death-spirals permanently. When
 * the budget is spent, callers reuse a stale flow field (or fall back to direct
 * steering if none exists). Reset each tick in G_RunFrame. */
DWORD g_heatmap_builds_this_frame = 0;
#define HEATMAP_BUILD_BUDGET 2

DWORD M_RefreshHeatmap(LPEDICT self) {
    LPEDICT route = self && self->secondarygoal ? self->secondarygoal : self;

    if (!route) {
        return 0;
    }
    /* For fixed waypoints (non-moving goals) the flow field only needs to be
     * computed once — reuse the cached result from the first call.  Monster
     * targets move, but rebuilding their heatmap every call (per chasing unit,
     * every tick) is catastrophic — the flow field is shared by all chasers, so
     * cache it on the target and rebuild only when the target has actually moved
     * past ~half a pathing cell. */
    if (route->heatmap2 && CM_ActivateCachedFlow(route->heatmap2)) {
        if (!(route->svflags & SVF_MONSTER)) {
            return route->heatmap2;
        }
        BOOL const moved = Vector2_distance(&route->s.origin2, &route->heatmap2_origin) >= 64.0f;
        BOOL const stale = (DWORD)(level.time - route->heatmap2_time) >= 400;
        if (!moved || !stale) {
            return route->heatmap2;
        }
        /* Wants a rebuild, but defer if the per-tick bake budget is spent. */
        if (g_heatmap_builds_this_frame >= HEATMAP_BUILD_BUDGET) {
            return route->heatmap2;
        }
    } else if (g_heatmap_builds_this_frame >= HEATMAP_BUILD_BUDGET) {
        /* No usable cache and budget spent: skip this tick. Caller (chasing a
         * moving target) falls back to direct steering toward the goal. */
        return route->heatmap2;
    }
    g_heatmap_builds_this_frame++;
    route->heatmap2 = CM_BuildHeatmap(route);
    route->heatmap2_origin = route->s.origin2;
    route->heatmap2_time = level.time;
    return route->heatmap2;
}

/* Advance the unit's animation frame by FRAMETIME milliseconds.
 * If the new frame would exceed the animation's end interval, the current
 * umove_t endfunc is called (e.g. to loop the walk cycle or transition to
 * the cooldown phase after an attack). */
void M_MoveFrame(LPEDICT self) {
    if (self->aiflags & AI_HOLD_FRAME)
        return;
    umove_t const *move = self->currentmove;
    LPCANIMATION anim = self->animation;
    if (!anim) {
        unit_setmove(self, self->currentmove);
        anim = self->animation;
        if (!anim) {
            return;
        }
    }
    DWORD next_frame = self->s.frame + FRAMETIME;
    if (!strcmp(anim->name, "birth")) {
        DWORD anim_len = anim->interval[1] - anim->interval[0];
        DWORD build_time = UNIT_BUILD_TIME_MSEC(self->class_id);
        if (build_time > 0) {
            next_frame = self->s.frame + FRAMETIME * anim_len / build_time;
        }
    }
    if (self->s.frame < anim->interval[0] ||
        self->s.frame >= anim->interval[1])
    {
        self->s.frame = anim->interval[0] ;
    } else if (next_frame >= anim->interval[1]) {
        SAFE_CALL(move->endfunc, self);
        if (!(self->aiflags & AI_HOLD_FRAME)) {
            self->s.frame = anim->interval[0] ;
        }
    } else {
        self->s.frame = next_frame;
    }
}

/* Per-unit think function registered on every monster/unit entity.
 * Called each game frame by G_RunEntity; drives the animation clock and
 * invokes the active umove_t think callback (e.g. ai_walk, ai_melee). */
void monster_think(LPEDICT self) {
    if (!self->currentmove)
        return;
    if (self->paused || self->stunned)
        return;
    M_MoveFrame(self);
    if (self->currentmove->think) {
        self->currentmove->think(self);
    }
}

void monster_start(LPEDICT self) {
    LPCANIMATION anim = self->animation;
    if (anim) {
        DWORD len = MAX(1, anim->interval[1] - anim->interval[0] - 1);
        self->s.frame = (anim->interval[0] + (rand() % len));
    }
}

//unitRace_t M_GetRace(LPCSTR string) {
//    if (!strcmp(string, STR_HUMAN)) return RACE_HUMAN;
//    if (!strcmp(string, STR_ORC)) return RACE_ORC;
//    if (!strcmp(string, STR_UNDEAD)) return RACE_UNDEAD;
//    if (!strcmp(string, STR_NIGHTELF)) return RACE_NIGHTELF;
//    if (!strcmp(string, STR_DEMON)) return RACE_DEMON;
//    if (!strcmp(string, STR_CREEPS)) return RACE_CREEPS;
//    if (!strcmp(string, STR_CRITTERS)) return RACE_CRITTERS;
//    if (!strcmp(string, STR_OTHER)) return RACE_OTHER;
//    if (!strcmp(string, STR_COMMONER)) return RACE_COMMONER;
//    return RACE_UNKNOWN;
//}


struct jpeg_imageinfo {
    int width;
    int height;
    int channels;
    DWORD size;
    int num_components;
    BYTE *data;
};

pathTex_t *M_LoadPathTex(LPCSTR filename) {
    pathTex_t *pathTex = NULL;
    if (filename && strlen(filename) > 1) {
        DWORD filesize;
        HANDLE buffer = gi.ReadFile(filename, &filesize);
        if (buffer) {
            pathTex = LoadTGA(buffer, filesize);
        }
        gi.MemFree(buffer);
        return pathTex;
    }
    return NULL;
}

DWORD M_LoadUberSplat(LPCSTR uber_splat) {
    if (IS_FOURCC(uber_splat)) {
        LPCSTR dir = FS_FindSheetCell(game.config.uberSplats, uber_splat, "dir");
        LPCSTR file = FS_FindSheetCell(game.config.uberSplats, uber_splat, "file");
        LPCSTR scale = FS_FindSheetCell(game.config.uberSplats, uber_splat, "scale");
        PATHSTR filename;
        snprintf(filename, sizeof(PATHSTR), "%s\\%s.blp", dir, file);
        return gi.ImageIndex(filename) | (atoi(scale) << 16);
    } else {
        return 0;
    }
}

static BOOL G_FileExists(LPCSTR filename) {
    DWORD filesize = 0;
    HANDLE buffer = gi.ReadFile(filename, &filesize);
    if (buffer) {
        gi.MemFree(buffer);
        return true;
    }
    return false;
}

static BOOL G_HasShadowName(LPCSTR shadow) {
    return shadow && shadow[0] && strcmp(shadow, "_");
}

DWORD G_LoadShadowTexture(LPCSTR shadow, BOOL allowDDSFallback) {
    PATHSTR filename;

    if (!G_HasShadowName(shadow)) {
        return 0;
    }

    snprintf(filename, sizeof(filename), "ReplaceableTextures\\Shadows\\%s.blp", shadow);
    if (G_FileExists(filename)) {
        return gi.ImageIndex(filename);
    }

    if (allowDDSFallback) {
        snprintf(filename, sizeof(filename), "ReplaceableTextures\\Shadows\\%s.dds", shadow);
        if (G_FileExists(filename)) {
            return gi.ImageIndex(filename);
        }
    }

    return 0;
}

static void M_SetUnitShadow(LPEDICT self) {
    LPCSTR unit_shadow = UNIT_SHADOW_IMAGE_UNIT(self->class_id);
    DWORD shadow = G_LoadShadowTexture(unit_shadow, true);
    if (!shadow) {
        shadow = G_LoadShadowTexture("Shadow", true);
    }
    if (!shadow) {
        return;
    }

    self->s.shadow = shadow;
    FLOAT shadow_x = UNIT_SHADOW_IMAGE_CENTER_X(self->class_id);
    FLOAT shadow_y = UNIT_SHADOW_IMAGE_CENTER_Y(self->class_id);
    FLOAT shadow_w = UNIT_SHADOW_IMAGE_WIDTH(self->class_id);
    FLOAT shadow_h = UNIT_SHADOW_IMAGE_HEIGHT(self->class_id);
    if (shadow_w <= 0 || shadow_h <= 0) {
        FLOAT size = MAX(72, UNIT_SELECTION_SCALE(self->class_id) * SEL_SCALE);
        shadow_x = size * 0.5f;
        shadow_y = size * 0.5f;
        shadow_w = size;
        shadow_h = size;
    }
    self->s.shadow_rect = ShadowPackRect(shadow_x, shadow_y, shadow_w, shadow_h);
}

static void M_SetBuildingShadow(LPEDICT self) {
    LPCSTR building_shadow = UNIT_BUILDING_SHADOW(self->class_id);
    DWORD shadow = G_LoadShadowTexture(building_shadow, false);
    if (!shadow) {
        if (G_HasShadowName(UNIT_SHADOW_IMAGE_UNIT(self->class_id))) {
            M_SetUnitShadow(self);
        }
        return;
    }

    self->s.shadow = shadow;
    self->s.shadow_rect = 0;
}

/* Initialize a unit entity from the unit data tables.
 * Reads model path, scale, collision radius, HP, mana, and attack parameters
 * (type, weapon class, damage dice, range, projectile model/speed) for the
 * unit's class_id and stores them in the edict. */
void SP_SpawnUnit(LPEDICT self) {
    PATHSTR model_filename;
    LPCSTR uber_splat = UNIT_UBER_SPLAT(self->class_id);
    LPCSTR path_tex = UNIT_PATH_TEX(self->class_id);
    sprintf(model_filename, "%s.mdx", UNIT_MODEL(self->class_id));
    self->s.model = G_RegisterModel(model_filename);
    self->s.splat = M_LoadUberSplat(uber_splat);
    if (UNIT_IS_BUILDING(self->class_id)) {
        M_SetBuildingShadow(self);
    } else {
        M_SetUnitShadow(self);
    }
    self->s.scale = UNIT_SCALING_VALUE(self->class_id);
    self->s.radius = UNIT_SELECTION_SCALE(self->class_id) * SEL_SCALE / 2;
    /* Unit-vs-unit separation uses the authentic collisionSize ('ucol') from
     * the unit data, matching WC3. Buildings have no meaningful collisionSize
     * and instead block via their pathing footprint (set from pathtex below). */
    {
        FLOAT const ucol = (FLOAT)UNIT_COLLISION(self->class_id);
        self->collision = ucol > 0.0f ? ucol : self->s.radius;
    }
//    printf("%.4s\n", &self->class_id);
    self->targtype = G_GetTargetType(UNIT_TARGETED_AS(self->class_id));
    if (UNIT_OCCLUDER_HEIGHT(self->class_id) > 0) {
        self->s.flags |= EF_FOW_BLOCKER;
    }
    if (UNIT_SIGHT_RADIUS(self->class_id) > 0 || UNIT_SIGHT_RADIUS_NIGHT(self->class_id) > 0) {
        self->s.flags |= EF_FOW_REVEALER;
    }
    self->mana.value = UNIT_MANA(self->class_id);
    self->mana.max_value = UNIT_MANA(self->class_id);
    self->health.value = UNIT_HP(self->class_id);
    self->health.max_value = UNIT_HP(self->class_id);
    self->invulnerable = G_ActorHasSkill(self, "Avul");
    self->unitinfo.MoveSpeed = UNIT_SPEED(self->class_id);
    self->balance.sight_radius.day = UNIT_SIGHT_RADIUS(self->class_id);
    self->balance.sight_radius.night = UNIT_SIGHT_RADIUS_NIGHT(self->class_id);
    self->think = monster_think;
    self->svflags |= SVF_MONSTER;
    
    self->defense_type = FindEnumValue(UNIT_DEFENSE_TYPE_NAME(self->class_id), defense_type);
    self->armor_value = UNIT_ARMOR_VALUE(self->class_id);
    self->attack1.type = FindEnumValue(UNIT_ATTACK1_ATTACK_TYPE(self->class_id), attack_type);
    self->attack1.weapon = FindEnumValue(UNIT_ATTACK1_WEAPON_TYPE(self->class_id), weapon_type);
    self->attack1.damageBase = UNIT_ATTACK1_DAMAGE_BASE(self->class_id);
    self->attack1.numberOfDice = UNIT_ATTACK1_DAMAGE_NUMBER_OF_DICE(self->class_id);
    self->attack1.sidesPerDie = UNIT_ATTACK1_DAMAGE_SIDES_PER_DIE(self->class_id);
    self->attack1.cooldown = UNIT_ATTACK1_BASE_COOLDOWN(self->class_id);
    self->attack1.damagePoint = UNIT_ATTACK1_DAMAGE_POINT(self->class_id);
    self->attack1.range = UNIT_ATTACK1_RANGE(self->class_id);
    
    if (self->attack1.weapon == WPN_MISSILE) {
        self->attack1.origin.x = UNIT_ATTACK1_LAUNCH_X(self->class_id);
        self->attack1.origin.y = UNIT_ATTACK1_LAUNCH_Y(self->class_id);
        self->attack1.origin.z = UNIT_ATTACK1_LAUNCH_Z(self->class_id);
        self->attack1.projectile.model = G_RegisterModel(UNIT_ATTACK1_PROJECTILE_ART(self->class_id));
        self->attack1.projectile.arc = UNIT_ATTACK1_PROJECTILE_ARC(self->class_id);
        self->attack1.projectile.speed = UNIT_ATTACK1_PROJECTILE_SPEED(self->class_id);        
//        printf("%.4s %s\n", &self->class_id, UNIT_ATTACK1_PROJECTILE_ART(self->class_id));
    }

    if ((self->pathtex = M_LoadPathTex(path_tex))) {
        /* Buildings: collide by footprint (their collisionSize is ~0). */
        if (UNIT_IS_BUILDING(self->class_id)) {
            self->collision = get_unit_collision(self->pathtex);
        }
    }
}

void M_CheckGround(LPEDICT self) {
    self->s.origin.z = CM_GetHeightAtPoint(self->s.origin.x, self->s.origin.y);
}

BOOL M_CheckAttack(LPEDICT self) {
    return false;
}

FLOAT M_DistanceToGoal(LPEDICT ent) {
    if (ent->goalentity) {
        return Vector2_distance(&ent->goalentity->s.origin2, &ent->s.origin2);
    } else {
        return 0;
    }
}

BYTE compress_stat(EDICTSTAT const *stat) {
    if (stat->max_value <= 0) {
        return 0;
    } else {
        return 255 * stat->value / stat->max_value;
    }
}
