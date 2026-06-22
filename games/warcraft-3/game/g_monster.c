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

DWORD M_RefreshHeatmap(LPEDICT self) {
    LPEDICT route = self && self->secondarygoal ? self->secondarygoal : self;

    if (!route) {
        return 0;
    }
    /* For fixed waypoints (non-moving goals) the flow field only needs to be
     * computed once — reuse the cached result from the first call.  Monster
     * targets can move, so their heatmap is always rebuilt to stay accurate. */
    if (!(route->svflags & SVF_MONSTER) &&
        route->heatmap2 &&
        CM_ActivateCachedFlow(route->heatmap2)) {
        return route->heatmap2;
    }
    route->heatmap2 = CM_BuildHeatmap(route);
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

/* Register the first sound file for a given SLK label+suffix and return its
 * configstring index, or 0 if the entry is not found or has no files. */
static int G_RegisterSoundLabel(sheetRow_t *sounds, LPCSTR label, LPCSTR suffix) {
    char key[128];
    snprintf(key, sizeof(key), "%s%s", label, suffix);
    LPCSTR files = FS_FindSheetCell(sounds, key, "FileNames");
    LPCSTR dir   = FS_FindSheetCell(sounds, key, "DirectoryBase");
    if (!files || !files[0]) return 0;
    /* Take the first comma-separated filename. */
    char first[256];
    LPCSTR comma = strchr(files, ',');
    if (comma)
        snprintf(first, sizeof(first), "%.*s", (int)(comma - files), files);
    else
        snprintf(first, sizeof(first), "%s", files);
    char path[512];
    if (dir && dir[0])
        snprintf(path, sizeof(path), "%s%s", dir, first);
    else
        snprintf(path, sizeof(path), "%s", first);
    return gi.SoundIndex(path);
}

/* Populate the unit's cached sound indices from UnitAckSounds.slk using the
 * "unitSound" label (e.g. "Footman").  Falls back gracefully if entries are
 * missing — sounds simply won't fire for that unit. */
static void G_RegisterUnitSounds(LPEDICT self) {
    LPCSTR label = UnitStringField(UnitsMetaData, self->class_id, "usnd");
    if (!label || !label[0]) return;
    sheetRow_t *ack = game.config.unitAckSounds;
    if (!ack) return;
    self->sound_attack = G_RegisterSoundLabel(ack, label, "YesAttack");
    /* Death sounds follow the pattern {label}Death but may not exist in the
     * AckSounds SLK.  Try the SLK first; fall back to the raw file path. */
    self->sound_death = G_RegisterSoundLabel(ack, label, "Death");
    if (!self->sound_death) {
        /* Derive death sound path from model directory: units\race\Name\NameDeath.wav */
        LPCSTR model = UNIT_MODEL(self->class_id);
        if (model && model[0]) {
            char path[512];
            snprintf(path, sizeof(path), "%s\\%sDeath.wav",
                     model,           /* e.g. units\human\Footman\Footman */
                     strrchr(model, '\\') ? strrchr(model, '\\') + 1 : model);
            /* Rewrite: strip model base name from dir and append death filename. */
            LPCSTR slash = strrchr(model, '\\');
            if (slash) {
                char dir_part[256];
                snprintf(dir_part, sizeof(dir_part), "%.*s", (int)(slash - model + 1), model);
                snprintf(path, sizeof(path), "%s%sDeath.wav", dir_part, slash + 1);
            }
            self->sound_death = gi.SoundIndex(path);
        }
    }
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
    self->s.scale = UNIT_SCALING_VALUE(self->class_id);
    self->s.radius = UNIT_SELECTION_SCALE(self->class_id) * SEL_SCALE / 2;
    self->collision = self->s.radius;//UNIT_COLLISION(self->class_id);
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
        self->collision = get_unit_collision(self->pathtex);
    }
    G_RegisterUnitSounds(self);
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
