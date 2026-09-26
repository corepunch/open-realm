/*
 * g_monster.c — Unit and monster shared behavior.
 *
 * This file owns the per-unit animation driver (M_MoveFrame) and unit initialization
 * (SP_SpawnUnit) which reads unit stats from the data tables and sets up
 * combat parameters, models, and collision radii.
 *
 * The think function registered on every unit entity is monster_think(),
 * which advances the current animation frame and calls the active umove_t *think callback each game tick.
 */
#include "g_local.h"

cstring_t attack_type[] = {
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
cstring_t defense_type[] = {
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

cstring_t weapon_type[] = {
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

uint32_t FindEnumValue(cstring_t value, cstring_t values[]) {
    if (!value)
        return 0;
    for (cstring_t *s = values; *s; s++) {
        if (!strcmp(*s, value)) {
            return (uint32_t)(s - values);
        }
    }
    return 0;
}

static float get_unit_collision(pathTex_t const *pathtex) {
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

bool player_pay(player_t *ps, uint32_t project) {
    UnitBalance_t const *b;
    if (!ps) return false;
    b = G_UnitBalance(project);
    if (b->goldCost > ps->stats[PLAYERSTATE_RESOURCE_GOLD]) return false;
    if (b->lumberCost > ps->stats[PLAYERSTATE_RESOURCE_LUMBER]) return false;
    ps->stats[PLAYERSTATE_RESOURCE_GOLD] -= b->goldCost;
    ps->stats[PLAYERSTATE_RESOURCE_LUMBER] -= b->lumberCost;
    return true;
}

bool M_IsDead(edict_t const *ent) {
    return ent->health.value <= 0;
}

/* Advance the unit's animation frame by its scaled simulation timestep.
 * If the new frame would exceed the animation's end interval, the current
 * umove_t endfunc is called and walk variants are rerolled when the move remains active. */
void M_MoveFrame(edict_t *self) {
    /* Construction keeps AI_HOLD_FRAME so the birth sequence never advances
     * independently of authoritative construction progress. Human progress is
     * Repair-driven; Orc/Undead/Night Elf progress is advanced by
     * G_RunConstructionFrame(). */
    if ((self->aiflags & AI_HOLD_FRAME) && self->construction.active) {
        G_UpdateConstructionAnimation(self);
        return;
    }
    if (self->aiflags & AI_HOLD_FRAME)
        return;
    umove_t const *move = self->currentmove;
    animation_t const *anim = self->animation;
    float frame_step = MAX(0.0f, FRAMETIME * self->animation_speed);
    if (!anim) {
        unit_setmove(self, self->currentmove);
        anim = self->animation;
        if (!anim) {
            return;
        }
    }
    if (move->animation_duration) {
        float const duration = move->animation_duration(self);
        uint32_t const frames = anim->interval[1] > anim->interval[0]
                           ? anim->interval[1] - anim->interval[0] : 0;
        if (duration > 0.0f && frames > 0) {
            float const elapsed = MAX(0.0f, MIN(duration, duration - self->wait));
            float const progress = MIN(1.0f, elapsed / duration);
            uint32_t const offset = frames > 1
                               ? (uint32_t)floorf(progress * (float)(frames - 1)) : 0;
            self->s.frame = anim->interval[0] + MIN(offset, frames - 1);
            return;
        }
    }
    uint32_t next_frame = self->s.frame + (uint32_t)frame_step;
    if (G_AnimationHasPrimary(anim, "birth")) {
        uint32_t anim_len = anim->interval[1] - anim->interval[0];
        uint32_t build_time = G_UnitBalance(self->class_id)->buildTime * 1000;
        if (build_time > 0) {
            next_frame = self->s.frame + (uint32_t)(frame_step * anim_len / build_time);
        }
    }
    if (self->s.frame < anim->interval[0] ||
        self->s.frame >= anim->interval[1])
    {
        self->s.frame = anim->interval[0] ;
    } else if (next_frame >= anim->interval[1]) {
        SAFE_CALL(move->endfunc, self);
        if (!(self->aiflags & AI_HOLD_FRAME)) {
            if (self->currentmove == move && self->animation == anim &&
                !self->animation_override && G_AnimationHasPrimary(anim, "walk"))
                G_SetUnitAnimation(self, self->animation_request);
            /* End callbacks may install a different move/animation. Restart
             * whichever animation is active after the callback; resetting to
             * the completed clip's first frame leaves the replacement model
             * sampling an unrelated sequence for one simulation tick. */
            animation_t const *active_anim = self->animation ? self->animation : anim;
            self->s.frame = active_anim->interval[0];
        }
    } else {
        self->s.frame = next_frame;
    }
}

/* Per-unit think function registered on every monster/unit entity.
 * Called each game frame by G_RunEntity; drives the animation clock and
 * invokes the active umove_t think callback (e.g. ai_walk, ai_melee). */
void monster_think(edict_t *self) {
    S_RunAbilityUpdates(self);
    if (!self->currentmove)
        return;
    if (self->paused || self->stunned) {
        if (self->paused && self->animation_override) M_MoveFrame(self);
        return;
    }
    M_MoveFrame(self);
    if (self->currentmove->think) {
        self->currentmove->think(self);
    }
}

void monster_start(edict_t *self) {
    animation_t const *anim = self->animation;
    if (anim) {
        uint32_t len = MAX(1, anim->interval[1] - anim->interval[0] - 1);
        self->s.frame = (anim->interval[0] + (rand() % len));
    }
}

//unitRace_t M_GetRace(cstring_t string) {
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
    uint32_t size;
    int num_components;
    uint8_t *data;
};

pathTex_t *M_LoadPathTex(cstring_t filename) {
    pathTex_t *pathTex = NULL;
    if (filename && strlen(filename) > 1) {
        uint32_t filesize;
        handle_t buffer = gi.ReadFile(filename, &filesize);
        if (buffer) {
            pathTex = LoadTGA(buffer, filesize);
            if (!pathTex) fprintf(stderr, "M_LoadPathTex: invalid TGA: %s\n", filename);
        } else {
            fprintf(stderr, "M_LoadPathTex: not found: %s\n", filename);
        }
        gi.MemFree(buffer);
        return pathTex;
    }
    return NULL;
}

uint32_t M_LoadUberSplat(cstring_t uber_splat) {
    if (IS_FOURCC(uber_splat)) {
        UberSplatData_t const *row = G_UberSplat(*(uint32_t const *)uber_splat);
        PATHSTR filename;
        if (!row->id) return 0;
        snprintf(filename, sizeof(PATHSTR), "%s\\%s.blp", row->Dir, row->file);
        return gi.ImageIndex(filename) | ((uint32_t)row->Scale << 16);
    } else {
        return 0;
    }
}

static bool G_FileExists(cstring_t filename) {
    uint32_t filesize = 0;
    handle_t buffer = gi.ReadFile(filename, &filesize);
    if (buffer) {
        gi.MemFree(buffer);
        return true;
    }
    return false;
}

static bool G_HasShadowName(cstring_t shadow) {
    return shadow && shadow[0] && strcmp(shadow, "_");
}

uint32_t G_LoadShadowTexture(cstring_t shadow, bool allowDDSFallback) {
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

static void M_SetUnitShadow(edict_t *self) {
    UnitUI_t const *ui = self->data.UnitUI;
    cstring_t unit_shadow = ui->unitShadowTexture;
    uint32_t shadow = G_LoadShadowTexture(unit_shadow, true);
    if (!shadow) {
        shadow = G_LoadShadowTexture("Shadow", true);
    }
    if (!shadow) {
        return;
    }

#ifndef USE_SHADOWMAPS
    self->s.shadow = shadow;
    float shadow_x = ui->shadowCenterX;
    float shadow_y = ui->shadowCenterY;
    float shadow_w = ui->shadowWidth;
    float shadow_h = ui->shadowHeight;
    if (shadow_w <= 0 || shadow_h <= 0) {
        float size = MAX(72, ui->selectionScale * SEL_SCALE);
        shadow_x = size * 0.5f;
        shadow_y = size * 0.5f;
        shadow_w = size;
        shadow_h = size;
    }
    self->s.shadow_rect = ShadowPackRect(shadow_x, shadow_y, shadow_w, shadow_h);
#endif
}

static void M_SetBuildingShadow(edict_t *self) {
    UnitUI_t const *ui = self->data.UnitUI;
    cstring_t building_shadow = ui->buildingShadowTexture;
    uint32_t shadow = G_LoadShadowTexture(building_shadow, false);
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

int g_treeFallSounds[3]; uint8_t g_numTreeFallSounds;

/* Cache authored UnitAck/UnitCombat variants through the shared sound-row
 * resolver so volume metadata follows the resulting configstring index. */
static void G_RegisterCombatVariants(uint16_t out[], uint8_t *count, uint8_t max, cstring_t key) {
    uint32_t variants = G_UnitCombatSoundVariantCount(key);
    for (uint32_t i = 0; i < variants && *count < max; i++) {
        int sound = G_UnitCombatSoundVariantIndex(key, i);
        if (sound) out[(*count)++] = (uint16_t)sound;
    }
}

static void G_RegisterSoundVariants(uint16_t out[], uint8_t *count, cstring_t label, cstring_t suffix) {
    uint32_t variants = G_UnitAckSoundVariantCount(label, suffix);
    for (uint32_t i = 0; i < variants && *count < MAX_UNIT_SELECT_SOUNDS; i++) {
        int sound = G_UnitAckSoundVariantIndex(label, suffix, i);
        if (sound) out[(*count)++] = (uint16_t)sound;
    }
}

/* Cache every native selection response so repeated clicks can choose among
 * the authored UnitAckSounds variants instead of repeating the first file. */
void G_RegisterSelectSounds(edict_t *self, cstring_t label) {
    G_RegisterSoundVariants(self->sound.select, &self->sound.num_select, label, "What");
}

/* Populate the unit's cached sound indices from UnitAckSounds.slk using the
 * "unitSound" label (e.g. "Footman").  Falls back gracefully if entries are
 * missing — sounds simply won't fire for that unit. */
static void G_RegisterUnitSounds(edict_t *self) {
    cstring_t label = self->data.UnitUI->soundLabel;
    if (!label || !label[0]) return;
    G_RegisterSelectSounds(self, label);
    /* Ordinary order and ready variants are cached per unit. YesAttack and
     * Pissed are selected from UnitAckSounds at the interaction that owns
     * them; they are not weapon-swing sounds. */
    G_RegisterSoundVariants(self->sound.yes, &self->sound.num_yes, label, "Yes");
    G_RegisterSoundVariants(self->sound.ready, &self->sound.num_ready, label, "Ready");
    /* Death sounds may be catalogued or shipped beside the unit model. */
    self->sound.death = G_UnitAckSoundVariantIndex(label, "Death", 0);
    if (!self->sound.death) {
        cstring_t model = self->data.UnitUI->modelFile;
        if (model && model[0]) {
            char path[512];
            cstring_t slash = strrchr(model, '\\');
            for (int numbered = 1; numbered >= 0; numbered--) {
                snprintf(path, sizeof(path), "%.*s%sDeath%s.wav",
                         slash ? (int)(slash - model + 1) : 0, model,
                         slash ? slash + 1 : model, numbered ? "1" : "");
                if (G_FileExists(path)) { self->sound.death = gi.SoundIndex(path); break; }
            }
        }
    }
    /* Chop-wood impact sound from UnitCombatSounds: {weapType1}Wood (e.g. MetalLightChopWood). */
    cstring_t ws = self->data.UnitWeapons->attack1.weaponSound;
    if (ws && ws[0] && ws[0] != '_') {
        char key[128];
        snprintf(key, sizeof(key), "%sWood", ws);
        G_RegisterCombatVariants(self->sound.chop, &self->sound.num_chop, 3, key);
    }
}

/* Register world-level sounds that are not per-unit: tree felling, etc.
 * Called once from G_InitGame after the archive is mounted. */
void G_RegisterGlobalSounds(void) {
    static cstring_t falls[] = {
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
uint32_t unit_spawn_aiflags(uint32_t class_id) { return G_UnitIsBuilding(class_id) ? AI_IMMOBILE : 0; }

/* Apply static ability traits after ordinary collision and vulnerability state. */
void G_ApplyUnitAbilityTraits(edict_t *ent) {
    if (!G_ActorHasSkill(ent, "Aloc")) return;
    ent->s.flags |= EF_NOT_SELECTABLE;
    ent->invulnerable = true;
    ent->collision = 0.0f;
    ent->no_pathing = true;
}

/* Initialize a unit entity from the unit data tables.
 * Reads model path, scale, collision radius, HP, mana, and attack parameters
 * (type, weapon class, damage dice, range, projectile model/speed) for the
 * unit's class_id and stores them in the edict. */
void SP_SpawnUnit(edict_t *self) {
    PATHSTR model_filename;
    UnitBalance_t const *b = self->data.UnitBalance;
    UnitData_t const *d = self->data.UnitData;
    UnitUI_t const *ui = self->data.UnitUI;
    UnitWeapons_t const *w = self->data.UnitWeapons;
    cstring_t uber_splat = ui->groundTexture;
    cstring_t path_tex = d->pathingTexture;
    G_InitStockSlots(self);
    self->runtime.flags = (unit_spawn_aiflags(self->class_id) & AI_IMMOBILE) ? UNIT_BALANCE_BUILDING : 0;
    if (G_UnitIsBuilding(self->class_id)) self->s.flags |= EF_BUILDING;
    if (S_UnitTypeIsGoldMine(self->class_id)) self->s.flags |= EF_RESOURCE_SOURCE;
    if (S_UnitTypeReturnsGold(self->class_id)) self->s.flags |= EF_RESOURCE_RETURN;
    if (!d->moveTypeName || strcmp(d->moveTypeName, "float")) self->s.flags |= EF_GROUND_CONFORM;
    G_NormalizeModelFilename(ui->modelFile, model_filename, sizeof(model_filename));
    self->s.model = G_RegisterModel(model_filename);
    G_ResetUnitAnimationProperties(self);
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
        float const ucol = G_UnitCollision(self->class_id);
        /* Real WC3 units always have ucol>0; if missing, fall back to 0 (block
         * via footprint, set below for buildings) — NOT s.radius, which is a
         * selection-circle scale, not a world-unit collision radius. */
        self->collision = ucol > 0.0f ? ucol : 0.0f;
    }
//    printf("%.4s\n", &self->class_id);
    self->targtype = G_GetTargetType(d->targetType);
    S_UnitAbilityEvent(self, A_UNIT_INIT);
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
    G_ApplyUnitAbilityTraits(self);
    self->unitinfo.MoveSpeed = b->speed;
    /* Warcraft object data owns model altitude.  Keep the mutable current
     * height separate from terrain support so SetUnitFlyHeight can change it
     * without losing the unit type's authored moveHeight/default. */
    self->unitinfo.FlyHeight = d->moveHeight;
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
        cstring_t const movetp = d->moveTypeName;
        if (movetp && !strcmp(movetp, "fly"))
            self->aiflags |= AI_FLYING;
    }
    /* Neutral creeps sleep until a hero enters acquisition range; non-neutral
     * units (including camp defenders made hostile by script) start awake. */
    if (self->s.player < MAX_PLAYERS &&
        level.mapinfo->players[self->s.player].playerType == kPlayerTypeNeutral)
        self->aiflags |= AI_SLEEPING;

    self->defense_type = FindEnumValue(b->defenseType, defense_type);
    self->armor_value = b->armor;
    /* Heroes carry their base primary attributes.  realHP/realM/realdef already
     * bake in the level-1 attribute bonus, so we just record the base values;
     * when the attributes later change (tomes, SetHeroStr/Agi/Int, level-up)
     * G_RecomputeHeroStats applies the per-point deltas (+25 HP / +15 mana /
     * +0.3 armor).  Non-heroes have no attributes (all zero) and are skipped. */
    {
        int32_t const baseStr = b->strength;
        int32_t const baseAgi = b->agility;
        int32_t const baseInt = b->intelligence;
        if (baseStr > 0 || baseAgi > 0 || baseInt > 0) {
            self->hero.str   = (uint32_t)baseStr;
            self->hero.agi   = (uint32_t)baseAgi;
            self->hero.intel = (uint32_t)baseInt;
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
    self->attack1.targetsAllowed = (uint32_t)w->attack1.targetsAllowed;
    self->attack1.areaFull = w->attack1.areaFull;
    self->attack1.areaMedium = w->attack1.areaMedium;
    self->attack1.areaSmall = w->attack1.areaSmall;
    self->attack1.factorMedium = w->attack1.factorMedium;
    self->attack1.factorSmall = w->attack1.factorSmall;
    self->attack1.maxTargets = w->attack1.maxTargets;
    self->attack1.damageLoss = w->attack1.damageLossFactor;

    /* Keep Attack 2 runtime state parallel with Attack 1 so target selection
     * can activate the authored secondary weapon profile. */
    self->attack2.type = FindEnumValue(w->attack2.attackType, attack_type);
    self->attack2.weapon = FindEnumValue(w->attack2.weaponType, weapon_type);
    self->attack2.damageBase = w->attack2.damageBase;
    self->attack2.numberOfDice = w->attack2.damageDice;
    self->attack2.sidesPerDie = w->attack2.damageSides;
    self->attack2.cooldown = w->attack2.cooldown;
    self->attack2.damagePoint = w->attack2.damagePoint;
    self->attack2.range = w->attack2.range;
    self->attack2.targetsAllowed = (uint32_t)w->attack2.targetsAllowed;
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
    S_CargoInitUnit(self);

    if (self->attack1.weapon == WPN_MISSILE || self->attack1.weapon == WPN_ARTILLERY) {
        self->attack1.origin.x = G_UnitAttack1LaunchX(self->class_id);
        self->attack1.origin.y = G_UnitAttack1LaunchY(self->class_id);
        self->attack1.origin.z = G_UnitAttack1LaunchZ(self->class_id);
        self->attack1.projectile.model = G_RegisterModel(G_UnitProfile(self->class_id)->attack[0].art);
        self->attack1.projectile.arc = G_UnitProfile(self->class_id)->attack[0].arc;
        self->attack1.projectile.speed = G_UnitProfile(self->class_id)->attack[0].speed;
    }
    if (self->attack2.weapon == WPN_MISSILE || self->attack2.weapon == WPN_ARTILLERY) {
        self->attack2.origin.x = G_UnitAttack1LaunchX(self->class_id);
        self->attack2.origin.y = G_UnitAttack1LaunchY(self->class_id);
        self->attack2.origin.z = G_UnitAttack1LaunchZ(self->class_id);
        self->attack2.projectile.model = G_RegisterModel(G_UnitProfile(self->class_id)->attack[1].art);
        self->attack2.projectile.arc = G_UnitProfile(self->class_id)->attack[1].arc;
        self->attack2.projectile.speed = G_UnitProfile(self->class_id)->attack[1].speed;
    }

    if ((self->pathtex = M_LoadPathTex(path_tex))) {
        /* Buildings: collide by footprint (their collisionSize is ~0). */
        if (self->runtime.flags & UNIT_BALANCE_BUILDING) {
            self->collision = get_unit_collision(self->pathtex);
        }
    }
    /* The client building-placement preview needs the gameplay collision radius,
     * not the selection-circle radius in s.radius, to paint live-unit blockers. */
    self->s.collision = self->collision;
    /* Resolve WC3's authored team-color precedence in the game module and
     * publish it through the generic entity effect bits consumed by MDX. */
    G_InitializeUnitTeamColor(self);
    G_InitializeUnitVertexColor(self);
    /* Establish the authored altitude immediately; MOVETYPE_STEP will refresh
     * the same support-surface calculation each simulation frame. */
    M_CheckGround(self);
    G_RegisterUnitSounds(self);
}

/* Walkable destructables are sparse, so keep a level list instead of scanning every map edict per unit tick. */
void G_RegisterGroundSurface(edict_t *ent) {
    if (!G_IsDestructable(ent) || !ent->data.DestructableData->walkable) return;
    G_UnregisterGroundSurface(ent);
    ent->ground_next = level.ground_surfaces;
    level.ground_surfaces = ent;
    if (!ent->destructable.dead && ent->destructable.placement_solid)
        ent->s.flags |= EF_GROUND_SURFACE;
}

void G_UnregisterGroundSurface(edict_t *ent) {
    edict_t * *link = &level.ground_surfaces;
    while (*link && *link != ent) link = &(*link)->ground_next;
    if (*link) *link = ent->ground_next;
    if (ent) {
        ent->ground_next = NULL;
        ent->s.flags &= ~EF_GROUND_SURFACE;
    }
}

void G_ClearGroundSurfaces(void) { level.ground_surfaces = NULL; }

bool M_CheckAttack(edict_t *self) {
    return false;
}

uint8_t compress_stat(edictStat_s const *stat) {
    if (stat->max_value <= 0) {
        return 0;
    } else {
        return 255 * stat->value / stat->max_value;
    }
}
