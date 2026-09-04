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
    /* size footprint cells wide -> radius = size * (32/2) = size*16.  The old
     * extra *1.3 inflated every building's collision circle 30% with no WC3
     * basis (buildings already block via their baked footprint). */
    return size * 16;
}

/* Reserve a body-queue-style ring in g_edicts so ordinary F_EDICT relocation owns every waypoint pointer. */
void G_InitWaypoints(void) {
    DWORD base;
    if (level.waypoints.count) return;
    base = level.waypoints.base = globals.num_edicts;
    FOR_LOOP(i, MAX_WAYPOINTS) {
        LPEDICT waypoint = G_Spawn();
        if (waypoint != g_edicts + base + i) gi.error("G_InitWaypoints: waypoint ring is not contiguous\n");
        waypoint->svflags |= SVF_NOCLIENT;
    }
    level.waypoints.count = MAX_WAYPOINTS;
}

/* Recycle one real edict from the fixed ring, matching Quake II's TRAIL/body queue ownership model. */
LPEDICT Waypoint_add(LPCVECTOR2 spot) {
    LPEDICT waypoint;
    G_InitWaypoints();
    waypoint = g_edicts + level.waypoints.base + level.waypoints.cursor;
    level.waypoints.cursor = (level.waypoints.cursor + 1) % MAX_WAYPOINTS;
    waypoint->s.origin.x = spot->x;
    waypoint->s.origin.y = spot->y;
    waypoint->heatmap2 = 0;
    waypoint->heatmap2_radius = 0;
    waypoint->secondarygoal = NULL;
    waypoint->collision = 0;
    M_CheckGround(waypoint);
    return waypoint;
}

BOOL player_pay(LPPLAYER ps, DWORD project) {
    UnitBalance_t const *b;
    if (!ps) return false;
    b = G_UnitBalance(project);
    if (b->goldCost > ps->stats[PLAYERSTATE_RESOURCE_GOLD]) return false;
    if (b->lumberCost > ps->stats[PLAYERSTATE_RESOURCE_LUMBER]) return false;
    ps->stats[PLAYERSTATE_RESOURCE_GOLD] -= b->goldCost;
    ps->stats[PLAYERSTATE_RESOURCE_LUMBER] -= b->lumberCost;
    return true;
}

BOOL M_IsDead(LPCEDICT ent) {
    return ent->health.value <= 0;
}

static int monster_harvest_path_debug_level(void) {
    LPCSTR value;
    value = gi.CvarString("wc3_harvest_path_debug", "0");
    return value ? atoi(value) : 0;
}

DWORD M_RefreshHeatmap(LPEDICT self, FLOAT radius) {
    LPEDICT route = self && self->secondarygoal ? self->secondarygoal : self;
    BOOL radius_matches;
    BOOL cached = false;
    DWORD generation;

    if (!route)
        return 0;

    radius_matches = fabsf(route->heatmap2_radius - radius) < 0.01f;
    if (radius_matches && route->heatmap2)
        cached = CM_ActivateCachedFlow(route->heatmap2);

    /* Fixed waypoints never move, so a still-cached field remains valid until
     * static pathing invalidates the routing cache. */
    if (cached && !(route->svflags & SVF_MONSTER))
        return route->heatmap2;

    if (cached && (route->svflags & SVF_MONSTER)) {
        BOOL const moved = Vector2_distance(&route->s.origin2, &route->heatmap2_origin) >= 64.0f;
        BOOL const stale = (DWORD)(level.time - route->heatmap2_time) >= 400;
        if (!moved || !stale)
            return route->heatmap2;
    }

    /* Cache misses are resumable in common/routing.c.  Return the old field for
     * a moving target while its replacement is being built; fixed goals with
     * no field simply wait until a later tick instead of steering straight into
     * the obstacle that caused routing to be needed. */
    generation = CM_RequestHeatmapForRadius(route, radius);
    if (!generation)
        return cached ? route->heatmap2 : 0;

    route->heatmap2 = generation;
    route->heatmap2_origin = route->s.origin2;
    route->heatmap2_time = level.time;
    route->heatmap2_radius = radius;

    if (monster_harvest_path_debug_level() >= 2 && route->targtype == TARG_TREE) {
        fprintf(stderr,
                "WC3_HARVEST_PATH heatmap target=%d reason=ready generation=%u radius=%.1f\n",
                route->s.number, route->heatmap2, radius);
    }
    return route->heatmap2;
}

/* Advance the unit's animation frame by FRAMETIME milliseconds.
 * If the new frame would exceed the animation's end interval, the current
 * umove_t endfunc is called (e.g. to loop the walk cycle or transition to
 * the cooldown phase after an attack). */
void M_MoveFrame(LPEDICT self) {
    /* Human construction keeps AI_HOLD_FRAME so a paused building never
     * advances on wall-clock time. Its birth sequence is instead driven by
     * authoritative construction progress, which also makes power building
     * accelerate the visible construction animation. */
    if ((self->aiflags & AI_HOLD_FRAME) && self->construction.active) {
        G_UpdateConstructionAnimation(self);
        return;
    }
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
        DWORD build_time = G_UnitBalance(self->class_id)->buildTime * 1000;
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
        if (buffer)
            pathTex = LoadTGA(buffer, filesize);
        else
            fprintf(stderr, "M_LoadPathTex: not found: %s\n", filename);
        gi.MemFree(buffer);
        return pathTex;
    }
    return NULL;
}

DWORD M_LoadUberSplat(LPCSTR uber_splat) {
    if (IS_FOURCC(uber_splat)) {
        UberSplatData_t const *row = G_UberSplat(*(DWORD const *)uber_splat);
        PATHSTR filename;
        if (!row->id) return 0;
        snprintf(filename, sizeof(PATHSTR), "%s\\%s.blp", row->Dir, row->file);
        return gi.ImageIndex(filename) | ((DWORD)row->Scale << 16);
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
    UnitUI_t const *ui = self->data.UnitUI;
    LPCSTR unit_shadow = ui->unitShadowTexture;
    DWORD shadow = G_LoadShadowTexture(unit_shadow, true);
    if (!shadow) {
        shadow = G_LoadShadowTexture("Shadow", true);
    }
    if (!shadow) {
        return;
    }

#ifndef USE_SHADOWMAPS
    self->s.shadow = shadow;
    FLOAT shadow_x = ui->shadowCenterX;
    FLOAT shadow_y = ui->shadowCenterY;
    FLOAT shadow_w = ui->shadowWidth;
    FLOAT shadow_h = ui->shadowHeight;
    if (shadow_w <= 0 || shadow_h <= 0) {
        FLOAT size = MAX(72, ui->selectionScale * SEL_SCALE);
        shadow_x = size * 0.5f;
        shadow_y = size * 0.5f;
        shadow_w = size;
        shadow_h = size;
    }
    self->s.shadow_rect = ShadowPackRect(shadow_x, shadow_y, shadow_w, shadow_h);
#endif
}

static void M_SetBuildingShadow(LPEDICT self) {
    UnitUI_t const *ui = self->data.UnitUI;
    LPCSTR building_shadow = ui->buildingShadowTexture;
    DWORD shadow = G_LoadShadowTexture(building_shadow, false);
    if (!shadow) {
        if (G_HasShadowName(ui->unitShadowTexture)) {
            M_SetUnitShadow(self);
        }
        return;
    }

#ifndef USE_SHADOWMAPS
    self->s.shadow = shadow;
    self->s.shadow_rect = 0;
#endif
}

int g_treeFallSounds[3]; BYTE g_numTreeFallSounds;

/* Register the first sound file for a given SLK label+suffix and return its
 * configstring index, or 0 if the entry is not found or has no files. */
static int G_RegisterSoundLabel(LPCSTR label, LPCSTR suffix) {
    char key[128];
    snprintf(key, sizeof(key), "%s%s", label, suffix);
    UnitAckSounds_t const *row = G_UnitAckSound(key);
    LPCSTR files = row->FileNames, dir = row->DirectoryBase;
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

/* Like G_RegisterSoundVariants but looks up in UnitCombatSounds instead of UnitAckSounds. */
static void G_RegisterCombatVariants(BYTE out[], BYTE *count, BYTE max, LPCSTR key) {
    char file[256], path[512];
    UnitAckSounds_t const *row = G_UnitCombatSound(key);
    LPCSTR files = row->FileNames, dir = row->DirectoryBase;
    while (files && files[0] && *count < max) {
        LPCSTR comma = strchr(files, ',');
        snprintf(file, sizeof(file), "%.*s", comma ? (int)(comma - files) : (int)strlen(files), files);
        snprintf(path, sizeof(path), "%s%s", dir ? dir : "", file);
        out[(*count)++] = (BYTE)gi.SoundIndex(path);
        files = comma ? comma + 1 : NULL;
    }
}

/* Register all comma-separated files for a given label+suffix into out[]/count.
 * Every variant lands in CS_SOUNDS so playback never misses a precache. */
static void G_RegisterSoundVariants(BYTE out[], BYTE *count, LPCSTR label, LPCSTR suffix) {
    char key[128], file[256], path[512];
    snprintf(key, sizeof(key), "%s%s", label, suffix);
    UnitAckSounds_t const *row = G_UnitAckSound(key);
    LPCSTR files = row->FileNames, dir = row->DirectoryBase;
    while (files && files[0] && *count < MAX_UNIT_SELECT_SOUNDS) {
        LPCSTR comma = strchr(files, ',');
        snprintf(file, sizeof(file), "%.*s", comma ? (int)(comma - files) : (int)strlen(files), files);
        snprintf(path, sizeof(path), "%s%s", dir ? dir : "", file);
        out[(*count)++] = (BYTE)gi.SoundIndex(path);
        files = comma ? comma + 1 : NULL;
    }
}

/* Cache every native selection response so repeated clicks can choose among
 * the authored UnitAckSounds variants instead of repeating the first file. */
void G_RegisterSelectSounds(LPEDICT self, LPCSTR label) {
    G_RegisterSoundVariants(self->sound.select, &self->sound.num_select, label, "What");
}

/* Populate the unit's cached sound indices from UnitAckSounds.slk using the
 * "unitSound" label (e.g. "Footman").  Falls back gracefully if entries are
 * missing — sounds simply won't fire for that unit. */
static void G_RegisterUnitSounds(LPEDICT self) {
    LPCSTR label = self->data.UnitUI->soundLabel;
    if (!label || !label[0]) return;
    G_RegisterSelectSounds(self, label);
    /* Register all order-confirmation variants so clients have them cached;
     * sound.attack keeps the first index for the attack-swing event. */
    G_RegisterSoundVariants(self->sound.yes, &self->sound.num_yes, label, "Yes");
    G_RegisterSoundVariants(self->sound.ready, &self->sound.num_ready, label, "Ready");
    {
        BYTE tmp[MAX_UNIT_SELECT_SOUNDS]; BYTE n = 0;
        G_RegisterSoundVariants(tmp, &n, label, "YesAttack");
        self->sound.attack = n ? tmp[0] : 0;
    }
    /* Death sounds follow the pattern {label}Death but may not exist in the
     * AckSounds SLK.  Try the SLK first; fall back to the raw file path. */
    self->sound.death = G_RegisterSoundLabel(label, "Death");
    if (!self->sound.death) {
        /* Derive death sound path from model directory: units\race\Name\NameDeath.wav */
        LPCSTR model = self->data.UnitUI->modelFile;
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
            self->sound.death = gi.SoundIndex(path);
        }
    }
    /* Chop-wood impact sound from UnitCombatSounds: {weapType1}Wood (e.g. MetalLightChopWood). */
    LPCSTR ws = self->data.UnitWeapons->attack1.weaponSound;
    if (ws && ws[0] && ws[0] != '_') {
        char key[128];
        snprintf(key, sizeof(key), "%sWood", ws);
        G_RegisterCombatVariants(self->sound.chop, &self->sound.num_chop, 3, key);
    }
}

/* Register world-level sounds that are not per-unit: tree felling, etc.
 * Called once from G_InitGame after the archive is mounted. */
void G_RegisterGlobalSounds(void) {
    static LPCSTR falls[] = {
        "Sound\\Destructibles\\TreeFall1.wav",
        "Sound\\Destructibles\\TreeFall2.wav",
        "Sound\\Destructibles\\TreeFall3.wav",
    };
    g_numTreeFallSounds = 0;
    FOR_LOOP(i, sizeof(falls) / sizeof(*falls)) {
        int idx = gi.SoundIndex(falls[i]);
        if (idx) g_treeFallSounds[g_numTreeFallSounds++] = idx;
    }
}

/* Unit data decides the persistent AI capabilities assigned at spawn. */
DWORD unit_spawn_aiflags(DWORD class_id) { return G_UnitIsBuilding(class_id) ? AI_IMMOBILE : 0; }

/* Initialize a unit entity from the unit data tables.
 * Reads model path, scale, collision radius, HP, mana, and attack parameters
 * (type, weapon class, damage dice, range, projectile model/speed) for the
 * unit's class_id and stores them in the edict. */
void SP_SpawnUnit(LPEDICT self) {
    PATHSTR model_filename;
    UnitBalance_t const *b = self->data.UnitBalance;
    UnitData_t const *d = self->data.UnitData;
    UnitUI_t const *ui = self->data.UnitUI;
    UnitWeapons_t const *w = self->data.UnitWeapons;
    LPCSTR uber_splat = ui->groundTexture;
    LPCSTR path_tex = d->pathingTexture;
    G_InitStockSlots(self);
    self->runtime.flags = (unit_spawn_aiflags(self->class_id) & AI_IMMOBILE) ? UNIT_BALANCE_BUILDING : 0;
    if (G_UnitIsBuilding(self->class_id)) self->s.flags |= EF_BUILDING;
    snprintf(model_filename, sizeof(model_filename), "%s.mdx", ui->modelFile);
    self->s.model = G_RegisterModel(model_filename);
    self->s.splat = M_LoadUberSplat(uber_splat);
    if (self->runtime.flags & UNIT_BALANCE_BUILDING) {
        M_SetBuildingShadow(self);
    } else {
        M_SetUnitShadow(self);
    }
    self->s.scale = ui->modelScale;
    self->s.radius = ui->selectionScale * SEL_SCALE / 2;
    /* Unit-vs-unit separation uses the authentic collisionSize ('ucol') from
     * the unit data, matching WC3. Buildings have no meaningful collisionSize
     * and instead block via their pathing footprint (set from pathtex below). */
    {
        FLOAT const ucol = G_UnitCollision(self->class_id);
        /* Real WC3 units always have ucol>0; if missing, fall back to 0 (block
         * via footprint, set below for buildings) — NOT s.radius, which is a
         * selection-circle scale, not a world-unit collision radius. */
        self->collision = ucol > 0.0f ? ucol : 0.0f;
    }
//    printf("%.4s\n", &self->class_id);
    self->targtype = G_GetTargetType(d->targetType);
    if (ui->occluderHeight > 0) {
        self->s.flags |= EF_FOW_BLOCKER;
        G_FowMarkBlockersDirty();
    }
    if (b->sightRadius > 0 || b->nightSightRadius > 0) {
        self->s.flags |= EF_FOW_REVEALER;
    }
    self->mana.max_value = b->maxMana;
    self->mana.value = MIN(self->mana.max_value, b->initialMana);
    self->health.value = b->maxHealth;
    self->health.max_value = b->maxHealth;
    self->invulnerable = G_ActorHasSkill(self, "Avul");
    self->unitinfo.MoveSpeed = b->speed;
    self->runtime.sight_radius.day = b->sightRadius;
    self->runtime.sight_radius.night = b->nightSightRadius;
    /* Unit-table values are immutable after spawn; cache them before the per-frame AI/FOW paths consume them. */
    self->runtime.acquisition_range = w->acquisitionRange;
    if (self->runtime.acquisition_range <= 0.0f)
        self->runtime.acquisition_range = self->runtime.sight_radius.day * 0.5f;
    if (self->runtime.sight_radius.day > 0.0f && self->runtime.acquisition_range > self->runtime.sight_radius.day)
        self->runtime.acquisition_range = self->runtime.sight_radius.day;
    self->think = monster_think;
    /* Blighted gold mines earn gold on an interval instead of via workers. */
    if (G_ActorHasSkill(self, "Abgm")) {
        self->think = blight_mine_think;
    }
    self->svflags |= SVF_MONSTER;
    /* Buildings use a single immobility contract so smart orders, combat, and
     * future movement paths cannot rotate or translate them independently. */
    if (self->runtime.flags & UNIT_BALANCE_BUILDING) self->aiflags |= AI_IMMOBILE;
    /* Cache the air/ground collision layer once. Flyers ('movetp' == "fly")
     * never collide with ground units and vice-versa. */
    {
        LPCSTR const movetp = d->moveTypeName;
        if (movetp && !strcmp(movetp, "fly"))
            self->aiflags |= AI_FLYING;
    }

    self->defense_type = FindEnumValue(b->defenseType, defense_type);
    self->armor_value = b->armor;
    /* Heroes carry their base primary attributes.  realHP/realM/realdef already
     * bake in the level-1 attribute bonus, so we just record the base values;
     * when the attributes later change (tomes, SetHeroStr/Agi/Int, level-up)
     * G_RecomputeHeroStats applies the per-point deltas (+25 HP / +15 mana /
     * +0.3 armor).  Non-heroes have no attributes (all zero) and are skipped. */
    {
        LONG const baseStr = b->strength;
        LONG const baseAgi = b->agility;
        LONG const baseInt = b->intelligence;
        if (baseStr > 0 || baseAgi > 0 || baseInt > 0) {
            self->hero.str   = (DWORD)baseStr;
            self->hero.agi   = (DWORD)baseAgi;
            self->hero.intel = (DWORD)baseInt;
            /* war3mapUnits.doo stores Hero level but not unspent skill
             * points. Seed the level-derived point budget before the map
             * script applies its authored SelectHeroSkill calls. */
            G_HeroInitializeProgression(self);
        }
    }
    self->attack1.type = FindEnumValue(w->attack1.attackType, attack_type);
    self->attack1.weapon = FindEnumValue(w->attack1.weaponType, weapon_type);
    self->attack1.damageBase = w->attack1.damageBase;
    self->attack1.numberOfDice = w->attack1.damageDice;
    self->attack1.sidesPerDie = w->attack1.damageSides;
    self->attack1.cooldown = w->attack1.cooldown;
    self->attack1.damagePoint = w->attack1.damagePoint;
    self->attack1.range = w->attack1.range;
    self->attack1.areaFull = w->attack1.areaFull;
    self->attack1.areaMedium = w->attack1.areaMedium;
    self->attack1.areaSmall = w->attack1.areaSmall;
    self->attack1.factorMedium = w->attack1.factorMedium;
    self->attack1.factorSmall = w->attack1.factorSmall;
    self->attack1.maxTargets = w->attack1.maxTargets;
    self->attack1.damageLoss = w->attack1.damageLossFactor;

    /* Keep Attack 2 runtime state parallel with Attack 1 even though order
     * selection still uses Attack 1 today. This lets upgrades/HUD math operate
     * on mutable runtime values instead of immutable SLK rows. */
    self->attack2.type = FindEnumValue(w->attack2.attackType, attack_type);
    self->attack2.weapon = FindEnumValue(w->attack2.weaponType, weapon_type);
    self->attack2.damageBase = w->attack2.damageBase;
    self->attack2.numberOfDice = w->attack2.damageDice;
    self->attack2.sidesPerDie = w->attack2.damageSides;
    self->attack2.cooldown = w->attack2.cooldown;
    self->attack2.damagePoint = w->attack2.damagePoint;
    self->attack2.range = w->attack2.range;
    self->attack2.areaFull = w->attack2.areaFull;
    self->attack2.areaMedium = w->attack2.areaMedium;
    self->attack2.areaSmall = w->attack2.areaSmall;
    self->attack2.factorMedium = w->attack2.factorMedium;
    self->attack2.factorSmall = w->attack2.factorSmall;
    self->attack2.maxTargets = w->attack2.maxTargets;
    self->attack2.damageLoss = w->attack2.damageLossFactor;
    /* Heroes: fold the primary-attribute attack-damage bonus into runtime
     * attacks now that base attributes and both weapon slots are loaded. */
    G_RecomputeHeroStats(self);
    /* Completed player upgrades are persistent techtree state, not producer
     * buffs. New units inherit the owner's current levels at spawn. */
    G_ApplyPlayerUpgradesToUnit(self);

    if (self->attack1.weapon == WPN_MISSILE) {
        self->attack1.origin.x = G_UnitAttack1LaunchX(self->class_id);
        self->attack1.origin.y = G_UnitAttack1LaunchY(self->class_id);
        self->attack1.origin.z = G_UnitAttack1LaunchZ(self->class_id);
        self->attack1.projectile.model = G_RegisterModel(G_UnitProfile(self->class_id)->attack[0].art);
        self->attack1.projectile.arc = G_UnitProfile(self->class_id)->attack[0].arc;
        self->attack1.projectile.speed = G_UnitProfile(self->class_id)->attack[0].speed;
    }

    if ((self->pathtex = M_LoadPathTex(path_tex))) {
        /* Buildings: collide by footprint (their collisionSize is ~0). */
        if (self->runtime.flags & UNIT_BALANCE_BUILDING) {
            self->collision = get_unit_collision(self->pathtex);
        }
    }
    G_RegisterUnitSounds(self);
}

/* Walkable destructables are sparse, so keep a level list instead of scanning every map edict per unit tick. */
void G_RegisterGroundSurface(LPEDICT ent) {
    if (!G_IsDestructable(ent) || !ent->data.DestructableData->walkable) return;
    G_UnregisterGroundSurface(ent);
    ent->ground_next = level.ground_surfaces;
    level.ground_surfaces = ent;
}

void G_UnregisterGroundSurface(LPEDICT ent) {
    LPEDICT *link = &level.ground_surfaces;
    while (*link && *link != ent) link = &(*link)->ground_next;
    if (*link) *link = ent->ground_next;
    if (ent) ent->ground_next = NULL;
}

void G_ClearGroundSurfaces(void) { level.ground_surfaces = NULL; }

/* A live walkable destructable replaces the lower terrain inside its authored pathing footprint. */
void M_CheckGround(LPEDICT self) {
    FLOAT height = CM_GetHeightAtPoint(self->s.origin.x, self->s.origin.y);
    FLOAT const cell = CM_PathCellWorldSize();
    for (LPEDICT surface = level.ground_surfaces; surface; surface = surface->ground_next) {
        pathTex_t const *pathtex = surface->pathtex;
        if (!surface->inuse || surface->destructable.dead ||
            !surface->destructable.placement_solid || !pathtex) continue;
        if (fabsf(self->s.origin.x - surface->s.origin.x) > pathtex->width * cell * 0.5f ||
            fabsf(self->s.origin.y - surface->s.origin.y) > pathtex->height * cell * 0.5f) continue;
        height = MAX(height, surface->s.origin.z);
    }
    self->s.origin.z = height;
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

BYTE compress_stat(edictStat_s const *stat) {
    if (stat->max_value <= 0) {
        return 0;
    } else {
        return 255 * stat->value / stat->max_value;
    }
}
