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

static entitySet_t scheduled_moves, sampled_moves;
#ifdef BZ_TESTS
static uint32_t move_owner_visits;
uint32_t M_TestMoveOwnerVisits(bool reset) {
    uint32_t count=move_owner_visits;
    if(reset)move_owner_visits=0;
    return count;
}
#endif

/* Membership belongs to the move transition, not periodic discovery. */
void M_TrackMove(edict_t const *ent) {
    uintptr_t index=((uintptr_t)ent-(uintptr_t)g_edicts)/sizeof(*ent);
    if(!g_edicts || index>=MAX_ENTITIES)return;
    umove_t const *move=ent->currentmove;
    entity_set_put(&scheduled_moves,index,ent->inuse && move && move->scheduled_think);
    entity_set_put(&sampled_moves,index,ent->inuse && move && move->sample_pose);
}

/* Raw transitions keep their existing animation/leave policy, but must update
 * derived owner membership just like ordinary unit_setmove transitions. */
void M_SetMove(edict_t *ent, umove_t *move) {
    if(ent->currentmove && (!move || ent->currentmove->proc!=move->proc))
        S_UnitAbilityMoveChanged(ent,move ? move->proc : NULL);
    ent->currentmove=move; M_TrackMove(ent);
}

void M_ResetMoveMembers(void) {
    scheduled_moves=(entitySet_t){0}; sampled_moves=(entitySet_t){0};
}

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
    if ((self->aiflags & AI_HOLD_FRAME) && self->construction) {
        G_UpdateConstructionAnimation(self);
        return;
    }
    if (self->aiflags & AI_HOLD_FRAME)
        return;
    /* JASS owns destructable sequences; do not restart them via the unit move clock. */
    if (G_IsDestructable(self) && self->animation_override)
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
                G_SetUnitAnimation(self, G_UnitAnimationRequest(self));
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
    if (self->currentmove->think && !(level.scheduled_frame && (self->currentmove->scheduled_think ||
            self->scheduled_think_frame == level.framenum))) {
        self->currentmove->think(self);
    }
}

/* Callback cadence is data on the owning move; the snapshot frame still owns animation. */
void M_RunScheduledThinks(void) {
    level.scheduled_think = true;
    S_BeginAbilityOwnerUpdates();
    for(uint32_t i=entity_set_next(&scheduled_moves,0);i<globals.num_edicts;i=entity_set_next(&scheduled_moves,i+1)) {
#ifdef BZ_TESTS
        move_owner_visits++;
#endif
        edict_t *self = g_edicts + i;
        if (!self->inuse || !G_UnitIsWorldActive(self) || self->paused || self->stunned ||
            !self->currentmove || !self->currentmove->scheduled_think) continue;
        self->scheduled_think_frame = level.framenum;
        SAFE_CALL(self->currentmove->think, self);
    }
    S_RunAbilityOwnerUpdates();
    level.scheduled_think = false;
}

void M_SamplePoses(void) {
    for(uint32_t i=entity_set_next(&sampled_moves,0);i<globals.num_edicts;i=entity_set_next(&sampled_moves,i+1)) {
#ifdef BZ_TESTS
        move_owner_visits++;
#endif
        edict_t *self = g_edicts + i;
        if (self->inuse && G_UnitIsWorldActive(self) && self->currentmove)
            SAFE_CALL(self->currentmove->sample_pose, self);
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
    return gi.FileExists(filename);
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

static bool M_SetUnitShadow(edict_t *self) {
    UnitUI_t const *ui = self->data.UnitUI;
    cstring_t unit_shadow = ui->unitShadowTexture;
    uint32_t shadow = G_LoadShadowTexture(unit_shadow, true);
    if (!shadow) {
        shadow = G_LoadShadowTexture("Shadow", true);
    }
    if (!shadow) {
        return false;
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
    return true;
}

static bool M_SetBuildingShadow(edict_t *self) {
    UnitUI_t const *ui = self->data.UnitUI;
    cstring_t building_shadow = ui->buildingShadowTexture;
    uint32_t shadow = G_LoadShadowTexture(building_shadow, false);
    if (!shadow) {
        if (G_HasShadowName(ui->unitShadowTexture)) {
            return M_SetUnitShadow(self);
        }
        return false;
    }

#ifndef USE_SHADOWMAPS
    self->s.shadow = shadow;
    self->s.shadow_rect = 0;
#endif
    return true;
}

static void unit_reset_sound_resources(void);
/* The standalone classification query is also used by diagnostic fixtures.
 * Construction uses its prepared definition's combat binding directly. */
#ifdef BZ_TESTS
static unitCombatTypes_t unit_combat_types[512];
#endif
#ifdef BZ_TESTS
static uint32_t unit_spawn_trait_builds, unit_projectile_resource_builds, unit_combat_type_builds;
#endif

void G_ResetUnitResources(void) {
    G_ResetUnitTypeBindings();
#ifdef BZ_TESTS
    memset(unit_combat_types, 0, sizeof(unit_combat_types));
#endif
    unit_reset_sound_resources();
}

/* Compile immutable classification once per bound type. Mutable unit traits
 * and ability callbacks still run in their original construction order. */
static unitSpawnTraits_t const *unit_spawn_type_traits_prepared(edict_t const *self, unitSpawnTraits_t *entry) {
    uint32_t metadata = G_UnitDataGeneration(), abilities = G_AbilityDataGeneration();
    if (entry->valid && entry->class_id == self->class_id && entry->metadata == metadata &&
        entry->abilities == abilities && entry->balance == self->data.UnitBalance &&
        entry->data == self->data.UnitData && entry->ui == self->data.UnitUI) return entry;
#ifdef BZ_TESTS
    unit_spawn_trait_builds++;
#endif
    uint32_t flags = 0;
    bool building = G_UnitIsBuilding(self->class_id);
    if (building) flags |= EF_BUILDING;
    if (S_UnitTypeIsGoldMine(self->class_id)) flags |= EF_RESOURCE_SOURCE;
    if (S_UnitTypeReturnsGold(self->class_id)) flags |= EF_RESOURCE_RETURN;
    if (S_UnitMovementType(self->data.UnitData) != UNIT_MOVE_FLOAT)
        flags |= EF_GROUND_CONFORM;
    float collision = G_UnitCollision(self->class_id);
    *entry = (unitSpawnTraits_t){ .class_id = self->class_id, .metadata = metadata, .abilities = abilities,
        .balance = self->data.UnitBalance, .data = self->data.UnitData, .ui = self->data.UnitUI,
        .flags = flags, .runtime = building ? UNIT_BALANCE_BUILDING : 0,
        .collision = collision > 0 ? collision : 0,
        .target = G_GetTargetType(self->data.UnitData->targetType), .valid = true };
    return entry;
}

static unitCombatTypes_t const *unit_spawn_combat_types_prepared(UnitBalance_t const *balance,
                                                      UnitWeapons_t const *weapons, unitCombatTypes_t *entry) {
    cstring_t names[5] = { balance->defenseType, weapons->attack1.attackType,
        weapons->attack1.weaponType, weapons->attack2.attackType, weapons->attack2.weaponType };
    uint32_t generation = G_UnitDataGeneration();
    if (entry->valid && entry->balance == balance && entry->weapons == weapons &&
        entry->generation == generation && !memcmp(entry->names, names, sizeof(names))) return entry;
#ifdef BZ_TESTS
    unit_combat_type_builds++;
#endif
    *entry = (unitCombatTypes_t){ .balance = balance, .weapons = weapons,
        .generation = generation, .valid = true,
        .defense = FindEnumValue(names[0], defense_type),
        .attack = { FindEnumValue(names[1], attack_type), FindEnumValue(names[3], attack_type) },
        .weapon = { FindEnumValue(names[2], weapon_type), FindEnumValue(names[4], weapon_type) } };
    memcpy(entry->names, names, sizeof(names));
    entry->defaults[0] = S_CompileAttackProfile(&weapons->attack1, 0, entry->attack[0], entry->weapon[0]);
    entry->defaults[1] = S_CompileAttackProfile(&weapons->attack2, 1, entry->attack[1], entry->weapon[1]);
    return entry;
}

/* Projectile media use the current class after initialization callbacks and
 * player upgrades. Cache only fields this phase actually assigns. */
static void unit_register_projectiles_prepared(edict_t *self, unitProjectileResources_t *entry) {
    unitAttack_t const *attacks[2] = { S_AttackProfileRead(self, 0), S_AttackProfileRead(self, 1) };
    uint32_t needed = 0;
    FOR_LOOP(i, 2)
        if (attacks[i]->weapon == WPN_MISSILE || attacks[i]->weapon == WPN_ARTILLERY) needed |= 1u << i;
    if (!needed) return;
    uint32_t metadata = G_UnitDataGeneration();
    uint64_t revision = gi.MediaRevision();
    if (!entry->valid || entry->class_id != self->class_id || entry->metadata != metadata ||
        entry->revision != revision) {
        *entry = (unitProjectileResources_t){ .class_id = self->class_id, .metadata = metadata,
            .origin = {G_UnitAttack1LaunchX(self->class_id), G_UnitAttack1LaunchY(self->class_id),
                       G_UnitAttack1LaunchZ(self->class_id)}, .valid = true };
    }
    FOR_LOOP(i, 2) {
        if (!(needed & (1u << i))) continue;
        if (!(entry->slots & (1u << i))) {
#ifdef BZ_TESTS
            unit_projectile_resource_builds++;
#endif
            UnitProfile_t const *profile = G_UnitProfile(self->class_id);
            entry->projectile[i].model = G_RegisterModel(profile->attack[i].art);
            entry->projectile[i].arc = profile->attack[i].arc;
            entry->projectile[i].speed = profile->attack[i].speed;
            entry->slots |= 1u << i;
        }
        if (self->attack_overrides[i]) {
            unitAttack_t *owned = S_AttackProfileWrite(self, i);
            owned->origin = entry->origin;
            owned->projectile.model = entry->projectile[i].model;
            owned->projectile.arc = entry->projectile[i].arc;
            owned->projectile.speed = entry->projectile[i].speed;
        } else {
            if (entry->base_profiles[i] != attacks[i]) {
                unitAttack_t value = *attacks[i];
                value.origin = entry->origin;
                value.projectile.model = entry->projectile[i].model;
                value.projectile.arc = entry->projectile[i].arc;
                value.projectile.speed = entry->projectile[i].speed;
                entry->base_profiles[i] = attacks[i];
                entry->complete_profiles[i] = S_InternAttackProfile(&value, i);
            }
            self->attack_profiles[i] = entry->complete_profiles[i];
        }
    }
    entry->revision = gi.MediaRevision();
}

static void unit_register_visuals_prepared(edict_t *self, unitVisualResources_t *cached) {
    uint64_t revision = gi.MediaRevision();
    uint32_t metadata = G_UnitDataGeneration();
    bool building = (self->runtime.flags & UNIT_BALANCE_BUILDING) != 0;
    if (cached->valid && cached->row == self->data.UnitUI && cached->class_id == self->class_id &&
        cached->revision == revision && cached->metadata == metadata && cached->building == building) {
        self->s.model = cached->model;
        self->s.splat = cached->splat;
#ifndef USE_SHADOWMAPS
        if (cached->shadow_set) {
            self->s.shadow = cached->shadow;
            self->s.shadow_rect = cached->shadow_rect;
        }
#endif
        return;
    }
    PATHSTR model_filename;
    G_NormalizeModelFilename(self->data.UnitUI->modelFile, model_filename, sizeof(model_filename));
    self->s.model = G_RegisterModel(model_filename);
    self->s.splat = M_LoadUberSplat(self->data.UnitUI->groundTexture);
    bool shadow_set = building ? M_SetBuildingShadow(self) : M_SetUnitShadow(self);
    *cached = (unitVisualResources_t){ .row = self->data.UnitUI, .class_id = self->class_id,
        .revision = gi.MediaRevision(), .metadata = metadata, .building = building,
        .model = self->s.model, .splat = self->s.splat, .valid = self->s.model != 0, .shadow_set = shadow_set
#ifndef USE_SHADOWMAPS
        , .shadow = self->s.shadow, .shadow_rect = self->s.shadow_rect
#endif
    };
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
    unitSoundProfile_t value = *G_UnitSoundProfile(self);
    self->sound_profile = &value;
    G_RegisterSoundVariants(value.select, &value.num_select, label, "What");
    G_SetUnitSoundProfile(self, &value);
}

/* Populate the unit's cached sound indices from UnitAckSounds.slk using the
 * "unitSound" label (e.g. "Footman").  Falls back gracefully if entries are
 * missing — sounds simply won't fire for that unit. */
static void G_RegisterUnitSounds(edict_t *self) {
    cstring_t label = self->data.UnitUI->soundLabel;
    if (!label || !label[0]) return;
    unitSoundProfile_t value = *G_UnitSoundProfile(self);
    /* Keep the partial registration visible during resource callbacks. The
     * stack value is frozen before return and never escapes this invocation. */
    self->sound_profile = &value;
    G_RegisterSoundVariants(value.select, &value.num_select, label, "What");
    /* Ordinary order and ready variants are cached per unit. YesAttack and
     * Pissed are selected from UnitAckSounds at the interaction that owns
     * them; they are not weapon-swing sounds. */
    G_RegisterSoundVariants(value.yes, &value.num_yes, label, "Yes");
    G_RegisterSoundVariants(value.ready, &value.num_ready, label, "Ready");
    /* Death sounds may be catalogued or shipped beside the unit model. */
    value.death = G_UnitAckSoundVariantIndex(label, "Death", 0);
    if (!value.death) {
        cstring_t model = self->data.UnitUI->modelFile;
        if (model && model[0]) {
            char path[512];
            cstring_t slash = strrchr(model, '\\');
            for (int numbered = 1; numbered >= 0; numbered--) {
                snprintf(path, sizeof(path), "%.*s%sDeath%s.wav",
                         slash ? (int)(slash - model + 1) : 0, model,
                         slash ? slash + 1 : model, numbered ? "1" : "");
                if (G_FileExists(path)) { value.death = gi.SoundIndex(path); break; }
            }
        }
    }
    /* Chop-wood impact sound from UnitCombatSounds: {weapType1}Wood (e.g. MetalLightChopWood). */
    cstring_t ws = self->data.UnitWeapons->attack1.weaponSound;
    if (ws && ws[0] && ws[0] != '_') {
        char key[128];
        snprintf(key, sizeof(key), "%sWood", ws);
        G_RegisterCombatVariants(value.chop, &value.num_chop, 3, key);
    }
    G_SetUnitSoundProfile(self, &value);
}

#ifdef BZ_TESTS
static uint32_t unit_sound_resource_builds;
#endif
static void unit_reset_sound_resources(void) {
#ifdef BZ_TESTS
    unit_sound_resource_builds = 0;
#endif
}
static void unit_register_sounds_prepared(edict_t *self, unitSoundResources_t *cached) {
    static unitSound_t const empty;
    bool fresh = (!self->sound_profile || self->sound_profile == &unit_sound_empty) &&
        memcmp(&self->sound, &empty, sizeof(empty)) == 0;
    uint64_t revision = gi.MediaRevision();
    uint32_t metadata = G_UnitDataGeneration(), catalog = G_SoundCatalogGeneration();
    if (fresh && cached->valid && cached->ui == self->data.UnitUI && cached->weapons == self->data.UnitWeapons &&
        cached->class_id == self->class_id && cached->metadata == metadata && cached->catalog == catalog &&
        cached->revision == revision) {
        self->sound_profile = cached->profile;
        return;
    }
#ifdef BZ_TESTS
    unit_sound_resource_builds++;
#endif
    G_RegisterUnitSounds(self);
    if (fresh) *cached = (unitSoundResources_t){ .ui = self->data.UnitUI, .weapons = self->data.UnitWeapons,
        .class_id = self->class_id, .metadata = metadata, .catalog = catalog,
        .revision = gi.MediaRevision(), .profile = G_UnitSoundProfile(self), .valid = true };
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
    if (!G_ActorHasAbilityCode(ent, MAKEFOURCC('A','l','o','c'))) return;
    ent->s.flags |= EF_NOT_SELECTABLE;
    ent->invulnerable = true;
    ent->collision = 0.0f;
    G_MarkMoveSpatialObject(ent);
    ent->no_pathing = true;
}

/* Initialize a unit entity from the unit data tables.
 * Reads model path, scale, collision radius, HP, mana, and attack parameters
 * (type, weapon class, damage dice, range, projectile model/speed) for the
 * unit's class_id and stores them in the edict. */
static unitRuntimeType_t *unit_construct_live_type(unitConstruction_t *construction) {
    unitRuntimeType_t *type = construction->type;
    if (type->rawcode == construction->unit->class_id && type->version == G_UnitDataGeneration() &&
        type->ability_version == G_AbilityDataGeneration()) return type;
    return G_UnitRuntimeType(construction->unit->class_id);
}

#ifdef BZ_TESTS
static unitSpawnTraits_t const *unit_spawn_type_traits(edict_t const *self) {
    return unit_spawn_type_traits_prepared(self, &G_UnitRuntimeType(self->class_id)->bindings.traits);
}
static unitCombatTypes_t const *unit_spawn_combat_types(UnitBalance_t const *balance, UnitWeapons_t const *weapons) {
    uintptr_t hash = ((uintptr_t)balance >> 4) ^ ((uintptr_t)weapons >> 4);
    return unit_spawn_combat_types_prepared(balance, weapons, unit_combat_types + ((hash ^ (hash >> 16)) & 511));
}
static void unit_register_visuals(edict_t *self) {
    unit_register_visuals_prepared(self, &G_UnitRuntimeType(self->class_id)->bindings.visuals);
}
static void unit_register_projectiles(edict_t *self) {
    unit_register_projectiles_prepared(self, &G_UnitRuntimeType(self->class_id)->bindings.projectiles);
}
static void unit_register_sounds(edict_t *self) {
    unit_register_sounds_prepared(self, &G_UnitRuntimeType(self->class_id)->bindings.sounds);
}
#endif

static void unit_construct_visuals(unitConstruction_t *construction) {
    edict_t *self = construction->unit;
    UnitUI_t const *ui = construction->captured.UnitUI;
    unitSpawnTraits_t const *traits = unit_spawn_type_traits_prepared(self, &construction->type->bindings.traits);
    self->runtime.flags = traits->runtime;
    self->s.flags |= traits->flags;
    unit_register_visuals_prepared(self, &construction->type->bindings.visuals);
    G_ResetUnitAnimationPropertiesPrepared(self, &construction->type->bindings.animation);
    self->s.scale = ui->modelScale;
    self->s.radius = ui->selectionScale * SEL_SCALE / 2;
    /* Unit-vs-unit separation uses the authentic collisionSize ('ucol') from
     * the unit data, matching WC3. Buildings have no meaningful collisionSize
     * and instead block via their pathing footprint (set from pathtex below). */
    self->collision = traits->collision;
    S_SetMoveFormationRank(self, construction->captured.UnitData->formationRank);
    S_SetMoveVisualPolicy(self, construction->captured.UnitData->orientationInterpolation);
//    printf("%.4s\n", &self->class_id);
    self->targtype = traits->target;
    G_CONSTRUCTION_TRACE(UNIT_CONSTRUCT_VISUALS, self, &construction->captured);
}

static void unit_construct_abilities(unitConstruction_t *construction) {
    edict_t *self = construction->unit;
    G_CONSTRUCTION_TRACE(UNIT_CONSTRUCT_ABILITIES_BEGIN, self, &construction->captured);
    if (construction->fresh) S_InitPreparedUnitAbilities(self, construction->type);
    else S_UnitAbilityEvent(self, A_UNIT_INIT);
    G_CONSTRUCTION_TRACE(UNIT_CONSTRUCT_ABILITIES_END, self, &construction->captured);
}

static void unit_construct_stats(unitConstruction_t *construction) {
    edict_t *self = construction->unit;
    UnitBalance_t const *b = construction->captured.UnitBalance;
    UnitData_t const *d = construction->captured.UnitData;
    UnitUI_t const *ui = construction->captured.UnitUI;
    UnitWeapons_t const *w = construction->captured.UnitWeapons;
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
    G_InitStockSlots(self);
    self->invulnerable = G_ActorHasAbilityCode(self, MAKEFOURCC('A','v','u','l'));
    G_ApplyUnitAbilityTraits(self);
    self->unitinfo.MoveSpeed = b->speed;
    self->hero_move_bonus = 0;
    self->unitinfo.PropWindow = DEG2RAD(d->propWin);
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
    G_CONSTRUCTION_TRACE(UNIT_CONSTRUCT_STATS, self, &construction->captured);
}

static void unit_construct_lifecycle(unitConstruction_t *construction) {
    edict_t *self = construction->unit;
    UnitData_t const *d = construction->captured.UnitData;
    self->think = monster_think;
    /* Blighted gold mines earn gold on an interval instead of via workers. */
    if (G_ActorHasAbilityCode(self, MAKEFOURCC('A','b','g','m'))) {
        self->think = blight_mine_think;
    }
    self->svflags |= SVF_MONSTER;
    /* Buildings use a single immobility contract so smart orders, combat, and
     * future movement paths cannot rotate or translate them independently. */
    if (self->runtime.flags & UNIT_BALANCE_BUILDING) self->aiflags |= AI_IMMOBILE;
    /* Cache the air/ground collision layer once. Flyers ('movetp' == "fly")
     * never collide with ground units and vice-versa. */
    if (S_UnitMovementType(d) == UNIT_MOVE_FLY)
        self->aiflags |= AI_FLYING;
    /* Neutral creeps sleep until a hero enters acquisition range; non-neutral
     * units (including camp defenders made hostile by script) start awake. */
    if (self->s.player < MAX_PLAYERS &&
        level.mapinfo->players[self->s.player].playerType == kPlayerTypeNeutral)
        self->aiflags |= AI_SLEEPING;

    G_CONSTRUCTION_TRACE(UNIT_CONSTRUCT_LIFECYCLE, self, &construction->captured);
}

static void unit_construct_combat(unitConstruction_t *construction) {
    edict_t *self = construction->unit;
    UnitBalance_t const *b = construction->captured.UnitBalance;
    UnitWeapons_t const *w = construction->captured.UnitWeapons;
    unitCombatTypes_t const *combat = unit_spawn_combat_types_prepared(b, w, &construction->type->bindings.combat);
    self->defense_type = combat->defense;
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
            self->hero.agi   = (uint32_t)MAX(0, (int64_t)baseAgi + self->hero_item_agility);
            self->hero.intel = (uint32_t)baseInt;
            /* war3mapUnits.doo stores Hero level but not unspent skill
             * points. Seed the level-derived point budget before the map
             * script applies its authored SelectHeroSkill calls. */
            G_HeroInitializeProgression(self);
        }
    }
    S_AttackApplyDefaults(self, 0, combat->defaults[0]);
    S_AttackApplyDefaults(self, 1, combat->defaults[1]);
    /* Heroes: fold the primary-attribute attack-damage bonus into runtime
     * attacks now that base attributes and both weapon slots are loaded. */
    G_RecomputeHeroStats(self);
    /* Completed player upgrades are persistent techtree state, not producer
     * buffs. New units inherit the owner's current levels at spawn. */
    G_ApplyPlayerUpgradesToUnit(self);
    G_CONSTRUCTION_TRACE(UNIT_CONSTRUCT_COMBAT, self, &construction->captured);
}

static void unit_construct_assets(unitConstruction_t *construction) {
    edict_t *self = construction->unit;
    S_CargoInitUnit(self);

    unit_register_projectiles_prepared(self, &unit_construct_live_type(construction)->bindings.projectiles);

    if ((self->pathtex = M_LoadPathTex(construction->path_texture))) {
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
    G_CONSTRUCTION_TRACE(UNIT_CONSTRUCT_ASSETS, self, &construction->captured);
}

static void unit_construct_support(unitConstruction_t *construction) {
    edict_t *self = construction->unit;
    M_CheckGround(self);
    G_CONSTRUCTION_TRACE(UNIT_CONSTRUCT_SUPPORT, self, &construction->captured);
}

static void unit_construct_sounds(unitConstruction_t *construction) {
    edict_t *self = construction->unit;
    unit_register_sounds_prepared(self, &unit_construct_live_type(construction)->bindings.sounds);
    G_CONSTRUCTION_TRACE(UNIT_CONSTRUCT_SOUNDS, self, &construction->captured);

}

static void unit_construct_autocast(unitConstruction_t *construction) {
    edict_t *self = construction->unit;
    /* `auto` / `udaa` is Warcraft's Default Active Ability rawcode.  Feed it
     * through the ordinary toggle path so ability policy, scheduler state,
     * and command-card presentation all start from the same state. */
    if (self->data.UnitAbilities && self->data.UnitAbilities->defaultActiveAbility)
        G_SetUnitAutocast(self, self->data.UnitAbilities->defaultActiveAbility, true);
    G_CONSTRUCTION_TRACE(UNIT_CONSTRUCT_AUTOCAST, self, &construction->captured);}

static void SpawnUnit(edict_t *self, bool fresh) {
    unitConstruction_t construction = {
        .unit = self, .captured = self->data, .type = G_UnitRuntimeType(self->class_id),
        .path_texture = self->data.UnitData->pathingTexture, .fresh = fresh
    };
    construction.stage = UNIT_CONSTRUCT_VISUALS;
    unit_construct_visuals(&construction);
    construction.stage = UNIT_CONSTRUCT_ABILITIES_END;
    unit_construct_abilities(&construction);
    construction.stage = UNIT_CONSTRUCT_STATS;
    unit_construct_stats(&construction);
    construction.stage = UNIT_CONSTRUCT_LIFECYCLE;
    unit_construct_lifecycle(&construction);
    construction.stage = UNIT_CONSTRUCT_COMBAT;
    unit_construct_combat(&construction);
    construction.stage = UNIT_CONSTRUCT_ASSETS;
    unit_construct_assets(&construction);
    construction.stage = UNIT_CONSTRUCT_SUPPORT;
    unit_construct_support(&construction);
    construction.stage = UNIT_CONSTRUCT_SOUNDS;
    unit_construct_sounds(&construction);
    construction.stage = UNIT_CONSTRUCT_AUTOCAST;
    unit_construct_autocast(&construction);
}

void SP_SpawnUnit(edict_t *self) { SpawnUnit(self, false); }
void SP_SpawnFreshUnit(edict_t *self) { SpawnUnit(self, true); }

/* Walkable destructables are sparse, so keep a level list instead of scanning every map edict per unit tick. */
void G_RegisterGroundSurface(edict_t *ent) {
    if (!G_IsDestructable(ent) || !ent->data.DestructableData->walkable) return;
    G_UnregisterGroundSurface(ent);
    ent->ground_next = level.ground_surfaces;
    level.ground_surfaces = ent;
    if (!ent->destructable->dead && ent->destructable->placement_solid)
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
